#pragma once

#include "Graph.h"
#include "PatchHistory.h"

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
    float cpuPeak = 0.0f;
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

    // Undo/redo over every edit above (message thread). Both return false
    // when there is nothing to apply; the description names the edit.
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return history.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history.canRedo(); }
    [[nodiscard]] juce::String getUndoDescription() const { return history.getUndoDescription(); }
    [[nodiscard]] juce::String getRedoDescription() const { return history.getRedoDescription(); }
    void closeEditGesture() noexcept { history.closeGesture(); }

    // Patch files store asset paths (NAM models, cab impulses) relative to
    // the patch's folder when they live under it, absolute otherwise. A
    // folder with the patch plus an assets/ subfolder is therefore already a
    // portable project; exportBundle builds one and zips it.
    juce::Result savePatch (const juce::File& file);
    juce::Result loadPatch (const juce::File& file);
    /** Merges another patch's nodes and cables into the current one (undoable). */
    juce::Result importPatch (const juce::File& file);
    /** Writes <name>.zip containing <name>/<name>.signalpatch + assets/. */
    juce::Result exportBundle (const juce::File& zipFile);
    /** Unzips a bundle under destinationRoot and returns its patch file. */
    static juce::Result extractBundle (const juce::File& zipFile, const juce::File& destinationRoot, juce::File& patchFileOut);
    void createDefaultPatch();
    /** File > New: back to the default rig, history cleared, nothing unsaved. */
    void newPatch();
    /** Edits since the last save/load/new (the autosave keeps its own flag). */
    [[nodiscard]] bool hasUnsavedChanges() const noexcept { return modifiedSinceSave; }

    juce::Result renameNode (NodeId id, const juce::String& newName);
    /** Copies a node (parameters, mod depths, bypass, extra state) next to the
        original. Returns the new id, or 0. */
    NodeId duplicateNode (NodeId id);

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
    void markDocumentEdited();
    void writeAutosaveIfDue();
    juce::File autosaveFile() const;
    juce::File audioStateFile() const;
    void saveAudioDeviceState();
    [[nodiscard]] juce::String currentDeviceSignature();

    juce::AudioDeviceManager deviceManager;
    PatchDocument document;
    PatchHistory history { document };

    std::atomic<RenderPlan*> pendingPlan { nullptr };
    std::atomic<RenderPlan*> retiredPlans { nullptr };
    RenderPlan* activePlan = nullptr; // Audio thread owns this while the callback is installed.

    std::atomic<bool> deviceReady { false };
    std::atomic<bool> panicMuted { false };
    std::atomic<bool> callbackRunning { false };
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<float> cpuPeak { 0.0f };
    std::atomic<int> graphLatencySamples { 0 };
    std::atomic<int> currentInputChannels { 0 };
    std::atomic<int> currentOutputChannels { 0 };
    std::atomic<int> currentBufferSize { 128 };
    std::atomic<int> observedBlockSize { 0 }; // what the callback really gets (pipewire-jack reports its max quantum)
    std::atomic<double> currentSampleRate { 48000.0 };
    std::array<char, 512> pendingDeviceErrorText {};
    std::atomic<int> pendingDeviceErrorLength { 0 }; // 0 empty, -1 writer/reader owns the buffer.

    float audioThreadMasterGain = 1.0f;
    juce::String graphMessage;
    juce::String deviceError;
    bool initialised = false;
    bool modifiedSinceSave = false;
    bool audioCallbackRegistered = false;
    bool restoredAudioDeviceState = false;
    juce::String configuredDeviceSignature;
    bool graphSwapFadingOut = false; // Audio-thread owned.
    bool documentDirty = false;
    juce::int64 lastDocumentChangeMs = 0;
};
} // namespace signalpatch
