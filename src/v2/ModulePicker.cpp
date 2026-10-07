#include "ModulePicker.h"
#include "Palette.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>

namespace signalpatch::v2
{
namespace
{
    constexpr float gap = 10.0f;
}

void ModulePicker::open (juce::String newTitle, std::function<void (NodeKind)> onPick)
{
    title = std::move (newTitle);
    callback = std::move (onPick);
    filter.clear();
    active = true;
    selected = 0;
    hover = -1;
    scrollOffset = 0.0f;
    drag.reset();
    rebuild();
}

void ModulePicker::rebuild()
{
    tiles.clear();
    groups.clear();
    const auto wanted = filter.trim().toLowerCase();
    for (const auto& entry : moduleCatalogue())
    {
        const juce::String group (entry.group);
        if (std::find (groups.begin(), groups.end(), group) == groups.end())
            groups.push_back (group);
        const juce::String label (entry.label), blurb (entry.blurb);
        if (wanted.isNotEmpty() && ! label.toLowerCase().contains (wanted) && ! blurb.toLowerCase().contains (wanted)
            && ! group.toLowerCase().contains (wanted))
            continue;
        tiles.push_back ({ entry.kind, label, blurb, group, {} });
    }
    selected = juce::jlimit (0, juce::jmax (0, static_cast<int> (tiles.size()) - 1), selected);
    laidOutWidth = 0.0f; // lay out again on the next draw or hit test
    layoutTiles (gridBounds().getWidth());
}

juce::Rectangle<float> ModulePicker::panelBounds() const noexcept
{
    const auto margin = touchMode ? 10.0f : 36.0f;
    const auto width = juce::jmin (static_cast<float> (windowW) - 2.0f * margin, 1180.0f);
    const auto height = static_cast<float> (windowH) - 2.0f * margin;
    return { (static_cast<float> (windowW) - width) * 0.5f, margin, width, height };
}

juce::Rectangle<float> ModulePicker::closeBounds() const noexcept
{
    const auto panel = panelBounds();
    const auto size = titleHeight() - 14.0f;
    return { panel.getRight() - size - 8.0f, panel.getY() + 7.0f, size, size };
}

juce::Rectangle<float> ModulePicker::chipBounds (int index) const noexcept
{
    const auto panel = panelBounds().reduced (12.0f, 0.0f);
    const auto count = juce::jmax (1, static_cast<int> (groups.size()));
    const auto width = (panel.getWidth() - static_cast<float> (count - 1) * 6.0f) / static_cast<float> (count);
    return { panel.getX() + static_cast<float> (index) * (width + 6.0f), panelBounds().getY() + titleHeight(), width, chipHeight() };
}

juce::Rectangle<float> ModulePicker::gridBounds() const noexcept
{
    const auto panel = panelBounds();
    const auto top = panel.getY() + titleHeight() + chipHeight() + 10.0f;
    return { panel.getX() + 12.0f, top, panel.getWidth() - 24.0f, panel.getBottom() - 10.0f - top };
}

void ModulePicker::layoutTiles (float width)
{
    if (width <= 0.0f)
        return;
    laidOutWidth = width;
    headings.clear();
    const auto columns = juce::jmax (1, static_cast<int> ((width + gap) / (tileWidth() + gap)));
    const auto w = (width - static_cast<float> (columns - 1) * gap) / static_cast<float> (columns);
    const auto headingHeight = touchMode ? 30.0f : 24.0f;
    float y = 0.0f;
    int column = 0;
    juce::String group;
    for (auto& tile : tiles)
    {
        if (tile.group != group)
        {
            if (column != 0)
                y += tileHeight() + gap;
            column = 0;
            group = tile.group;
            headings.push_back ({ group, y });
            y += headingHeight;
        }
        tile.bounds = { static_cast<float> (column) * (w + gap), y, w, tileHeight() };
        if (++column == columns)
        {
            column = 0;
            y += tileHeight() + gap;
        }
    }
    if (column != 0)
        y += tileHeight() + gap;
    contentHeight = y;
    clampScroll();
}

void ModulePicker::clampScroll()
{
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight - gridBounds().getHeight()), scrollOffset);
}

int ModulePicker::tileAt (float x, float y) const noexcept
{
    const auto grid = gridBounds();
    if (! grid.contains (x, y))
        return -1;
    const juce::Point<float> content (x - grid.getX(), y - grid.getY() + scrollOffset);
    for (std::size_t index = 0; index < tiles.size(); ++index)
        if (tiles[index].bounds.contains (content))
            return static_cast<int> (index);
    return -1;
}

int ModulePicker::chipAt (float x, float y) const noexcept
{
    for (int index = 0; index < static_cast<int> (groups.size()); ++index)
        if (chipBounds (index).contains (x, y))
            return index;
    return -1;
}

