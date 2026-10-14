#pragma once

#include <cctype>
#include <string>

namespace eqt {

// Shared by UI preferences and hotbar persistence. Packet/config values must
// not be allowed to escape the per-character cache directory.
inline std::string UiIdentityPart(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (char c : input) {
        const auto u = static_cast<unsigned char>(c);
        result.push_back(std::isalnum(u) || c == '_' || c == '-' ? c : '_');
    }
    return result.empty() ? "unknown" : result;
}

inline std::string UiIdentityFileStem(const std::string& character,
                                      const std::string& server) {
    return "UI_" + UiIdentityPart(character) + "_" + UiIdentityPart(server);
}

} // namespace eqt
