#include "Screen.h"

#include <cstring>

namespace c3::screen
{
namespace
{
    // advance, glyph box height, baseline from the top of the box
    // (u8g2: max char width/height and y offset of each font).
    const FontMetrics fontTable[3] = {
        { 5, 7, 6 },   // 5x7
        { 6, 10, 8 },  // 6x10
        { 7, 13, 11 }, // 7x13B
    };

    int boundedLength (const char* s, int maxChars)
    {
        int n = 0;
        while (n < maxChars && s[n] != '\0')
            ++n;
        return n;
    }

    bool lit (uint8_t led, bool blinkPhase)
    {
        return led == 1 || (led == 2 && blinkPhase);
    }

    void centred (DrawList& out, Font font, int y, const char* text, int maxChars)
    {
        const int n = boundedLength (text, maxChars);
        out.text (font, (kWidth - textWidth (font, n)) / 2, y, text, n);
    }

    void brand (const State& s, DrawList& out)
    {
        centred (out, Font::medium, 0, "SignalPatch", 12);
        char model[5] = { 'C', '3', '\0', '\0', '\0' };
        if (s.nameNumber >= 1 && s.nameNumber <= 9)
        {
            model[2] = ' ';
            model[3] = static_cast<char> ('0' + s.nameNumber);
        }
        centred (out, Font::medium, 11, model, 4);
    }

    void advertising (const State& s, DrawList& out)
    {
        brand (s, out);
        out.text (Font::small, 14, 28, "pair me");
        if (s.blinkSlow)
            out.box (52, 29, 5, 5);
    }

    void connected (const State& s, DrawList& out)
    {
        brand (s, out);
        centred (out, Font::small, 28, "connected", 14);
    }

    void overlay (const Overlay& o, DrawList& out)
    {
        out.text (Font::medium, 0, 0, o.title, 12);
        out.text (Font::large, 0, 12, o.value, 10);
        if (o.bar)
        {
            out.frame (0, 30, kWidth, 10);
            const int fillWidth = (o.position > 127 ? 127 : o.position) * 68 / 127;
            if (fillWidth > 0)
                out.box (2, 32, fillWidth, 6);
        }
    }

    // A chip: a lit box with dark text, or (unlit) just the text.
    void chip (DrawList& out, int x, int w, const char* text, bool on)
    {
        if (on)
            out.box (x, 11, w, 9);
        out.text (Font::small, x + 1, 12, text, (w - 1) / 5, on ? 0 : 1);
    }

