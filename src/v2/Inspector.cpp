#include "Inspector.h"
#include "Palette.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>

namespace signalpatch::v2
{
Inspector::Inspector (NVGcontext* context, int fontId, PatchEngine& engineToUse)
    : vg (context), font (fontId), engine (engineToUse)
{
}

void Inspector::show (NodeId id, int focusParameter)
{
    if (id != nodeId)
        scrollOffset = 0.0f;
    nodeId = id;
    highlighted = focusParameter;
    active = true;
    drag.reset();
    if (focusParameter >= 0)
    {
        // Bring the asked-for knob into view (the panel height is known after the first draw).
        const auto visible = juce::jmax (rowHeight(), lastPanel.getHeight() - titleHeight - footerHeight);
        const auto top = static_cast<float> (focusParameter) * rowHeight();
        if (top < scrollOffset || top + rowHeight() > scrollOffset + visible)
            scrollOffset = juce::jmax (0.0f, top - rowHeight() * 0.5f);
    }
}

void Inspector::close() noexcept
{
    active = false;
    drag.reset();
    highlighted = -1;
}

const NodeModel* Inspector::model() const
{
    return engine.getDocument().findNode (nodeId);
}

juce::Rectangle<float> Inspector::rowBounds (int row) const noexcept
{
    return { lastPanel.getX(), lastPanel.getY() + titleHeight + static_cast<float> (row) * rowHeight() - scrollOffset,
             lastPanel.getWidth(), rowHeight() };
}

juce::Rectangle<float> Inspector::fieldBounds (int row, Field field) const noexcept
{
    const auto bounds = rowBounds (row);
    const auto lineOne = touchMode ? 32.0f : 24.0f;
    const auto lineTwo = touchMode ? 40.0f : 30.0f;
    const auto left = bounds.getX() + 12.0f;
    const auto inner = bounds.getWidth() - 24.0f;
    if (field == Field::value)
        return { left + 118.0f, bounds.getY() + 8.0f, 108.0f, lineOne };
    const auto gap = 6.0f;
    const auto width = (inner - 3.0f * gap) / 4.0f;
    const auto index = field == Field::minimum ? 0 : field == Field::maximum ? 1 : field == Field::curve ? 2 : 3;
    return { left + static_cast<float> (index) * (width + gap), bounds.getY() + 8.0f + lineOne + 6.0f, width, lineTwo };
}

juce::Rectangle<float> Inspector::closeBounds() const noexcept
{
    return { lastPanel.getRight() - 42.0f, lastPanel.getY() + 6.0f, 34.0f, 34.0f };
}

juce::Rectangle<float> Inspector::resetBounds() const noexcept
{
    return { lastPanel.getX() + 12.0f, lastPanel.getBottom() - footerHeight + 8.0f, 132.0f, footerHeight - 16.0f };
}

Inspector::Hit Inspector::hitAt (float x, float y) const
{
    const auto* node = model();
    if (node == nullptr || y < lastPanel.getY() + titleHeight || y > lastPanel.getBottom() - footerHeight)
        return {};
    for (int row = 0; row < node->processor->getNumParameters(); ++row)
    {
        const auto& parameter = node->processor->getParameter (row);
        for (const auto field : { Field::value, Field::minimum, Field::maximum, Field::curve, Field::depth })
        {
            if (field == Field::depth && parameter.inputPortIndex < 0)
                continue;
            if (fieldBounds (row, field).expanded (touchMode ? 3.0f : 1.0f).contains (x, y))
                return { row, field };
        }
    }
    return {};
}

float Inspector::amountOf (const DspParameter& parameter, Field field) const
{
    const auto shape = parameter.getShape();
    switch (field)
    {
        case Field::value:   return parameter.getNormalisedValue();
        case Field::minimum: return parameter.range.convertTo0to1 (shape.minimum);
        case Field::maximum: return parameter.range.convertTo0to1 (shape.maximum);
        case Field::curve:   return (shape.curve + 1.0f) * 0.5f;
        case Field::depth:   return parameter.getModulationDepth();
        case Field::none:    break;
    }
    return 0.0f;
}

void Inspector::setAmount (int parameterIndex, Field field, float amount)
{
    const auto* node = model();
    if (node == nullptr || ! juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        return;
    const auto& parameter = node->processor->getParameter (parameterIndex);
    amount = juce::jlimit (0.0f, 1.0f, amount);
    auto shape = parameter.getShape();
    switch (field)
    {
        case Field::value:   engine.setParameter (nodeId, parameterIndex, parameter.valueFromNormalised (amount)); break;
        case Field::minimum: shape.minimum = parameter.range.snapToLegalValue (parameter.range.convertFrom0to1 (amount)); break;
        case Field::maximum: shape.maximum = parameter.range.snapToLegalValue (parameter.range.convertFrom0to1 (amount)); break;
        case Field::curve:
            shape.curve = amount * 2.0f - 1.0f;
            if (std::abs (shape.curve) < 0.03f)
                shape.curve = 0.0f; // straight is easy to find again
            break;
        case Field::depth:   engine.setModulationDepth (nodeId, parameterIndex, amount); break;
        case Field::none:    break;
    }
    if (field == Field::minimum || field == Field::maximum || field == Field::curve)
        if (! juce::exactlyEqual (shape.minimum, shape.maximum))
            engine.setParameterShape (nodeId, parameterIndex, shape);
    if (changed)
        changed (nodeId);
}

void Inspector::setFromText (int parameterIndex, Field field, const juce::String& text)
{
    const auto* node = model();
    if (node == nullptr || ! juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        return;
    const auto trimmed = text.trim().toLowerCase();
    if (trimmed.isEmpty())
        return;
    const auto& parameter = node->processor->getParameter (parameterIndex);
    auto number = trimmed.getFloatValue();
    if (trimmed.endsWithChar ('k') || trimmed.endsWith ("khz"))
        number *= 1000.0f; // "2.5k"
    auto shape = parameter.getShape();
    switch (field)
    {
        case Field::value:   engine.setParameter (nodeId, parameterIndex, juce::jlimit (parameter.range.start, parameter.range.end, number)); break;
        case Field::minimum: shape.minimum = number; break;
        case Field::maximum: shape.maximum = number; break;
        case Field::curve:   shape.curve = trimmed.startsWith ("lin") ? 0.0f : number; break;
        case Field::depth:   engine.setModulationDepth (nodeId, parameterIndex, juce::jlimit (0.0f, 1.0f, number / 100.0f)); break;
        case Field::none:    break;
    }
    if (field == Field::minimum || field == Field::maximum || field == Field::curve)
        engine.setParameterShape (nodeId, parameterIndex, shape);
    engine.closeEditGesture();
    if (changed)
        changed (nodeId);
}

void Inspector::resetField (int parameterIndex, Field field)
{
    const auto* node = model();
    if (node == nullptr || ! juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        return;
    const auto& parameter = node->processor->getParameter (parameterIndex);
    auto shape = parameter.getShape();
    const auto natural = parameter.defaultShape();
    switch (field)
    {
        case Field::value:   engine.setParameter (nodeId, parameterIndex, parameter.defaultValue); break;
        case Field::minimum: shape.minimum = natural.minimum; break;
        case Field::maximum: shape.maximum = natural.maximum; break;
        case Field::curve:   shape.curve = 0.0f; break;
        case Field::depth:   engine.setModulationDepth (nodeId, parameterIndex, 0.5f); break;
        case Field::none:    break;
    }
    if (field == Field::minimum || field == Field::maximum || field == Field::curve)
        engine.setParameterShape (nodeId, parameterIndex, shape);
    engine.closeEditGesture();
    if (changed)
        changed (nodeId);
}

void Inspector::prompt (int parameterIndex, Field field)
{
    const auto* node = model();
    if (node == nullptr || ! askText || ! juce::isPositiveAndBelow (parameterIndex, node->processor->getNumParameters()))
        return;
    const auto& parameter = node->processor->getParameter (parameterIndex);
    const auto shape = parameter.getShape();
    const auto unit = parameter.unit.isNotEmpty() ? " (" + parameter.unit + ")" : juce::String();
    const auto limits = "  [" + juce::String (parameter.range.start, 2) + " to " + juce::String (parameter.range.end, 2) + "]";
    juce::String title, initial;
    switch (field)
    {
        case Field::value:   title = parameter.name + unit; initial = juce::String (parameter.getValue(), 3); break;
        case Field::minimum: title = parameter.name + " knob minimum" + unit + limits; initial = juce::String (shape.minimum, 3); break;
        case Field::maximum: title = parameter.name + " knob maximum" + unit + limits; initial = juce::String (shape.maximum, 3); break;
        case Field::curve:   title = parameter.name + " curve  [-1 fine at the top ... 0 straight ... 1 fine at the bottom]"; initial = juce::String (shape.curve, 2); break;
        case Field::depth:   title = parameter.name + " mod depth (%)"; initial = juce::String (juce::roundToInt (parameter.getModulationDepth() * 100.0f)); break;
        case Field::none:    return;
    }
    const auto id = nodeId;
    askText (title, initial, [this, id, parameterIndex, field] (const juce::String& text)
    {
        if (id == nodeId) // still the same module
            setFromText (parameterIndex, field, text);
    });
}

juce::String Inspector::fieldText (const DspParameter& parameter, Field field) const
{
    const auto shape = parameter.getShape();
    switch (field)
    {
        case Field::value:   return formatParameterValue (parameter, parameter.getValue());
        case Field::minimum: return formatParameterValue (parameter, shape.minimum);
        case Field::maximum: return formatParameterValue (parameter, shape.maximum);
        case Field::curve:   return juce::exactlyEqual (shape.curve, 0.0f) ? juce::String ("linear") : (shape.curve > 0.0f ? "+" : "") + juce::String (shape.curve, 2);
        case Field::depth:   return juce::String (juce::roundToInt (parameter.getModulationDepth() * 100.0f)) + "%";
        case Field::none:    break;
    }
    return {};
}

bool Inspector::mouseButton (int button, bool pressed, float x, float y, double now)
{
    juce::ignoreUnused (now);
    if (! active)
        return false;
    if (! pressed)
    {
        if (! drag.has_value())
            return false;
        const auto finished = *drag;
        drag.reset();
        engine.closeEditGesture();
        if (! finished.moved && button == GLFW_MOUSE_BUTTON_LEFT)
            prompt (finished.hit.parameter, finished.hit.field); // a click types the number
        return true;
    }
    if (! lastPanel.contains (x, y))
        return false;
    if (button == GLFW_MOUSE_BUTTON_LEFT && closeBounds().contains (x, y))
    {
        close();
        return true;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && resetBounds().contains (x, y))
    {
        if (const auto* node = model())
        {
            engine.closeEditGesture();
            engine.beginCompoundEditGesture ("reset " + node->processor->getName() + " ranges");
            for (int index = 0; index < node->processor->getNumParameters(); ++index)
                engine.setParameterShape (nodeId, index, node->processor->getParameter (index).defaultShape());
            engine.closeEditGesture();
            if (changed)
                changed (nodeId);
        }
        return true;
    }
    const auto hit = hitAt (x, y);
    if (hit.field != Field::none)
    {
        highlighted = hit.parameter;
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
            resetField (hit.parameter, hit.field);
        else if (button == GLFW_MOUSE_BUTTON_LEFT)
            if (const auto* node = model())
            {
                engine.closeEditGesture();
                drag = Drag { hit, y, amountOf (node->processor->getParameter (hit.parameter), hit.field), false };
            }
    }
    return true; // the panel is opaque: nothing under it gets the click
}

bool Inspector::mouseMove (float x, float y)
{
    juce::ignoreUnused (x);
    if (! drag.has_value())
        return false;
    if (std::abs (y - drag->startY) > 3.0f)
        drag->moved = true;
    if (drag->moved)
        setAmount (drag->hit.parameter, drag->hit.field, drag->startAmount + (drag->startY - y) / (touchMode ? 240.0f : 180.0f));
    return true;
}

bool Inspector::scroll (double dy, float x, float y)
{
    if (! contains (x, y))
        return false;
    const auto* node = model();
    const auto content = node != nullptr ? static_cast<float> (node->processor->getNumParameters()) * rowHeight() : 0.0f;
    const auto visible = lastPanel.getHeight() - titleHeight - footerHeight;
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, content - visible), scrollOffset - static_cast<float> (dy) * 36.0f);
    return true;
}

void Inspector::drawCurve (juce::Rectangle<float> area, const DspParameter& parameter, NVGcolor colour)
{
    nvgBeginPath (vg);
    nvgRoundedRect (vg, area.getX(), area.getY(), area.getWidth(), area.getHeight(), 3.0f);
    nvgFillColor (vg, palette::nodeDark);
    nvgFill (vg);
    const auto inner = area.reduced (3.0f);
    const auto pointAt = [&] (float travel)
    {
        const auto position = parameter.range.convertTo0to1 (parameter.valueFromNormalised (travel));
        return juce::Point<float> (inner.getX() + inner.getWidth() * travel, inner.getBottom() - inner.getHeight() * position);
    };
    nvgBeginPath (vg);
    constexpr int segments = 24;
    for (int step = 0; step <= segments; ++step)
    {
        const auto point = pointAt (static_cast<float> (step) / static_cast<float> (segments));
        if (step == 0) nvgMoveTo (vg, point.x, point.y); else nvgLineTo (vg, point.x, point.y);
    }
    nvgStrokeColor (vg, alpha (colour, 0.9f));
    nvgStrokeWidth (vg, 1.4f);
    nvgStroke (vg);
    const auto live = parameter.getLiveNormalised();
    if (std::abs (live - parameter.getNormalisedValue()) > 0.002f)
    {
        const auto point = pointAt (live);
        nvgBeginPath (vg);
        nvgCircle (vg, point.x, point.y, 2.6f);
        nvgFillColor (vg, palette::control);
        nvgFill (vg);
    }
    const auto point = pointAt (parameter.getNormalisedValue());
    nvgBeginPath (vg);
    nvgCircle (vg, point.x, point.y, 2.4f);
    nvgFillColor (vg, palette::text);
    nvgFill (vg);
}

void Inspector::draw (int windowWidth, int windowHeight, float top, float bottomInset)
{
    if (! active)
        return;
    const auto* node = model();
    if (node == nullptr)
    {
        close(); // the module went away
        return;
    }
    const auto width = juce::jmin (panelWidth, static_cast<float> (windowWidth) - 40.0f);
    lastPanel = { static_cast<float> (windowWidth) - width, top, width, static_cast<float> (windowHeight) - top - bottomInset };
    const auto colour = accent (node->processor->getKind());

    nvgBeginPath (vg);
    nvgRect (vg, lastPanel.getX(), lastPanel.getY(), lastPanel.getWidth(), lastPanel.getHeight());
    nvgFillColor (vg, alpha (palette::panel, 0.97f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRect (vg, lastPanel.getX(), lastPanel.getY(), 2.0f, lastPanel.getHeight());
    nvgFillColor (vg, colour);
    nvgFill (vg);

    nvgFontFaceId (vg, font);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize (vg, 9.0f);
    nvgTextLetterSpacing (vg, 1.2f);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, lastPanel.getX() + 14.0f, lastPanel.getY() + 14.0f, "INSPECTOR", nullptr);
    nvgTextLetterSpacing (vg, 0.4f);
    nvgFontSize (vg, 14.0f);
    nvgFillColor (vg, palette::text);
    nvgText (vg, lastPanel.getX() + 14.0f, lastPanel.getY() + 31.0f,
             (node->processor->getName().toUpperCase() + "  #" + juce::String (nodeId)).toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    {
        const auto cross = closeBounds();
        nvgBeginPath (vg);
        nvgMoveTo (vg, cross.getCentreX() - 6.0f, cross.getCentreY() - 6.0f);
        nvgLineTo (vg, cross.getCentreX() + 6.0f, cross.getCentreY() + 6.0f);
        nvgMoveTo (vg, cross.getCentreX() + 6.0f, cross.getCentreY() - 6.0f);
        nvgLineTo (vg, cross.getCentreX() - 6.0f, cross.getCentreY() + 6.0f);
        nvgStrokeColor (vg, palette::mutedText);
        nvgStrokeWidth (vg, 1.8f);
        nvgStroke (vg);
    }

    const auto rowsTop = lastPanel.getY() + titleHeight;
    const auto rowsHeight = lastPanel.getHeight() - titleHeight - footerHeight;
    const auto count = node->processor->getNumParameters();
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, static_cast<float> (count) * rowHeight() - rowsHeight), scrollOffset);

    nvgSave (vg);
    nvgScissor (vg, lastPanel.getX(), rowsTop, lastPanel.getWidth(), rowsHeight);
    if (count == 0)
    {
        nvgFontSize (vg, 11.0f);
        nvgFillColor (vg, palette::mutedText);
        nvgText (vg, lastPanel.getX() + 14.0f, rowsTop + 22.0f, "This module has no knobs.", nullptr);
    }
    for (int row = 0; row < count; ++row)
    {
        const auto bounds = rowBounds (row);
        if (bounds.getBottom() < rowsTop || bounds.getY() > rowsTop + rowsHeight)
            continue;
        const auto& parameter = node->processor->getParameter (row);
        const auto shape = parameter.getShape();
        const auto natural = parameter.defaultShape();
        if (row == highlighted)
        {
            nvgBeginPath (vg);
            nvgRect (vg, bounds.getX() + 2.0f, bounds.getY() + 2.0f, bounds.getWidth() - 2.0f, bounds.getHeight() - 4.0f);
            nvgFillColor (vg, alpha (colour, 0.08f));
            nvgFill (vg);
        }
        nvgBeginPath (vg);
        nvgRect (vg, bounds.getX() + 12.0f, bounds.getBottom() - 1.0f, bounds.getWidth() - 24.0f, 1.0f);
        nvgFillColor (vg, nvgRGBAf (1, 1, 1, 0.05f));
        nvgFill (vg);

        const auto valueBox = fieldBounds (row, Field::value);
        nvgFontSize (vg, 11.0f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::text);
        nvgSave (vg);
        nvgIntersectScissor (vg, bounds.getX() + 12.0f, valueBox.getY(), 102.0f, valueBox.getHeight());
        nvgText (vg, bounds.getX() + 14.0f, valueBox.getCentreY(), parameter.name.toUpperCase().toRawUTF8(), nullptr);
        nvgRestore (vg);
        drawCurve ({ valueBox.getRight() + 8.0f, valueBox.getY(), bounds.getRight() - 12.0f - valueBox.getRight() - 8.0f, valueBox.getHeight() },
                   parameter, colour);

        for (const auto field : { Field::value, Field::minimum, Field::maximum, Field::curve, Field::depth })
        {
            if (field == Field::depth && parameter.inputPortIndex < 0)
                continue;
            const auto box = fieldBounds (row, field);
            const bool dragging = drag.has_value() && drag->hit.parameter == row && drag->hit.field == field;
            const bool edited = (field == Field::minimum && ! juce::exactlyEqual (shape.minimum, natural.minimum))
                             || (field == Field::maximum && ! juce::exactlyEqual (shape.maximum, natural.maximum))
                             || (field == Field::curve && ! juce::exactlyEqual (shape.curve, 0.0f));
            nvgBeginPath (vg);
            nvgRoundedRect (vg, box.getX(), box.getY(), box.getWidth(), box.getHeight(), 4.0f);
            nvgFillColor (vg, dragging ? lighter (palette::panelRaised, 0.08f) : palette::panelRaised);
            nvgFill (vg);
            nvgStrokeColor (vg, dragging ? alpha (colour, 0.9f) : edited ? alpha (palette::selection, 0.55f) : nvgRGBAf (1, 1, 1, 0.07f));
            nvgStrokeWidth (vg, 1.0f);
            nvgStroke (vg);
            if (field == Field::value)
            {
                nvgFontSize (vg, 11.5f);
                nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor (vg, palette::text);
                nvgText (vg, box.getCentreX(), box.getCentreY(), fieldText (parameter, field).toRawUTF8(), nullptr);
                continue;
            }
            const char* caption = field == Field::minimum ? "MIN" : field == Field::maximum ? "MAX" : field == Field::curve ? "CURVE" : "MOD";
            nvgFontSize (vg, 7.5f);
            nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor (vg, alpha (palette::mutedText, 0.9f));
            nvgText (vg, box.getX() + 5.0f, box.getY() + 3.0f, caption, nullptr);
            nvgFontSize (vg, 10.5f);
            nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor (vg, edited ? palette::selection : palette::text);
            nvgText (vg, box.getCentreX(), box.getBottom() - box.getHeight() * 0.36f, fieldText (parameter, field).toRawUTF8(), nullptr);
        }
    }
    nvgRestore (vg);

    // Footer: reset everything, and how the fields work.
    nvgBeginPath (vg);
    nvgRect (vg, lastPanel.getX() + 2.0f, lastPanel.getBottom() - footerHeight, lastPanel.getWidth() - 2.0f, footerHeight);
    nvgFillColor (vg, palette::panel);
    nvgFill (vg);
    const auto reset = resetBounds();
    nvgBeginPath (vg);
    nvgRoundedRect (vg, reset.getX(), reset.getY(), reset.getWidth(), reset.getHeight(), 4.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgFontSize (vg, 9.5f);
    nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, reset.getCentreX(), reset.getCentreY(), "RESET ALL RANGES", nullptr);
    nvgFontSize (vg, 8.5f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, reset.getRight() + 10.0f, reset.getCentreY() - 6.0f, touchMode ? "drag a field, tap to type" : "drag a field, click to type", nullptr);
    nvgText (vg, reset.getRight() + 10.0f, reset.getCentreY() + 6.0f, touchMode ? "hold a field to reset it" : "right-click resets it", nullptr);
}
} // namespace signalpatch::v2
