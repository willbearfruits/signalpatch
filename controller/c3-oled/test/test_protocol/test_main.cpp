// Settings table and SysEx protocol (docs/CONTROLLER.md).

#include "core/Protocol.h"
#include "core/Settings.h"

#include <unity.h>

#include <cstring>
#include <vector>

using namespace c3;
using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

namespace
{
proto::Parsed handle (const Bytes& message, proto::Feedback& feedback)
{
    return proto::handleSysEx (message.data(), message.size(), feedback);
}

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
} // namespace

// ------------------------------------------------------------ Settings

void test_settings_defaults_match_the_document()
{
    const uint8_t expected[kSettingCount] = { 10, 16, 17, 18, 11, 12, 13, 20, 60, 61, 62, 63,
                                              1, 0, 3, 30, 4, 1, 0, 1, 100, 0, 0, 0 };
    Settings s;
    for (uint8_t key = 0; key < kSettingCount; ++key)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE (expected[key], s.get (key), "default");
    TEST_ASSERT_EQUAL (10, s.midiChannel());
    TEST_ASSERT_TRUE (s.gyroMode() == GyroMode::pushToggles);
}

void test_settings_ranges_match_the_document()
{
    struct Range { uint8_t key, min, max; };
    const Range ranges[] = { { kMidiChannel, 1, 16 }, { kPot1Cc, 0, 119 }, { kEncoderCc, 0, 119 },
                             { kButton1Note, 0, 127 }, { kPushNote, 0, 127 }, { kExpressionOn, 0, 1 },
                             { kGyroMode, 0, 3 }, { kGyroRange, 5, 90 }, { kGyroSmoothing, 0, 10 },
                             { kPotDeadBand, 0, 4 }, { kBrightness, 0, 127 }, { kSleepMinutes, 0, 60 },
                             { kNameNumber, 0, 9 } };
    for (const auto& r : ranges)
    {
        TEST_ASSERT_EQUAL_UINT8 (r.min, settingInfo (r.key).minimum);
        TEST_ASSERT_EQUAL_UINT8 (r.max, settingInfo (r.key).maximum);
        Settings s;
        TEST_ASSERT_TRUE (s.set (r.key, -5));
        TEST_ASSERT_EQUAL_UINT8 (r.min, s.get (r.key));
        TEST_ASSERT_TRUE (s.set (r.key, 300));
        TEST_ASSERT_EQUAL_UINT8 (r.max, s.get (r.key));
    }
}

void test_settings_unknown_keys_are_ignored()
{
    Settings s;
    const Settings before = s;
    TEST_ASSERT_FALSE (s.set (0x18, 5));
    TEST_ASSERT_FALSE (s.set (0x7F, 5));
    TEST_ASSERT_TRUE (s == before);
    TEST_ASSERT_EQUAL (0, s.get (0x40));
}

void test_settings_blob_round_trip_and_damage()
{
    Settings s;
    s.set (kMidiChannel, 3);
    s.set (kGyroRange, 45);
    uint8_t blob[Settings::kBlobSize];
    s.toBlob (blob);

    Settings loaded;
    TEST_ASSERT_TRUE (loaded.fromBlob (blob, sizeof (blob)));
    TEST_ASSERT_TRUE (loaded == s);

    // An older blob with fewer keys keeps the newer ones at their defaults.
    uint8_t older[2 + 4] = { Settings::kBlobVersion, 4, 2, 30, 31, 32 };
    TEST_ASSERT_TRUE (loaded.fromBlob (older, sizeof (older)));
    TEST_ASSERT_EQUAL (2, loaded.midiChannel());
    TEST_ASSERT_EQUAL (32, loaded.get (kPot3Cc));
    TEST_ASSERT_EQUAL (11, loaded.get (kExpressionCc));

    // Out-of-range values in flash are clamped.
    blob[2 + kMidiChannel] = 0;
    blob[2 + kGyroRange] = 200;
    TEST_ASSERT_TRUE (loaded.fromBlob (blob, sizeof (blob)));
    TEST_ASSERT_EQUAL (1, loaded.midiChannel());
    TEST_ASSERT_EQUAL (90, loaded.get (kGyroRange));

    // Not ours: defaults.
    blob[0] = 99;
    TEST_ASSERT_FALSE (loaded.fromBlob (blob, sizeof (blob)));
    TEST_ASSERT_TRUE (loaded == Settings());
    TEST_ASSERT_FALSE (loaded.fromBlob (blob, 1));
    TEST_ASSERT_FALSE (loaded.fromBlob (nullptr, 0));
    const uint8_t truncated[] = { Settings::kBlobVersion, 24, 1, 2 };
    TEST_ASSERT_FALSE (loaded.fromBlob (truncated, sizeof (truncated)));
}

