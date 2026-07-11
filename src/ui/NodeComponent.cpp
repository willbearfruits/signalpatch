#include "NodeComponent.h"

#include <cmath>

namespace signalpatch::ui
{
NodeComponent::NodeComponent (PatchEngine& engineToUse, NodeId nodeId)
    : engine (engineToUse), id (nodeId)
{
    setRepaintsOnMouseActivity (true);
    if (const auto* node = model())
    {
        const auto accent = kindAccent (node->processor->getKind());
        parameterWidgets.reserve (static_cast<std::size_t> (node->processor->getNumParameters()));
        for (int parameterIndex = 0; parameterIndex < node->processor->getNumParameters(); ++parameterIndex)
        {
            // Grid-style parameters are edited in the preview, not with knobs.
            if (node->processor->getKind() == NodeKind::stepSequencer && parameterIndex >= 2)
                continue;
            if (node->processor->getKind() == NodeKind::drumMachine && parameterIndex >= 5)
                continue;

            auto& parameter = node->processor->getParameter (parameterIndex);
            ParameterWidgets widgets;
            widgets.parameterIndex = parameterIndex;
            widgets.value = std::make_unique<juce::Slider>();
            widgets.value->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            widgets.value->setColour (juce::Slider::thumbColourId, accent.brighter (0.1f));
            widgets.value->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 18);
            widgets.value->setRange (parameter.range.start, parameter.range.end, parameter.range.interval);
            if (std::abs (parameter.range.skew - 1.0f) > 1.0e-5f)
                widgets.value->setSkewFactor (parameter.range.skew);
            widgets.value->setNumDecimalPlacesToDisplay (2);
            widgets.value->setValue (parameter.getValue(), juce::dontSendNotification);
            widgets.value->setDoubleClickReturnValue (true, parameter.range.convertFrom0to1 (0.5f));
            const auto unit = parameter.unit;
            widgets.value->textFromValueFunction = [unit] (double value)
            {
                if (unit == "Hz")
                    return value >= 1000.0 ? juce::String (value / 1000.0, 2) + " kHz"
                                           : juce::String (value, value < 10.0 ? 2 : 0) + " Hz";
                if (unit == "ms")
                    return juce::String (value, value >= 100.0 ? 0 : 1) + " ms";
                if (unit == "dB")
                    return juce::String (value, 1) + " dB";
                if (unit == "%")
                    return juce::String (value, 1) + "%";
                if (unit == ":1")
                    return juce::String (value, 1) + ":1";
                return juce::String (value, 2);
            };
            widgets.value->valueFromTextFunction = [unit] (const juce::String& text)
            {
                auto value = text.getDoubleValue();
                if (unit == "Hz" && text.containsIgnoreCase ("khz"))
                    value *= 1000.0;
                return value;
            };
            auto* valueSlider = widgets.value.get();
            valueSlider->onDragStart = [this]
            {
                if (onSelected)
                    onSelected (id);
            };
            valueSlider->onValueChange = [this, parameterIndex, valueSlider]
            {
                engine.setParameter (id, parameterIndex, static_cast<float> (valueSlider->getValue()));
            };
            addAndMakeVisible (*widgets.value);

            widgets.depth = std::make_unique<juce::Slider>();
            widgets.depth->setSliderStyle (juce::Slider::LinearHorizontal);
            widgets.depth->setColour (juce::Slider::trackColourId, colours::control.withAlpha (0.85f));
            widgets.depth->setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 16);
            widgets.depth->setRange (0.0, 1.0, 0.01);
            widgets.depth->setValue (parameter.getModulationDepth(), juce::dontSendNotification);
            auto* depthSlider = widgets.depth.get();
            depthSlider->onValueChange = [this, parameterIndex, depthSlider]
            {
                engine.setModulationDepth (id, parameterIndex, static_cast<float> (depthSlider->getValue()));
            };
            addAndMakeVisible (*widgets.depth);
            parameterWidgets.push_back (std::move (widgets));
        }

        if (node->processor->isFeedbackGuard())
        {
            safetyResetButton = std::make_unique<juce::TextButton> ("RESET LOOP");
            safetyResetButton->setColour (juce::TextButton::buttonColourId, colours::feedback.darker (0.45f));
            safetyResetButton->setTooltip ("Clear a tripped feedback guard and its delayed samples");
            safetyResetButton->onClick = [this] { engine.resetNodeSafety (id); };
            addAndMakeVisible (*safetyResetButton);
        }

        const auto kind = node->processor->getKind();
        auto addCommand = [this] (const juce::String& label, const juce::String& command,
                                  juce::Colour activeColour)
        {
            CommandButton entry;
            entry.button = std::make_unique<juce::TextButton> (label);
            entry.command = command;
            entry.activeColour = activeColour;
            entry.button->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
            entry.button->onClick = [this, command]
            {
                engine.sendNodeCommand (id, command);
                repaint();
            };
            addAndMakeVisible (*entry.button);
            commandButtons.push_back (std::move (entry));
        };

