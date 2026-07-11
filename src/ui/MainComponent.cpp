#include "MainComponent.h"

namespace signalpatch::ui
{
MainComponent::PaletteButton::PaletteButton (NodeKind kindToUse, const juce::String& name)
    : juce::Button (name), kind (kindToUse)
{
}

void MainComponent::PaletteButton::paintButton (juce::Graphics& graphics,
                                                bool isMouseOverButton,
                                                bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = kindAccent (kind);
    auto fill = colours::panelRaised.interpolatedWith (accent, isMouseOverButton ? 0.16f : 0.07f);
    if (isButtonDown)
        fill = fill.darker (0.15f);

    graphics.setGradientFill (juce::ColourGradient (fill.brighter (0.05f), bounds.getCentreX(), bounds.getY(),
                                                    fill.darker (0.07f), bounds.getCentreX(),
                                                    bounds.getBottom(), false));
    graphics.fillRoundedRectangle (bounds, 6.0f);
    graphics.setColour (isMouseOverButton ? accent.withAlpha (0.6f)
                                          : juce::Colours::white.withAlpha (0.06f));
    graphics.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    // Module-colour spine on the left edge, like a rack ear.
    graphics.setColour (accent.withAlpha (isMouseOverButton ? 1.0f : 0.8f));
    graphics.fillRoundedRectangle (bounds.getX() + 2.5f, bounds.getY() + 4.0f, 3.0f,
                                   bounds.getHeight() - 8.0f, 1.5f);

    auto content = bounds.reduced (12.0f, 5.0f);
    const auto glyphArea = content.removeFromLeft (16.0f).withSizeKeepingCentre (13.0f, 13.0f);
    drawKindGlyph (graphics, kind, glyphArea, accent);

    graphics.setColour (colours::text.withAlpha (isMouseOverButton ? 1.0f : 0.88f));
    graphics.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    graphics.drawFittedText (getButtonText(), content.withTrimmedLeft (7.0f).toNearestInt(),
                             juce::Justification::centredLeft, 1);
}

MainComponent::MainComponent()
    : canvas (engine)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    setOpaque (true);

    juce::String previousGroup;
    for (const auto& entry : nodePalette())
    {
        if (previousGroup != entry.group)
        {
            previousGroup = entry.group;
            auto label = std::make_unique<juce::Label> (juce::String(), entry.group);
            label->setColour (juce::Label::textColourId, colours::mutedText);
            label->setFont (juce::FontOptions (9.5f, juce::Font::bold));
            label->setJustificationType (juce::Justification::bottomLeft);
            paletteHost.addAndMakeVisible (*label);
            paletteGroupLabels.push_back (std::move (label));
        }
        else
        {
            paletteGroupLabels.push_back (nullptr);
        }

        auto button = std::make_unique<PaletteButton> (entry.kind, entry.label);
        button->setTooltip (entry.tooltip);
        const auto kind = entry.kind;
        button->onClick = [this, kind]
        {
            canvas.addNodeAtVisibleCentre (kind, visibleCanvasArea());
        };
        paletteHost.addAndMakeVisible (*button);
        paletteButtons.push_back (std::move (button));
    }

    paletteViewport.setViewedComponent (&paletteHost, false);
    paletteViewport.setScrollBarsShown (true, false);
    paletteViewport.setScrollBarThickness (6);
    paletteViewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (paletteViewport);

    canvasHost.addAndMakeVisible (canvas);
    canvasHost.setSize (canvas.getWidth(), canvas.getHeight());
    viewport.setViewedComponent (&canvasHost, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);

    for (auto* button : { &audioSetupButton, &saveButton, &loadButton, &panicButton,
                          &zoomOutButton, &zoomResetButton, &zoomInButton })
    {
        button->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        addAndMakeVisible (*button);
    }
    panicButton.setColour (juce::TextButton::buttonColourId, colours::warning.darker (0.42f));

    audioSetupButton.onClick = [this] { showAudioSetup(); };
    saveButton.onClick = [this] { showSaveDialog(); };
    loadButton.onClick = [this] { showLoadDialog(); };
    panicButton.onClick = [this]
    {
        engine.togglePanic();
        updateStatus();
    };
    zoomOutButton.onClick = [this] { setCanvasZoom (canvasZoom - 0.1f); };
    zoomResetButton.onClick = [this] { setCanvasZoom (1.0f); };
    zoomInButton.onClick = [this] { setCanvasZoom (canvasZoom + 0.1f); };

