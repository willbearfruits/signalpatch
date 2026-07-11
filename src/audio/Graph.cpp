#include "Graph.h"

#include "Processors.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace signalpatch
{
namespace
{
constexpr float outputSafetyCeiling = 0.98f;

int firstAudioInputPort (const DspNode& node)
{
    for (int i = 0; i < node.getNumInputPorts(); ++i)
        if (node.getInputPort (i).type == SignalType::audio)
            return i;

    return -1;
}

int firstAudioOutputPort (const DspNode& node)
{
    for (int i = 0; i < node.getNumOutputPorts(); ++i)
        if (node.getOutputPort (i).type == SignalType::audio)
            return i;

    return -1;
}
} // namespace

juce::String nodeKindName (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::hardwareInput:       return "Hardware Inputs";
        case NodeKind::hardwareOutput:      return "Hardware Outputs";
        case NodeKind::gain:                return "Gain";
        case NodeKind::mixer:               return "4-Channel Mixer";
        case NodeKind::crossfade:           return "Crossfade";
        case NodeKind::distortion:          return "Distortion";
        case NodeKind::filter:              return "Filter";
        case NodeKind::delay:               return "Delay";
        case NodeKind::reverb:              return "Reverb";
        case NodeKind::chorus:              return "Chorus";
        case NodeKind::phaser:              return "Phaser";
        case NodeKind::tremolo:             return "Tremolo";
        case NodeKind::bitcrusher:          return "Bitcrusher";
        case NodeKind::ringMod:             return "Ring Modulator";
        case NodeKind::vowelFilter:         return "Vowel Filter";
        case NodeKind::pitchShifter:        return "Pitch Shifter";
        case NodeKind::vocoder:             return "Vocoder";
        case NodeKind::pitchCorrector:      return "Autotune";
        case NodeKind::granular:            return "Granular";
        case NodeKind::compressor:          return "Compressor";
        case NodeKind::limiter:             return "Limiter";
        case NodeKind::gate:                return "Noise Gate";
        case NodeKind::feedbackGuard:       return "Feedback Guard";
        case NodeKind::monoSynth:           return "Mono Synth";
        case NodeKind::noiseSource:         return "Noise";
        case NodeKind::pluck:               return "Pluck";
        case NodeKind::drumMachine:         return "Drum Machine";
        case NodeKind::sampler:             return "Sampler";
        case NodeKind::fourTrack:           return "4-Track";
        case NodeKind::lfo:                 return "LFO";
        case NodeKind::randomLfo:           return "Random";
        case NodeKind::envelopeFollower:    return "Envelope Follower";
        case NodeKind::stepSequencer:       return "8-Step Sequencer";
        case NodeKind::macro:               return "Macro";
        case NodeKind::spectralFollower:    return "Spectral Follower";
        case NodeKind::script:              return "Script";
        case NodeKind::neuralAmpPlaceholder:return "Neural Amp";
    }

    return "Unknown";
}

juce::String nodeKindKey (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::hardwareInput:        return "hardware-input";
        case NodeKind::hardwareOutput:       return "hardware-output";
        case NodeKind::gain:                 return "gain";
        case NodeKind::mixer:                return "mixer-4";
        case NodeKind::crossfade:            return "crossfade";
        case NodeKind::distortion:           return "distortion";
        case NodeKind::filter:               return "filter-svf";
        case NodeKind::delay:                return "delay";
        case NodeKind::reverb:               return "reverb";
        case NodeKind::chorus:               return "chorus";
        case NodeKind::phaser:               return "phaser";
        case NodeKind::tremolo:              return "tremolo";
        case NodeKind::bitcrusher:           return "bitcrusher";
        case NodeKind::ringMod:              return "ring-mod";
        case NodeKind::vowelFilter:          return "vowel-filter";
        case NodeKind::pitchShifter:         return "pitch-shifter";
        case NodeKind::vocoder:              return "vocoder-12";
        case NodeKind::pitchCorrector:       return "pitch-corrector";
        case NodeKind::granular:             return "granular-cloud";
        case NodeKind::compressor:           return "compressor";
        case NodeKind::limiter:              return "limiter";
        case NodeKind::gate:                 return "noise-gate";
        case NodeKind::feedbackGuard:        return "feedback-guard";
        case NodeKind::monoSynth:            return "mono-synth";
        case NodeKind::noiseSource:          return "noise-source";
        case NodeKind::pluck:                return "karplus-pluck";
        case NodeKind::drumMachine:          return "drum-machine-8";
        case NodeKind::sampler:              return "sampler-live";
        case NodeKind::fourTrack:            return "four-track";
        case NodeKind::lfo:                  return "lfo";
        case NodeKind::randomLfo:            return "random-lfo";
        case NodeKind::envelopeFollower:     return "envelope-follower";
        case NodeKind::stepSequencer:        return "step-sequencer-8";
        case NodeKind::macro:                return "macro";
        case NodeKind::spectralFollower:     return "spectral-follower";
        case NodeKind::script:               return "script-expr";
        case NodeKind::neuralAmpPlaceholder: return "neural-amp";
    }

    return "unknown";
}

