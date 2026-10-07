#include "Protocol.h"

#include <cstring>

namespace c3::proto
{
size_t buildHello (uint8_t* out, size_t capacity)
{
    if (capacity < kHelloSize)
        return 0;
    const uint8_t hello[kHelloSize] = { 0xF0, kManufacturerId, kDeviceId, kHello, kFromController,
                                        kModel, kFirmwareMajor, kFirmwareMinor, 0xF7 };
    std::memcpy (out, hello, kHelloSize);
    return kHelloSize;
}

size_t buildSettingsDump (const Settings& settings, uint8_t* out, size_t capacity)
{
    if (capacity < kSettingsDumpSize)
        return 0;
    size_t n = 0;
    out[n++] = 0xF0;
    out[n++] = kManufacturerId;
    out[n++] = kDeviceId;
    out[n++] = kSettingsDump;
    for (uint8_t key = 0; key < kSettingCount; ++key)
    {
        out[n++] = key;
        out[n++] = settings.get (key);
    }
    out[n++] = 0xF7;
    return n;
}

void Feedback::clear()
{
    std::memset (led, 0, sizeof (led));
    std::memset (label, 0, sizeof (label));
    activeSlot = -1;
    std::memset (rig, 0, sizeof (rig));
    muted = false;
    std::memset (cc, 0, sizeof (cc));
    ++stamp;
}

void copyText (const uint8_t* text, size_t length, char* out, size_t capacity)
{
    if (capacity == 0)
        return;
    size_t n = 0;
    for (size_t i = 0; i < length && n + 1 < capacity; ++i)
    {
        const uint8_t c = text[i];
        out[n++] = (c >= 32 && c < 127) ? static_cast<char> (c) : '?';
    }
    out[n] = '\0';
}

bool isOurs (const uint8_t* message, size_t length)
{
    if (message == nullptr || length < 5 || message[0] != 0xF0 || message[length - 1] != 0xF7)
        return false;
    if (message[1] != kManufacturerId || message[2] != kDeviceId)
        return false;
    for (size_t i = 1; i + 1 < length; ++i)
        if (message[i] & 0x80)
            return false;
    return true;
}

namespace
{
    // Sets a text field and reports whether it changed.
    bool setText (char* field, size_t capacity, const uint8_t* text, size_t length)
    {
        char next[32];
        copyText (text, length, next, capacity < sizeof (next) ? capacity : sizeof (next));
        if (std::strcmp (field, next) == 0)
            return false;
        std::strcpy (field, next);
        return true;
    }

    bool applyFeedback (uint8_t command, const uint8_t* p, size_t n, Feedback& fb)
    {
        switch (command)
        {
            case kSwitchLed:
                if (n < 2 || p[0] >= Feedback::kSwitches || p[1] > 2)
                    return false;
                fb.led[p[0]] = p[1];
                return true;
            case kSwitchLabel:
                if (n < 1 || p[0] >= Feedback::kSwitches)
                    return false;
                setText (fb.label[p[0]], sizeof (fb.label[0]), p + 1, n - 1);
                return true;
            case kSlotActive:
                if (n < 2 || p[0] >= Feedback::kSlots || p[1] > 1)
                    return false;
                if (p[1] == 1)
                    fb.activeSlot = static_cast<int8_t> (p[0]);
                else if (fb.activeSlot == static_cast<int8_t> (p[0]))
                    fb.activeSlot = -1;
                return true;
            case kRigName:
                setText (fb.rig, sizeof (fb.rig), p, n);
                return true;
            case kCcLabel:
            {
                if (n < 1)
                    return false;
                // A new label means a new target: its value is unknown until a 0x06 says it.
                auto& cc = fb.cc[p[0]];
                setText (cc.label, sizeof (cc.label), p + 1, n - 1);
                cc.value[0] = '\0';
                cc.position = 0;
                cc.valueStamp = 0;
                return true;
            }
            case kCcValue:
            {
                if (n < 2)
                    return false;
                auto& cc = fb.cc[p[0]];
                setText (cc.value, sizeof (cc.value), p + 2, n - 2);
                cc.position = p[1];
                cc.valueStamp = fb.stamp + 1; // the stamp this change gets below
                return true;
            }
            case kFlags:
                if (n < 1)
                    return false;
                fb.muted = (p[0] & 0x01) != 0;
                return true;
            default:
                return false;
        }
    }
} // namespace

Parsed handleSysEx (const uint8_t* message, size_t length, Feedback& feedback)
{
    Parsed parsed;
    if (! isOurs (message, length))
        return parsed;
    parsed.command = message[3];
    parsed.payload = message + 4;
    parsed.payloadLength = length - 5;

    switch (parsed.command)
    {
        case kHello:
            if (parsed.payloadLength >= 1 && parsed.payload[0] == kFromSignalPatch)
                parsed.kind = Kind::appHello;
            return parsed; // our own hello looped back stays ignored
        case kSettingsRequest:
            parsed.kind = Kind::settingsRequest;
            return parsed;
        case kSettingsChange:
            parsed.kind = Kind::settingsChange;
            return parsed;
        case kSettingsReset:
            parsed.kind = Kind::settingsReset;
            return parsed;
        case kGyroZero:
            parsed.kind = Kind::gyroZero;
            return parsed;
        default:
            break;
    }

    if (applyFeedback (parsed.command, parsed.payload, parsed.payloadLength, feedback))
    {
        ++feedback.stamp;
        parsed.kind = Kind::feedback;
    }
    return parsed;
}

int applySettingsChange (const uint8_t* payload, size_t length, Settings& settings)
{
    int applied = 0;
    for (size_t i = 0; i + 1 < length; i += 2)
        if (settings.set (payload[i], payload[i + 1]))
            ++applied;
    return applied;
}
} // namespace c3::proto
