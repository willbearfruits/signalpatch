#include "ControllerPage.h"
#include "Palette.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace signalpatch::v2
{
namespace
{
    int settingOr (const std::map<int, int>& settings, int key, int fallback)
    {
        const auto found = settings.find (key);
        return found != settings.end() ? found->second : fallback;
    }

    const controller::SettingInfo* settingInfo (int key)
    {
        for (const auto& info : controller::handheldSettings())
            if (info.key == key)
                return &info;
        return nullptr;
    }

    juce::String settingText (const controller::SettingInfo& info, int value)
    {
        if (juce::isPositiveAndBelow (value, static_cast<int> (info.choices.size())))
            return info.choices[static_cast<std::size_t> (value)];
        return juce::String (value);
    }
} // namespace

std::vector<ControllerPage::Control> ControllerPage::controlsFor (const std::map<int, int>& settings)
{
    using Source = MidiMapping::Source;
    const auto channel = settingOr (settings, 0x00, 10);
    return {
        { "Button 1", Source::note, channel, settingOr (settings, 0x08, 60), false },
        { "Button 2", Source::note, channel, settingOr (settings, 0x09, 61), false },
        { "Button 3", Source::note, channel, settingOr (settings, 0x0A, 62), false },
        { "Encoder push", Source::note, channel, settingOr (settings, 0x0B, 63), false },
        { "Encoder", Source::controlChange, channel, settingOr (settings, 0x07, 20), true },
        { "Pot 1", Source::controlChange, channel, settingOr (settings, 0x01, 16), false },
        { "Pot 2", Source::controlChange, channel, settingOr (settings, 0x02, 17), false },
        { "Pot 3", Source::controlChange, channel, settingOr (settings, 0x03, 18), false },
        { "Expression", Source::controlChange, channel, settingOr (settings, 0x04, 11), false },
        { "Gyro tilt", Source::controlChange, channel, settingOr (settings, 0x05, 12), false },
        { "Gyro roll", Source::controlChange, channel, settingOr (settings, 0x06, 13), false },
    };
}

void ControllerPage::open() noexcept
{
    active = true;
    drag.reset();
    scrollOffset = 0.0f;
    engineSettingsSeen.clear(); // take the controller's settings afresh
    if (const auto current = controller(); current.has_value() && current->model == controller::handheldC3 && ! current->settingsKnown)
        engine.requestControllerSettings (current->name);
}

std::optional<PatchEngine::ConnectedController> ControllerPage::controller() const
{
    const auto all = engine.getControllers();
    for (const auto& candidate : all)
        if (candidate.model == controller::handheldC3)
            return candidate;
    if (! all.empty())
        return all.front();
    return std::nullopt;
}

std::vector<ControllerPage::Control> ControllerPage::controls() const
{
    const auto current = controller();
    return controlsFor (current.has_value() && current->settingsKnown ? current->settings : std::map<int, int> {});
}

int ControllerPage::rowCount() const
{
    return tab == Tab::controls ? static_cast<int> (controls().size()) : static_cast<int> (controller::handheldSettings().size());
}

juce::Rectangle<float> ControllerPage::listArea() const noexcept
{
    return { lastPanel.getX(), lastPanel.getY() + headerHeight(), lastPanel.getWidth(), lastPanel.getHeight() - headerHeight() - footerHeight() };
}

juce::Rectangle<float> ControllerPage::rowBounds (int row) const noexcept
{
    const auto list = listArea();
    return { list.getX() + 8.0f, list.getY() + static_cast<float> (row) * rowHeight() - scrollOffset, list.getWidth() - 16.0f, rowHeight() - 4.0f };
}

juce::Rectangle<float> ControllerPage::partBounds (Part part, int row) const noexcept
{
    const auto panel = lastPanel;
    const auto tabTop = panel.getY() + headerHeight() - (touchMode ? 48.0f : 38.0f);
    const auto tabHeight = touchMode ? 42.0f : 32.0f;
    const auto halfWidth = (panel.getWidth() - 24.0f) * 0.5f;
    const auto footerTop = panel.getBottom() - footerHeight() + 7.0f;
    const auto footerButtonHeight = footerHeight() - 14.0f;
    const auto bounds = rowBounds (row);
    const auto inner = bounds.getHeight() - 16.0f;
    switch (part)
    {
        case Part::close:       return { panel.getRight() - (touchMode ? 52.0f : 40.0f), panel.getY() + 6.0f, touchMode ? 44.0f : 32.0f, touchMode ? 44.0f : 32.0f };
        case Part::tabControls: return { panel.getX() + 8.0f, tabTop, halfWidth, tabHeight };
        case Part::tabSettings: return { panel.getX() + 16.0f + halfWidth, tabTop, halfWidth, tabHeight };
        case Part::assign:      return { bounds.getRight() - (touchMode ? 96.0f : 76.0f), bounds.getY() + 8.0f, touchMode ? 88.0f : 70.0f, inner };
        case Part::plus:        return { bounds.getRight() - (touchMode ? 52.0f : 38.0f), bounds.getY() + 8.0f, touchMode ? 44.0f : 32.0f, inner };
        case Part::value:       return { bounds.getRight() - (touchMode ? 52.0f : 38.0f) - 6.0f - 128.0f, bounds.getY() + 8.0f, 128.0f, inner };
        case Part::minus:       return { bounds.getRight() - (touchMode ? 52.0f : 38.0f) - 12.0f - 128.0f - (touchMode ? 44.0f : 32.0f), bounds.getY() + 8.0f,
                                         touchMode ? 44.0f : 32.0f, inner };
        case Part::footerLeft:  return { panel.getX() + 8.0f, footerTop, halfWidth, footerButtonHeight };
        case Part::footerRight: return { panel.getX() + 16.0f + halfWidth, footerTop, halfWidth, footerButtonHeight };
        case Part::none:        break;
    }
    return {};
}

ControllerPage::Hit ControllerPage::hitAt (float x, float y) const noexcept
{
    for (const auto part : { Part::close, Part::tabControls, Part::tabSettings, Part::footerLeft, Part::footerRight })
        if (partBounds (part, 0).contains (x, y))
            return { part, -1 };
    const auto list = listArea();
    if (! list.contains (x, y))
        return {};
    const auto row = static_cast<int> ((y - list.getY() + scrollOffset) / rowHeight());
    if (! juce::isPositiveAndBelow (row, rowCount()))
        return {};
    const auto parts = tab == Tab::controls ? std::initializer_list<Part> { Part::assign }
                                            : std::initializer_list<Part> { Part::minus, Part::value, Part::plus };
    for (const auto part : parts)
        if (partBounds (part, row).expanded (2.0f).contains (x, y))
            return { part, row };
    return { Part::none, row };
}

void ControllerPage::clampScroll()
{
    const auto content = static_cast<float> (rowCount()) * rowHeight();
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, content - listArea().getHeight()), scrollOffset);
}

