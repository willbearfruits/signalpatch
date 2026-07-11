#pragma once

#include "Graph.h"

namespace signalpatch
{
std::shared_ptr<DspNode> createNodeProcessor (NodeKind kind);
std::shared_ptr<DspNode> createHardwareInputProcessor (const juce::StringArray& channelNames,
                                                       const std::vector<int>& callbackChannels = {});
std::shared_ptr<DspNode> createHardwareOutputProcessor (const juce::StringArray& channelNames,
                                                        const std::vector<int>& callbackChannels = {});
} // namespace signalpatch
