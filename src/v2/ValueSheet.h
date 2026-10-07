#pragma once

#include "../audio/PatchEngine.h"

#include <nanovg.h>

#include <functional>
#include <optional>

namespace signalpatch::v2
{
// One knob, big enough for a finger: a sheet along the bottom with the
// module and knob names, the exact value, a wide slider, - and + for single
// steps, Reset, Type and More (the knob's menu). Dragging anywhere on the
// slider moves the value by the finger's movement, so a touch never makes it
// jump; FINE slows the drag down five times. Opened by a tap on a knob in
// touch mode. Left / Right (the d-pad) step it, Up / Down step ten times,
// Escape or Enter closes. Not modal: the rest of the screen keeps working.
class ValueSheet
{
public:
    ValueSheet (NVGcontext* context, int fontId, PatchEngine& engineToUse) : vg (context), font (fontId), engine (engineToUse) {}

    void open (NodeId node, int parameter);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    [[nodiscard]] NodeId node() const noexcept { return nodeId; }
    [[nodiscard]] int parameter() const noexcept { return parameterIndex; }
    [[nodiscard]] bool contains (float x, float y) const noexcept { return active && lastPanel.contains (x, y); }
    [[nodiscard]] bool isDragging() const noexcept { return drag.has_value(); }

    bool mouseButton (int button, bool pressed, float x, float y);
    bool mouseMove (float x, float y);
    bool key (int key, int mods);
    /** left / right: the room the sheet may take (the inspector docks on the right); bottomInset: the slot bar. */
    void draw (float left, float right, int windowHeight, float bottomInset);

    /** The rack's text prompt: title, initial text, what to do with the answer. */
    std::function<void (const juce::String&, const juce::String&, std::function<void (const juce::String&)>)> askText;
    /** The knob's menu (MIDI learn, range...), at a window position. */
    std::function<void (NodeId, int, float, float)> showMenu;
    /** The value changed: the plate must be redrawn. */
    std::function<void (NodeId)> changed;

private:
    enum class Part { none, slider, minus, plus, reset, type, fine, more, close };
    struct Drag
    {
        float startX = 0.0f, startNormalised = 0.0f;
    };

    [[nodiscard]] const DspParameter* current() const;
    [[nodiscard]] juce::Rectangle<float> bounds (Part part) const noexcept;
    [[nodiscard]] Part partAt (float x, float y) const noexcept;
    void setNormalised (float normalised);
    void step (int steps);

    NVGcontext* vg;
    int font;
    PatchEngine& engine;
    bool active = false, fine = false;
    NodeId nodeId = 0;
    int parameterIndex = -1;
    juce::Rectangle<float> lastPanel;
    std::optional<Drag> drag;
    Part pressed = Part::none;

    static constexpr float sheetHeight = 168.0f;
};
} // namespace signalpatch::v2
