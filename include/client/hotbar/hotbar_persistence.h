#pragma once

#include <string>

namespace eqt {
class HotbarModel;

// JSON twin of UI_<character>_<server>.ini. The official [HotButtons] values
// encode source-specific references, not this model's typed IDs; writing them
// into the INI would also race the renderer's UiPreferences writer.
std::string HotbarCacheName(const std::string& character,
                            const std::string& server);
bool LoadCharacterHotbar(const std::string& character, const std::string& server,
                         HotbarModel& model);
bool SaveCharacterHotbar(const std::string& character, const std::string& server,
                         const HotbarModel& model);
} // namespace eqt
