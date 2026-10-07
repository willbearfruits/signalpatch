// Controller behaviour (inputs, gyro modes, hello, settings) and the screen layout.

#include "core/BleMidi.h"
#include "core/Controller.h"
#include "core/Screen.h"

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace c3;
using Bytes = std::vector<uint8_t>;

namespace
{
struct FakeHost : ControllerHost
{
    std::vector<Bytes> sent;
    int saves = 0;
    Settings saved;

    void sendMidi (const uint8_t* m, size_t n) override { sent.emplace_back (m, m + n); }
    void saveSettings (const Settings& s) override
    {
        ++saves;
        saved = s;
    }
    void clear() { sent.clear(); }
    bool sentHello() const
    {
        for (const auto& m : sent)
            if (m.size() == 9 && m[3] == 0x7F && m[4] == 0x00)
                return true;
        return false;
    }
    int count (uint8_t firstByte) const
    {
        int n = 0;
        for (const auto& m : sent)
            n += (! m.empty() && m[0] == firstByte) ? 1 : 0;
        return n;
    }
};

Bytes sysex (std::initializer_list<uint8_t> body, const char* text = nullptr)
{
    Bytes m { 0xF0, 0x7D, 0x53 };
    m.insert (m.end(), body.begin(), body.end());
    if (text != nullptr)
        for (const char* c = text; *c != '\0'; ++c)
            m.push_back (static_cast<uint8_t> (*c));
    m.push_back (0xF7);
    return m;
}

void receive (Controller& c, const Bytes& m, uint32_t now) { c.midiIn (m.data(), m.size(), now); }

void expectBytes (const Bytes& expected, const Bytes& actual)
{
    TEST_ASSERT_EQUAL_UINT32 (expected.size(), actual.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY (expected.data(), actual.data(), expected.size());
}

// Connected, subscribed, SignalPatch answered.
void bringUp (Controller& c, FakeHost& host, uint32_t now = 1000)
{
    c.connected (now);
    c.subscribed (true, now);
    c.tick (now);
    receive (c, sysex ({ 0x7F, 0x01 }), now + 5);
    host.clear();
}

ImuSample tilted (float pitchDeg, float rollDeg)
{
    const float p = pitchDeg * 0.01745329f;
    const float r = rollDeg * 0.01745329f;
    ImuSample s;
    s.ax = -std::sin (p);
    s.ay = std::cos (p) * std::sin (r);
    s.az = std::cos (p) * std::cos (r);
    return s;
}

void checkFits (const screen::DrawList& list)
{
    TEST_ASSERT_FALSE (list.overflowed());
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& op = list[i];
        char message[128];
        std::snprintf (message, sizeof (message), "op %d '%s' at %d,%d size %dx%d", i, op.text, op.x, op.y, op.w, op.h);
        TEST_ASSERT_TRUE_MESSAGE (op.x >= 0 && op.y >= 0, message);
        TEST_ASSERT_TRUE_MESSAGE (op.x + op.w <= screen::kWidth, message);
        TEST_ASSERT_TRUE_MESSAGE (op.y + op.h <= screen::kHeight, message);
        if (op.type == screen::Op::Type::text)
            TEST_ASSERT_EQUAL_INT_MESSAGE (static_cast<int> (std::strlen (op.text)) * screen::metrics (op.font).advance, op.w,
                                           message);
    }
}

bool hasText (const screen::DrawList& list, const char* text)
{
    for (int i = 0; i < list.size(); ++i)
        if (list[i].type == screen::Op::Type::text && std::strcmp (list[i].text, text) == 0)
            return true;
    return false;
}
} // namespace

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------ Buttons and encoder

void test_buttons_send_notes_on_channel_10()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    c.button (0, true, 2000);
    c.button (0, false, 2100);
    c.button (1, true, 2200);
    c.button (1, false, 2300);
    TEST_ASSERT_EQUAL (4, host.sent.size());
    expectBytes ({ 0x99, 60, 127 }, host.sent[0]);
    expectBytes ({ 0x89, 60, 0 }, host.sent[1]);
    expectBytes ({ 0x99, 61, 127 }, host.sent[2]);
    expectBytes ({ 0x89, 61, 0 }, host.sent[3]);
}

