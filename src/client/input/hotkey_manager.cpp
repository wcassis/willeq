#include "client/input/hotkey_manager.h"
#include "common/logging.h"
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
    {"ToggleRendererMode", HotkeyAction::ToggleRendererMode},

    // Player - Movement
    {"MoveForward", HotkeyAction::MoveForward},
    {"MoveBackward", HotkeyAction::MoveBackward},
    {"StrafeLeft", HotkeyAction::StrafeLeft},
    {"StrafeRight", HotkeyAction::StrafeRight},
    {"TurnLeft", HotkeyAction::TurnLeft},
    {"TurnRight", HotkeyAction::TurnRight},
    {"Jump", HotkeyAction::Jump},

    // Player - Toggles
    {"ToggleAutorun", HotkeyAction::ToggleAutorun},
    {"ToggleAutoAttack", HotkeyAction::ToggleAutoAttack},
    {"ToggleInventory", HotkeyAction::ToggleInventory},
    {"ToggleSkills", HotkeyAction::ToggleSkills},
    {"ToggleGroup", HotkeyAction::ToggleGroup},
    {"TogglePetWindow", HotkeyAction::TogglePetWindow},
    {"ToggleVendor", HotkeyAction::ToggleVendor},
    {"ToggleTrainer", HotkeyAction::ToggleTrainer},
    {"ToggleCollision", HotkeyAction::ToggleCollision},
    {"ToggleCollisionDebug", HotkeyAction::ToggleCollisionDebug},
    {"ToggleZoneLineVisualization", HotkeyAction::ToggleZoneLineVisualization},

    // Player - Interaction
    {"InteractDoor", HotkeyAction::InteractDoor},
    {"InteractWorldObject", HotkeyAction::InteractWorldObject},
    {"Hail", HotkeyAction::Hail},
    {"ClearTarget", HotkeyAction::ClearTarget},

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

    // Admin
    {"SaveEntities", HotkeyAction::SaveEntities},
    {"ToggleLighting", HotkeyAction::ToggleLighting},
    {"ToggleHelmDebug", HotkeyAction::ToggleHelmDebug},
    {"HelmPrintState", HotkeyAction::HelmPrintState},
    {"AnimSpeedDecrease", HotkeyAction::AnimSpeedDecrease},
    {"AnimSpeedIncrease", HotkeyAction::AnimSpeedIncrease},
    {"AmbientLightDecrease", HotkeyAction::AmbientLightDecrease},
    {"AmbientLightIncrease", HotkeyAction::AmbientLightIncrease},
    {"CorpseZOffsetUp", HotkeyAction::CorpseZOffsetUp},
    {"CorpseZOffsetDown", HotkeyAction::CorpseZOffsetDown},
    {"EyeHeightUp", HotkeyAction::EyeHeightUp},
    {"EyeHeightDown", HotkeyAction::EyeHeightDown},
    {"ParticleMultiplierDecrease", HotkeyAction::ParticleMultiplierDecrease},
    {"ParticleMultiplierIncrease", HotkeyAction::ParticleMultiplierIncrease},
    {"HeadVariantPrev", HotkeyAction::HeadVariantPrev},
    {"HeadVariantNext", HotkeyAction::HeadVariantNext},

    // Admin - Helm UV
    {"HelmUOffsetLeft", HotkeyAction::HelmUOffsetLeft},
    {"HelmUOffsetRight", HotkeyAction::HelmUOffsetRight},
    {"HelmVOffsetUp", HotkeyAction::HelmVOffsetUp},
    {"HelmVOffsetDown", HotkeyAction::HelmVOffsetDown},
    {"HelmUScaleDecrease", HotkeyAction::HelmUScaleDecrease},
    {"HelmUScaleIncrease", HotkeyAction::HelmUScaleIncrease},
    {"HelmVScaleDecrease", HotkeyAction::HelmVScaleDecrease},
    {"HelmVScaleIncrease", HotkeyAction::HelmVScaleIncrease},
    {"HelmRotateLeft", HotkeyAction::HelmRotateLeft},
    {"HelmRotateRight", HotkeyAction::HelmRotateRight},
    {"HelmReset", HotkeyAction::HelmReset},
    {"HelmUVSwap", HotkeyAction::HelmUVSwap},
    {"HelmVFlip", HotkeyAction::HelmVFlip},
    {"HelmUFlip", HotkeyAction::HelmUFlip},

    // Admin - Collision
    {"CollisionHeightUp", HotkeyAction::CollisionHeightUp},
    {"CollisionHeightDown", HotkeyAction::CollisionHeightDown},
    {"StepHeightUp", HotkeyAction::StepHeightUp},
    {"StepHeightDown", HotkeyAction::StepHeightDown},

    // Repair
    {"RepairRotateXPos", HotkeyAction::RepairRotateXPos},
    {"RepairRotateXNeg", HotkeyAction::RepairRotateXNeg},
    {"RepairRotateYPos", HotkeyAction::RepairRotateYPos},
    {"RepairRotateYNeg", HotkeyAction::RepairRotateYNeg},
    {"RepairRotateZPos", HotkeyAction::RepairRotateZPos},
    {"RepairRotateZNeg", HotkeyAction::RepairRotateZNeg},
    {"RepairFlipX", HotkeyAction::RepairFlipX},
    {"RepairFlipY", HotkeyAction::RepairFlipY},
    {"RepairFlipZ", HotkeyAction::RepairFlipZ},
    {"RepairReset", HotkeyAction::RepairReset},

    {nullptr, HotkeyAction::Count}  // Sentinel
};

