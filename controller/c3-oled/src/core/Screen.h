#pragma once

// What the 72x40 OLED shows, as a list of boxes, frames and strings. The
// layout is pure so the host tests can check that every string fits; the
// firmware's Display only replays the list through U8g2 and skips frames
// that did not change.

#include <cstdint>

namespace c3::screen
{
constexpr int kWidth = 72;
constexpr int kHeight = 40;

/** The three U8g2 fonts in use (all monospaced X11 fonts). */
enum class Font : uint8_t
{
    small,  // u8g2_font_5x7_tr
    medium, // u8g2_font_6x10_tr
    large   // u8g2_font_7x13B_tr
};

struct FontMetrics
{
    uint8_t advance;  // pixels per character
    uint8_t height;   // glyph box height (U8g2 max char height)
    uint8_t baseline; // baseline below the top of the glyph box
};

/** Measured from the U8g2 2.36.18 font data; the firmware checks them at boot. */
const FontMetrics& metrics (Font font);

/** Width of `length` characters (an upper bound of U8g2's string width). */
int textWidth (Font font, int length);

struct Op
{
    enum class Type : uint8_t { box, frame, text };
    static constexpr int kMaxText = 16;

    Type type = Type::box;
    Font font = Font::small;
    uint8_t color = 1; // 1 lit, 0 dark (drawn over a lit box)
    int16_t x = 0, y = 0, w = 0, h = 0; // text: the glyph box of the whole string
    char text[kMaxText + 1] {};

    bool operator== (const Op& other) const;
};

class DrawList
{
public:
    static constexpr int kMaxOps = 32;

    void clear() { count = 0; overflow = false; }
    void box (int x, int y, int w, int h, uint8_t color = 1);
    void frame (int x, int y, int w, int h, uint8_t color = 1);
    /** Draws at most `maxChars` characters of `text` with its glyph box's top-left at x, y. */
    void text (Font font, int x, int y, const char* text, int maxChars = Op::kMaxText, uint8_t color = 1);

    int size() const { return count; }
    const Op& operator[] (int index) const { return ops[index]; }
    bool overflowed() const { return overflow; }

    bool operator== (const DrawList& other) const;
    bool operator!= (const DrawList& other) const { return ! (*this == other); }

private:
    Op* add();
    Op ops[kMaxOps];
    int count = 0;
    bool overflow = false;
};

enum class Link : uint8_t
{
    advertising, // nobody connected
    connected,   // a central connected, no SignalPatch hello yet
    app          // SignalPatch answered
};

struct Overlay
{
    bool visible = false;
    bool bar = false;      // draw the position bar
    uint8_t position = 0;  // 0..127
    char title[13] {};
    char value[11] {};
};

struct State
{
    Link link = Link::advertising;
    uint8_t nameNumber = 0;  // 0 = none
    bool blinkFast = false;  // LED blink phase (~2 Hz)
    bool blinkSlow = false;  // "pair me" phase (~1 Hz)

    // From SignalPatch
    char rig[17] {};
    int8_t slot = -1;
    bool muted = false;
    uint8_t led[4] {};       // buttons 1-3, push
    char label[4][9] {};

    bool gyroOn = false;
    bool gyroMissing = false; // a gyro mode is set but no MPU-6050 answered
    bool button3HoldsGyro = false;

    Overlay overlay;
};

void layout (const State& state, DrawList& out);

/** Splits an up-to-8-character label over two 4-character lines (at a space when there is one). */
void splitLabel (const char* label, char* line1, char* line2);
} // namespace c3::screen