        if (kind == NodeKind::sampler)
        {
            addCommand ("REC", "rec", colours::warning);
            addCommand ("PLAY", "play", colours::okay);
            addCommand ("CLR", "clear", colours::mutedText);
        }
        else if (kind == NodeKind::fourTrack)
        {
            addCommand ("PLAY", "play", colours::okay);
            addCommand ("REC", "rec", colours::warning);
            addCommand ("RTZ", "rtz", colours::mutedText);
            for (int track = 1; track <= 4; ++track)
                addCommand (juce::String (track), "arm" + juce::String (track), colours::warning);
        }
        else if (kind == NodeKind::neuralAmpPlaceholder)
        {
            CommandButton entry;
            entry.button = std::make_unique<juce::TextButton> ("LOAD MODEL");
            entry.command = "load";
            entry.activeColour = colours::panelRaised;
            entry.button->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
            entry.button->onClick = [this] { chooseNamModel(); };
            addAndMakeVisible (*entry.button);
            commandButtons.push_back (std::move (entry));
        }
        else if (kind == NodeKind::script)
        {
            scriptEditor = std::make_unique<juce::TextEditor>();
            scriptEditor->setMultiLine (true, true);
            scriptEditor->setReturnKeyStartsNewLine (false);
            scriptEditor->setFont (juce::Font (juce::FontOptions (
                juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
            scriptEditor->setColour (juce::TextEditor::backgroundColourId, colours::nodeDark);
            scriptEditor->setText (node->processor->getExtraState()
                                       .getProperty ("expr", "tanh(x * (1 + a * 9))").toString(),
                                   juce::dontSendNotification);
            scriptEditor->onReturnKey = [this] { applyScriptText(); };
            addAndMakeVisible (*scriptEditor);

            scriptApplyButton = std::make_unique<juce::TextButton> ("APPLY");
            scriptApplyButton->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
            scriptApplyButton->onClick = [this] { applyScriptText(); };
            addAndMakeVisible (*scriptApplyButton);
        }
    }
}

void NodeComponent::applyScriptText()
{
    if (scriptEditor == nullptr)
        return;
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("expr", scriptEditor->getText());
    engine.applyNodeExtraState (id, juce::var (object.release()));
    repaint();
}

void NodeComponent::chooseNamModel()
{
    auto initialDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Projects/GitHub/external_clones/neural-amp-modeler-lv2-a2/models");
    if (! initialDirectory.isDirectory())
        initialDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    modelChooser = std::make_unique<juce::FileChooser> ("Load a NAM model", initialDirectory, "*.nam");
    modelChooser->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("model", file.getFullPathName());
        engine.applyNodeExtraState (id, juce::var (object.release()));
        repaint();
    });
}

bool NodeComponent::hasStompSwitch() const noexcept
{
    const auto* node = model();
    return node != nullptr && node->processor->isBypassable();
}

juce::Point<float> NodeComponent::stompCentre() const noexcept
{
    return { static_cast<float> (getWidth()) * 0.5f,
             static_cast<float> (getHeight()) - railHeight - stompZoneHeight * 0.5f - 2.0f };
}

void NodeComponent::refreshCommandButtons()
{
    const auto* node = model();
    if (node == nullptr)
        return;
    for (auto& entry : commandButtons)
    {
        if (entry.command == "load")
            continue;
        const auto active = node->processor->uiToggleState (entry.command);
        entry.button->setColour (juce::TextButton::buttonColourId,
                                 active ? entry.activeColour.darker (0.2f) : colours::panelRaised);
    }
}

void NodeComponent::showNodeMenu()
{
    const auto* node = model();
    if (node == nullptr)
        return;
    juce::PopupMenu menu;
    if (node->processor->isBypassable())
        menu.addItem (1, node->processor->isBypassed() ? "Enable (unbypass)" : "Bypass");
    if (! node->hardware)
        menu.addItem (2, "Delete node");
    if (menu.getNumItems() == 0)
        return;
    // Node components are rebuilt whenever the engine broadcasts a change, so
    // the async menu callback must survive this component being deleted.
    juce::Component::SafePointer<NodeComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safeThis] (int result)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        const auto* current = self->model();
        if (current == nullptr)
            return;
        if (result == 1)
            self->engine.setNodeBypassed (self->id, ! current->processor->isBypassed());
        else if (result == 2 && self->onRemoveRequested)
            self->onRemoveRequested (self->id);
    });
}

const NodeModel* NodeComponent::model() const noexcept
{
    return engine.getDocument().findNode (id);
}

int NodeComponent::preferredWidth() const noexcept
{
    const auto* node = model();
    if (node != nullptr && node->hardware)
        return 300;
    return 282;
}

int NodeComponent::preferredHeight() const noexcept
{
    const auto* node = model();
    if (node == nullptr)
        return 180;
    const auto kind = node->processor->getKind();
    const auto portRows = juce::jmax (node->processor->getNumInputPorts(), node->processor->getNumOutputPorts());
    const auto portsHeight = static_cast<int> (firstPortY + portRows * portSpacing + 18.0f);
    auto visibleParameterCount = node->processor->getNumParameters();
    if (kind == NodeKind::stepSequencer)
        visibleParameterCount = juce::jmin (2, visibleParameterCount);
    else if (kind == NodeKind::drumMachine)
        visibleParameterCount = juce::jmin (5, visibleParameterCount);
    const auto parameterRows = (visibleParameterCount + 1) / 2;
    const auto controlsHeight = static_cast<int> (controlsTop + parameterRows * controlRowHeight + 12.0f);
    auto height = juce::jmax (node->hardware ? 150 : 210, portsHeight, controlsHeight);
    if (kind == NodeKind::script)
        height += 92;
    if (kind == NodeKind::sampler || kind == NodeKind::neuralAmpPlaceholder)
        height += 32;
    if (kind == NodeKind::fourTrack)
        height += 62;
    if (node->processor->isBypassable())
        height += stompZoneHeight;
    return height + static_cast<int> (railHeight * 2.0f); // rack rails top + bottom
}

int NodeComponent::signature() const noexcept
{
    if (const auto* node = model())
        return node->processor->getNumInputPorts() * 10000
             + node->processor->getNumOutputPorts() * 100
             + node->processor->getNumParameters();
    return 0;
}

void NodeComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

juce::Point<float> NodeComponent::localInputPortCentre (int port) const
{
    return { 9.0f, firstPortY + static_cast<float> (port) * portSpacing };
}

juce::Point<float> NodeComponent::localOutputPortCentre (int port) const
{
    return { static_cast<float> (getWidth()) - 9.0f,
             firstPortY + static_cast<float> (port) * portSpacing };
}

juce::Point<float> NodeComponent::inputPortCentreInParent (int port) const
{
    return getPosition().toFloat() + localInputPortCentre (port);
}

juce::Point<float> NodeComponent::outputPortCentreInParent (int port) const
{
    return getPosition().toFloat() + localOutputPortCentre (port);
}

std::optional<int> NodeComponent::inputPortNear (juce::Point<float> pointInParent, float radius) const
{
    if (const auto* node = model())
        for (int port = 0; port < node->processor->getNumInputPorts(); ++port)
            if (pointInParent.getDistanceFrom (inputPortCentreInParent (port)) <= radius)
                return port;
    return std::nullopt;
}

std::optional<int> NodeComponent::outputPortNear (juce::Point<float> localPoint, float radius) const
{
    if (const auto* node = model())
        for (int port = 0; port < node->processor->getNumOutputPorts(); ++port)
            if (localPoint.getDistanceFrom (localOutputPortCentre (port)) <= radius)
                return port;
    return std::nullopt;
}

juce::Rectangle<float> NodeComponent::previewBounds() const noexcept
{
    return { 68.0f, railHeight + headerHeight + 10.0f, static_cast<float> (getWidth()) - 136.0f, 78.0f };
}

void NodeComponent::drawPort (juce::Graphics& graphics,
                              juce::Point<float> centre,
                              const PortInfo& port,
                              bool output,
                              bool showLabel,
                              float signalLevel)
{
    const auto baseColour = port.type == SignalType::audio ? colours::audio : colours::control;
    const auto colour = port.active ? baseColour : colours::mutedText.withAlpha (0.5f);
    const auto glow = juce::jlimit (0.0f, 1.0f, signalLevel);

    if (glow > 0.02f)
    {
        graphics.setColour (colour.withAlpha (0.35f * glow));
        graphics.fillEllipse (centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f);
    }

    if (port.type == SignalType::audio)
    {
        // Recessed jack well with a coloured ring; the centre pin lights with signal.
        graphics.setColour (juce::Colours::black.withAlpha (0.55f));
        graphics.fillEllipse (centre.x - 6.5f, centre.y - 6.5f, 13.0f, 13.0f);
        graphics.setColour (colours::nodeDark.brighter (0.15f));
        graphics.fillEllipse (centre.x - 5.5f, centre.y - 5.5f, 11.0f, 11.0f);
        graphics.setColour (colour);
        graphics.drawEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f, 1.8f);
        graphics.setColour (colour.withAlpha (port.active ? 0.35f + 0.65f * glow : 0.2f));
        graphics.fillEllipse (centre.x - 2.2f, centre.y - 2.2f, 4.4f, 4.4f);
    }
    else
    {
        juce::Path diamond;
        diamond.startNewSubPath (centre.x, centre.y - 6.5f);
        diamond.lineTo (centre.x + 6.5f, centre.y);
        diamond.lineTo (centre.x, centre.y + 6.5f);
        diamond.lineTo (centre.x - 6.5f, centre.y);
        diamond.closeSubPath();
        graphics.setColour (juce::Colours::black.withAlpha (0.5f));
        graphics.fillPath (diamond);
        graphics.setColour (colour);
        graphics.strokePath (diamond, juce::PathStrokeType (1.8f));
        graphics.setColour (colour.withAlpha (0.35f + 0.65f * glow));
        graphics.fillEllipse (centre.x - 1.8f, centre.y - 1.8f, 3.6f, 3.6f);
    }

    if (showLabel)
    {
        graphics.setColour (port.active
                                ? colours::mutedText.brighter (glow * 0.8f)
                                : colours::mutedText.withAlpha (0.55f));
        graphics.setFont (9.5f);
        const auto labelBounds = output
            ? juce::Rectangle<float> (centre.x - 66.0f, centre.y - 7.0f, 56.0f, 14.0f)
            : juce::Rectangle<float> (centre.x + 10.0f, centre.y - 7.0f, 58.0f, 14.0f);
        graphics.drawFittedText (port.name, labelBounds.toNearestInt(),
                                 output ? juce::Justification::centredRight : juce::Justification::centredLeft,
                                 1, 0.72f);
    }
}

void NodeComponent::drawWaveform (juce::Graphics& graphics,
                                  juce::Rectangle<float> area,
                                  const WaveformSnapshot& waveform,
                                  juce::Colour colour,
                                  float alpha)
{
    juce::Path shape;
    const auto centreY = area.getCentreY();
    const auto amplitude = area.getHeight() * 0.46f;
    for (int bucket = 0; bucket < WaveformSnapshot::bucketCount; ++bucket)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (bucket)
                                     / static_cast<float> (WaveformSnapshot::bucketCount - 1);
        const auto y = centreY - juce::jlimit (-1.0f, 1.0f, waveform.high[static_cast<std::size_t> (bucket)]) * amplitude;
        if (bucket == 0)
            shape.startNewSubPath (x, y);
        else
            shape.lineTo (x, y);
    }
    for (int bucket = WaveformSnapshot::bucketCount - 1; bucket >= 0; --bucket)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (bucket)
                                     / static_cast<float> (WaveformSnapshot::bucketCount - 1);
        const auto y = centreY - juce::jlimit (-1.0f, 1.0f, waveform.low[static_cast<std::size_t> (bucket)]) * amplitude;
        shape.lineTo (x, y);
    }
    shape.closeSubPath();

    graphics.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha),
                                                    area.getCentreX(), area.getY(),
                                                    colour.withAlpha (alpha * 0.15f),
                                                    area.getCentreX(), area.getBottom(), false));
    graphics.fillPath (shape);
    graphics.setColour (colour.withAlpha (juce::jmin (1.0f, alpha + 0.2f) * 0.35f));
    graphics.strokePath (shape, juce::PathStrokeType (2.6f));
    graphics.setColour (colour.withAlpha (juce::jmin (1.0f, alpha + 0.35f)));
    graphics.strokePath (shape, juce::PathStrokeType (1.1f));
}

