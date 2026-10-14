#include "client/input/hotkey_manager.h"
#include "common/logging.h"
#include "client/data_paths.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace eqt {
namespace input {

// =============================================================================
// Key Name <-> EKEY_CODE Mapping Tables
// =============================================================================

struct KeyNameMapping {
    const char* name;
    irr::EKEY_CODE code;
};

static const KeyNameMapping s_keyNameMappings[] = {
    // Letters
    {"A", irr::KEY_KEY_A}, {"B", irr::KEY_KEY_B}, {"C", irr::KEY_KEY_C},
    {"D", irr::KEY_KEY_D}, {"E", irr::KEY_KEY_E}, {"F", irr::KEY_KEY_F},
    {"G", irr::KEY_KEY_G}, {"H", irr::KEY_KEY_H}, {"I", irr::KEY_KEY_I},
    {"J", irr::KEY_KEY_J}, {"K", irr::KEY_KEY_K}, {"L", irr::KEY_KEY_L},
    {"M", irr::KEY_KEY_M}, {"N", irr::KEY_KEY_N}, {"O", irr::KEY_KEY_O},
    {"P", irr::KEY_KEY_P}, {"Q", irr::KEY_KEY_Q}, {"R", irr::KEY_KEY_R},
    {"S", irr::KEY_KEY_S}, {"T", irr::KEY_KEY_T}, {"U", irr::KEY_KEY_U},
    {"V", irr::KEY_KEY_V}, {"W", irr::KEY_KEY_W}, {"X", irr::KEY_KEY_X},
    {"Y", irr::KEY_KEY_Y}, {"Z", irr::KEY_KEY_Z},

    // Numbers (top row)
    {"0", irr::KEY_KEY_0}, {"1", irr::KEY_KEY_1}, {"2", irr::KEY_KEY_2},
    {"3", irr::KEY_KEY_3}, {"4", irr::KEY_KEY_4}, {"5", irr::KEY_KEY_5},
    {"6", irr::KEY_KEY_6}, {"7", irr::KEY_KEY_7}, {"8", irr::KEY_KEY_8},
    {"9", irr::KEY_KEY_9},

    // Function keys
    {"F1", irr::KEY_F1}, {"F2", irr::KEY_F2}, {"F3", irr::KEY_F3},
    {"F4", irr::KEY_F4}, {"F5", irr::KEY_F5}, {"F6", irr::KEY_F6},
    {"F7", irr::KEY_F7}, {"F8", irr::KEY_F8}, {"F9", irr::KEY_F9},
    {"F10", irr::KEY_F10}, {"F11", irr::KEY_F11}, {"F12", irr::KEY_F12},

    // Navigation
    {"Up", irr::KEY_UP}, {"Down", irr::KEY_DOWN},
    {"Left", irr::KEY_LEFT}, {"Right", irr::KEY_RIGHT},
    {"PageUp", irr::KEY_PRIOR}, {"PageDown", irr::KEY_NEXT},
    {"Home", irr::KEY_HOME}, {"End", irr::KEY_END},
    {"Insert", irr::KEY_INSERT}, {"Delete", irr::KEY_DELETE},

    // Special keys
    {"Escape", irr::KEY_ESCAPE}, {"Return", irr::KEY_RETURN},
    {"Enter", irr::KEY_RETURN},  // Alias
    {"Space", irr::KEY_SPACE}, {"Tab", irr::KEY_TAB},
    {"Backspace", irr::KEY_BACK},

    // Modifier keys (for reference, not typically used as primary key)
    {"Shift", irr::KEY_SHIFT}, {"Control", irr::KEY_CONTROL},
    {"Ctrl", irr::KEY_CONTROL},  // Alias
    {"Menu", irr::KEY_MENU},  // Alt key on some systems

    // Numpad
    {"Numpad0", irr::KEY_NUMPAD0}, {"Numpad1", irr::KEY_NUMPAD1},
    {"Numpad2", irr::KEY_NUMPAD2}, {"Numpad3", irr::KEY_NUMPAD3},
    {"Numpad4", irr::KEY_NUMPAD4}, {"Numpad5", irr::KEY_NUMPAD5},
    {"Numpad6", irr::KEY_NUMPAD6}, {"Numpad7", irr::KEY_NUMPAD7},
    {"Numpad8", irr::KEY_NUMPAD8}, {"Numpad9", irr::KEY_NUMPAD9},
    {"NumpadAdd", irr::KEY_ADD}, {"NumpadSubtract", irr::KEY_SUBTRACT},
    {"NumpadMultiply", irr::KEY_MULTIPLY}, {"NumpadDivide", irr::KEY_DIVIDE},
    {"NumpadDecimal", irr::KEY_DECIMAL}, {"NumLock", irr::KEY_NUMLOCK},

    // OEM/Punctuation keys
    {"Grave", irr::KEY_OEM_3},       // ` ~
    {"Backtick", irr::KEY_OEM_3},    // Alias
    {"Tilde", irr::KEY_OEM_3},       // Alias
    {"Minus", irr::KEY_MINUS},       // - _
    {"Equals", irr::KEY_PLUS},       // = +
    {"Plus", irr::KEY_PLUS},         // Alias
    {"BracketLeft", irr::KEY_OEM_4}, // [ {
    {"BracketRight", irr::KEY_OEM_6},// ] }
    {"Backslash", irr::KEY_OEM_5},   // \ |
    {"Semicolon", irr::KEY_OEM_1},   // ; :
    {"Apostrophe", irr::KEY_OEM_7},  // ' "
    {"Quote", irr::KEY_OEM_7},       // Alias
    {"Comma", irr::KEY_COMMA},       // , <
    {"Period", irr::KEY_PERIOD},     // . >
    {"Slash", irr::KEY_OEM_2},       // / ?

    // Scroll/Pause/Print
    {"ScrollLock", irr::KEY_SCROLL},
    {"Pause", irr::KEY_PAUSE},
    {"Print", irr::KEY_PRINT},
    {"PrintScreen", irr::KEY_PRINT},  // Alias

    {nullptr, irr::KEY_KEY_CODES_COUNT}  // Sentinel
};

// =============================================================================
// Action Name <-> HotkeyAction Mapping Tables
// =============================================================================

struct ActionNameMapping {
    const char* name;
    HotkeyAction action;
};

// B905 (#225): the eight actions deleted for having no dispatch consumer (see
// hotkey_manager.h). A hotkeys.json written before that -- the shipped file's
// old revision or a user copy saved by the Options Keys page -- still names
// them; they are skipped quietly rather than warned about as unknown.
static const char* const kRetiredActionNames[] = {
    "ToggleUILock", "SaveUILayout", "ResetUIDefaults", "ConfirmDialog",
    "CancelDialog", "SubmitInput", "CancelInput", "ChatAutocomplete",
};

static bool isRetiredActionName(const std::string& name) {
    for (const char* retired : kRetiredActionNames) {
        if (name == retired) return true;
    }
    return false;
}

static const ActionNameMapping s_actionNameMappings[] = {
    // Global
    {"Quit", HotkeyAction::Quit},
    {"Screenshot", HotkeyAction::Screenshot},
    {"ToggleWireframe", HotkeyAction::ToggleWireframe},
    {"ToggleHUD", HotkeyAction::ToggleHUD},
    {"ToggleNameTags", HotkeyAction::ToggleNameTags},
    {"ToggleZoneLights", HotkeyAction::ToggleZoneLights},
    {"ToggleCameraMode", HotkeyAction::ToggleCameraMode},
    {"ToggleOldModels", HotkeyAction::ToggleOldModels},
    {"ToggleAllUI", HotkeyAction::ToggleAllUI},
    {"ScrollUIUp", HotkeyAction::ScrollUIUp},
    {"ScrollUIDown", HotkeyAction::ScrollUIDown},

    // Player - Movement
    {"MoveForward", HotkeyAction::MoveForward},
    {"MoveBackward", HotkeyAction::MoveBackward},
    {"StrafeLeft", HotkeyAction::StrafeLeft},
    {"StrafeRight", HotkeyAction::StrafeRight},
    {"TurnLeft", HotkeyAction::TurnLeft},
    {"TurnRight", HotkeyAction::TurnRight},
    {"Jump", HotkeyAction::Jump},
    {"SwimUp", HotkeyAction::SwimUp},
    {"SwimDown", HotkeyAction::SwimDown},

    // Player - Toggles
    {"ToggleAutorun", HotkeyAction::ToggleAutorun},
    {"ToggleAutoAttack", HotkeyAction::ToggleAutoAttack},
    {"ToggleInventory", HotkeyAction::ToggleInventory},
    {"ToggleSkills", HotkeyAction::ToggleSkills},
    {"ToggleGroup", HotkeyAction::ToggleGroup},
    {"TogglePetWindow", HotkeyAction::TogglePetWindow},
    {"ToggleVendor", HotkeyAction::ToggleVendor},
    {"ToggleTrainer", HotkeyAction::ToggleTrainer},
    {"ToggleSpellbook", HotkeyAction::ToggleSpellbook},
    {"ToggleBuffWindow", HotkeyAction::ToggleBuffWindow},
    {"ToggleCombatAbilities", HotkeyAction::ToggleCombatAbilities},
    {"ToggleOptions", HotkeyAction::ToggleOptions},
    {"ToggleFriends", HotkeyAction::ToggleFriends},  // B897
    {"ToggleCollision", HotkeyAction::ToggleCollision},
    {"ToggleCollisionDebug", HotkeyAction::ToggleCollisionDebug},
    {"ToggleZoneLineVisualization", HotkeyAction::ToggleZoneLineVisualization},
    {"ToggleMapOverlay", HotkeyAction::ToggleMapOverlay},
    {"RotateMapOverlay", HotkeyAction::RotateMapOverlay},
    {"MirrorMapOverlayX", HotkeyAction::MirrorMapOverlayX},
    {"ToggleNavmeshOverlay", HotkeyAction::ToggleNavmeshOverlay},
    {"RotateNavmeshOverlay", HotkeyAction::RotateNavmeshOverlay},
    {"MirrorNavmeshOverlayX", HotkeyAction::MirrorNavmeshOverlayX},
    {"ToggleFrustumCulling", HotkeyAction::ToggleFrustumCulling},

    // Player - Interaction
    {"InteractDoor", HotkeyAction::InteractDoor},
    {"InteractWorldObject", HotkeyAction::InteractWorldObject},
    {"Interact", HotkeyAction::Interact},
    {"Hail", HotkeyAction::Hail},
    {"ClearTarget", HotkeyAction::ClearTarget},
    {"Consider", HotkeyAction::Consider},
    {"Attack", HotkeyAction::Attack},
    {"RangeAttack", HotkeyAction::RangeAttack},  // B900 (#216): unbound, owed
    {"ReplyToTell", HotkeyAction::ReplyToTell},

    // Player - Targeting
    {"TargetSelf", HotkeyAction::TargetSelf},
    {"TargetGroupMember1", HotkeyAction::TargetGroupMember1},
    {"TargetGroupMember2", HotkeyAction::TargetGroupMember2},
    {"TargetGroupMember3", HotkeyAction::TargetGroupMember3},
    {"TargetGroupMember4", HotkeyAction::TargetGroupMember4},
    {"TargetGroupMember5", HotkeyAction::TargetGroupMember5},
    {"TargetNearestPC", HotkeyAction::TargetNearestPC},
    {"TargetNearestNPC", HotkeyAction::TargetNearestNPC},
    {"CycleTargets", HotkeyAction::CycleTargets},
    {"CycleTargetsReverse", HotkeyAction::CycleTargetsReverse},

    // Player - Chat
    {"OpenChat", HotkeyAction::OpenChat},
    {"OpenChatSlash", HotkeyAction::OpenChatSlash},

    // Player - Spell Gems
    {"SpellGem1", HotkeyAction::SpellGem1},
    {"SpellGem2", HotkeyAction::SpellGem2},
    {"SpellGem3", HotkeyAction::SpellGem3},
    {"SpellGem4", HotkeyAction::SpellGem4},
    {"SpellGem5", HotkeyAction::SpellGem5},
    {"SpellGem6", HotkeyAction::SpellGem6},
    {"SpellGem7", HotkeyAction::SpellGem7},
    {"SpellGem8", HotkeyAction::SpellGem8},

    // Player - Hotbar
    {"HotbarSlot1", HotkeyAction::HotbarSlot1},
    {"HotbarSlot2", HotkeyAction::HotbarSlot2},
    {"HotbarSlot3", HotkeyAction::HotbarSlot3},
    {"HotbarSlot4", HotkeyAction::HotbarSlot4},
    {"HotbarSlot5", HotkeyAction::HotbarSlot5},
    {"HotbarSlot6", HotkeyAction::HotbarSlot6},
    {"HotbarSlot7", HotkeyAction::HotbarSlot7},
    {"HotbarSlot8", HotkeyAction::HotbarSlot8},
    {"HotbarSlot9", HotkeyAction::HotbarSlot9},
    {"HotbarSlot10", HotkeyAction::HotbarSlot10},

    // Player - Camera
    {"CameraZoomIn", HotkeyAction::CameraZoomIn},
    {"CameraZoomOut", HotkeyAction::CameraZoomOut},
    {"CameraOrbitLeft", HotkeyAction::CameraOrbitLeft},     // T461
    {"CameraOrbitRight", HotkeyAction::CameraOrbitRight},   // T461
    {"CameraPitchUp", HotkeyAction::CameraPitchUp},         // T461
    {"CameraPitchDown", HotkeyAction::CameraPitchDown},     // T461
    {"CameraRaise", HotkeyAction::CameraRaise},             // T471
    {"CameraLower", HotkeyAction::CameraLower},             // T471

    // Player - Audio Volume
    {"MusicVolumeUp", HotkeyAction::MusicVolumeUp},
    {"MusicVolumeDown", HotkeyAction::MusicVolumeDown},
    {"EffectsVolumeUp", HotkeyAction::EffectsVolumeUp},
    {"EffectsVolumeDown", HotkeyAction::EffectsVolumeDown},

    // Player - Lighting
    {"CycleObjectLights", HotkeyAction::CycleObjectLights},

    {nullptr, HotkeyAction::Count}  // Sentinel
};

// =============================================================================
// Mode Name Mapping
// =============================================================================

static const char* s_modeNames[] = {
    "global",
    "player"
};

// =============================================================================
// Helper Functions
// =============================================================================

static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

// =============================================================================
// HotkeyBinding Implementation
// =============================================================================

bool HotkeyBinding::matches(irr::EKEY_CODE key, bool ctrl, bool shift, bool alt) const {
    if (keyCode != key) return false;

    ModifierFlags inputMods = ModifierFlags::None;
    if (ctrl) inputMods = inputMods | ModifierFlags::Ctrl;
    if (shift) inputMods = inputMods | ModifierFlags::Shift;
    if (alt) inputMods = inputMods | ModifierFlags::Alt;

    return modifiers == inputMods;
}

// =============================================================================
// HotkeyManager Implementation
// =============================================================================

HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager instance;
    return instance;
}

