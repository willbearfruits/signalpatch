#include "Menu.h"
#include "Palette.h"

#include <GLFW/glfw3.h>

#include <cmath>

namespace signalpatch::v2
{
// ---------------------------------------------------------------- Menu

float Menu::rowHeight (const MenuItem& item) const noexcept
{
    if (item.separator)
        return separatorHeight;
    if (item.header)
        return headerHeight;
    return itemHeight;
}

float Menu::itemTop (const Level& level, int index) const noexcept
{
    auto y = level.y + padding;
    for (int i = 0; i < index; ++i)
        y += rowHeight (level.items[static_cast<std::size_t> (i)]);
    return y;
}

int Menu::itemAt (const Level& level, float x, float y) const noexcept
{
    if (! contains (level, x, y))
        return -1;
    auto top = level.y + padding;
    for (std::size_t i = 0; i < level.items.size(); ++i)
    {
        const auto height = rowHeight (level.items[i]);
        if (y >= top && y < top + height)
            return level.items[i].enabled || ! level.items[i].children.empty() ? static_cast<int> (i) : -1;
        top += height;
    }
    return -1;
}

bool Menu::contains (const Level& level, float x, float y) const noexcept
{
    return x >= level.x && x <= level.x + level.w && y >= level.y && y <= level.y + level.h;
}

void Menu::pushLevel (std::vector<MenuItem> items, float x, float y)
{
    Level level;
    level.items = std::move (items);
    float width = 150.0f;
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 12.0f);
    for (const auto& item : level.items)
    {
        float bounds[4] {};
        nvgTextBounds (vg, 0.0f, 0.0f, item.text.toRawUTF8(), nullptr, bounds);
        auto needed = bounds[2] - bounds[0] + 44.0f;
        if (item.shortcut.isNotEmpty())
        {
            nvgTextBounds (vg, 0.0f, 0.0f, item.shortcut.toRawUTF8(), nullptr, bounds);
            needed += bounds[2] - bounds[0] + 24.0f;
        }
        if (! item.children.empty())
            needed += 18.0f;
        width = juce::jmax (width, needed);
    }
    level.w = width;
    level.h = padding * 2.0f;
    for (const auto& item : level.items)
        level.h += rowHeight (item);
    level.x = juce::jlimit (4.0f, juce::jmax (4.0f, static_cast<float> (windowW) - level.w - 4.0f), x);
    level.y = juce::jlimit (4.0f, juce::jmax (4.0f, static_cast<float> (windowH) - level.h - 4.0f), y);
    levels.push_back (std::move (level));
}

void Menu::open (std::vector<MenuItem> items, float x, float y, std::function<void (int)> onPick)
{
    levels.clear();
    callback = std::move (onPick);
    pushLevel (std::move (items), x, y);
}

void Menu::close()
{
    levels.clear();
    callback = nullptr;
}

bool Menu::mouseMove (float x, float y)
{
    if (levels.empty())
        return false;
    // Deepest level containing the pointer wins; hovering an item with
    // children opens it and discards anything deeper.
    for (int depth = static_cast<int> (levels.size()) - 1; depth >= 0; --depth)
    {
        auto& level = levels[static_cast<std::size_t> (depth)];
        const auto index = itemAt (level, x, y);
        if (! contains (level, x, y))
            continue;
        if (index == level.hover)
            return true;
        level.hover = index;
        levels.resize (static_cast<std::size_t> (depth) + 1);
        level.openedChild = -1;
        if (index >= 0 && ! level.items[static_cast<std::size_t> (index)].children.empty())
        {
            level.openedChild = index;
            const auto childX = level.x + level.w - 4.0f;
            const auto childY = itemTop (level, index) - padding;
            pushLevel (level.items[static_cast<std::size_t> (index)].children, childX, childY);
            if (levels.back().x < childX) // no room on the right: flip to the left
                levels.back().x = juce::jmax (4.0f, level.x - levels.back().w + 4.0f);
        }
        return true;
    }
    return true; // pointer outside every level: still ours while open
}

bool Menu::mouseButton (int button, bool pressed, float x, float y)
{
    if (levels.empty())
        return false;
    if (! pressed)
        return true;
    for (int depth = static_cast<int> (levels.size()) - 1; depth >= 0; --depth)
    {
        auto& level = levels[static_cast<std::size_t> (depth)];
        if (! contains (level, x, y))
            continue;
        const auto index = itemAt (level, x, y);
        if (index < 0)
            return true;
        auto& item = level.items[static_cast<std::size_t> (index)];
        if (! item.children.empty())
        {
            mouseMove (x, y);
            return true;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            const auto picked = item.id;
            auto done = std::move (callback);
            close();
            if (done)
                done (picked);
        }
        return true;
    }
    close(); // clicked outside
    return true;
}

bool Menu::key (int keyCode)
{
    if (levels.empty())
        return false;
    if (keyCode == GLFW_KEY_ESCAPE)
        close();
    return true;
}

