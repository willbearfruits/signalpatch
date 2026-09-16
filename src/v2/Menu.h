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
// Arrow keys walk it too (right/left open and close submenus, Enter picks),
// which is what the gamepad drives.
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

    /** Touch: rows, panels and the on-screen keyboard grow so a finger can hit them. */
    void setTouchMode (bool touch) noexcept
    {
        const auto scale = touch ? 1.9f : 1.0f;
        itemHeight = 24.0f * scale;
        headerHeight = 20.0f * scale;
        separatorHeight = 9.0f * scale;
    }

    float itemHeight = 24.0f;
    float headerHeight = 20.0f;
    float separatorHeight = 9.0f;
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
    void openChild (int depth, int index);
    [[nodiscard]] int nextSelectable (const Level& level, int from, int direction) const noexcept;
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

    // On-screen keyboard for a pad or a touch screen: arrows walk the grid,
    // Enter presses the highlighted key (OK accepts), Backspace deletes.
    void setKeyboardVisible (bool visible) noexcept { keyboardVisible = visible; }
    void setTouchMode (bool touch) noexcept { lastKeyHeight = touch ? 46.0f : 30.0f; }
    [[nodiscard]] bool isKeyboardVisible() const noexcept { return keyboardVisible; }
    void acceptNow();
    /** The gamepad: A presses the highlighted key, the d-pad walks the grid. */
    void padKey (int key);
    /** A tap on the on-screen keyboard. Returns true when the prompt handled it. */
    bool mouseButton (int button, bool pressed, float x, float y);

private:
    static constexpr int keyboardRows = 5, keyboardColumns = 10;
    [[nodiscard]] static juce::String keyAt (int row, int column);
    [[nodiscard]] juce::Rectangle<float> keyBounds (int row, int column, int span) const noexcept;
    void pressHighlightedKey();

    NVGcontext* vg;
    int font;
    bool active = false;
    bool keyboardVisible = false;
    int keyRow = 0, keyColumn = 0;
    juce::Rectangle<float> lastPanel;   // where draw() put the panel, for taps
    float lastKeyWidth = 0.0f, lastKeyHeight = 30.0f, lastKeyGap = 5.0f;
    juce::String title, text;
    std::function<void (const juce::String&)> accept;
};
// In-canvas file browser: directories first, files filtered by extension,
// wheel to scroll, click to enter/select, double-click or Enter to pick,
// Backspace goes up, typing filters by name. Used for models, impulses and
// patches so nothing leaves the renderer.
class FileBrowser
{
public:
    explicit FileBrowser (NVGcontext* vg, int font) : vg (vg), font (font) {}

    void open (juce::String title, const juce::File& directory, juce::StringArray extensions,
               std::function<void (const juce::File&)> onPick);
    void setTouchMode (bool touch) noexcept { rowHeight = touch ? 40.0f : 22.0f; }
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    void close() noexcept { active = false; }

    bool mouseMove (float x, float y);
    bool mouseButton (int button, bool pressed, float x, float y, double now);
    bool scroll (double dy);
    bool key (int key, int mods);
    bool character (juce::juce_wchar codepoint);
    void draw (int windowWidth, int windowHeight);

private:
    struct Entry
    {
        juce::File file;
        bool directory = false;
    };

    void refresh();
    void enter (const juce::File& directory);
    void pick (const juce::File& file);
    [[nodiscard]] juce::Rectangle<float> panel (int windowWidth, int windowHeight) const noexcept;
    [[nodiscard]] int rowAt (float x, float y) const noexcept;

    NVGcontext* vg;
    int font;
    bool active = false;
    juce::String title;
    juce::File current;
    juce::StringArray extensions;
    juce::String filter;
    std::vector<Entry> entries;
    std::function<void (const juce::File&)> accept;
    int hover = -1, selected = -1;
    float scrollOffset = 0.0f;
    juce::Rectangle<float> lastPanel;
    double lastClickTime = -1.0;
    int lastClickRow = -1;

    float rowHeight = 22.0f;
    static constexpr float headerHeight = 58.0f;
};
} // namespace signalpatch::v2
