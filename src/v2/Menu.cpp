#include "Menu.h"
#include "Palette.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
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
        openChild (depth, index);
        return true;
    }
    return true; // pointer outside every level: still ours while open
}

void Menu::openChild (int depth, int index)
{
    levels.resize (static_cast<std::size_t> (depth) + 1);
    auto& level = levels[static_cast<std::size_t> (depth)];
    level.hover = index;
    level.openedChild = -1;
    if (index >= 0 && ! level.items[static_cast<std::size_t> (index)].children.empty())
    {
        level.openedChild = index;
        const auto childX = level.x + level.w - 4.0f;
        const auto childY = itemTop (level, index) - padding;
        pushLevel (level.items[static_cast<std::size_t> (index)].children, childX, childY);
        if (levels.back().x < childX) // no room on the right: flip to the left
            levels.back().x = juce::jmax (4.0f, levels[static_cast<std::size_t> (depth)].x - levels.back().w + 4.0f);
    }
}

int Menu::nextSelectable (const Level& level, int from, int direction) const noexcept
{
    const auto count = static_cast<int> (level.items.size());
    if (count == 0)
        return -1;
    auto index = from;
    for (int step = 0; step < count; ++step)
    {
        index = from < 0 && direction < 0 && step == 0 ? count - 1 : (index + direction + count) % count;
        const auto& item = level.items[static_cast<std::size_t> (index)];
        if (item.enabled || ! item.children.empty())
            return index;
    }
    return from;
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
    const auto depth = static_cast<int> (levels.size()) - 1;
    auto& level = levels.back();
    switch (keyCode)
    {
        case GLFW_KEY_ESCAPE:
            close();
            break;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_UP:
            level.hover = nextSelectable (level, level.hover, keyCode == GLFW_KEY_DOWN ? 1 : -1);
            break;
        case GLFW_KEY_LEFT:
            if (depth > 0)
            {
                levels.pop_back();
                levels.back().openedChild = -1;
            }
            break;
        case GLFW_KEY_RIGHT:
            if (level.hover >= 0 && ! level.items[static_cast<std::size_t> (level.hover)].children.empty())
            {
                openChild (depth, level.hover);
                levels.back().hover = nextSelectable (levels.back(), -1, 1);
            }
            break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_SPACE:
            if (level.hover >= 0)
            {
                auto& item = level.items[static_cast<std::size_t> (level.hover)];
                if (! item.children.empty())
                {
                    openChild (depth, level.hover);
                    levels.back().hover = nextSelectable (levels.back(), -1, 1);
                }
                else if (item.enabled)
                {
                    const auto picked = item.id;
                    auto done = std::move (callback);
                    close();
                    if (done)
                        done (picked);
                }
            }
            break;
        default:
            break;
    }
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

juce::String TextPrompt::keyAt (int row, int column)
{
    static const char* rows[keyboardRows] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-_.'", nullptr };
    if (row < keyboardRows - 1)
        return juce::String::charToString (static_cast<juce::juce_wchar> (rows[row][column]));
    // Last row: wide keys.
    return column < 4 ? "SPACE" : column < 7 ? "DEL" : "OK";
}

void TextPrompt::pressHighlightedKey()
{
    const auto key = keyAt (keyRow, keyColumn);
    if (key == "OK")
        acceptNow();
    else if (key == "DEL")
        text = text.dropLastCharacters (1);
    else if (key == "SPACE")
        character (' ');
    else
        character (key[0]);
}

void TextPrompt::acceptNow()
{
    active = false;
    if (accept)
        accept (text);
}

bool TextPrompt::key (int keyCode, int mods)
{
    if (! active)
        return false;
    if (keyCode == GLFW_KEY_ESCAPE)
        active = false;
    else if (keyboardVisible && (keyCode == GLFW_KEY_LEFT || keyCode == GLFW_KEY_RIGHT))
        keyColumn = (keyColumn + (keyCode == GLFW_KEY_RIGHT ? 1 : keyboardColumns - 1)) % keyboardColumns;
    else if (keyboardVisible && (keyCode == GLFW_KEY_UP || keyCode == GLFW_KEY_DOWN))
        keyRow = (keyRow + (keyCode == GLFW_KEY_DOWN ? 1 : keyboardRows - 1)) % keyboardRows;
    else if (keyCode == GLFW_KEY_ENTER || keyCode == GLFW_KEY_KP_ENTER)
        acceptNow();
    else if (keyCode == GLFW_KEY_BACKSPACE)
    {
        if ((mods & GLFW_MOD_CONTROL) != 0)
            text.clear();
        else if (text.isNotEmpty())
            text = text.dropLastCharacters (1);
    }
    return true;
}

juce::Rectangle<float> TextPrompt::keyBounds (int row, int column, int span) const noexcept
{
    const auto left = lastPanel.getX() + 14.0f + static_cast<float> (column) * (lastKeyWidth + lastKeyGap);
    const auto top = lastPanel.getY() + 92.0f + static_cast<float> (row) * (lastKeyHeight + lastKeyGap);
    return { left, top, lastKeyWidth * static_cast<float> (span) + lastKeyGap * static_cast<float> (span - 1), lastKeyHeight };
}

bool TextPrompt::mouseButton (int button, bool pressed, float x, float y)
{
    if (! active)
        return false;
    if (! pressed || button != GLFW_MOUSE_BUTTON_LEFT)
        return true;
    if (keyboardVisible && lastKeyWidth > 0.0f)
        for (int row = 0; row < keyboardRows; ++row)
            for (int column = 0; column < keyboardColumns;)
            {
                const auto label = keyAt (row, column);
                int span = 1;
                while (column + span < keyboardColumns && keyAt (row, column + span) == label && row == keyboardRows - 1)
                    ++span;
                if (keyBounds (row, column, span).contains (x, y))
                {
                    keyRow = row;
                    keyColumn = column;
                    pressHighlightedKey();
                    return true;
                }
                column += span;
            }
    if (! lastPanel.isEmpty() && ! lastPanel.contains (x, y))
        active = false; // a tap outside the panel cancels
    return true;
}

void TextPrompt::padKey (int keyCode)
{
    if (! active)
        return;
    if (keyCode == GLFW_KEY_ENTER)
        pressHighlightedKey();
    else if (keyCode == GLFW_KEY_SPACE)
        character (' ');
    else
        key (keyCode, 0);
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
    const float keyH = lastKeyHeight > 0.0f ? lastKeyHeight : 30.0f, keyGap = lastKeyGap;
    const float w = 420.0f, h = 92.0f + (keyboardVisible ? static_cast<float> (keyboardRows) * (keyH + keyGap) + 6.0f : 0.0f);
    const auto x = (windowWidth - w) * 0.5f, y = (windowHeight - h) * 0.42f;
    lastPanel = { x, y, w, h };
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
    nvgText (vg, x + 16.0f, y + 78.0f, keyboardVisible ? "d-pad walks the keys, A presses, B deletes, Y or OK applies   Esc/Start cancels"
                                                       : "Enter to apply   Esc to cancel", nullptr);
    if (! keyboardVisible)
        return;
    const auto keyW = (w - 28.0f - keyGap * static_cast<float> (keyboardColumns - 1)) / static_cast<float> (keyboardColumns);
    lastKeyWidth = keyW;
    lastKeyHeight = keyH;
    lastKeyGap = keyGap;
    nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    for (int row = 0; row < keyboardRows; ++row)
    {
        const auto top = keyBounds (row, 0, 1).getY();
        int column = 0;
        while (column < keyboardColumns)
        {
            const auto label = keyAt (row, column);
            int span = 1;
            while (column + span < keyboardColumns && keyAt (row, column + span) == label && row == keyboardRows - 1)
                ++span;
            const auto bounds = keyBounds (row, column, span);
            const auto left = bounds.getX();
            const auto width = bounds.getWidth();
            const bool highlighted = row == keyRow && keyColumn >= column && keyColumn < column + span;
            nvgBeginPath (vg);
            nvgRoundedRect (vg, left, top, width, keyH, 4.0f);
            nvgFillColor (vg, highlighted ? palette::control : palette::nodeDark);
            nvgFill (vg);
            nvgFontSize (vg, label.length() > 1 ? 9.5f : 12.0f);
            nvgFillColor (vg, highlighted ? palette::nodeDark : palette::text);
            nvgText (vg, left + width * 0.5f, top + keyH * 0.5f, label.toRawUTF8(), nullptr);
            column += span;
        }
    }
}
// ---------------------------------------------------------------- FileBrowser

void FileBrowser::open (juce::String promptTitle, const juce::File& directory, juce::StringArray wantedExtensions,
                        std::function<void (const juce::File&)> onPick)
{
    title = std::move (promptTitle);
    extensions = std::move (wantedExtensions);
    accept = std::move (onPick);
    filter.clear();
    active = true;
    enter (directory.isDirectory() ? directory : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
}

void FileBrowser::enter (const juce::File& directory)
{
    current = directory;
    scrollOffset = 0.0f;
    selected = -1;
    hover = -1;
    filter.clear();
    refresh();
}

void FileBrowser::refresh()
{
    entries.clear();
    auto children = current.findChildFiles (juce::File::findFilesAndDirectories, false);
    children.sort();
    for (const auto& child : children)
    {
        if (child.getFileName().startsWith ("."))
            continue;
        if (filter.isNotEmpty() && ! child.getFileName().containsIgnoreCase (filter))
            continue;
        if (child.isDirectory())
            entries.push_back ({ child, true });
    }
    for (const auto& child : children)
    {
        if (child.isDirectory() || child.getFileName().startsWith ("."))
            continue;
        if (filter.isNotEmpty() && ! child.getFileName().containsIgnoreCase (filter))
            continue;
        bool wanted = extensions.isEmpty();
        for (const auto& extension : extensions)
            if (child.hasFileExtension (extension))
                wanted = true;
        if (wanted)
            entries.push_back ({ child, false });
    }
}

void FileBrowser::pick (const juce::File& file)
{
    active = false;
    if (accept)
        accept (file);
}

juce::Rectangle<float> FileBrowser::panel (int windowWidth, int windowHeight) const noexcept
{
    const auto w = juce::jmin (640.0f, static_cast<float> (windowWidth) - 40.0f);
    const auto h = juce::jmin (560.0f, static_cast<float> (windowHeight) - 80.0f);
    return { (windowWidth - w) * 0.5f, (windowHeight - h) * 0.45f, w, h };
}

int FileBrowser::rowAt (float x, float y) const noexcept
{
    const auto listTop = lastPanel.getY() + headerHeight;
    const auto listBottom = lastPanel.getBottom() - 12.0f;
    if (x < lastPanel.getX() || x > lastPanel.getRight() || y < listTop || y > listBottom)
        return -1;
    const auto row = static_cast<int> ((y - listTop + scrollOffset) / rowHeight);
    return row >= 0 && row <= static_cast<int> (entries.size()) ? row : -1; // row 0 is ".."
}

bool FileBrowser::mouseMove (float x, float y)
{
    if (! active)
        return false;
    hover = rowAt (x, y);
    return true;
}

bool FileBrowser::mouseButton (int button, bool pressed, float x, float y, double now)
{
    if (! active)
        return false;
    if (! pressed || button != GLFW_MOUSE_BUTTON_LEFT)
        return true;
    if (! lastPanel.contains (x, y))
    {
        active = false;
        return true;
    }
    const auto row = rowAt (x, y);
    if (row < 0)
        return true;
    const bool doubleClick = row == lastClickRow && now - lastClickTime < 0.4;
    lastClickRow = row;
    lastClickTime = now;
    if (row == 0)
    {
        if (doubleClick || true)
            enter (current.getParentDirectory());
        return true;
    }
    const auto& entry = entries[static_cast<std::size_t> (row - 1)];
    if (entry.directory)
    {
        enter (entry.file);
        return true;
    }
    selected = row;
    if (doubleClick)
        pick (entry.file);
    return true;
}

bool FileBrowser::scroll (double dy)
{
    if (! active)
        return false;
    const auto listHeight = lastPanel.getHeight() - headerHeight - 12.0f;
    const auto contentHeight = static_cast<float> (entries.size() + 1) * rowHeight;
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight - listHeight), scrollOffset - static_cast<float> (dy) * 40.0f);
    return true;
}

