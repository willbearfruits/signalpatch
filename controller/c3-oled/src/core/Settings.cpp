#include "Settings.h"

#include <cstring>

namespace c3
{
namespace
{
    // The table in docs/CONTROLLER.md, in key order.
    const SettingInfo infos[kSettingCount] = {
        { 10, 1, 16 },  // 0x00 MIDI channel
        { 16, 0, 119 }, // 0x01 Pot 1 CC
        { 17, 0, 119 }, // 0x02 Pot 2 CC
        { 18, 0, 119 }, // 0x03 Pot 3 CC
        { 11, 0, 119 }, // 0x04 Expression CC
        { 12, 0, 119 }, // 0x05 Gyro tilt CC
        { 13, 0, 119 }, // 0x06 Gyro roll CC
        { 20, 0, 119 }, // 0x07 Encoder CC
        { 60, 0, 127 }, // 0x08 Button 1 note
        { 61, 0, 127 }, // 0x09 Button 2 note
        { 62, 0, 127 }, // 0x0A Button 3 note
        { 63, 0, 127 }, // 0x0B Encoder push note
        { 1, 0, 1 },    // 0x0C Expression input on
        { 0, 0, 1 },    // 0x0D Expression reversed
        { 3, 0, 3 },    // 0x0E Gyro mode
        { 30, 5, 90 },  // 0x0F Gyro range, degrees each side of level
        { 4, 0, 10 },   // 0x10 Gyro smoothing
        { 1, 0, 4 },    // 0x11 Pot dead band, CC steps
        { 0, 0, 1 },    // 0x12 Encoder reversed
        { 1, 0, 1 },    // 0x13 Encoder acceleration
        { 100, 0, 127 },// 0x14 Display brightness
        { 0, 0, 1 },    // 0x15 Display upside down
        { 0, 0, 60 },   // 0x16 Display sleep, minutes (0 = never)
        { 0, 0, 9 },    // 0x17 Number added to the Bluetooth name
    };
} // namespace

const SettingInfo& settingInfo (uint8_t key)
{
    return infos[key < kSettingCount ? key : 0];
}

uint8_t Settings::clampValue (uint8_t key, int value)
{
    const auto& info = settingInfo (key);
    if (value < info.minimum)
        return info.minimum;
    if (value > info.maximum)
        return info.maximum;
    return static_cast<uint8_t> (value);
}

void Settings::resetToDefaults()
{
    for (uint8_t key = 0; key < kSettingCount; ++key)
        values[key] = infos[key].defaultValue;
}

bool Settings::set (uint8_t key, int value)
{
    if (! isKnownKey (key))
        return false;
    values[key] = clampValue (key, value);
    return true;
}

void Settings::toBlob (uint8_t* out) const
{
    out[0] = kBlobVersion;
    out[1] = kSettingCount;
    std::memcpy (out + 2, values, kSettingCount);
}

bool Settings::fromBlob (const uint8_t* data, size_t length)
{
    resetToDefaults();
    if (data == nullptr || length < 2 || data[0] != kBlobVersion)
        return false;
    const size_t count = data[1];
    if (length < 2 + count)
        return false;
    for (size_t key = 0; key < count && key < kSettingCount; ++key)
        values[key] = clampValue (static_cast<uint8_t> (key), data[2 + key]);
    return true;
}

bool Settings::operator== (const Settings& other) const
{
    return std::memcmp (values, other.values, kSettingCount) == 0;
}
} // namespace c3