std::optional<NodeKind> nodeKindFromKey (const juce::String& key)
{
    for (const auto kind : { NodeKind::hardwareInput, NodeKind::hardwareOutput, NodeKind::gain,
                             NodeKind::mixer, NodeKind::crossfade, NodeKind::distortion,
                             NodeKind::filter, NodeKind::delay, NodeKind::reverb,
                             NodeKind::chorus, NodeKind::phaser, NodeKind::tremolo,
                             NodeKind::bitcrusher, NodeKind::ringMod, NodeKind::vowelFilter,
                             NodeKind::pitchShifter, NodeKind::vocoder, NodeKind::pitchCorrector,
                             NodeKind::granular, NodeKind::compressor, NodeKind::limiter,
                             NodeKind::gate, NodeKind::feedbackGuard, NodeKind::monoSynth,
                             NodeKind::noiseSource, NodeKind::pluck, NodeKind::drumMachine,
                             NodeKind::sampler, NodeKind::fourTrack, NodeKind::lfo,
                             NodeKind::randomLfo, NodeKind::envelopeFollower, NodeKind::stepSequencer,
                             NodeKind::macro, NodeKind::spectralFollower, NodeKind::script,
                             NodeKind::neuralAmpPlaceholder })
        if (key == nodeKindKey (kind))
            return kind;

    return std::nullopt;
}

SignalMeter::SignalMeter() noexcept
{
    static std::atomic<int> nextRefreshPhase { 0 };
    blocksUntilWaveformRefresh = nextRefreshPhase.fetch_add (1, std::memory_order_relaxed) & 7;
    for (auto& value : lows)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : highs)
        value.store (0.0f, std::memory_order_relaxed);
}

void SignalMeter::capture (const float* samples, int numSamples) noexcept
{
    if (samples == nullptr || numSamples <= 0)
    {
        rms.store (0.0f, std::memory_order_relaxed);
        peak.store (0.0f, std::memory_order_relaxed);
        return;
    }

    double sumSquares = 0.0;
    float blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto sample = std::isfinite (samples[i]) ? samples[i] : 0.0f;
        sumSquares += static_cast<double> (sample) * static_cast<double> (sample);
        blockPeak = juce::jmax (blockPeak, std::abs (sample));
    }

    rms.store (static_cast<float> (std::sqrt (sumSquares / static_cast<double> (numSamples))),
               std::memory_order_relaxed);
    peak.store (blockPeak, std::memory_order_relaxed);

    if (blocksUntilWaveformRefresh > 0)
    {
        --blocksUntilWaveformRefresh;
        return;
    }
    blocksUntilWaveformRefresh = 7;

    for (int bucket = 0; bucket < WaveformSnapshot::bucketCount; ++bucket)
    {
        const int begin = (bucket * numSamples) / WaveformSnapshot::bucketCount;
        const int end = juce::jmax (begin + 1, ((bucket + 1) * numSamples) / WaveformSnapshot::bucketCount);
        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();

        for (int sampleIndex = begin; sampleIndex < juce::jmin (end, numSamples); ++sampleIndex)
        {
            const auto sample = std::isfinite (samples[sampleIndex]) ? samples[sampleIndex] : 0.0f;
            low = juce::jmin (low, sample);
            high = juce::jmax (high, sample);
        }

        if (begin >= numSamples)
            low = high = samples[juce::jmin (begin, numSamples - 1)];

        lows[static_cast<std::size_t> (bucket)].store (low, std::memory_order_relaxed);
        highs[static_cast<std::size_t> (bucket)].store (high, std::memory_order_relaxed);
    }
}