void test_note_off_matches_the_note_on_after_a_settings_change()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    c.button (0, true, 2000);
    receive (c, sysex ({ 0x12, 0x00, 0x01, 0x08, 0x30 }), 2010); // channel 1, button 1 note 48
    host.clear();
    c.button (0, false, 2100);
    expectBytes ({ 0x89, 60, 0 }, host.sent[0]); // released where it was pressed
    c.button (0, true, 2200);
    expectBytes ({ 0x90, 48, 127 }, host.sent[1]);
}

void test_encoder_sends_relative_cc()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    c.encoder (1, 2000);
    c.encoder (-1, 2500);
    c.encoder (2, 3000);
    TEST_ASSERT_EQUAL (4, host.sent.size()); // one message per detent
    expectBytes ({ 0xB9, 20, 65 }, host.sent[0]);
    expectBytes ({ 0xB9, 20, 63 }, host.sent[1]);
    expectBytes ({ 0xB9, 20, 65 }, host.sent[2]);

    receive (c, sysex ({ 0x12, 0x12, 0x01 }), 4000); // reversed
    host.clear();
    c.encoder (1, 5000);
    expectBytes ({ 0xB9, 20, 63 }, host.sent[0]);
}

void test_encoder_acceleration_on_fast_spins()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    uint32_t t = 2000;
    for (int i = 0; i < 15; ++i, t += 8)
        c.encoder (1, t);
    TEST_ASSERT_TRUE (host.sent.back()[2] > 66);

    receive (c, sysex ({ 0x12, 0x13, 0x00 }), t + 1000); // acceleration off
    host.clear();
    t += 2000;
    for (int i = 0; i < 15; ++i, t += 8)
        c.encoder (1, t);
    for (const auto& m : host.sent)
        if (m[0] == 0xB9)
            TEST_ASSERT_EQUAL (65, m[2]);
}

// ------------------------------------------------------------ Pots and expression

void test_pots_send_only_on_movement()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    for (int i = 0; i < 20; ++i)
        c.analog (0, 1000, 2000 + 4 * i); // still: the first reading is not sent
    TEST_ASSERT_EQUAL (0, host.sent.size());
    for (int i = 0; i < 40; ++i)
        c.analog (0, 2450, 2100 + 4 * i);
    TEST_ASSERT_TRUE (host.sent.size() > 0);
    expectBytes ({ 0xB9, 16, 127 }, host.sent.back());
    for (size_t i = 1; i < host.sent.size(); ++i)
        TEST_ASSERT_TRUE (host.sent[i][2] > host.sent[i - 1][2]);
}

void test_expression_switch_and_reverse()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    c.analog (3, 0, 2000);
    for (int i = 0; i < 40; ++i)
        c.analog (3, 2450, 2004 + 4 * i);
    expectBytes ({ 0xB9, 11, 127 }, host.sent.back());

    receive (c, sysex ({ 0x12, 0x0D, 0x01 }), 3000); // reversed
    host.clear();
    c.analog (3, 2450, 3004);
    for (int i = 0; i < 40; ++i)
        c.analog (3, 0, 3008 + 4 * i);
    expectBytes ({ 0xB9, 11, 127 }, host.sent.back());

    receive (c, sysex ({ 0x12, 0x0C, 0x00 }), 4000); // off
    host.clear();
    for (int i = 0; i < 40; ++i)
        c.analog (3, (i % 2) * 2450, 4004 + 4 * i);
    TEST_ASSERT_EQUAL (0, host.sent.size());
}

// ------------------------------------------------------------ Gyro modes

