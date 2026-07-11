#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace signalpatch
{
using NodeId = std::uint32_t;

enum class SignalType
{
    audio,
    control
};

enum class NodeKind
{
    hardwareInput,
    hardwareOutput,
    gain,
    mixer,
    crossfade,
    distortion,
    filter,
    delay,
    reverb,
    chorus,
    phaser,
    tremolo,
    bitcrusher,
    ringMod,
    vowelFilter,
    pitchShifter,
    vocoder,
    pitchCorrector,
    granular,
    compressor,
    limiter,
    gate,
    feedbackGuard,
    monoSynth,
    noiseSource,
    pluck,
    drumMachine,
    sampler,
    fourTrack,
    lfo,
    randomLfo,
    envelopeFollower,
    stepSequencer,
    macro,
    spectralFollower,
    script,
    neuralAmpPlaceholder,
    neuralPedal
};

juce::String nodeKindName (NodeKind kind);
juce::String nodeKindKey (NodeKind kind);
std::optional<NodeKind> nodeKindFromKey (const juce::String& key);

struct PortInfo
{
    juce::String name;
    SignalType type = SignalType::audio;
    int parameterIndex = -1;
    int hardwareChannelIndex = -1;
    int callbackChannelIndex = -1;
    bool active = true;
};

struct WaveformSnapshot
{
    static constexpr int bucketCount = 96;
    std::array<float, bucketCount> low {};
    std::array<float, bucketCount> high {};
    float rms = 0.0f;
    float peak = 0.0f;
};

class SignalMeter
{
public:
    SignalMeter() noexcept;

    void capture (const float* samples, int numSamples) noexcept;
    [[nodiscard]] WaveformSnapshot snapshot() const noexcept;
    [[nodiscard]] float getRms() const noexcept;
    [[nodiscard]] float getPeak() const noexcept;

private:
    std::array<std::atomic<float>, WaveformSnapshot::bucketCount> lows;
    std::array<std::atomic<float>, WaveformSnapshot::bucketCount> highs;
    std::atomic<float> rms { 0.0f };
    std::atomic<float> peak { 0.0f };
    int blocksUntilWaveformRefresh = 0;
};

class DspParameter
{
public:
    DspParameter (juce::String stableId,
                  juce::String displayName,
                  juce::String unit,
                  juce::NormalisableRange<float> valueRange,
                  float defaultValue,
                  float defaultModulationDepth = 0.5f);

    void prepare (double sampleRate) noexcept;
    [[nodiscard]] float nextValue (float modulation) noexcept;

    void setValue (float value) noexcept;
    [[nodiscard]] float getValue() const noexcept;
    void setNormalisedValue (float value) noexcept;
    [[nodiscard]] float getNormalisedValue() const noexcept;

    void setModulationDepth (float value) noexcept;
    [[nodiscard]] float getModulationDepth() const noexcept;

    const juce::String id;
    const juce::String name;
    const juce::String unit;
    const juce::NormalisableRange<float> range;
    int inputPortIndex = -1;

private:
    std::atomic<float> baseNormalised { 0.0f };
    std::atomic<float> modulationDepth { 0.5f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
};

// enable_shared_from_this: nodes are always owned by shared_ptr (document +
// render plans), and async workers (e.g. NAM model loading) need a weak
// handle that outlives UI rebuilds.
class DspNode : public std::enable_shared_from_this<DspNode>
{
public:
    DspNode (NodeKind kindToUse, juce::String displayName);
    virtual ~DspNode() = default;

    DspNode (const DspNode&) = delete;
    DspNode& operator= (const DspNode&) = delete;

    [[nodiscard]] NodeKind getKind() const noexcept { return kind; }
    [[nodiscard]] const juce::String& getName() const noexcept { return name; }
    void setName (juce::String newName) { name = std::move (newName); }

    [[nodiscard]] int getNumInputPorts() const noexcept;
    [[nodiscard]] int getNumOutputPorts() const noexcept;
    [[nodiscard]] const PortInfo& getInputPort (int index) const;
    [[nodiscard]] const PortInfo& getOutputPort (int index) const;

    [[nodiscard]] int getNumParameters() const noexcept;
    [[nodiscard]] DspParameter& getParameter (int index) const;

    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;
    void render (const juce::AudioBuffer<float>& inputs,
                 juce::AudioBuffer<float>& outputs,
                 int numSamples) noexcept;
    void renderFeedbackWrite (const juce::AudioBuffer<float>& inputs, int numSamples) noexcept;

    [[nodiscard]] bool isFeedbackGuard() const noexcept { return kind == NodeKind::feedbackGuard; }
    [[nodiscard]] int latencySamples() const noexcept { return reportedLatencySamples; }

    /** Pedal-style bypass. Safe to toggle from the message thread while audio
        runs; the callback reads it atomically. Guards and hardware endpoints
        are never bypassable. */
    [[nodiscard]] bool isBypassable() const noexcept
    {
        return kind != NodeKind::feedbackGuard
            && kind != NodeKind::hardwareInput
            && kind != NodeKind::hardwareOutput;
    }
    [[nodiscard]] bool isBypassed() const noexcept { return bypassed.load (std::memory_order_relaxed); }
    void setBypassed (bool shouldBypass) noexcept
    {
        bypassed.store (shouldBypass && isBypassable(), std::memory_order_relaxed);
    }

    /** Kind-specific state beyond parameters (script text, model paths).
        Called on the message thread only; may allocate. */
    [[nodiscard]] virtual juce::var getExtraState() const { return {}; }
    virtual void setExtraState (const juce::var&) {}

    /** Kind-specific UI commands ("rec", "play", "arm1"...). Message thread
        only; implementations communicate with the callback through atomics. */
    virtual bool handleUiCommand (const juce::String&) { return false; }
    [[nodiscard]] virtual bool uiToggleState (const juce::String&) const { return false; }

    [[nodiscard]] WaveformSnapshot inputWaveform() const noexcept;
    [[nodiscard]] WaveformSnapshot outputWaveform (int port = 0) const noexcept;
    [[nodiscard]] float outputRms (int port = 0) const noexcept;
    [[nodiscard]] float outputPeak (int port = 0) const noexcept;

    [[nodiscard]] virtual int currentStep() const noexcept { return -1; }
    [[nodiscard]] virtual float gainReductionDb() const noexcept { return 0.0f; }
    [[nodiscard]] virtual bool safetyTripped() const noexcept { return false; }
    virtual void resetSafety() noexcept {}
    [[nodiscard]] virtual juce::String statusText() const { return {}; }

protected:
    PortInfo& addInputPort (juce::String name, SignalType type);
    PortInfo& addOutputPort (juce::String name, SignalType type);
    DspParameter& addParameter (juce::String stableId,
                                juce::String displayName,
                                juce::String unit,
                                juce::NormalisableRange<float> range,
                                float defaultValue,
                                float defaultModulationDepth = 0.5f,
                                bool modulatable = true);

    [[nodiscard]] float parameterValue (int parameterIndex,
                                        const juce::AudioBuffer<float>& inputs,
                                        int sampleIndex) noexcept;
    void setReportedLatencySamples (int samples) noexcept { reportedLatencySamples = juce::jmax (0, samples); }

    virtual void prepareDsp (double sampleRate, int maximumBlockSize) = 0;
    virtual void resetDsp() noexcept = 0;
    virtual void processDsp (const juce::AudioBuffer<float>& inputs,
                             juce::AudioBuffer<float>& outputs,
                             int numSamples) noexcept = 0;
    virtual void processFeedbackWriteDsp (const juce::AudioBuffer<float>&, int) noexcept {}

private:
    NodeKind kind;
    juce::String name;
    std::vector<PortInfo> inputPorts;
    std::vector<PortInfo> outputPorts;
    std::vector<std::unique_ptr<DspParameter>> parameters;
    SignalMeter primaryInputMeter;
    std::vector<std::unique_ptr<SignalMeter>> outputMeters;
    int reportedLatencySamples = 0;
    std::atomic<bool> bypassed { false };
};

struct NodeModel
{
    NodeId id = 0;
    std::shared_ptr<DspNode> processor;
    juce::Point<float> position;
    bool hardware = false;
};

struct Connection
{
    NodeId sourceNode = 0;
    int sourcePort = 0;
    NodeId destinationNode = 0;
    int destinationPort = 0;

    [[nodiscard]] bool operator== (const Connection& other) const noexcept
    {
        return sourceNode == other.sourceNode
            && sourcePort == other.sourcePort
            && destinationNode == other.destinationNode
            && destinationPort == other.destinationPort;
    }
};

class PatchDocument
{
public:
    static constexpr NodeId hardwareInputId = 1;
    static constexpr NodeId hardwareOutputId = 2;

    PatchDocument();

    void configureHardware (const juce::StringArray& inputNames,
                            const juce::StringArray& outputNames,
                            const std::vector<int>& inputCallbackChannels = {},
                            const std::vector<int>& outputCallbackChannels = {});
    void prepareAll (double sampleRate, int maximumBlockSize);

    NodeId addNode (NodeKind kind, juce::Point<float> position, std::optional<NodeId> requestedId = std::nullopt);
    bool removeNode (NodeId id);
    juce::Result addConnection (Connection connection);
    bool removeConnection (const Connection& connection);
    void clearUserPatch();

    [[nodiscard]] NodeModel* findNode (NodeId id) noexcept;
    [[nodiscard]] const NodeModel* findNode (NodeId id) const noexcept;
    [[nodiscard]] std::vector<NodeModel>& getNodes() noexcept { return nodes; }
    [[nodiscard]] const std::vector<NodeModel>& getNodes() const noexcept { return nodes; }
    [[nodiscard]] const std::vector<Connection>& getConnections() const noexcept { return connections; }

    [[nodiscard]] juce::var toJson() const;
    juce::Result loadJson (const juce::var& value);

private:
    std::vector<NodeModel> nodes;
    std::vector<Connection> connections;
    NodeId nextNodeId = 100;
    double currentSampleRate = 48000.0;
    int currentMaximumBlockSize = 128;
    bool hardwareLayoutConfigured = false;
};

class RenderPlan
{
public:
    ~RenderPlan();

    RenderPlan (const RenderPlan&) = delete;
    RenderPlan& operator= (const RenderPlan&) = delete;

    void render (const float* const* hardwareInputs,
                 int numHardwareInputs,
                 float* const* hardwareOutputs,
                 int numHardwareOutputs,
                 int hardwareOffset,
                 int numSamples) noexcept;

    [[nodiscard]] int getMaximumBlockSize() const noexcept { return maximumBlockSize; }
    [[nodiscard]] int getGraphLatencySamples() const noexcept { return graphLatencySamples; }

    RenderPlan* retiredNext = nullptr;
    bool makesDeviceReady = false;

private:
    struct SourceRef
    {
        int nodeIndex = -1;
        int portIndex = -1;
    };

    struct RenderNode
    {
        std::shared_ptr<DspNode> processor;
        juce::AudioBuffer<float> inputs;
        juce::AudioBuffer<float> outputs;
        std::vector<std::vector<SourceRef>> incoming;
    };

    explicit RenderPlan (int maximumBlockSizeToUse);
    void mixInputs (int nodeIndex, int numSamples) noexcept;

    std::vector<std::unique_ptr<RenderNode>> renderNodes;
    std::vector<int> executionOrder;
    std::vector<int> feedbackNodes;
    int hardwareInputNode = -1;
    int hardwareOutputNode = -1;
    int maximumBlockSize = 128;
    int graphLatencySamples = 0;

    friend class GraphCompiler;
};

struct GraphCompileResult
{
    std::unique_ptr<RenderPlan> plan;
    juce::String error;

    [[nodiscard]] bool succeeded() const noexcept { return plan != nullptr; }
};

class GraphCompiler
{
public:
    static GraphCompileResult compile (const PatchDocument& document,
                                       int maximumBlockSize,
                                       bool makesDeviceReady = false);
};
} // namespace signalpatch
