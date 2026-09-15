#include "PatchEngine.h"

#if JUCE_LINUX
 #include <sched.h>
#endif
#include "PatchBundle.h"

#include <cmath>

namespace signalpatch
{
namespace
{
    // Matches the Zoom F4 across backends: PipeWire/JACK exposes it as
    // "Zoom F4 Pro", raw ALSA as "H and F Series Multi Track Usb".
    bool looksLikePreferredInterface (const juce::String& deviceName)
    {
        const auto lower = deviceName.toLowerCase();
        return lower.contains ("zoom") || lower.contains ("f4") || lower.contains ("h and f");
    }
} // namespace

PatchEngine::PatchEngine() = default;

PatchEngine::~PatchEngine()
{
    shutdown();
}

juce::Result PatchEngine::initialise()
{
    if (initialised)
        return juce::Result::ok();

    deviceManager.addMidiInputDeviceCallback ({}, this); // every enabled input
    refreshMidiInputs();
    auto deviceResult = openDefaultDevice();
    if (! restoredAudioDeviceState)
        configureAllAvailableChannels();
    rebuildForCurrentDevice (false);

    if (document.getConnections().empty())
    {
        // Autosave recovery: resume the last session, muted for safety, and
        // fall back to the default patch on any parse or compile failure.
        bool restored = false;
        const auto autosave = autosaveFile();
        if (autosave.existsAsFile())
        {
            auto parsed = juce::JSON::parse (autosave);
            if (! parsed.isVoid())
                bundle::rebaseAssetPaths (parsed, autosave.getParentDirectory(), false);
            if (! parsed.isVoid()
                && document.loadJson (parsed).wasOk()
                && ! document.getConnections().empty())
            {
                bundle::loadAudioContent (document, parsed, autosave.getParentDirectory());
                setPanicMuted (true);
                restored = compileAndPublish (false, false);
                if (restored)
                    graphMessage = "Restored last session muted - press MUTED to fade in";
            }
        }
        if (! restored)
        {
            document.clearUserPatch();
            setPanicMuted (false);
            createDefaultPatch();
        }
    }
    else
    {
        compileAndPublish (false, false);
    }

    deviceManager.addChangeListener (this);
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        audioThreadMasterGain = 0.0f;
        deviceReady.store (true, std::memory_order_release);
        deviceManager.addAudioCallback (this);
        audioCallbackRegistered = true;
    }

    startTimerHz (20);
    initialised = true;
    return deviceResult;
}

void PatchEngine::shutdown()
{
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    if (! initialised && activePlan == nullptr && pendingPlan.load() == nullptr)
        return;

    stopTimer();
    cancelPendingUpdate();
    deviceManager.removeChangeListener (this);
    deviceReady.store (false, std::memory_order_release);
    if (audioCallbackRegistered)
    {
        deviceManager.removeAudioCallback (this);
        audioCallbackRegistered = false;
    }
    callbackRunning.store (false, std::memory_order_release);

    writeAutosaveIfDue();
    saveAudioDeviceState();

    delete pendingPlan.exchange (nullptr, std::memory_order_acq_rel);
    delete activePlan;
    activePlan = nullptr;
    reclaimRetiredPlans();
    deviceManager.closeAudioDevice();
    initialised = false;
}

juce::Result PatchEngine::openDefaultDevice()
{
    std::unique_ptr<juce::XmlElement> savedState;
    const auto stateFile = audioStateFile();
    if (stateFile.existsAsFile())
        savedState = juce::parseXML (stateFile);

    auto error = deviceManager.initialise (2, 2, savedState.get(), true, {}, nullptr);
    if (error.isNotEmpty())
    {
        deviceError = error;
        return juce::Result::fail (error);
    }

    deviceError.clear();
    restoredAudioDeviceState = savedState != nullptr;
    if (! restoredAudioDeviceState)
        applyPreferredCaptureDevice();
    return juce::Result::ok();
}

void PatchEngine::applyPreferredCaptureDevice()
{
    // Shapes the first launch only; once a device choice has been saved the
    // restored state wins and this is never reached.
    // Try JACK before raw ALSA: on a PipeWire desktop the interface's ALSA
    // playback side is held by the sound server, so a raw ALSA open "succeeds"
    // with inputs only and the rig is silent.
    juce::Array<juce::AudioIODeviceType*> orderedTypes;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type != nullptr)
            (type->getTypeName() == "JACK" ? orderedTypes.insert (0, type) : orderedTypes.add (type));

    for (auto* type : orderedTypes)
    {
        type->scanForDevices();
        for (const auto& inputName : type->getDeviceNames (true))
        {
            if (! looksLikePreferredInterface (inputName))
                continue;

            deviceManager.setCurrentAudioDeviceType (type->getTypeName(), true);
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.inputDeviceName = inputName;
            for (const auto& outputName : type->getDeviceNames (false))
                if (looksLikePreferredInterface (outputName))
                {
                    setup.outputDeviceName = outputName;
                    break;
                }
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;
            if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty())
            {
                if (auto* device = deviceManager.getCurrentAudioDevice();
                    device != nullptr && device->getActiveOutputChannels().countNumberOfSetBits() > 0)
                    return; // The preferred interface is open with outputs; stop searching.
                deviceManager.closeAudioDevice(); // Inputs only: keep looking on another backend.
            }
        }
    }
}