HotkeyManager::HotkeyManager() {
    setupDefaults();
}

irr::EKEY_CODE HotkeyManager::keyNameToCode(const std::string& name) {
    std::string lowerName = toLower(name);

    for (const auto& mapping : s_keyNameMappings) {
        if (mapping.name == nullptr) break;
        if (toLower(mapping.name) == lowerName) {
            return mapping.code;
        }
    }

    return irr::KEY_KEY_CODES_COUNT;
}

std::string HotkeyManager::keyCodeToName(irr::EKEY_CODE code) {
    for (const auto& mapping : s_keyNameMappings) {
        if (mapping.name == nullptr) break;
        if (mapping.code == code) {
            return mapping.name;
        }
    }

    return "Unknown";
}

HotkeyAction HotkeyManager::actionNameToEnum(const std::string& name) {
    for (const auto& mapping : s_actionNameMappings) {
        if (mapping.name == nullptr) break;
        if (mapping.name == name) {
            return mapping.action;
        }
    }

    return HotkeyAction::Count;
}

std::string HotkeyManager::actionEnumToName(HotkeyAction action) {
    for (const auto& mapping : s_actionNameMappings) {
        if (mapping.name == nullptr) break;
        if (mapping.action == action) {
            return mapping.name;
        }
    }

    return "Unknown";
}

