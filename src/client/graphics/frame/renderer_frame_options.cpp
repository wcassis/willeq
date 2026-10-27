// renderer_frame_options.cpp - the Options window's toggle and the ONE place
// a wired Options control reaches its setting (T434).
//
// Two functions, and between them they are the whole seam between the window
// the player clicks and the settings behind it:
//
//   toggleOptionsWindow()   - Alt+O, and the arm that used to be `break;`
//   applyOptionsSetting()   - a moved slider -> the audio manager or the
//                             renderer's render distance
//
// WHY THE APPLIER IS HERE AND NOT IN StaticUIInput. Two of the three wired
// controls reach the game thread through an intent
// (MusicVolumeChangeIntent / EffectsVolumeChangeIntent, the only volume
// intents in the tree) and the third is a direct renderer call
// (IrrlichtRenderer::setRenderDistance). StaticUIInput is device-free and
// pushes no intent, so it resolves a click into "this setting moved" and
// this is the single body that acts on it, for the click path and for the
// preferences-load path alike.
//
// T439 (board B432): those two intents used to carry a DELTA, which forced
// this file to keep optionsAppliedMusicStep_ / optionsAppliedSoundStep_ --
// a shadow of what it had last told the audio manager. A /musicvolume sets
// the audio manager ABSOLUTELY and nothing published the result back, so the
// shadow went stale and the next drag applied a difference computed against
// a value that was no longer true. Both records are gone: the intents carry
// the ABSOLUTE step and the audio manager publishes what it actually holds
// back on GameEventType::VolumesChanged, which applyVolumesChanged() writes
// straight into optionsState_. There is now exactly one record of a volume
// on each side of the bridge, and the game side is authoritative.
//
// See include/client/graphics/ui/options_panel.h for the measured 0..20
// scale and the flagged clip-plane mapping.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/options_command.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/overlays/overlays_component.h"  // B994
#include "client/graphics/ui/options_keys.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/events/renderer_intents.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool FrameComponent::toggleOptionsWindow() {
    optionsState_.isOpen = !optionsState_.isOpen;
    if (!optionsState_.isOpen) optionsState_.colorPicker.isOpen = false;  // B993
    LOG_INFO(MOD_GRAPHICS, "Options window {}",
             optionsState_.isOpen ? "opened" : "closed");
    return optionsState_.isOpen;
}

void FrameComponent::applyOptionsSetting(OptionsLayout::Setting setting) {
    // Runs once per UI frame even when no setting changed: zone/preset caps
    // can change independently of a slider gesture.
    optionsState_.appliedClipPlane = owner_.getRenderDistance();
    switch (setting) {
        case OptionsLayout::Setting::None:
            return;

        case OptionsLayout::Setting::MusicVolume: {
            const int step = optionsState_.musicVolume;
            if (ctx_.bridge) {
                ctx_.bridge->pushIntent(
                    eqt::events::MusicVolumeChangeIntent{step});
            }
            LOG_INFO(MOD_GRAPHICS, "Options: music volume {} %", step * 5);
            return;
        }

        case OptionsLayout::Setting::SoundVolume: {
            const int step = optionsState_.soundVolume;
            if (ctx_.bridge) {
                ctx_.bridge->pushIntent(
                    eqt::events::EffectsVolumeChangeIntent{step});
            }
            LOG_INFO(MOD_GRAPHICS, "Options: sound volume {} %", step * 5);
            return;
        }

        case OptionsLayout::Setting::ClipPlane: {
            // The same setting `/renderdist` / `/clipplane` already drives.
            // setRenderDistance() is a no-op while a zone is loading and
            // caps against the constrained preset and the server's own zone
            // max clip -- all of which is its business, not this call's.
            const float dist = optionsClipPlaneDistance(optionsState_.clipPlane);
            owner_.setRenderDistance(dist);
            optionsState_.appliedClipPlane = owner_.getRenderDistance();
            LOG_INFO(MOD_GRAPHICS, "Options: far clip plane {:.0f}", dist);
            return;
        }

        default:
            break;
    }
    applyOptionsSettingB430(setting);
}

