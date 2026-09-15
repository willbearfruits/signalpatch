#pragma once

#include <JuceHeader.h>

#include <nanovg.h>

#include <functional>
#include <vector>

namespace signalpatch::v2
{
struct MenuItem
{
    juce::String text;
    int id = 0;
    bool enabled = true;
    bool separator = false;
    bool header = false;
    juce::String shortcut;
    std::vector<MenuItem> children;

    static MenuItem item (int id, juce::String text, juce::String shortcut = {}, bool enabled = true)
    {
        MenuItem entry;
        entry.id = id;
        entry.text = std::move (text);
        entry.shortcut = std::move (shortcut);
        entry.enabled = enabled;
        return entry;
    }
    static MenuItem sub (juce::String text, std::vector<MenuItem> children)
    {
        MenuItem entry;
        entry.text = std::move (text);
        entry.children = std::move (children);
        return entry;
    }
    static MenuItem sectionHeader (juce::String text)
    {
        MenuItem entry;
        entry.text = std::move (text);
        entry.header = true;
        entry.enabled = false;
        return entry;
    }
    static MenuItem line()
    {
        MenuItem entry;
        entry.separator = true;
        entry.enabled = false;
        return entry;
    }
};

// Popup menu drawn by the rack's own renderer, so it opens exactly at the
// pointer and looks like the rest of the instrument. Hover opens submenus;
// a click on a leaf reports its id; Escape or a click outside closes it.
class Menu
{
public:
    explicit Menu (NVGcontext* vg, int font) : vg (vg), font (font) {}

    void open (std::vector<MenuItem> items, float x, float y, std::function<void (int)> onPick);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return ! levels.empty(); }

    // All in window coordinates. Return true when the event was consumed.
    bool mouseMove (float x, float y);
    bool mouseButton (int button, bool pressed, float x, float y);
    bool key (int key);

    void draw (int windowWidth, int windowHeight);
    void setWindowSize (int width, int height) noexcept { windowW = width; windowH = height; }

    static constexpr float itemHeight = 24.0f;
    static constexpr float headerHeight = 20.0f;
    static constexpr float separatorHeight = 9.0f;
    static constexpr float padding = 6.0f;

private:
    struct Level
    {
        std::vector<MenuItem> items;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        int hover = -1;
        int openedChild = -1;
    };

    void pushLevel (std::vector<MenuItem> items, float x, float y);
    [[nodiscard]] float itemTop (const Level& level, int index) const noexcept;
    [[nodiscard]] float rowHeight (const MenuItem& item) const noexcept;
    [[nodiscard]] int itemAt (const Level& level, float x, float y) const noexcept;
    [[nodiscard]] bool contains (const Level& level, float x, float y) const noexcept;

    NVGcontext* vg;
    int font;
    int windowW = 1600, windowH = 1000;
    std::vector<Level> levels;
    std::function<void (int)> callback;
};

// A one-line text prompt (rename, set value) drawn in the same style.
class TextPrompt
{
public:
    explicit TextPrompt (NVGcontext* vg, int font) : vg (vg), font (font) {}

    void open (juce::String title, juce::String initial, std::function<void (const juce::String&)> onAccept);
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    void close() noexcept { active = false; }

    bool key (int key, int mods);
    bool character (juce::juce_wchar codepoint);
    void draw (int windowWidth, int windowHeight, double now);

private:
    NVGcontext* vg;
    int font;
    bool active = false;
    juce::String title, text;
    std::function<void (const juce::String&)> accept;
};
} // namespace signalpatch::v2
