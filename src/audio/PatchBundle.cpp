#include "PatchBundle.h"

namespace signalpatch::bundle
{
namespace
{
    constexpr const char* assetKeys[] { "model", "ir", "irB" };

    juce::String assetSubfolder (const juce::String& key)
    {
        return key == "model" ? "models" : "irs";
    }
} // namespace

void rebaseAssetPaths (juce::var& root, const juce::File& baseDirectory, bool toRelative)
{
    auto* rootObject = root.getDynamicObject();
    if (rootObject == nullptr || baseDirectory == juce::File())
        return;
    auto* nodes = rootObject->getProperty ("nodes").getArray();
    if (nodes == nullptr)
        return;
    for (auto& nodeValue : *nodes)
    {
        auto* node = nodeValue.getDynamicObject();
        if (node == nullptr)
            continue;
        auto rebase = [&] (juce::DynamicObject* object, const char* key)
        {
            if (object == nullptr || ! object->hasProperty (key))
                return;
            const auto path = object->getProperty (key).toString();
            if (path.isEmpty())
                return;
            if (toRelative)
            {
                const juce::File file (path);
                if (juce::File::isAbsolutePath (path) && file.isAChildOf (baseDirectory))
                    object->setProperty (key, file.getRelativePathFrom (baseDirectory).replaceCharacter ('\\', '/'));
            }
            else if (! juce::File::isAbsolutePath (path))
                object->setProperty (key, baseDirectory.getChildFile (path).getFullPathName());
        };
        rebase (node, "audio");
        auto* extra = node->getProperty ("extra").getDynamicObject();
        for (const auto* key : assetKeys)
            rebase (extra, key);
    }
}

juce::var toJsonWithAudio (const PatchDocument& document, const juce::File& patchFile,
                           std::unordered_map<NodeId, juce::uint32>& alreadySaved)
{
    auto json = document.toJson();
    auto* root = json.getDynamicObject();
    auto* nodes = root != nullptr ? root->getProperty ("nodes").getArray() : nullptr;
    if (nodes == nullptr)
        return json;
    const auto folder = patchFile.getParentDirectory().getChildFile ("assets").getChildFile ("audio");
    const auto stem = patchFile.getFileNameWithoutExtension();
    for (auto& nodeValue : *nodes)
    {
        auto* object = nodeValue.getDynamicObject();
        if (object == nullptr)
            continue;
        const auto id = static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("id")));
        const auto* model = document.findNode (id);
        if (model == nullptr || ! model->processor->hasAudioContent())
            continue;
        const auto file = folder.getChildFile (stem + "-" + juce::String (id) + ".wav");
        const auto version = model->processor->audioContentVersion();
        const auto saved = alreadySaved.find (id);
        const bool fresh = saved == alreadySaved.end() || saved->second != version || ! file.existsAsFile();
        if (fresh)
        {
            const auto audio = model->processor->exportAudioContent();
            if (audio.getNumSamples() == 0)
                continue;
            folder.createDirectory();
            // Written next to the old take and moved over it only when complete:
            // a crash or a full disk mid-write keeps the previous recording.
            const auto partial = file.getSiblingFile (file.getFileName() + ".part");
            partial.deleteFile();
            bool complete = false;
            {
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                    new juce::FileOutputStream (partial), document.getSampleRate(), static_cast<unsigned int> (audio.getNumChannels()), 32, {}, 0));
                if (writer != nullptr)
                    complete = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()) && writer->flush();
            }
            if (! complete || ! partial.moveFileTo (file))
            {
                partial.deleteFile();
                if (! file.existsAsFile())
                    continue; // nothing usable to point the patch at
            }
            else
                alreadySaved[id] = version;
        }
        object->setProperty ("audio", file.getFullPathName());
    }
    rebaseAssetPaths (json, patchFile.getParentDirectory(), true);
    return json;
}

void loadAudioContent (PatchDocument& document, const juce::var& json, const juce::File& patchDirectory)
{
    const auto* root = json.getDynamicObject();
    const auto* nodes = root != nullptr ? root->getProperty ("nodes").getArray() : nullptr;
    if (nodes == nullptr)
        return;
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    for (const auto& nodeValue : *nodes)
    {
        const auto* object = nodeValue.getDynamicObject();
        if (object == nullptr || ! object->hasProperty ("audio"))
            continue;
        auto path = object->getProperty ("audio").toString();
        if (! juce::File::isAbsolutePath (path))
            path = patchDirectory.getChildFile (path).getFullPathName();
        const auto id = static_cast<NodeId> (static_cast<juce::int64> (object->getProperty ("id")));
        auto* model = document.findNode (id);
        if (model == nullptr)
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (juce::File (path)));
        if (reader == nullptr)
            continue;
        juce::AudioBuffer<float> audio (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        // A take recorded at another rate is resampled so it keeps its speed and pitch.
        const auto targetRate = document.getSampleRate();
        if (targetRate > 0.0 && reader->sampleRate > 0.0 && std::abs (reader->sampleRate - targetRate) > 0.5 && audio.getNumSamples() > 0)
        {
            const auto ratio = reader->sampleRate / targetRate;
            const auto outLength = static_cast<int> (std::floor (audio.getNumSamples() / ratio));
            juce::AudioBuffer<float> converted (audio.getNumChannels(), juce::jmax (1, outLength));
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process (ratio, audio.getReadPointer (channel), converted.getWritePointer (channel), converted.getNumSamples(),
                                      audio.getNumSamples(), 0);
            }
            audio = std::move (converted);
        }
        model->processor->importAudioContent (audio);
    }
}