void PatchEngine::configureAllAvailableChannels()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto inputNames = device->getInputChannelNames();
    const auto outputNames = device->getOutputChannelNames();
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    if (! inputNames.isEmpty())
        setup.inputChannels.setRange (0, inputNames.size(), true);
    if (! outputNames.isEmpty())
        setup.outputChannels.setRange (0, outputNames.size(), true);

    const auto rates = device->getAvailableSampleRates();
    if (rates.contains (48000.0))
        setup.sampleRate = 48000.0;
    const auto bufferSizes = device->getAvailableBufferSizes();
    if (bufferSizes.contains (64))
        setup.bufferSize = 64;

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        deviceError = "Could not activate every device channel: " + error;
}

void PatchEngine::rebuildForCurrentDevice (bool markDeviceReady)
{
    history.clear();
    auto* device = deviceManager.getCurrentAudioDevice();
    juce::StringArray inputNames;
    juce::StringArray outputNames;
    std::vector<int> inputCallbackChannels;
    std::vector<int> outputCallbackChannels;
    double sampleRate = 48000.0;
    int blockSize = 128;
    int activeInputCount = 0;
    int activeOutputCount = 0;

    if (device != nullptr)
    {
        const auto allInputNames = device->getInputChannelNames();
        const auto allOutputNames = device->getOutputChannelNames();
        const auto activeInputs = device->getActiveInputChannels();
        const auto activeOutputs = device->getActiveOutputChannels();
        for (int channel = 0; channel < allInputNames.size(); ++channel)
        {
            inputNames.add (allInputNames[channel]);
            inputCallbackChannels.push_back (activeInputs[channel] ? activeInputCount++ : -1);
        }
        for (int channel = 0; channel < allOutputNames.size(); ++channel)
        {
            outputNames.add (allOutputNames[channel]);
            outputCallbackChannels.push_back (activeOutputs[channel] ? activeOutputCount++ : -1);
        }

        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    currentInputChannels.store (activeInputCount, std::memory_order_relaxed);
    currentOutputChannels.store (activeOutputCount, std::memory_order_relaxed);
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBufferSize.store (juce::jmax (1, blockSize), std::memory_order_relaxed);

    document.configureHardware (inputNames, outputNames, inputCallbackChannels, outputCallbackChannels);
    document.prepareAll (sampleRate, juce::jmax (1, blockSize));
    compileAndPublish (markDeviceReady, false);
    configuredDeviceSignature = currentDeviceSignature();
    sendChangeMessage();
}

bool PatchEngine::compileAndPublish (bool markDeviceReady, bool markDirty)
{
    auto compiled = GraphCompiler::compile (document,
                                            juce::jmax (1, currentBufferSize.load (std::memory_order_relaxed)),
                                            markDeviceReady);
    if (! compiled.succeeded())
    {
        graphMessage = compiled.error;
        sendChangeMessage();
        return false;
    }

    graphMessage = "Graph live";
    graphLatencySamples.store (compiled.plan->getGraphLatencySamples(), std::memory_order_relaxed);
    publishPlan (std::move (compiled.plan));
    if (markDirty)
    {
        documentDirty = modifiedSinceSave = true;
        lastDocumentChangeMs = juce::Time::currentTimeMillis();
    }
    sendChangeMessage();
    return true;
}

void PatchEngine::publishPlan (std::unique_ptr<RenderPlan> plan) noexcept
{
    auto* rawPlan = plan.release();
    auto* replacedPending = pendingPlan.exchange (rawPlan, std::memory_order_acq_rel);
    delete replacedPending; // A plan still in pendingPlan has never been seen by the callback.
}

void PatchEngine::retirePlanFromAudioThread (RenderPlan* plan) noexcept
{
    if (plan == nullptr)
        return;

    auto* head = retiredPlans.load (std::memory_order_relaxed);
    do
    {
        plan->retiredNext = head;
    }
    while (! retiredPlans.compare_exchange_weak (head, plan,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
}

void PatchEngine::reclaimRetiredPlans() noexcept
{
    auto* plan = retiredPlans.exchange (nullptr, std::memory_order_acq_rel);
    while (plan != nullptr)
    {
        auto* next = plan->retiredNext;
        delete plan;
        plan = next;
    }
}

void PatchEngine::markDocumentEdited()
{
    documentDirty = modifiedSinceSave = true;
    lastDocumentChangeMs = juce::Time::currentTimeMillis();
}

NodeId PatchEngine::addNode (NodeKind kind, juce::Point<float> position)
{
    const auto id = document.addNode (kind, position);
    if (id != 0)
    {
        history.recordNodeAdded (id);
        compileAndPublish (false);
    }
    return id;
}

bool PatchEngine::removeNode (NodeId id)
{
    if (! history.removeNode (id))
        return false;
    compileAndPublish (false);
    return true;
}

juce::Result PatchEngine::connect (Connection connection)
{
    const auto result = document.addConnection (connection);
    if (result.failed())
        return result;
    if (! compileAndPublish (false))
    {
        // The last valid graph keeps running, but the rejected cable must not
        // linger in the document or every later edit fails to compile too.
        document.removeConnection (connection);
        return juce::Result::fail (graphMessage);
    }
    history.recordConnected (connection);
    return juce::Result::ok();
}

bool PatchEngine::disconnect (const Connection& connection)
{
    if (! document.removeConnection (connection))
        return false;
    history.recordDisconnected (connection);
    compileAndPublish (false);
    return true;
}

void PatchEngine::moveNode (NodeId id, juce::Point<float> position)
{
    if (auto* node = document.findNode (id))
    {
        history.recordMove (id, node->position, position);
        node->position = position;
        markDocumentEdited();
    }
}

void PatchEngine::setParameter (NodeId id, int parameterIndex, float value)
{
    if (auto* node = document.findNode (id))
    {
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            auto& parameter = node->processor->getParameter (parameterIndex);
            history.recordParameter (id, parameterIndex, parameter.getValue(), value);
            parameter.setValue (value);
            markDocumentEdited();
        }
    }
}

void PatchEngine::setModulationDepth (NodeId id, int parameterIndex, float depth)
{
    if (auto* node = document.findNode (id))
    {
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            auto& parameter = node->processor->getParameter (parameterIndex);
            history.recordModulationDepth (id, parameterIndex, parameter.getModulationDepth(), depth);
            parameter.setModulationDepth (depth);
            markDocumentEdited();
        }
    }
}

bool PatchEngine::undo()
{
    const auto applied = history.undo();
    if (applied == PatchHistory::Applied::none)
        return false;
    if (applied == PatchHistory::Applied::structure)
        compileAndPublish (false);
    else
    {
        markDocumentEdited();
        sendChangeMessage();
    }
    return true;
}

bool PatchEngine::redo()
{
    const auto applied = history.redo();
    if (applied == PatchHistory::Applied::none)
        return false;
    if (applied == PatchHistory::Applied::structure)
        compileAndPublish (false);
    else
    {
        markDocumentEdited();
        sendChangeMessage();
    }
    return true;
}

void PatchEngine::resetNodeSafety (NodeId id)
{
    if (auto* node = document.findNode (id))
        node->processor->resetSafety();
}

void PatchEngine::setNodeBypassed (NodeId id, bool bypassed)
{
    if (auto* node = document.findNode (id))
    {
        // Atomic flag read by the callback; no recompilation required.
        history.recordBypass (id, node->processor->isBypassed(), bypassed);
        node->processor->setBypassed (bypassed);
        documentDirty = modifiedSinceSave = true;
        lastDocumentChangeMs = juce::Time::currentTimeMillis();
        sendChangeMessage();
    }
}

bool PatchEngine::sendNodeCommand (NodeId id, const juce::String& command)
{
    if (auto* node = document.findNode (id))
        return node->processor->handleUiCommand (command);
    return false;
}

void PatchEngine::applyNodeExtraState (NodeId id, const juce::var& state)
{
    if (auto* node = document.findNode (id))
    {
        history.recordExtraState (id, node->processor->getExtraState(), state);
        node->processor->setExtraState (state);
        documentDirty = modifiedSinceSave = true;
        lastDocumentChangeMs = juce::Time::currentTimeMillis();
        sendChangeMessage();
    }
}

void PatchEngine::setPanicMuted (bool shouldMute) noexcept
{
    panicMuted.store (shouldMute, std::memory_order_release);
}

void PatchEngine::togglePanic() noexcept
{
    setPanicMuted (! isPanicMuted());
}

bool PatchEngine::isPanicMuted() const noexcept
{
    return panicMuted.load (std::memory_order_acquire);
}

EngineStatus PatchEngine::getStatus() const
{
    EngineStatus status;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        status.deviceName = device->getName();
        status.backendName = device->getTypeName();
        status.xruns = device->getXRunCount();
    }
    else
    {
        status.deviceName = "Offline";
        status.backendName = "No audio backend";
    }
    status.sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    {
        const auto observed = observedBlockSize.load (std::memory_order_relaxed);
        status.bufferSize = observed > 0 ? observed : currentBufferSize.load (std::memory_order_relaxed);
    }
    status.inputChannels = currentInputChannels.load (std::memory_order_relaxed);
    status.outputChannels = currentOutputChannels.load (std::memory_order_relaxed);
    status.graphLatencySamples = graphLatencySamples.load (std::memory_order_relaxed);
    status.cpuLoad = cpuLoad.load (std::memory_order_relaxed);
    status.cpuPeak = cpuPeak.load (std::memory_order_relaxed);
    status.running = callbackRunning.load (std::memory_order_relaxed);
   #if JUCE_LINUX
    {
        const auto scheduler = callbackScheduler.load (std::memory_order_relaxed);
        status.realtimeThread = scheduler < 0 || scheduler == SCHED_FIFO || scheduler == SCHED_RR;
    }
   #endif
    status.panicMuted = isPanicMuted();
    status.graphMessage = graphMessage;
    if (deviceError.isNotEmpty())
        status.graphMessage = deviceError + (graphMessage.isNotEmpty() ? " | " + graphMessage : juce::String());
    return status;
}

