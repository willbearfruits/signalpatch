#pragma once

#include "Graph.h"

#include <vector>

namespace signalpatch
{
// Message-thread-only undo/redo over a PatchDocument.
//
// Entries are recorded after the document mutation has already happened (the
// engine only records edits that also compiled), so undo/redo never re-runs
// the original code path: each entry knows how to flip itself back and forth.
// Removed nodes keep their processor alive inside the entry, so undoing a
// delete brings back the same object — parameters, loaded NAM model and any
// recorded tape included — instead of a fresh instance.
//
// Continuous edits (knob drags, node drags) coalesce: another edit of the same
// target within the gesture window folds into the top entry, so one drag is
// one undo step.
class PatchHistory
{
public:
    enum class Applied
    {
        none,      // nothing to undo/redo
        values,    // parameters/position/bypass changed; no recompile needed
        structure  // nodes or cables changed; the graph must be recompiled
    };

    explicit PatchHistory (PatchDocument& documentToTrack);

    // Structural edits, recorded after the document already changed.
    void recordNodeAdded (NodeId id);
    void recordConnected (const Connection& connection);
    void recordDisconnected (const Connection& connection);

    // Removal is performed here because the node and its cables must be
    // captured before they disappear. Returns false if the node was not found.
    bool removeNode (NodeId id);

    // Continuous edits; before/after are the values around the change.
    void recordParameter (NodeId id, int parameterIndex, float before, float after);
    void recordModulationDepth (NodeId id, int parameterIndex, float before, float after);
    void recordMove (NodeId id, juce::Point<float> before, juce::Point<float> after);
    void recordBypass (NodeId id, bool before, bool after);
    void recordExtraState (NodeId id, juce::var before, juce::var after);
    void recordRename (NodeId id, const juce::String& before, const juce::String& after);

    Applied undo();
    Applied redo();
    [[nodiscard]] bool canUndo() const noexcept { return ! undoStack.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return ! redoStack.empty(); }
    [[nodiscard]] juce::String getUndoDescription() const;
    [[nodiscard]] juce::String getRedoDescription() const;
    [[nodiscard]] int getUndoCount() const noexcept { return static_cast<int> (undoStack.size()); }

    // Ends the current coalescing window; the next continuous edit starts a
    // fresh entry even if it targets the same control.
    void closeGesture() noexcept;

    void clear();

    static constexpr int maximumEntries = 200;
    static constexpr juce::int64 gestureWindowMs = 800;

private:
    enum class Kind
    {
        nodeExistence, // model present (after) or absent
        cable,         // connection present (after) or absent
        parameter,
        modulationDepth,
        move,
        bypass,
        extraState,
        rename
    };

    struct Entry
    {
        Kind kind = Kind::parameter;
        juce::String description;
        NodeId node = 0;
        int parameterIndex = -1;
        bool presentAfter = true;

        float floatBefore = 0.0f, floatAfter = 0.0f;
        juce::Point<float> pointBefore, pointAfter;
        bool boolBefore = false, boolAfter = false;
        juce::var varBefore, varAfter;

        NodeModel model;
        std::vector<Connection> cables;
        double preparedSampleRate = 0.0;
        int preparedMaximumBlockSize = 0;
        Connection connection;

        juce::int64 lastEditMs = 0;
    };

    void push (Entry entry);
    bool tryCoalesce (const Entry& entry);
    Applied apply (Entry& entry, bool forward);
    [[nodiscard]] juce::String nodeLabel (NodeId id) const;
    [[nodiscard]] static bool isContinuous (Kind kind) noexcept;

    PatchDocument& document;
    std::vector<Entry> undoStack;
    std::vector<Entry> redoStack;
    bool gestureOpen = false;
};
} // namespace signalpatch
