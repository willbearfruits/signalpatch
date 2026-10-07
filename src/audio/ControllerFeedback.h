#pragma once

#include "Graph.h"
#include "MidiMap.h"

#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// Feedback for hardware controllers (docs/CONTROLLER.md): what each switch
// and each continuous control currently means, which rig slot is live, the
// rig name, the mute flag; plus the settings SysEx of the C3 handheld.
// Pure functions over the document so the engine can diff and send only
// what changed, and the tests can check the bytes without a MIDI port.
namespace signalpatch::controller
{
constexpr int switchCount = 8;
constexpr int firstSwitchNote = 60; // footswitch n = note 60 + n on channel 10
constexpr int switchChannel = 10;
constexpr int slotCount = 5;

constexpr juce::uint8 manufacturerId = 0x7D; // non-commercial
constexpr juce::uint8 deviceId = 0x53;       // 'S'
enum Command : juce::uint8 { switchLed = 0x01, switchLabel = 0x02, slotActive = 0x03, rigName = 0x04, controlLabel = 0x05, controlValue = 0x06,
                             flags = 0x08, settingsRequest = 0x10, settingsDump = 0x11, settingsSet = 0x12, settingsReset = 0x13, gyroZero = 0x14,
                             hello = 0x7F };
enum HelloSide : juce::uint8 { fromController = 0x00, fromSignalPatch = 0x01 };
enum Model : int { unknownModel = 0, footController = 0x01, handheldC3 = 0x02 };

/** What the app is pointing at, for the view actions ("knob", "module-knob-n") and the flags. */
struct ViewContext
{
    NodeId touchedNode = 0;
    int touchedParameter = -1; // the knob last touched ("knob")
    NodeId selectedNode = 0;   // the selected module ("module-knob-n", "next-knob")
    bool muted = false;
};

struct SwitchState
{
    int led = 0; // 0 off, 1 on, 2 blink
    juce::String label;
    bool operator== (const SwitchState& other) const noexcept { return led == other.led && label == other.label; }
    bool operator!= (const SwitchState& other) const noexcept { return ! (*this == other); }
};

struct ControlState
{
    juce::String label;     // up to 12 chars, 7-bit; empty = drives nothing
    juce::String valueText; // up to 10 chars, as the app prints the value
    int position = -1;      // 0-127 knob travel, -1 unknown (no 0x06 is sent)
    bool operator== (const ControlState& other) const noexcept
    {
        return label == other.label && valueText == other.valueText && position == other.position;
    }
    bool operator!= (const ControlState& other) const noexcept { return ! (*this == other); }
};

struct State
{
    std::array<SwitchState, switchCount> switches;
    int activeSlot = -1;
    juce::String rig;
    std::map<int, ControlState> controls; // by CC number
    bool muted = false;
};

/** Where a controller's controls sit: the channel it sends on and the note of each switch.
    The default is the protocol's (channel 10, notes 60-67); a C3 reports its own in its settings. */
struct Layout
{
    int channel = switchChannel;
    std::array<int, switchCount> switchNotes { 60, 61, 62, 63, 64, 65, 66, 67 };
};

/** Patch mappings first, then the app-wide ones, for switches and for each CC in controlNumbers. */
State computeState (const PatchDocument& document, const std::vector<MidiMapping>& globalMappings, int activeSlot,
                    const juce::String& rig, const ViewContext& view, const std::vector<int>& controlNumbers,
                    const Layout& layout = {});

/** Messages that bring a controller from `previous` to `next`; everything when `full`. */
std::vector<juce::MidiMessage> encode (const State& previous, const State& next, bool full);

/** CC 4, 11-13, 16-18, 20-23: what to label when a controller reported no settings. */
const std::vector<int>& defaultControlNumbers();
/** The CCs a C3 reported in its settings (keys 0x01-0x07), without repeats; empty when none. */
std::vector<int> controlNumbersFromSettings (const std::map<int, int>& settings);
/** The channel (key 0x00) and switch notes (buttons 1-3 and the encoder push, keys 0x08-0x0B) a C3 reported. */
Layout layoutFromSettings (const std::map<int, int>& settings);

struct HelloInfo
{
    int side = 0, model = 0, firmwareMajor = 0, firmwareMinor = 0;
};
/** F0 7D 53 7F side [model major minor] F7; the short form is model 0. */
std::optional<HelloInfo> parseHello (const juce::MidiMessage& message);
juce::MidiMessage helloMessage (HelloSide side);
[[nodiscard]] bool isHello (const juce::MidiMessage& message, HelloSide side) noexcept;
/** A switch's label for a mapping (the pedal's name, "REC <pedal>", "RIG n", a group's name, "MUTE"...). */
[[nodiscard]] juce::String labelFor (const PatchDocument& document, const MidiMapping& mapping);

struct SettingInfo
{
    int key;
    const char* name;
    int defaultValue, minimum, maximum;
    std::vector<const char*> choices; // a name per value for small enums and on/off settings; empty for numbers
};
/** Every setting of the C3 handheld (CONTROLLER.md), in key order. */
const std::vector<SettingInfo>& handheldSettings();
juce::MidiMessage settingsRequestMessage();
juce::MidiMessage settingsSetMessage (const std::vector<std::pair<int, int>>& changes);
juce::MidiMessage settingsResetMessage();
juce::MidiMessage gyroZeroMessage();
/** F0 7D 53 11 key value ... F7 -> key -> value; nullopt for anything else or an odd byte count. */
std::optional<std::map<int, int>> parseSettingsDump (const juce::MidiMessage& message);
} // namespace signalpatch::controller