juce::Result PatchEngine::savePatch (const juce::File& file)
{
    if (file == juce::File())
        return juce::Result::fail ("No patch file selected.");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create the patch directory.");
    if (file != savedAudioTarget)
    {
        savedAudioVersions.clear(); // a new target needs its own audio files
        savedAudioTarget = file;
    }
    auto json = bundle::toJsonWithAudio (document, file, savedAudioVersions);
    if (! file.replaceWithText (juce::JSON::toString (json, true)))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    modifiedSinceSave = false;
    return juce::Result::ok();
}

juce::Result PatchEngine::importPatch (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("Patch file not found.");
    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("The patch JSON is invalid.");
    bundle::rebaseAssetPaths (parsed, file.getParentDirectory(), false);

    std::vector<NodeId> addedNodes;
    std::vector<Connection> addedCables;
    const auto result = document.mergeJson (parsed, { 60.0f, 60.0f }, addedNodes, addedCables);
    if (result.failed())
        return result;
    for (const auto id : addedNodes)
        history.recordNodeAdded (id);
    for (const auto& cable : addedCables)
        history.recordConnected (cable);
    if (! compileAndPublish (false))
    {
        // Roll the merge back rather than leave an uncompilable document.
        for (const auto& cable : addedCables)
            document.removeConnection (cable);
        for (const auto id : addedNodes)
            document.removeNode (id);
        history.clear();
        compileAndPublish (false);
        return juce::Result::fail ("Imported patch could not be merged: " + graphMessage);
    }
    return juce::Result::ok();
}