void ControllerPage::setSetting (int key, int value)
{
    const auto* info = settingInfo (key);
    const auto current = controller();
    if (info == nullptr || ! current.has_value())
        return;
    value = juce::jlimit (info->minimum, info->maximum, value);
    shownSettings[key] = value;
    engine.setControllerSettings (current->name, { { key, value } });
}

void ControllerPage::changeSetting (int key, int delta)
{
    const auto* info = settingInfo (key);
    if (info != nullptr)
        setSetting (key, settingOr (shownSettings, key, info->defaultValue) + delta);
}

void ControllerPage::activate (const Hit& hit)
{
    const auto current = controller();
    switch (hit.part)
    {
        case Part::close: close(); break;
        case Part::tabControls: tab = Tab::controls; scrollOffset = 0.0f; break;
        case Part::tabSettings:
            tab = Tab::settings;
            scrollOffset = 0.0f;
            if (current.has_value() && ! current->settingsKnown)
                engine.requestControllerSettings (current->name);
            break;
        case Part::assign:
        {
            const auto all = controls();
            if (juce::isPositiveAndBelow (hit.row, static_cast<int> (all.size())) && showAssignMenu)
            {
                const auto area = partBounds (Part::assign, hit.row);
                showAssignMenu (all[static_cast<std::size_t> (hit.row)], area.getX() - 200.0f, area.getBottom());
            }
            break;
        }
        case Part::minus:
        case Part::plus:
        case Part::value:
        {
            const auto& settings = controller::handheldSettings();
            if (! current.has_value() || ! current->settingsKnown || ! juce::isPositiveAndBelow (hit.row, static_cast<int> (settings.size())))
                break;
            const auto key = settings[static_cast<std::size_t> (hit.row)].key;
            if (hit.part == Part::value)
            {
                if (showSettingChoices)
                {
                    const auto area = partBounds (Part::value, hit.row);
                    showSettingChoices (key, area.getX(), area.getBottom());
                }
            }
            else
                changeSetting (key, hit.part == Part::plus ? 1 : -1);
            break;
        }
        case Part::footerLeft:
            if (tab == Tab::controls)
            {
                if (applyDefaultLayout)
                    applyDefaultLayout();
            }
            else if (current.has_value())
                engine.requestControllerSettings (current->name);
            break;
        case Part::footerRight:
            if (current.has_value())
            {
                if (tab == Tab::controls)
                    engine.zeroControllerGyro (current->name);
                else
                    engine.resetControllerSettings (current->name);
            }
            break;
        case Part::none: break;
    }
}