HotkeyMode HotkeyManager::modeNameToEnum(const std::string& name) {
    std::string lowerName = toLower(name);
    for (size_t i = 0; i < sizeof(s_modeNames) / sizeof(s_modeNames[0]); ++i) {
        if (toLower(s_modeNames[i]) == lowerName) {
            return static_cast<HotkeyMode>(i);
        }
    }
    return HotkeyMode::Global;
}

std::string HotkeyManager::modeEnumToName(HotkeyMode mode) {
    size_t idx = static_cast<size_t>(mode);
    if (idx < sizeof(s_modeNames) / sizeof(s_modeNames[0])) {
        return s_modeNames[idx];
    }
    return "unknown";
}

std::string HotkeyManager::bindingToString(const HotkeyBinding& binding) {
    std::string result;

    if (hasModifier(binding.modifiers, ModifierFlags::Ctrl)) {
        result += "Ctrl+";
    }
    if (hasModifier(binding.modifiers, ModifierFlags::Shift)) {
        result += "Shift+";
    }
    if (hasModifier(binding.modifiers, ModifierFlags::Alt)) {
        result += "Alt+";
    }

    result += keyCodeToName(binding.keyCode);
    return result;
}

bool HotkeyManager::parseBindingString(const std::string& bindingStr,
                                       irr::EKEY_CODE& outKeyCode,
                                       ModifierFlags& outModifiers) const {
    outKeyCode = irr::KEY_KEY_CODES_COUNT;
    outModifiers = ModifierFlags::None;

    // Split by '+'
    std::vector<std::string> parts;
    std::string current;
    for (char c : bindingStr) {
        if (c == '+') {
            if (!current.empty()) {
                parts.push_back(trim(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(trim(current));
    }

    if (parts.empty()) {
        return false;
    }

    // Parse each part - last one is the primary key, others are modifiers
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        std::string lowerPart = toLower(part);

        if (i < parts.size() - 1) {
            // This is a modifier
            if (lowerPart == "ctrl" || lowerPart == "control") {
                outModifiers = outModifiers | ModifierFlags::Ctrl;
            } else if (lowerPart == "shift") {
                outModifiers = outModifiers | ModifierFlags::Shift;
            } else if (lowerPart == "alt") {
                outModifiers = outModifiers | ModifierFlags::Alt;
            } else {
                LOG_ERROR(MOD_INPUT,
                    "HotkeyManager: Unknown modifier '{}' in binding '{}'", part, bindingStr);
                return false;
            }
        } else {
            // This is the primary key
            outKeyCode = keyNameToCode(part);
            if (outKeyCode == irr::KEY_KEY_CODES_COUNT) {
                LOG_ERROR(MOD_INPUT,
                    "HotkeyManager: Unknown key '{}' in binding '{}'", part, bindingStr);
                return false;
            }
        }
    }

    return true;
}

void HotkeyManager::setupDefaults() {
    m_bindings.clear();
    m_movementKeys.clear();

    auto addBinding = [this](HotkeyAction action, HotkeyMode mode,
                             irr::EKEY_CODE key, ModifierFlags mods = ModifierFlags::None) {
        HotkeyBinding binding;
        binding.action = action;
        binding.mode = mode;
        binding.keyCode = key;
        binding.modifiers = mods;
        m_bindings.push_back(binding);

        // Track movement keys separately for continuous polling
        if (action >= HotkeyAction::MoveForward && action <= HotkeyAction::TurnRight) {
            m_movementKeys[key] = action;
        }
    };

    // === Global Bindings (always active) ===
    addBinding(HotkeyAction::Screenshot, HotkeyMode::Global, irr::KEY_F12);
    addBinding(HotkeyAction::Quit, HotkeyMode::Global, irr::KEY_ESCAPE, ModifierFlags::Shift);
    // T434: Alt+O, MEASURED at the official client's Options > Keys > UI
    // page (review/T377/oracle/T377_oracle_options_keys_UI_page2.png):
    // "Toggle options window   Alt O", with an EMPTY Alternate column. The
    // bare `O` this used to bind was a fable invention of exactly the class
    // T382 removed for the group and pet windows, and it left `O` bound to a
    // window that did not exist. `O` is now unbound, as it is in the oracle.
    addBinding(HotkeyAction::ToggleOptions, HotkeyMode::Global, irr::KEY_KEY_O, ModifierFlags::Alt);
    // B897: Alt+F, MEASURED on the same page
    // (review/T377/oracle/T377_oracle_options_keys_UI_page1.png): "Toggle
    // friend window   Alt F" (eqstr 6599), Alternate column empty.
    addBinding(HotkeyAction::ToggleFriends, HotkeyMode::Global, irr::KEY_KEY_F, ModifierFlags::Alt);
    // B582 (GitLab #49, Will's ruling 2026-09-18, docs/decisions.md
    // 2026-09-18 item 2): adopt the official keymap. F10 = "Toggle UI Display",
    // MEASURED on the oracle's Options > Keys > UI page (T466) -- F11, which
    // this used to bind, is the official client's netstat toggle, which fable
    // does not have, so F11 is unbound. The old F10/F11/Shift+F10/Shift+F11
    // volume bindings below are gone too: the oracle's Camera, UI and Commands
    // pages name no volume key.
    addBinding(HotkeyAction::ToggleAllUI, HotkeyMode::Global, irr::KEY_F10);
    // T313: PageUp/PageDown scroll the static-UI panel under the cursor --
    // chat scrollback, the skills list, the loot list -- which is what they
    // do in the real client. PageDown used to be a second binding for
    // StrafeRight; that moved to Home, pairing with StrafeLeft's End, so
    // scrolling a loot list does not also strafe the player.
    addBinding(HotkeyAction::ScrollUIUp, HotkeyMode::Global, irr::KEY_PRIOR);
    addBinding(HotkeyAction::ScrollUIDown, HotkeyMode::Global, irr::KEY_NEXT);
    // B562 (board; GitLab #182) added the file's eight file-only bindings
    // here. B905 (#225, UIGAP-small) then DELETED seven of them, plus
    // SubmitInput below: none had a dispatch consumer, none is on the oracle's
    // Options > Keys pages, and as Global bindings they shadowed Tab
    // (CycleTargets), Escape (ClearTarget) and Return (OpenChat) -- so Tab has
    // not cycled targets in a production run since the file carried them.
    // ToggleZoneLights, the one with a consumer, stays. It sits in the
    // player section of the file, and getAction() runs Global first, so the
    // default must match that mode exactly or the tables disagree.
    addBinding(HotkeyAction::ToggleZoneLights, HotkeyMode::Player, irr::KEY_F4, ModifierFlags::Ctrl);

    // === Player Mode - Movement ===
    addBinding(HotkeyAction::MoveForward, HotkeyMode::Player, irr::KEY_KEY_W);
    addBinding(HotkeyAction::MoveForward, HotkeyMode::Player, irr::KEY_UP);
    addBinding(HotkeyAction::MoveBackward, HotkeyMode::Player, irr::KEY_KEY_S);
    addBinding(HotkeyAction::MoveBackward, HotkeyMode::Player, irr::KEY_DOWN);
    addBinding(HotkeyAction::StrafeLeft, HotkeyMode::Player, irr::KEY_KEY_A, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::StrafeLeft, HotkeyMode::Player, irr::KEY_END);
    addBinding(HotkeyAction::StrafeRight, HotkeyMode::Player, irr::KEY_KEY_D, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::StrafeRight, HotkeyMode::Player, irr::KEY_HOME);  // T313: was Page Down
    addBinding(HotkeyAction::TurnLeft, HotkeyMode::Player, irr::KEY_KEY_A);
    addBinding(HotkeyAction::TurnLeft, HotkeyMode::Player, irr::KEY_LEFT);
    addBinding(HotkeyAction::TurnRight, HotkeyMode::Player, irr::KEY_KEY_D);
    addBinding(HotkeyAction::TurnRight, HotkeyMode::Player, irr::KEY_RIGHT);
    addBinding(HotkeyAction::Jump, HotkeyMode::Player, irr::KEY_SPACE);
    // SwimUp/SwimDown - [ and ] for vertical swimming movement
    addBinding(HotkeyAction::SwimUp, HotkeyMode::Player, irr::KEY_OEM_4);    // [
    addBinding(HotkeyAction::SwimDown, HotkeyMode::Player, irr::KEY_OEM_6);  // ]

    // === Player Mode - Combat ===
    addBinding(HotkeyAction::ToggleAutoAttack, HotkeyMode::Player, irr::KEY_KEY_Q);
    addBinding(HotkeyAction::Attack, HotkeyMode::Player, irr::KEY_KEY_Q, ModifierFlags::Ctrl);

    // === Player Mode - Toggles ===
    addBinding(HotkeyAction::ToggleAutorun, HotkeyMode::Player, irr::KEY_OEM_3);  // Grave/Backtick
    addBinding(HotkeyAction::ToggleAutorun, HotkeyMode::Player, irr::KEY_ADD);    // Numpad +
    addBinding(HotkeyAction::ToggleAutorun, HotkeyMode::Player, irr::KEY_NUMLOCK);
    addBinding(HotkeyAction::ToggleInventory, HotkeyMode::Player, irr::KEY_KEY_I);
    addBinding(HotkeyAction::ToggleSkills, HotkeyMode::Player, irr::KEY_KEY_K);
    // T382 (B170). MEASURED at the official client's Options > Keys > UI page,
    // T377 (review/T377/oracle/T377_oracle_options_keys_UI_page1..4.png):
    // Toggle party window = Alt P, Toggle buff window = Alt B, Toggle pet info
    // window = Alt F1 -- every one of them with an EMPTY Alternate column.
    // fable's old G (group) was an invention (G is bound to nothing in the UI
    // or Commands category; Alt G is Guild Management), and its old plain P
    // (pet) was the official key for Toggle Customer Service Win, so a player
    // who learned the real client pressed P expecting the petition window and
    // got a pet window. Both are gone; only the measured primaries remain.
    addBinding(HotkeyAction::ToggleGroup, HotkeyMode::Player, irr::KEY_KEY_P, ModifierFlags::Alt);
    addBinding(HotkeyAction::TogglePetWindow, HotkeyMode::Player, irr::KEY_F1, ModifierFlags::Alt);
    addBinding(HotkeyAction::ToggleVendor, HotkeyMode::Player, irr::KEY_KEY_V);
    addBinding(HotkeyAction::ToggleTrainer, HotkeyMode::Player, irr::KEY_KEY_T);
    addBinding(HotkeyAction::ToggleSpellbook, HotkeyMode::Player, irr::KEY_KEY_B, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::ToggleBuffWindow, HotkeyMode::Player, irr::KEY_KEY_B, ModifierFlags::Alt);
    // T415 (board B320). MEASURED at the official client's Options > Keys > UI
    // page, T377 (review/T377/oracle/T377_oracle_options_keys_UI_page3.png and
    // page4): "Toggle Combat Ability Window   Alt C", Alternate column empty.
    // Alt+C is free here -- ToggleCollision is Ctrl+Alt+C.
    addBinding(HotkeyAction::ToggleCombatAbilities, HotkeyMode::Player, irr::KEY_KEY_C, ModifierFlags::Alt);
    addBinding(HotkeyAction::ToggleCollision, HotkeyMode::Player, irr::KEY_KEY_C, ModifierFlags::Ctrl | ModifierFlags::Alt);
    addBinding(HotkeyAction::ToggleZoneLineVisualization, HotkeyMode::Player, irr::KEY_KEY_Z, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::ToggleMapOverlay, HotkeyMode::Player, irr::KEY_KEY_M, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::RotateMapOverlay, HotkeyMode::Player, irr::KEY_KEY_M, ModifierFlags::Ctrl | ModifierFlags::Shift);
    addBinding(HotkeyAction::MirrorMapOverlayX, HotkeyMode::Player, irr::KEY_KEY_M, ModifierFlags::Ctrl | ModifierFlags::Alt);
    addBinding(HotkeyAction::ToggleNavmeshOverlay, HotkeyMode::Player, irr::KEY_KEY_N, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::RotateNavmeshOverlay, HotkeyMode::Player, irr::KEY_KEY_N, ModifierFlags::Ctrl | ModifierFlags::Shift);
    addBinding(HotkeyAction::MirrorNavmeshOverlayX, HotkeyMode::Player, irr::KEY_KEY_N, ModifierFlags::Ctrl | ModifierFlags::Alt);
    addBinding(HotkeyAction::ToggleFrustumCulling, HotkeyMode::Player, irr::KEY_KEY_V, ModifierFlags::Ctrl);

    // === Player Mode - Interaction ===
    addBinding(HotkeyAction::Interact, HotkeyMode::Player, irr::KEY_KEY_U);
    addBinding(HotkeyAction::Hail, HotkeyMode::Player, irr::KEY_KEY_H);
    addBinding(HotkeyAction::ClearTarget, HotkeyMode::Player, irr::KEY_ESCAPE);
    addBinding(HotkeyAction::Consider, HotkeyMode::Player, irr::KEY_KEY_C);
    addBinding(HotkeyAction::ReplyToTell, HotkeyMode::Player, irr::KEY_KEY_R);

    // === Player Mode - Targeting (F1-F8, Tab) ===
    addBinding(HotkeyAction::TargetSelf, HotkeyMode::Player, irr::KEY_F1);
    addBinding(HotkeyAction::TargetGroupMember1, HotkeyMode::Player, irr::KEY_F2);
    addBinding(HotkeyAction::TargetGroupMember2, HotkeyMode::Player, irr::KEY_F3);
    addBinding(HotkeyAction::TargetGroupMember3, HotkeyMode::Player, irr::KEY_F4);
    addBinding(HotkeyAction::TargetGroupMember4, HotkeyMode::Player, irr::KEY_F5);
    addBinding(HotkeyAction::TargetGroupMember5, HotkeyMode::Player, irr::KEY_F6);
    addBinding(HotkeyAction::TargetNearestPC, HotkeyMode::Player, irr::KEY_F7);
    addBinding(HotkeyAction::TargetNearestNPC, HotkeyMode::Player, irr::KEY_F8);
    addBinding(HotkeyAction::CycleTargets, HotkeyMode::Player, irr::KEY_TAB);
    addBinding(HotkeyAction::CycleTargetsReverse, HotkeyMode::Player, irr::KEY_TAB, ModifierFlags::Shift);

    // === Player Mode - Chat ===
    addBinding(HotkeyAction::OpenChat, HotkeyMode::Player, irr::KEY_RETURN);
    addBinding(HotkeyAction::OpenChatSlash, HotkeyMode::Player, irr::KEY_OEM_2);  // Slash
    // Submitting and cancelling chat are the text entry's own keys
    // (applyTextEntryKey), not hotkeys: B905 deleted SubmitInput/CancelInput.

    // === Player Mode - Spell Gems (Alt+1-8) ===
    addBinding(HotkeyAction::SpellGem1, HotkeyMode::Player, irr::KEY_KEY_1, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem2, HotkeyMode::Player, irr::KEY_KEY_2, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem3, HotkeyMode::Player, irr::KEY_KEY_3, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem4, HotkeyMode::Player, irr::KEY_KEY_4, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem5, HotkeyMode::Player, irr::KEY_KEY_5, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem6, HotkeyMode::Player, irr::KEY_KEY_6, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem7, HotkeyMode::Player, irr::KEY_KEY_7, ModifierFlags::Alt);
    addBinding(HotkeyAction::SpellGem8, HotkeyMode::Player, irr::KEY_KEY_8, ModifierFlags::Alt);

    // === Player Mode - Hotbar (1-0, no modifier) ===
    addBinding(HotkeyAction::HotbarSlot1, HotkeyMode::Player, irr::KEY_KEY_1);
    addBinding(HotkeyAction::HotbarSlot2, HotkeyMode::Player, irr::KEY_KEY_2);
    addBinding(HotkeyAction::HotbarSlot3, HotkeyMode::Player, irr::KEY_KEY_3);
    addBinding(HotkeyAction::HotbarSlot4, HotkeyMode::Player, irr::KEY_KEY_4);
    addBinding(HotkeyAction::HotbarSlot5, HotkeyMode::Player, irr::KEY_KEY_5);
    addBinding(HotkeyAction::HotbarSlot6, HotkeyMode::Player, irr::KEY_KEY_6);
    addBinding(HotkeyAction::HotbarSlot7, HotkeyMode::Player, irr::KEY_KEY_7);
    addBinding(HotkeyAction::HotbarSlot8, HotkeyMode::Player, irr::KEY_KEY_8);
    addBinding(HotkeyAction::HotbarSlot9, HotkeyMode::Player, irr::KEY_KEY_9);
    addBinding(HotkeyAction::HotbarSlot10, HotkeyMode::Player, irr::KEY_KEY_0);

    // === Player Mode - Camera ===
    addBinding(HotkeyAction::CameraZoomIn, HotkeyMode::Player, irr::KEY_PLUS);
    addBinding(HotkeyAction::CameraZoomOut, HotkeyMode::Player, irr::KEY_MINUS);
    // B562: T466's oracle Camera page also measured Shift+Plus / NumpadSubtract
    // (the shipped file carries them, T471); the defaults must, or the
    // fallback run (file unreadable) loses them.
    addBinding(HotkeyAction::CameraZoomIn, HotkeyMode::Player, irr::KEY_PLUS, ModifierFlags::Shift);
    addBinding(HotkeyAction::CameraZoomOut, HotkeyMode::Player, irr::KEY_SUBTRACT);
    // T461: F9 is Will's ruling and REPLACES Ctrl+F5, with no alias. The
    // official action is TOGGLECAM ("Toggle camera view", eqstr_us.txt 6536),
    // and T466 MEASURED F9 as its key on the official client.
    addBinding(HotkeyAction::ToggleCameraMode, HotkeyMode::Player, irr::KEY_F9);
    // T471: the rest of the official Camera page, MEASURED by T466
    // (review/T466/oracle/T466_oracle_options_keys_Camera_page1_root_1024x768.png),
    // primary and alternate column each. Census before binding: no fable
    // binding used Insert, Delete, any numpad digit, or Alt with an arrow,
    // PageUp/PageDown or a numpad digit. The bare arrows stay movement and the
    // bare PageUp/PageDown stay UI scroll -- getAction() and the movement poll
    // both compare modifiers exactly.
    addBinding(HotkeyAction::CameraZoomIn, HotkeyMode::Player, irr::KEY_NUMPAD7);
    addBinding(HotkeyAction::CameraZoomIn, HotkeyMode::Player, irr::KEY_INSERT);
    addBinding(HotkeyAction::CameraZoomOut, HotkeyMode::Player, irr::KEY_NUMPAD1);
    addBinding(HotkeyAction::CameraZoomOut, HotkeyMode::Player, irr::KEY_DELETE);
    addBinding(HotkeyAction::CameraRaise, HotkeyMode::Player, irr::KEY_NUMPAD8, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraRaise, HotkeyMode::Player, irr::KEY_UP, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraLower, HotkeyMode::Player, irr::KEY_NUMPAD2, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraLower, HotkeyMode::Player, irr::KEY_DOWN, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraOrbitRight, HotkeyMode::Player, irr::KEY_NUMPAD6, ModifierFlags::Alt);  // Tether-slide right
    addBinding(HotkeyAction::CameraOrbitRight, HotkeyMode::Player, irr::KEY_RIGHT, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraOrbitLeft, HotkeyMode::Player, irr::KEY_NUMPAD4, ModifierFlags::Alt);   // Tether-slide left
    addBinding(HotkeyAction::CameraOrbitLeft, HotkeyMode::Player, irr::KEY_LEFT, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraPitchUp, HotkeyMode::Player, irr::KEY_NUMPAD9, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraPitchUp, HotkeyMode::Player, irr::KEY_PRIOR, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraPitchDown, HotkeyMode::Player, irr::KEY_NUMPAD3, ModifierFlags::Alt);
    addBinding(HotkeyAction::CameraPitchDown, HotkeyMode::Player, irr::KEY_NEXT, ModifierFlags::Alt);

    // === Player Mode - Lighting ===
    // B582 (GitLab #49, Will's ruling 2026-09-18): the official client's L
    // toggles the Leadership window, which is post-Velious (LoY/GoD) and
    // IGNORED by Will's feature-gap ruling -- so fable's CycleObjectLights
    // (L) is unbound. It has no slash route of its own: /olight is the
    // on/off toggle (renderer_slash_commands.cpp), not the cycle; the
    // Options > Keys Debug page still reaches it.

    // === Player Mode - Audio Volume ===
    // B582: unbound. The oracle's Options > Keys pages (Camera, UI, Commands;
    // T466) name no volume key; the official route is the slider. The Options
    // General sliders and /musicvolume /effectsvolume remain (docs/decisions.md
    // "Volumes round-trip", CLAUDE.md). The MusicVolume*/EffectsVolume*
    // receiver arms in renderer_event_receiver.cpp are kept for a key the user
    // binds on the Options Keys page (options_keys.cpp:57); the sliders push
    // their intents directly (renderer_frame_options.cpp:67,77).

    rebuildIndex();
}

void HotkeyManager::rebuildIndex() {
    m_keyIndex.clear();
    m_movementKeys.clear();

    for (size_t i = 0; i < m_bindings.size(); ++i) {
        const auto& binding = m_bindings[i];
        uint32_t hash = makeKeyHash(binding.keyCode, binding.modifiers);
        m_keyIndex.emplace(hash, i);

        // Track movement keys for continuous polling
        if (binding.action >= HotkeyAction::MoveForward &&
            binding.action <= HotkeyAction::TurnRight &&
            binding.modifiers == ModifierFlags::None) {
            m_movementKeys[binding.keyCode] = binding.action;
        }
    }
}

uint32_t HotkeyManager::makeKeyHash(irr::EKEY_CODE key, ModifierFlags mods) const {
    return (static_cast<uint32_t>(key) << 8) | static_cast<uint32_t>(mods);
}

bool HotkeyManager::loadFromFile(const std::string& path) {
    // T399 (board B235): shipped data resolves through the one path rule.
    // config/hotkeys.json IS shipped, so a miss is not "no user overrides
    // yet" -- it means the shipped bindings are not the ones in effect.
    // That was a LOG_DEBUG; it is an ERROR now.
    const std::string resolved = eqt::ResolveDataPath(path);
    std::ifstream file(resolved);
    if (!file.is_open()) {
        LOG_ERROR(MOD_INPUT,
                  "HotkeyManager: cannot open '{}' (resolved to '{}') -- the SHIPPED key "
                  "bindings are not loaded; the built-in defaults are in effect",
                  path, resolved);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_INPUT, "HotkeyManager: Failed to parse {}: {}", resolved, errors);
        return false;
    }

    m_configPath = resolved;

    // Clear existing bindings and set up fresh from file
    m_bindings.clear();
    m_movementKeys.clear();

    if (root.isMember("bindings")) {
        const Json::Value& bindings = root["bindings"];

        if (bindings.isMember("global")) {
            loadModeBindings(bindings["global"], HotkeyMode::Global);
        }
        if (bindings.isMember("player")) {
            loadModeBindings(bindings["player"], HotkeyMode::Player);
        }
    }

    rebuildIndex();

    LOG_INFO(MOD_INPUT, "HotkeyManager: Loaded {} bindings from {}",
        m_bindings.size(), path);

    // Check for conflicts
    logConflicts();

    return true;
}

void HotkeyManager::loadModeBindings(const Json::Value& modeObj, HotkeyMode mode) {
    for (const auto& actionName : modeObj.getMemberNames()) {
        HotkeyAction action = actionNameToEnum(actionName);
        if (action == HotkeyAction::Count && isRetiredActionName(actionName)) {
            // B905: a user copy saved before the deletion still names them.
            LOG_INFO(MOD_INPUT,
                "HotkeyManager: ignoring retired action '{}' (B905: it had no consumer)", actionName);
            continue;
        }
        if (action == HotkeyAction::Count) {
            LOG_WARN(MOD_INPUT,
                "HotkeyManager: Unknown action '{}' in config", actionName);
            continue;
        }

        const Json::Value& bindingValue = modeObj[actionName];

        // Can be a single string or array of strings
        std::vector<std::string> bindingStrings;
        if (bindingValue.isString()) {
            bindingStrings.push_back(bindingValue.asString());
        } else if (bindingValue.isArray()) {
            for (const auto& item : bindingValue) {
                if (item.isString()) {
                    bindingStrings.push_back(item.asString());
                }
            }
        }

        for (const auto& bindingStr : bindingStrings) {
            irr::EKEY_CODE keyCode;
            ModifierFlags modifiers;

            if (parseBindingString(bindingStr, keyCode, modifiers)) {
                HotkeyBinding binding;
                binding.action = action;
                binding.mode = mode;
                binding.keyCode = keyCode;
                binding.modifiers = modifiers;
                m_bindings.push_back(binding);

                // Track movement keys
                if (action >= HotkeyAction::MoveForward &&
                    action <= HotkeyAction::TurnRight &&
                    modifiers == ModifierFlags::None) {
                    m_movementKeys[keyCode] = action;
                }
            }
        }
    }
}

bool HotkeyManager::saveToFile(const std::string& path) {
    std::string savePath = path.empty() ? m_configPath : path;

    Json::Value root;
    root["version"] = 1;

    Json::Value bindings;

    // Group bindings by mode
    std::map<HotkeyMode, Json::Value> modeBindings;

    for (const auto& binding : m_bindings) {
        std::string actionName = actionEnumToName(binding.action);
        std::string bindingStr = bindingToString(binding);
        std::string modeName = modeEnumToName(binding.mode);

        // Check if action already has bindings (for arrays)
        if (modeBindings[binding.mode].isMember(actionName)) {
            Json::Value& existing = modeBindings[binding.mode][actionName];
            if (existing.isString()) {
                // Convert to array
                std::string oldValue = existing.asString();
                existing = Json::Value(Json::arrayValue);
                existing.append(oldValue);
            }
            existing.append(bindingStr);
        } else {
            modeBindings[binding.mode][actionName] = bindingStr;
        }
    }

    for (const auto& [mode, modeObj] : modeBindings) {
        bindings[modeEnumToName(mode)] = modeObj;
    }

    root["bindings"] = bindings;

    std::ofstream file(savePath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_INPUT, "HotkeyManager: Cannot write to {}", savePath);
        return false;
    }

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &file);

    LOG_INFO(MOD_INPUT, "HotkeyManager: Saved {} bindings to {}",
        m_bindings.size(), savePath);

    return true;
}