void test_gyro_mode_3_short_and_long_push()
{
    FakeHost host;
    Controller c (host, Settings());
    c.setGyroPresent (true);
    bringUp (c, host);
    c.button (3, true, 2000);
    c.tick (2300);
    TEST_ASSERT_EQUAL (0, host.sent.size()); // nothing until release
    c.button (3, false, 2400);
    TEST_ASSERT_EQUAL (2, host.sent.size());
    expectBytes ({ 0x99, 63, 127 }, host.sent[0]);
    expectBytes ({ 0x89, 63, 0 }, host.sent[1]);
    TEST_ASSERT_FALSE (c.gyroActive());

    host.clear();
    c.button (3, true, 3000);
    c.tick (3599);
    TEST_ASSERT_FALSE (c.gyroActive());
    c.tick (3600);
    TEST_ASSERT_TRUE (c.gyroActive()); // toggled while still held
    c.button (3, false, 4000);
    TEST_ASSERT_EQUAL (0, host.count (0x99));

    // Switched on: zeroed at the current angle, so level reads 64.
    c.imu (tilted (25.0f, -10.0f), 0.01f, 4010);
    c.imu (tilted (25.0f, -10.0f), 0.01f, 4020);
    TEST_ASSERT_EQUAL (2, host.sent.size());
    expectBytes ({ 0xB9, 12, 64 }, host.sent[0]);
    expectBytes ({ 0xB9, 13, 64 }, host.sent[1]);

    c.button (3, true, 5000);
    c.tick (5700);
    TEST_ASSERT_FALSE (c.gyroActive());
    c.button (3, false, 5800);
}

void test_gyro_mode_2_button_3_holds_it_on()
{
    FakeHost host;
    Settings s;
    s.set (kGyroMode, 2);
    Controller c (host, s);
    c.setGyroPresent (true);
    bringUp (c, host);
    c.button (2, true, 2000);
    TEST_ASSERT_TRUE (c.gyroActive());
    c.button (2, false, 2500);
    TEST_ASSERT_FALSE (c.gyroActive());
    TEST_ASSERT_EQUAL (0, host.count (0x99)); // no note
    TEST_ASSERT_EQUAL (0, host.count (0x89));

    // The push is a plain button in mode 2.
    c.button (3, true, 3000);
    expectBytes ({ 0x99, 63, 127 }, host.sent.back());
}

void test_gyro_mode_1_and_range()
{
    FakeHost host;
    Settings s;
    s.set (kGyroMode, 1);
    s.set (kGyroSmoothing, 0);
    Controller c (host, s);
    c.setGyroPresent (true);
    TEST_ASSERT_TRUE (c.gyroActive());
    bringUp (c, host);
    uint32_t t = 2000;
    for (int i = 0; i < 5; ++i, t += 10)
        c.imu (tilted (0, 0), 0.01f, t);
    for (int i = 0; i < 400; ++i, t += 10)
        c.imu (tilted (30.0f, -15.0f), 0.01f, t); // one range up, half a range down
    Bytes lastTilt, lastRoll;
    for (const auto& m : host.sent)
    {
        if (m[1] == 12)
            lastTilt = m;
        if (m[1] == 13)
            lastRoll = m;
    }
    TEST_ASSERT_EQUAL (127, lastTilt[2]);
    TEST_ASSERT_INT_WITHIN (1, 32, lastRoll[2]);

    // 0x14 zeroes it where it is.
    receive (c, sysex ({ 0x14 }), t);
    host.clear();
    for (int i = 0; i < 5; ++i, t += 10)
        c.imu (tilted (30.0f, -15.0f), 0.01f, t);
    expectBytes ({ 0xB9, 12, 64 }, host.sent[0]);
}

void test_gyro_missing()
{
    FakeHost host;
    Controller c (host, Settings());
    c.setGyroPresent (false);
    bringUp (c, host);
    c.button (3, true, 2000);
    c.tick (2700);
    c.button (3, false, 2800);
    TEST_ASSERT_FALSE (c.gyroActive());
    screen::State state;
    c.screenState (2900, state);
    TEST_ASSERT_TRUE (state.gyroMissing);
    TEST_ASSERT_TRUE (state.overlay.visible);
    TEST_ASSERT_EQUAL_STRING ("missing", state.overlay.value);
    c.imu (tilted (10, 10), 0.01f, 3000);
    TEST_ASSERT_EQUAL (0, host.sent.size());
}

