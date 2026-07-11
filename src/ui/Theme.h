#pragma once

#include "../audio/Graph.h"

#include <JuceHeader.h>

namespace signalpatch::ui
{
namespace colours
{
inline const juce::Colour workspace     { 0xff0d1114 };
inline const juce::Colour workspaceEdge { 0xff090c0e };
inline const juce::Colour panel         { 0xff14191d };
inline const juce::Colour panelRaised   { 0xff1e262c };
inline const juce::Colour node          { 0xff222b31 };
inline const juce::Colour nodeTop       { 0xff2a343c };
inline const juce::Colour nodeDark      { 0xff11161a };
inline const juce::Colour text          { 0xffefeadd };
inline const juce::Colour mutedText     { 0xff8e9aa1 };
inline const juce::Colour grid          { 0xff232d34 };
inline const juce::Colour gridDot       { 0xff2d3941 };
inline const juce::Colour audio         { 0xff4fd8c4 };
inline const juce::Colour control       { 0xffb48cff };
inline const juce::Colour feedback      { 0xffffb45b };
inline const juce::Colour warning       { 0xffff5f58 };
inline const juce::Colour okay          { 0xff92d95e };
inline const juce::Colour selection     { 0xfff0d67a };
} // namespace colours

/** Accent colour that identifies a node family everywhere: header, palette,
    knob arcs and scope traces. */
juce::Colour kindAccent (NodeKind kind);

/** Three/four letter family tag shown on the node header and palette. */
juce::String kindTag (NodeKind kind);

/** Draws the small identifying glyph for a node kind inside `area`. */
void drawKindGlyph (juce::Graphics& graphics, NodeKind kind,
                    juce::Rectangle<float> area, juce::Colour colour);

/** One user-creatable node kind with its palette grouping. Shared by the
    module palette and the canvas right-click menu. */
struct NodePaletteEntry
{
    NodeKind kind;
    const char* label;
    const char* group;
    const char* tooltip;
};

const std::vector<NodePaletteEntry>& nodePalette();

class LabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LabLookAndFeel();

    void drawRotarySlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawLinearSlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float minSliderPosition,
                           float maxSliderPosition,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics& graphics,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isMouseOverButton,
                               bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;
};
} // namespace signalpatch::ui