// ------------------------------------------------------------ Builders

void test_hello_bytes()
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL (9, proto::buildHello (out, sizeof (out)));
    const uint8_t expected[] = { 0xF0, 0x7D, 0x53, 0x7F, 0x00, 0x02, 0x01, 0x00, 0xF7 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY (expected, out, 9);
    TEST_ASSERT_EQUAL (0, proto::buildHello (out, 8));
}

void test_settings_dump_bytes()
{
    Settings s;
    s.set (kBrightness, 127);
    uint8_t out[64];
    const size_t n = proto::buildSettingsDump (s, out, sizeof (out));
    TEST_ASSERT_EQUAL (53, n);
    TEST_ASSERT_EQUAL_HEX8 (0xF0, out[0]);
    TEST_ASSERT_EQUAL_HEX8 (0x7D, out[1]);
    TEST_ASSERT_EQUAL_HEX8 (0x53, out[2]);
    TEST_ASSERT_EQUAL_HEX8 (0x11, out[3]);
    for (uint8_t key = 0; key < kSettingCount; ++key)
    {
        TEST_ASSERT_EQUAL_HEX8 (key, out[4 + 2 * key]);
        TEST_ASSERT_EQUAL_HEX8 (s.get (key), out[5 + 2 * key]);
    }
    TEST_ASSERT_EQUAL_HEX8 (0xF7, out[52]);
    for (size_t i = 1; i < 52; ++i)
        TEST_ASSERT_TRUE (out[i] < 0x80);
    TEST_ASSERT_EQUAL (0, proto::buildSettingsDump (s, out, 52));
}

// ------------------------------------------------------------ Parsing

void test_parse_hellos()
{
    proto::Feedback fb;
    TEST_ASSERT_TRUE (handle (sysex ({ 0x7F, 0x01 }), fb).kind == proto::Kind::appHello);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x7F, 0x00 }), fb).kind == proto::Kind::ignored); // our own, looped back
    TEST_ASSERT_TRUE (handle (sysex ({ 0x7F, 0x00, 0x02, 0x01, 0x00 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x7F }), fb).kind == proto::Kind::ignored);
}

