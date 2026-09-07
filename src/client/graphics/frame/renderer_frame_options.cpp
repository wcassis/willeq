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
// controls reach the game thread through an intent that carries a DELTA
// (MusicVolumeChangeIntent / EffectsVolumeChangeIntent, the only volume
// intents in the tree) and the third is a direct renderer call
// (IrrlichtRenderer::setRenderDistance). A delta needs the previous value,
// and the previous value the AUDIO MANAGER holds is not readable from the
// render thread -- so the renderer keeps its own record of what it last told
// it (optionsAppliedMusicStep_ / optionsAppliedSoundStep_) and sends the
// difference. StaticUIInput therefore resolves a click into "this setting
// moved" and this is the single body that acts on it, for the click path and
// for the preferences-load path alike.
//
// See include/client/graphics/ui/options_panel.h for the measured 0..20
// scale and the flagged clip-plane mapping.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/options_panel.h"
#include "client/events/renderer_intents.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool FrameComponent::toggleOptionsWindow() {
    optionsState_.isOpen = !optionsState_.isOpen;
    LOG_INFO(MOD_GRAPHICS, "Options window {}",
             optionsState_.isOpen ? "opened" : "closed");
    return optionsState_.isOpen;
}

void FrameComponent::applyOptionsSetting(OptionsLayout::Setting setting) {
    switch (setting) {
        case OptionsLayout::Setting::None:
            return;

        case OptionsLayout::Setting::MusicVolume: {
            const int step = optionsState_.musicVolume;
            if (step == optionsAppliedMusicStep_) return;
            const float delta = optionsVolumeFraction(step)
                              - optionsVolumeFraction(optionsAppliedMusicStep_);
            optionsAppliedMusicStep_ = step;
            if (ctx_.bridge) {
                ctx_.bridge->pushIntent(
                    eqt::events::MusicVolumeChangeIntent{delta});
            }
            LOG_INFO(MOD_GRAPHICS, "Options: music volume {} %", step * 5);
            return;
        }

        case OptionsLayout::Setting::SoundVolume: {
            const int step = optionsState_.soundVolume;
            if (step == optionsAppliedSoundStep_) return;
            const float delta = optionsVolumeFraction(step)
                              - optionsVolumeFraction(optionsAppliedSoundStep_);
            optionsAppliedSoundStep_ = step;
            if (ctx_.bridge) {
                ctx_.bridge->pushIntent(
                    eqt::events::EffectsVolumeChangeIntent{delta});
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
            LOG_INFO(MOD_GRAPHICS, "Options: far clip plane {:.0f}", dist);
            return;
        }
    }
}

}  // namespace Graphics
}  // namespace EQT
