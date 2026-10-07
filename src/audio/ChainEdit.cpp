#include "ChainEdit.h"

#include <algorithm>
#include <unordered_set>

namespace signalpatch::chain
{
namespace
{
    // "In L" -> "In R", "L" -> "R": the right half of a stereo pair, -1 for a mono port.
    int rightPartner (const DspNode& node, int index, bool output)
    {
        const auto& name = output ? node.getOutputPort (index).name : node.getInputPort (index).name;
        if (! name.endsWith (" L") && name != "L")
            return -1;
        const auto wanted = name == "L" ? juce::String ("R") : name.dropLastCharacters (1) + "R";
        const auto count = output ? node.getNumOutputPorts() : node.getNumInputPorts();
        for (int i = 0; i < count; ++i)
        {
            const auto& port = output ? node.getOutputPort (i) : node.getInputPort (i);
            if (port.name == wanted && port.type == SignalType::audio)
                return i;
        }
        return -1;
    }

    bool isAudioCable (const PatchDocument& document, const Connection& cable)
    {
        const auto* source = document.findNode (cable.sourceNode);
        const auto* destination = document.findNode (cable.destinationNode);
        return source != nullptr && destination != nullptr
            && juce::isPositiveAndBelow (cable.sourcePort, source->processor->getNumOutputPorts())
            && juce::isPositiveAndBelow (cable.destinationPort, destination->processor->getNumInputPorts())
            && source->processor->getOutputPort (cable.sourcePort).type == SignalType::audio
            && destination->processor->getInputPort (cable.destinationPort).type == SignalType::audio;
    }

    void addUnique (std::vector<Port>& ports, Port port)
    {
        if (std::find (ports.begin(), ports.end(), port) == ports.end())
            ports.push_back (port);
    }

    // One mono output into two inputs of the same module (a mono chain into both
    // hardware outputs): the higher input becomes the right side, so a stereo
    // module put in there plays in stereo and a mono one feeds both as before.
    void splitMonoPair (Splice& splice)
    {
        if (! splice.destinationsRight.empty())
            return;
        auto& left = splice.destinationsLeft;
        for (std::size_t i = 0; i < left.size(); ++i)
            for (std::size_t j = i + 1; j < left.size(); ++j)
                if (left[i].first == left[j].first)
                {
                    const auto second = left[j].second > left[i].second ? j : i;
                    splice.destinationsRight.push_back (left[second]);
                    left.erase (left.begin() + static_cast<std::ptrdiff_t> (second));
                    return;
                }
    }

    bool exists (const PatchDocument& document, const Connection& cable)
    {
        const auto& cables = document.getConnections();
        return std::find (cables.begin(), cables.end(), cable) != cables.end();
    }

    // Connects every source to every destination, collecting what failed.
    void wire (Editor& editor, const std::vector<Port>& sources, const std::vector<Port>& destinations, juce::StringArray& problems)
    {
        for (const auto& source : sources)
            for (const auto& destination : destinations)
            {
                const Connection cable { source.first, source.second, destination.first, destination.second };
                if (source.first == destination.first || exists (editor.document(), cable))
                    continue;
                const auto result = editor.connect (cable);
                if (result.failed())
                    problems.addIfNotAlreadyThere (result.getErrorMessage());
            }
    }

    // Puts a module that has no audio cables of its own into the splice.
    void spliceModule (Editor& editor, NodeId module, const Splice& where, juce::StringArray& problems)
    {
        const auto* model = editor.document().findNode (module);
        if (model == nullptr)
        {
            problems.add ("That module is gone.");
            return;
        }
        const auto ins = audioInputs (*model->processor);
        const auto outs = audioOutputs (*model->processor);
        for (const auto& cable : where.replaced)
            editor.disconnect (cable);
        if (ins.left >= 0)
        {
            wire (editor, where.sourcesLeft, { { module, ins.left } }, problems);
            if (ins.right >= 0) // a mono chain into a stereo module feeds both of its inputs
                wire (editor, where.sourcesRight.empty() ? where.sourcesLeft : where.sourcesRight, { { module, ins.right } }, problems);
        }
        if (outs.left >= 0)
        {
            wire (editor, { { module, outs.left } }, where.destinationsLeft, problems);
            wire (editor, { { module, outs.right >= 0 ? outs.right : outs.left } }, where.destinationsRight, problems);
        }
    }

