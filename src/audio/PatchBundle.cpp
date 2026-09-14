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
        auto* extra = node->getProperty ("extra").getDynamicObject();
        if (extra == nullptr)
            continue;
        for (const auto* key : assetKeys)
        {
            if (! extra->hasProperty (key))
                continue;
            const auto path = extra->getProperty (key).toString();
            if (path.isEmpty())
                continue;
            if (toRelative)
            {
                const juce::File file (path);
                if (juce::File::isAbsolutePath (path) && file.isAChildOf (baseDirectory))
                    extra->setProperty (key, file.getRelativePathFrom (baseDirectory).replaceCharacter ('\\', '/'));
            }
            else if (! juce::File::isAbsolutePath (path))
            {
                extra->setProperty (key, baseDirectory.getChildFile (path).getFullPathName());
            }
        }
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
    auto json = document.toJson();
    juce::StringArray missing;
    if (auto* rootObject = json.getDynamicObject())
        if (auto* nodes = rootObject->getProperty ("nodes").getArray())
            for (auto& nodeValue : *nodes)
            {
                auto* node = nodeValue.getDynamicObject();
                auto* extra = node != nullptr ? node->getProperty ("extra").getDynamicObject() : nullptr;
                if (extra == nullptr)
                    continue;
                for (const auto* key : assetKeys)
                {
                    if (! extra->hasProperty (key))
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
