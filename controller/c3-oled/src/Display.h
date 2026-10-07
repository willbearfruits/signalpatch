#pragma once

// The 0.42" 72x40 SSD1306 through U8g2. Replays a core::screen::DrawList into
// the frame buffer and sends it one 8-pixel tile row per loop pass (about
// 2 ms of I2C each), so a redraw never holds up inputs or BLE for long.

#include "core/Screen.h"

#include <cstdint>

class Display
{
public:
    static constexpr uint32_t kFrameMs = 33; // at most ~30 frames a second

    /** Starts U8g2 (which starts the I2C bus). Returns false when nothing answers at the OLED address. */
    bool begin();

    /** Brightness 0-127, upside down, asleep: applied when they change. */
    void configure (uint8_t brightness, bool upsideDown, bool awake);

    /** True when a new frame may start (no transfer running, frame interval passed, awake). */
    bool readyForFrame (uint32_t now) const;

    /** Starts sending `list` if it differs from what is shown. Call only when readyForFrame(). */
    void show (const c3::screen::DrawList& list, uint32_t now);

    /** Sends the next tile row of a frame in progress. Call every loop pass. */
    void service();

    /** Draws two lines at once, blocking (boot messages only). */
    void message (const char* line1, const char* line2);

private:
    void render (const c3::screen::DrawList& list);
    void checkFontMetrics();

    c3::screen::DrawList shown;
    bool present = false;
    bool dirty = true;
    bool sleeping = false;
    bool flipped = false;
    int contrast = -1;
    int row = 5; // 5 = no transfer running
    uint32_t lastFrameMs = 0;
};
