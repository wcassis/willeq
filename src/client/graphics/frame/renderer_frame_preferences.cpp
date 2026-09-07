// renderer_frame_preferences.cpp - THE per-character persistence path
// (T432, board B275).
//
// Three functions, and between them they are the only code in the client that
// reads or writes a player's saved UI state:
//
//   uiPreferencesBinding()      - names the panel members that are persisted
//   setUiPreferencesIdentity()  - the character + server, from Application at
//                                 zone-in (the one place both are known)
//   syncUiPreferences()         - load-then-apply on the first call for an
//                                 identity, save-on-difference on every one
//                                 after; called once per frame and once from
//                                 IrrlichtRenderer::shutdown() (the /camp
//                                 path)
//
// The save is a COMPARISON against the last written value, not a set of
// per-toggle hooks. Nine toggles could each have called save(); the tenth
// would have forgotten to, and the file would have silently stopped tracking
// one window. Comparing the whole value makes that class of bug unreachable.
//
// Everything here runs on the render thread: the panel states and the camera
// mode are render-thread state, and the identity strings are written once by
// Application::startLoadingThread(), which is also the render thread.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/ui/ui_preferences.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

UiPreferencesBinding FrameComponent::uiPreferencesBinding() {
    UiPreferencesBinding b;
    b.inventory = &inventoryState_;
    b.spellbook = &spellbookState_;
    b.skills = &skillsPopupState_;
    b.group = &groupPanelState_;
    b.pet = &petPanelState_;
    b.buff = &buffBarState_;
    b.combatAbility = &combatAbilityState_;
    return b;
}

void FrameComponent::setUiPreferencesIdentity(const std::string& character,
                                              const std::string& server) {
    if (character == uiPrefsCharacter_ && server == uiPrefsServer_) return;
    uiPrefsCharacter_ = character;
    uiPrefsServer_ = server;
    // A new identity re-arms the load: zoning does not change the character,
    // but a character change (or the first zone-in of a run) must re-read.
    uiPrefsLoaded_ = false;
    LOG_INFO(MOD_UI, "UI preferences identity: {} on {}", character, server);
}

void FrameComponent::syncUiPreferences() {
    if (uiPrefsCharacter_.empty()) return;

    const int cameraMode = static_cast<int>(ctx_.PlayerCamera().cameraMode());

    if (!uiPrefsLoaded_) {
        // Start from what the client currently holds, so a key this file does
        // not state keeps the panel struct's own default -- the official
        // client's "a default is stated by omission" rule (T368).
        UiPreferences p = CollectUiPreferences(uiPreferencesBinding(), cameraMode);
        if (LoadUiPreferences(uiPrefsCharacter_, uiPrefsServer_, p)) {
            ApplyUiPreferences(p, uiPreferencesBinding());
            ctx_.PlayerCamera().setCameraMode(static_cast<CameraMode>(p.cameraMode));
        }
        // Either way the client now agrees with the file (or with the
        // defaults a fresh character keeps), so record that as the baseline:
        // a fresh character must not write a file until it changes something.
        uiPrefsLastSaved_ = CollectUiPreferences(
            uiPreferencesBinding(), static_cast<int>(ctx_.PlayerCamera().cameraMode()));
        uiPrefsLoaded_ = true;
        return;
    }

    const UiPreferences current =
        CollectUiPreferences(uiPreferencesBinding(), cameraMode);
    if (current == uiPrefsLastSaved_) return;
    // The baseline advances whether or not the write landed. A failure here
    // means no cache directory is writable at all -- not a transient -- and
    // retrying it once per frame would turn one un-writable directory into a
    // 60 Hz open() storm. SaveUiPreferences() logs the reason.
    SaveUiPreferences(uiPrefsCharacter_, uiPrefsServer_, current);
    uiPrefsLastSaved_ = current;
}

// The pass-through Application uses. Kept in this TU so the whole feature --
// declaration aside -- is one file.
void IrrlichtRenderer::setUiPreferencesIdentity(const std::string& character,
                                                const std::string& server) {
    if (frame_) frame_->setUiPreferencesIdentity(character, server);
}

}  // namespace Graphics
}  // namespace EQT