void ModulePicker::scrollToGroup (const juce::String& group)
{
    for (const auto& heading : headings)
        if (heading.group == group)
        {
            scrollOffset = heading.y;
            clampScroll();
            for (std::size_t index = 0; index < tiles.size(); ++index)
                if (tiles[index].group == group)
                {
                    selected = static_cast<int> (index);
                    break;
                }
            return;
        }
}

void ModulePicker::keepSelectionVisible()
{
    if (! juce::isPositiveAndBelow (selected, static_cast<int> (tiles.size())))
        return;
    const auto& bounds = tiles[static_cast<std::size_t> (selected)].bounds;
    const auto visible = gridBounds().getHeight();
    if (bounds.getY() - 30.0f < scrollOffset)
        scrollOffset = bounds.getY() - 30.0f;
    else if (bounds.getBottom() + 6.0f > scrollOffset + visible)
        scrollOffset = bounds.getBottom() + 6.0f - visible;
    clampScroll();
}

void ModulePicker::moveSelection (int dx, int dy)
{
    if (tiles.empty())
        return;
    if (dx != 0)
        selected = juce::jlimit (0, static_cast<int> (tiles.size()) - 1, selected + dx);
    if (dy != 0)
    {
        // The nearest tile, by column, in the next row up or down.
        const auto current = tiles[static_cast<std::size_t> (selected)].bounds;
        int best = -1;
        float bestRow = 0.0f, bestDx = 0.0f;
        for (std::size_t index = 0; index < tiles.size(); ++index)
        {
            const auto& bounds = tiles[index].bounds;
            const auto rowDistance = (bounds.getY() - current.getY()) * static_cast<float> (dy);
            if (rowDistance <= 1.0f)
                continue;
            const auto columnDistance = std::abs (bounds.getCentreX() - current.getCentreX());
            if (best < 0 || rowDistance < bestRow - 1.0f || (std::abs (rowDistance - bestRow) <= 1.0f && columnDistance < bestDx))
            {
                best = static_cast<int> (index);
                bestRow = rowDistance;
                bestDx = columnDistance;
            }
        }
        if (best >= 0)
            selected = best;
    }
    keepSelectionVisible();
}

void ModulePicker::pick (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (tiles.size())))
        return;
    const auto kind = tiles[static_cast<std::size_t> (index)].kind;
    auto done = std::move (callback);
    close();
    if (done)
        done (kind);
}

bool ModulePicker::mouseMove (float x, float y)
{
    if (! active)
        return false;
    if (drag.has_value())
    {
        if (std::abs (y - drag->startY) > 8.0f || std::abs (x - drag->startX) > 8.0f)
            drag->moved = true;
        if (drag->moved)
        {
            scrollOffset = drag->startScroll - (y - drag->startY);
            clampScroll();
        }
    }
    hover = drag.has_value() && drag->moved ? -1 : tileAt (x, y);
    return true;
}

bool ModulePicker::mouseButton (int button, bool pressed, float x, float y)
{
    if (! active)
        return false;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
    {
        if (pressed && button == GLFW_MOUSE_BUTTON_RIGHT && ! panelBounds().contains (x, y))
            close();
        return true;
    }
    if (pressed)
    {
        if (! panelBounds().contains (x, y) || closeBounds().expanded (6.0f).contains (x, y))
        {
            close();
            return true;
        }
        if (const auto chip = chipAt (x, y); chip >= 0)
        {
            scrollToGroup (groups[static_cast<std::size_t> (chip)]);
            return true;
        }
        if (gridBounds().contains (x, y))
            drag = Drag { x, y, scrollOffset, false };
        return true;
    }
    if (drag.has_value())
    {
        const bool tapped = ! drag->moved;
        drag.reset();
        if (tapped)
            pick (tileAt (x, y));
    }
    return true;
}

bool ModulePicker::scroll (double dy)
{
    if (! active)
        return false;
    scrollOffset -= static_cast<float> (dy) * 48.0f;
    clampScroll();
    return true;
}