WaveformSnapshot SignalMeter::snapshot() const noexcept
{
    WaveformSnapshot result;
    for (int i = 0; i < WaveformSnapshot::bucketCount; ++i)
    {
        result.low[static_cast<std::size_t> (i)] = lows[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
        result.high[static_cast<std::size_t> (i)] = highs[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
    }
    result.rms = rms.load (std::memory_order_relaxed);
    result.peak = peak.load (std::memory_order_relaxed);
    return result;
}

float SignalMeter::getRms() const noexcept
{
    return rms.load (std::memory_order_relaxed);
}

float SignalMeter::getPeak() const noexcept
{
    return peak.load (std::memory_order_relaxed);
}

DspParameter::DspParameter (juce::String stableId,
                            juce::String displayName,
                            juce::String unitToUse,
                            juce::NormalisableRange<float> valueRange,
                            float defaultValue,
                            float defaultModulationDepth)
    : id (std::move (stableId)),
      name (std::move (displayName)),
      unit (std::move (unitToUse)),
      range (std::move (valueRange))
{
    setValue (defaultValue);
    setModulationDepth (defaultModulationDepth);
    smoother.setCurrentAndTargetValue (getNormalisedValue());
}

void DspParameter::prepare (double sampleRate) noexcept
{
    smoother.reset (sampleRate, 0.01);
    smoother.setCurrentAndTargetValue (getNormalisedValue());
}

float DspParameter::nextValue (float modulation) noexcept
{
    auto base = getNormalisedValue();
    if (! std::isfinite (base))
        base = 0.0f;
    if (! std::isfinite (modulation))
        modulation = 0.0f;
    auto depth = getModulationDepth();
    if (! std::isfinite (depth))
        depth = 0.0f;

    smoother.setTargetValue (base);
    auto smoothed = smoother.getNextValue();
    if (! std::isfinite (smoothed))
    {
        smoother.setCurrentAndTargetValue (base);
        smoothed = base;
    }
    const auto normalised = juce::jlimit (0.0f, 1.0f, smoothed + modulation * depth);
    return range.convertFrom0to1 (normalised);
}

void DspParameter::setValue (float value) noexcept
{
    if (! std::isfinite (value))
        return;
    setNormalisedValue (range.convertTo0to1 (range.snapToLegalValue (value)));
}

float DspParameter::getValue() const noexcept
{
    return range.convertFrom0to1 (getNormalisedValue());
}

void DspParameter::setNormalisedValue (float value) noexcept
{
    if (! std::isfinite (value))
        return;
    baseNormalised.store (juce::jlimit (0.0f, 1.0f, value), std::memory_order_relaxed);
}

float DspParameter::getNormalisedValue() const noexcept
{
    return baseNormalised.load (std::memory_order_relaxed);
}

void DspParameter::setModulationDepth (float value) noexcept
{
    if (! std::isfinite (value))
        return;
    modulationDepth.store (juce::jlimit (0.0f, 1.0f, value), std::memory_order_relaxed);
}

float DspParameter::getModulationDepth() const noexcept
{
    return modulationDepth.load (std::memory_order_relaxed);
}

DspNode::DspNode (NodeKind kindToUse, juce::String displayName)
    : kind (kindToUse), name (std::move (displayName))
{
}

int DspNode::getNumInputPorts() const noexcept
{
    return static_cast<int> (inputPorts.size());
}

int DspNode::getNumOutputPorts() const noexcept
{
    return static_cast<int> (outputPorts.size());
}

const PortInfo& DspNode::getInputPort (int index) const
{
    return inputPorts.at (static_cast<std::size_t> (index));
}

const PortInfo& DspNode::getOutputPort (int index) const
{
    return outputPorts.at (static_cast<std::size_t> (index));
}

int DspNode::getNumParameters() const noexcept
{
    return static_cast<int> (parameters.size());
}

DspParameter& DspNode::getParameter (int index) const
{
    return *parameters.at (static_cast<std::size_t> (index));
}

void DspNode::prepare (double sampleRate, int maximumBlockSize)
{
    for (auto& parameter : parameters)
        parameter->prepare (sampleRate);
    prepareDsp (sampleRate, maximumBlockSize);
}

void DspNode::reset() noexcept
{
    resetDsp();
}

void DspNode::render (const juce::AudioBuffer<float>& inputs,
                      juce::AudioBuffer<float>& outputs,
                      int numSamples) noexcept
{
    if (isBypassed())
    {
        // True pedal bypass: first audio input passes straight to the first
        // audio output; everything else this node would emit goes silent.
        for (int channel = 0; channel < outputs.getNumChannels(); ++channel)
            juce::FloatVectorOperations::clear (outputs.getWritePointer (channel), numSamples);
        const auto throughIn = firstAudioInputPort (*this);
        const auto throughOut = firstAudioOutputPort (*this);
        if (throughIn >= 0 && throughOut >= 0
            && throughIn < inputs.getNumChannels() && throughOut < outputs.getNumChannels())
            juce::FloatVectorOperations::copy (outputs.getWritePointer (throughOut),
                                               inputs.getReadPointer (throughIn), numSamples);
    }
    else
    {
        processDsp (inputs, outputs, numSamples);
    }

    const auto inputPort = firstAudioInputPort (*this);
    if (inputPort >= 0 && inputPort < inputs.getNumChannels())
        primaryInputMeter.capture (inputs.getReadPointer (inputPort), numSamples);

    for (int port = 0; port < juce::jmin (outputs.getNumChannels(), getNumOutputPorts()); ++port)
        outputMeters[static_cast<std::size_t> (port)]->capture (outputs.getReadPointer (port), numSamples);
}

void DspNode::renderFeedbackWrite (const juce::AudioBuffer<float>& inputs, int numSamples) noexcept
{
    const auto inputPort = firstAudioInputPort (*this);
    if (inputPort >= 0 && inputPort < inputs.getNumChannels())
        primaryInputMeter.capture (inputs.getReadPointer (inputPort), numSamples);
    processFeedbackWriteDsp (inputs, numSamples);
}

WaveformSnapshot DspNode::inputWaveform() const noexcept
{
    return primaryInputMeter.snapshot();
}

WaveformSnapshot DspNode::outputWaveform (int port) const noexcept
{
    if (juce::isPositiveAndBelow (port, static_cast<int> (outputMeters.size())))
        return outputMeters[static_cast<std::size_t> (port)]->snapshot();
    return {};
}

float DspNode::outputRms (int port) const noexcept
{
    if (juce::isPositiveAndBelow (port, static_cast<int> (outputMeters.size())))
        return outputMeters[static_cast<std::size_t> (port)]->getRms();
    return 0.0f;
}

float DspNode::outputPeak (int port) const noexcept
{
    if (juce::isPositiveAndBelow (port, static_cast<int> (outputMeters.size())))
        return outputMeters[static_cast<std::size_t> (port)]->getPeak();
    return 0.0f;
}

PortInfo& DspNode::addInputPort (juce::String portName, SignalType type)
{
    inputPorts.push_back ({ std::move (portName), type, -1, -1, -1, true });
    return inputPorts.back();
}

PortInfo& DspNode::addOutputPort (juce::String portName, SignalType type)
{
    outputPorts.push_back ({ std::move (portName), type, -1, -1, -1, true });
    outputMeters.push_back (std::make_unique<SignalMeter>());
    return outputPorts.back();
}

DspParameter& DspNode::addParameter (juce::String stableId,
                                     juce::String displayName,
                                     juce::String unitToUse,
                                     juce::NormalisableRange<float> range,
                                     float defaultValue,
                                     float defaultModulationDepth,
                                     bool modulatable)
{
    const auto parameterIndex = static_cast<int> (parameters.size());
    auto parameter = std::make_unique<DspParameter> (std::move (stableId), displayName, std::move (unitToUse),
                                                      std::move (range), defaultValue, defaultModulationDepth);
    if (modulatable)
    {
        parameter->inputPortIndex = static_cast<int> (inputPorts.size());
        inputPorts.push_back ({ displayName + " mod", SignalType::control, parameterIndex, -1, -1, true });
    }
    parameters.push_back (std::move (parameter));
    return *parameters.back();
}

float DspNode::parameterValue (int parameterIndex,
                               const juce::AudioBuffer<float>& inputs,
                               int sampleIndex) noexcept
{
    auto& parameter = getParameter (parameterIndex);
    float modulation = 0.0f;
    if (juce::isPositiveAndBelow (parameter.inputPortIndex, inputs.getNumChannels())
        && juce::isPositiveAndBelow (sampleIndex, inputs.getNumSamples()))
        modulation = inputs.getSample (parameter.inputPortIndex, sampleIndex);

    if (! std::isfinite (modulation))
        modulation = 0.0f;
    return parameter.nextValue (juce::jlimit (-1.0f, 1.0f, modulation));
}

PatchDocument::PatchDocument()
{
    auto input = createHardwareInputProcessor ({});
    auto output = createHardwareOutputProcessor ({});
    input->prepare (currentSampleRate, currentMaximumBlockSize);
    output->prepare (currentSampleRate, currentMaximumBlockSize);
    nodes.push_back ({ hardwareInputId, std::move (input), { 80.0f, 180.0f }, true });
    nodes.push_back ({ hardwareOutputId, std::move (output), { 1900.0f, 180.0f }, true });
}

void PatchDocument::configureHardware (const juce::StringArray& rawInputNames,
                                       const juce::StringArray& rawOutputNames,
                                       const std::vector<int>& rawInputCallbackChannels,
                                       const std::vector<int>& rawOutputCallbackChannels)
{
    auto inputNames = rawInputNames;
    auto outputNames = rawOutputNames;
    auto inputCallbackChannels = rawInputCallbackChannels;
    auto outputCallbackChannels = rawOutputCallbackChannels;
    if (inputCallbackChannels.empty())
        for (int channel = 0; channel < inputNames.size(); ++channel)
            inputCallbackChannels.push_back (channel);
    if (outputCallbackChannels.empty())
        for (int channel = 0; channel < outputNames.size(); ++channel)
            outputCallbackChannels.push_back (channel);

    if (hardwareLayoutConfigured)
    {
        if (const auto* previous = findNode (hardwareInputId))
            for (int channel = inputNames.size(); channel < previous->processor->getNumOutputPorts(); ++channel)
            {
                auto name = previous->processor->getOutputPort (channel).name;
                if (! name.containsIgnoreCase ("missing"))
                    name += " [missing]";
                inputNames.add (name);
                inputCallbackChannels.push_back (-1);
            }
        if (const auto* previous = findNode (hardwareOutputId))
            for (int channel = outputNames.size(); channel < previous->processor->getNumInputPorts(); ++channel)
            {
                auto name = previous->processor->getInputPort (channel).name;
                if (! name.containsIgnoreCase ("missing"))
                    name += " [missing]";
                outputNames.add (name);
                outputCallbackChannels.push_back (-1);
            }
    }

    auto replaceHardware = [&] (NodeId id, std::shared_ptr<DspNode> processor, juce::Point<float> defaultPosition)
    {
        if (auto* existing = findNode (id))
        {
            existing->processor = std::move (processor);
            existing->processor->prepare (currentSampleRate, currentMaximumBlockSize);
            return;
        }

        processor->prepare (currentSampleRate, currentMaximumBlockSize);
        nodes.push_back ({ id, std::move (processor), defaultPosition, true });
    };

    replaceHardware (hardwareInputId, createHardwareInputProcessor (inputNames, inputCallbackChannels), { 80.0f, 180.0f });
    replaceHardware (hardwareOutputId, createHardwareOutputProcessor (outputNames, outputCallbackChannels), { 1900.0f, 180.0f });
    hardwareLayoutConfigured = true;
}

void PatchDocument::prepareAll (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    currentMaximumBlockSize = juce::jmax (1, maximumBlockSize);
    for (auto& node : nodes)
        node.processor->prepare (currentSampleRate, currentMaximumBlockSize);
}

NodeId PatchDocument::addNode (NodeKind kind, juce::Point<float> position, std::optional<NodeId> requestedId)
{
    if (kind == NodeKind::hardwareInput || kind == NodeKind::hardwareOutput)
        return 0;

    auto processor = createNodeProcessor (kind);
    if (processor == nullptr)
        return 0;

    NodeId id = requestedId.value_or (nextNodeId++);
    if (id == hardwareInputId || id == hardwareOutputId || findNode (id) != nullptr)
        return 0;

    nextNodeId = juce::jmax (nextNodeId, id + 1);
    processor->prepare (currentSampleRate, currentMaximumBlockSize);
    nodes.push_back ({ id, std::move (processor), position, false });
    return id;
}

bool PatchDocument::removeNode (NodeId id)
{
    if (id == hardwareInputId || id == hardwareOutputId)
        return false;

    const auto oldSize = nodes.size();
    nodes.erase (std::remove_if (nodes.begin(), nodes.end(), [id] (const NodeModel& node) { return node.id == id; }),
                 nodes.end());
    if (nodes.size() == oldSize)
        return false;

    connections.erase (std::remove_if (connections.begin(), connections.end(), [id] (const Connection& connection)
    {
        return connection.sourceNode == id || connection.destinationNode == id;
    }), connections.end());
    return true;
}

juce::Result PatchDocument::addConnection (Connection connection)
{
    auto* source = findNode (connection.sourceNode);
    auto* destination = findNode (connection.destinationNode);
    if (source == nullptr || destination == nullptr)
        return juce::Result::fail ("One end of that cable no longer exists.");
    if (! juce::isPositiveAndBelow (connection.sourcePort, source->processor->getNumOutputPorts())
        || ! juce::isPositiveAndBelow (connection.destinationPort, destination->processor->getNumInputPorts()))
        return juce::Result::fail ("That port is no longer available.");
    if (source->processor->getOutputPort (connection.sourcePort).type
        != destination->processor->getInputPort (connection.destinationPort).type)
        return juce::Result::fail ("Audio cables connect to audio ports; control cables connect to control ports.");
    if (std::find (connections.begin(), connections.end(), connection) != connections.end())
        return juce::Result::fail ("That cable already exists.");

    connections.push_back (connection);
    return juce::Result::ok();
}

bool PatchDocument::removeConnection (const Connection& connection)
{
    const auto oldSize = connections.size();
    connections.erase (std::remove (connections.begin(), connections.end(), connection), connections.end());
    return oldSize != connections.size();
}

void PatchDocument::clearUserPatch()
{
    nodes.erase (std::remove_if (nodes.begin(), nodes.end(), [] (const NodeModel& node) { return ! node.hardware; }),
                 nodes.end());
    connections.clear();
}

NodeModel* PatchDocument::findNode (NodeId id) noexcept
{
    const auto found = std::find_if (nodes.begin(), nodes.end(), [id] (const NodeModel& node) { return node.id == id; });
    return found == nodes.end() ? nullptr : &*found;
}

const NodeModel* PatchDocument::findNode (NodeId id) const noexcept
{
    const auto found = std::find_if (nodes.begin(), nodes.end(), [id] (const NodeModel& node) { return node.id == id; });
    return found == nodes.end() ? nullptr : &*found;
}

juce::var PatchDocument::toJson() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("schema", 1);
    root->setProperty ("format", "signalpatch");

    juce::Array<juce::var> nodeValues;
    for (const auto& node : nodes)
    {
        auto nodeObject = std::make_unique<juce::DynamicObject>();
        nodeObject->setProperty ("id", static_cast<juce::int64> (node.id));
        nodeObject->setProperty ("kind", nodeKindKey (node.processor->getKind()));
        nodeObject->setProperty ("name", node.processor->getName());
        nodeObject->setProperty ("x", node.position.x);
        nodeObject->setProperty ("y", node.position.y);

        if (node.hardware)
        {
            juce::Array<juce::var> portValues;
            const auto isInputDevice = node.processor->getKind() == NodeKind::hardwareInput;
            const auto portCount = isInputDevice ? node.processor->getNumOutputPorts()
                                                 : node.processor->getNumInputPorts();
            for (int portIndex = 0; portIndex < portCount; ++portIndex)
            {
                const auto& port = isInputDevice ? node.processor->getOutputPort (portIndex)
                                                 : node.processor->getInputPort (portIndex);
                auto portObject = std::make_unique<juce::DynamicObject>();
                portObject->setProperty ("index", port.hardwareChannelIndex >= 0
                                                   ? port.hardwareChannelIndex : portIndex);
                portObject->setProperty ("label", port.name);
                portValues.add (juce::var (portObject.release()));
            }
            nodeObject->setProperty ("hardwarePorts", portValues);
        }

        juce::Array<juce::var> parameterValues;
        for (int parameterIndex = 0; parameterIndex < node.processor->getNumParameters(); ++parameterIndex)
        {
            const auto& parameter = node.processor->getParameter (parameterIndex);
            auto parameterObject = std::make_unique<juce::DynamicObject>();
            parameterObject->setProperty ("id", parameter.id);
            parameterObject->setProperty ("value", parameter.getValue());
            parameterObject->setProperty ("depth", parameter.getModulationDepth());
            parameterValues.add (juce::var (parameterObject.release()));
        }
        nodeObject->setProperty ("parameters", parameterValues);
        if (node.processor->isBypassed())
            nodeObject->setProperty ("bypassed", true);
        const auto extraState = node.processor->getExtraState();
        if (! extraState.isVoid())
            nodeObject->setProperty ("extra", extraState);
        nodeValues.add (juce::var (nodeObject.release()));
    }
    root->setProperty ("nodes", nodeValues);

    juce::Array<juce::var> connectionValues;
    for (const auto& connection : connections)
    {
        auto connectionObject = std::make_unique<juce::DynamicObject>();
        connectionObject->setProperty ("sourceNode", static_cast<juce::int64> (connection.sourceNode));
        connectionObject->setProperty ("sourcePort", connection.sourcePort);
        connectionObject->setProperty ("destinationNode", static_cast<juce::int64> (connection.destinationNode));
        connectionObject->setProperty ("destinationPort", connection.destinationPort);
        connectionValues.add (juce::var (connectionObject.release()));
    }
    root->setProperty ("connections", connectionValues);
    return juce::var (root.release());
}

juce::Result PatchDocument::loadJson (const juce::var& value)
{
    const auto* root = value.getDynamicObject();
    if (root == nullptr || root->getProperty ("format").toString() != "signalpatch")
        return juce::Result::fail ("This is not a SignalPatch document.");
    if (static_cast<int> (root->getProperty ("schema")) > 1)
        return juce::Result::fail ("This patch was made by a newer SignalPatch version.");

    constexpr int maximumSavedHardwareChannels = 256;
    if (const auto* savedNodes = root->getProperty ("nodes").getArray())
        for (const auto& savedNode : *savedNodes)
            if (const auto* object = savedNode.getDynamicObject())
                if (const auto* savedPorts = object->getProperty ("hardwarePorts").getArray())
                    for (const auto& savedPort : *savedPorts)
                        if (const auto* portObject = savedPort.getDynamicObject())
                        {
                            const auto physicalIndex = static_cast<int> (portObject->getProperty ("index"));
                            if (physicalIndex < 0 || physicalIndex >= maximumSavedHardwareChannels)
                                return juce::Result::fail ("Patch contains an invalid hardware channel index.");
                        }

    clearUserPatch();

    juce::StringArray retainedInputNames;
    juce::StringArray retainedOutputNames;
    std::vector<int> retainedInputCallbacks;
    std::vector<int> retainedOutputCallbacks;
    if (const auto* input = findNode (hardwareInputId))
        for (int port = 0; port < input->processor->getNumOutputPorts(); ++port)
        {
            const auto& info = input->processor->getOutputPort (port);
            retainedInputNames.add (info.name);
            retainedInputCallbacks.push_back (info.callbackChannelIndex);
        }
    if (const auto* output = findNode (hardwareOutputId))
        for (int port = 0; port < output->processor->getNumInputPorts(); ++port)
        {
            const auto& info = output->processor->getInputPort (port);
            retainedOutputNames.add (info.name);
            retainedOutputCallbacks.push_back (info.callbackChannelIndex);
        }

    if (const auto* savedNodes = root->getProperty ("nodes").getArray())
    {
        for (const auto& savedNode : *savedNodes)
        {
            const auto* object = savedNode.getDynamicObject();
            if (object == nullptr)
                continue;
            const auto kind = nodeKindFromKey (object->getProperty ("kind").toString());
            if (! kind.has_value() || (*kind != NodeKind::hardwareInput && *kind != NodeKind::hardwareOutput))
                continue;
            const auto* savedPorts = object->getProperty ("hardwarePorts").getArray();
            if (savedPorts == nullptr)
                continue;
            auto& names = *kind == NodeKind::hardwareInput ? retainedInputNames : retainedOutputNames;
            auto& callbacks = *kind == NodeKind::hardwareInput ? retainedInputCallbacks : retainedOutputCallbacks;
            for (const auto& savedPort : *savedPorts)
            {
                const auto* portObject = savedPort.getDynamicObject();
                if (portObject == nullptr)
                    continue;
                const auto physicalIndex = static_cast<int> (portObject->getProperty ("index"));
                while (names.size() <= physicalIndex)
                {
                    const auto nextIndex = names.size();
                    names.add ("Missing channel " + juce::String (nextIndex + 1));
                    callbacks.push_back (-1);
                }
                if (physicalIndex >= 0 && callbacks[static_cast<std::size_t> (physicalIndex)] < 0)
                {
                    auto label = portObject->getProperty ("label").toString();
                    if (label.isEmpty())
                        label = "Channel " + juce::String (physicalIndex + 1);
                    if (! label.containsIgnoreCase ("missing"))
                        label += " [missing]";
                    names.set (physicalIndex, label);
                }
            }
        }
    }
    configureHardware (retainedInputNames, retainedOutputNames,
                       retainedInputCallbacks, retainedOutputCallbacks);

    if (const auto* nodeArray = root->getProperty ("nodes").getArray())
    {
        for (const auto& nodeValue : *nodeArray)
        {
            const auto* object = nodeValue.getDynamicObject();
            if (object == nullptr)
                continue;
            const auto maybeKind = nodeKindFromKey (object->getProperty ("kind").toString());
            if (! maybeKind.has_value())
                continue;

            const auto id = static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("id")));
            NodeModel* model = nullptr;
            if (*maybeKind == NodeKind::hardwareInput)
                model = findNode (hardwareInputId);
            else if (*maybeKind == NodeKind::hardwareOutput)
                model = findNode (hardwareOutputId);
            else
            {
                const auto createdId = addNode (*maybeKind,
                                                { static_cast<float> (object->getProperty ("x")),
                                                  static_cast<float> (object->getProperty ("y")) }, id);
                model = findNode (createdId);
            }

            if (model == nullptr)
                continue;
            model->position = { static_cast<float> (object->getProperty ("x")),
                                static_cast<float> (object->getProperty ("y")) };
            const auto savedName = object->getProperty ("name").toString();
            if (savedName.isNotEmpty())
                model->processor->setName (savedName);
            model->processor->setBypassed (static_cast<bool> (object->getProperty ("bypassed")));
            if (object->hasProperty ("extra"))
                model->processor->setExtraState (object->getProperty ("extra"));

            if (const auto* parameterArray = object->getProperty ("parameters").getArray())
            {
                for (const auto& parameterValue : *parameterArray)
                {
                    const auto* parameterObject = parameterValue.getDynamicObject();
                    if (parameterObject == nullptr)
                        continue;
                    const auto parameterId = parameterObject->getProperty ("id").toString();
                    for (int parameterIndex = 0; parameterIndex < model->processor->getNumParameters(); ++parameterIndex)
                    {
                        auto& parameter = model->processor->getParameter (parameterIndex);
                        if (parameter.id == parameterId)
                        {
                            parameter.setValue (static_cast<float> (parameterObject->getProperty ("value")));
                            parameter.setModulationDepth (static_cast<float> (parameterObject->getProperty ("depth")));
                            break;
                        }
                    }
                }
            }
        }
    }

    if (const auto* connectionArray = root->getProperty ("connections").getArray())
    {
        for (const auto& connectionValue : *connectionArray)
        {
            const auto* object = connectionValue.getDynamicObject();
            if (object == nullptr)
                continue;
            Connection connection {
                static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("sourceNode"))),
                static_cast<int> (object->getProperty ("sourcePort")),
                static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("destinationNode"))),
                static_cast<int> (object->getProperty ("destinationPort"))
            };
            addConnection (connection);
        }
    }

    return juce::Result::ok();
}

