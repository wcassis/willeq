/*
 * EQT Name Utilities
 *
 * Utilities for converting EverQuest server names to human-readable format.
 * Server names use underscores and trailing numbers (e.g., "Guard_Hezlan000")
 * which should be displayed as "Guard Hezlan".
 */

#ifndef EQT_NAME_UTILS_H
#define EQT_NAME_UTILS_H

#include <string>
#include <algorithm>

namespace EQT {

// Convert server name format to human-readable display name
// "Guard_Hezlan000" -> "Guard Hezlan"
// "a_skeleton001" -> "a skeleton"
inline std::string toDisplayName(const std::string& serverName) {
    if (serverName.empty()) {
        return serverName;
    }

    std::string result = serverName;

    // Replace underscores with spaces
    std::replace(result.begin(), result.end(), '_', ' ');

    // Remove trailing digits (like "000", "001", "00", etc.)
    // Find where trailing digits start
    size_t endPos = result.length();
    while (endPos > 0 && std::isdigit(result[endPos - 1])) {
        --endPos;
    }

    // Only trim if we found trailing digits and there's still content left
    if (endPos < result.length() && endPos > 0) {
        result = result.substr(0, endPos);
    }

    // Trim trailing spaces that might result from "Name_000" -> "Name "
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

// Wide string version for UI components
inline std::wstring toDisplayNameW(const std::string& serverName) {
    std::string narrow = toDisplayName(serverName);
    return std::wstring(narrow.begin(), narrow.end());
}

} // namespace EQT

#endif // EQT_NAME_UTILS_H
