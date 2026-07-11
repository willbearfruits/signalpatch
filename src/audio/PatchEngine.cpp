#include "PatchEngine.h"

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
            const auto parsed = juce::JSON::parse (autosave);
            if (! parsed.isVoid()
                && document.loadJson (parsed).wasOk()
                && ! document.getConnections().empty())
            {
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
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;
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
                return; // The preferred interface is open; stop searching.
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
        documentDirty = true;
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

NodeId PatchEngine::addNode (NodeKind kind, juce::Point<float> position)
{
    const auto id = document.addNode (kind, position);
    if (id != 0)
        compileAndPublish (false);
    return id;
}

bool PatchEngine::removeNode (NodeId id)
{
    if (! document.removeNode (id))
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
        return juce::Result::fail (graphMessage);
    return juce::Result::ok();
}

bool PatchEngine::disconnect (const Connection& connection)
{
    if (! document.removeConnection (connection))
        return false;
    compileAndPublish (false);
    return true;
}

void PatchEngine::moveNode (NodeId id, juce::Point<float> position)
{
    if (auto* node = document.findNode (id))
    {
        node->position = position;
        documentDirty = true;
        lastDocumentChangeMs = juce::Time::currentTimeMillis();
    }
}

void PatchEngine::setParameter (NodeId id, int parameterIndex, float value)
{
    if (auto* node = document.findNode (id))
    {
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            node->processor->getParameter (parameterIndex).setValue (value);
            documentDirty = true;
            lastDocumentChangeMs = juce::Time::currentTimeMillis();
        }
    }
}

void PatchEngine::setModulationDepth (NodeId id, int parameterIndex, float depth)
{
    if (auto* node = document.findNode (id))
    {
        if (juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        {
            node->processor->getParameter (parameterIndex).setModulationDepth (depth);
            documentDirty = true;
            lastDocumentChangeMs = juce::Time::currentTimeMillis();
        }
    }
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
        node->processor->setBypassed (bypassed);
        documentDirty = true;
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
        node->processor->setExtraState (state);
        documentDirty = true;
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
    status.bufferSize = currentBufferSize.load (std::memory_order_relaxed);
    status.inputChannels = currentInputChannels.load (std::memory_order_relaxed);
    status.outputChannels = currentOutputChannels.load (std::memory_order_relaxed);
    status.graphLatencySamples = graphLatencySamples.load (std::memory_order_relaxed);
    status.cpuLoad = cpuLoad.load (std::memory_order_relaxed);
    status.running = callbackRunning.load (std::memory_order_relaxed);
    status.panicMuted = isPanicMuted();
    status.graphMessage = graphMessage;
    if (deviceError.isNotEmpty())
        status.graphMessage = deviceError + (graphMessage.isNotEmpty() ? " | " + graphMessage : juce::String());
    return status;
}

juce::Result PatchEngine::savePatch (const juce::File& file) const
{
    if (file == juce::File())
        return juce::Result::fail ("No patch file selected.");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create the patch directory.");
    if (! file.replaceWithText (juce::JSON::toString (document.toJson(), true)))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result PatchEngine::loadPatch (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("Patch file not found.");
    const auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("The patch JSON is invalid.");

    setPanicMuted (true);
    const auto result = document.loadJson (parsed);
    if (result.failed())
        return result;
    if (! compileAndPublish (false))
        return juce::Result::fail (graphMessage);
    graphMessage = "Patch loaded muted - press PANIC to fade audio back in";
    return juce::Result::ok();
}

void PatchEngine::createDefaultPatch()
{
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
    cpuLoad.store (static_cast<float> (juce::jlimit (0.0, 4.0, elapsed / blockDuration)),
                   std::memory_order_relaxed);
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
    if (file.replaceWithText (juce::JSON::toString (document.toJson(), true)))
        documentDirty = false;
}
} // namespace signalpatch
