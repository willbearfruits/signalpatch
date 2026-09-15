#include "ControllerFeedback.h"

#include <initializer_list>

namespace signalpatch::controller
{
namespace
{
    juce::String ascii7 (const juce::String& text, int maxLength)
    {
        juce::String out;
        for (const auto c : text)
        {
            if (out.length() >= maxLength)
                break;
            out += (c >= 32 && c < 127) ? juce::String::charToString (c) : juce::String ("?");
        }
        return out;
    }

    juce::MidiMessage sysex (std::initializer_list<juce::uint8> header, const juce::String& text = {})
    {
        std::vector<juce::uint8> bytes { manufacturerId, deviceId };
        bytes.insert (bytes.end(), header.begin(), header.end());
        for (const auto c : text)
            bytes.push_back (static_cast<juce::uint8> (c & 0x7F));
        return juce::MidiMessage::createSysExMessage (bytes.data(), static_cast<int> (bytes.size()));
    }

    juce::String shortName (const PatchDocument& document, NodeId id)
    {
        const auto* node = document.findNode (id);
        return node == nullptr ? juce::String() : node->processor->getName();
    }
} // namespace

juce::String labelFor (const PatchDocument& document, const MidiMapping& mapping)
{
    switch (mapping.target)
    {
        case MidiMapping::Target::bypass:    return shortName (document, mapping.node);
        case MidiMapping::Target::command:   return mapping.command.toUpperCase() + " " + shortName (document, mapping.node);
        case MidiMapping::Target::slot:      return "RIG " + juce::String (mapping.slot + 1);
        case MidiMapping::Target::groupBypass:
            for (const auto& group : document.getGroups())
                if (group.id == mapping.groupId)
                    return group.name;
            return "GROUP";
        case MidiMapping::Target::parameter:
        {
            const auto* node = document.findNode (mapping.node);
            if (node == nullptr || ! juce::isPositiveAndBelow (mapping.parameter, node->processor->getNumParameters()))
                return {};
            return node->processor->getParameter (mapping.parameter).name;
        }
    }
    return {};
}

State computeState (const PatchDocument& document, int activeSlot, const juce::String& rig)
{
    State state;
    state.activeSlot = activeSlot;
    state.rig = ascii7 (rig, 16);
    for (int index = 0; index < switchCount; ++index)
    {
        auto& sw = state.switches[static_cast<std::size_t> (index)];
        for (const auto& mapping : document.getMidiMappings())
        {
            if (! mapping.matches (MidiMapping::Source::note, switchChannel, firstSwitchNote + index))
                continue;
            sw.label = ascii7 (labelFor (document, mapping), 8);
            switch (mapping.target)
            {
                case MidiMapping::Target::bypass:
                    if (const auto* node = document.findNode (mapping.node))
                        sw.led = node->processor->isBypassed() ? 0 : 1;
                    break;
                case MidiMapping::Target::command:
                    if (const auto* node = document.findNode (mapping.node))
                        sw.led = node->processor->uiToggleState (mapping.command) ? (mapping.command == "rec" ? 2 : 1) : 0;
                    break;
                case MidiMapping::Target::slot:
                    sw.led = mapping.slot == activeSlot ? 1 : 0;
                    break;
                case MidiMapping::Target::groupBypass:
                    for (const auto& group : document.getGroups())
                        if (group.id == mapping.groupId)
                            for (const auto member : group.members)
                                if (const auto* node = document.findNode (member); node != nullptr && ! node->processor->isBypassed())
                                    sw.led = 1;
                    break;
                case MidiMapping::Target::parameter:
                    break;
            }
            break; // first mapping on the switch owns its LED
        }
    }
    return state;
}

std::vector<juce::MidiMessage> encode (const State& previous, const State& next, bool full)
{
    std::vector<juce::MidiMessage> out;
    for (int index = 0; index < switchCount; ++index)
    {
        const auto& before = previous.switches[static_cast<std::size_t> (index)];
        const auto& after = next.switches[static_cast<std::size_t> (index)];
        if (full || before.led != after.led)
            out.push_back (sysex ({ switchLed, static_cast<juce::uint8> (index), static_cast<juce::uint8> (after.led) }));
        if (full || before.label != after.label)
            out.push_back (sysex ({ switchLabel, static_cast<juce::uint8> (index) }, after.label));
    }
    if (full || previous.activeSlot != next.activeSlot)
        for (int slot = 0; slot < slotCount; ++slot)
            if (full || slot == previous.activeSlot || slot == next.activeSlot)
                out.push_back (sysex ({ slotActive, static_cast<juce::uint8> (slot), static_cast<juce::uint8> (slot == next.activeSlot ? 1 : 0) }));
    if (full || previous.rig != next.rig)
        out.push_back (sysex ({ rigName }, next.rig));
    return out;
}

juce::MidiMessage helloMessage (HelloSide side)
{
    return sysex ({ hello, side });
}

bool isHello (const juce::MidiMessage& message, HelloSide side) noexcept
{
    if (! message.isSysEx() || message.getSysExDataSize() < 4)
        return false;
    const auto* data = message.getSysExData();
    return data[0] == manufacturerId && data[1] == deviceId && data[2] == hello && data[3] == side;
}
} // namespace signalpatch::controller