// ------------------------------------------------------------ Hello and settings

void test_hello_after_subscribe_repeats_until_answered()
{
    FakeHost host;
    Controller c (host, Settings());
    c.connected (1000);
    c.tick (1000);
    TEST_ASSERT_EQUAL (0, host.sent.size()); // nobody listening yet
    c.subscribed (true, 1200);
    c.tick (1200);
    TEST_ASSERT_EQUAL (1, host.sent.size());
    expectBytes ({ 0xF0, 0x7D, 0x53, 0x7F, 0x00, 0x02, 0x01, 0x00, 0xF7 }, host.sent[0]);
    c.tick (4199);
    TEST_ASSERT_EQUAL (1, host.sent.size());
    c.tick (4200);
    TEST_ASSERT_EQUAL (2, host.sent.size());

    receive (c, sysex ({ 0x7F, 0x01 }), 4300);
    TEST_ASSERT_TRUE (c.appConnected());
    c.tick (8000);
    c.tick (12000);
    TEST_ASSERT_EQUAL (2, host.sent.size());

    c.disconnected (13000);
    TEST_ASSERT_FALSE (c.appConnected());
    c.connected (14000);
    c.subscribed (true, 14000);
    c.tick (14001);
    TEST_ASSERT_EQUAL (3, host.sent.size());
}

void test_hello_probe_after_long_silence()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host, 1000);
    c.button (0, true, 20000);
    TEST_ASSERT_FALSE (host.sentHello());
    c.button (0, false, 40000); // 39 s without a word from the app
    TEST_ASSERT_TRUE (host.sentHello());
    c.tick (42000);
    TEST_ASSERT_TRUE (c.appConnected());
    c.tick (43001);
    TEST_ASSERT_FALSE (c.appConnected()); // no answer: back to waiting
    host.clear();
    c.tick (43002);
    TEST_ASSERT_TRUE (host.sentHello());
    receive (c, sysex ({ 0x7F, 0x01 }), 43100);
    TEST_ASSERT_TRUE (c.appConnected());
}

void test_settings_request_change_reset()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);

    receive (c, sysex ({ 0x10 }), 2000);
    TEST_ASSERT_EQUAL (1, host.sent.size());
    TEST_ASSERT_EQUAL (53, host.sent[0].size());
    TEST_ASSERT_EQUAL_HEX8 (0x11, host.sent[0][3]);
    TEST_ASSERT_EQUAL (0, host.saves);

    host.clear();
    receive (c, sysex ({ 0x12, 0x14, 0x20, 0x0F, 0x7F, 0x77, 0x01 }), 2100); // brightness, range clamped, unknown key
    TEST_ASSERT_EQUAL (1, host.saves);
    TEST_ASSERT_EQUAL (32, host.saved.get (kBrightness));
    TEST_ASSERT_EQUAL (90, host.saved.get (kGyroRange));
    const Bytes& dump = host.sent[0];
    TEST_ASSERT_EQUAL (32, dump[5 + 2 * kBrightness]);
    TEST_ASSERT_EQUAL (90, dump[5 + 2 * kGyroRange]);

    host.clear();
    receive (c, sysex ({ 0x12, 0x14, 0x20 }), 2200); // no change: answered, not saved again
    TEST_ASSERT_EQUAL (1, host.saves);
    TEST_ASSERT_EQUAL (1, host.sent.size());

    host.clear();
    receive (c, sysex ({ 0x13 }), 2300);
    TEST_ASSERT_EQUAL (2, host.saves);
    TEST_ASSERT_TRUE (host.saved == Settings());
    TEST_ASSERT_EQUAL (100, host.sent[0][5 + 2 * kBrightness]);
}

void test_name_number_applies_after_restart()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    receive (c, sysex ({ 0x12, 0x17, 0x04 }), 2000);
    TEST_ASSERT_EQUAL (4, c.settings().get (kNameNumber));
    screen::State state;
    c.disconnected (3000);
    c.screenState (3000, state);
    TEST_ASSERT_EQUAL (0, state.nameNumber);

    Controller restarted (host, c.settings());
    restarted.screenState (0, state);
    TEST_ASSERT_EQUAL (4, state.nameNumber);
}

