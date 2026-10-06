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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "client/data_paths.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/friends_panel.h"
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
    {"OptionsWindow", &UiPreferences::optionsWindow},   // T434
    {"FriendsWindow", &UiPreferences::friendsWindow},   // B897
};

// T434 -- the `[Options]` section. Same anti-drift device as kWindows: one
// table, walked by both the parse and the serialise, so the two cannot
// disagree about a key name. The key names are the oracle's own; see the
// flagged deviation in ui_preferences.h about which FILE the oracle keeps
// them in. Every value is a 0..20 slider step and is clamped on read, so a
// hand-edited file cannot produce an out-of-range one.
struct OptionRow {
    const char* key;
    int UiPreferences::*field;
};

const OptionRow kOptions[] = {
    {"Music", &UiPreferences::musicVolume},
    {"SoundVolume", &UiPreferences::soundVolume},
    {"ClipPlane", &UiPreferences::clipPlane},
};

constexpr int kOptionMaxStep = 20;

// T461 -- the two user cameras' `[Camera]` keys. Same anti-drift device: one
// table, walked by the parse and the serialise. fable's own key names and
// units (see the flagged deviation in ui_preferences.h).
struct CameraRigRow {
    const char* key;
    CameraRig UiPreferences::*rig;
    float CameraRig::*field;
};

const CameraRigRow kCameraRigKeys[] = {
    {"User1Azimuth", &UiPreferences::user1Camera, &CameraRig::azimuthDeg},
    {"User1Pitch", &UiPreferences::user1Camera, &CameraRig::pitchDeg},
    {"User1Distance", &UiPreferences::user1Camera, &CameraRig::distance},
    {"User1Height", &UiPreferences::user1Camera, &CameraRig::height},
    {"User2Azimuth", &UiPreferences::user2Camera, &CameraRig::azimuthDeg},
    {"User2Pitch", &UiPreferences::user2Camera, &CameraRig::pitchDeg},
    {"User2Distance", &UiPreferences::user2Camera, &CameraRig::distance},
    {"User2Height", &UiPreferences::user2Camera, &CameraRig::height},
};

// B430: the settings-table half of `[Options]` -- the same anti-drift device
// as kOptions (one table, walked by parse and serialise), with the table in
// options_settings.cpp. Persisted = Renderer-owned AND carries an ini key.
bool IsPersistedSetting(int i) {
    if (i <= 0 || i >= OptionsLayout::kSettingCount) return false;
    const auto& info = OptionsLayout::allSettings()[i];
    return info.owner == OptionsLayout::Owner::Renderer && info.iniKey != nullptr;
}

// A bool key accepts the oracle's TRUE/FALSE spelling (eqclient.ini
// `CombatMusic=TRUE`) as well as 0/1; anything the table refuses keeps the
// default, exactly as an absent key would.
void ParseSettingKey(const std::string& key, const std::string& value, UiPreferences& p) {
    for (int i = 0; i < OptionsLayout::kSettingCount; ++i) {
        if (!IsPersistedSetting(i)) continue;
        const auto& info = OptionsLayout::allSettings()[i];
        if (key != info.iniKey) continue;
        int v = 0;
        const std::string t = Trim(value);
        if (info.kind == OptionsLayout::ValueKind::Bool &&
            (t == "TRUE" || t == "true" || t == "FALSE" || t == "false")) {
            v = (t == "TRUE" || t == "true") ? 1 : 0;
        } else {
            try {
                v = std::stoi(t);
            } catch (...) {
                return;
            }
        }
        if (OptionsLayout::isValidSettingValue(info.setting, v)) p.optionValues[i] = v;
        return;
    }
}

}  // namespace