RenderPlan::RenderPlan (int maximumBlockSizeToUse)
    : maximumBlockSize (juce::jmax (1, maximumBlockSizeToUse))
{
}

RenderPlan::~RenderPlan() = default;

void RenderPlan::mixInputs (int nodeIndex, int numSamples) noexcept
{
    auto& destination = *renderNodes[static_cast<std::size_t> (nodeIndex)];
    for (int destinationPort = 0; destinationPort < destination.inputs.getNumChannels(); ++destinationPort)
    {
        auto* destinationSamples = destination.inputs.getWritePointer (destinationPort);
        bool hasSource = false;
        for (const auto& sourceRef : destination.incoming[static_cast<std::size_t> (destinationPort)])
        {
            const auto& source = *renderNodes[static_cast<std::size_t> (sourceRef.nodeIndex)];
            if (! juce::isPositiveAndBelow (sourceRef.portIndex, source.outputs.getNumChannels()))
                continue;
            const auto* sourceSamples = source.outputs.getReadPointer (sourceRef.portIndex);
            if (! hasSource)
                juce::FloatVectorOperations::copy (destinationSamples, sourceSamples, numSamples);
            else
                juce::FloatVectorOperations::add (destinationSamples, sourceSamples, numSamples);
            hasSource = true;
        }

        for (int sample = 0; sample < numSamples; ++sample)
            if (! std::isfinite (destinationSamples[sample]))
                destinationSamples[sample] = 0.0f;
    }
}