// ------------------------------------------------------------ Screen state

void test_feedback_reaches_the_screen()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    receive (c, sysex ({ 0x01, 0x00, 0x01 }), 2000);
    receive (c, sysex ({ 0x01, 0x02, 0x02 }), 2000);
    receive (c, sysex ({ 0x01, 0x03, 0x01 }), 2000);
    receive (c, sysex ({ 0x02, 0x00 }, "FUZZ"), 2000);
    receive (c, sysex ({ 0x02, 0x03 }, "NEXT"), 2000);
    receive (c, sysex ({ 0x03, 0x01, 0x01 }), 2000);
    receive (c, sysex ({ 0x04 }, "Clean"), 2000);
    receive (c, sysex ({ 0x08, 0x01 }), 2000);
    screen::State s;
    c.screenState (2100, s);
    TEST_ASSERT_TRUE (s.link == screen::Link::app);
    TEST_ASSERT_EQUAL (1, s.led[0]);
    TEST_ASSERT_EQUAL (2, s.led[2]);
    TEST_ASSERT_EQUAL (1, s.led[3]);
    TEST_ASSERT_EQUAL_STRING ("FUZZ", s.label[0]);
    TEST_ASSERT_EQUAL_STRING ("NEXT", s.label[3]);
    TEST_ASSERT_EQUAL (1, s.slot);
    TEST_ASSERT_EQUAL_STRING ("Clean", s.rig);
    TEST_ASSERT_TRUE (s.muted);
    TEST_ASSERT_FALSE (s.overlay.visible);
}

void test_overlay_label_and_value_text()
{
    FakeHost host;
    Controller c (host, Settings());
    bringUp (c, host);
    receive (c, sysex ({ 0x05, 16 }, "Cutoff"), 2000);
    receive (c, sysex ({ 0x06, 16, 10 }, "200 Hz"), 2000); // from before the move: stale

    c.analog (0, 0, 3000);
    for (int i = 0; i < 30; ++i)
        c.analog (0, 1240, 3004 + 4 * i);
    screen::State s;
    c.screenState (3200, s);
    TEST_ASSERT_TRUE (s.overlay.visible);
    TEST_ASSERT_EQUAL_STRING ("Cutoff", s.overlay.title);
    TEST_ASSERT_EQUAL_STRING ("64", s.overlay.value); // raw until the app answers
    TEST_ASSERT_TRUE (s.overlay.bar);

    receive (c, sysex ({ 0x06, 16, 64 }, "1.20 kHz"), 3210);
    c.screenState (3220, s);
    TEST_ASSERT_EQUAL_STRING ("1.20 kHz", s.overlay.value);
    TEST_ASSERT_EQUAL (64, s.overlay.position);

    c.screenState (3200 + Controller::kOverlayMs, s);
    TEST_ASSERT_FALSE (s.overlay.visible);

    // No label from the app: the control's own name.
    c.analog (1, 0, 6000);
    for (int i = 0; i < 30; ++i)
        c.analog (1, 2450, 6004 + 4 * i);
    c.screenState (6200, s);
    TEST_ASSERT_EQUAL_STRING ("Pot 2", s.overlay.title);
    TEST_ASSERT_EQUAL_STRING ("127", s.overlay.value);

    // The encoder shows its step until a 0x06 arrives.
    c.encoder (-1, 9000);
    c.screenState (9001, s);
    TEST_ASSERT_EQUAL_STRING ("Encoder", s.overlay.title);
    TEST_ASSERT_EQUAL_STRING ("-1", s.overlay.value);
    TEST_ASSERT_FALSE (s.overlay.bar);
}