bool ControllerPage::mouseButton (int button, bool pressed, float x, float y)
{
    if (! active)
        return false;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return contains (x, y);
    if (pressed)
    {
        if (! contains (x, y))
            return false;
        drag = Drag { y, scrollOffset, false, hitAt (x, y) };
        return true;
    }
    if (! drag.has_value())
        return contains (x, y);
    const auto finished = *drag;
    drag.reset();
    if (! finished.moved)
    {
        const auto released = hitAt (x, y);
        if (released.part == finished.pressed.part && released.row == finished.pressed.row)
            activate (released);
    }
    return true;
}

bool ControllerPage::mouseMove (float, float y)
{
    if (! active || ! drag.has_value())
        return false;
    if (std::abs (y - drag->startY) > 8.0f)
        drag->moved = true;
    if (drag->moved)
    {
        scrollOffset = drag->startScroll - (y - drag->startY);
        clampScroll();
    }
    return true;
}

bool ControllerPage::scroll (double dy, float x, float y)
{
    if (! contains (x, y))
        return false;
    scrollOffset -= static_cast<float> (dy) * 40.0f;
    clampScroll();
    return true;
}

bool ControllerPage::wantsRedraw()
{
    if (! active)
        return false;
    bool changed = false;
    if (engine.getControlActivityVersion() != seenActivity)
    {
        seenActivity = engine.getControlActivityVersion();
        changed = true;
    }
    if (const auto current = controller(); current.has_value() && current->settingsKnown && current->settings != engineSettingsSeen)
    {
        engineSettingsSeen = current->settings;
        shownSettings = current->settings; // the controller's word wins over what the page guessed
        changed = true;
    }
    return changed;
}

