#pragma once

#include "../audio/PatchEngine.h"

#include <nanovg.h>

#include <functional>
#include <optional>
#include <vector>

namespace signalpatch::v2
{
// The CONTROLLER page: a panel docked to the right that shows the connected
// hardware controller (docs/CONTROLLER.md). CONTROLS lists every control of
// the C3 handheld with its MIDI message, what it drives (in this patch or as
// an app action in every patch) and its last value, each with ASSIGN.
// SETTINGS edits the controller's own settings, which live on the controller.
// The page draws and hit-tests; the rack builds the menus and changes the
// mappings, through the callbacks.
class ControllerPage
{
public:
    /** One control of the controller, as the app sees it. */
    struct Control
    {
        juce::String name;
        MidiMapping::Source source = MidiMapping::Source::controlChange;
        int channel = 10;
        int number = 0;
        bool relative = false; // the encoder
    };
    /** The C3's controls, numbered from its settings (or the protocol's defaults). */
    static std::vector<Control> controlsFor (const std::map<int, int>& settings);

    ControllerPage (NVGcontext* context, int fontId, PatchEngine& engineToUse) : vg (context), font (fontId), engine (engineToUse) {}

    void open() noexcept;
    void close() noexcept { active = false; drag.reset(); }
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    void setTouchMode (bool touch) noexcept { touchMode = touch; }
    [[nodiscard]] bool contains (float x, float y) const noexcept { return active && lastPanel.contains (x, y); }
    [[nodiscard]] float dockedWidth() const noexcept { return active ? lastPanel.getWidth() : 0.0f; }
    [[nodiscard]] bool isDragging() const noexcept { return drag.has_value(); }
    /** The controller the page shows: the first C3, else the first that said hello; nullopt when none. */
    [[nodiscard]] std::optional<PatchEngine::ConnectedController> controller() const;
    [[nodiscard]] std::vector<Control> controls() const;
    /** Sends one setting to the controller (clamped to its range) and shows it until the controller answers. */
    void setSetting (int key, int value);

    bool mouseButton (int button, bool pressed, float x, float y);
    bool mouseMove (float x, float y);
    bool scroll (double dy, float x, float y);
    void draw (int windowWidth, int windowHeight, float top, float bottomInset);
    /** Live values arrived or settings changed: the panel wants a frame. */
    [[nodiscard]] bool wantsRedraw();

    // What the rack does for the page.
    std::function<void (const Control&, float x, float y)> showAssignMenu;
    std::function<void()> applyDefaultLayout;
    /** A setting's choice list (key, window position) or a number prompt. */
    std::function<void (int key, float x, float y)> showSettingChoices;
    std::function<void (const juce::String&, const juce::String&, std::function<void (const juce::String&)>)> askText;
    /** What a control drives now, as one line ("Drive - Distortion", "Next slot (all patches)"), empty for nothing. */
    std::function<juce::String (const Control&)> describe;

private:
    enum class Tab { controls, settings };
    enum class Part { none, close, tabControls, tabSettings, assign, minus, plus, value, footerLeft, footerRight };
    struct Hit
    {
        Part part = Part::none;
        int row = -1;
    };
    struct Drag
    {
        float startY = 0.0f, startScroll = 0.0f;
        bool moved = false;
        Hit pressed;
    };

    [[nodiscard]] float rowHeight() const noexcept { return touchMode ? 62.0f : 46.0f; }
    [[nodiscard]] float headerHeight() const noexcept { return touchMode ? 136.0f : 112.0f; }
    [[nodiscard]] float footerHeight() const noexcept { return touchMode ? 60.0f : 46.0f; }
    [[nodiscard]] juce::Rectangle<float> listArea() const noexcept;
    [[nodiscard]] juce::Rectangle<float> rowBounds (int row) const noexcept;
    [[nodiscard]] juce::Rectangle<float> partBounds (Part part, int row) const noexcept;
    [[nodiscard]] Hit hitAt (float x, float y) const noexcept;
    [[nodiscard]] int rowCount() const;
    void clampScroll();
    void activate (const Hit& hit);
    void changeSetting (int key, int delta);

    NVGcontext* vg;
    int font;
    PatchEngine& engine;
    bool active = false, touchMode = false;
    Tab tab = Tab::controls;
    float scrollOffset = 0.0f;
    juce::Rectangle<float> lastPanel;
    std::optional<Drag> drag;
    juce::uint32 seenActivity = 0;
    std::map<int, int> shownSettings, engineSettingsSeen; // what the page shows, and the controller's last report
};
} // namespace signalpatch::v2