// B430/B433: every setting added after T434. Kept beside the legacy arms so
// "the one place a wired control reaches its setting" stays one file.
void FrameComponent::applyOptionsSettingB430(OptionsLayout::Setting setting) {
    using OptionsLayout::Owner;
    using OptionsLayout::Setting;
    const auto& info = OptionsLayout::settingInfo(setting);

    // Server-owned: ask the game thread, which sends it and publishes the
    // applied value back (GameOptionChanged -> applyGameOptionChanged).
    if (info.owner == Owner::Game) {
        if (!ctx_.bridge) return;
        eqt::events::GameOptionIntent intent;
        intent.option = info.gameOption;
        intent.value = gameOptionValueFor(setting, optionsSettingStep(optionsState_, setting) != 0);
        ctx_.bridge->pushIntent(intent);
        LOG_INFO(MOD_GRAPHICS, "Options: {} -> {}", info.name, intent.value);
        return;
    }

    // A chat filter: the whole OP_SetServerFilter payload, every time.
    if (OptionsLayout::filterRow(setting) >= 0) {
        if (!ctx_.bridge) return;
        eqt::events::GameOptionIntent intent;
        intent.option = eqt::GameOption::ServerFilters;
        intent.filters = OptionsLayout::serverFilterPayload(optionsState_.values);
        ctx_.bridge->pushIntent(intent);
        LOG_INFO(MOD_GRAPHICS, "Options: filter {} = {}", info.name,
                 OptionsLayout::choiceText(setting, optionsSettingStep(optionsState_, setting)));
        return;
    }

    auto on = [this](Setting s) { return optionsSettingStep(optionsState_, s) != 0; };
    switch (setting) {
        case Setting::PetWindowPopup:
            // A preference read when a pet ARRIVES (IrrlichtBridge's
            // PetCreated arm); nothing to tell anyone now.
            return;
        case Setting::KeysReloadDefaults:
            // B433: OKP_DefaultKeysButton -- drop the user copy, reload the
            // shipped config/hotkeys.json (global, not per character).
            optionsState_.keysCaptureAction = -1;
            eqt::input::HotkeyManager::instance().restoreShippedBindings();
            LOG_INFO(MOD_GRAPHICS, "Options: key bindings reloaded from defaults");
            return;
        case Setting::TargetRing:
            // B994: the overlay draws it after the target outline each frame.
            ctx_.Overlays().setTargetRingEnabled(on(Setting::TargetRing));
            LOG_INFO(MOD_GRAPHICS, "Options: 3D target ring {}",
                     on(Setting::TargetRing) ? "on" : "off");
            return;
        case Setting::SpellParticleOpacity:
        case Setting::SpellParticleDensity:
        case Setting::SpellParticleNearClip:
        case Setting::EnvParticleOpacity:
        case Setting::EnvParticleDensity:
        case Setting::EnvParticleNearClip:
        case Setting::SpellParticleCastFilter:  // B1165
        case Setting::ShowSpellEffects:         // B1165
            // B995: nothing to push -- the sim bridge reads particleOptionKnobs()
            // every frame, so the next frame's spawn and draw use the value.
            LOG_INFO(MOD_GRAPHICS, "Options: {} = {}", info.name,
                     optionsSettingStep(optionsState_, setting));
            return;
        case Setting::TextColors:
        case Setting::TextColorReset:
            // B993: the chat panel reads the table by pointer at draw time
            // (ChatPanelState::textColors), so a changed row is on screen the
            // next frame; syncUiPreferences() persists it.
            LOG_INFO(MOD_GRAPHICS, "Options: chat colours {}",
                     setting == Setting::TextColorReset ? "reset to the defaults" : "changed");
            return;
        case Setting::PCNames:
        case Setting::NPCNames:
            // B431: the two kinds narrow the Ctrl+F3 master switch; neither
            // moves the other.
            if (ctx_.entityRenderer) {
                ctx_.entityRenderer->setNameTagKindsVisible(on(Setting::PCNames),
                                                            on(Setting::NPCNames));
            }
            LOG_INFO(MOD_GRAPHICS, "Options: PC names {} NPC names {}",
                     on(Setting::PCNames) ? "on" : "off", on(Setting::NPCNames) ? "on" : "off");
            return;
        case Setting::MouseInvertY:
        case Setting::MouseLookSpring:
        case Setting::MouseLook:
        case Setting::MouseWheelZoom:
        case Setting::MouseSensitivity: {
            // B433: the whole Mouse page is one value on the camera.
            MouseOptions m;
            m.invertY = on(Setting::MouseInvertY);
            m.lookSpring = on(Setting::MouseLookSpring);
            m.mouseLook = on(Setting::MouseLook);
            m.wheelZoom = on(Setting::MouseWheelZoom);
            m.sensitivity = optionsSettingStep(optionsState_, Setting::MouseSensitivity);
            ctx_.PlayerCamera().setMouseOptions(m);
            LOG_INFO(MOD_GRAPHICS, "Options: mouse {} = {}", info.name,
                     optionsSettingStep(optionsState_, setting));
            return;
        }
        default:
            return;
    }
}