void test_generic_screen_before_signalpatch()
{
    FakeHost host;
    Controller c (host, Settings());
    c.connected (1000);
    c.subscribed (true, 1000);
    screen::State s;
    c.screenState (1000, s);
    TEST_ASSERT_TRUE (s.link == screen::Link::connected);
    TEST_ASSERT_FALSE (s.overlay.visible);
    c.analog (0, 0, 2000);
    for (int i = 0; i < 30; ++i)
        c.analog (0, 1240, 2004 + 4 * i);
    c.screenState (2200, s);
    TEST_ASSERT_EQUAL_STRING ("Pot 1", s.overlay.title);
    TEST_ASSERT_EQUAL_STRING ("CC16  64", s.overlay.value);
    c.button (1, true, 3000);
    c.screenState (3001, s);
    TEST_ASSERT_EQUAL_STRING ("N61 on", s.overlay.value);
    c.encoder (1, 4000);
    c.screenState (4001, s);
    TEST_ASSERT_EQUAL_STRING ("CC20  +1", s.overlay.value);
}

void test_display_sleep_and_wake()
{
    FakeHost host;
    Settings settings;
    settings.set (kSleepMinutes, 1);
    Controller c (host, settings);
    TEST_ASSERT_TRUE (c.displayAwake (0));
    TEST_ASSERT_TRUE (c.displayAwake (59999));
    TEST_ASSERT_FALSE (c.displayAwake (60000));
    c.button (0, true, 70000);
    TEST_ASSERT_TRUE (c.displayAwake (70001));
    c.encoder (1, 200000);
    TEST_ASSERT_TRUE (c.displayAwake (200001));

    Controller never (host, Settings());
    TEST_ASSERT_TRUE (never.displayAwake (100000000));
}

// ------------------------------------------------------------ Layout

void test_split_label()
{
    char a[5], b[5];
    screen::splitLabel ("FUZZ", a, b);
    TEST_ASSERT_EQUAL_STRING ("FUZZ", a);
    TEST_ASSERT_EQUAL_STRING ("", b);
    screen::splitLabel ("RIG 2", a, b);
    TEST_ASSERT_EQUAL_STRING ("RIG", a);
    TEST_ASSERT_EQUAL_STRING ("2", b);
    screen::splitLabel ("REC LOOP", a, b);
    TEST_ASSERT_EQUAL_STRING ("REC", a);
    TEST_ASSERT_EQUAL_STRING ("LOOP", b);
    screen::splitLabel ("OVERDRIV", a, b);
    TEST_ASSERT_EQUAL_STRING ("OVER", a);
    TEST_ASSERT_EQUAL_STRING ("DRIV", b);
    screen::splitLabel ("Fuzz Fac", a, b);
    TEST_ASSERT_EQUAL_STRING ("Fuzz", a);
    TEST_ASSERT_EQUAL_STRING ("Fac", b);
    screen::splitLabel ("AB CDEFG", a, b);
    TEST_ASSERT_EQUAL_STRING ("AB", a);
    TEST_ASSERT_EQUAL_STRING ("CDEF", b);
    screen::splitLabel ("", a, b);
    TEST_ASSERT_EQUAL_STRING ("", a);
}

