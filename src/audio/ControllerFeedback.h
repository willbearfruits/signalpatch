#pragma once

#include "Graph.h"
#include "MidiMap.h"

#include <array>
#include <vector>

// Feedback for the SignalPatch foot controller (docs/CONTROLLER.md): what
// each footswitch currently means, which rig slot is live, the rig name.
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
enum Command : juce::uint8 { switchLed = 0x01, switchLabel = 0x02, slotActive = 0x03, rigName = 0x04, hello = 0x7F };
enum HelloSide : juce::uint8 { fromController = 0x00, fromSignalPatch = 0x01 };

struct SwitchState
{
    int led = 0; // 0 off, 1 on, 2 blink
    juce::String label;
    bool operator== (const SwitchState& other) const noexcept { return led == other.led && label == other.label; }
    bool operator!= (const SwitchState& other) const noexcept { return ! (*this == other); }
};

struct State
{
    std::array<SwitchState, switchCount> switches;
    int activeSlot = -1;
    juce::String rig;
};

/** Reads the document's mappings for the controller's notes and program changes. */
State computeState (const PatchDocument& document, int activeSlot, const juce::String& rig);

/** Messages that bring a controller from `previous` to `next`; everything when `full`. */
std::vector<juce::MidiMessage> encode (const State& previous, const State& next, bool full);

juce::MidiMessage helloMessage (HelloSide side);
[[nodiscard]] bool isHello (const juce::MidiMessage& message, HelloSide side) noexcept;
[[nodiscard]] juce::String labelFor (const PatchDocument& document, const MidiMapping& mapping);
} // namespace signalpatch::controller