void FrameComponent::applyAllOptionsSettings() {
    applyOptionsSetting(OptionsLayout::Setting::MusicVolume);
    applyOptionsSetting(OptionsLayout::Setting::SoundVolume);
    applyOptionsSetting(OptionsLayout::Setting::ClipPlane);
    // One payload carries every filter, so one representative is enough.
    applyOptionsSetting(OptionsLayout::Setting::FilterBadWords);
    for (int i = 1; i < OptionsLayout::kSettingCount; ++i) {
        const auto s = static_cast<OptionsLayout::Setting>(i);
        const auto& info = OptionsLayout::settingInfo(s);
        // Persisted renderer settings only: Game-owned ones are the server's
        // and the legacy three and the filters went above.
        if (info.owner != OptionsLayout::Owner::Renderer) continue;
        if (OptionsLayout::filterRow(s) >= 0) continue;
        applyOptionsSetting(s);
    }
}

// B433: the Keys page's capture. While a slot waits for its key the chat
// gate holds every key (RendererEventReceiver passes only F1-F12 on to the
// hotkeys), the next non-modifier key becomes the binding, ESC cancels.
// Returns true while capturing, so processChatInput() reads no key itself.
bool FrameComponent::processOptionsKeyCapture() {
    if (!ctx_.eventReceiver) return false;
    const bool capturing = optionsState_.isOpen && optionsState_.keysCaptureAction >= 0;
    if (!capturing) {
        if (optionsKeyCaptureGate_) {
            optionsKeyCaptureGate_ = false;
            ctx_.eventReceiver->setChatInputFocused(chatInputFocused_);
        }
        optionsState_.keysCaptureAction = -1;
        return false;
    }
    if (!optionsKeyCaptureGate_) {
        optionsKeyCaptureGate_ = true;
        ctx_.eventReceiver->setChatInputFocused(true);
        ctx_.eventReceiver->clearPendingKeyEvents();
        return true;
    }
    ctx_.eventReceiver->escapeKeyPressed();  // ESC here cancels, never closes a window
    ctx_.eventReceiver->enterKeyPressed();
    ctx_.eventReceiver->slashKeyPressed();
    while (ctx_.eventReceiver->hasPendingKeyEvents()) {
        const auto ke = ctx_.eventReceiver->popKeyEvent();
        const bool alt = ctx_.eventReceiver->isKeyDown(irr::KEY_LMENU) ||
                         ctx_.eventReceiver->isKeyDown(irr::KEY_RMENU);
        OptionsKeys::finishKeyCapture(optionsState_, ke.key, ke.ctrl, ke.shift, alt);
        if (optionsState_.keysCaptureAction < 0) break;
    }
    return true;
}

// B430/B433: /option and /filter. The grammar is device-free
// (options_command.cpp); every setting it changed goes through the same
// applyOptionsSetting() a click does, and syncUiPreferences() saves it.
Environment::ParticleKnobs FrameComponent::particleOptionKnobs() const {
    return particleKnobsFromOptions(optionsState_.values, optionsState_.iniFloats);
}

EQ::SpellFxOptions FrameComponent::spellFxOptions() const {
    return spellFxOptionsFromOptions(optionsState_.values, optionsState_.iniFloats);
}

std::vector<std::string> FrameComponent::runOptionsCommand(const std::string& cmd,
                                                           const std::string& args) {
    OptionsCommandResult r = RunOptionsCommand(optionsState_, cmd, args);
    for (const auto s : r.changed) applyOptionsSetting(s);
    return r.lines;
}

}  // namespace Graphics
}  // namespace EQT
