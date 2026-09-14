#include "PatchHistory.h"

#include <algorithm>

namespace signalpatch
{
PatchHistory::PatchHistory (PatchDocument& documentToTrack)
    : document (documentToTrack)
{
}

juce::String PatchHistory::nodeLabel (NodeId id) const
{
    if (const auto* node = document.findNode (id))
        return node->processor->getName();
    return "node";
}

bool PatchHistory::isContinuous (Kind kind) noexcept
{
    return kind == Kind::parameter || kind == Kind::modulationDepth || kind == Kind::move;
}

void PatchHistory::recordNodeAdded (NodeId id)
{
    const auto* node = document.findNode (id);
    if (node == nullptr || node->hardware)
        return;

    Entry entry;
    entry.kind = Kind::nodeExistence;
    entry.description = "add " + node->processor->getName();
    entry.node = id;
    entry.presentAfter = true;
    entry.model = *node;
    entry.preparedSampleRate = document.getSampleRate();
    entry.preparedMaximumBlockSize = document.getMaximumBlockSize();
    push (std::move (entry));
}

bool PatchHistory::removeNode (NodeId id)
{
    const auto* node = document.findNode (id);
    if (node == nullptr || node->hardware)
        return false;

    Entry entry;
    entry.kind = Kind::nodeExistence;
    entry.description = "delete " + node->processor->getName();
    entry.node = id;
    entry.presentAfter = false;
    entry.model = *node;
    entry.preparedSampleRate = document.getSampleRate();
    entry.preparedMaximumBlockSize = document.getMaximumBlockSize();
    for (const auto& connection : document.getConnections())
        if (connection.sourceNode == id || connection.destinationNode == id)
            entry.cables.push_back (connection);

    if (! document.removeNode (id))
        return false;

    push (std::move (entry));
    return true;
}

void PatchHistory::recordConnected (const Connection& connection)
{
    Entry entry;
    entry.kind = Kind::cable;
    entry.description = "connect " + nodeLabel (connection.sourceNode) + " to " + nodeLabel (connection.destinationNode);
    entry.presentAfter = true;
    entry.connection = connection;
    push (std::move (entry));
}

void PatchHistory::recordDisconnected (const Connection& connection)
{
    Entry entry;
    entry.kind = Kind::cable;
    entry.description = "remove cable " + nodeLabel (connection.sourceNode) + " to " + nodeLabel (connection.destinationNode);
    entry.presentAfter = false;
    entry.connection = connection;
    push (std::move (entry));
}

void PatchHistory::recordParameter (NodeId id, int parameterIndex, float before, float after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::parameter;
    entry.node = id;
    entry.parameterIndex = parameterIndex;
    entry.floatBefore = before;
    entry.floatAfter = after;
    if (const auto* node = document.findNode (id))
        entry.description = nodeLabel (id) + " " + node->processor->getParameter (parameterIndex).name.toLowerCase();
    push (std::move (entry));
}

void PatchHistory::recordModulationDepth (NodeId id, int parameterIndex, float before, float after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::modulationDepth;
    entry.node = id;
    entry.parameterIndex = parameterIndex;
    entry.floatBefore = before;
    entry.floatAfter = after;
    if (const auto* node = document.findNode (id))
        entry.description = nodeLabel (id) + " " + node->processor->getParameter (parameterIndex).name.toLowerCase() + " mod depth";
    push (std::move (entry));
}

void PatchHistory::recordMove (NodeId id, juce::Point<float> before, juce::Point<float> after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::move;
    entry.node = id;
    entry.description = "move " + nodeLabel (id);
    entry.pointBefore = before;
    entry.pointAfter = after;
    push (std::move (entry));
}

void PatchHistory::recordBypass (NodeId id, bool before, bool after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::bypass;
    entry.node = id;
    entry.description = juce::String (after ? "bypass " : "engage ") + nodeLabel (id);
    entry.boolBefore = before;
    entry.boolAfter = after;
    push (std::move (entry));
}

void PatchHistory::recordExtraState (NodeId id, juce::var before, juce::var after)
{
    Entry entry;
    entry.kind = Kind::extraState;
    entry.node = id;
    entry.description = "change " + nodeLabel (id);
    entry.varBefore = std::move (before);
    entry.varAfter = std::move (after);
    push (std::move (entry));
}

void PatchHistory::recordRename (NodeId id, const juce::String& before, const juce::String& after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::rename;
    entry.node = id;
    entry.description = "rename " + before;
    entry.varBefore = before;
    entry.varAfter = after;
    push (std::move (entry));
}

bool PatchHistory::tryCoalesce (const Entry& entry)
{
    if (! gestureOpen || undoStack.empty() || ! isContinuous (entry.kind))
        return false;

    auto& top = undoStack.back();
    if (top.kind != entry.kind || top.node != entry.node || top.parameterIndex != entry.parameterIndex)
        return false;
    if (entry.lastEditMs - top.lastEditMs > gestureWindowMs)
        return false;

    top.floatAfter = entry.floatAfter;
    top.pointAfter = entry.pointAfter;
    top.lastEditMs = entry.lastEditMs;
    return true;
}

void PatchHistory::push (Entry entry)
{
    entry.lastEditMs = juce::Time::currentTimeMillis();
    redoStack.clear();

    if (tryCoalesce (entry))
        return;

    gestureOpen = isContinuous (entry.kind);
    undoStack.push_back (std::move (entry));
    if (undoStack.size() > static_cast<size_t> (maximumEntries))
        undoStack.erase (undoStack.begin());
}

void PatchHistory::closeGesture() noexcept
{
    gestureOpen = false;
}

void PatchHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
    gestureOpen = false;
}

