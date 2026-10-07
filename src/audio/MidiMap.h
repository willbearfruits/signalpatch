#pragma once

#include "Graph.h"

#include <vector>

namespace signalpatch
{
// One MIDI control bound to one thing in the patch. Saved with the patch
// ("midi" array), so a rig carries its controller layout, and slots that
// glide adopt the target's map. A view mapping is an app action rather than
// a module: the knob last touched, a knob of the selected module, the next
// or previous slot, mute (the action is in `command`). The app keeps those
// in its settings too, as mappings that work in every patch.
struct MidiMapping
{
    enum class Source { controlChange, note, programChange };
    enum class Target { parameter, bypass, command, slot, groupBypass, view };

    Source source = Source::controlChange;
    int channel = 0;      // 0 = any channel
    int number = 0;       // CC number, note number, or program number
    Target target = Target::parameter;
    NodeId node = 0;      // parameter / bypass / command
    int parameter = -1;   // parameter target
    int groupId = -1;     // groupBypass target
    int slot = -1;        // slot target (0-based)
    juce::String command; // command target (e.g. "rec"); view action: "knob", "next-knob",
                          // "module-knob-1".."module-knob-3", "next-slot", "prev-slot", "mute"
    bool relative = false; // CC 64 +/- n nudges the knob instead of setting it (encoders)
    int low = 0, high = 127; // expression calibration: this CC span covers the whole knob

    /** Absolute CC value -> 0..1 through the calibrated span (swapped ends invert). */
    [[nodiscard]] float normalised (int value) const noexcept
    {
        if (low == high)
            return value >= low ? 1.0f : 0.0f;
        return juce::jlimit (0.0f, 1.0f, static_cast<float> (value - low) / static_cast<float> (high - low));
    }

    [[nodiscard]] bool matches (Source messageSource, int messageChannel, int messageNumber) const noexcept
    {
        return source == messageSource && number == messageNumber && (channel == 0 || channel == messageChannel);
    }
    [[nodiscard]] juce::String sourceLabel() const
    {
        const juce::String prefix = source == Source::controlChange ? "CC" : source == Source::note ? "N" : "PC";
        return prefix + juce::String (number) + (channel > 0 ? "/" + juce::String (channel) : juce::String());
    }
};

juce::var midiMappingsToJson (const std::vector<MidiMapping>& mappings);
std::vector<MidiMapping> midiMappingsFromJson (const juce::var& value);
} // namespace signalpatch