juce::Result exportBundle (const PatchDocument& document, const juce::File& zipFile)
{
    if (zipFile == juce::File())
        return juce::Result::fail ("No file selected.");
    auto name = zipFile.getFileNameWithoutExtension();
    if (name.endsWithIgnoreCase (".signalpatch"))
        name = name.dropLastCharacters (12);
    if (name.isEmpty())
        name = "patch";

    const auto staging = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("signalpatch-export-" + juce::Uuid().toString());
    const auto folder = staging.getChildFile (name);
    if (! folder.createDirectory())
        return juce::Result::fail ("Could not create a temporary folder.");

    // Copy every referenced asset into assets/<models|irs>/ and point the
    // saved patch at the copies with relative paths.
    std::unordered_map<NodeId, juce::uint32> savedVersions;
    auto json = toJsonWithAudio (document, folder.getChildFile (name + ".signalpatch"), savedVersions);
    rebaseAssetPaths (json, folder, false); // absolute again so the copy loop below sees real files
    juce::StringArray missing;
    if (auto* rootObject = json.getDynamicObject())
        if (auto* nodes = rootObject->getProperty ("nodes").getArray())
            for (auto& nodeValue : *nodes)
            {
                auto* node = nodeValue.getDynamicObject();
                if (node == nullptr)
                    continue;
                auto* extra = node->getProperty ("extra").getDynamicObject();
                if (node->hasProperty ("audio"))
                {
                    const juce::File source (node->getProperty ("audio").toString());
                    if (source.existsAsFile())
                    {
                        const auto assetDir = folder.getChildFile ("assets").getChildFile ("audio");
                        assetDir.createDirectory();
                        const auto target = assetDir.getChildFile (source.getFileName());
                        if (! target.existsAsFile())
                            source.copyFileTo (target);
                        node->setProperty ("audio", target.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
                    }
                }
                for (const auto* key : assetKeys)
                {
                    if (extra == nullptr || ! extra->hasProperty (key))
                        continue;
                    const juce::File source (extra->getProperty (key).toString());
                    if (! source.existsAsFile())
                    {
                        missing.add (source.getFileName());
                        continue;
                    }
                    const auto assetDir = folder.getChildFile ("assets").getChildFile (assetSubfolder (key));
                    assetDir.createDirectory();
                    auto target = assetDir.getChildFile (source.getFileName());
                    for (int suffix = 2; target.existsAsFile() && ! target.hasIdenticalContentTo (source); ++suffix)
                        target = assetDir.getChildFile (source.getFileNameWithoutExtension() + "-" + juce::String (suffix)
                                                        + source.getFileExtension());
                    if (! target.existsAsFile() && ! source.copyFileTo (target))
                        return juce::Result::fail ("Could not copy " + source.getFileName());
                    extra->setProperty (key, target.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
                }
            }
    const auto patchFile = folder.getChildFile (name + ".signalpatch");
    if (! patchFile.replaceWithText (juce::JSON::toString (json, true)))
        return juce::Result::fail ("Could not write the bundled patch.");

    juce::ZipFile::Builder builder;
    for (const auto& entry : staging.findChildFiles (juce::File::findFiles, true))
        builder.addFile (entry, 6, entry.getRelativePathFrom (staging).replaceCharacter ('\\', '/'));
    zipFile.deleteFile();
    {
        juce::FileOutputStream output (zipFile);
        if (! output.openedOk() || ! builder.writeToStream (output, nullptr))
        {
            staging.deleteRecursively();
            return juce::Result::fail ("Could not write " + zipFile.getFullPathName());
        }
    }
    staging.deleteRecursively();
    if (! missing.isEmpty())
        return juce::Result::fail ("Exported, but these assets were missing on disk: " + missing.joinIntoString (", "));
    return juce::Result::ok();
}

juce::Result extractBundle (const juce::File& zipFile, const juce::File& destinationRoot, juce::File& patchFileOut)
{
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0)
        return juce::Result::fail ("The zip is empty or unreadable.");
    if (! destinationRoot.createDirectory())
        return juce::Result::fail ("Could not create " + destinationRoot.getFullPathName());
    const auto result = zip.uncompressTo (destinationRoot, true);
    if (result.failed())
        return result;
    // Prefer the patch that shares the bundle's name, else the first found.
    juce::File best;
    for (const auto& file : destinationRoot.findChildFiles (juce::File::findFiles, true, "*.signalpatch"))
        if (best == juce::File() || file.getFileNameWithoutExtension() == zipFile.getFileNameWithoutExtension())
            best = file;
    if (best == juce::File())
        return juce::Result::fail ("No .signalpatch file inside the zip.");
    patchFileOut = best;
    return juce::Result::ok();
}
} // namespace signalpatch::bundle
