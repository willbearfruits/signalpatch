#pragma once

// The controller's settings (docs/CONTROLLER.md, "Settings"): 7-bit keys and
// values, stored in flash, read and written by SignalPatch over SysEx.
// Pure C++ so the host tests can check defaults, ranges and the stored blob.

#include <cstddef>
#include <cstdint>

namespace c3
{
enum SettingKey : uint8_t
{
    kMidiChannel = 0x00,
    kPot1Cc = 0x01,
    kPot2Cc = 0x02,
    kPot3Cc = 0x03,
    kExpressionCc = 0x04,
    kGyroTiltCc = 0x05,
    kGyroRollCc = 0x06,
    kEncoderCc = 0x07,
    kButton1Note = 0x08,
    kButton2Note = 0x09,
    kButton3Note = 0x0A,
    kPushNote = 0x0B,
    kExpressionOn = 0x0C,
    kExpressionReversed = 0x0D,
    kGyroMode = 0x0E,
    kGyroRange = 0x0F,
    kGyroSmoothing = 0x10,
    kPotDeadBand = 0x11,
    kEncoderReversed = 0x12,
    kEncoderAcceleration = 0x13,
    kBrightness = 0x14,
    kUpsideDown = 0x15,
    kSleepMinutes = 0x16,
    kNameNumber = 0x17,
    kSettingCount = 0x18
};

enum class GyroMode : uint8_t
{
    off = 0,
    alwaysOn = 1,
    whileButton3Held = 2,
    pushToggles = 3
};

struct SettingInfo
{
    uint8_t defaultValue;
    uint8_t minimum;
    uint8_t maximum;
};

/** Default and range of a known key (key < kSettingCount). */
const SettingInfo& settingInfo (uint8_t key);

class Settings
{
public:
    Settings() { resetToDefaults(); }

    static bool isKnownKey (uint8_t key) { return key < kSettingCount; }

    /** The value a key would hold after storing `value` (clamped to its range). */
    static uint8_t clampValue (uint8_t key, int value);

    void resetToDefaults();

    /** Stores a clamped value. Returns false (and changes nothing) for an unknown key. */
    bool set (uint8_t key, int value);
    uint8_t get (uint8_t key) const { return isKnownKey (key) ? values[key] : 0; }

    uint8_t midiChannel() const { return values[kMidiChannel]; } // 1..16
    GyroMode gyroMode() const { return static_cast<GyroMode> (values[kGyroMode]); }
    bool flag (uint8_t key) const { return get (key) != 0; }

    // Flash format: version, key count, then one value per key. A blob from an
    // older firmware with fewer keys leaves the newer keys at their defaults.
    static constexpr uint8_t kBlobVersion = 1;
    static constexpr size_t kBlobSize = 2 + kSettingCount;
    void toBlob (uint8_t* out) const;
    /** Loads a blob, clamping every value. Returns false (defaults kept) when the blob is not ours. */
    bool fromBlob (const uint8_t* data, size_t length);

    bool operator== (const Settings& other) const;
    bool operator!= (const Settings& other) const { return ! (*this == other); }

private:
    uint8_t values[kSettingCount];
};
} // namespace c3
