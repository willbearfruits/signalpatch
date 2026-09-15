#pragma once

#include "Graph.h"
#include "PatchHistory.h"
#include "MidiMap.h"
#include "ControllerFeedback.h"

#include <array>
#include <atomic>
#include <functional>
#include <unordered_map>

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
    // False when the callback thread runs under the ordinary scheduler
    // (no rtkit / realtime-privileges): expect xruns whenever the desktop is busy.
    bool realtimeThread = true;
};

class PatchEngine final : private juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::AsyncUpdater,
                          private juce::Timer,
                          private juce::MidiInputCallback,
                          public juce::ChangeBroadcaster
{
public:
    // ---- MIDI control ----
    // Every available MIDI input is opened; messages hop to the message
    // thread and are applied against the patch's mappings. The UI can arm a
    // learn hook (it sees the next mapped-able message first) and receives
    // slot / group targets it owns through onMidiUiTarget.
    void setMidiMappings (std::vector<MidiMapping> mappings);        // undoable, broadcasts
    void applyMidiMappingsJson (const juce::var& mappings);           // slot glides adopt the target map
    [[nodiscard]] const std::vector<MidiMapping>& getMidiMappings() const noexcept { return document.getMidiMappings(); }
    std::function<bool (const juce::MidiMessage&)> midiLearnHook;     // return true to consume
    std::function<void (const MidiMapping&, const juce::MidiMessage&)> onMidiUiTarget;
    std::function<void (NodeId)> onParameterChangedByMidi;           // UI plate refresh
    [[nodiscard]] juce::StringArray getOpenMidiInputNames() const;
    [[nodiscard]] bool hasMidiInputs() const noexcept { return midiInputsOpen > 0; }
    [[nodiscard]] juce::String getLastMidiDescription() const { return lastMidiDescription; }
    // Controller feedback (docs/CONTROLLER.md): a device that says hello on
    // its input gets its same-named output opened and receives switch LEDs,
    // labels, the live slot and the rig name, diffed on the engine timer.
    void setControllerContext (int activeSlot, const juce::String& rigName);
    [[nodiscard]] int getControllerCount() const noexcept { return static_cast<int> (controllerOutputs.size()); }

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
    void beginCompoundEditGesture (const juce::String& description) { history.beginCompoundGesture (description); }

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
    /** Pedalboard placement; nullopt returns the module to the auto layout. */
    void setBoardPosition (NodeId id, std::optional<juce::Point<float>> position);
    /** Replaces the pedal groups (undoable, broadcasts). */
    void setGroups (std::vector<PedalGroup> groups);
    void applyGroupsJson (const juce::var& groups);
    /** Parameter change that leaves no undo entry: for glides and automation. */
    void setParameterNoHistory (NodeId id, int parameterIndex, float value);
    void setModulationDepthNoHistory (NodeId id, int parameterIndex, float depth);
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
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleMidiOnMessageThread (const juce::MidiMessage& message);
    void refreshMidiInputs();
    void applyMidiMapping (const MidiMapping& mapping, const juce::MidiMessage& message);
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
    std::atomic<int> callbackScheduler { -1 }; // sched_getscheduler() of the callback thread, sampled once
    std::atomic<double> currentSampleRate { 48000.0 };
    std::array<char, 512> pendingDeviceErrorText {};
    std::atomic<int> pendingDeviceErrorLength { 0 }; // 0 empty, -1 writer/reader owns the buffer.

    float audioThreadMasterGain = 1.0f;
    juce::String graphMessage;
    juce::String deviceError;
    bool initialised = false;
    bool modifiedSinceSave = false;
    std::unordered_map<NodeId, juce::uint32> savedAudioVersions;    // per explicit save target
    std::unordered_map<NodeId, juce::uint32> autosavedAudioVersions; // per autosave
    juce::File savedAudioTarget;
    struct MidiNoteEvent { int channel = 0, note = 0, velocity = 0; bool on = false; };
    juce::AbstractFifo midiNoteFifo { 256 };
    std::array<MidiNoteEvent, 256> midiNoteEvents {};
    int midiInputsOpen = 0;
    int midiRefreshCountdown = 0;
    juce::String lastMidiDescription;
    std::unordered_map<juce::int64, bool> midiCommandGate; // rising-edge detection per (mapping index)
    void openControllerOutput (const juce::String& inputName);
    void sendControllerFeedback (bool full);
    std::vector<std::unique_ptr<juce::MidiOutput>> controllerOutputs;
    controller::State controllerState;
    int controllerSlot = -1;
    juce::String controllerRig;
    bool audioCallbackRegistered = false;
    bool restoredAudioDeviceState = false;
    juce::String configuredDeviceSignature;
    bool graphSwapFadingOut = false; // Audio-thread owned.
    bool documentDirty = false;
    juce::int64 lastDocumentChangeMs = 0;
};
} // namespace signalpatch