void HotkeyManager::applyOverrides(const Json::Value& overrides) {
    // Overrides have the same structure as the "bindings" section
    if (overrides.isMember("global")) {
        loadModeBindings(overrides["global"], HotkeyMode::Global);
    }
    if (overrides.isMember("player")) {
        loadModeBindings(overrides["player"], HotkeyMode::Player);
    }

    rebuildIndex();
    logConflicts();
}

void HotkeyManager::resetToDefaults() {
    setupDefaults();
}

bool HotkeyManager::reload() {
    if (m_configPath.empty()) {
        LOG_WARN(MOD_INPUT, "HotkeyManager: No config path set for reload");
        return false;
    }

    // Reset to defaults first, then load from file
    setupDefaults();
    return loadFromFile(m_configPath);
}

std::optional<HotkeyAction> HotkeyManager::getAction(
    irr::EKEY_CODE keyCode,
    bool ctrl, bool shift, bool alt,
    HotkeyMode currentMode) const {

    ModifierFlags inputMods = ModifierFlags::None;
    if (ctrl) inputMods = inputMods | ModifierFlags::Ctrl;
    if (shift) inputMods = inputMods | ModifierFlags::Shift;
    if (alt) inputMods = inputMods | ModifierFlags::Alt;

    uint32_t hash = makeKeyHash(keyCode, inputMods);

    auto range = m_keyIndex.equal_range(hash);

    // First pass: prefer Global bindings (always take priority)
    for (auto it = range.first; it != range.second; ++it) {
        const auto& binding = m_bindings[it->second];
        if (binding.mode == HotkeyMode::Global) {
            return binding.action;
        }
    }

    // Second pass: check mode-specific bindings
    for (auto it = range.first; it != range.second; ++it) {
        const auto& binding = m_bindings[it->second];
        if (binding.mode == currentMode) {
            return binding.action;
        }
    }

    return std::nullopt;
}