juce::Result PatchEngine::exportBundle (const juce::File& zipFile)
{
    return bundle::exportBundle (document, zipFile);
}

juce::Result PatchEngine::extractBundle (const juce::File& zipFile, const juce::File& destinationRoot, juce::File& patchFileOut)
{
    return bundle::extractBundle (zipFile, destinationRoot, patchFileOut);
}

void PatchEngine::newPatch()
{
    setPanicMuted (true);
    createDefaultPatch();
    modifiedSinceSave = false;
}

juce::Result PatchEngine::renameNode (NodeId id, const juce::String& newName)
{
    auto* node = document.findNode (id);
    if (node == nullptr)
        return juce::Result::fail ("That node no longer exists.");
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail ("A node needs a name.");
    history.recordRename (id, node->processor->getName(), trimmed);
    node->processor->setName (trimmed);
    markDocumentEdited();
    sendChangeMessage();
    return juce::Result::ok();
}

void PatchEngine::setBoardPosition (NodeId id, std::optional<juce::Point<float>> position)
{
    if (auto* node = document.findNode (id))
    {
        history.recordBoardMove (id, node->boardPosition, position);
        node->boardPosition = position;
        markDocumentEdited();
    }
}

void PatchEngine::setGroups (std::vector<PedalGroup> groups)
{
    const auto before = document.groupsToJson();
    document.setGroups (std::move (groups));
    history.recordGroups (before, document.groupsToJson());
    markDocumentEdited();
    sendChangeMessage();
}

void PatchEngine::setMidiMappings (std::vector<MidiMapping> mappings)
{
    const auto before = midiMappingsToJson (document.getMidiMappings());
    document.setMidiMappings (std::move (mappings));
    history.recordMidi (before, midiMappingsToJson (document.getMidiMappings()));
    markDocumentEdited();
    sendChangeMessage();
}

void PatchEngine::applyMidiMappingsJson (const juce::var& mappings)
{
    const auto before = midiMappingsToJson (document.getMidiMappings());
    document.setMidiMappings (midiMappingsFromJson (mappings));
    history.recordMidi (before, midiMappingsToJson (document.getMidiMappings()));
    markDocumentEdited();
    sendChangeMessage();
}

juce::StringArray PatchEngine::getOpenMidiInputNames() const
{
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);
    return names;
}

void PatchEngine::refreshMidiInputs()
{
    // Open everything that is plugged in; a hot-plugged controller shows up
    // on the next refresh (timer, every few seconds).
    int open = 0;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        if (! deviceManager.isMidiInputDeviceEnabled (device.identifier))
            deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            ++open;
    }
    midiInputsOpen = open;
}

void PatchEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    if (message.isSysEx())
    {
        if (controller::isHello (message, controller::fromController) && source != nullptr)
        {
            const auto name = source->getName();
            juce::MessageManager::callAsync ([this, name] { openControllerOutput (name); });
        }
        return;
    }
    // MIDI thread. Notes go straight to the audio thread through a lock-free
    // FIFO (block-accurate, no allocation); everything hops to the message
    // thread too, where mappings and learn live.
    if (message.isNoteOnOrOff())
    {
        const auto scope = midiNoteFifo.write (1);
        if (scope.blockSize1 > 0)
            midiNoteEvents[static_cast<std::size_t> (scope.startIndex1)] = { message.getChannel(), message.getNoteNumber(),
                                                                             message.isNoteOn (true) ? message.getVelocity() : 0,
                                                                             message.isNoteOn (true) };
    }
    juce::MessageManager::callAsync ([this, message] { handleMidiOnMessageThread (message); });
}

void PatchEngine::openControllerOutput (const juce::String& inputName)
{
    for (const auto& output : controllerOutputs)
        if (output->getName() == inputName)
        {
            output->sendMessageNow (controller::helloMessage (controller::fromSignalPatch));
            sendControllerFeedback (true);
            return;
        }
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
    {
        if (device.name != inputName)
            continue;
        if (auto output = juce::MidiOutput::openDevice (device.identifier))
        {
            output->sendMessageNow (controller::helloMessage (controller::fromSignalPatch));
            controllerOutputs.push_back (std::move (output));
            lastMidiDescription = "controller: " + inputName;
            sendControllerFeedback (true);
            sendChangeMessage();
            return;
        }
    }
}

void PatchEngine::setControllerContext (int activeSlot, const juce::String& rigName)
{
    controllerSlot = activeSlot;
    controllerRig = rigName;
    sendControllerFeedback (false);
}

void PatchEngine::sendControllerFeedback (bool full)
{
    if (controllerOutputs.empty())
        return;
    auto next = controller::computeState (document, controllerSlot, controllerRig);
    const auto messages = controller::encode (controllerState, next, full);
    controllerState = std::move (next);
    for (const auto& message : messages)
        for (const auto& output : controllerOutputs)
            output->sendMessageNow (message);
}

void PatchEngine::handleMidiOnMessageThread (const juce::MidiMessage& message)
{
    MidiMapping::Source source;
    int number = 0;
    if (message.isController())            { source = MidiMapping::Source::controlChange; number = message.getControllerNumber(); }
    else if (message.isNoteOn (true))      { source = MidiMapping::Source::note; number = message.getNoteNumber(); }
    else if (message.isNoteOff (true))     { source = MidiMapping::Source::note; number = message.getNoteNumber(); }
    else if (message.isProgramChange())    { source = MidiMapping::Source::programChange; number = message.getProgramChangeNumber(); }
    else
        return;
    lastMidiDescription = (source == MidiMapping::Source::controlChange ? "CC " + juce::String (number) + " = " + juce::String (message.getControllerValue())
                         : source == MidiMapping::Source::note ? "Note " + juce::String (number) + (message.isNoteOn (true) ? " on" : " off")
                                                                : "Program " + juce::String (number))
                        + "  ch " + juce::String (message.getChannel());
    if (midiLearnHook && (message.isController() || message.isNoteOn (true) || message.isProgramChange()))
        if (midiLearnHook (message))
            return;
    const auto& mappings = document.getMidiMappings();
    for (std::size_t index = 0; index < mappings.size(); ++index)
        if (mappings[index].matches (source, message.getChannel(), number))
            applyMidiMapping (mappings[index], message);
}

