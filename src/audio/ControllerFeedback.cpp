#include "ControllerFeedback.h"

#include <algorithm>
#include <initializer_list>

namespace signalpatch::controller
{
namespace
{
    constexpr int switchLabelLength = 8;
    constexpr int rigNameLength = 16;
    constexpr int controlLabelLength = 12;
    constexpr int valueTextLength = 10;

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

    juce::uint8 byte7 (int value) noexcept
    {
        return static_cast<juce::uint8> (juce::jlimit (0, 127, value));
    }

    juce::MidiMessage sysex (std::initializer_list<juce::uint8> header, const juce::String& text = {})
    {
        std::vector<juce::uint8> bytes { manufacturerId, deviceId };
        bytes.insert (bytes.end(), header.begin(), header.end());
        for (const auto c : text)
            bytes.push_back (static_cast<juce::uint8> (c & 0x7F));
        return juce::MidiMessage::createSysExMessage (bytes.data(), static_cast<int> (bytes.size()));
    }

    /** Header bytes 7D 53 <command> present; the payload follows at data[3]. */
    bool hasHeader (const juce::MidiMessage& message, juce::uint8 command) noexcept
    {
        if (! message.isSysEx() || message.getSysExDataSize() < 3)
            return false;
        const auto* data = message.getSysExData();
        return data[0] == manufacturerId && data[1] == deviceId && data[2] == command;
    }

    juce::String shortName (const PatchDocument& document, NodeId id)
    {
        const auto* node = document.findNode (id);
        return node == nullptr ? juce::String() : node->processor->getName();
    }

    const DspParameter* findParameter (const PatchDocument& document, NodeId id, int index)
    {
        const auto* node = document.findNode (id);
        if (node == nullptr || ! juce::isPositiveAndBelow (index, node->processor->getNumParameters()))
            return nullptr;
        return &node->processor->getParameter (index);
    }

    // A copy of formatParameterValue in src/v2/Palette.h (the engine does not
    // include UI headers): the controller prints a value the way the app does.
    juce::String formatValue (const DspParameter& parameter, float value)
    {
        const auto& unit = parameter.unit;
        if (unit == "Hz")
            return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                    : juce::String (value, value < 10.0f ? 2 : 0) + " Hz";
        if (unit == "ms")
            return juce::String (value, value >= 100.0f ? 0 : 1) + " ms";
        if (unit == "dB")
            return juce::String (value, 1) + " dB";
        if (unit == "%")
            return juce::String (value, 1) + "%";
        if (unit == ":1")
            return juce::String (value, 1) + ":1";
        return juce::String (value, 2);
    }

    ControlState knobState (const DspParameter& parameter)
    {
        ControlState state;
        state.label = ascii7 (parameter.name, controlLabelLength);
        state.valueText = ascii7 (formatValue (parameter, parameter.getValue()), valueTextLength);
        state.position = juce::jlimit (0, 127, juce::roundToInt (parameter.getNormalisedValue() * 127.0f));
        return state;
    }

    /** "module-knob-2" -> 2; 0 when the action is not a module knob. */
    int moduleKnobNumber (const juce::String& action)
    {
        if (! action.startsWith ("module-knob-"))
            return 0;
        const auto number = action.fromLastOccurrenceOf ("-", false, false).getIntValue();
        return number > 0 ? number : 0;
    }

    juce::String viewSwitchLabel (const juce::String& action)
    {
        if (action == "mute")      return "MUTE";
        if (action == "next-slot") return "SLOT +";
        if (action == "prev-slot") return "SLOT -";
        if (action == "next-knob") return "KNOB +";
        if (action == "knob")      return "KNOB";
        if (const auto n = moduleKnobNumber (action); n > 0)
            return "KNOB " + juce::String (n);
        return action.toUpperCase();
    }

    ControlState viewControlState (const PatchDocument& document, const MidiMapping& mapping, const ViewContext& view)
    {
        const auto& action = mapping.command;
        if (action == "knob")
        {
            if (const auto* parameter = findParameter (document, view.touchedNode, view.touchedParameter))
                return knobState (*parameter);
            return { "Knob", {}, -1 };
        }
        if (const auto n = moduleKnobNumber (action); n > 0)
        {
            if (const auto* parameter = findParameter (document, view.selectedNode, n - 1))
                return knobState (*parameter);
            return { "Knob " + juce::String (n), {}, -1 };
        }
        if (action == "mute")
            return { "Mute", view.muted ? "On" : "Off", view.muted ? 127 : 0 };
        if (action == "next-knob") return { "Next knob", {}, -1 };
        if (action == "next-slot") return { "Next slot", {}, -1 };
        if (action == "prev-slot") return { "Prev slot", {}, -1 };
        return { ascii7 (action, controlLabelLength), {}, -1 };
    }