    deviceLabel.setColour (juce::Label::textColourId, colours::text);
    deviceLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    deviceLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (deviceLabel);

    performanceLabel.setColour (juce::Label::textColourId, colours::mutedText);
    performanceLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    performanceLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (performanceLabel);

    messageLabel.setColour (juce::Label::textColourId, colours::mutedText);
    messageLabel.setFont (juce::FontOptions (11.0f));
    messageLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (messageLabel);

    canvas.onStatus = [this] (const juce::String& message)
    {
        setMessage (message, message.containsIgnoreCase ("blocked") || message.containsIgnoreCase ("unsafe"));
    };
    canvas.onSelectionChanged = [this] (NodeId id)
    {
        inspectorNodeId = id;
        if (id != 0)
            inspectorConnection.reset();
        repaint();
    };
    canvas.onCableSelectionChanged = [this] (std::optional<Connection> connection)
    {
        inspectorConnection = connection;
        if (connection.has_value())
            inspectorNodeId = 0;
        repaint();
    };

    engine.addChangeListener (this);
    const auto result = engine.initialise();
    if (result.failed())
        setMessage ("Audio is offline: " + result.getErrorMessage(), true);
    else if (engine.getStatus().graphMessage.containsIgnoreCase ("restored"))
        setMessage (engine.getStatus().graphMessage);
    else
        setMessage ("Graph live. Drag a glowing output port to a matching input.");

    updateStatus();
    startTimerHz (10);
    setSize (1440, 900);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false);
    paletteViewport.setViewedComponent (nullptr, false);
    setLookAndFeel (nullptr);
}

void MainComponent::setMessage (juce::String message, bool error)
{
    messageIsError = error;
    messageLabel.setColour (juce::Label::textColourId, error ? colours::warning : colours::mutedText);
    messageLabel.setText (std::move (message), juce::dontSendNotification);
}

void MainComponent::updateStatus()
{
    const auto status = engine.getStatus();
    engineRunning = status.running && ! status.panicMuted;
    deviceLabel.setText (status.deviceName + "  /  " + status.backendName,
                         juce::dontSendNotification);
    const auto bufferMs = status.sampleRate > 0.0
        ? 1000.0 * static_cast<double> (status.bufferSize) / status.sampleRate
        : 0.0;
    const auto graphMs = status.sampleRate > 0.0
        ? 1000.0 * static_cast<double> (status.graphLatencySamples) / status.sampleRate
        : 0.0;
    performanceLabel.setText (
        juce::String (status.sampleRate / 1000.0, 1) + " kHz  |  "
        + juce::String (status.bufferSize) + " samples (" + juce::String (bufferMs, 2) + " ms/buffer)  |  "
        + juce::String (status.inputChannels) + " IN -> " + juce::String (status.outputChannels) + " OUT  |  "
        + "graph " + juce::String (status.graphLatencySamples) + " smp / " + juce::String (graphMs, 2) + " ms  |  "
        + "DSP " + juce::String (status.cpuLoad * 100.0f, 1) + "% (pk "
        + juce::String (status.cpuPeak * 100.0f, 0) + "%)  |  xruns "
        + (status.xruns >= 0 ? juce::String (status.xruns) : "?"),
        juce::dontSendNotification);

    panicButton.setButtonText (status.panicMuted ? "MUTED - RESET" : "PANIC");
    panicButton.setColour (juce::TextButton::buttonColourId,
                           status.panicMuted ? colours::warning : colours::warning.darker (0.42f));

    if ((status.graphMessage.containsIgnoreCase ("blocked")
         || status.graphMessage.containsIgnoreCase ("error"))
        && ! messageIsError)
        setMessage (status.graphMessage, true);
}

void MainComponent::timerCallback()
{
    updateStatus();
    repaint();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateStatus();
    repaint();
}

void MainComponent::showAudioSetup()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(),
        0, 256,
        0, 256,
        true, true, true, false);
    selector->setSize (720, 600);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "SignalPatch Audio Setup";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