void NodeComponent::drawPreview (juce::Graphics& graphics, const NodeModel& node)
{
    const auto kind = node.processor->getKind();
    const auto accent = kindAccent (kind);
    auto area = previewBounds();

    // Inset instrument window: dark well, faint reference lines, inner shadow.
    graphics.setColour (colours::nodeDark);
    graphics.fillRoundedRectangle (area, 5.0f);
    graphics.setColour (colours::grid.withAlpha (0.7f));
    graphics.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX() + 3.0f, area.getRight() - 3.0f);
    graphics.setColour (colours::grid.withAlpha (0.35f));
    graphics.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * 0.25f),
                                 area.getX() + 3.0f, area.getRight() - 3.0f);
    graphics.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * 0.75f),
                                 area.getX() + 3.0f, area.getRight() - 3.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.5f));
    graphics.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);
    graphics.setColour (juce::Colours::white.withAlpha (0.05f));
    graphics.drawHorizontalLine (juce::roundToInt (area.getBottom()), area.getX() + 4.0f, area.getRight() - 4.0f);

    if (kind == NodeKind::stepSequencer)
    {
        const auto active = node.processor->currentStep();
        const auto gap = 3.0f;
        const auto width = (area.getWidth() - gap * 9.0f) / 8.0f;
        for (int step = 0; step < 8; ++step)
        {
            const auto value = node.processor->getParameter (2 + step).getValue();
            auto barArea = juce::Rectangle<float> (area.getX() + gap + step * (width + gap),
                                                    area.getY() + 5.0f,
                                                    width,
                                                    area.getHeight() - 10.0f);
            // Slot guide so empty steps stay visible and editable.
            graphics.setColour (colours::grid.withAlpha (0.5f));
            graphics.fillRoundedRectangle (barArea.withSizeKeepingCentre (barArea.getWidth(), 2.0f), 1.0f);

            const auto barColour = step == active ? colours::selection : accent.withAlpha (0.7f);
            graphics.setColour (barColour);
            auto filled = value >= 0.0f
                ? barArea.withTop (area.getCentreY() - value * barArea.getHeight() * 0.48f)
                         .withBottom (area.getCentreY())
                : barArea.withTop (area.getCentreY())
                         .withBottom (area.getCentreY() - value * barArea.getHeight() * 0.48f);
            graphics.fillRoundedRectangle (filled, 1.5f);
            if (step == active)
            {
                graphics.setColour (colours::selection.withAlpha (0.25f));
                graphics.fillRoundedRectangle (barArea, 2.0f);
            }
        }
        return;
    }

    if (kind == NodeKind::drumMachine)
    {
        const auto active = node.processor->currentStep();
        const auto grid = area.reduced (3.0f);
        const auto cellWidth = grid.getWidth() / 8.0f;
        const auto cellHeight = grid.getHeight() / 3.0f;
        const juce::Colour laneColours[3] { accent, colours::okay, colours::audio };
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 8; ++column)
            {
                auto cell = juce::Rectangle<float> (grid.getX() + column * cellWidth,
                                                    grid.getY() + row * cellHeight,
                                                    cellWidth, cellHeight).reduced (1.5f);
                const auto on = node.processor->getParameter (5 + row * 8 + column).getValue() > 0.5f;
                if (column == active)
                {
                    graphics.setColour (colours::selection.withAlpha (0.18f));
                    graphics.fillRoundedRectangle (cell.expanded (1.0f), 2.0f);
                }
                graphics.setColour (on ? laneColours[row].withAlpha (column == active ? 1.0f : 0.8f)
                                       : colours::nodeDark.brighter (0.15f));
                graphics.fillRoundedRectangle (cell, 2.0f);
                if (! on)
                {
                    graphics.setColour (colours::grid.brighter (0.15f));
                    graphics.drawRoundedRectangle (cell, 2.0f, 0.8f);
                }
            }
        return;
    }

    if (kind == NodeKind::compressor || kind == NodeKind::limiter || kind == NodeKind::gate)
    {
        const auto reduction = juce::jlimit (0.0f, 24.0f, node.processor->gainReductionDb());
        const auto meterArea = area.reduced (10.0f, 20.0f);
        graphics.setColour (colours::nodeDark.brighter (0.12f));
        graphics.fillRoundedRectangle (meterArea, 2.0f);
        for (int tick = 1; tick < 4; ++tick)
        {
            const auto tickX = meterArea.getX() + meterArea.getWidth() * static_cast<float> (tick) / 4.0f;
            graphics.setColour (colours::grid.brighter (0.2f));
            graphics.drawVerticalLine (juce::roundToInt (tickX), meterArea.getY(), meterArea.getBottom());
        }
        const auto fillWidth = meterArea.getWidth() * reduction / 24.0f;
        if (fillWidth > 0.5f)
        {
            graphics.setGradientFill (juce::ColourGradient (colours::okay, meterArea.getX(), 0.0f,
                                                            colours::feedback, meterArea.getRight(), 0.0f, false));
            graphics.fillRoundedRectangle (meterArea.withWidth (fillWidth), 2.0f);
        }
        graphics.setColour (colours::text);
        graphics.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        graphics.drawText (juce::String (reduction, 1) + " dB GR",
                           area.toNearestInt().withTrimmedBottom (2), juce::Justification::centredBottom);
        return;
    }

    if (kind == NodeKind::delay)
    {
        const auto feedback = node.processor->getParameter (1).getValue() * 0.01f;
        for (int echo = 0; echo < 6; ++echo)
        {
            const auto height = juce::jmax (2.0f, area.getHeight() * 0.68f * std::pow (feedback, static_cast<float> (echo)));
            const auto x = area.getX() + 10.0f + echo * (area.getWidth() - 20.0f) / 5.0f;
            graphics.setColour (accent.withAlpha (0.85f - 0.12f * static_cast<float> (echo)));
            graphics.fillRoundedRectangle (x - 1.8f, area.getCentreY() - height * 0.5f, 3.6f, height, 1.8f);
        }
        return;
    }

    if (kind == NodeKind::hardwareInput)
    {
        const auto lanes = juce::jmin (8, node.processor->getNumOutputPorts());
        const auto laneHeight = (area.getHeight() - 10.0f) / static_cast<float> (juce::jmax (1, lanes));
        for (int lane = 0; lane < lanes; ++lane)
        {
            float laneRms = 0.0f;
            for (int port = lane; port < node.processor->getNumOutputPorts(); port += lanes)
                laneRms = juce::jmax (laneRms, node.processor->outputRms (port));
            const auto level = juce::jlimit (0.0f, 1.0f, std::sqrt (laneRms));
            const auto y = area.getY() + 5.0f + static_cast<float> (lane) * laneHeight;
            graphics.setColour (colours::nodeDark.brighter (0.15f));
            graphics.fillRoundedRectangle (area.getX() + 5.0f, y, area.getWidth() - 10.0f,
                                           juce::jmax (2.0f, laneHeight - 3.0f), 1.5f);
            if (level > 0.005f)
            {
                graphics.setGradientFill (juce::ColourGradient (accent.withAlpha (0.9f), area.getX(), 0.0f,
                                                                colours::warning, area.getRight(), 0.0f, false));
                graphics.fillRoundedRectangle (area.getX() + 5.0f, y, (area.getWidth() - 10.0f) * level,
                                               juce::jmax (2.0f, laneHeight - 3.0f), 1.5f);
            }
        }
        return;
    }

    if (kind == NodeKind::hardwareOutput)
    {
        drawWaveform (graphics, area.reduced (4.0f), node.processor->inputWaveform(), accent, 0.5f);
        return;
    }

    if (kind == NodeKind::distortion)
        drawWaveform (graphics, area.reduced (4.0f), node.processor->inputWaveform(), colours::text, 0.18f);
    drawWaveform (graphics, area.reduced (4.0f), node.processor->outputWaveform(), accent, 0.5f);
}