void test_every_screen_fits()
{
    screen::DrawList list;
    screen::State s;

    for (uint8_t number = 0; number <= 9; ++number)
        for (int blink = 0; blink < 2; ++blink)
        {
            s = screen::State {};
            s.nameNumber = number;
            s.blinkSlow = blink != 0;
            screen::layout (s, list);
            checkFits (list);
            TEST_ASSERT_TRUE (hasText (list, "SignalPatch"));
            TEST_ASSERT_TRUE (hasText (list, "pair me"));
            s.link = screen::Link::connected;
            screen::layout (s, list);
            checkFits (list);
            TEST_ASSERT_TRUE (hasText (list, "connected"));
        }
    s = screen::State {};
    s.nameNumber = 7;
    screen::layout (s, list);
    TEST_ASSERT_TRUE (hasText (list, "C3 7"));

    // Home with the widest strings the protocol allows, every LED combination.
    const char* rigs[] = { "", "Clean", "WWWWWWWWWW", "WWWWWWWWWWW", "WWWWWWWWWWWWWWWW" };
    const char* labels[] = { "", "W", "WWWW", "WWWWW", "WW WWWWW", "WWWWWWWW", "RIG 5" };
    for (const char* rig : rigs)
        for (const char* label : labels)
            for (int leds = 0; leds < 81; ++leds)
            {
                s = screen::State {};
                s.link = screen::Link::app;
                std::strcpy (s.rig, rig);
                s.slot = static_cast<int8_t> (leds % 6) - 1;
                s.muted = leds % 2 == 0;
                s.gyroOn = leds % 3 == 0;
                s.gyroMissing = leds % 3 == 1;
                s.button3HoldsGyro = leds % 5 == 0;
                s.blinkFast = leds % 7 < 3;
                int code = leds;
                for (int i = 0; i < 4; ++i)
                {
                    s.led[i] = static_cast<uint8_t> (code % 3);
                    code /= 3;
                    std::strcpy (s.label[i], label);
                }
                screen::layout (s, list);
                checkFits (list);
            }

    // Overlays: 12-character labels, 10-character values, full and empty bars.
    s = screen::State {};
    s.link = screen::Link::app;
    s.overlay.visible = true;
    std::strcpy (s.overlay.title, "WWWWWWWWWWWW");
    std::strcpy (s.overlay.value, "WWWWWWWWWW");
    for (int position : { 0, 1, 64, 126, 127 })
    {
        s.overlay.bar = true;
        s.overlay.position = static_cast<uint8_t> (position);
        screen::layout (s, list);
        checkFits (list);
        TEST_ASSERT_TRUE (hasText (list, "WWWWWWWWWW"));
    }
    s.link = screen::Link::connected;
    std::strcpy (s.overlay.value, "CC119 127");
    screen::layout (s, list);
    checkFits (list);
}

void test_screens_from_the_controller_fit()
{
    FakeHost host;
    Controller c (host, Settings());
    screen::DrawList list;
    screen::State s;
    c.connected (0);
    c.subscribed (true, 0);
    c.encoder (-5, 10);
    c.screenState (11, s);
    screen::layout (s, list);
    checkFits (list);
    TEST_ASSERT_TRUE (hasText (list, "CC20  -1"));

    bringUp (c, host, 100);
    receive (c, sysex ({ 0x05, 20 }, "Feedback 100"), 200);
    receive (c, sysex ({ 0x06, 20, 127 }, "-12.50 dB/"), 300);
    c.encoder (1, 250);
    receive (c, sysex ({ 0x06, 20, 127 }, "-12.50 dB/"), 300);
    c.screenState (301, s);
    screen::layout (s, list);
    checkFits (list);
    TEST_ASSERT_TRUE (hasText (list, "Feedback 100"));
    TEST_ASSERT_TRUE (hasText (list, "-12.50 dB/"));
}

// ------------------------------------------------------------ End to end over BLE-MIDI packets

namespace
{
struct PacketLog
{
    std::vector<Bytes> packets;
    static void sink (void* context, const uint8_t* p, size_t n) { static_cast<PacketLog*> (context)->packets.emplace_back (p, p + n); }
};

struct MessageLog
{
    std::vector<Bytes> messages;
    static void sink (void* context, const uint8_t* m, size_t n) { static_cast<MessageLog*> (context)->messages.emplace_back (m, m + n); }
};

// The firmware's path: controller → encoder → packets.
struct EncodingHost : ControllerHost
{
    PacketLog out;
    blemidi::Encoder encoder { &PacketLog::sink, &out };
    void sendMidi (const uint8_t* m, size_t n) override { encoder.add (m, n, 1234); }
    void saveSettings (const Settings&) override {}
};

struct Delivery
{
    Controller* controller;
    static void sink (void* context, const uint8_t* m, size_t n) { static_cast<Delivery*> (context)->controller->midiIn (m, n, 2000); }
};
} // namespace

