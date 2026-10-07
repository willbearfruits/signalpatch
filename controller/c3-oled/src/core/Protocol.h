#pragma once

// SignalPatch controller SysEx (docs/CONTROLLER.md): F0 7D 53 <command> ... F7.
// Builds what the controller sends (hello, settings dump) and parses what the
// app sends into a Feedback snapshot the display draws from.

#include "Settings.h"

#include <cstddef>
#include <cstdint>

namespace c3::proto
{
constexpr uint8_t kManufacturerId = 0x7D; // non-commercial
constexpr uint8_t kDeviceId = 0x53;       // 'S'
constexpr uint8_t kModel = 0x02;          // the C3 handheld
constexpr uint8_t kFirmwareMajor = 1;
constexpr uint8_t kFirmwareMinor = 0;

enum Command : uint8_t
{
    kSwitchLed = 0x01,
    kSwitchLabel = 0x02,
    kSlotActive = 0x03,
    kRigName = 0x04,
    kCcLabel = 0x05,
    kCcValue = 0x06,
    kFlags = 0x08,
    kSettingsRequest = 0x10,
    kSettingsDump = 0x11,
    kSettingsChange = 0x12,
    kSettingsReset = 0x13,
    kGyroZero = 0x14,
    kHello = 0x7F
};

enum HelloSide : uint8_t
{
    kFromController = 0x00,
    kFromSignalPatch = 0x01
};

constexpr size_t kHelloSize = 9;                              // F0 7D 53 7F 00 02 01 00 F7
constexpr size_t kSettingsDumpSize = 5 + 2 * kSettingCount;   // F0 7D 53 11 (key value)... F7

/** Writes the controller's hello. Returns its length, or 0 if `capacity` is too small. */
size_t buildHello (uint8_t* out, size_t capacity);

/** Writes 0x11 with every key and value. Returns its length, or 0 if `capacity` is too small. */
size_t buildSettingsDump (const Settings& settings, uint8_t* out, size_t capacity);

/** What SignalPatch has told the controller so far. */
struct Feedback
{
    static constexpr int kSwitches = 8;
    static constexpr int kSlots = 5;
    static constexpr int kLabelChars = 8;
    static constexpr int kRigChars = 16;
    static constexpr int kCcLabelChars = 12;
    static constexpr int kCcValueChars = 10;

    struct Cc
    {
        char label[kCcLabelChars + 1];
        char value[kCcValueChars + 1];
        uint8_t position;
        uint32_t valueStamp; // 0 = no 0x06 yet; otherwise the Feedback::stamp it arrived with
    };

    uint8_t led[kSwitches];                       // 0 off, 1 on, 2 blink
    char label[kSwitches][kLabelChars + 1];
    int8_t activeSlot;                            // -1 = none
    char rig[kRigChars + 1];
    bool muted;
    Cc cc[128];
    uint32_t stamp;                               // moves on every change

    Feedback() : stamp (0) { clear(); }
    void clear();
};

enum class Kind : uint8_t
{
    ignored,          // not ours, malformed, or a command we do not know
    appHello,         // F0 7D 53 7F 01 ...
    feedback,         // applied to the Feedback
    settingsRequest,  // 0x10
    settingsChange,   // 0x12: pairs are in `payload`
    settingsReset,    // 0x13
    gyroZero          // 0x14
};

struct Parsed
{
    Kind kind = Kind::ignored;
    uint8_t command = 0;
    const uint8_t* payload = nullptr; // bytes between the command and F7
    size_t payloadLength = 0;
};

/** True for F0 7D 53 ... F7 with nothing but 7-bit bytes inside. */
bool isOurs (const uint8_t* message, size_t length);

/** Parses one complete SysEx (F0 ... F7). Feedback commands are applied to `feedback`. */
Parsed handleSysEx (const uint8_t* message, size_t length, Feedback& feedback);

/** Applies the key/value pairs of a 0x12 payload. Unknown keys and a trailing odd byte are ignored.
    Returns how many known keys it set. */
int applySettingsChange (const uint8_t* payload, size_t length, Settings& settings);

/** Copies 7-bit text into `out` (capacity includes the terminator); control characters become '?'. */
void copyText (const uint8_t* text, size_t length, char* out, size_t capacity);
} // namespace c3::proto