bool FileBrowser::key (int keyCode, int mods)
{
    if (! active)
        return false;
    juce::ignoreUnused (mods);
    if (keyCode == GLFW_KEY_ESCAPE)
        active = false;
    else if (keyCode == GLFW_KEY_BACKSPACE)
    {
        if (filter.isNotEmpty())
        {
            filter = filter.dropLastCharacters (1);
            refresh();
        }
        else
            enter (current.getParentDirectory());
    }
    else if (keyCode == GLFW_KEY_DOWN || keyCode == GLFW_KEY_UP)
    {
        const auto count = static_cast<int> (entries.size());
        selected = juce::jlimit (1, juce::jmax (1, count), selected + (keyCode == GLFW_KEY_DOWN ? 1 : -1));
        const auto rowTop = static_cast<float> (selected) * rowHeight;
        const auto listHeight = lastPanel.getHeight() - headerHeight - 12.0f;
        if (rowTop - scrollOffset > listHeight - rowHeight)
            scrollOffset = rowTop - listHeight + rowHeight;
        if (rowTop < scrollOffset)
            scrollOffset = rowTop;
    }
    else if (keyCode == GLFW_KEY_ENTER || keyCode == GLFW_KEY_KP_ENTER)
    {
        if (selected >= 1 && selected <= static_cast<int> (entries.size()))
        {
            const auto& entry = entries[static_cast<std::size_t> (selected - 1)];
            if (entry.directory)
                enter (entry.file);
            else
                pick (entry.file);
        }
    }
    return true;
}

