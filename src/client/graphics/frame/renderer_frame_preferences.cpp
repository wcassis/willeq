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
#include "client/graphics/ui/options_panel.h"
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
    b.options = &optionsState_;          // T434
    b.friends = &friendsState_;          // B897
    b.tips = &tipPrefs_;                 // B834
    return b;
}

void FrameComponent::setUiPreferencesIdentity(const std::string& character,
                                              const std::string& server) {
    if (character == uiPrefsCharacter_ && server == uiPrefsServer_) return;
    // T467 (board B560): a change FROM a character -- not the first identity
    // of a run -- must not hand the new character the old one's state. The
    // reset itself runs in syncUiPreferences(), on the first frame after the
    // zone load, because its clip-plane half is a renderer call that is a
    // no-op while a zone is loading.
    uiPrefsResetPending_ = !uiPrefsCharacter_.empty();
    uiPrefsCharacter_ = character;
    uiPrefsServer_ = server;
    // A new identity re-arms the load: zoning does not change the character,
    // but a character change (or the first zone-in of a run) must re-read.
    uiPrefsLoaded_ = false;
    // B834: the tips popped "this session" are per character.
    tipState_.shownThisSession.clear();
    tipState_.totdShownThisSession = false;
    LOG_INFO(MOD_UI, "UI preferences identity: {} on {}", character, server);
}

void FrameComponent::syncEqClientIni() {
    // B1175 (#494): the client reads eqclient.ini [Defaults] once at start
    // (eqgame.exe.c:132608-132671) and writes it when a value changes; the
    // same compare-against-the-last-write device as the per-character file.
    if (!eqclientLoaded_) {
        EqClientIni p = CollectEqClientIni(optionsState_);
        if (LoadEqClientIni(p)) ApplyEqClientIni(p, optionsState_);
        eqclientLastSaved_ = CollectEqClientIni(optionsState_);
        eqclientLoaded_ = true;
        return;
    }
    const EqClientIni current = CollectEqClientIni(optionsState_);
    if (current == eqclientLastSaved_) return;
    SaveEqClientIni(current);
    eqclientLastSaved_ = current;
}

void FrameComponent::syncUiPreferences() {
    // B1175: the global file first -- it needs no character.
    syncEqClientIni();
    if (uiPrefsCharacter_.empty()) return;

    // T461: the camera half of the value -- the mode and the two user cameras.
    auto collect = [this]() {
        const PlayerCameraComponent& camera = ctx_.PlayerCamera();
        return CollectUiPreferences(uiPreferencesBinding(),
                                    static_cast<int>(camera.cameraMode()),
                                    *camera.cameraRigFor(CameraMode::User1),
                                    *camera.cameraRigFor(CameraMode::User2));
    };

    if (!uiPrefsLoaded_) {
        // T464 (board B532): a zone load no longer resets the camera to Chase
        // (PlayerCameraComponent::placeCameraForZoneIn()), so the default a
        // character with no saved view gets is applied HERE, once per
        // identity -- otherwise a second character in the same process would
        // inherit the first one's camera. A file that states View= overrides
        // it just below.
        ctx_.PlayerCamera().setCameraMode(kDefaultCameraMode);

        // T467 (board B560): on a change of character every persisted field
        // goes back to its DEFAULT before the new file is read, so a key that
        // file does not state -- all of them, for a character with no file --
        // keeps the default (T368's rule), not the previous character's value.
        // The FIRST identity of a run is not reset: before it the client
        // already holds real values (the audio manager's published volumes,
        // applyVolumesChanged()), and those are what a fresh character keeps.
        const bool reset = uiPrefsResetPending_;
        uiPrefsResetPending_ = false;
        if (reset) {
            const UiPreferences defaults;
            ApplyUiPreferences(defaults, uiPreferencesBinding());
            ctx_.PlayerCamera().setUserCameraRigs(defaults.user1Camera, defaults.user2Camera);
            // T486 (item 7): the Chase race scale is per character too.
            ctx_.PlayerCamera().resetChaseEyeScale();
        }

        // Start from what the client now holds, so a key this file does not
        // state keeps the default -- the official client's "a default is
        // stated by omission" rule (T368).
        UiPreferences p = collect();
        const bool loaded = LoadUiPreferences(uiPrefsCharacter_, uiPrefsServer_, p);
        if (loaded) {
            ApplyUiPreferences(p, uiPreferencesBinding());
            // T461: the rigs before the mode, so entering a user camera
            // places it where the file says. p.cameraMode was range-checked
            // on parse; the mapping is explicit, never a cast.
            ctx_.PlayerCamera().setUserCameraRigs(p.user1Camera, p.user2Camera);
            CameraMode mode;
            if (cameraModeFromIniValue(p.cameraMode, mode)) {
                ctx_.PlayerCamera().setCameraMode(mode);
            }
        }
        if (loaded || reset) {
            // T434: the three Options sliders are the first persisted fields
            // that are not just a struct member -- each has a setting BEHIND
            // it that has to be told. Apply is device-free by design and
            // cannot push an intent, so the three go through the same one
            // body a click does. T467: a reset moved them too, so a second
            // character with no file tells the audio manager and the clip
            // plane about its defaults exactly as a file would.
            // B430: and every persisted setting since (the filters' one
            // OP_SetServerFilter payload among them).
            applyAllOptionsSettings();
        }
        // Either way the client now agrees with the file (or with the
        // defaults a fresh character keeps), so record that as the baseline:
        // a fresh character must not write a file until it changes something.
        uiPrefsLastSaved_ = collect();
        uiPrefsLoaded_ = true;
        return;
    }

    const UiPreferences current = collect();
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
