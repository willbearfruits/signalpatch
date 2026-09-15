#pragma once

#include "Graph.h"

#include <vector>

namespace signalpatch
{
// One MIDI control bound to one thing in the patch. Saved with the patch
// ("midi" array), so a rig carries its controller layout, and slots that
// glide adopt the target's map.
struct MidiMapping
{
    enum class Source { controlChange, note, programChange };
    enum class Target { parameter, bypass, command, slot, groupBypass };

    Source source = Source::controlChange;
    int channel = 0;      // 0 = any channel
    int number = 0;       // CC number, note number, or program number
    Target target = Target::parameter;
    NodeId node = 0;      // parameter / bypass / command
    int parameter = -1;   // parameter target
    int groupId = -1;     // groupBypass target
    int slot = -1;        // slot target (0-based)
    juce::String command; // command target (e.g. "rec")

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