bool HotkeyManager::isMovementKey(irr::EKEY_CODE keyCode, HotkeyAction& outAction) const {
    auto it = m_movementKeys.find(keyCode);
    if (it != m_movementKeys.end()) {
        outAction = it->second;
        return true;
    }
    return false;
}

std::vector<HotkeyBinding> HotkeyManager::getBindingsForAction(HotkeyAction action) const {
    std::vector<HotkeyBinding> result;
    for (const auto& binding : m_bindings) {
        if (binding.action == action) {
            result.push_back(binding);
        }
    }
    return result;
}

std::vector<ConflictInfo> HotkeyManager::detectConflicts() const {
    std::vector<ConflictInfo> conflicts;

    for (size_t i = 0; i < m_bindings.size(); ++i) {
        for (size_t j = i + 1; j < m_bindings.size(); ++j) {
            const auto& b1 = m_bindings[i];
            const auto& b2 = m_bindings[j];

            // Check if same key + modifiers
            if (b1.keyCode == b2.keyCode && b1.modifiers == b2.modifiers) {
                // Conflict if same mode, or if either is Global
                bool sameMode = (b1.mode == b2.mode);
                bool globalConflict = (b1.mode == HotkeyMode::Global ||
                                       b2.mode == HotkeyMode::Global);

                if (sameMode || globalConflict) {
                    ConflictInfo info;
                    // B345: binding1/binding2 went with their report fields.
                    std::string modeInfo;
                    if (sameMode) {
                        modeInfo = " in " + modeEnumToName(b1.mode) + " mode";
                    } else {
                        modeInfo = " (global conflicts with " + modeEnumToName(
                            b1.mode == HotkeyMode::Global ? b2.mode : b1.mode) + ")";
                    }

                    info.message = "Hotkey conflict: " +
                        actionEnumToName(b1.action) + " and " +
                        actionEnumToName(b2.action) + " both bound to " +
                        bindingToString(b1) + modeInfo;

                    conflicts.push_back(info);
                }
            }
        }
    }

    return conflicts;
}

void HotkeyManager::logConflicts() const {
    auto conflicts = detectConflicts();
    for (const auto& c : conflicts) {
        LOG_WARN(MOD_INPUT, "HotkeyManager: {}", c.message);
    }
}

} // namespace input
} // namespace eqt