void NodeComponent::paint (juce::Graphics& graphics)
{
    const auto* node = model();
    if (node == nullptr)
        return;

    const auto kind = node->processor->getKind();
    const auto accent = kindAccent (kind);
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    constexpr auto corner = 6.0f;

    // Layered soft shadow keeps modules floating above the rack.
    graphics.setColour (juce::Colours::black.withAlpha (0.14f));
    graphics.fillRoundedRectangle (bounds.expanded (2.0f).translated (0.0f, 5.0f), corner + 2.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.3f));
    graphics.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), corner);

    // Accent-tinted face plate: every module family gets its own panel colour.
    graphics.setGradientFill (juce::ColourGradient (
        colours::nodeTop.interpolatedWith (accent, 0.22f), bounds.getCentreX(), bounds.getY(),
        colours::node.darker (0.1f).interpolatedWith (accent, 0.12f), bounds.getCentreX(),
        bounds.getBottom(), false));
    graphics.fillRoundedRectangle (bounds, corner);

    // Mounting rails top and bottom, screws seated in the rails.
    const auto topRail = bounds.withHeight (railHeight);
    const auto bottomRail = bounds.withTop (bounds.getBottom() - railHeight);
    for (const auto& rail : { topRail, bottomRail })
    {
        juce::Graphics::ScopedSaveState state (graphics);
        juce::Path railClip;
        const bool isTop = rail.getY() < bounds.getCentreY();
        railClip.addRoundedRectangle (rail.getX(), rail.getY(), rail.getWidth(), rail.getHeight(),
                                      corner, corner, isTop, isTop, ! isTop, ! isTop);
        graphics.reduceClipRegion (railClip);
        graphics.setGradientFill (juce::ColourGradient (colours::nodeDark.brighter (0.22f),
                                                        rail.getCentreX(), rail.getY(),
                                                        colours::nodeDark.brighter (0.02f),
                                                        rail.getCentreX(), rail.getBottom(), false));
        graphics.fillRect (rail);
        graphics.setColour (juce::Colours::white.withAlpha (0.06f));
        graphics.drawHorizontalLine (juce::roundToInt (isTop ? rail.getBottom() - 1.0f : rail.getY()),
                                     rail.getX(), rail.getRight());
    }

    const auto screw = [&graphics] (float screwX, float screwY)
    {
        graphics.setColour (juce::Colours::black.withAlpha (0.55f));
        graphics.fillEllipse (screwX - 3.4f, screwY - 3.4f, 6.8f, 6.8f);
        graphics.setColour (colours::mutedText.withAlpha (0.75f));
        graphics.drawEllipse (screwX - 2.7f, screwY - 2.7f, 5.4f, 5.4f, 1.0f);
        graphics.drawLine (screwX - 1.9f, screwY - 1.9f, screwX + 1.9f, screwY + 1.9f, 1.0f);
    };
    screw (bounds.getX() + 11.0f, topRail.getCentreY());
    screw (bounds.getRight() - 11.0f, topRail.getCentreY());
    screw (bounds.getX() + 11.0f, bottomRail.getCentreY());
    screw (bounds.getRight() - 11.0f, bottomRail.getCentreY());

    // Header: bright accent silkscreen strip below the top rail.
    const auto header = bounds.withTop (topRail.getBottom()).withHeight (headerHeight);
    const auto brightHeader = accent.getPerceivedBrightness() > 0.62f;
    const auto titleColour = brightHeader ? colours::nodeDark : colours::text;
    graphics.setGradientFill (juce::ColourGradient (accent.darker (0.02f), header.getCentreX(), header.getY(),
                                                    accent.darker (0.35f), header.getCentreX(),
                                                    header.getBottom(), false));
    graphics.fillRect (header);
    graphics.setColour (juce::Colours::white.withAlpha (0.12f));
    graphics.fillRect (header.withHeight (header.getHeight() * 0.42f));
    graphics.setColour (juce::Colours::black.withAlpha (0.3f));
    graphics.drawHorizontalLine (juce::roundToInt (header.getBottom()) - 1,
                                 header.getX(), header.getRight());

    drawKindGlyph (graphics, kind,
                   { header.getX() + 11.0f, header.getCentreY() - 6.5f, 13.0f, 13.0f },
                   titleColour.withAlpha (0.92f));

    graphics.setColour (titleColour);
    graphics.setFont (juce::FontOptions (13.5f, juce::Font::bold).withKerningFactor (0.04f));
    graphics.drawFittedText (node->processor->getName().toUpperCase(),
                             header.reduced (32.0f, 0.0f).toNearestInt(),
                             juce::Justification::centredLeft, 1);

    // Family tag chip, node id and an output-activity LED on the right.
    const auto tag = kindTag (kind);
    const auto chip = juce::Rectangle<float> (header.getRight() - 44.0f, header.getCentreY() - 8.0f, 34.0f, 16.0f);
    graphics.setColour (juce::Colours::black.withAlpha (brightHeader ? 0.45f : 0.28f));
    graphics.fillRoundedRectangle (chip, 8.0f);
    graphics.setColour (colours::text.withAlpha (0.9f));
    graphics.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    graphics.drawText (tag, chip.toNearestInt(), juce::Justification::centred);
    graphics.setColour (titleColour.withAlpha (0.55f));
    graphics.setFont (8.5f);
    graphics.drawText ("#" + juce::String (id),
                       chip.translated (-chip.getWidth() - 4.0f, 0.0f).toNearestInt(),
                       juce::Justification::centredRight);

    if (node->processor->getNumOutputPorts() > 0)
    {
        float activity = 0.0f;
        for (int port = 0; port < node->processor->getNumOutputPorts(); ++port)
            activity = juce::jmax (activity, node->processor->outputRms (port));
        activity = juce::jlimit (0.0f, 1.0f, std::sqrt (activity));
        const auto ledX = chip.getX() - 56.0f;
        const auto ledY = header.getCentreY();
        graphics.setColour (juce::Colours::black.withAlpha (0.45f));
        graphics.fillEllipse (ledX - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
        if (activity > 0.01f)
        {
            graphics.setColour (colours::okay.withAlpha (0.35f * activity));
            graphics.fillEllipse (ledX - 6.5f, ledY - 6.5f, 13.0f, 13.0f);
        }
        graphics.setColour (activity > 0.01f ? colours::okay.withAlpha (0.35f + 0.65f * activity)
                                             : colours::nodeDark.brighter (0.2f));
        graphics.fillEllipse (ledX - 2.6f, ledY - 2.6f, 5.2f, 5.2f);
    }

    drawPreview (graphics, *node);

    const bool denseInputList = node->processor->getNumInputPorts() > 6;
    for (int port = 0; port < node->processor->getNumInputPorts(); ++port)
    {
        const auto& inputPort = node->processor->getInputPort (port);
        drawPort (graphics, localInputPortCentre (port), inputPort, false,
                  ! (denseInputList && inputPort.type == SignalType::control));
    }
    for (int port = 0; port < node->processor->getNumOutputPorts(); ++port)
        drawPort (graphics, localOutputPortCentre (port), node->processor->getOutputPort (port), true, true,
                  std::sqrt (juce::jmax (0.0f, node->processor->outputRms (port))));

    for (int widgetIndex = 0; widgetIndex < static_cast<int> (parameterWidgets.size()); ++widgetIndex)
    {
        const auto parameterIndex = parameterWidgets[static_cast<std::size_t> (widgetIndex)].parameterIndex;
        const auto column = widgetIndex % 2;
        const auto row = widgetIndex / 2;
        const auto x = 18.0f + column * ((getWidth() - 28.0f) * 0.5f);
        const auto y = controlsTop + row * controlRowHeight;
        graphics.setColour (colours::text.withAlpha (0.9f));
        graphics.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        graphics.drawFittedText (node->processor->getParameter (parameterIndex).name.toUpperCase(),
                                 juce::Rectangle<float> (x, y - 3.0f, 112.0f, 15.0f).toNearestInt(),
                                 juce::Justification::centred, 1);
        graphics.setColour (colours::mutedText.withAlpha (0.8f));
        graphics.setFont (8.0f);
        graphics.drawText ("MOD DEPTH", juce::Rectangle<float> (x, y + 68.0f, 68.0f, 11.0f).toNearestInt(),
                           juce::Justification::centredLeft);
    }

    const auto status = node->processor->statusText();
    if (status.isNotEmpty() && safetyResetButton == nullptr)
    {
        graphics.setColour (node->processor->safetyTripped() ? colours::warning : colours::mutedText);
        graphics.setFont (9.0f);
        graphics.drawFittedText (status,
                                 juce::Rectangle<int> (70, static_cast<int> (railHeight + headerHeight + 90.0f),
                                                       getWidth() - 140, 17),
                                 juce::Justification::centred, 1);
    }

    refreshCommandButtons();
    if (hasStompSwitch())
    {
        const auto centre = stompCentre();
        const auto engaged = ! node->processor->isBypassed();
        graphics.setColour (juce::Colours::white.withAlpha (0.05f));
        graphics.drawHorizontalLine (static_cast<int> (getHeight() - railHeight - stompZoneHeight - 2),
                                     bounds.getX() + 14.0f, bounds.getRight() - 14.0f);

        // Footswitch: outer bezel, brushed cap, status LED.
        graphics.setColour (juce::Colours::black.withAlpha (0.5f));
        graphics.fillEllipse (centre.x - 15.0f, centre.y - 15.0f, 30.0f, 30.0f);
        graphics.setGradientFill (juce::ColourGradient (colours::nodeTop.brighter (0.25f),
                                                        centre.x, centre.y - 13.0f,
                                                        colours::nodeDark.brighter (0.1f),
                                                        centre.x, centre.y + 13.0f, false));
        graphics.fillEllipse (centre.x - 13.0f, centre.y - 13.0f, 26.0f, 26.0f);
        graphics.setColour (juce::Colours::white.withAlpha (0.12f));
        graphics.drawEllipse (centre.x - 10.0f, centre.y - 10.0f, 20.0f, 20.0f, 1.2f);
        graphics.setColour (juce::Colours::black.withAlpha (0.4f));
        graphics.drawEllipse (centre.x - 13.0f, centre.y - 13.0f, 26.0f, 26.0f, 1.2f);

        const auto ledX = centre.x - 44.0f;
        if (engaged)
        {
            graphics.setColour (accent.withAlpha (0.35f));
            graphics.fillEllipse (ledX - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);
            graphics.setColour (accent);
            graphics.fillEllipse (ledX - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);
        }
        else
        {
            graphics.setColour (colours::nodeDark);
            graphics.fillEllipse (ledX - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
            graphics.setColour (colours::mutedText.withAlpha (0.4f));
            graphics.drawEllipse (ledX - 4.0f, centre.y - 4.0f, 8.0f, 8.0f, 1.0f);
        }
        graphics.setColour (colours::mutedText.withAlpha (0.8f));
        graphics.setFont (8.0f);
        graphics.drawText (engaged ? "ON" : "BYP",
                           juce::Rectangle<int> (static_cast<int> (centre.x) + 22,
                                                 static_cast<int> (centre.y) - 6, 40, 12),
                           juce::Justification::centredLeft);
    }

    // Border: selection halo, tripped-safety pulse, or a quiet hairline.
    if (node->processor->safetyTripped())
    {
        const auto pulse = 0.5f + 0.5f * std::sin (static_cast<float> (juce::Time::getMillisecondCounter() % 900)
                                                   / 900.0f * juce::MathConstants<float>::twoPi);
        graphics.setColour (colours::warning.withAlpha (0.35f + 0.3f * pulse));
        graphics.drawRoundedRectangle (bounds.expanded (2.0f), corner + 2.0f, 3.5f);
        graphics.setColour (colours::warning);
        graphics.drawRoundedRectangle (bounds, corner, 2.0f);
    }
    else if (selected)
    {
        graphics.setColour (colours::selection.withAlpha (0.25f));
        graphics.drawRoundedRectangle (bounds.expanded (2.5f), corner + 2.5f, 4.0f);
        graphics.setColour (colours::selection);
        graphics.drawRoundedRectangle (bounds, corner, 2.0f);
    }
    else
    {
        graphics.setColour (juce::Colours::white.withAlpha (0.07f));
        graphics.drawRoundedRectangle (bounds, corner, 1.0f);
    }
}

void NodeComponent::resized()
{
    const auto columnWidth = (getWidth() - 28) / 2;
    for (int widgetIndex = 0; widgetIndex < static_cast<int> (parameterWidgets.size()); ++widgetIndex)
    {
        const auto column = widgetIndex % 2;
        const auto row = widgetIndex / 2;
        const auto x = 14 + column * columnWidth;
        const auto y = static_cast<int> (controlsTop + row * controlRowHeight);
        parameterWidgets[static_cast<std::size_t> (widgetIndex)].value->setBounds (x + 20, y + 10, 82, 58);
        parameterWidgets[static_cast<std::size_t> (widgetIndex)].depth->setBounds (x + 2, y + 78, columnWidth - 10, 16);
    }
    if (safetyResetButton != nullptr)
        safetyResetButton->setBounds (getWidth() / 2 - 45, 128, 90, 20);

    auto bottom = getHeight() - 6 - static_cast<int> (railHeight);
    if (hasStompSwitch())
        bottom -= stompZoneHeight;

    if (scriptEditor != nullptr)
    {
        scriptApplyButton->setBounds (getWidth() - 74, bottom - 24, 62, 20);
        scriptEditor->setBounds (12, bottom - 88, getWidth() - 24, 60);
    }
    else if (! commandButtons.empty())
    {
        const auto* node = model();
        const auto kind = node != nullptr ? node->processor->getKind() : NodeKind::gain;
        if (kind == NodeKind::fourTrack && commandButtons.size() >= 7)
        {
            // Transport row then arm row.
            const auto width = (getWidth() - 24 - 12) / 3;
            for (int index = 0; index < 3; ++index)
                commandButtons[static_cast<std::size_t> (index)].button->setBounds (
                    12 + index * (width + 6), bottom - 56, width, 22);
            const auto armWidth = (getWidth() - 24 - 18) / 4;
            for (int index = 0; index < 4; ++index)
                commandButtons[static_cast<std::size_t> (index + 3)].button->setBounds (
                    12 + index * (armWidth + 6), bottom - 28, armWidth, 22);
        }
        else
        {
            const auto count = static_cast<int> (commandButtons.size());
            const auto width = (getWidth() - 24 - (count - 1) * 6) / count;
            for (int index = 0; index < count; ++index)
                commandButtons[static_cast<std::size_t> (index)].button->setBounds (
                    12 + index * (width + 6), bottom - 26, width, 22);
        }
    }
}

void NodeComponent::paintOverChildren (juce::Graphics& graphics)
{
    const auto* node = model();
    if (node == nullptr || ! node->processor->isBypassed())
        return;

    // Dim everything above the stomp zone so a bypassed module reads as off.
    auto dimmed = getLocalBounds().toFloat().reduced (2.0f);
    dimmed.removeFromBottom (static_cast<float> (stompZoneHeight) + railHeight);
    graphics.setColour (colours::workspace.withAlpha (0.55f));
    graphics.fillRoundedRectangle (dimmed, 8.0f);
    graphics.setColour (colours::mutedText);
    graphics.setFont (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.12f));
    graphics.drawText ("BYPASSED", dimmed.toNearestInt(), juce::Justification::centred);
}