bool ModulePicker::key (int keyCode, int)
{
    if (! active)
        return false;
    switch (keyCode)
    {
        case GLFW_KEY_ESCAPE:
            if (filter.isNotEmpty())
            {
                filter.clear();
                rebuild();
            }
            else
                close();
            break;
        case GLFW_KEY_BACKSPACE:
            if (filter.isNotEmpty())
            {
                filter = filter.dropLastCharacters (1);
                rebuild();
            }
            break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            pick (selected);
            break;
        case GLFW_KEY_LEFT:  moveSelection (-1, 0); break;
        case GLFW_KEY_RIGHT: moveSelection (1, 0); break;
        case GLFW_KEY_UP:    moveSelection (0, -1); break;
        case GLFW_KEY_DOWN:  moveSelection (0, 1); break;
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_PAGE_DOWN:
        {
            if (tiles.empty())
                break;
            const auto current = tiles[static_cast<std::size_t> (selected)].group;
            auto at = std::find (groups.begin(), groups.end(), current);
            const auto step = keyCode == GLFW_KEY_PAGE_DOWN ? 1 : -1;
            // The next family that still has tiles under the filter.
            for (int tries = 0; tries < static_cast<int> (groups.size()); ++tries)
            {
                auto index = static_cast<int> (at - groups.begin()) + step;
                index = (index + static_cast<int> (groups.size())) % static_cast<int> (groups.size());
                at = groups.begin() + index;
                const auto hasTiles = std::any_of (tiles.begin(), tiles.end(), [&] (const Tile& tile) { return tile.group == *at; });
                if (hasTiles)
                {
                    scrollToGroup (*at);
                    break;
                }
            }
            break;
        }
        default: break;
    }
    return true;
}

bool ModulePicker::character (juce::juce_wchar codepoint)
{
    if (! active)
        return false;
    if (codepoint >= 32 && codepoint < 127 && filter.length() < 24)
    {
        filter += juce::String::charToString (codepoint);
        selected = 0;
        scrollOffset = 0.0f;
        rebuild();
    }
    return true;
}