void RenderPlan::render (const float* const* hardwareInputs,
                         int numHardwareInputs,
                         float* const* hardwareOutputs,
                         int numHardwareOutputs,
                         int hardwareOffset,
                         int numSamples) noexcept
{
    jassert (numSamples <= maximumBlockSize);
    numSamples = juce::jmin (numSamples, maximumBlockSize);

    for (int channel = 0; channel < numHardwareOutputs; ++channel)
        if (hardwareOutputs[channel] != nullptr)
            juce::FloatVectorOperations::clear (hardwareOutputs[channel] + hardwareOffset, numSamples);

    for (auto& node : renderNodes)
    {
        node->inputs.clear (0, numSamples);
        node->outputs.clear (0, numSamples);
    }

    for (const auto nodeIndex : executionOrder)
    {
        auto& renderNode = *renderNodes[static_cast<std::size_t> (nodeIndex)];
        auto& processor = *renderNode.processor;

        if (nodeIndex == hardwareInputNode)
        {
            for (int port = 0; port < renderNode.outputs.getNumChannels(); ++port)
            {
                const auto callbackChannel = processor.getOutputPort (port).callbackChannelIndex;
                if (juce::isPositiveAndBelow (callbackChannel, numHardwareInputs)
                    && hardwareInputs != nullptr && hardwareInputs[callbackChannel] != nullptr)
                    renderNode.outputs.copyFrom (port, 0, hardwareInputs[callbackChannel] + hardwareOffset, numSamples);
            }
            processor.render (renderNode.inputs, renderNode.outputs, numSamples);
            continue;
        }

        if (processor.isFeedbackGuard())
        {
            processor.render (renderNode.inputs, renderNode.outputs, numSamples);
            continue;
        }

        mixInputs (nodeIndex, numSamples);
        processor.render (renderNode.inputs, renderNode.outputs, numSamples);
    }

    for (const auto nodeIndex : feedbackNodes)
    {
        mixInputs (nodeIndex, numSamples);
        auto& node = *renderNodes[static_cast<std::size_t> (nodeIndex)];
        node.processor->renderFeedbackWrite (node.inputs, numSamples);
    }

    if (juce::isPositiveAndBelow (hardwareOutputNode, static_cast<int> (renderNodes.size())))
    {
        const auto& outputNode = *renderNodes[static_cast<std::size_t> (hardwareOutputNode)];
        for (int port = 0; port < outputNode.inputs.getNumChannels(); ++port)
        {
            const auto callbackChannel = outputNode.processor->getInputPort (port).callbackChannelIndex;
            if (! juce::isPositiveAndBelow (callbackChannel, numHardwareOutputs)
                || hardwareOutputs[callbackChannel] == nullptr)
                continue;
            const auto* source = outputNode.inputs.getReadPointer (port);
            auto* destination = hardwareOutputs[callbackChannel] + hardwareOffset;
            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto value = std::isfinite (source[sample]) ? source[sample] : 0.0f;
                destination[sample] = juce::jlimit (-outputSafetyCeiling, outputSafetyCeiling, value);
            }
        }
    }
}