    // What fed `in` goes straight to what `out` fed (the module between them is leaving).
    void bridge (Editor& editor, const Splice& in, const Splice& out, juce::StringArray& problems)
    {
        wire (editor, in.sourcesLeft, out.destinationsLeft, problems);
        wire (editor, in.sourcesRight.empty() ? in.sourcesLeft : in.sourcesRight, out.destinationsRight, problems);
    }
} // namespace

StereoPorts audioInputs (const DspNode& node)
{
    StereoPorts ports;
    for (int i = 0; i < node.getNumInputPorts() && ports.left < 0; ++i)
        if (node.getInputPort (i).type == SignalType::audio)
            ports.left = i;
    if (ports.left >= 0)
        ports.right = rightPartner (node, ports.left, false);
    // The hardware output's first two channels are the stereo pair.
    if (node.getKind() == NodeKind::hardwareOutput && ports.left == 0 && ports.right < 0 && node.getNumInputPorts() > 1)
        ports.right = 1;
    return ports;
}

StereoPorts audioOutputs (const DspNode& node)
{
    StereoPorts ports;
    for (int i = 0; i < node.getNumOutputPorts() && ports.left < 0; ++i)
        if (node.getOutputPort (i).type == SignalType::audio)
            ports.left = i;
    if (ports.left >= 0)
        ports.right = rightPartner (node, ports.left, true);
    return ports;
}

Splice after (const PatchDocument& document, NodeId node)
{
    Splice splice;
    const auto* model = document.findNode (node);
    if (model == nullptr)
        return splice;
    const auto outs = audioOutputs (*model->processor);
    if (outs.left < 0)
        return splice;
    splice.sourcesLeft.push_back ({ node, outs.left });
    if (outs.right >= 0)
        splice.sourcesRight.push_back ({ node, outs.right });
    for (const auto& cable : document.getConnections())
    {
        if (cable.sourceNode != node || ! isAudioCable (document, cable))
            continue;
        if (cable.sourcePort == outs.left)
            addUnique (splice.destinationsLeft, { cable.destinationNode, cable.destinationPort });
        else if (cable.sourcePort == outs.right)
            addUnique (splice.destinationsRight, { cable.destinationNode, cable.destinationPort });
        else
            continue;
        splice.replaced.push_back (cable);
    }
    splitMonoPair (splice);
    return splice;
}

Splice before (const PatchDocument& document, NodeId node)
{
    Splice splice;
    const auto* model = document.findNode (node);
    if (model == nullptr)
        return splice;
    const auto ins = audioInputs (*model->processor);
    if (ins.left < 0)
        return splice;
    splice.destinationsLeft.push_back ({ node, ins.left });
    if (ins.right >= 0)
        splice.destinationsRight.push_back ({ node, ins.right });
    for (const auto& cable : document.getConnections())
    {
        if (cable.destinationNode != node || ! isAudioCable (document, cable))
            continue;
        if (cable.destinationPort == ins.left)
            addUnique (splice.sourcesLeft, { cable.sourceNode, cable.sourcePort });
        else if (cable.destinationPort == ins.right)
            addUnique (splice.sourcesRight, { cable.sourceNode, cable.sourcePort });
        else
            continue;
        splice.replaced.push_back (cable);
    }
    // The same mono source on both sides is one source.
    if (splice.sourcesRight == splice.sourcesLeft)
        splice.sourcesRight.clear();
    return splice;
}

Splice onCable (const PatchDocument& document, const Connection& cable)
{
    Splice splice;
    const auto* source = document.findNode (cable.sourceNode);
    if (source == nullptr || ! isAudioCable (document, cable))
        return splice;
    const auto outs = audioOutputs (*source->processor);
    auto leftOut = cable.sourcePort, rightOut = -1;
    if (cable.sourcePort == outs.left)
        rightOut = outs.right;
    else if (cable.sourcePort == outs.right)
    {
        leftOut = outs.left;
        rightOut = outs.right;
    }
    splice.sourcesLeft.push_back ({ cable.sourceNode, leftOut });
    if (rightOut >= 0)
        splice.sourcesRight.push_back ({ cable.sourceNode, rightOut });
    for (const auto& other : document.getConnections())
    {
        if (other.sourceNode != cable.sourceNode || other.destinationNode != cable.destinationNode || ! isAudioCable (document, other))
            continue;
        if (other.sourcePort == leftOut)
            addUnique (splice.destinationsLeft, { other.destinationNode, other.destinationPort });
        else if (other.sourcePort == rightOut)
            addUnique (splice.destinationsRight, { other.destinationNode, other.destinationPort });
        else
            continue;
        splice.replaced.push_back (other);
    }
    splitMonoPair (splice);
    return splice;
}

Splice endOfChain (const PatchDocument& document)
{
    std::vector<NodeId> feeders;
    const Connection* firstCable = nullptr;
    for (const auto& cable : document.getConnections())
        if (cable.destinationNode == PatchDocument::hardwareOutputId && isAudioCable (document, cable))
        {
            if (firstCable == nullptr)
                firstCable = &cable;
            if (std::find (feeders.begin(), feeders.end(), cable.sourceNode) == feeders.end())
                feeders.push_back (cable.sourceNode);
        }
    if (feeders.size() == 1)
        return onCable (document, *firstCable);
    if (feeders.size() > 1)
        return before (document, PatchDocument::hardwareOutputId);
    // Nothing reaches the outputs yet: from the first input to the first two outputs.
    auto splice = before (document, PatchDocument::hardwareOutputId);
    if (const auto* input = document.findNode (PatchDocument::hardwareInputId))
        if (audioOutputs (*input->processor).left >= 0)
            splice.sourcesLeft.push_back ({ PatchDocument::hardwareInputId, audioOutputs (*input->processor).left });
    return splice;
}

NodeId insert (Editor& editor, NodeKind kind, juce::Point<float> position, const Splice& where, juce::String& note, float makeRoom)
{
    note.clear();
    editor.beginStep ("Add " + nodeKindName (kind));
    const auto id = editor.add (kind, position);
    if (id == 0)
    {
        editor.endStep();
        note = "Could not add " + nodeKindName (kind);
        return 0;
    }
    const auto* model = editor.document().findNode (id);
    const auto ins = audioInputs (*model->processor);
    const auto outs = audioOutputs (*model->processor);
    juce::StringArray problems;
    if (outs.left < 0 && ins.left >= 0)
    {
        // An analyser (envelope, spectral follower) listens to the chain where it is put.
        wire (editor, where.sourcesLeft, { { id, ins.left } }, problems);
        note = model->processor->getName() + " listens to the chain here";
    }
    else if (outs.left < 0)
        note = model->processor->getName() + " has no audio output: added without cables";
    else if (ins.left < 0)
    {
        // A source joins the chain where it is put: its output sums into the same inputs.
        Splice mixIn = where;
        mixIn.replaced.clear();
        mixIn.sourcesLeft.clear();
        mixIn.sourcesRight.clear();
        spliceModule (editor, id, mixIn, problems);
        note = model->processor->getName() + " mixed into the chain";
    }
    else
        spliceModule (editor, id, where, problems);
    if (problems.isEmpty() && makeRoom > 0.0f)
        shiftDownstream (editor, id, position.x, makeRoom);
    editor.endStep();
    if (! problems.isEmpty())
    {
        editor.undoStep();
        note = problems.joinIntoString ("; ");
        return 0;
    }
    return id;
}

juce::Result removeAndBridge (Editor& editor, NodeId node)
{
    const auto& document = editor.document();
    const auto* model = document.findNode (node);
    if (model == nullptr)
        return juce::Result::fail ("That module is gone.");
    if (model->hardware)
        return juce::Result::fail ("The hardware inputs and outputs stay.");
    const auto in = before (document, node);
    const auto out = after (document, node);
    editor.beginStep ("Remove " + model->processor->getName());
    editor.remove (node);
    juce::StringArray problems;
    bridge (editor, in, out, problems);
    editor.endStep();
    if (! problems.isEmpty())
    {
        editor.undoStep();
        return juce::Result::fail (problems.joinIntoString ("; "));
    }
    return juce::Result::ok();
}

NodeId neighbour (const PatchDocument& document, NodeId node, int direction)
{
    const auto splice = direction < 0 ? before (document, node) : after (document, node);
    const auto& ports = direction < 0 ? splice.sourcesLeft : splice.destinationsLeft;
    NodeId found = 0;
    for (const auto& port : ports)
    {
        if (found != 0 && port.first != found)
            return 0;
        found = port.first;
    }
    return found;
}

juce::Result move (Editor& editor, NodeId node, int direction)
{
    const auto& document = editor.document();
    const auto* model = document.findNode (node);
    if (model == nullptr || model->hardware)
        return juce::Result::fail ("Only modules move along the chain.");
    const auto ins = audioInputs (*model->processor);
    const auto outs = audioOutputs (*model->processor);
    if (ins.left < 0 || outs.left < 0)
        return juce::Result::fail (model->processor->getName() + " is not in the audio chain.");
    const auto other = neighbour (document, node, direction);
    if (other == 0)
        return juce::Result::fail (direction < 0 ? "More than one module feeds it (or none)." : "It feeds more than one module (or none).");
    if (other == PatchDocument::hardwareInputId || other == PatchDocument::hardwareOutputId)
        return juce::Result::fail (direction < 0 ? "Already first in the chain." : "Already last in the chain.");
    const auto* otherModel = document.findNode (other);
    if (otherModel == nullptr || audioInputs (*otherModel->processor).left < 0 || audioOutputs (*otherModel->processor).left < 0)
        return juce::Result::fail ("The module next to it is not in the audio chain.");
    // Copies: the document's vectors change below.
    const auto name = model->processor->getName();
    const auto here = model->position, there = otherModel->position;

    editor.beginStep ("Move " + name + (direction < 0 ? " earlier" : " later"));
    juce::StringArray problems;
    {
        // Out of the chain, gap closed...
        const auto in = before (document, node);
        const auto out = after (document, node);
        for (const auto& cable : in.replaced)
            editor.disconnect (cable);
        for (const auto& cable : out.replaced)
            editor.disconnect (cable);
        bridge (editor, in, out, problems);
    }
    // ...and back in on the other side of its neighbour.
    if (problems.isEmpty())
        spliceModule (editor, node, direction < 0 ? before (document, other) : after (document, other), problems);
    editor.move (node, there);
    editor.move (other, here);
    editor.endStep();
    if (! problems.isEmpty())
    {
        editor.undoStep();
        return juce::Result::fail (problems.joinIntoString ("; "));
    }
    return juce::Result::ok();
}

void shiftDownstream (Editor& editor, NodeId node, float minimumX, float dx)
{
    const auto& document = editor.document();
    std::unordered_set<NodeId> reached;
    std::vector<NodeId> frontier { node };
    while (! frontier.empty())
    {
        const auto current = frontier.back();
        frontier.pop_back();
        for (const auto& cable : document.getConnections())
            if (cable.sourceNode == current && cable.destinationNode != node && isAudioCable (document, cable)
                && reached.insert (cable.destinationNode).second)
                frontier.push_back (cable.destinationNode);
    }
    std::vector<std::pair<NodeId, juce::Point<float>>> moves;
    for (const auto& model : document.getNodes())
        if (reached.count (model.id) > 0 && model.position.x >= minimumX)
            moves.emplace_back (model.id, model.position + juce::Point<float> (dx, 0.0f));
    for (const auto& [id, position] : moves)
        editor.move (id, position);
}
} // namespace signalpatch::chain