void test_settings_round_trip_over_20_byte_packets()
{
    EncodingHost host;
    host.encoder.setPacketSize (20);
    Controller c (host, Settings());
    c.connected (1000);
    c.subscribed (true, 1000);
    c.tick (1000);

    // The app's side: hello, a request, a change of every key, all split into 20-byte packets.
    PacketLog appOut;
    blemidi::Encoder app (&PacketLog::sink, &appOut);
    app.setPacketSize (20);
    const Bytes hello = sysex ({ 0x7F, 0x01 });
    const Bytes request = sysex ({ 0x10 });
    Bytes change { 0xF0, 0x7D, 0x53, 0x12 };
    for (uint8_t key = 0; key < kSettingCount; ++key)
    {
        change.push_back (key);
        change.push_back (static_cast<uint8_t> (settingInfo (key).maximum));
    }
    change.push_back (0xF7);
    app.add (hello.data(), hello.size(), 0);
    app.add (request.data(), request.size(), 1);
    app.add (change.data(), change.size(), 2);
    app.flush();
    TEST_ASSERT_TRUE (appOut.packets.size() >= 4);

    uint8_t sysexBuffer[256];
    Delivery delivery { &c };
    blemidi::Decoder device (sysexBuffer, sizeof (sysexBuffer), &Delivery::sink, &delivery);
    for (const auto& p : appOut.packets)
        device.decodePacket (p.data(), p.size());
    TEST_ASSERT_TRUE (c.appConnected());
    TEST_ASSERT_EQUAL (16, c.settings().midiChannel());
    TEST_ASSERT_EQUAL (90, c.settings().get (kGyroRange));

    c.button (0, true, 3000);
    host.encoder.flush();

    MessageLog received;
    uint8_t appBuffer[256];
    blemidi::Decoder appDecoder (appBuffer, sizeof (appBuffer), &MessageLog::sink, &received);
    for (const auto& p : host.out.packets)
    {
        TEST_ASSERT_TRUE (p.size() <= 20);
        appDecoder.decodePacket (p.data(), p.size());
    }
    // hello, the dump for 0x10, the dump for 0x12, the note on channel 16 (number 127 after the change).
    TEST_ASSERT_EQUAL (4, received.messages.size());
    expectBytes ({ 0xF0, 0x7D, 0x53, 0x7F, 0x00, 0x02, 0x01, 0x00, 0xF7 }, received.messages[0]);
    TEST_ASSERT_EQUAL (53, received.messages[1].size());
    TEST_ASSERT_EQUAL (10, received.messages[1][5]);
    TEST_ASSERT_EQUAL (53, received.messages[2].size());
    for (uint8_t key = 0; key < kSettingCount; ++key)
        TEST_ASSERT_EQUAL (settingInfo (key).maximum, received.messages[2][5 + 2 * key]);
    expectBytes ({ 0x9F, 127, 127 }, received.messages[3]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST (test_buttons_send_notes_on_channel_10);
    RUN_TEST (test_note_off_matches_the_note_on_after_a_settings_change);
    RUN_TEST (test_encoder_sends_relative_cc);
    RUN_TEST (test_encoder_acceleration_on_fast_spins);
    RUN_TEST (test_pots_send_only_on_movement);
    RUN_TEST (test_expression_switch_and_reverse);
    RUN_TEST (test_gyro_mode_3_short_and_long_push);
    RUN_TEST (test_gyro_mode_2_button_3_holds_it_on);
    RUN_TEST (test_gyro_mode_1_and_range);
    RUN_TEST (test_gyro_missing);
    RUN_TEST (test_hello_after_subscribe_repeats_until_answered);
    RUN_TEST (test_hello_probe_after_long_silence);
    RUN_TEST (test_settings_request_change_reset);
    RUN_TEST (test_name_number_applies_after_restart);
    RUN_TEST (test_feedback_reaches_the_screen);
    RUN_TEST (test_overlay_label_and_value_text);
    RUN_TEST (test_generic_screen_before_signalpatch);
    RUN_TEST (test_display_sleep_and_wake);
    RUN_TEST (test_split_label);
    RUN_TEST (test_every_screen_fits);
    RUN_TEST (test_screens_from_the_controller_fit);
    RUN_TEST (test_settings_round_trip_over_20_byte_packets);
    return UNITY_END();
}
