#pragma once

#include "Graph.h"

#include <unordered_map>

// Portable projects. A patch file stores asset paths (NAM models, cab
// impulses) relative to its own folder when they live under it, absolute
// otherwise, so a folder holding the patch plus assets/ already travels.
// exportBundle builds such a folder from the live document and zips it;
// extractBundle unpacks one. Headless: no engine, no UI, fully testable.
namespace signalpatch::bundle
{
/** Rewrites every asset path in a patch JSON tree: to paths relative to
    baseDirectory (when inside it) or back to absolute. */
void rebaseAssetPaths (juce::var& root, const juce::File& baseDirectory, bool toRelative);

/** Writes <name>.zip containing <name>/<name>.signalpatch + assets/. Fails
    (after writing) if referenced assets were missing on disk. */
juce::Result exportBundle (const PatchDocument& document, const juce::File& zipFile);

/** Writes each node's recorded audio (loops, tapes, samples) as a WAV under
    <patch folder>/assets/audio/ and returns the patch JSON with an "audio"
    entry per node pointing at it (relative). Nodes whose audio is unchanged
    since `alreadySaved[id]` keep their existing file. */
juce::var toJsonWithAudio (const PatchDocument& document, const juce::File& patchFile,
                           std::unordered_map<NodeId, juce::uint32>& alreadySaved);

/** Loads the WAVs an "audio" entry names into the matching nodes. */
void loadAudioContent (PatchDocument& document, const juce::var& json, const juce::File& patchDirectory);

/** Unzips a bundle under destinationRoot and returns its patch file. */
juce::Result extractBundle (const juce::File& zipFile, const juce::File& destinationRoot, juce::File& patchFileOut);
} // namespace signalpatch::bundle