void PatchEngine::applyMidiMapping (const MidiMapping& mapping, const juce::MidiMessage& message)
{
    // "On" for notes is note-on; for CCs it is value >= 64; program changes are always on.
    const bool isOn = message.isNoteOn (true) || (message.isController() && message.getControllerValue() >= 64) || message.isProgramChange();
    const bool isEdge = message.isNoteOn (true) || message.isProgramChange()
                      || (message.isController() && ! message.isNoteOff (true)); // CC: act on every message, gate below
    switch (mapping.target)
    {
        case MidiMapping::Target::parameter:
        {
            if (! message.isController())
                return;
            const auto* node = document.findNode (mapping.node);
            if (node == nullptr || ! juce::isPositiveAndBelow (mapping.parameter, node->processor->getNumParameters()))
                return;
            auto& parameter = node->processor->getParameter (mapping.parameter);
            if (mapping.relative)
            {
                // Relative encoder ("64 +/- n"): 65..127 = +1..+63, 63..0 = -1..-64.
                // One detent moves the knob ~1/128 of its range; fast spins send bigger n.
                const auto delta = message.getControllerValue() - 64;
                const auto step = static_cast<float> (delta) / 128.0f;
                parameter.setNormalisedValue (juce::jlimit (0.0f, 1.0f, parameter.getNormalisedValue() + step));
            }
            else
                parameter.setValue (parameter.range.convertFrom0to1 (static_cast<float> (message.getControllerValue()) / 127.0f));
            markDocumentEdited();
            if (onParameterChangedByMidi)
                onParameterChangedByMidi (mapping.node);
            return;
        }
        case MidiMapping::Target::bypass:
        {
            const auto* node = document.findNode (mapping.node);
            if (node == nullptr)
                return;
            if (message.isNoteOn (true))
                setNodeBypassed (mapping.node, ! node->processor->isBypassed()); // a note toggles
            else if (message.isController())
                setNodeBypassed (mapping.node, ! isOn);                          // a switch CC sets
            return;
        }
        case MidiMapping::Target::command:
        {
            // Rising edge only, so a latching switch CC does not fire twice.
            const auto key = static_cast<juce::int64> (mapping.node) * 1000 + mapping.command.hashCode() % 1000;
            const bool wasOn = midiCommandGate[key];
            midiCommandGate[key] = isOn;
            if (isEdge && isOn && ! wasOn)
                sendNodeCommand (mapping.node, mapping.command);
            return;
        }
        case MidiMapping::Target::slot:
        case MidiMapping::Target::groupBypass:
            if (isOn && onMidiUiTarget)
                onMidiUiTarget (mapping, message);
            return;
    }
}

void PatchEngine::applyGroupsJson (const juce::var& groups)
{
    const auto before = document.groupsToJson();
    document.groupsFromJson (groups);
    history.recordGroups (before, document.groupsToJson());
    markDocumentEdited();
    sendChangeMessage();
}

void PatchEngine::setParameterNoHistory (NodeId id, int parameterIndex, float value)
{
    if (auto* node = document.findNode (id))
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            node->processor->getParameter (parameterIndex).setValue (value);
            markDocumentEdited();
        }
}

void PatchEngine::setModulationDepthNoHistory (NodeId id, int parameterIndex, float depth)
{
    if (auto* node = document.findNode (id))
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            node->processor->getParameter (parameterIndex).setModulationDepth (depth);
            markDocumentEdited();
        }
}

NodeId PatchEngine::duplicateNode (NodeId id)
{
    const auto* source = document.findNode (id);
    if (source == nullptr || source->hardware)
        return 0;
    const auto kind = source->processor->getKind();
    const auto position = source->position + juce::Point<float> (48.0f, 48.0f);
    const auto newId = document.addNode (kind, position);
    if (newId == 0)
        return 0;
    // addNode may have reallocated the node vector: look both up again.
    source = document.findNode (id);
    auto* copy = document.findNode (newId);
    if (source == nullptr || copy == nullptr)
        return 0;
    const auto parameterCount = juce::jmin (source->processor->getNumParameters(), copy->processor->getNumParameters());
    for (int index = 0; index < parameterCount; ++index)
    {
        const auto& from = source->processor->getParameter (index);
        auto& to = copy->processor->getParameter (index);
        to.setValue (from.getValue());
        to.setModulationDepth (from.getModulationDepth());
    }
    copy->processor->setName (source->processor->getName());
    copy->processor->setBypassed (source->processor->isBypassed());
    const auto extra = source->processor->getExtraState();
    if (! extra.isVoid())
        copy->processor->setExtraState (extra);
    history.recordNodeAdded (newId);
    compileAndPublish (false);
    return newId;
}

juce::Result PatchEngine::loadPatch (const juce::File& file)
{
    history.clear();
    if (! file.existsAsFile())
        return juce::Result::fail ("Patch file not found.");
    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("The patch JSON is invalid.");
    bundle::rebaseAssetPaths (parsed, file.getParentDirectory(), false);

    setPanicMuted (true);
    const auto result = document.loadJson (parsed);
    if (result.failed())
        return result;
    bundle::loadAudioContent (document, parsed, file.getParentDirectory());
    if (! compileAndPublish (false))
        return juce::Result::fail (graphMessage);
    modifiedSinceSave = false;
    savedAudioVersions.clear();
    savedAudioTarget = juce::File();
    graphMessage = "Patch loaded muted - press PANIC to fade audio back in";
    return juce::Result::ok();
}

