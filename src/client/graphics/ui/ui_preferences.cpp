/*
 * Per-character UI preferences -- see include/client/graphics/ui/ui_preferences.h
 * for the shape, the provenance and why the file lands where it does (T432,
 * board B275).
 *
 * Everything here is free functions over plain structs and std::string: no
 * device, no Irrlicht call, no thread. That is deliberate -- it is what lets
 * tests/test_ui_preferences.cpp exercise the whole read/write path, including
 * the on-disk round trip, with no client at all.
 */

#include "client/graphics/ui/ui_preferences.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "client/data_paths.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

namespace {

// One sanitised filename part. The character name arrives from a server
// packet and the server name from user config, so neither may be trusted to
// stay inside the cache directory: anything outside [A-Za-z0-9_-] becomes
// '_', and an empty result becomes "unknown".
std::string SanitizePart(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) return "unknown";
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The official client writes Show=0 / Show=1. Accept those plus the
// true/false spelling its other keys use (Fades=true), so a hand-edited file
// behaves the way a reader would expect.
bool ParseBool(const std::string& v, bool fallback) {
    const std::string t = Trim(v);
    if (t == "0" || t == "false" || t == "FALSE" || t == "False") return false;
    if (t == "1" || t == "true" || t == "TRUE" || t == "True") return true;
    return fallback;
}

// Section name -> the UiPreferences member its Show= key drives. Keeping the
// table in one place is what makes parse and serialise incapable of drifting:
// both walk it.
struct WindowRow {
    const char* section;
    bool UiPreferences::*field;
};

const WindowRow kWindows[] = {
    {"InventoryWindow", &UiPreferences::inventoryWindow},
    {"SpellBookWnd", &UiPreferences::spellBookWnd},
    {"SkillsWindow", &UiPreferences::skillsWindow},
    {"GroupWindow", &UiPreferences::groupWindow},
    {"PetInfoWindow", &UiPreferences::petInfoWindow},
    {"BuffWindow", &UiPreferences::buffWindow},
    {"CombatAbilityWnd", &UiPreferences::combatAbilityWnd},
};

}  // namespace

UiPreferences CollectUiPreferences(const UiPreferencesBinding& b, int cameraMode) {
    UiPreferences p;
    if (b.inventory) {
        p.inventoryWindow = (b.inventory->activePopup == PopupType::Inventory);
    }
    if (b.spellbook) p.spellBookWnd = b.spellbook->isOpen;
    if (b.skills) p.skillsWindow = b.skills->isOpen;
    if (b.group) p.groupWindow = b.group->isOpen;
    if (b.pet) p.petInfoWindow = b.pet->isOpen;
    if (b.buff) p.buffWindow = b.buff->isOpen;
    if (b.combatAbility) p.combatAbilityWnd = b.combatAbility->isOpen;
    p.cameraMode = cameraMode;
    return p;
}

void ApplyUiPreferences(const UiPreferences& p, const UiPreferencesBinding& b) {
    // The three centre popups are mutually exclusive live (toggleSkillsWindow()
    // and the /newbook handler enforce it), so a file naming two of them open
    // -- only reachable by hand-editing -- is resolved here with a fixed
    // priority rather than left to paint both. Skills is the one whose own
    // toggle closes the other two, so it wins.
    bool skills = p.skillsWindow;
    bool spellbook = p.spellBookWnd && !skills;
    bool inventory = p.inventoryWindow && !skills && !spellbook;

    if (b.skills) b.skills->isOpen = skills;
    if (b.spellbook) b.spellbook->isOpen = spellbook;
    if (b.inventory) {
        if (inventory) {
            b.inventory->activePopup = PopupType::Inventory;
        } else if (b.inventory->activePopup == PopupType::Inventory) {
            // Only ever clear OUR popup: a vendor or bank window open at
            // apply time is not this file's business.
            b.inventory->activePopup = PopupType::None;
        }
    }
    if (b.group) b.group->isOpen = p.groupWindow;
    if (b.pet) b.pet->isOpen = p.petInfoWindow;
    if (b.buff) b.buff->isOpen = p.buffWindow;
    if (b.combatAbility) b.combatAbility->isOpen = p.combatAbilityWnd;
}

std::string UiPreferencesFileName(const std::string& character,
                                  const std::string& server) {
    return "UI_" + SanitizePart(character) + "_" + SanitizePart(server) + ".ini";
}

std::string UiPreferencesCacheName(const std::string& character,
                                   const std::string& server) {
    return "ui/" + UiPreferencesFileName(character, server);
}

UiPreferences ParseUiPreferences(const std::string& text,
                                 const UiPreferences& defaults) {
    UiPreferences p = defaults;
    std::istringstream in(text);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        // Strip a trailing CR: the official files are CRLF.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(t.substr(0, eq));
        const std::string value = t.substr(eq + 1);

        if (section == "Camera") {
            if (key == "Mode") {
                try {
                    const int m = std::stoi(Trim(value));
                    // Clamp to the enumerators PlayerCameraComponent knows;
                    // an out-of-range value in a hand-edited file must not
                    // become an out-of-range CameraMode cast.
                    if (m >= 0 && m <= 2) p.cameraMode = m;
                } catch (...) {
                    // Not a number -- keep the default, exactly as an absent
                    // key would.
                }
            }
            continue;
        }
        if (key != "Show") continue;
        for (const auto& row : kWindows) {
            if (section == row.section) {
                p.*(row.field) = ParseBool(value, p.*(row.field));
                break;
            }
        }
    }
    return p;
}

std::string SerializeUiPreferences(const UiPreferences& p) {
    std::string out;
    out += "; WillEQ per-character UI state. Section and key names are the\n";
    out += "; official client's own (UI_<char>_<server>.ini); fable persists\n";
    out += "; only the user-changeable open/closed state and camera mode.\n";
    for (const auto& row : kWindows) {
        out += "[";
        out += row.section;
        out += "]\nShow=";
        out += (p.*(row.field)) ? "1" : "0";
        out += "\n";
    }
    out += "[Camera]\nMode=" + std::to_string(p.cameraMode) + "\n";
    return out;
}

bool LoadUiPreferences(const std::string& character, const std::string& server,
                       UiPreferences& out) {
    const std::string path =
        eqt::ResolveCachePathForRead(UiPreferencesCacheName(character, server));
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "UI preferences: no file for '{}' on '{}' -- defaults",
                  character, server);
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = ParseUiPreferences(buf.str(), out);
    LOG_INFO(MOD_UI, "UI preferences loaded from {}", path);
    return true;
}

bool SaveUiPreferences(const std::string& character, const std::string& server,
                       const UiPreferences& p) {
    const std::string path =
        eqt::ResolveCachePathForWrite(UiPreferencesCacheName(character, server));
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "UI preferences: no writable cache directory");
        return false;
    }
    // Write a temp file beside the target and rename, so an interrupted save
    // never leaves a half-written file that the next start would parse.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream outFile(tmp, std::ios::binary | std::ios::trunc);
        if (!outFile) {
            LOG_WARN(MOD_UI, "UI preferences: could not write {}", tmp);
            return false;
        }
        outFile << SerializeUiPreferences(p);
        if (!outFile) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        LOG_WARN(MOD_UI, "UI preferences: could not rename {} -> {}", tmp, path);
        std::remove(tmp.c_str());
        return false;
    }
    LOG_DEBUG(MOD_UI, "UI preferences saved to {}", path);
    return true;
}

}  // namespace Graphics
}  // namespace EQT