// =============================================================================
// Mode Name Mapping
// =============================================================================

static const char* s_modeNames[] = {
    "global",
    "player",
    "repair",
    "admin"
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

std::string HotkeyBinding::toString() const {
    return HotkeyManager::bindingToString(*this);
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

    // === Global Bindings ===
    addBinding(HotkeyAction::ToggleWireframe, HotkeyMode::Global, irr::KEY_F1);
    addBinding(HotkeyAction::ToggleHUD, HotkeyMode::Global, irr::KEY_F2);
    addBinding(HotkeyAction::ToggleNameTags, HotkeyMode::Global, irr::KEY_F3);
    addBinding(HotkeyAction::ToggleZoneLights, HotkeyMode::Global, irr::KEY_F4);
    addBinding(HotkeyAction::ToggleCameraMode, HotkeyMode::Global, irr::KEY_F5);
    addBinding(HotkeyAction::ToggleOldModels, HotkeyMode::Global, irr::KEY_F6);
    addBinding(HotkeyAction::ToggleRendererMode, HotkeyMode::Global, irr::KEY_F9);
    addBinding(HotkeyAction::Screenshot, HotkeyMode::Global, irr::KEY_F12);
    addBinding(HotkeyAction::Quit, HotkeyMode::Global, irr::KEY_ESCAPE, ModifierFlags::Shift);

    // === Player Mode - Movement ===
    addBinding(HotkeyAction::MoveForward, HotkeyMode::Player, irr::KEY_KEY_W);
    addBinding(HotkeyAction::MoveForward, HotkeyMode::Player, irr::KEY_UP);
    addBinding(HotkeyAction::MoveBackward, HotkeyMode::Player, irr::KEY_KEY_S);
    addBinding(HotkeyAction::MoveBackward, HotkeyMode::Player, irr::KEY_DOWN);
    addBinding(HotkeyAction::StrafeLeft, HotkeyMode::Player, irr::KEY_KEY_Q);
    addBinding(HotkeyAction::StrafeRight, HotkeyMode::Player, irr::KEY_KEY_E);
    addBinding(HotkeyAction::TurnLeft, HotkeyMode::Player, irr::KEY_KEY_A);
    addBinding(HotkeyAction::TurnLeft, HotkeyMode::Player, irr::KEY_LEFT);
    addBinding(HotkeyAction::TurnRight, HotkeyMode::Player, irr::KEY_KEY_D);
    addBinding(HotkeyAction::TurnRight, HotkeyMode::Player, irr::KEY_RIGHT);
    addBinding(HotkeyAction::Jump, HotkeyMode::Player, irr::KEY_SPACE);

    // === Player Mode - Toggles ===
    addBinding(HotkeyAction::ToggleAutorun, HotkeyMode::Player, irr::KEY_KEY_R);
    addBinding(HotkeyAction::ToggleAutorun, HotkeyMode::Player, irr::KEY_NUMLOCK);
    addBinding(HotkeyAction::ToggleAutoAttack, HotkeyMode::Player, irr::KEY_OEM_3);  // Grave/Backtick
    addBinding(HotkeyAction::ToggleInventory, HotkeyMode::Player, irr::KEY_KEY_I);
    addBinding(HotkeyAction::ToggleSkills, HotkeyMode::Player, irr::KEY_KEY_K);
    addBinding(HotkeyAction::ToggleGroup, HotkeyMode::Player, irr::KEY_KEY_G);
    addBinding(HotkeyAction::TogglePetWindow, HotkeyMode::Player, irr::KEY_KEY_P);
    addBinding(HotkeyAction::ToggleVendor, HotkeyMode::Player, irr::KEY_KEY_V);
    addBinding(HotkeyAction::ToggleTrainer, HotkeyMode::Player, irr::KEY_KEY_T);
    addBinding(HotkeyAction::ToggleCollision, HotkeyMode::Player, irr::KEY_KEY_C);
    addBinding(HotkeyAction::ToggleCollisionDebug, HotkeyMode::Player, irr::KEY_KEY_C, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::ToggleZoneLineVisualization, HotkeyMode::Player, irr::KEY_KEY_Z);

    // === Player Mode - Interaction ===
    addBinding(HotkeyAction::InteractDoor, HotkeyMode::Player, irr::KEY_KEY_U);
    addBinding(HotkeyAction::InteractWorldObject, HotkeyMode::Player, irr::KEY_KEY_O);
    addBinding(HotkeyAction::Hail, HotkeyMode::Player, irr::KEY_KEY_H);
    addBinding(HotkeyAction::ClearTarget, HotkeyMode::Player, irr::KEY_ESCAPE);

    // === Player Mode - Chat ===
    addBinding(HotkeyAction::OpenChat, HotkeyMode::Player, irr::KEY_RETURN);
    addBinding(HotkeyAction::OpenChatSlash, HotkeyMode::Player, irr::KEY_OEM_2);  // Slash

    // === Player Mode - Spell Gems (1-8, no modifier) ===
    addBinding(HotkeyAction::SpellGem1, HotkeyMode::Player, irr::KEY_KEY_1);
    addBinding(HotkeyAction::SpellGem2, HotkeyMode::Player, irr::KEY_KEY_2);
    addBinding(HotkeyAction::SpellGem3, HotkeyMode::Player, irr::KEY_KEY_3);
    addBinding(HotkeyAction::SpellGem4, HotkeyMode::Player, irr::KEY_KEY_4);
    addBinding(HotkeyAction::SpellGem5, HotkeyMode::Player, irr::KEY_KEY_5);
    addBinding(HotkeyAction::SpellGem6, HotkeyMode::Player, irr::KEY_KEY_6);
    addBinding(HotkeyAction::SpellGem7, HotkeyMode::Player, irr::KEY_KEY_7);
    addBinding(HotkeyAction::SpellGem8, HotkeyMode::Player, irr::KEY_KEY_8);

    // === Player Mode - Hotbar (Ctrl+1-0) ===
    addBinding(HotkeyAction::HotbarSlot1, HotkeyMode::Player, irr::KEY_KEY_1, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot2, HotkeyMode::Player, irr::KEY_KEY_2, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot3, HotkeyMode::Player, irr::KEY_KEY_3, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot4, HotkeyMode::Player, irr::KEY_KEY_4, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot5, HotkeyMode::Player, irr::KEY_KEY_5, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot6, HotkeyMode::Player, irr::KEY_KEY_6, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot7, HotkeyMode::Player, irr::KEY_KEY_7, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot8, HotkeyMode::Player, irr::KEY_KEY_8, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot9, HotkeyMode::Player, irr::KEY_KEY_9, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HotbarSlot10, HotkeyMode::Player, irr::KEY_KEY_0, ModifierFlags::Ctrl);

    // === Player Mode - Camera Zoom ===
    addBinding(HotkeyAction::CameraZoomIn, HotkeyMode::Player, irr::KEY_PLUS);
    addBinding(HotkeyAction::CameraZoomOut, HotkeyMode::Player, irr::KEY_MINUS);

    // === Admin Mode ===
    addBinding(HotkeyAction::SaveEntities, HotkeyMode::Admin, irr::KEY_F10);
    addBinding(HotkeyAction::ToggleLighting, HotkeyMode::Admin, irr::KEY_F11);
    addBinding(HotkeyAction::ToggleHelmDebug, HotkeyMode::Admin, irr::KEY_F7);
    addBinding(HotkeyAction::HelmPrintState, HotkeyMode::Admin, irr::KEY_F8);
    addBinding(HotkeyAction::AnimSpeedDecrease, HotkeyMode::Admin, irr::KEY_OEM_4);  // [
    addBinding(HotkeyAction::AnimSpeedIncrease, HotkeyMode::Admin, irr::KEY_OEM_6);  // ]
    addBinding(HotkeyAction::AmbientLightIncrease, HotkeyMode::Admin, irr::KEY_PRIOR);  // Page Up
    addBinding(HotkeyAction::AmbientLightDecrease, HotkeyMode::Admin, irr::KEY_NEXT);   // Page Down
    addBinding(HotkeyAction::CorpseZOffsetUp, HotkeyMode::Admin, irr::KEY_KEY_P);
    addBinding(HotkeyAction::CorpseZOffsetDown, HotkeyMode::Admin, irr::KEY_KEY_P, ModifierFlags::Shift);
    addBinding(HotkeyAction::EyeHeightUp, HotkeyMode::Admin, irr::KEY_KEY_Y);
    addBinding(HotkeyAction::EyeHeightDown, HotkeyMode::Admin, irr::KEY_KEY_Y, ModifierFlags::Shift);
    addBinding(HotkeyAction::ParticleMultiplierDecrease, HotkeyMode::Admin, irr::KEY_MINUS, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::ParticleMultiplierIncrease, HotkeyMode::Admin, irr::KEY_PLUS, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HeadVariantPrev, HotkeyMode::Admin, irr::KEY_KEY_H);
    addBinding(HotkeyAction::HeadVariantNext, HotkeyMode::Admin, irr::KEY_KEY_N);

    // Admin - Helm UV adjustments
    addBinding(HotkeyAction::HelmUOffsetLeft, HotkeyMode::Admin, irr::KEY_KEY_I);
    addBinding(HotkeyAction::HelmUOffsetRight, HotkeyMode::Admin, irr::KEY_KEY_K);
    addBinding(HotkeyAction::HelmVOffsetUp, HotkeyMode::Admin, irr::KEY_KEY_L);
    addBinding(HotkeyAction::HelmVOffsetDown, HotkeyMode::Admin, irr::KEY_KEY_J);
    addBinding(HotkeyAction::HelmUScaleDecrease, HotkeyMode::Admin, irr::KEY_KEY_O);
    addBinding(HotkeyAction::HelmUScaleIncrease, HotkeyMode::Admin, irr::KEY_KEY_P);
    addBinding(HotkeyAction::HelmVScaleDecrease, HotkeyMode::Admin, irr::KEY_COMMA);
    addBinding(HotkeyAction::HelmVScaleIncrease, HotkeyMode::Admin, irr::KEY_PERIOD);
    addBinding(HotkeyAction::HelmRotateLeft, HotkeyMode::Admin, irr::KEY_MINUS);
    addBinding(HotkeyAction::HelmRotateRight, HotkeyMode::Admin, irr::KEY_PLUS);
    addBinding(HotkeyAction::HelmReset, HotkeyMode::Admin, irr::KEY_KEY_0);
    addBinding(HotkeyAction::HelmUVSwap, HotkeyMode::Admin, irr::KEY_KEY_S, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HelmVFlip, HotkeyMode::Admin, irr::KEY_KEY_V, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::HelmUFlip, HotkeyMode::Admin, irr::KEY_KEY_U, ModifierFlags::Ctrl);

    // Admin - Collision height adjustments
    addBinding(HotkeyAction::CollisionHeightUp, HotkeyMode::Admin, irr::KEY_KEY_T);
    addBinding(HotkeyAction::CollisionHeightDown, HotkeyMode::Admin, irr::KEY_KEY_G);
    addBinding(HotkeyAction::StepHeightUp, HotkeyMode::Admin, irr::KEY_KEY_Y);
    addBinding(HotkeyAction::StepHeightDown, HotkeyMode::Admin, irr::KEY_KEY_B);

    // === Repair Mode ===
    addBinding(HotkeyAction::RepairRotateXPos, HotkeyMode::Repair, irr::KEY_KEY_X);
    addBinding(HotkeyAction::RepairRotateXNeg, HotkeyMode::Repair, irr::KEY_KEY_X, ModifierFlags::Shift);
    addBinding(HotkeyAction::RepairRotateYPos, HotkeyMode::Repair, irr::KEY_KEY_Y);
    addBinding(HotkeyAction::RepairRotateYNeg, HotkeyMode::Repair, irr::KEY_KEY_Y, ModifierFlags::Shift);
    addBinding(HotkeyAction::RepairRotateZPos, HotkeyMode::Repair, irr::KEY_KEY_Z);
    addBinding(HotkeyAction::RepairRotateZNeg, HotkeyMode::Repair, irr::KEY_KEY_Z, ModifierFlags::Shift);
    addBinding(HotkeyAction::RepairFlipX, HotkeyMode::Repair, irr::KEY_KEY_1, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::RepairFlipY, HotkeyMode::Repair, irr::KEY_KEY_2, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::RepairFlipZ, HotkeyMode::Repair, irr::KEY_KEY_3, ModifierFlags::Ctrl);
    addBinding(HotkeyAction::RepairReset, HotkeyMode::Repair, irr::KEY_KEY_R, ModifierFlags::Ctrl);

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
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_DEBUG(MOD_INPUT, "HotkeyManager: Config file not found: {}", path);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_INPUT, "HotkeyManager: Failed to parse {}: {}", path, errors);
        return false;
    }

    m_configPath = path;

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
        if (bindings.isMember("repair")) {
            loadModeBindings(bindings["repair"], HotkeyMode::Repair);
        }
        if (bindings.isMember("admin")) {
            loadModeBindings(bindings["admin"], HotkeyMode::Admin);
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
    if (overrides.isMember("repair")) {
        loadModeBindings(overrides["repair"], HotkeyMode::Repair);
    }
    if (overrides.isMember("admin")) {
        loadModeBindings(overrides["admin"], HotkeyMode::Admin);
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
    for (auto it = range.first; it != range.second; ++it) {
        const auto& binding = m_bindings[it->second];

        // Check if mode matches (Global bindings always match)
        if (binding.mode == HotkeyMode::Global || binding.mode == currentMode) {
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
                    info.binding1 = b1;
                    info.binding2 = b2;

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
