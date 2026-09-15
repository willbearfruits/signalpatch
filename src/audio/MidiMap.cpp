#include "MidiMap.h"

namespace signalpatch
{
namespace
{
    const char* sourceKey (MidiMapping::Source source)
    {
        switch (source)
        {
            case MidiMapping::Source::controlChange: return "cc";
            case MidiMapping::Source::note:          return "note";
            case MidiMapping::Source::programChange: return "program";
        }
        return "cc";
    }

    const char* targetKey (MidiMapping::Target target)
    {
        switch (target)
        {
            case MidiMapping::Target::parameter:   return "parameter";
            case MidiMapping::Target::bypass:      return "bypass";
            case MidiMapping::Target::command:     return "command";
            case MidiMapping::Target::slot:        return "slot";
            case MidiMapping::Target::groupBypass: return "group";
        }
        return "parameter";
    }
} // namespace

juce::var midiMappingsToJson (const std::vector<MidiMapping>& mappings)
{
    juce::Array<juce::var> values;
    for (const auto& mapping : mappings)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("source", sourceKey (mapping.source));
        object->setProperty ("channel", mapping.channel);
        object->setProperty ("number", mapping.number);
        object->setProperty ("target", targetKey (mapping.target));
        object->setProperty ("node", static_cast<juce::int64> (mapping.node));
        object->setProperty ("parameter", mapping.parameter);
        object->setProperty ("group", mapping.groupId);
        object->setProperty ("slot", mapping.slot);
        object->setProperty ("command", mapping.command);
        if (mapping.relative)
            object->setProperty ("relative", true);
        values.add (juce::var (object.release()));
    }
    return values;
}

std::vector<MidiMapping> midiMappingsFromJson (const juce::var& value)
{
    std::vector<MidiMapping> mappings;
    const auto* array = value.getArray();
    if (array == nullptr)
        return mappings;
    for (const auto& entry : *array)
    {
        const auto* object = entry.getDynamicObject();
        if (object == nullptr)
            continue;
        MidiMapping mapping;
        const auto source = object->getProperty ("source").toString();
        mapping.source = source == "note" ? MidiMapping::Source::note
                       : source == "program" ? MidiMapping::Source::programChange
                                             : MidiMapping::Source::controlChange;
        const auto target = object->getProperty ("target").toString();
        mapping.target = target == "bypass" ? MidiMapping::Target::bypass
                       : target == "command" ? MidiMapping::Target::command
                       : target == "slot" ? MidiMapping::Target::slot
                       : target == "group" ? MidiMapping::Target::groupBypass
                                           : MidiMapping::Target::parameter;
        mapping.channel = static_cast<int> (object->getProperty ("channel"));
        mapping.number = static_cast<int> (object->getProperty ("number"));
        mapping.node = static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("node")));
        mapping.parameter = static_cast<int> (object->getProperty ("parameter"));
        mapping.groupId = static_cast<int> (object->getProperty ("group"));
        mapping.slot = static_cast<int> (object->getProperty ("slot"));
        mapping.command = object->getProperty ("command").toString();
        mapping.relative = static_cast<bool> (object->getProperty ("relative"));
        mappings.push_back (std::move (mapping));
    }
    return mappings;
}
} // namespace signalpatch