    ControlState controlStateFor (const PatchDocument& document, const MidiMapping& mapping, const ViewContext& view)
    {
        switch (mapping.target)
        {
            case MidiMapping::Target::parameter:
                if (const auto* parameter = findParameter (document, mapping.node, mapping.parameter))
                    return knobState (*parameter);
                return {};
            case MidiMapping::Target::bypass:
                if (const auto* node = document.findNode (mapping.node))
                {
                    const bool on = ! node->processor->isBypassed();
                    return { ascii7 (node->processor->getName(), controlLabelLength), on ? "On" : "Off", on ? 127 : 0 };
                }
                return {};
            case MidiMapping::Target::command:
            case MidiMapping::Target::slot:
            case MidiMapping::Target::groupBypass:
                return { ascii7 (labelFor (document, mapping), controlLabelLength), {}, -1 };
            case MidiMapping::Target::view:
                return viewControlState (document, mapping, view);
        }
        return {};
    }

    SwitchState switchStateFor (const PatchDocument& document, const MidiMapping& mapping, int activeSlot, const ViewContext& view)
    {
        SwitchState sw;
        sw.label = ascii7 (labelFor (document, mapping), switchLabelLength);
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
            case MidiMapping::Target::view:
                sw.led = mapping.command == "mute" && view.muted ? 1 : 0;
                break;
            case MidiMapping::Target::parameter:
                break;
        }
        return sw;
    }

    /** The first patch mapping on the control, else the first app-wide one. */
    const MidiMapping* findMapping (const std::vector<MidiMapping>& patch, const std::vector<MidiMapping>& global,
                                    MidiMapping::Source source, int channel, int number)
    {
        for (const auto* list : { &patch, &global })
            for (const auto& mapping : *list)
                if (mapping.matches (source, channel, number))
                    return &mapping;
        return nullptr;
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
            const auto* parameter = findParameter (document, mapping.node, mapping.parameter);
            return parameter == nullptr ? juce::String() : parameter->name;
        }
        case MidiMapping::Target::view:      return viewSwitchLabel (mapping.command);
    }
    return {};
}

State computeState (const PatchDocument& document, const std::vector<MidiMapping>& globalMappings, int activeSlot,
                    const juce::String& rig, const ViewContext& view, const std::vector<int>& controlNumbers,
                    const Layout& layout)
{
    State state;
    state.activeSlot = activeSlot;
    state.rig = ascii7 (rig, rigNameLength);
    state.muted = view.muted;
    const auto& patchMappings = document.getMidiMappings();
    for (int index = 0; index < switchCount; ++index)
    {
        const auto note = layout.switchNotes[static_cast<std::size_t> (index)];
        if (const auto* mapping = findMapping (patchMappings, globalMappings, MidiMapping::Source::note, layout.channel, note))
            state.switches[static_cast<std::size_t> (index)] = switchStateFor (document, *mapping, activeSlot, view);
    }
    for (const auto number : controlNumbers)
    {
        if (! juce::isPositiveAndBelow (number, 128))
            continue;
        const auto* mapping = findMapping (patchMappings, globalMappings, MidiMapping::Source::controlChange, layout.channel, number);
        state.controls[number] = mapping == nullptr ? ControlState {} : controlStateFor (document, *mapping, view);
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
            out.push_back (sysex ({ switchLed, static_cast<juce::uint8> (index), byte7 (after.led) }));
        if (full || before.label != after.label)
            out.push_back (sysex ({ switchLabel, static_cast<juce::uint8> (index) }, ascii7 (after.label, switchLabelLength)));
    }
    if (full || previous.activeSlot != next.activeSlot)
        for (int slot = 0; slot < slotCount; ++slot)
            if (full || slot == previous.activeSlot || slot == next.activeSlot)
                out.push_back (sysex ({ slotActive, static_cast<juce::uint8> (slot), static_cast<juce::uint8> (slot == next.activeSlot ? 1 : 0) }));
    if (full || previous.rig != next.rig)
        out.push_back (sysex ({ rigName }, ascii7 (next.rig, rigNameLength)));

    // Controls the controller no longer reports are cleared; the rest are sent when
    // they changed. A label always goes before its value, and a control whose value
    // became unknown gets its label again (the controller drops the old value on a label).
    for (const auto& [number, before] : previous.controls)
        if (next.controls.find (number) == next.controls.end() && (full || before.label.isNotEmpty() || before.position >= 0))
            out.push_back (sysex ({ controlLabel, byte7 (number) }));
    for (const auto& [number, after] : next.controls)
    {
        const auto found = previous.controls.find (number);
        const bool known = found != previous.controls.end();
        const ControlState before = known ? found->second : ControlState {};
        const bool labelChanged = full || ! known || before.label != after.label || (before.position >= 0 && after.position < 0);
        if (labelChanged)
            out.push_back (sysex ({ controlLabel, byte7 (number) }, ascii7 (after.label, controlLabelLength)));
        if (after.position >= 0 && (labelChanged || before.position != after.position || before.valueText != after.valueText))
            out.push_back (sysex ({ controlValue, byte7 (number), byte7 (after.position) }, ascii7 (after.valueText, valueTextLength)));
    }
    if (full || previous.muted != next.muted)
        out.push_back (sysex ({ flags, static_cast<juce::uint8> (next.muted ? 1 : 0) }));
    return out;
}

const std::vector<int>& defaultControlNumbers()
{
    static const std::vector<int> numbers { 4, 11, 12, 13, 16, 17, 18, 20, 21, 22, 23 };
    return numbers;
}