void test_parse_switch_leds_and_labels()
{
    proto::Feedback fb;
    const uint32_t stamp = fb.stamp;
    TEST_ASSERT_TRUE (handle (sysex ({ 0x01, 0x00, 0x01 }), fb).kind == proto::Kind::feedback);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x01, 0x02, 0x02 }), fb).kind == proto::Kind::feedback);
    TEST_ASSERT_EQUAL (1, fb.led[0]);
    TEST_ASSERT_EQUAL (2, fb.led[2]);
    TEST_ASSERT_TRUE (fb.stamp > stamp);

    handle (sysex ({ 0x02, 0x01 }, "DRIVE"), fb);
    TEST_ASSERT_EQUAL_STRING ("DRIVE", fb.label[1]);
    handle (sysex ({ 0x02, 0x01 }, "LONGLABEL123"), fb);
    TEST_ASSERT_EQUAL_STRING ("LONGLABE", fb.label[1]); // 8 chars
    handle (sysex ({ 0x02, 0x01, 'A', 0x07, 'B' }), fb);
    TEST_ASSERT_EQUAL_STRING ("A?B", fb.label[1]);      // control characters
    handle (sysex ({ 0x02, 0x01 }), fb);
    TEST_ASSERT_EQUAL_STRING ("", fb.label[1]);         // cleared

    // Out of range or short: ignored, state untouched.
    TEST_ASSERT_TRUE (handle (sysex ({ 0x01, 0x08, 0x01 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x01, 0x00, 0x03 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x01, 0x00 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x02 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x02, 0x09 }, "X"), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_EQUAL (1, fb.led[0]);
}

void test_parse_slots_rig_flags()
{
    proto::Feedback fb;
    TEST_ASSERT_EQUAL (-1, fb.activeSlot);
    for (uint8_t slot = 0; slot < 5; ++slot)
        handle (sysex ({ 0x03, slot, static_cast<uint8_t> (slot == 2 ? 1 : 0) }), fb);
    TEST_ASSERT_EQUAL (2, fb.activeSlot);
    handle (sysex ({ 0x03, 0x02, 0x00 }), fb);
    TEST_ASSERT_EQUAL (-1, fb.activeSlot);
    handle (sysex ({ 0x03, 0x04, 0x01 }), fb);
    handle (sysex ({ 0x03, 0x02, 0x00 }), fb); // another slot going off leaves the live one
    TEST_ASSERT_EQUAL (4, fb.activeSlot);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x03, 0x05, 0x01 }), fb).kind == proto::Kind::ignored);

    handle (sysex ({ 0x04 }, "Clean Machine"), fb);
    TEST_ASSERT_EQUAL_STRING ("Clean Machine", fb.rig);
    handle (sysex ({ 0x04 }, "ABCDEFGHIJKLMNOPQRSTU"), fb);
    TEST_ASSERT_EQUAL_STRING ("ABCDEFGHIJKLMNOP", fb.rig); // 16 chars

    handle (sysex ({ 0x08, 0x01 }), fb);
    TEST_ASSERT_TRUE (fb.muted);
    handle (sysex ({ 0x08, 0x02 }), fb);
    TEST_ASSERT_FALSE (fb.muted);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x08 }), fb).kind == proto::Kind::ignored);
}

void test_parse_cc_labels_and_values()
{
    proto::Feedback fb;
    handle (sysex ({ 0x05, 16 }, "Cutoff"), fb);
    TEST_ASSERT_EQUAL_STRING ("Cutoff", fb.cc[16].label);
    handle (sysex ({ 0x05, 17 }, "A label too long"), fb);
    TEST_ASSERT_EQUAL_STRING ("A label too ", fb.cc[17].label); // 12 chars
    TEST_ASSERT_EQUAL (0u, fb.cc[16].valueStamp);

    const uint32_t before = fb.stamp;
    handle (sysex ({ 0x06, 16, 100 }, "6.50 kHz"), fb);
    TEST_ASSERT_EQUAL_STRING ("6.50 kHz", fb.cc[16].value);
    TEST_ASSERT_EQUAL (100, fb.cc[16].position);
    TEST_ASSERT_TRUE (fb.cc[16].valueStamp > before);
    TEST_ASSERT_EQUAL (fb.stamp, fb.cc[16].valueStamp);

    handle (sysex ({ 0x06, 16, 5 }, "12345678901234"), fb);
    TEST_ASSERT_EQUAL_STRING ("1234567890", fb.cc[16].value); // 10 chars
    TEST_ASSERT_TRUE (handle (sysex ({ 0x06, 16 }), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x05 }), fb).kind == proto::Kind::ignored);

    // A 0x05 clears the value until the next 0x06 for that CC.
    handle (sysex ({ 0x05, 16 }, "Mix"), fb);
    TEST_ASSERT_EQUAL_STRING ("Mix", fb.cc[16].label);
    TEST_ASSERT_EQUAL_STRING ("", fb.cc[16].value);
    TEST_ASSERT_EQUAL (0, fb.cc[16].position);
    TEST_ASSERT_EQUAL (0u, fb.cc[16].valueStamp);
    handle (sysex ({ 0x06, 16, 64 }, "50 %"), fb);
    TEST_ASSERT_EQUAL_STRING ("50 %", fb.cc[16].value);
    TEST_ASSERT_TRUE (fb.cc[16].valueStamp != 0);
}

