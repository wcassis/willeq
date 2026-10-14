#pragma once

#include <string>

namespace eqt {
class HotbarModel;

// Official hotbutton assignments live in the character INI's [HotButtons],
// separate from UI_<character>_<server>.ini (window preferences).
std::string HotbarCacheName(const std::string& character,
                            const std::string& server);
enum class HotbarLoadResult { Missing, Loaded, Error };
HotbarLoadResult LoadCharacterHotbar(const std::string& character,
                                    const std::string& server, HotbarModel& model);
bool SaveCharacterHotbar(const std::string& character, const std::string& server,
                         const HotbarModel& model);
} // namespace eqt
