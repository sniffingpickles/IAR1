#pragma once

#include "../core/settings.hpp"

namespace iar {

// LoadSettings reads persisted settings from the OBS module config dir
// (settings.json). Returns true if a settings file existed; `out` is left at
// defaults when it does not.
bool LoadSettings(Settings &out);

// SaveSettings persists settings to the OBS module config dir, creating it if
// needed.
void SaveSettings(const Settings &s);

} // namespace iar
