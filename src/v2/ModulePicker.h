#pragma once

#include "../audio/Graph.h"

#include <JuceHeader.h>
#include <nanovg.h>

#include <functional>
#include <optional>
#include <vector>

namespace signalpatch::v2
{
// Every module as a tile big enough for a finger, grouped by family, with a
// line saying what it does. The family chips along the top jump to a family;
// a drag scrolls; typing filters. Arrows (the d-pad) walk the tiles, Page
// Up / Down (the bumpers) jump family, Enter (A) adds, Escape (B) closes.
// The caller decides where the module goes (after the selected pedal, into a
// cable, at the end of the chain); the title says which.
class ModulePicker
{
public:
    ModulePicker (NVGcontext* context, int fontId) : vg (context), font (fontId) {}

    void open (juce::String title, std::function<void (NodeKind)> onPick);
    void close() noexcept { active = false; drag.reset(); }
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    void setTouchMode (bool touch) noexcept { touchMode = touch; }

    // Window coordinates (logical pixels). Each returns true when it used the event.
    bool mouseMove (float x, float y);
    bool mouseButton (int button, bool pressed, float x, float y);
    bool scroll (double dy);
    bool key (int key, int mods);
    bool character (juce::juce_wchar codepoint);
    void draw (int windowWidth, int windowHeight);

private:
    struct Tile
    {
        NodeKind kind;
        juce::String label, blurb, group;
        juce::Rectangle<float> bounds; // content coordinates (before scrolling)
    };
    struct Heading
    {
        juce::String group;
        float y = 0.0f; // content coordinates
    };
    struct Drag
    {
        float startX = 0.0f, startY = 0.0f, startScroll = 0.0f;
        bool moved = false;
    };

    void rebuild();
    void layoutTiles (float width);
    [[nodiscard]] juce::Rectangle<float> panelBounds() const noexcept;
    [[nodiscard]] juce::Rectangle<float> gridBounds() const noexcept;
    [[nodiscard]] juce::Rectangle<float> closeBounds() const noexcept;
    [[nodiscard]] juce::Rectangle<float> chipBounds (int index) const noexcept;
    [[nodiscard]] int tileAt (float x, float y) const noexcept;
    [[nodiscard]] int chipAt (float x, float y) const noexcept;
    void scrollToGroup (const juce::String& group);
    void keepSelectionVisible();
    void clampScroll();
    void moveSelection (int dx, int dy);
    void pick (int index);

    [[nodiscard]] float tileWidth() const noexcept { return touchMode ? 178.0f : 156.0f; }
    [[nodiscard]] float tileHeight() const noexcept { return touchMode ? 76.0f : 58.0f; }
    [[nodiscard]] float titleHeight() const noexcept { return touchMode ? 64.0f : 48.0f; }
    [[nodiscard]] float chipHeight() const noexcept { return touchMode ? 44.0f : 26.0f; }

    NVGcontext* vg;
    int font;
    bool active = false, touchMode = false;
    juce::String title, filter;
    std::function<void (NodeKind)> callback;
    std::vector<Tile> tiles;
    std::vector<Heading> headings;
    std::vector<juce::String> groups; // the families, in catalogue order
    int selected = 0, hover = -1;
    float scrollOffset = 0.0f, contentHeight = 0.0f, laidOutWidth = 0.0f;
    int windowW = 1280, windowH = 720;
    std::optional<Drag> drag;
};
} // namespace signalpatch::v2