GraphCompileResult GraphCompiler::compile (const PatchDocument& document,
                                           int maximumBlockSize,
                                           bool makesDeviceReady)
{
    GraphCompileResult result;
    auto plan = std::unique_ptr<RenderPlan> (new RenderPlan (maximumBlockSize));
    plan->makesDeviceReady = makesDeviceReady;

    const auto& documentNodes = document.getNodes();
    std::unordered_map<NodeId, int> nodeIndices;
    nodeIndices.reserve (documentNodes.size());

    for (int index = 0; index < static_cast<int> (documentNodes.size()); ++index)
    {
        const auto& model = documentNodes[static_cast<std::size_t> (index)];
        nodeIndices.emplace (model.id, index);
        auto renderNode = std::make_unique<RenderPlan::RenderNode>();
        renderNode->processor = model.processor;
        renderNode->inputs.setSize (model.processor->getNumInputPorts(), plan->maximumBlockSize, false, true, false);
        renderNode->outputs.setSize (model.processor->getNumOutputPorts(), plan->maximumBlockSize, false, true, false);
        renderNode->incoming.resize (static_cast<std::size_t> (model.processor->getNumInputPorts()));
        plan->renderNodes.push_back (std::move (renderNode));

        if (model.processor->getKind() == NodeKind::hardwareInput)
            plan->hardwareInputNode = index;
        else if (model.processor->getKind() == NodeKind::hardwareOutput)
            plan->hardwareOutputNode = index;
        else if (model.processor->isFeedbackGuard())
            plan->feedbackNodes.push_back (index);
    }

    if (plan->hardwareInputNode < 0 || plan->hardwareOutputNode < 0)
    {
        result.error = "The graph has no hardware input or output node.";
        return result;
    }

    const int nodeCount = static_cast<int> (documentNodes.size());
    std::vector<int> indegree (static_cast<std::size_t> (nodeCount), 0);
    std::vector<std::vector<int>> adjacency (static_cast<std::size_t> (nodeCount));

    for (const auto& connection : document.getConnections())
    {
        const auto sourceFound = nodeIndices.find (connection.sourceNode);
        const auto destinationFound = nodeIndices.find (connection.destinationNode);
        if (sourceFound == nodeIndices.end() || destinationFound == nodeIndices.end())
        {
            result.error = "A cable points to a missing node.";
            return result;
        }

        const auto sourceIndex = sourceFound->second;
        const auto destinationIndex = destinationFound->second;
        const auto& source = *plan->renderNodes[static_cast<std::size_t> (sourceIndex)];
        const auto& destination = *plan->renderNodes[static_cast<std::size_t> (destinationIndex)];
        if (! juce::isPositiveAndBelow (connection.sourcePort, source.processor->getNumOutputPorts())
            || ! juce::isPositiveAndBelow (connection.destinationPort, destination.processor->getNumInputPorts()))
        {
            result.error = "A cable points to a port that no longer exists.";
            return result;
        }
        if (source.processor->getOutputPort (connection.sourcePort).type
            != destination.processor->getInputPort (connection.destinationPort).type)
        {
            result.error = "A cable joins incompatible signal types.";
            return result;
        }

        plan->renderNodes[static_cast<std::size_t> (destinationIndex)]
            ->incoming[static_cast<std::size_t> (connection.destinationPort)]
            .push_back ({ sourceIndex, connection.sourcePort });

        if (! destination.processor->isFeedbackGuard())
        {
            adjacency[static_cast<std::size_t> (sourceIndex)].push_back (destinationIndex);
            ++indegree[static_cast<std::size_t> (destinationIndex)];
        }
    }

    std::deque<int> ready;
    for (int index = 0; index < nodeCount; ++index)
        if (indegree[static_cast<std::size_t> (index)] == 0)
            ready.push_back (index);

    while (! ready.empty())
    {
        const auto nodeIndex = ready.front();
        ready.pop_front();
        plan->executionOrder.push_back (nodeIndex);
        for (const auto destination : adjacency[static_cast<std::size_t> (nodeIndex)])
            if (--indegree[static_cast<std::size_t> (destination)] == 0)
                ready.push_back (destination);
    }

    if (static_cast<int> (plan->executionOrder.size()) != nodeCount)
    {
        result.error = "Unsafe zero-delay cycle blocked. Put a Feedback Guard in every feedback loop.";
        return result;
    }

    std::vector<int> pathLatency (static_cast<std::size_t> (nodeCount), 0);
    for (const auto nodeIndex : plan->executionOrder)
    {
        auto& renderNode = *plan->renderNodes[static_cast<std::size_t> (nodeIndex)];
        int maximumInputLatency = 0;
        if (! renderNode.processor->isFeedbackGuard())
            for (const auto& inputSources : renderNode.incoming)
                for (const auto& source : inputSources)
                    maximumInputLatency = juce::jmax (maximumInputLatency,
                                                      pathLatency[static_cast<std::size_t> (source.nodeIndex)]);
        pathLatency[static_cast<std::size_t> (nodeIndex)] = maximumInputLatency
                                                          + renderNode.processor->latencySamples();
    }
    plan->graphLatencySamples = pathLatency[static_cast<std::size_t> (plan->hardwareOutputNode)];

    result.plan = std::move (plan);
    return result;
}
} // namespace signalpatch