void test_parse_settings_commands()
{
    proto::Feedback fb;
    TEST_ASSERT_TRUE (handle (sysex ({ 0x10 }), fb).kind == proto::Kind::settingsRequest);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x13 }), fb).kind == proto::Kind::settingsReset);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x14 }), fb).kind == proto::Kind::gyroZero);
    TEST_ASSERT_TRUE (handle (sysex ({ 0x11, 0x00, 0x05 }), fb).kind == proto::Kind::ignored); // a dump is ours to send

    const Bytes change = sysex ({ 0x12, 0x00, 0x02, 0x50, 0x09, 0x0F, 0x7F, 0x14 });
    const auto parsed = handle (change, fb);
    TEST_ASSERT_TRUE (parsed.kind == proto::Kind::settingsChange);
    TEST_ASSERT_EQUAL (7, parsed.payloadLength);

    Settings s;
    // channel 2, unknown key 0x50 ignored, gyro range 127 clamped to 90, trailing odd byte ignored
    TEST_ASSERT_EQUAL (2, proto::applySettingsChange (parsed.payload, parsed.payloadLength, s));
    TEST_ASSERT_EQUAL (2, s.midiChannel());
    TEST_ASSERT_EQUAL (90, s.get (kGyroRange));
    TEST_ASSERT_EQUAL (100, s.get (kBrightness));
}

void test_parse_rejects_malformed_sysex()
{
    proto::Feedback fb;
    const uint32_t stamp = fb.stamp;
    const Bytes bad[] = {
        {},
        { 0xF0 },
        { 0xF0, 0xF7 },
        { 0xF0, 0x7D, 0xF7 },
        { 0xF0, 0x7D, 0x53, 0xF7 },
        { 0xF0, 0x7E, 0x53, 0x01, 0x00, 0x01, 0xF7 },  // other manufacturer
        { 0xF0, 0x7D, 0x54, 0x01, 0x00, 0x01, 0xF7 },  // other device
        { 0xF0, 0x7D, 0x53, 0x01, 0x00, 0x81, 0xF7 },  // 8-bit byte inside
        { 0xF0, 0x7D, 0x53, 0x01, 0x00, 0x01 },        // no F7
        { 0x90, 0x7D, 0x53, 0x01, 0x00, 0x01, 0xF7 },  // not a SysEx
        { 0xF0, 0x7D, 0x53, 0x09, 0x00, 0xF7 },        // unknown command
        { 0xF0, 0x7D, 0x53, 0x07, 0xF7 },
    };
    for (const auto& m : bad)
        TEST_ASSERT_TRUE (proto::handleSysEx (m.data(), m.size(), fb).kind == proto::Kind::ignored);
    TEST_ASSERT_TRUE (proto::handleSysEx (nullptr, 5, fb).kind == proto::Kind::ignored);
    TEST_ASSERT_EQUAL (stamp, fb.stamp);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST (test_settings_defaults_match_the_document);
    RUN_TEST (test_settings_ranges_match_the_document);
    RUN_TEST (test_settings_unknown_keys_are_ignored);
    RUN_TEST (test_settings_blob_round_trip_and_damage);
    RUN_TEST (test_hello_bytes);
    RUN_TEST (test_settings_dump_bytes);
    RUN_TEST (test_parse_hellos);
    RUN_TEST (test_parse_switch_leds_and_labels);
    RUN_TEST (test_parse_slots_rig_flags);
    RUN_TEST (test_parse_cc_labels_and_values);
    RUN_TEST (test_parse_settings_commands);
    RUN_TEST (test_parse_rejects_malformed_sysex);
    return UNITY_END();
}
