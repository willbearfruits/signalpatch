#pragma once

#include "../audio/PatchEngine.h"

#include <nanovg.h>

#include <functional>
#include <optional>

namespace signalpatch::v2
{
// The inspector: a panel docked to the right edge that lists one module's
// knobs with everything the plate has no room for - the exact value, the
// travel the knob covers (min / max, in real units), the curve of that travel
// and the depth of its mod socket. Fields are dragged or tapped and typed;
// a right click (or a double tap) puts one back to its default. It is not
// modal: the rack keeps working beside it and the panel follows the selection.
class Inspector
{
public:
    Inspector (NVGcontext* vg, int font, PatchEngine& engine);

    void show (NodeId id, int focusParameter = -1);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    [[nodiscard]] NodeId node() const noexcept { return nodeId; }
    void setTouchMode (bool touch) noexcept { touchMode = touch; }
    /** True while a field is being dragged: the pointer belongs to the panel wherever it goes. */
    [[nodiscard]] bool isDragging() const noexcept { return drag.has_value(); }
    [[nodiscard]] bool contains (float x, float y) const noexcept { return active && lastPanel.contains (x, y); }

    bool mouseButton (int button, bool pressed, float x, float y, double now);
    bool mouseMove (float x, float y);
    bool scroll (double dy, float x, float y);
    void draw (int windowWidth, int windowHeight, float top, float bottomInset);
    /** Width taken from the right edge while open (things docked there move over). */
    [[nodiscard]] float dockedWidth() const noexcept { return active ? lastPanel.getWidth() : 0.0f; }

    /** The rack's text prompt: title, initial text, what to do with the answer. */
    std::function<void (const juce::String&, const juce::String&, std::function<void (const juce::String&)>)> askText;
    /** A value, shape or depth changed: the module's plate must be redrawn. */
    std::function<void (NodeId)> changed;

private:
    enum class Field { value, minimum, maximum, curve, depth, none };
    struct Hit
    {
        int parameter = -1;
        Field field = Field::none;
    };
    struct Drag
    {
        Hit hit;
        float startY = 0.0f, startAmount = 0.0f;
        bool moved = false;
    };

    [[nodiscard]] const NodeModel* model() const;
    [[nodiscard]] float rowHeight() const noexcept { return touchMode ? 92.0f : 74.0f; }
    [[nodiscard]] juce::Rectangle<float> rowBounds (int row) const noexcept;
    [[nodiscard]] juce::Rectangle<float> fieldBounds (int row, Field field) const noexcept;
    [[nodiscard]] juce::Rectangle<float> closeBounds() const noexcept;
    [[nodiscard]] juce::Rectangle<float> resetBounds() const noexcept;
    [[nodiscard]] Hit hitAt (float x, float y) const;
    /** The field as a 0..1 amount a drag can add to. */
    [[nodiscard]] float amountOf (const DspParameter& parameter, Field field) const;
    void setAmount (int parameterIndex, Field field, float amount);
    void setFromText (int parameterIndex, Field field, const juce::String& text);
    void resetField (int parameterIndex, Field field);
    void prompt (int parameterIndex, Field field);
    [[nodiscard]] juce::String fieldText (const DspParameter& parameter, Field field) const;
    void drawCurve (juce::Rectangle<float> area, const DspParameter& parameter, NVGcolor colour);

    NVGcontext* vg;
    int font;
    PatchEngine& engine;
    bool active = false, touchMode = false;
    NodeId nodeId = 0;
    int highlighted = -1;
    float scrollOffset = 0.0f;
    juce::Rectangle<float> lastPanel;
    std::optional<Drag> drag;
    Hit lastClick;
    double lastClickTime = -1.0;

    static constexpr float panelWidth = 336.0f;
    static constexpr float titleHeight = 46.0f;
    static constexpr float footerHeight = 44.0f;
};
} // namespace signalpatch::v2
