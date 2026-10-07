#pragma once

#include "Graph.h"

#include <utility>
#include <vector>

// Patch edits that treat the audio path as a chain of pedals, for screens
// where aiming a cable is hard (a 7-inch handheld, a gamepad): put a module
// after or before another one with the cables moved over, take one out and
// close the gap, move one along the chain. Every operation is one undo step.
//
// The operations go through an Editor so they run the same against the live
// engine (compiled, undoable) and against a bare document in the tests.
namespace signalpatch::chain
{
/** A node's first audio port and its stereo partner ("In L" / "In R"), -1 when absent. */
struct StereoPorts
{
    int left = -1, right = -1;
};
[[nodiscard]] StereoPorts audioInputs (const DspNode& node);
[[nodiscard]] StereoPorts audioOutputs (const DspNode& node);

class Editor
{
public:
    virtual ~Editor() = default;
    [[nodiscard]] virtual const PatchDocument& document() const = 0;
    virtual NodeId add (NodeKind kind, juce::Point<float> position) = 0;
    virtual bool remove (NodeId id) = 0;
    /** Fails (and leaves the document as it was) when the graph would not compile. */
    virtual juce::Result connect (Connection connection) = 0;
    virtual bool disconnect (const Connection& connection) = 0;
    virtual void move (NodeId id, juce::Point<float> position) = 0;
    /** Everything until endStep() is one undo step. */
    virtual void beginStep (const juce::String& description) = 0;
    virtual void endStep() = 0;
    /** Takes the last step back (an operation that failed half-way). */
    virtual void undoStep() = 0;
};

using Port = std::pair<NodeId, int>;

/** Where a module goes: the outputs that will feed it, the inputs it will feed,
    and the cables between them it replaces. */
struct Splice
{
    std::vector<Port> sourcesLeft, sourcesRight;
    std::vector<Port> destinationsLeft, destinationsRight;
    std::vector<Connection> replaced;
};

/** Right after node: everything its first audio output (and R partner) fed goes through the new module. */
[[nodiscard]] Splice after (const PatchDocument& document, NodeId node);
/** Right before node: everything that fed its first audio input (and R partner). */
[[nodiscard]] Splice before (const PatchDocument& document, NodeId node);
/** Into a cable, together with the other audio cables between the same two modules (the R side). */
[[nodiscard]] Splice onCable (const PatchDocument& document, const Connection& cable);
/** The end of the main chain: before the hardware output, after whatever feeds it. */
[[nodiscard]] Splice endOfChain (const PatchDocument& document);

/** Adds a module at position and splices it in, as one undo step. A source (audio out,
    no audio in: a synth, the drum machine) is mixed into the splice's destinations; an
    analyser (audio in, control out) listens to the splice's sources; a module with no
    audio at all is added without cables. `note` says which of those happened, or why
    nothing was added (then the result is 0 and the patch is unchanged). makeRoom > 0
    pushes what the new module feeds that far to the right, in the same undo step. */
NodeId insert (Editor& editor, NodeKind kind, juce::Point<float> position, const Splice& where, juce::String& note,
               float makeRoom = 0.0f);

/** Removes a module and connects what fed it to what it fed (bypass, made permanent). */
juce::Result removeAndBridge (Editor& editor, NodeId node);

/** Swaps a module with the one feeding it (direction -1) or the one it feeds (+1). */
juce::Result move (Editor& editor, NodeId node, int direction);

/** The one module feeding node's first audio input (-1) or fed by its first audio output (+1);
    0 when there is none or more than one. */
[[nodiscard]] NodeId neighbour (const PatchDocument& document, NodeId node, int direction);

/** Moves every module fed (by audio, directly or not) from node right by dx, if it sits at x >= minimumX. */
void shiftDownstream (Editor& editor, NodeId node, float minimumX, float dx);
} // namespace signalpatch::chain
