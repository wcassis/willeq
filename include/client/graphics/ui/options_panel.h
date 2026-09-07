/*
 * The Options window — renderer-side state, draw and hit tests (T434).
 *
 * Until this existed `O` reached a bare `break;` in
 * renderer_frame_input.cpp with the comment "WindowManager removed — options
 * window not available", and a `PopupType` enumerator named Options had zero
 * uses in the whole tree: a bound key advertising a window that was not
 * there. T432's Review recorded it as the one `[…]Window` section it could
 * not persist because there was nothing to persist. T439 DELETED that
 * enumerator (board B434): this window is independent, not a centre popup,
 * so the enumerator never gained a producer or a consumer.
 *
 * GEOMETRY is entirely options_layout.h, quoted from the oracle's
 * EQUI_OptionsWindow.xml. This TU computes no rect.
 *
 * WHAT IS WIRED, and why only three controls are.
 * The rule (T363) is that nothing may advertise a setting fable does not
 * have, so a control whose `OptionsLayout::Setting` is `None` is drawn
 * GREYED and is not hit-testable. Three are wired, because three settings
 * exist behind a reachable seam today:
 *
 *   OGP_MusicVolumeSlider  -> AudioManager music volume, through the
 *                             existing MusicVolumeChangeIntent
 *   OGP_SoundVolumeSlider  -> AudioManager effects volume, through the
 *                             existing EffectsVolumeChangeIntent
 *   ODP_ClipPlaneSlider    -> IrrlichtRenderer::setRenderDistance(), the
 *                             same setting `/clipplane` already drives
 *
 * THE 0..20 SCALE IS MEASURED, not chosen. /mnt/projects/EverQuestP1999/
 * eqclient.ini records `Music=18`, `SoundVolume=10`, `Realism=1`,
 * `Gamma=11`, `ClipPlane=20` — and the live oracle capture
 * review/T377/oracle/T377_oracle_options_alt_o_1024x768.png shows that same
 * client displaying Music Volume **90 %**, Sound Volume **50 %** and Sound
 * Realism **5 %**. 18/20 = 90 %, 10/20 = 50 %, 1/20 = 5 %: the official
 * client stores these sliders as an integer 0..20 and prints value x 5 as a
 * percentage. fable uses the same 21 positions and the same readout, so a
 * fable file and an official one mean the same thing by the same number.
 *
 * THE CLIP-PLANE MAPPING IS FABLE'S, and is flagged. The oracle's
 * `ClipPlane=20` is a slider position, and the world units it means are not
 * recoverable from the ini. fable maps the 21 positions linearly onto its
 * own render-distance range in 50-unit steps — step*50 + 50, i.e. 50..1050 —
 * which puts `config/display_settings.json`'s shipped `renderDistance: 300`
 * exactly on step 5 and stays inside the 50..10000 band `/renderdist`
 * already enforces.
 */

#pragma once

#include <string>

#include "client/graphics/ui/options_layout.h"
#include "client/state/event_bus.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;

struct OptionsPanelState {
    /**
     * The Alt+O toggle. MEASURED at the official client's Options > Keys >
     * UI page (review/T377/oracle/T377_oracle_options_keys_UI_page2.png),
     * whose row reads `Toggle options window   Alt O   -------` — Keypress
     * `Alt O`, Alternate column empty. fable bound a bare `O`, which was an
     * invention of the same class as the `G`/`P` T382 removed.
     *
     * Default CLOSED, also measured: /mnt/projects/EverQuestP1999/
     * UI_Obian_project1999.ini carries `[OptionsWindow] … Show=0`.
     */
    bool isOpen = false;

    /** Which of OPTW_OptionsSubwindows' seven <Pages> is shown. */
    int activeTab = 0;

    // --- The three wired settings, on the oracle's own 0..20 scale ---
    // The defaults are fable's own shipped values expressed on that scale,
    // and OptionsPanelDefaultsMatchTheShippedSettings pins them:
    //   music   0.7 (bridge_component.h m_audio_config_music_volume)   -> 14
    //   effects 1.0 (bridge_component.h m_audio_config_effects_volume) -> 20
    //   render  300 (config/display_settings.json renderDistance)      ->  5
    int musicVolume = 14;
    int soundVolume = 20;
    int clipPlane = 5;

    int hoveredTab = -1;
    int hoveredControl = -1;
};

/** Does the window draw this frame? The toggle alone. */
bool optionsWindowIsVisible(const OptionsPanelState& state);

/** The 0..20 step behind a wired setting (0 for `None`). */
int optionsSettingStep(const OptionsPanelState& state,
                       OptionsLayout::Setting setting);

/** Set a wired setting's step, clamped to 0..20. No-op for `None`. */
void setOptionsSettingStep(OptionsPanelState& state,
                           OptionsLayout::Setting setting, int step);

/** A slider's readout, the oracle's own `value x 5` percentage. */
std::string optionsSettingText(const OptionsPanelState& state,
                               OptionsLayout::Setting setting);

/**
 * The clip-plane slider's step <-> fable's render distance. See the flagged
 * mapping in the header comment: step*50 + 50, clamped to 0..20.
 */
float optionsClipPlaneDistance(int step);
int optionsClipPlaneStep(float distance);

/**
 * Volume step <-> the 0..1 float AudioManager takes. Thin wrappers over
 * eqt::audio::volumeFractionFromStep / volumeStepFromFraction
 * (include/client/audio/volume_steps.h) -- T439 moved the arithmetic there
 * because the GAME thread now needs it too, and options_layout.h's
 * `irr::s32` cannot be included game-side.
 */
float optionsVolumeFraction(int step);
int optionsVolumeStep(float fraction);

/**
 * T439 (board B432): the applied volumes coming back from the audio manager.
 * This is what stops the two sliders drifting -- a /musicvolume, the config
 * apply and a slider drag all land here, so the panel shows what the audio
 * manager HOLDS rather than what the renderer last sent it. Returns true if
 * either slider moved.
 */
bool applyVolumesChanged(OptionsPanelState& state,
                         const eqt::state::VolumesChangedData& data);

/** Render the window. Only when optionsWindowIsVisible(). */
void renderOptionsWindow(UIRenderer& ui, const UILayout& layout,
                         const OptionsPanelState& state);

/** Hit-test the tab strip; returns 0..6 or -1. -1 for a closed window. */
int hitTestOptionsTab(const UILayout& layout, const OptionsPanelState& state,
                      int mouseX, int mouseY);

/**
 * Hit-test the active page's controls; returns an index into
 * `OptionsLayout::kControls` or -1.
 *
 * A control whose `setting` is `None` is NOT hit-testable and returns -1 —
 * that is the T363 rule made mechanical, so a greyed control can never be
 * clicked into doing something.
 */
int hitTestOptionsControl(const UILayout& layout, const OptionsPanelState& state,
                          int mouseX, int mouseY);

/** Is the point anywhere inside the window? Used to consume the click. */
bool optionsWindowContains(const UILayout& layout,
                           const OptionsPanelState& state, int mouseX,
                           int mouseY);

}  // namespace Graphics
}  // namespace EQT