    void home (const State& s, DrawList& out)
    {
        // Row 0: the live slot in a lit box, then the rig name.
        out.box (0, 0, 9, 10);
        const char slotText[2] = { s.slot >= 0 && s.slot <= 8 ? static_cast<char> ('1' + s.slot) : '-', '\0' };
        out.text (Font::medium, 2, 0, slotText, 1, 0);
        if (boundedLength (s.rig, 16) <= 10)
            out.text (Font::medium, 12, 0, s.rig, 10);
        else
            out.text (Font::small, 12, 1, s.rig, 12);

        // Row 1: MUTED, the encoder push, the gyro.
        if (s.muted)
            chip (out, 0, 27, "MUTED", true);
        if (s.label[3][0] != '\0' || s.led[3] != 0)
            chip (out, 29, 21, s.label[3][0] != '\0' ? s.label[3] : "PUSH", lit (s.led[3], s.blinkFast));
        if (s.gyroOn)
            chip (out, 51, 21, "GYRO", true);
        else if (s.gyroMissing)
        {
            chip (out, 51, 21, "GYRO", false);
            out.box (51, 15, 21, 1); // struck through: no MPU-6050
        }

        // Row 2: buttons 1-3, lit while their LED is on.
        for (int i = 0; i < 3; ++i)
        {
            const int x = 24 * i;
            const bool gyroButton = i == 2 && s.button3HoldsGyro;
            const bool on = gyroButton ? s.gyroOn : lit (s.led[i], s.blinkFast);
            const char* label = gyroButton ? "GYRO" : s.label[i];
            if (on)
                out.box (x, 21, 24, 19);
            else
                out.frame (x, 21, 24, 19);
            const uint8_t color = on ? 0 : 1;
            if (label[0] == '\0')
            {
                const char number[2] = { static_cast<char> ('1' + i), '\0' };
                out.text (Font::small, x + 10, 27, number, 1, color);
                continue;
            }
            char line1[5], line2[5];
            splitLabel (label, line1, line2);
            out.text (Font::small, x + 2, 23, line1, 4, color);
            out.text (Font::small, x + 2, 31, line2, 4, color);
        }
    }
} // namespace

const FontMetrics& metrics (Font font)
{
    return fontTable[static_cast<int> (font) < 3 ? static_cast<int> (font) : 0];
}

int textWidth (Font font, int length)
{
    return length * metrics (font).advance;
}

bool Op::operator== (const Op& o) const
{
    return type == o.type && font == o.font && color == o.color && x == o.x && y == o.y && w == o.w && h == o.h
           && std::strcmp (text, o.text) == 0;
}

Op* DrawList::add()
{
    if (count >= kMaxOps)
    {
        overflow = true;
        return nullptr;
    }
    ops[count] = Op {};
    return &ops[count++];
}

void DrawList::box (int x, int y, int w, int h, uint8_t color)
{
    if (Op* op = add())
    {
        op->type = Op::Type::box;
        op->x = static_cast<int16_t> (x);
        op->y = static_cast<int16_t> (y);
        op->w = static_cast<int16_t> (w);
        op->h = static_cast<int16_t> (h);
        op->color = color;
    }
}

void DrawList::frame (int x, int y, int w, int h, uint8_t color)
{
    if (Op* op = add())
    {
        op->type = Op::Type::frame;
        op->x = static_cast<int16_t> (x);
        op->y = static_cast<int16_t> (y);
        op->w = static_cast<int16_t> (w);
        op->h = static_cast<int16_t> (h);
        op->color = color;
    }
}

void DrawList::text (Font font, int x, int y, const char* s, int maxChars, uint8_t color)
{
    if (s == nullptr)
        return;
    const int n = boundedLength (s, maxChars < Op::kMaxText ? maxChars : Op::kMaxText);
    if (n == 0)
        return;
    if (Op* op = add())
    {
        op->type = Op::Type::text;
        op->font = font;
        op->color = color;
        op->x = static_cast<int16_t> (x);
        op->y = static_cast<int16_t> (y);
        op->w = static_cast<int16_t> (textWidth (font, n));
        op->h = metrics (font).height;
        std::memcpy (op->text, s, static_cast<size_t> (n));
        op->text[n] = '\0';
    }
}

bool DrawList::operator== (const DrawList& other) const
{
    if (count != other.count || overflow != other.overflow)
        return false;
    for (int i = 0; i < count; ++i)
        if (! (ops[i] == other.ops[i]))
            return false;
    return true;
}

void splitLabel (const char* label, char* line1, char* line2)
{
    line1[0] = line2[0] = '\0';
    const int n = boundedLength (label, 8);
    if (n <= 4)
    {
        std::memcpy (line1, label, static_cast<size_t> (n));
        line1[n] = '\0';
        return;
    }
    int split = -1;
    for (int i = 4; i >= 1; --i)
        if (label[i] == ' ')
        {
            split = i;
            break;
        }
    int first = split > 0 ? split : 4;
    while (first > 0 && label[first - 1] == ' ')
        --first;
    std::memcpy (line1, label, static_cast<size_t> (first));
    line1[first] = '\0';
    int start = split > 0 ? split + 1 : 4;
    while (start < n && label[start] == ' ')
        ++start;
    int m = 0;
    while (start + m < n && m < 4)
    {
        line2[m] = label[start + m];
        ++m;
    }
    line2[m] = '\0';
}

void layout (const State& s, DrawList& out)
{
    out.clear();
    switch (s.link)
    {
        case Link::advertising:
            advertising (s, out);
            return;
        case Link::connected:
            if (s.overlay.visible)
                overlay (s.overlay, out);
            else
                connected (s, out);
            return;
        case Link::app:
            if (s.overlay.visible)
                overlay (s.overlay, out);
            else
                home (s, out);
            return;
    }
}
} // namespace c3::screen
