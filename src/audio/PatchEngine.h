#pragma once

#include "Graph.h"

#include <array>
#include <atomic>

namespace signalpatch
{
struct EngineStatus
{
    juce::String deviceName;
    juce::String backendName;
    juce::String graphMessage;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int inputChannels = 0;
    int outputChannels = 0;
    int graphLatencySamples = 0;
    int xruns = -1;
    float cpuLoad = 0.0f;
    bool running = false;
    bool panicMuted = false;
};

class PatchEngine final : private juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::AsyncUpdater,
                          private juce::Timer,
                          public juce::ChangeBroadcaster
{
public:
    PatchEngine();
    ~PatchEngine() override;

    PatchEngine (const PatchEngine&) = delete;
    PatchEngine& operator= (const PatchEngine&) = delete;

    juce::Result initialise();
    void shutdown();

    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    [[nodiscard]] const PatchDocument& getDocument() const noexcept { return document; }
    [[nodiscard]] EngineStatus getStatus() const;

    NodeId addNode (NodeKind kind, juce::Point<float> position);
    bool removeNode (NodeId id);
    juce::Result connect (Connection connection);
    bool disconnect (const Connection& connection);
    void moveNode (NodeId id, juce::Point<float> position);
    void setParameter (NodeId id, int parameterIndex, float value);
    void setModulationDepth (NodeId id, int parameterIndex, float depth);
    void resetNodeSafety (NodeId id);
    void setNodeBypassed (NodeId id, bool bypassed);
    bool sendNodeCommand (NodeId id, const juce::String& command);
    void applyNodeExtraState (NodeId id, const juce::var& state);

    void setPanicMuted (bool shouldMute) noexcept;
    void togglePanic() noexcept;
    [[nodiscard]] bool isPanicMuted() const noexcept;

    juce::Result savePatch (const juce::File& file) const;
    juce::Result loadPatch (const juce::File& file);
    void createDefaultPatch();

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    juce::Result openDefaultDevice();
    void applyPreferredCaptureDevice();
    void configureAllAvailableChannels();
    void rebuildForCurrentDevice (bool markDeviceReady);
    bool compileAndPublish (bool markDeviceReady, bool markDirty = true);
    void publishPlan (std::unique_ptr<RenderPlan> plan) noexcept;
    void retirePlanFromAudioThread (RenderPlan* plan) noexcept;
    void reclaimRetiredPlans() noexcept;
    void writeAutosaveIfDue();
    juce::File autosaveFile() const;
    juce::File audioStateFile() const;
    void saveAudioDeviceState();
    [[nodiscard]] juce::String currentDeviceSignature();

    juce::AudioDeviceManager deviceManager;
    PatchDocument document;

    std::atomic<RenderPlan*> pendingPlan { nullptr };
    std::atomic<RenderPlan*> retiredPlans { nullptr };
    RenderPlan* activePlan = nullptr; // Audio thread owns this while the callback is installed.

    std::atomic<bool> deviceReady { false };
    std::atomic<bool> panicMuted { false };
    std::atomic<bool> callbackRunning { false };
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<int> graphLatencySamples { 0 };
    std::atomic<int> currentInputChannels { 0 };
    std::atomic<int> currentOutputChannels { 0 };
    std::atomic<int> currentBufferSize { 128 };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::array<char, 512> pendingDeviceErrorText {};
    std::atomic<int> pendingDeviceErrorLength { 0 }; // 0 empty, -1 writer/reader owns the buffer.

    float audioThreadMasterGain = 1.0f;
    juce::String graphMessage;
    juce::String deviceError;
    bool initialised = false;
    bool audioCallbackRegistered = false;
    bool restoredAudioDeviceState = false;
    juce::String configuredDeviceSignature;
    bool graphSwapFadingOut = false; // Audio-thread owned.
    bool documentDirty = false;
    juce::int64 lastDocumentChangeMs = 0;
};
} // namespace signalpatch