void ModulePicker::draw (int windowWidth, int windowHeight)
{
    if (! active)
        return;
    if (windowWidth != windowW || windowHeight != windowH)
    {
        windowW = windowWidth;
        windowH = windowHeight;
        laidOutWidth = 0.0f;
    }
    const auto grid = gridBounds();
    if (std::abs (grid.getWidth() - laidOutWidth) > 0.5f)
        layoutTiles (grid.getWidth());

    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (windowW), static_cast<float> (windowH));
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.55f));
    nvgFill (vg);

    const auto panel = panelBounds();
    nvgBeginPath (vg);
    nvgRoundedRect (vg, panel.getX() + 2.0f, panel.getY() + 6.0f, panel.getWidth(), panel.getHeight(), 10.0f);
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.45f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, panel.getX(), panel.getY(), panel.getWidth(), panel.getHeight(), 10.0f);
    nvgFillColor (vg, palette::panel);
    nvgFill (vg);
    nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.08f));
    nvgStrokeWidth (vg, 1.0f);
    nvgStroke (vg);

    // Title, filter and close.
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, touchMode ? 17.0f : 14.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    const auto titleY = panel.getY() + titleHeight() * 0.5f;
    nvgText (vg, panel.getX() + 16.0f, titleY, title.toUpperCase().toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    float titleBounds[4] {};
    nvgTextBounds (vg, panel.getX() + 16.0f, titleY, title.toUpperCase().toRawUTF8(), nullptr, titleBounds);
    nvgFontSize (vg, touchMode ? 13.0f : 11.0f);
    const auto hintX = titleBounds[2] + 24.0f;
    if (filter.isNotEmpty())
    {
        nvgFillColor (vg, palette::selection);
        nvgText (vg, hintX, titleY, ("filter: " + filter + "_   (Backspace, Esc clears)").toRawUTF8(), nullptr);
    }
    else
    {
        nvgFillColor (vg, palette::mutedText);
        nvgText (vg, hintX, titleY, touchMode ? "tap to add, drag to scroll" : "click or Enter adds, arrows walk, type to filter, Esc closes", nullptr);
    }
    const auto close = closeBounds();
    nvgBeginPath (vg);
    nvgRoundedRect (vg, close.getX(), close.getY(), close.getWidth(), close.getHeight(), 6.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgBeginPath (vg);
    const auto cross = close.reduced (close.getWidth() * 0.32f);
    nvgMoveTo (vg, cross.getX(), cross.getY());
    nvgLineTo (vg, cross.getRight(), cross.getBottom());
    nvgMoveTo (vg, cross.getRight(), cross.getY());
    nvgLineTo (vg, cross.getX(), cross.getBottom());
    nvgStrokeColor (vg, palette::text);
    nvgStrokeWidth (vg, 2.0f);
    nvgStroke (vg);

    // Family chips: the one at the top of the view is lit.
    juce::String currentGroup;
    for (const auto& heading : headings)
        if (heading.y <= scrollOffset + 4.0f)
            currentGroup = heading.group;
    if (currentGroup.isEmpty() && ! headings.empty())
        currentGroup = headings.front().group;
    for (int index = 0; index < static_cast<int> (groups.size()); ++index)
    {
        const auto& group = groups[static_cast<std::size_t> (index)];
        const auto bounds = chipBounds (index);
        const bool present = std::any_of (tiles.begin(), tiles.end(), [&] (const Tile& tile) { return tile.group == group; });
        const auto colour = familyAccent (group);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 5.0f);
        nvgFillColor (vg, group == currentGroup ? mix (palette::panelRaised, colour, 0.28f) : palette::panelRaised);
        nvgFill (vg);
        nvgBeginPath (vg);
        nvgRect (vg, bounds.getX() + 6.0f, bounds.getBottom() - 3.0f, bounds.getWidth() - 12.0f, 2.0f);
        nvgFillColor (vg, alpha (colour, present ? 0.9f : 0.25f));
        nvgFill (vg);
        nvgFontSize (vg, touchMode ? 12.5f : 10.0f);
        nvgTextLetterSpacing (vg, 0.8f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, alpha (palette::text, present ? 0.95f : 0.35f));
        nvgText (vg, bounds.getCentreX(), bounds.getCentreY() - 1.0f, group.toRawUTF8(), nullptr);
        nvgTextLetterSpacing (vg, 0.0f);
    }

    // The grid.
    nvgSave (vg);
    nvgScissor (vg, grid.getX(), grid.getY(), grid.getWidth(), grid.getHeight());
    for (const auto& heading : headings)
    {
        const auto y = grid.getY() + heading.y - scrollOffset;
        if (y < grid.getY() - 40.0f || y > grid.getBottom())
            continue;
        nvgFontSize (vg, touchMode ? 12.0f : 10.0f);
        nvgTextLetterSpacing (vg, 1.0f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
        nvgFillColor (vg, palette::mutedText);
        nvgText (vg, grid.getX() + 2.0f, y + (touchMode ? 24.0f : 19.0f), heading.group.toRawUTF8(), nullptr);
        nvgTextLetterSpacing (vg, 0.0f);
    }
    for (std::size_t index = 0; index < tiles.size(); ++index)
    {
        const auto& tile = tiles[index];
        const auto bounds = tile.bounds.translated (grid.getX(), grid.getY() - scrollOffset);
        if (bounds.getBottom() < grid.getY() || bounds.getY() > grid.getBottom())
            continue;
        const bool isSelected = static_cast<int> (index) == selected;
        const bool isHovered = static_cast<int> (index) == hover;
        const auto colour = familyAccent (tile.group);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 7.0f);
        nvgFillColor (vg, isHovered ? lighter (palette::panelRaised, 0.08f) : palette::panelRaised);
        nvgFill (vg);
        if (isSelected)
        {
            nvgStrokeColor (vg, palette::selection);
            nvgStrokeWidth (vg, 2.0f);
            nvgStroke (vg);
        }
        nvgBeginPath (vg);
        nvgRoundedRect (vg, bounds.getX() + 7.0f, bounds.getY() + 9.0f, 4.0f, bounds.getHeight() - 18.0f, 2.0f);
        nvgFillColor (vg, colour);
        nvgFill (vg);
        nvgSave (vg);
        nvgIntersectScissor (vg, bounds.getX(), bounds.getY(), bounds.getWidth() - 6.0f, bounds.getHeight());
        nvgFontSize (vg, touchMode ? 15.0f : 12.5f);
        nvgTextLetterSpacing (vg, 0.6f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFillColor (vg, palette::text);
        nvgText (vg, bounds.getX() + 20.0f, bounds.getY() + bounds.getHeight() * 0.44f, tile.label.toRawUTF8(), nullptr);
        nvgTextLetterSpacing (vg, 0.0f);
        nvgFontSize (vg, touchMode ? 12.0f : 10.5f);
        nvgFillColor (vg, palette::mutedText);
        nvgText (vg, bounds.getX() + 20.0f, bounds.getY() + bounds.getHeight() * 0.76f, tile.blurb.toRawUTF8(), nullptr);
        nvgRestore (vg);
    }
    if (tiles.empty())
    {
        nvgFontSize (vg, 13.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::mutedText);
        nvgText (vg, grid.getCentreX(), grid.getY() + 60.0f, ("No module matches \"" + filter + "\"").toRawUTF8(), nullptr);
    }
    nvgRestore (vg);

    // Where the view is in the list.
    if (contentHeight > grid.getHeight())
    {
        const auto trackH = grid.getHeight();
        const auto thumbH = juce::jmax (30.0f, trackH * grid.getHeight() / contentHeight);
        const auto thumbY = grid.getY() + (trackH - thumbH) * scrollOffset / juce::jmax (1.0f, contentHeight - grid.getHeight());
        nvgBeginPath (vg);
        nvgRoundedRect (vg, panel.getRight() - 7.0f, thumbY, 3.0f, thumbH, 1.5f);
        nvgFillColor (vg, alpha (palette::text, 0.25f));
        nvgFill (vg);
    }
}
} // namespace signalpatch::v2