std::vector<int> controlNumbersFromSettings (const std::map<int, int>& settings)
{
    std::vector<int> numbers;
    for (int key = 0x01; key <= 0x07; ++key)
        if (const auto found = settings.find (key); found != settings.end()
            && juce::isPositiveAndBelow (found->second, 128)
            && std::find (numbers.begin(), numbers.end(), found->second) == numbers.end())
            numbers.push_back (found->second);
    return numbers;
}

Layout layoutFromSettings (const std::map<int, int>& settings)
{
    Layout layout;
    if (const auto found = settings.find (0x00); found != settings.end() && found->second >= 1 && found->second <= 16)
        layout.channel = found->second;
    for (int index = 0; index < 4; ++index)
        if (const auto found = settings.find (0x08 + index); found != settings.end() && juce::isPositiveAndBelow (found->second, 128))
            layout.switchNotes[static_cast<std::size_t> (index)] = found->second;
    return layout;
}

std::optional<HelloInfo> parseHello (const juce::MidiMessage& message)
{
    if (! hasHeader (message, hello) || message.getSysExDataSize() < 4)
        return std::nullopt;
    const auto* data = message.getSysExData();
    const auto size = message.getSysExDataSize();
    HelloInfo info;
    info.side = data[3];
    if (size >= 5) info.model = data[4];
    if (size >= 6) info.firmwareMajor = data[5];
    if (size >= 7) info.firmwareMinor = data[6];
    return info;
}

juce::MidiMessage helloMessage (HelloSide side)
{
    return sysex ({ hello, side });
}

bool isHello (const juce::MidiMessage& message, HelloSide side) noexcept
{
    if (! hasHeader (message, hello) || message.getSysExDataSize() < 4)
        return false;
    return message.getSysExData()[3] == side;
}

const std::vector<SettingInfo>& handheldSettings()
{
    static const std::vector<const char*> onOff { "Off", "On" };
    static const std::vector<SettingInfo> settings {
        { 0x00, "MIDI channel",           10,  1,  16, {} },
        { 0x01, "Pot 1 CC",               16,  0, 119, {} },
        { 0x02, "Pot 2 CC",               17,  0, 119, {} },
        { 0x03, "Pot 3 CC",               18,  0, 119, {} },
        { 0x04, "Expression CC",          11,  0, 119, {} },
        { 0x05, "Gyro tilt CC",           12,  0, 119, {} },
        { 0x06, "Gyro roll CC",           13,  0, 119, {} },
        { 0x07, "Encoder CC",             20,  0, 119, {} },
        { 0x08, "Button 1 note",          60,  0, 127, {} },
        { 0x09, "Button 2 note",          61,  0, 127, {} },
        { 0x0A, "Button 3 note",          62,  0, 127, {} },
        { 0x0B, "Encoder push note",      63,  0, 127, {} },
        { 0x0C, "Expression input",        1,  0,   1, onOff },
        { 0x0D, "Expression reversed",     0,  0,   1, onOff },
        { 0x0E, "Gyro mode",               3,  0,   3, { "Off", "Always on", "While button 3 is held", "Long press on the encoder" } },
        { 0x0F, "Gyro range, degrees",    30,  5,  90, {} },
        { 0x10, "Gyro smoothing",          4,  0,  10, {} },
        { 0x11, "Pot dead band",           1,  0,   4, {} },
        { 0x12, "Encoder reversed",        0,  0,   1, onOff },
        { 0x13, "Encoder acceleration",    1,  0,   1, onOff },
        { 0x14, "Display brightness",    100,  0, 127, {} },
        { 0x15, "Display upside down",     0,  0,   1, onOff },
        { 0x16, "Display sleep, minutes",  0,  0,  60, {} },
        { 0x17, "Bluetooth name number",   0,  0,   9, {} },
    };
    return settings;
}

juce::MidiMessage settingsRequestMessage()
{
    return sysex ({ settingsRequest });
}

juce::MidiMessage settingsSetMessage (const std::vector<std::pair<int, int>>& changes)
{
    std::vector<juce::uint8> bytes { manufacturerId, deviceId, settingsSet };
    for (const auto& [key, value] : changes)
    {
        bytes.push_back (byte7 (key));
        bytes.push_back (byte7 (value));
    }
    return juce::MidiMessage::createSysExMessage (bytes.data(), static_cast<int> (bytes.size()));
}

juce::MidiMessage settingsResetMessage()
{
    return sysex ({ settingsReset });
}

juce::MidiMessage gyroZeroMessage()
{
    return sysex ({ gyroZero });
}

std::optional<std::map<int, int>> parseSettingsDump (const juce::MidiMessage& message)
{
    if (! hasHeader (message, settingsDump))
        return std::nullopt;
    const auto size = message.getSysExDataSize();
    if ((size - 3) % 2 != 0)
        return std::nullopt;
    const auto* data = message.getSysExData();
    std::map<int, int> settings;
    for (int index = 3; index + 1 < size; index += 2)
        settings[data[index] & 0x7F] = data[index + 1] & 0x7F;
    return settings;
}
} // namespace signalpatch::controller
