#include "PatchHistory.h"
#include "MidiMap.h"

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
    return kind == Kind::parameter || kind == Kind::modulationDepth || kind == Kind::parameterShape
        || kind == Kind::move || kind == Kind::boardMove;
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
    entry.groupsBeforeRemoval = document.groupsToJson();
    entry.midiBeforeRemoval = midiMappingsToJson (document.getMidiMappings());

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

void PatchHistory::recordParameterShape (NodeId id, int parameterIndex, ParameterShape before, float valueBefore,
                                         ParameterShape after, float valueAfter)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::parameterShape;
    entry.node = id;
    entry.parameterIndex = parameterIndex;
    entry.shapeBefore = before;
    entry.shapeAfter = after;
    entry.floatBefore = valueBefore;
    entry.floatAfter = valueAfter;
    if (const auto* node = document.findNode (id))
        entry.description = nodeLabel (id) + " " + node->processor->getParameter (parameterIndex).name.toLowerCase() + " range";
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

void PatchHistory::recordBoardMove (NodeId id, std::optional<juce::Point<float>> before, std::optional<juce::Point<float>> after)
{
    if (before == after)
        return;
    Entry entry;
    entry.kind = Kind::boardMove;
    entry.node = id;
    entry.description = "move " + nodeLabel (id) + " on the board";
    entry.hadPointBefore = before.has_value();
    entry.hasPointAfter = after.has_value();
    entry.pointBefore = before.value_or (juce::Point<float>());
    entry.pointAfter = after.value_or (juce::Point<float>());
    push (std::move (entry));
}

void PatchHistory::recordGroups (juce::var before, juce::var after)
{
    Entry entry;
    entry.kind = Kind::groups;
    entry.description = "change pedal groups";
    entry.varBefore = std::move (before);
    entry.varAfter = std::move (after);
    push (std::move (entry));
}

void PatchHistory::recordMidi (juce::var before, juce::var after)
{
    Entry entry;
    entry.kind = Kind::midi;
    entry.description = "change MIDI mapping";
    entry.varBefore = std::move (before);
    entry.varAfter = std::move (after);
    push (std::move (entry));
}

bool PatchHistory::tryCoalesce (const Entry& entry)
{
    if (! gestureOpen || undoStack.empty() || ! isContinuous (entry.kind))
        return false;

    // Inside a compound gesture the targets interleave (every module of a
    // multi-drag moves on every motion event), so look through the whole
    // compound for this target rather than only at the top entry.
    for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it)
    {
        auto& top = *it;
        if (compoundGesture != 0 && top.gestureId != compoundGesture)
            return false;
        if (top.kind != entry.kind || top.node != entry.node || top.parameterIndex != entry.parameterIndex)
        {
            if (compoundGesture == 0)
                return false;
            continue;
        }
        if (compoundGesture == 0 && entry.lastEditMs - top.lastEditMs > gestureWindowMs)
            return false;
        top.floatAfter = entry.floatAfter;
        top.shapeAfter = entry.shapeAfter;
        top.pointAfter = entry.pointAfter;
        top.hasPointAfter = entry.hasPointAfter;
        top.lastEditMs = entry.lastEditMs;
        return true;
    }
    return false;
}

void PatchHistory::beginCompoundGesture (juce::String description)
{
    compoundGesture = nextGestureId++;
    compoundDescription = std::move (description);
    gestureOpen = true;
}

void PatchHistory::push (Entry entry)
{
    entry.lastEditMs = juce::Time::currentTimeMillis();
    redoStack.clear();

    if (tryCoalesce (entry))
        return;

    if (compoundGesture != 0)
    {
        entry.gestureId = compoundGesture;
        entry.description = compoundDescription;
        gestureOpen = true;
    }
    else
        gestureOpen = isContinuous (entry.kind);
    undoStack.push_back (std::move (entry));
    if (undoStack.size() > static_cast<size_t> (maximumEntries))
        undoStack.erase (undoStack.begin());
}

void PatchHistory::closeGesture() noexcept
{
    gestureOpen = false;
    compoundGesture = 0;
}

void PatchHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
    gestureOpen = false;
    compoundGesture = 0;
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
                // Undo is stack-ordered, so the document is back at the moment right
                // after the removal: the snapshot from just before it is exact.
                if (! entry.groupsBeforeRemoval.isVoid())
                    document.groupsFromJson (entry.groupsBeforeRemoval);
                if (! entry.midiBeforeRemoval.isVoid())
                    document.setMidiMappings (midiMappingsFromJson (entry.midiBeforeRemoval));
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
                entry.groupsBeforeRemoval = document.groupsToJson();
                entry.midiBeforeRemoval = midiMappingsToJson (document.getMidiMappings());
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
        case Kind::parameterShape:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr || ! juce::isPositiveAndBelow (entry.parameterIndex, node->processor->getNumParameters()))
                return Applied::none;
            auto& parameter = node->processor->getParameter (entry.parameterIndex);
            parameter.setShape (forward ? entry.shapeAfter : entry.shapeBefore);
            parameter.setValue (forward ? entry.floatAfter : entry.floatBefore);
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
        case Kind::boardMove:
        {
            auto* node = document.findNode (entry.node);
            if (node == nullptr)
                return Applied::none;
            const bool has = forward ? entry.hasPointAfter : entry.hadPointBefore;
            if (has)
                node->boardPosition = forward ? entry.pointAfter : entry.pointBefore;
            else
                node->boardPosition.reset();
            return Applied::values;
        }
        case Kind::groups:
            document.groupsFromJson (forward ? entry.varAfter : entry.varBefore);
            return Applied::values;
        case Kind::midi:
            document.setMidiMappings (midiMappingsFromJson (forward ? entry.varAfter : entry.varBefore));
            return Applied::values;
    }
    return Applied::none;
}

PatchHistory::Applied PatchHistory::undo()
{
    closeGesture();
    auto result = Applied::none;
    while (! undoStack.empty())
    {
        auto entry = std::move (undoStack.back());
        undoStack.pop_back();
        const auto gesture = entry.gestureId;
        const auto applied = apply (entry, false);
        if (applied != Applied::none)
        {
            redoStack.push_back (std::move (entry));
            result = juce::jmax (result, applied);
        }
        // Stale entries (target vanished after a device relayout) are skipped;
        // a compound gesture keeps going while the next entry shares its id.
        const bool more = gesture != 0 && ! undoStack.empty() && undoStack.back().gestureId == gesture;
        if (result != Applied::none && ! more)
            return result;
    }
    return result;
}

PatchHistory::Applied PatchHistory::redo()
{
    closeGesture();
    auto result = Applied::none;
    while (! redoStack.empty())
    {
        auto entry = std::move (redoStack.back());
        redoStack.pop_back();
        const auto gesture = entry.gestureId;
        const auto applied = apply (entry, true);
        if (applied != Applied::none)
        {
            undoStack.push_back (std::move (entry));
            result = juce::jmax (result, applied);
        }
        const bool more = gesture != 0 && ! redoStack.empty() && redoStack.back().gestureId == gesture;
        if (result != Applied::none && ! more)
            return result;
    }
    return result;
}
} // namespace signalpatch