UiPreferences CollectUiPreferences(const UiPreferencesBinding& b, int cameraMode,
                                   const CameraRig& user1Camera,
                                   const CameraRig& user2Camera) {
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
    if (b.options) {
        p.optionsWindow = b.options->isOpen;
        p.musicVolume = b.options->musicVolume;
        p.soundVolume = b.options->soundVolume;
        p.clipPlane = b.options->clipPlane;
        for (int i = 0; i < OptionsLayout::kSettingCount; ++i) {
            if (IsPersistedSetting(i)) p.optionValues[i] = b.options->values[i];
        }
    }
    if (b.friends) p.friendsWindow = b.friends->isOpen;
    if (b.tips) p.tips = *b.tips;
    p.cameraMode = cameraMode;
    p.user1Camera = user1Camera;
    p.user2Camera = user2Camera;
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
    if (b.friends) b.friends->isOpen = p.friendsWindow;
    if (b.tips) *b.tips = p.tips;
    if (b.options) {
        b.options->isOpen = p.optionsWindow;
        // The steps were clamped on parse; clamp again here so a value
        // assembled in code (not read from a file) cannot get through either.
        b.options->musicVolume = std::max(0, std::min(kOptionMaxStep, p.musicVolume));
        b.options->soundVolume = std::max(0, std::min(kOptionMaxStep, p.soundVolume));
        b.options->clipPlane = std::max(0, std::min(kOptionMaxStep, p.clipPlane));
        // B430: a value assembled in code is checked against the table too.
        for (int i = 0; i < OptionsLayout::kSettingCount; ++i) {
            if (!IsPersistedSetting(i)) continue;
            const auto s = static_cast<OptionsLayout::Setting>(i);
            if (OptionsLayout::isValidSettingValue(s, p.optionValues[i])) {
                b.options->values[i] = p.optionValues[i];
            }
        }
    }
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
    // T461: `View=` wins over a legacy `Mode=` wherever either appears.
    bool sawView = false;
    bool sawLegacyMode = false;
    int legacyMode = p.cameraMode;
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
            // Every value is range-checked here, so a hand-edited file can
            // never become an out-of-range CameraMode cast or a NaN camera;
            // a value that is not a number keeps the default, exactly as an
            // absent key would.
            if (key == "View") {
                try {
                    CameraMode m;
                    const int v = std::stoi(Trim(value));
                    if (cameraModeFromIniValue(v, m)) {
                        p.cameraMode = v;
                        sawView = true;
                    }
                } catch (...) {
                }
            } else if (key == "Mode") {
                // T432's key, 0 Free / 1 Follow / 2 FirstPerson -- mapped,
                // never cast (the numberings collide at 1 and 2).
                try {
                    CameraMode m;
                    if (cameraModeFromLegacyIniMode(std::stoi(Trim(value)), m)) {
                        legacyMode = static_cast<int>(m);
                        sawLegacyMode = true;
                    }
                } catch (...) {
                }
            } else {
                for (const auto& row : kCameraRigKeys) {
                    if (key != row.key) continue;
                    try {
                        const float v = std::stof(Trim(value));
                        if (std::isfinite(v)) (p.*(row.rig)).*(row.field) = v;
                    } catch (...) {
                    }
                    break;
                }
            }
            continue;
        }
        // B834 (#202): the official [Tips] section. ShowTips / ShowTOTD
        // are switches; any other key is one tip's "don't show again"
        // (`Inventory=0`); a 1 re-enables it.
        if (section == "Tips") {
            if (key == "ShowTips") {
                p.tips.showTips = ParseBool(value, p.tips.showTips);
            } else if (key == "ShowTOTD") {
                p.tips.showTotd = ParseBool(value, p.tips.showTotd);
            } else if (!key.empty()) {
                if (ParseBool(value, true)) {
                    p.tips.off.erase(key);
                } else {
                    p.tips.off.insert(key);
                }
            }
            continue;
        }
        if (section == "Options") {
            bool matched = false;
            for (const auto& row : kOptions) {
                if (key != row.key) continue;
                matched = true;
                try {
                    const int v = std::stoi(Trim(value));
                    if (v >= 0 && v <= kOptionMaxStep) p.*(row.field) = v;
                } catch (...) {
                    // Not a number -- keep the default, as an absent key would.
                }
                break;
            }
            if (!matched) ParseSettingKey(key, value, p);
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
    if (!sawView && sawLegacyMode) p.cameraMode = legacyMode;
    clampCameraRig(p.user1Camera);
    clampCameraRig(p.user2Camera);
    return p;
}

std::string SerializeUiPreferences(const UiPreferences& p) {
    std::string out;
    out += "; WillEQ per-character UI state. Section and key names are the\n";
    out += "; official client's own (UI_<char>_<server>.ini); fable persists\n";
    out += "; only the user-changeable open/closed state, the camera and the\n";
    out += "; two user cameras (fable's own [Camera] keys and units).\n";
    for (const auto& row : kWindows) {
        out += "[";
        out += row.section;
        out += "]\nShow=";
        out += (p.*(row.field)) ? "1" : "0";
        out += "\n";
    }
    out += "[Options]\n";
    for (const auto& row : kOptions) {
        out += row.key;
        out += "=" + std::to_string(p.*(row.field)) + "\n";
    }
    // B430/B433: every persisted setting, in table order, under its oracle key.
    for (int i = 0; i < OptionsLayout::kSettingCount; ++i) {
        if (!IsPersistedSetting(i)) continue;
        out += OptionsLayout::allSettings()[i].iniKey;
        out += "=" + std::to_string(p.optionValues[i]) + "\n";
    }
    // B834: the official [Tips] section, as the real files write it.
    out += "[Tips]\nShowTips=";
    out += p.tips.showTips ? "1" : "0";
    out += "\nShowTOTD=";
    out += p.tips.showTotd ? "1" : "0";
    out += "\n";
    for (const std::string& key : p.tips.off) out += key + "=0\n";
    out += "[Camera]\nView=" + std::to_string(p.cameraMode) + "\n";
    for (const auto& row : kCameraRigKeys) {
        out += row.key;
        out += "=" + std::to_string((p.*(row.rig)).*(row.field)) + "\n";
    }
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