juce::Rectangle<int> MainComponent::visibleCanvasArea() const
{
    const auto view = viewport.getViewArea();
    return { juce::roundToInt (static_cast<float> (view.getX()) / canvasZoom),
             juce::roundToInt (static_cast<float> (view.getY()) / canvasZoom),
             juce::roundToInt (static_cast<float> (view.getWidth()) / canvasZoom),
             juce::roundToInt (static_cast<float> (view.getHeight()) / canvasZoom) };
}

void MainComponent::setCanvasZoom (float newZoom)
{
    newZoom = juce::jlimit (0.5f, 1.6f, newZoom);
    if (std::abs (newZoom - canvasZoom) < 0.001f)
        return;

    const auto view = viewport.getViewArea();
    const auto logicalCentre = juce::Point<float> (
        static_cast<float> (view.getCentreX()) / canvasZoom,
        static_cast<float> (view.getCentreY()) / canvasZoom);
    canvasZoom = newZoom;
    canvas.setTransform (juce::AffineTransform::scale (canvasZoom));
    canvasHost.setSize (juce::roundToInt (static_cast<float> (canvas.getWidth()) * canvasZoom),
                        juce::roundToInt (static_cast<float> (canvas.getHeight()) * canvasZoom));
    viewport.setViewPosition (juce::roundToInt (logicalCentre.x * canvasZoom
                                                - static_cast<float> (view.getWidth()) * 0.5f),
                              juce::roundToInt (logicalCentre.y * canvasZoom
                                                - static_cast<float> (view.getHeight()) * 0.5f));
    zoomResetButton.setButtonText (juce::String (juce::roundToInt (canvasZoom * 100.0f)) + "%");
    setMessage ("Canvas zoom " + juce::String (juce::roundToInt (canvasZoom * 100.0f)) + "%");
}

void MainComponent::showSaveDialog()
{
    auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("untitled.signalpatch");
    fileChooser = std::make_unique<juce::FileChooser> ("Save SignalPatch patch", suggested, "*.signalpatch");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;
        if (! file.hasFileExtension ("signalpatch"))
            file = file.withFileExtension ("signalpatch");
        const auto result = engine.savePatch (file);
        setMessage (result.wasOk() ? "Saved " + file.getFileName() : result.getErrorMessage(), result.failed());
    });
}

void MainComponent::loadPatchFile (const juce::File& file)
{
    const auto result = engine.loadPatch (file);
    setMessage (result.wasOk() ? "Loaded muted: " + file.getFileName() + " - press MUTED to fade in"
                               : result.getErrorMessage(), result.failed());
}

void MainComponent::showLoadDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load SignalPatch patch",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.signalpatch");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        const auto result = engine.loadPatch (file);
        setMessage (result.wasOk() ? "Loaded muted: " + file.getFileName() + " - press MUTED to fade in"
                                   : result.getErrorMessage(), result.failed());
    });
}

void MainComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (colours::workspace);

    // Header bar.
    graphics.setGradientFill (juce::ColourGradient (colours::panelRaised.brighter (0.02f), 0.0f, 0.0f,
                                                    colours::panel, 0.0f, static_cast<float> (headerHeight),
                                                    false));
    graphics.fillRect (0, 0, getWidth(), headerHeight);

    // Side panels and footer.
    graphics.setColour (colours::panel);
    graphics.fillRect (0, headerHeight, paletteWidth, getHeight() - headerHeight - footerHeight);
    graphics.fillRect (getWidth() - inspectorWidth, headerHeight, inspectorWidth,
                       getHeight() - headerHeight - footerHeight);
    graphics.fillRect (0, getHeight() - footerHeight, getWidth(), footerHeight);

    graphics.setColour (juce::Colours::black.withAlpha (0.4f));
    graphics.drawHorizontalLine (headerHeight - 1, 0.0f, static_cast<float> (getWidth()));
    graphics.drawVerticalLine (paletteWidth - 1, static_cast<float> (headerHeight),
                               static_cast<float> (getHeight() - footerHeight));
    graphics.drawVerticalLine (getWidth() - inspectorWidth, static_cast<float> (headerHeight),
                               static_cast<float> (getHeight() - footerHeight));
    graphics.drawHorizontalLine (getHeight() - footerHeight, 0.0f, static_cast<float> (getWidth()));
    graphics.setColour (juce::Colours::white.withAlpha (0.04f));
    graphics.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    // Signal-family colour strip along the very top: a little rack rainbow.
    juce::ColourGradient strip (colours::audio, 0.0f, 0.0f,
                                colours::control, static_cast<float> (getWidth()), 0.0f, false);
    strip.addColour (0.35, colours::okay);
    strip.addColour (0.65, colours::feedback);
    graphics.setGradientFill (strip);
    graphics.fillRect (0, 0, getWidth(), 3);

    // Wordmark.
    graphics.setColour (colours::audio);
    graphics.fillRoundedRectangle (18.0f, 12.0f, 4.0f, 38.0f, 2.0f);
    graphics.setColour (colours::text);
    graphics.setFont (juce::FontOptions (20.0f, juce::Font::bold).withKerningFactor (0.06f));
    graphics.drawText ("SIGNALPATCH", 32, 8, 190, 25, juce::Justification::centredLeft);
    graphics.setColour (colours::mutedText);
    graphics.setFont (juce::FontOptions (9.5f).withKerningFactor (0.12f));
    graphics.drawText ("REAL-TIME MODULAR PROCESSOR", 33, 34, 190, 16, juce::Justification::centredLeft);

    // Engine state lamp next to the device readout.
    const auto lampColour = engineRunning ? colours::okay : colours::warning;
    graphics.setColour (lampColour.withAlpha (0.3f));
    graphics.fillEllipse (static_cast<float> (deviceLabel.getX()) - 17.0f,
                          static_cast<float> (deviceLabel.getY()) + 7.0f, 12.0f, 12.0f);
    graphics.setColour (lampColour);
    graphics.fillEllipse (static_cast<float> (deviceLabel.getX()) - 14.0f,
                          static_cast<float> (deviceLabel.getY()) + 10.0f, 6.0f, 6.0f);

    // Palette heading.
    graphics.setColour (colours::mutedText);
    graphics.setFont (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.1f));
    graphics.drawText ("ADD MODULES", 14, headerHeight + 12, paletteWidth - 28, 18,
                       juce::Justification::centredLeft);
    graphics.setColour (colours::audio.withAlpha (0.5f));
    graphics.fillRect (14, headerHeight + 31, 30, 2);

    // Inspector heading.
    const auto inspectorX = getWidth() - inspectorWidth + 16;
    graphics.setColour (colours::mutedText);
    graphics.setFont (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.1f));
    graphics.drawText ("INSPECTOR", inspectorX, headerHeight + 12, inspectorWidth - 32, 18,
                       juce::Justification::centredLeft);
    graphics.setColour (colours::audio.withAlpha (0.5f));
    graphics.fillRect (inspectorX, headerHeight + 31, 30, 2);

    graphics.setColour (colours::text);
    graphics.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    if (inspectorConnection.has_value())
    {
        const auto* source = engine.getDocument().findNode (inspectorConnection->sourceNode);
        const auto* destination = engine.getDocument().findNode (inspectorConnection->destinationNode);
        if (source != nullptr && destination != nullptr)
        {
            const auto& sourcePort = source->processor->getOutputPort (inspectorConnection->sourcePort);
            const auto& destinationPort = destination->processor->getInputPort (inspectorConnection->destinationPort);
            const auto rms = source->processor->outputRms (inspectorConnection->sourcePort);
            const auto peak = source->processor->outputPeak (inspectorConnection->sourcePort);
            graphics.drawFittedText (source->processor->getName() + " -> " + destination->processor->getName(),
                                     juce::Rectangle<int> (inspectorX, headerHeight + 44, inspectorWidth - 32, 40),
                                     juce::Justification::centredLeft, 2);
            graphics.setColour (colours::mutedText);
            graphics.setFont (juce::FontOptions (11.0f));
            const auto details = "TYPE  " + juce::String (sourcePort.type == SignalType::audio ? "AUDIO / SOLID" : "CONTROL / DASHED")
                               + "\nSOURCE  " + sourcePort.name
                               + "\nDESTINATION  " + destinationPort.name
                               + "\nRMS  " + juce::String (juce::Decibels::gainToDecibels (rms, -120.0f), 1) + " dBFS"
                               + "\nPEAK  " + juce::String (juce::Decibels::gainToDecibels (peak, -120.0f), 1) + " dBFS"
                               + "\nSOURCE LATENCY  " + juce::String (source->processor->latencySamples()) + " samples";
            graphics.drawFittedText (details,
                                     juce::Rectangle<int> (inspectorX, headerHeight + 92, inspectorWidth - 32, 155),
                                     juce::Justification::topLeft, 10);
            graphics.setColour (colours::grid.brighter (0.25f));
            graphics.drawHorizontalLine (headerHeight + 260,
                                         static_cast<float> (inspectorX),
                                         static_cast<float> (getWidth() - 16));
            graphics.setColour (colours::mutedText);
            graphics.drawFittedText ("Cable brightness, glow and comet speed follow this live RMS value. Press Delete to remove the selected route.",
                                     juce::Rectangle<int> (inspectorX, headerHeight + 278, inspectorWidth - 32, 80),
                                     juce::Justification::topLeft, 6);
        }
    }
    else if (const auto* node = engine.getDocument().findNode (inspectorNodeId))
    {
        const auto accent = kindAccent (node->processor->getKind());
        graphics.setColour (accent);
        graphics.fillRoundedRectangle (static_cast<float> (inspectorX), static_cast<float> (headerHeight + 47),
                                       4.0f, 24.0f, 2.0f);
        graphics.setColour (colours::text);
        graphics.drawFittedText (node->processor->getName(),
                                 juce::Rectangle<int> (inspectorX + 12, headerHeight + 44, inspectorWidth - 44, 30),
                                 juce::Justification::centredLeft, 1);
        graphics.setColour (colours::mutedText);
        graphics.setFont (juce::FontOptions (11.0f));
        auto details = "TYPE  " + nodeKindKey (node->processor->getKind()).toUpperCase()
                     + "\nINPUTS  " + juce::String (node->processor->getNumInputPorts())
                     + "\nOUTPUTS  " + juce::String (node->processor->getNumOutputPorts())
                     + "\nPARAMETERS  " + juce::String (node->processor->getNumParameters())
                     + "\nLATENCY  " + juce::String (node->processor->latencySamples()) + " samples";
        const auto nodeStatus = node->processor->statusText();
        if (nodeStatus.isNotEmpty())
            details += "\n\n" + nodeStatus;
        graphics.drawFittedText (details,
                                 juce::Rectangle<int> (inspectorX, headerHeight + 82, inspectorWidth - 32, 150),
                                 juce::Justification::topLeft, 12);

        graphics.setColour (colours::grid.brighter (0.25f));
        graphics.drawHorizontalLine (headerHeight + 248,
                                     static_cast<float> (inspectorX),
                                     static_cast<float> (getWidth() - 16));
        graphics.setColour (colours::mutedText);
        graphics.drawFittedText (
            "Each diamond input is a modulation socket. The small MOD DEPTH slider sets how far a -1..+1 control signal moves that parameter. Base values are smoothed on the audio thread.",
            juce::Rectangle<int> (inspectorX, headerHeight + 266, inspectorWidth - 32, 118),
            juce::Justification::topLeft, 8);
    }
    else
    {
        graphics.drawText ("Nothing selected", inspectorX, headerHeight + 46,
                           inspectorWidth - 32, 24, juce::Justification::centredLeft);
        graphics.setColour (colours::mutedText);
        graphics.setFont (juce::FontOptions (11.0f));
        graphics.drawFittedText (
            "Add a module from the left. Drag solid circular ports for audio. Drag diamond ports for control. Select a cable and press Delete to remove it. Escape is always panic mute.",
            juce::Rectangle<int> (inspectorX, headerHeight + 84, inspectorWidth - 32, 150),
            juce::Justification::topLeft, 10);
    }

    graphics.setColour (colours::feedback);
    graphics.setFont (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.08f));
    graphics.drawText ("FEEDBACK RULE", inspectorX, getHeight() - footerHeight - 136,
                       inspectorWidth - 32, 18, juce::Justification::centredLeft);
    graphics.setColour (colours::mutedText);
    graphics.setFont (juce::FontOptions (10.0f));
    graphics.drawFittedText (
        "Every cycle must cross a Feedback Guard. Unguarded zero-delay loops stay visible but never replace the last valid live graph.",
        juce::Rectangle<int> (inspectorX, getHeight() - footerHeight - 112, inspectorWidth - 32, 78),
        juce::Justification::topLeft, 6);

    // Footer status lamp.
    const auto footerLamp = messageIsError ? colours::warning : colours::okay;
    graphics.setColour (footerLamp.withAlpha (0.9f));
    graphics.fillEllipse (14.0f, static_cast<float> (getHeight() - footerHeight) + 12.0f, 6.0f, 6.0f);

    // Master output meter (level at the hardware output module's input).
    if (const auto* outputNode = engine.getDocument().findNode (PatchDocument::hardwareOutputId))
    {
        const auto level = juce::jlimit (0.0f, 1.0f,
                                         std::sqrt (outputNode->processor->inputWaveform().rms));
        const auto meter = juce::Rectangle<float> (static_cast<float> (getWidth()) - 172.0f,
                                                   static_cast<float> (getHeight() - footerHeight) + 10.0f,
                                                   130.0f, 10.0f);
        graphics.setColour (colours::mutedText);
        graphics.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        graphics.drawText ("OUT", meter.translated (-32.0f, -1.0f).withWidth (28.0f).toNearestInt(),
                           juce::Justification::centredRight);
        graphics.setColour (colours::nodeDark);
        graphics.fillRoundedRectangle (meter, 2.0f);
        constexpr int segments = 16;
        for (int segment = 0; segment < segments; ++segment)
        {
            const auto proportion = static_cast<float> (segment) / segments;
            if (proportion >= level)
                break;
            const auto segmentColour = proportion > 0.85f ? colours::warning
                                     : proportion > 0.65f ? colours::feedback
                                                          : colours::okay;
            graphics.setColour (segmentColour.withAlpha (0.9f));
            graphics.fillRoundedRectangle (meter.getX() + 2.0f + proportion * (meter.getWidth() - 4.0f),
                                           meter.getY() + 2.0f,
                                           (meter.getWidth() - 4.0f) / segments - 1.5f,
                                           meter.getHeight() - 4.0f, 1.0f);
        }
    }
}