juce::String PatchHistory::getUndoDescription() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().description;
}

juce::String PatchHistory::getRedoDescription() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().description;
}

PatchHistory::Applied PatchHistory::apply (Entry& entry, bool forward)
{
    switch (entry.kind)
    {
        case Kind::nodeExistence:
        {
            const auto shouldExist = forward ? entry.presentAfter : ! entry.presentAfter;
            if (shouldExist)
            {
                if (! document.insertNode (entry.model, entry.preparedSampleRate, entry.preparedMaximumBlockSize))
                    return Applied::none;
                for (const auto& cable : entry.cables)
                    document.addConnection (cable);
            }
            else
            {
                // Capture the cables attached right now so redo of an add (or
                // undo of a delete that was later re-cabled) restores them.
                entry.cables.clear();
                for (const auto& connection : document.getConnections())
                    if (connection.sourceNode == entry.node || connection.destinationNode == entry.node)
                        entry.cables.push_back (connection);
                if (const auto* node = document.findNode (entry.node))
                    entry.model = *node;
                if (! document.removeNode (entry.node))
                    return Applied::none;
            }
            return Applied::structure;
        }
        case Kind::cable:
        {
            const auto shouldExist = forward ? entry.presentAfter : ! entry.presentAfter;
            if (shouldExist)
            {
                if (document.addConnection (entry.connection).failed())
                    return Applied::none;
            }
            else if (! document.removeConnection (entry.connection))
            {
                return Applied::none;
            }
            return Applied::structure;
        }
        case Kind::parameter:
        case Kind::modulationDepth:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr || ! juce::isPositiveAndBelow (entry.parameterIndex, node->processor->getNumParameters()))
                return Applied::none;
            auto& parameter = node->processor->getParameter (entry.parameterIndex);
            const auto value = forward ? entry.floatAfter : entry.floatBefore;
            if (entry.kind == Kind::parameter)
                parameter.setValue (value);
            else
                parameter.setModulationDepth (value);
            return Applied::values;
        }
        case Kind::move:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr)
                return Applied::none;
            node->position = forward ? entry.pointAfter : entry.pointBefore;
            return Applied::values;
        }
        case Kind::bypass:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr)
                return Applied::none;
            node->processor->setBypassed (forward ? entry.boolAfter : entry.boolBefore);
            return Applied::values;
        }
        case Kind::extraState:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr)
                return Applied::none;
            node->processor->setExtraState (forward ? entry.varAfter : entry.varBefore);
            return Applied::values;
        }
        case Kind::rename:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr)
                return Applied::none;
            node->processor->setName ((forward ? entry.varAfter : entry.varBefore).toString());
            return Applied::values;
        }
    }
    return Applied::none;
}

PatchHistory::Applied PatchHistory::undo()
{
    gestureOpen = false;
    while (! undoStack.empty())
    {
        auto entry = std::move (undoStack.back());
        undoStack.pop_back();
        const auto applied = apply (entry, false);
        if (applied == Applied::none)
            continue; // Target vanished (e.g. device relayout); skip the stale entry.
        redoStack.push_back (std::move (entry));
        return applied;
    }
    return Applied::none;
}

PatchHistory::Applied PatchHistory::redo()
{
    gestureOpen = false;
    while (! redoStack.empty())
    {
        auto entry = std::move (redoStack.back());
        redoStack.pop_back();
        const auto applied = apply (entry, true);
        if (applied == Applied::none)
            continue;
        undoStack.push_back (std::move (entry));
        return applied;
    }
    return Applied::none;
}
} // namespace signalpatch
