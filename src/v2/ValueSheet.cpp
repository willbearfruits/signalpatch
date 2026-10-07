#include "ValueSheet.h"
#include "Palette.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>


namespace signalpatch::v2
{
void ValueSheet::open (NodeId node, int parameter)
{
    if (active && (node != nodeId || parameter != parameterIndex))
        engine.closeEditGesture();
    nodeId = node;
    parameterIndex = parameter;
    active = current() != nullptr;
    drag.reset();
    pressed = Part::none;
}

void ValueSheet::close() noexcept
{
    if (drag.has_value())
        engine.closeEditGesture();
    active = false;
    drag.reset();
    pressed = Part::none;
}

const DspParameter* ValueSheet::current() const
{
    const auto* model = engine.getDocument().findNode (nodeId);
    if (model == nullptr || ! juce::isPositiveAndBelow (parameterIndex, model->processor->getNumParameters()))
        return nullptr;
    return &model->processor->getParameter (parameterIndex);
}

juce::Rectangle<float> ValueSheet::bounds (Part part) const noexcept
{
    const auto panel = lastPanel;
    const auto x = panel.getX() + 14.0f, right = panel.getRight() - 14.0f;
    const auto sliderTop = panel.getY() + 58.0f, sliderHeight = 56.0f;
    const auto buttonTop = panel.getY() + 122.0f, buttonHeight = 38.0f, buttonWidth = 112.0f;
    switch (part)
    {
        case Part::close:  return { panel.getRight() - 52.0f, panel.getY() + 8.0f, 44.0f, 44.0f };
        case Part::minus:  return { x, sliderTop, 72.0f, sliderHeight };
        case Part::plus:   return { right - 72.0f, sliderTop, 72.0f, sliderHeight };
        case Part::slider: return { x + 86.0f, sliderTop, right - x - 172.0f, sliderHeight };
        case Part::reset:  return { x, buttonTop, buttonWidth, buttonHeight };
        case Part::type:   return { x + (buttonWidth + 10.0f), buttonTop, buttonWidth, buttonHeight };
        case Part::fine:   return { x + 2.0f * (buttonWidth + 10.0f), buttonTop, buttonWidth, buttonHeight };
        case Part::more:   return { x + 3.0f * (buttonWidth + 10.0f), buttonTop, buttonWidth, buttonHeight };
        case Part::none:   break;
    }
    return {};
}

ValueSheet::Part ValueSheet::partAt (float x, float y) const noexcept
{
    for (const auto part : { Part::close, Part::minus, Part::plus, Part::slider, Part::reset, Part::type, Part::fine, Part::more })
        if (bounds (part).contains (x, y))
            return part;
    return Part::none;
}

void ValueSheet::setNormalised (float normalised)
{
    const auto* parameter = current();
    if (parameter == nullptr)
        return;
    engine.setParameter (nodeId, parameterIndex, parameter->valueFromNormalised (juce::jlimit (0.0f, 1.0f, normalised)));
    if (changed)
        changed (nodeId);
}

void ValueSheet::step (int steps)
{
    const auto* parameter = current();
    if (parameter == nullptr || steps == 0)
        return;
    if (parameter->range.interval >= 1.0f)
    {
        // Whole-number knobs (a key, a scale, a switch) move one value per step,
        // in the direction the knob turns.
        const bool rising = parameter->valueFromNormalised (1.0f) >= parameter->valueFromNormalised (0.0f);
        const auto delta = parameter->range.interval * static_cast<float> (steps) * (rising ? 1.0f : -1.0f);
        const auto value = juce::jlimit (parameter->range.start, parameter->range.end, parameter->getValue() + delta);
        setNormalised (parameter->normalisedFromValue (value));
    }
    else
        setNormalised (parameter->getNormalisedValue() + static_cast<float> (steps) * (fine ? 0.002f : 0.01f));
    engine.closeEditGesture();
}

bool ValueSheet::mouseButton (int button, bool isDown, float x, float y)
{
    if (! active)
        return false;
    if (! isDown)
    {
        if (drag.has_value())
        {
            drag.reset();
            engine.closeEditGesture();
            return true;
        }
        const auto released = partAt (x, y);
        const auto was = pressed;
        pressed = Part::none;
        if (was == Part::none || released != was)
            return contains (x, y);
        switch (released)
        {
            case Part::close: close(); break;
            case Part::fine: fine = ! fine; break;
            case Part::reset:
                if (const auto* parameter = current())
                {
                    engine.setParameter (nodeId, parameterIndex, parameter->defaultValue);
                    engine.closeEditGesture();
                    if (changed)
                        changed (nodeId);
                }
                break;
            case Part::type:
                if (const auto* parameter = current(); parameter != nullptr && askText)
                {
                    const auto id = nodeId;
                    const auto index = parameterIndex;
                    askText ("Set " + parameter->name + (parameter->unit.isNotEmpty() ? " (" + parameter->unit + ")" : juce::String()),
                             juce::String (parameter->getValue(), 3), [this, id, index] (const juce::String& text)
                    {
                        const auto* model = engine.getDocument().findNode (id);
                        if (model == nullptr || ! juce::isPositiveAndBelow (index, model->processor->getNumParameters()))
                            return;
                        const auto& target = model->processor->getParameter (index);
                        engine.setParameter (id, index, juce::jlimit (target.range.start, target.range.end, text.getFloatValue()));
                        engine.closeEditGesture();
                        if (changed)
                            changed (id);
                    });
                }
                break;
            case Part::more:
                if (showMenu)
                    showMenu (nodeId, parameterIndex, bounds (Part::more).getX(), bounds (Part::more).getY());
                break;
            case Part::minus: case Part::plus: case Part::slider: case Part::none: break;
        }
        return true;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return contains (x, y);
    if (current() == nullptr)
    {
        close();
        return false;
    }
    pressed = partAt (x, y);
    switch (pressed)
    {
        case Part::minus: step (-1); break;
        case Part::plus:  step (1); break;
        case Part::slider: drag = Drag { x, current()->getNormalisedValue() }; break;
        case Part::none: return contains (x, y);
        case Part::reset: case Part::type: case Part::fine: case Part::more: case Part::close: break;
    }
    return true;
}

bool ValueSheet::mouseMove (float x, float)
{
    if (! active || ! drag.has_value())
        return false;
    const auto width = juce::jmax (40.0f, bounds (Part::slider).getWidth());
    setNormalised (drag->startNormalised + (x - drag->startX) / width * (fine ? 0.2f : 1.0f));
    return true;
}

bool ValueSheet::key (int keyCode, int mods)
{
    if (! active)
        return false;
    juce::ignoreUnused (mods);
    switch (keyCode)
    {
        case GLFW_KEY_LEFT:  step (-1); return true;
        case GLFW_KEY_RIGHT: step (1); return true;
        case GLFW_KEY_DOWN:  step (-10); return true;
        case GLFW_KEY_UP:    step (10); return true;
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            close();
            return true;
        default: return false;
    }
}

void ValueSheet::draw (float left, float right, int windowHeight, float bottomInset)
{
    if (! active)
        return;
    const auto* parameter = current();
    if (parameter == nullptr)
    {
        close();
        return;
    }
    const auto* model = engine.getDocument().findNode (nodeId);
    lastPanel = { left + 10.0f, static_cast<float> (windowHeight) - bottomInset - sheetHeight - 10.0f, right - left - 20.0f, sheetHeight };
    const auto panel = lastPanel;
    const auto colour = familyAccent ([&]
    {
        for (const auto& entry : moduleCatalogue())
            if (entry.kind == model->processor->getKind())
                return juce::String (entry.group);
        return juce::String();
    }());

    nvgBeginPath (vg);
    nvgRoundedRect (vg, panel.getX() + 2.0f, panel.getY() + 6.0f, panel.getWidth(), panel.getHeight(), 12.0f);
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.45f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, panel.getX(), panel.getY(), panel.getWidth(), panel.getHeight(), 12.0f);
    nvgFillColor (vg, alpha (palette::panel, 0.98f));
    nvgFill (vg);
    nvgStrokeColor (vg, alpha (colour, 0.5f));
    nvgStrokeWidth (vg, 1.2f);
    nvgStroke (vg);

    // Names and the value.
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 13.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, panel.getX() + 16.0f, panel.getY() + 22.0f, model->processor->getName().toUpperCase().toRawUTF8(), nullptr);
    nvgFontSize (vg, 17.0f);
    nvgFillColor (vg, palette::text);
    nvgText (vg, panel.getX() + 16.0f, panel.getY() + 44.0f, parameter->name.toUpperCase().toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgFontSize (vg, 28.0f);
    nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, bounds (Part::close).getX() - 16.0f, panel.getY() + 32.0f, formatParameterValue (*parameter, parameter->getValue()).toRawUTF8(), nullptr);

