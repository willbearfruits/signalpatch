#include "Display.h"

#include "Pins.h"

#include <Arduino.h>
#include <Wire.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow" // inside U8g2lib.h, not this file
#include <U8g2lib.h>
#pragma GCC diagnostic pop

namespace
{
// U8g2 starts the bus itself on SDA/SCL (Wire.begin(5, 6)).
U8G2_SSD1306_72X40_ER_F_HW_I2C oled (U8G2_R0, U8X8_PIN_NONE, pins::kI2cScl, pins::kI2cSda);

constexpr int kTileColumns = c3::screen::kWidth / 8; // 9
constexpr int kTileRows = c3::screen::kHeight / 8;   // 5

const uint8_t* fontData (c3::screen::Font font)
{
    switch (font)
    {
        case c3::screen::Font::small: return u8g2_font_5x7_tr;
        case c3::screen::Font::medium: return u8g2_font_6x10_tr;
        case c3::screen::Font::large: return u8g2_font_7x13B_tr;
    }
    return u8g2_font_5x7_tr;
}
} // namespace

bool Display::begin()
{
    oled.setI2CAddress (pins::kOledAddress << 1);
    oled.begin();
    oled.setFontMode (1); // transparent: dark text over a lit box
    oled.setBitmapMode (1);

    Wire.beginTransmission (pins::kOledAddress);
    present = Wire.endTransmission() == 0;
    checkFontMetrics();
    return present;
}

void Display::checkFontMetrics()
{
    // The layout (core/Screen.cpp) places text from these numbers; say so if
    // a different U8g2 version changed a font.
    const c3::screen::Font fonts[] = { c3::screen::Font::small, c3::screen::Font::medium, c3::screen::Font::large };
    for (const auto font : fonts)
    {
        oled.setFont (fontData (font));
        const auto& m = c3::screen::metrics (font);
        const int advance = (oled.getStrWidth ("0000000000") - oled.getStrWidth ("0")) / 9;
        const int height = oled.getMaxCharHeight();
        const int baseline = oled.getMaxCharHeight() + oled.getDescent();
        if (advance != m.advance || height != m.height || baseline != m.baseline)
            Serial.printf ("display: font %d metrics %d/%d/%d, layout expects %d/%d/%d\n", static_cast<int> (font), advance,
                           height, baseline, m.advance, m.height, m.baseline);
    }
}

void Display::configure (uint8_t brightness, bool upsideDown, bool awake)
{
    const int c = brightness * 2 + (brightness > 0 ? 1 : 0); // 0..255
    if (c != contrast)
    {
        contrast = c;
        oled.setContrast (static_cast<uint8_t> (c));
    }
    if (upsideDown != flipped)
    {
        flipped = upsideDown;
        oled.setDisplayRotation (upsideDown ? U8G2_R2 : U8G2_R0);
        dirty = true;
    }
    if (awake == sleeping)
    {
        sleeping = ! awake;
        oled.setPowerSave (sleeping ? 1 : 0);
        dirty = true;
    }
}

bool Display::readyForFrame (uint32_t now) const
{
    return ! sleeping && row >= kTileRows && now - lastFrameMs >= kFrameMs;
}

void Display::show (const c3::screen::DrawList& list, uint32_t now)
{
    if (! dirty && list == shown)
        return;
    render (list);
    shown = list;
    dirty = false;
    lastFrameMs = now;
    row = 0;
    service();
}

void Display::service()
{
    if (row >= kTileRows)
        return;
    oled.updateDisplayArea (0, static_cast<uint8_t> (row), kTileColumns, 1);
    ++row;
}

void Display::render (const c3::screen::DrawList& list)
{
    oled.clearBuffer();
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& op = list[i];
        oled.setDrawColor (op.color);
        switch (op.type)
        {
            case c3::screen::Op::Type::box:
                oled.drawBox (op.x, op.y, op.w, op.h);
                break;
            case c3::screen::Op::Type::frame:
                oled.drawFrame (op.x, op.y, op.w, op.h);
                break;
            case c3::screen::Op::Type::text:
                oled.setFont (fontData (op.font));
                oled.drawStr (op.x, op.y + c3::screen::metrics (op.font).baseline, op.text);
                break;
        }
    }
    oled.setDrawColor (1);
}

void Display::message (const char* line1, const char* line2)
{
    c3::screen::DrawList list;
    list.text (c3::screen::Font::medium, 0, 4, line1, 12);
    list.text (c3::screen::Font::medium, 0, 20, line2, 12);
    render (list);
    oled.sendBuffer();
    dirty = true;
}