void NodeComponent::mouseDown (const juce::MouseEvent& event)
{
    if (onSelected)
        onSelected (id);

    if (event.mods.isPopupMenu())
    {
        showNodeMenu();
        return;
    }

    if (hasStompSwitch() && event.position.getDistanceFrom (stompCentre()) <= 16.0f)
    {
        if (const auto* node = model())
            engine.setNodeBypassed (id, ! node->processor->isBypassed());
        repaint();
        return;
    }

    if (const auto port = outputPortNear (event.position, 13.0f))
    {
        draggingCable = true;
        draggingOutputPort = *port;
        if (onCableDragStarted)
            onCableDragStarted (id, *port, outputPortCentreInParent (*port));
        return;
    }

    if (editSequencerStepAt (event.position))
    {
        editingSequencerStep = true;
        return;
    }

    if (editDrumCellAt (event.position))
        return;

    if (event.position.y <= headerHeight)
    {
        draggingNode = true;
        dragOffset = event.position;
        toFront (false);
    }
}

void NodeComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (editingSequencerStep)
    {
        editSequencerStepAt (event.position);
        return;
    }
    const auto parentPoint = getPosition().toFloat() + event.position;
    if (draggingCable)
    {
        if (onCableDragged)
            onCableDragged (parentPoint);
        return;
    }

    if (draggingNode)
    {
        auto newPosition = parentPoint - dragOffset;
        newPosition.x = juce::jmax (0.0f, newPosition.x);
        newPosition.y = juce::jmax (0.0f, newPosition.y);
        setTopLeftPosition (newPosition.toInt());
        engine.moveNode (id, newPosition);
        if (getParentComponent() != nullptr)
            getParentComponent()->repaint();
    }
}