bool FileBrowser::character (juce::juce_wchar codepoint)
{
    if (! active)
        return false;
    if (codepoint >= 32 && filter.length() < 40)
    {
        filter += juce::String::charToString (codepoint);
        refresh();
        scrollOffset = 0.0f;
    }
    return true;
}

void FileBrowser::draw (int windowWidth, int windowHeight)
{
    if (! active)
        return;
    lastPanel = panel (windowWidth, windowHeight);
    const auto& box = lastPanel;
    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (windowWidth), static_cast<float> (windowHeight));
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.35f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, box.getX() + 2.0f, box.getY() + 6.0f, box.getWidth(), box.getHeight(), 8.0f);
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.45f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, box.getX(), box.getY(), box.getWidth(), box.getHeight(), 8.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.1f));
    nvgStroke (vg);

    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 11.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, box.getX() + 16.0f, box.getY() + 18.0f, title.toUpperCase().toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgFontSize (vg, 12.0f);
    nvgFillColor (vg, palette::text);
    auto pathText = current.getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (pathText.startsWith (home))
        pathText = "~" + pathText.substring (home.length());
    if (filter.isNotEmpty())
        pathText += "    filter: " + filter;
    nvgText (vg, box.getX() + 16.0f, box.getY() + 40.0f, pathText.toRawUTF8(), nullptr);

    const auto listTop = box.getY() + headerHeight;
    const auto listHeight = box.getHeight() - headerHeight - 12.0f;
    nvgSave (vg);
    nvgScissor (vg, box.getX(), listTop, box.getWidth(), listHeight);
    auto drawRow = [&] (int row, const juce::String& name, bool directory, bool wanted)
    {
        const auto y = listTop + static_cast<float> (row) * rowHeight - scrollOffset;
        if (y + rowHeight < listTop || y > listTop + listHeight)
            return;
        if (row == hover || row == selected)
        {
            nvgBeginPath (vg);
            nvgRoundedRect (vg, box.getX() + 8.0f, y, box.getWidth() - 16.0f, rowHeight, 4.0f);
            nvgFillColor (vg, row == selected ? alpha (palette::selection, 0.25f) : lighter (palette::grid, 0.3f));
            nvgFill (vg);
        }
        nvgFontSize (vg, 12.0f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, directory ? palette::audio : wanted ? palette::text : alpha (palette::mutedText, 0.6f));
        nvgText (vg, box.getX() + 18.0f, y + rowHeight * 0.5f, (directory ? name + "/" : name).toRawUTF8(), nullptr);
    };
    drawRow (0, "..", true, true);
    for (std::size_t i = 0; i < entries.size(); ++i)
        drawRow (static_cast<int> (i) + 1, entries[i].file.getFileName(), entries[i].directory, true);
    nvgRestore (vg);

    const auto contentHeight = static_cast<float> (entries.size() + 1) * rowHeight;
    if (contentHeight > listHeight)
    {
        const auto trackH = listHeight - 8.0f;
        const auto thumbH = juce::jmax (24.0f, trackH * listHeight / contentHeight);
        const auto thumbY = listTop + 4.0f + (trackH - thumbH) * (scrollOffset / (contentHeight - listHeight));
        nvgBeginPath (vg);
        nvgRoundedRect (vg, box.getRight() - 8.0f, thumbY, 4.0f, thumbH, 2.0f);
        nvgFillColor (vg, alpha (palette::mutedText, 0.5f));
        nvgFill (vg);
    }
    nvgFontSize (vg, 9.5f);
    nvgFillColor (vg, alpha (palette::mutedText, 0.7f));
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    nvgText (vg, box.getX() + 16.0f, box.getBottom() - 3.0f, "type to filter   Backspace up   Enter or double-click to choose   Esc to cancel", nullptr);
}
} // namespace signalpatch::v2