void Menu::draw (int windowWidth, int windowHeight)
{
    windowW = windowWidth;
    windowH = windowHeight;
    for (const auto& level : levels)
    {
        nvgBeginPath (vg);
        nvgRoundedRect (vg, level.x + 2.0f, level.y + 5.0f, level.w, level.h, 6.0f);
        nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.45f));
        nvgFill (vg);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, level.x, level.y, level.w, level.h, 6.0f);
        nvgFillColor (vg, palette::panelRaised);
        nvgFill (vg);
        nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.08f));
        nvgStrokeWidth (vg, 1.0f);
        nvgStroke (vg);

        nvgFontFaceId (vg, font);
        auto top = level.y + padding;
        for (std::size_t i = 0; i < level.items.size(); ++i)
        {
            const auto& item = level.items[i];
            const auto height = rowHeight (item);
            if (item.separator)
            {
                nvgBeginPath (vg);
                nvgMoveTo (vg, level.x + 10.0f, top + height * 0.5f);
                nvgLineTo (vg, level.x + level.w - 10.0f, top + height * 0.5f);
                nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.08f));
                nvgStroke (vg);
            }
            else if (item.header)
            {
                nvgFontSize (vg, 9.5f);
                nvgTextLetterSpacing (vg, 1.0f);
                nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor (vg, alpha (palette::mutedText, 0.9f));
                nvgText (vg, level.x + 14.0f, top + height * 0.5f, item.text.toRawUTF8(), nullptr);
                nvgTextLetterSpacing (vg, 0.0f);
            }
            else
            {
                const bool hovered = static_cast<int> (i) == level.hover;
                if (hovered && (item.enabled || ! item.children.empty()))
                {
                    nvgBeginPath (vg);
                    nvgRoundedRect (vg, level.x + 4.0f, top, level.w - 8.0f, height, 4.0f);
                    nvgFillColor (vg, lighter (palette::grid, 0.3f));
                    nvgFill (vg);
                }
                nvgFontSize (vg, 12.0f);
                nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgFillColor (vg, item.enabled || ! item.children.empty() ? palette::text : alpha (palette::mutedText, 0.5f));
                nvgText (vg, level.x + 14.0f, top + height * 0.5f, item.text.toRawUTF8(), nullptr);
                if (item.shortcut.isNotEmpty())
                {
                    nvgFontSize (vg, 10.5f);
                    nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                    nvgFillColor (vg, alpha (palette::mutedText, 0.8f));
                    nvgText (vg, level.x + level.w - 14.0f, top + height * 0.5f, item.shortcut.toRawUTF8(), nullptr);
                }
                if (! item.children.empty())
                {
                    const auto ax = level.x + level.w - 14.0f, ay = top + height * 0.5f;
                    nvgBeginPath (vg);
                    nvgMoveTo (vg, ax - 4.0f, ay - 4.0f);
                    nvgLineTo (vg, ax, ay);
                    nvgLineTo (vg, ax - 4.0f, ay + 4.0f);
                    nvgStrokeColor (vg, palette::mutedText);
                    nvgStrokeWidth (vg, 1.4f);
                    nvgStroke (vg);
                }
            }
            top += height;
        }
    }
}

// ---------------------------------------------------------------- TextPrompt

void TextPrompt::open (juce::String promptTitle, juce::String initial, std::function<void (const juce::String&)> onAccept)
{
    title = std::move (promptTitle);
    text = std::move (initial);
    accept = std::move (onAccept);
    active = true;
}

bool TextPrompt::key (int keyCode, int mods)
{
    if (! active)
        return false;
    if (keyCode == GLFW_KEY_ESCAPE)
        active = false;
    else if (keyCode == GLFW_KEY_ENTER || keyCode == GLFW_KEY_KP_ENTER)
    {
        active = false;
        if (accept)
            accept (text);
    }
    else if (keyCode == GLFW_KEY_BACKSPACE)
    {
        if ((mods & GLFW_MOD_CONTROL) != 0)
            text.clear();
        else if (text.isNotEmpty())
            text = text.dropLastCharacters (1);
    }
    return true;
}

bool TextPrompt::character (juce::juce_wchar codepoint)
{
    if (! active)
        return false;
    if (codepoint >= 32 && text.length() < 80)
        text += juce::String::charToString (codepoint);
    return true;
}

void TextPrompt::draw (int windowWidth, int windowHeight, double now)
{
    if (! active)
        return;
    const float w = 420.0f, h = 92.0f;
    const auto x = (windowWidth - w) * 0.5f, y = (windowHeight - h) * 0.42f;
    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (windowWidth), static_cast<float> (windowHeight));
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.35f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, x, y, w, h, 8.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.1f));
    nvgStroke (vg);
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 11.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, x + 16.0f, y + 20.0f, title.toUpperCase().toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, x + 14.0f, y + 36.0f, w - 28.0f, 30.0f, 4.0f);
    nvgFillColor (vg, palette::nodeDark);
    nvgFill (vg);
    nvgFontSize (vg, 14.0f);
    nvgFillColor (vg, palette::text);
    nvgText (vg, x + 24.0f, y + 51.0f, text.toRawUTF8(), nullptr);
    float bounds[4] {};
    nvgTextBounds (vg, x + 24.0f, y + 51.0f, text.toRawUTF8(), nullptr, bounds);
    if (std::fmod (now, 1.0) < 0.55)
    {
        nvgBeginPath (vg);
        nvgRect (vg, bounds[2] + 1.0f, y + 43.0f, 1.5f, 16.0f);
        nvgFillColor (vg, palette::selection);
        nvgFill (vg);
    }
    nvgFontSize (vg, 9.5f);
    nvgFillColor (vg, alpha (palette::mutedText, 0.7f));
    nvgText (vg, x + 16.0f, y + 78.0f, "Enter to apply   Esc to cancel", nullptr);
}
} // namespace signalpatch::v2