void ControllerPage::draw (int windowWidth, int windowHeight, float top, float bottomInset)
{
    if (! active)
        return;
    const auto width = touchMode ? 470.0f : 410.0f;
    lastPanel = { static_cast<float> (windowWidth) - width, top, width, static_cast<float> (windowHeight) - top - bottomInset };
    clampScroll();
    const auto panel = lastPanel;
    const auto current = controller();
    const bool hasSettings = current.has_value() && current->settingsKnown;

    nvgBeginPath (vg);
    nvgRect (vg, panel.getX(), panel.getY(), panel.getWidth(), panel.getHeight());
    nvgFillColor (vg, alpha (palette::panel, 0.98f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgMoveTo (vg, panel.getX(), panel.getY());
    nvgLineTo (vg, panel.getX(), panel.getBottom());
    nvgStrokeColor (vg, nvgRGBAf (0, 0, 0, 0.5f));
    nvgStrokeWidth (vg, 1.0f);
    nvgStroke (vg);

    nvgFontFaceId (vg, font);
    nvgFontSize (vg, touchMode ? 16.0f : 14.0f);
    nvgTextLetterSpacing (vg, 1.0f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, panel.getX() + 16.0f, panel.getY() + (touchMode ? 28.0f : 22.0f), "CONTROLLER", nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    {
        const auto area = partBounds (Part::close, 0).reduced (touchMode ? 13.0f : 10.0f);
        nvgBeginPath (vg);
        nvgMoveTo (vg, area.getX(), area.getY());
        nvgLineTo (vg, area.getRight(), area.getBottom());
        nvgMoveTo (vg, area.getRight(), area.getY());
        nvgLineTo (vg, area.getX(), area.getBottom());
        nvgStrokeColor (vg, palette::text);
        nvgStrokeWidth (vg, 2.0f);
        nvgStroke (vg);
    }

    // Who is connected.
    juce::String status;
    if (! current.has_value())
        status = "No controller has said hello. Pair \"SignalPatch C3\" in the Bluetooth settings; it is picked up a few seconds after it connects. "
                 "Any other MIDI controller works through MIDI learn.";
    else
    {
        status = current->name;
        if (current->model == controller::handheldC3)
            status << "  -  C3 handheld, firmware " << current->firmwareMajor << "." << current->firmwareMinor;
        else if (current->model == controller::footController)
            status << "  -  foot controller";
        if (! current->hasOutput)
            status << "  -  no feedback port found";
        if (current->model == controller::handheldC3 && ! hasSettings)
            status << "  -  waiting for its settings";
    }
    nvgFontSize (vg, touchMode ? 12.5f : 11.0f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor (vg, current.has_value() ? palette::okay : palette::mutedText);
    nvgTextBox (vg, panel.getX() + 16.0f, panel.getY() + (touchMode ? 52.0f : 42.0f), panel.getWidth() - 32.0f, status.toRawUTF8(), nullptr);

    auto button = [this] (juce::Rectangle<float> area, const juce::String& label, bool lit, bool enabled)
    {
        nvgBeginPath (vg);
        nvgRoundedRect (vg, area.getX(), area.getY(), area.getWidth(), area.getHeight(), 6.0f);
        nvgFillColor (vg, lit ? mix (palette::panelRaised, palette::control, 0.35f) : palette::panelRaised);
        nvgFill (vg);
        nvgFontSize (vg, touchMode ? 12.5f : 10.5f);
        nvgTextLetterSpacing (vg, 0.8f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, enabled ? palette::text : alpha (palette::mutedText, 0.5f));
        nvgText (vg, area.getCentreX(), area.getCentreY(), label.toRawUTF8(), nullptr);
        nvgTextLetterSpacing (vg, 0.0f);
    };
    button (partBounds (Part::tabControls, 0), "CONTROLS", tab == Tab::controls, true);
    button (partBounds (Part::tabSettings, 0), "SETTINGS", tab == Tab::settings, true);

    // The list.
    const auto list = listArea();
    nvgSave (vg);
    nvgScissor (vg, list.getX(), list.getY(), list.getWidth(), list.getHeight());
    if (tab == Tab::controls)
    {
        const auto all = controls();
        for (int row = 0; row < static_cast<int> (all.size()); ++row)
        {
            const auto bounds = rowBounds (row);
            if (bounds.getBottom() < list.getY() || bounds.getY() > list.getBottom())
                continue;
            const auto& control = all[static_cast<std::size_t> (row)];
            nvgBeginPath (vg);
            nvgRoundedRect (vg, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 6.0f);
            nvgFillColor (vg, alpha (palette::panelRaised, 0.55f));
            nvgFill (vg);
            nvgFontSize (vg, touchMode ? 14.0f : 12.5f);
            nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor (vg, palette::text);
            nvgText (vg, bounds.getX() + 12.0f, bounds.getY() + bounds.getHeight() * 0.36f, control.name.toRawUTF8(), nullptr);
            nvgFontSize (vg, touchMode ? 11.5f : 10.0f);
            nvgFillColor (vg, palette::mutedText);
            const auto message = (control.source == MidiMapping::Source::note ? "note " : "CC ") + juce::String (control.number)
                               + (control.relative ? " relative" : "") + "  ch " + juce::String (control.channel);
            nvgText (vg, bounds.getX() + 12.0f, bounds.getY() + bounds.getHeight() * 0.72f, message.toRawUTF8(), nullptr);

            // What it drives, and its last value under it.
            const auto assign = partBounds (Part::assign, row);
            const auto textX = bounds.getX() + (touchMode ? 150.0f : 128.0f);
            const auto textWidth = assign.getX() - 10.0f - textX;
            const auto described = describe ? describe (control) : juce::String();
            const auto drives = described.upToFirstOccurrenceOf ("\n", false, false);
            const auto where = described.fromFirstOccurrenceOf ("\n", false, false);
            nvgSave (vg);
            nvgIntersectScissor (vg, textX, bounds.getY(), juce::jmax (0.0f, textWidth), bounds.getHeight());
            nvgFontSize (vg, touchMode ? 12.5f : 11.0f);
            nvgFillColor (vg, drives.isEmpty() ? alpha (palette::mutedText, 0.6f) : palette::text);
            nvgText (vg, textX, bounds.getY() + bounds.getHeight() * 0.36f, drives.isEmpty() ? "nothing" : drives.toRawUTF8(), nullptr);
            // Where the mapping lives, then the last value as a bar.
            nvgFontSize (vg, touchMode ? 10.5f : 9.5f);
            nvgFillColor (vg, palette::mutedText);
            nvgText (vg, textX, bounds.getY() + bounds.getHeight() * 0.72f, where.toRawUTF8(), nullptr);
            nvgRestore (vg);
            if (const auto value = engine.getLastControlValue (control.source, control.number))
            {
                const auto barX = textX + (touchMode ? 82.0f : 70.0f), barWidth = textX + textWidth - barX;
                const auto barY = bounds.getY() + bounds.getHeight() * 0.72f - 2.5f;
                nvgBeginPath (vg);
                nvgRoundedRect (vg, barX, barY, barWidth, 5.0f, 2.5f);
                nvgFillColor (vg, palette::nodeDark);
                nvgFill (vg);
                nvgBeginPath (vg);
                nvgRoundedRect (vg, barX, barY, barWidth * static_cast<float> (*value) / 127.0f, 5.0f, 2.5f);
                nvgFillColor (vg, palette::control);
                nvgFill (vg);
            }
            button (assign, "ASSIGN", false, true);
        }
    }
    else
    {
        const auto& settings = controller::handheldSettings();
        for (int row = 0; row < static_cast<int> (settings.size()); ++row)
        {
            const auto bounds = rowBounds (row);
            if (bounds.getBottom() < list.getY() || bounds.getY() > list.getBottom())
                continue;
            const auto& info = settings[static_cast<std::size_t> (row)];
            nvgBeginPath (vg);
            nvgRoundedRect (vg, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 6.0f);
            nvgFillColor (vg, alpha (palette::panelRaised, 0.55f));
            nvgFill (vg);
            const auto minus = partBounds (Part::minus, row);
            nvgSave (vg);
            nvgIntersectScissor (vg, bounds.getX(), bounds.getY(), minus.getX() - bounds.getX() - 6.0f, bounds.getHeight());
            nvgFontSize (vg, touchMode ? 13.0f : 11.5f);
            nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor (vg, hasSettings ? palette::text : palette::mutedText);
            nvgText (vg, bounds.getX() + 12.0f, bounds.getCentreY(), info.name, nullptr);
            nvgRestore (vg);
            const auto value = settingOr (shownSettings, info.key, info.defaultValue);
            button (minus, "-", false, hasSettings && value > info.minimum);
            button (partBounds (Part::value, row), hasSettings ? settingText (info, value) : juce::String ("-"), false, hasSettings);
            button (partBounds (Part::plus, row), "+", false, hasSettings && value < info.maximum);
        }
    }
    nvgRestore (vg);

    if (tab == Tab::controls)
    {
        button (partBounds (Part::footerLeft, 0), "DEFAULT LAYOUT", false, true);
        button (partBounds (Part::footerRight, 0), "ZERO THE GYRO", false, current.has_value());
    }
    else
    {
        button (partBounds (Part::footerLeft, 0), "READ AGAIN", false, current.has_value());
        button (partBounds (Part::footerRight, 0), "BACK TO DEFAULTS", false, hasSettings);
    }
}
} // namespace signalpatch::v2