void PatchEngine::createDefaultPatch()
{
    history.clear();
    document.clearUserPatch();
    const auto gain = document.addNode (NodeKind::gain, { 430.0f, 190.0f });
    const auto distortion = document.addNode (NodeKind::distortion, { 760.0f, 190.0f });
    const auto limiter = document.addNode (NodeKind::limiter, { 1090.0f, 190.0f });
    document.addNode (NodeKind::lfo, { 430.0f, 600.0f });
    document.addNode (NodeKind::stepSequencer, { 760.0f, 600.0f });
    document.addNode (NodeKind::feedbackGuard, { 1420.0f, 560.0f });

    const auto* hardwareInput = document.findNode (PatchDocument::hardwareInputId);
    const auto* hardwareOutput = document.findNode (PatchDocument::hardwareOutputId);
    int firstActiveInput = -1;
    int firstActiveOutput = -1;
    if (hardwareInput != nullptr)
        for (int port = 0; port < hardwareInput->processor->getNumOutputPorts(); ++port)
            if (hardwareInput->processor->getOutputPort (port).active)
            {
                firstActiveInput = port;
                break;
            }
    if (hardwareOutput != nullptr)
        for (int port = 0; port < hardwareOutput->processor->getNumInputPorts(); ++port)
            if (hardwareOutput->processor->getInputPort (port).active)
            {
                firstActiveOutput = port;
                break;
            }

    if (firstActiveInput >= 0)
        document.addConnection ({ PatchDocument::hardwareInputId, firstActiveInput, gain, 0 });
    document.addConnection ({ gain, 0, distortion, 0 });
    document.addConnection ({ distortion, 0, limiter, 0 });
    if (firstActiveOutput >= 0)
        document.addConnection ({ limiter, 0, PatchDocument::hardwareOutputId, firstActiveOutput });
    compileAndPublish (false);
}

void PatchEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    callbackRunning.store (true, std::memory_order_relaxed);
    if (numSamples > 0)
        observedBlockSize.store (numSamples, std::memory_order_relaxed);
   #if JUCE_LINUX
    // One non-blocking syscall on the first block: is this thread actually realtime?
    if (callbackScheduler.load (std::memory_order_relaxed) < 0)
        callbackScheduler.store (sched_getscheduler (0), std::memory_order_relaxed);
   #endif
    if (activePlan != nullptr && midiNoteFifo.getNumReady() > 0)
    {
        const auto scope = midiNoteFifo.read (midiNoteFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
        {
            const auto& event = midiNoteEvents[static_cast<std::size_t> (scope.startIndex1 + i)];
            activePlan->dispatchMidiNote (event.channel, event.note, event.velocity, event.on);
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            const auto& event = midiNoteEvents[static_cast<std::size_t> (scope.startIndex2 + i)];
            activePlan->dispatchMidiNote (event.channel, event.note, event.velocity, event.on);
        }
    }

    const auto hasPendingPlan = pendingPlan.load (std::memory_order_acquire) != nullptr;
    if (hasPendingPlan && activePlan != nullptr && audioThreadMasterGain > 1.0e-4f)
        graphSwapFadingOut = true;

    if (hasPendingPlan && (activePlan == nullptr || audioThreadMasterGain <= 1.0e-4f))
    {
        if (auto* replacement = pendingPlan.exchange (nullptr, std::memory_order_acq_rel))
        {
            auto* previous = activePlan;
            activePlan = replacement;
            graphSwapFadingOut = false;
            retirePlanFromAudioThread (previous);
            graphLatencySamples.store (activePlan->getGraphLatencySamples(), std::memory_order_relaxed);
            if (activePlan->makesDeviceReady)
                deviceReady.store (true, std::memory_order_release);
        }
    }

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numSamples <= 0)
    {
        cpuLoad.store (0.0f, std::memory_order_relaxed);
        return;
    }

    if (deviceReady.load (std::memory_order_acquire) && activePlan != nullptr)
    {
        const auto maximumChunk = activePlan->getMaximumBlockSize();
        for (int offset = 0; offset < numSamples; offset += maximumChunk)
        {
            const auto chunk = juce::jmin (maximumChunk, numSamples - offset);
            activePlan->render (inputChannelData, numInputChannels,
                                outputChannelData, numOutputChannels,
                                offset, chunk);
        }
    }

    const auto targetGain = (panicMuted.load (std::memory_order_relaxed) || graphSwapFadingOut) ? 0.0f : 1.0f;
    const auto sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    const auto maximumStep = static_cast<float> (1.0 / juce::jmax (1.0, sampleRate * 0.005));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        audioThreadMasterGain += juce::jlimit (-maximumStep, maximumStep, targetGain - audioThreadMasterGain);
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                outputChannelData[channel][sample] *= audioThreadMasterGain;
    }

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto blockDuration = static_cast<double> (numSamples) / juce::jmax (1.0, sampleRate);
    const auto ratio = static_cast<float> (juce::jlimit (0.0, 4.0, elapsed / blockDuration));
    cpuLoad.store (ratio, std::memory_order_relaxed);
    // Worst-case latching matters more than the average; a lost race with the
    // message-thread decay only shortens how long a peak is displayed.
    if (ratio > cpuPeak.load (std::memory_order_relaxed))
        cpuPeak.store (ratio, std::memory_order_relaxed);
}

void PatchEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const auto inputs = device->getActiveInputChannels().countNumberOfSetBits();
    const auto outputs = device->getActiveOutputChannels().countNumberOfSetBits();
    const auto sampleRate = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();
    const bool changed = inputs != currentInputChannels.load (std::memory_order_relaxed)
                      || outputs != currentOutputChannels.load (std::memory_order_relaxed)
                      || std::abs (sampleRate - currentSampleRate.load (std::memory_order_relaxed)) > 0.01
                      || blockSize != currentBufferSize.load (std::memory_order_relaxed);
    if (changed)
    {
        deviceReady.store (false, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void PatchEngine::audioDeviceStopped()
{
    observedBlockSize.store (0, std::memory_order_relaxed);
    callbackScheduler.store (-1, std::memory_order_relaxed);
    callbackRunning.store (false, std::memory_order_relaxed);
    deviceReady.store (false, std::memory_order_release);
}

void PatchEngine::audioDeviceError (const juce::String& errorMessage)
{
    int expected = 0;
    if (pendingDeviceErrorLength.compare_exchange_strong (expected, -1, std::memory_order_acq_rel))
    {
        const auto* utf8 = errorMessage.toRawUTF8();
        int length = 0;
        while (utf8[length] != '\0' && length < static_cast<int> (pendingDeviceErrorText.size()) - 1)
        {
            pendingDeviceErrorText[static_cast<std::size_t> (length)] = utf8[length];
            ++length;
        }
        pendingDeviceErrorText[static_cast<std::size_t> (length)] = '\0';
        pendingDeviceErrorLength.store (length, std::memory_order_release);
    }
    deviceReady.store (false, std::memory_order_release);
}

void PatchEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    triggerAsyncUpdate();
}

void PatchEngine::handleAsyncUpdate()
{
    if (audioCallbackRegistered
        && deviceReady.load (std::memory_order_acquire)
        && currentDeviceSignature() == configuredDeviceSignature)
    {
        saveAudioDeviceState();
        sendChangeMessage();
        return;
    }

    deviceReady.store (false, std::memory_order_release);
    if (audioCallbackRegistered)
    {
        deviceManager.removeAudioCallback (this);
        audioCallbackRegistered = false;
    }
    rebuildForCurrentDevice (true);
    audioThreadMasterGain = 0.0f;
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        deviceReady.store (true, std::memory_order_release);
        deviceManager.addAudioCallback (this);
        audioCallbackRegistered = true;
    }
    saveAudioDeviceState();
}

void PatchEngine::timerCallback()
{
    reclaimRetiredPlans();
    writeAutosaveIfDue();
    if (--midiRefreshCountdown <= 0)
    {
        midiRefreshCountdown = 30; // hot-plug scan every few seconds (timer runs ~10 Hz)
        refreshMidiInputs();
    }
    sendControllerFeedback (false); // cheap when nothing moved, nothing when no controller
    // Let the displayed worst case fade over a few seconds.
    cpuPeak.store (cpuPeak.load (std::memory_order_relaxed) * 0.985f, std::memory_order_relaxed);

    auto errorLength = pendingDeviceErrorLength.load (std::memory_order_acquire);
    if (errorLength > 0
        && pendingDeviceErrorLength.compare_exchange_strong (errorLength, -1, std::memory_order_acq_rel))
    {
        deviceError = juce::String::fromUTF8 (pendingDeviceErrorText.data(), errorLength);
        pendingDeviceErrorLength.store (0, std::memory_order_release);
        sendChangeMessage();
    }
}

juce::File PatchEngine::autosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SignalPatch")
        .getChildFile ("autosave.signalpatch");
}

juce::File PatchEngine::audioStateFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SignalPatch")
        .getChildFile ("audio-device.xml");
}

void PatchEngine::saveAudioDeviceState()
{
    const auto state = deviceManager.createStateXml();
    if (state == nullptr)
        return;
    const auto file = audioStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (state->toString());
}

juce::String PatchEngine::currentDeviceSignature()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "offline";
    return device->getTypeName() + "|" + device->getName()
         + "|" + juce::String (device->getCurrentSampleRate(), 3)
         + "|" + juce::String (device->getCurrentBufferSizeSamples())
         + "|" + device->getActiveInputChannels().toString (2)
         + "|" + device->getActiveOutputChannels().toString (2);
}

void PatchEngine::writeAutosaveIfDue()
{
    if (! documentDirty)
        return;
    if (juce::Time::currentTimeMillis() - lastDocumentChangeMs < 1500)
        return;

    const auto file = autosaveFile();
    file.getParentDirectory().createDirectory();
    // Audio files are rewritten only when a recording changed since the last
    // autosave; the JSON is cheap and written every time.
    const auto json = bundle::toJsonWithAudio (document, file, autosavedAudioVersions);
    if (file.replaceWithText (juce::JSON::toString (json, true)))
        documentDirty = false;
}
} // namespace signalpatch