    auto button = [this] (Part part, const char* label, bool lit)
    {
        const auto area = bounds (part);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, area.getX(), area.getY(), area.getWidth(), area.getHeight(), 8.0f);
        nvgFillColor (vg, lit ? mix (palette::panelRaised, palette::selection, 0.35f) : (pressed == part ? lighter (palette::panelRaised, 0.1f) : palette::panelRaised));
        nvgFill (vg);
        nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.1f));
        nvgStrokeWidth (vg, 1.0f);
        nvgStroke (vg);
        nvgFontSize (vg, part == Part::minus || part == Part::plus ? 26.0f : 13.0f);
        nvgTextLetterSpacing (vg, part == Part::minus || part == Part::plus ? 0.0f : 0.8f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::text);
        nvgText (vg, area.getCentreX(), area.getCentreY() - (part == Part::minus || part == Part::plus ? 2.0f : 0.0f), label, nullptr);
        nvgTextLetterSpacing (vg, 0.0f);
    };
    button (Part::minus, "-", false);
    button (Part::plus, "+", false);
    button (Part::reset, "RESET", false);
    button (Part::type, "TYPE", false);
    button (Part::fine, "FINE", fine);
    button (Part::more, "MORE...", false);

    // Close.
    {
        const auto area = bounds (Part::close).reduced (13.0f);
        nvgBeginPath (vg);
        nvgMoveTo (vg, area.getX(), area.getY());
        nvgLineTo (vg, area.getRight(), area.getBottom());
        nvgMoveTo (vg, area.getRight(), area.getY());
        nvgLineTo (vg, area.getX(), area.getBottom());
        nvgStrokeColor (vg, palette::text);
        nvgStrokeWidth (vg, 2.0f);
        nvgStroke (vg);
    }

    // The slider: travel from the knob's minimum to its maximum.
    const auto track = bounds (Part::slider);
    const auto normalised = parameter->getNormalisedValue();
    const auto centreY = track.getCentreY();
    nvgBeginPath (vg);
    nvgRoundedRect (vg, track.getX(), centreY - 4.0f, track.getWidth(), 8.0f, 4.0f);
    nvgFillColor (vg, palette::nodeDark);
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, track.getX(), centreY - 4.0f, track.getWidth() * normalised, 8.0f, 4.0f);
    nvgFillColor (vg, colour);
    nvgFill (vg);
    const auto thumbX = track.getX() + track.getWidth() * normalised;
    nvgBeginPath (vg);
    nvgCircle (vg, thumbX, centreY, drag.has_value() ? 17.0f : 15.0f);
    nvgFillColor (vg, palette::text);
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgCircle (vg, thumbX, centreY, 5.0f);
    nvgFillColor (vg, colour);
    nvgFill (vg);
    const auto defaultX = track.getX() + track.getWidth() * parameter->normalisedFromValue (parameter->defaultValue);
    nvgBeginPath (vg);
    nvgRect (vg, defaultX - 1.0f, centreY + 10.0f, 2.0f, 8.0f);
    nvgFillColor (vg, alpha (palette::mutedText, 0.8f));
    nvgFill (vg);

    // The ends of the travel, under the buttons' row on the right.
    nvgFontSize (vg, 11.5f);
    nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    const auto shape = parameter->getShape();
    nvgText (vg, panel.getRight() - 16.0f, bounds (Part::reset).getCentreY(),
             (formatParameterValue (*parameter, shape.minimum) + "  to  " + formatParameterValue (*parameter, shape.maximum)
              + (fine ? "   (fine: drag moves 1/5)" : "")).toRawUTF8(), nullptr);
}
} // namespace signalpatch::v2
