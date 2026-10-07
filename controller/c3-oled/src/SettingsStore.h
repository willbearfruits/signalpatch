#pragma once

// Settings in NVS flash (Preferences), as the blob c3::Settings defines.

#include "core/Settings.h"

namespace settings_store
{
/** Loads the stored settings; defaults (clamped) when nothing valid is stored. */
c3::Settings load();
void save (const c3::Settings& settings);
} // namespace settings_store
