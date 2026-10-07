#include "SettingsStore.h"

#include <Preferences.h>

namespace
{
const char* const kNamespace = "signalpatch";
const char* const kKey = "settings";
} // namespace

namespace settings_store
{
c3::Settings load()
{
    c3::Settings settings;
    Preferences preferences;
    if (! preferences.begin (kNamespace, true))
        return settings; // nothing stored yet
    uint8_t blob[64];
    const size_t length = preferences.getBytesLength (kKey);
    if (length > 0 && length <= sizeof (blob) && preferences.getBytes (kKey, blob, length) == length)
        settings.fromBlob (blob, length);
    preferences.end();
    return settings;
}

void save (const c3::Settings& settings)
{
    uint8_t blob[c3::Settings::kBlobSize];
    settings.toBlob (blob);
    Preferences preferences;
    if (! preferences.begin (kNamespace, false))
        return;
    preferences.putBytes (kKey, blob, sizeof (blob));
    preferences.end();
}
} // namespace settings_store