void MainComponent::layoutPalette()
{
    constexpr int buttonHeight = 34;
    constexpr int buttonGap = 5;
    constexpr int groupGap = 24;
    const auto width = paletteWidth - 20;

    int y = 2;
    for (std::size_t index = 0; index < paletteButtons.size(); ++index)
    {
        if (paletteGroupLabels[index] != nullptr)
        {
            if (index > 0)
                y += 8;
            paletteGroupLabels[index]->setBounds (2, y, width - 4, groupGap - 6);
            y += groupGap;
        }
        paletteButtons[index]->setBounds (2, y, width - 4, buttonHeight);
        y += buttonHeight + buttonGap;
    }
    paletteHost.setSize (width, y + 8);
}

void MainComponent::resized()
{
    auto top = juce::Rectangle<int> (0, 0, getWidth(), headerHeight);
    audioSetupButton.setBounds (top.removeFromRight (122).reduced (7, 14));
    panicButton.setBounds (top.removeFromRight (126).reduced (7, 14));
    loadButton.setBounds (top.removeFromRight (78).reduced (7, 14));
    saveButton.setBounds (top.removeFromRight (78).reduced (7, 14));
    zoomInButton.setBounds (top.removeFromRight (40).reduced (4, 14));
    zoomResetButton.setBounds (top.removeFromRight (67).reduced (4, 14));
    zoomOutButton.setBounds (top.removeFromRight (40).reduced (4, 14));
    auto statusArea = top.withTrimmedLeft (248).reduced (8, 7);
    deviceLabel.setBounds (statusArea.removeFromTop (25));
    performanceLabel.setBounds (statusArea);

    const auto contentY = headerHeight;
    const auto contentHeight = getHeight() - headerHeight - footerHeight;
    viewport.setBounds (paletteWidth, contentY,
                        getWidth() - paletteWidth - inspectorWidth,
                        contentHeight);

    paletteViewport.setBounds (10, headerHeight + 40, paletteWidth - 12,
                               contentHeight - 48);
    layoutPalette();

    messageLabel.setBounds (28, getHeight() - footerHeight, getWidth() - 42, footerHeight);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        engine.setPanicMuted (true);
        setMessage ("PANIC: outputs fading to silence", true);
        updateStatus();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        showSaveDialog();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
    {
        showLoadDialog();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && (key.getKeyCode() == '+' || key.getKeyCode() == '='))
    {
        setCanvasZoom (canvasZoom + 0.1f);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == '-')
    {
        setCanvasZoom (canvasZoom - 0.1f);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == '0')
    {
        setCanvasZoom (1.0f);
        return true;
    }
    return canvas.keyPressed (key);
}
} // namespace signalpatch::ui
