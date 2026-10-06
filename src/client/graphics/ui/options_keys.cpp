/*
 * The Options window's Keys page (B433) -- see options_keys.h.
 */

#include "client/graphics/ui/options_keys.h"

#include <cctype>

#include "client/graphics/ui/options_panel.h"

namespace EQT {
namespace Graphics {
namespace OptionsKeys {

namespace {

using HA = eqt::input::HotkeyAction;

// The Command / Keypress / Alternate column widths, quoted from
// OKP_KeyboardAssignmentList's <Columns> (180 / 75 / 75).
constexpr int kCommandColW = 180;
constexpr int kKeypressColW = 75;

const char* const kNames[kCategoryCount] = {
    "Movement",     // measured (review/T377/oracle/T377_oracle_options_keys_movement.png)
    "Commands",     // measured (..._keys_Commands_page1.png)
    "Target",       // fable's grouping
    "Spells",       // fable's grouping
    "Hot Buttons",  // fable's grouping
    "Camera",       // fable's grouping
    "UI",           // measured (..._keys_UI_page1..4.png)
    "Debug",        // fable's own developer toggles, no oracle counterpart
};

const std::vector<std::vector<HA>>& table() {
    static const std::vector<std::vector<HA>> kTable = {
        {HA::MoveForward, HA::MoveBackward, HA::StrafeLeft, HA::StrafeRight, HA::TurnLeft,
         HA::TurnRight, HA::Jump, HA::SwimUp, HA::SwimDown, HA::ToggleAutorun},
        {HA::ToggleAutoAttack, HA::Attack, HA::Interact, HA::InteractDoor,
         HA::InteractWorldObject, HA::Hail, HA::Consider, HA::ReplyToTell, HA::OpenChat,
         HA::OpenChatSlash, HA::ChatAutocomplete, HA::SubmitInput, HA::CancelInput,
         HA::ConfirmDialog, HA::CancelDialog, HA::Quit, HA::Screenshot},
        {HA::ClearTarget, HA::TargetSelf, HA::TargetGroupMember1, HA::TargetGroupMember2,
         HA::TargetGroupMember3, HA::TargetGroupMember4, HA::TargetGroupMember5,
         HA::TargetNearestPC, HA::TargetNearestNPC, HA::CycleTargets, HA::CycleTargetsReverse},
        {HA::SpellGem1, HA::SpellGem2, HA::SpellGem3, HA::SpellGem4, HA::SpellGem5,
         HA::SpellGem6, HA::SpellGem7, HA::SpellGem8},
        {HA::HotbarSlot1, HA::HotbarSlot2, HA::HotbarSlot3, HA::HotbarSlot4, HA::HotbarSlot5,
         HA::HotbarSlot6, HA::HotbarSlot7, HA::HotbarSlot8, HA::HotbarSlot9, HA::HotbarSlot10},
        {HA::ToggleCameraMode, HA::CameraZoomIn, HA::CameraZoomOut, HA::CameraOrbitLeft,
         HA::CameraOrbitRight, HA::CameraPitchUp, HA::CameraPitchDown, HA::CameraRaise,
         HA::CameraLower},
        {HA::ToggleAllUI, HA::ToggleInventory, HA::ToggleSkills, HA::ToggleGroup,
         HA::TogglePetWindow, HA::ToggleVendor, HA::ToggleTrainer, HA::ToggleSpellbook,
         HA::ToggleBuffWindow, HA::ToggleCombatAbilities, HA::ToggleOptions, HA::ScrollUIUp,
         HA::ScrollUIDown, HA::ToggleUILock, HA::SaveUILayout, HA::ResetUIDefaults,
         HA::MusicVolumeUp, HA::MusicVolumeDown, HA::EffectsVolumeUp, HA::EffectsVolumeDown},
        {HA::ToggleWireframe, HA::ToggleHUD, HA::ToggleNameTags, HA::ToggleZoneLights,
         HA::ToggleOldModels, HA::ToggleCollision, HA::ToggleCollisionDebug,
         HA::ToggleZoneLineVisualization, HA::ToggleMapOverlay, HA::RotateMapOverlay,
         HA::MirrorMapOverlayX, HA::ToggleNavmeshOverlay, HA::RotateNavmeshOverlay,
         HA::MirrorNavmeshOverlayX, HA::ToggleFrustumCulling, HA::CycleObjectLights},
    };
    return kTable;
}

bool isModifierKey(irr::EKEY_CODE key) {
    switch (key) {
        case irr::KEY_SHIFT: case irr::KEY_LSHIFT: case irr::KEY_RSHIFT:
        case irr::KEY_CONTROL: case irr::KEY_LCONTROL: case irr::KEY_RCONTROL:
        case irr::KEY_MENU: case irr::KEY_LMENU: case irr::KEY_RMENU:
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* categoryName(int category) {
    if (category < 0 || category >= kCategoryCount) return "";
    return kNames[category];
}

const std::vector<eqt::input::HotkeyAction>& actionsIn(int category) {
    static const std::vector<HA> kEmpty;
    if (category < 0 || category >= kCategoryCount) return kEmpty;
    return table()[category];
}

std::string commandText(eqt::input::HotkeyAction action) {
    const std::string name = eqt::input::HotkeyManager::actionEnumToName(action);
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool digit = std::isdigit(static_cast<unsigned char>(c)) != 0;
        const bool prevDigit = i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]));
        if (i > 0 && ((upper && !std::isupper(static_cast<unsigned char>(name[i - 1]))) ||
                      (digit && !prevDigit))) {
            out.push_back(' ');
        }
        out.push_back(i > 0 && upper ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c);
    }
    return out;
}

std::string bindingText(eqt::input::HotkeyAction action, int slot) {
    const auto bindings = eqt::input::HotkeyManager::instance().getBindingsForAction(action);
    if (slot < 0 || slot >= static_cast<int>(bindings.size())) return "-------";
    return eqt::input::HotkeyManager::bindingToString(bindings[slot]);
}

int columnAt(int listLeft, int listWidth, int mouseX) {
    const int rel = mouseX - listLeft;
    (void)listWidth;
    if (rel < kCommandColW) return 0;
    if (rel < kCommandColW + kKeypressColW) return 1;
    return 2;
}

bool finishKeyCapture(OptionsPanelState& state, irr::EKEY_CODE key, bool ctrl, bool shift,
                      bool alt, bool save) {
    if (state.keysCaptureAction < 0) return false;
    if (isModifierKey(key)) return false;
    const auto action = static_cast<eqt::input::HotkeyAction>(state.keysCaptureAction);
    const int slot = state.keysCaptureSlot;
    state.keysCaptureAction = -1;
    if (key == irr::KEY_ESCAPE) return false;  // cancel, nothing bound
    using eqt::input::ModifierFlags;
    ModifierFlags mods = ModifierFlags::None;
    if (ctrl) mods = mods | ModifierFlags::Ctrl;
    if (shift) mods = mods | ModifierFlags::Shift;
    if (alt) mods = mods | ModifierFlags::Alt;
    auto& mgr = eqt::input::HotkeyManager::instance();
    if (!mgr.setBinding(action, slot, key, mods)) return false;
    if (save) mgr.saveUserBindings();
    return true;
}

}  // namespace OptionsKeys
}  // namespace Graphics
}  // namespace EQT