void NodeComponent::mouseUp (const juce::MouseEvent& event)
{
    if (draggingCable && onCableDragEnded)
        onCableDragEnded (getPosition().toFloat() + event.position);
    draggingCable = false;
    draggingOutputPort = -1;
    draggingNode = false;
    editingSequencerStep = false;
}

bool NodeComponent::editSequencerStepAt (juce::Point<float> localPoint)
{
    const auto* node = model();
    const auto area = previewBounds().reduced (4.0f);
    if (node == nullptr || node->processor->getKind() != NodeKind::stepSequencer || ! area.contains (localPoint))
        return false;

    const auto stepWidth = area.getWidth() / 8.0f;
    const auto step = juce::jlimit (0, 7, static_cast<int> ((localPoint.x - area.getX()) / stepWidth));
    const auto value = juce::jlimit (-1.0f, 1.0f,
                                     (area.getCentreY() - localPoint.y) / (area.getHeight() * 0.48f));
    engine.setParameter (id, 2 + step, value);
    repaint();
    return true;
}

bool NodeComponent::editDrumCellAt (juce::Point<float> localPoint)
{
    const auto* node = model();
    const auto area = previewBounds().reduced (3.0f);
    if (node == nullptr || node->processor->getKind() != NodeKind::drumMachine || ! area.contains (localPoint))
        return false;

    const auto column = juce::jlimit (0, 7, static_cast<int> ((localPoint.x - area.getX())
                                                              / (area.getWidth() / 8.0f)));
    const auto row = juce::jlimit (0, 2, static_cast<int> ((localPoint.y - area.getY())
                                                           / (area.getHeight() / 3.0f)));
    const auto parameterIndex = 5 + row * 8 + column;
    const auto current = node->processor->getParameter (parameterIndex).getValue();
    engine.setParameter (id, parameterIndex, current > 0.5f ? 0.0f : 1.0f);
    repaint();
    return true;
}
} // namespace signalpatch::ui
