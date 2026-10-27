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
 * WHAT T434 WIRED (B430/B433 wired more -- see options_settings.h and
 * options_classification.cpp for the live census).
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
#include "client/graphics/ui/color_picker.h"  // B993
#include "client/chat/chat_text_colors.h"     // B993
#include "client/graphics/environment/particle_options.h"  // B995
#include "client/spell/spell_fx_options.h"                  // B1165
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
    // Effective renderer distance, not a persisted slider step. The frame
    // refreshes it after applying settings and when a zone cap changes.
    float appliedClipPlane = 300.0f;
    // B1100: the slider whose press this window owns until release.
    int draggedSlider = -1;
    bool leftHeld = false;  // Physical button state sampled before UI rendering.

    // --- B430/B433: every other wired setting, indexed by Setting ---
    // Renderer-owned values are persisted; Game-owned ones mirror what the
    // game thread last published (applyGameOptionChanged). The three legacy
    // sliders above do NOT live here -- their slots are unused.
    OptionsLayout::OptionValues values = OptionsLayout::defaultOptionValues();
    // B1165: the applied ini float of a particle float setting whose `values`
    // slot only holds the choice the window shows (options_settings.h
    // OptionIniFloats); nullopt = the choice's table float.
    OptionsLayout::OptionIniFloats iniFloats{};

    /** The Filters page: OFP_FilterList's selected row (0..kFilterCount-1). */
    int filterSelectedRow = 0;

    // --- The Keys page (B433, options_keys.h) ---
    int keysCategory = 0;        // OKP_KeyboardFilterCombobox
    int keysCaptureAction = -1;  // a HotkeyAction waiting for its key, or -1
    int keysCaptureSlot = 0;     // 0 Keypress, 1 Alternate

    // --- The Colors page (B993, options_colors.cpp) ---
    // The player's [TextColors] table; the chat panel draws from it by
    // pointer (ChatPanelState::textColors) and UiPreferences persists it.
    eqt::ui::ChatTextColors textColors = eqt::ui::defaultChatTextColors();
    int colorSelectedRow = 1;    // OCP_ColorChooseList's selection, User_N (1-based)
    int colorListTop = 1;        // the first User_N the list shows (VScroll)
    ColorPickerState colorPicker;  // EQUI_ColorPickerWnd

    // --- Display > Particles (B995, options_particles.cpp) ---
    int particlesTab = 0;        // ODP_ParticlesTabs: 0 Spell, 1 Env, 2 Player

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

/**
 * B430: set any wired setting's value. A legacy slider clamps (as above);
 * every other setting REFUSES a value its table row does not allow. Returns
 * true when the stored value changed.
 */
bool setOptionsValue(OptionsPanelState& state, OptionsLayout::Setting setting,
                     int value);

/**
 * B430: a game-owned option the game thread published
 * (GameEventType::GameOptionChanged). AnonStatus fans out to BOTH the
 * Anonymous and the Roleplaying checkbox -- the server holds one value.
 * Returns true if any checkbox moved.
 */
bool applyGameOptionChanged(OptionsPanelState& state, eqt::GameOption option,
                            int value);

/**
 * B430: the game-option value a click on game-owned `setting` asks for when
 * it becomes `checked` (e.g. Roleplaying on -> AnonStatus 2, off -> 0).
 */
int gameOptionValueFor(OptionsLayout::Setting setting, bool checked);

/** The filter OFP_FilterList has selected (a Filter* Setting). */
OptionsLayout::Setting optionsSelectedFilter(const OptionsPanelState& state);

/** A slider's readout, the oracle's own `value x 5` percentage. */
std::string optionsSettingText(const OptionsPanelState& state,
                               OptionsLayout::Setting setting);

/**
 * The clip-plane slider's step <-> fable's render distance. See the flagged
 * mapping in the header comment: step*50 + 50, clamped to 0..20.
 * B606 (#189) deleted optionsClipPlaneStep() (zero callers): nothing ever
 * turned a distance back into a step -- the panel only ever stores the step.
 * The volume step <-> 0..1-float conversion is eqt::audio::volume*
 * (include/client/audio/volume_steps.h); B606 deleted the two options_-named
 * wrappers over it (zero callers -- callers use the audio-side functions).
 */
float optionsClipPlaneDistance(int step);

/**
 * T439 (board B432): the applied volumes coming back from the audio manager.
 * This is what stops the two sliders drifting -- a /musicvolume, the config
 * apply and a slider drag all land here, so the panel shows what the audio
 * manager HOLDS rather than what the renderer last sent it. Returns true if
 * either slider moved.
 */
bool applyVolumesChanged(OptionsPanelState& state,
                         const eqt::state::VolumesChangedData& data);

/**
 * B433: draw wired combobox / listbox control `i` -- its current choice, or
 * its rows (options_lists.cpp). Called by renderOptionsWindow().
 */
void renderOptionsWiredList(UIRenderer& ui, const UILayout& layout,
                            const OptionsPanelState& state, int i);

/**
 * B430: the ONE click resolver for a wired control. `i` is the control under
 * the click (hitTestOptionsControl). Toggles a checkbox, steps a slider,
 * cycles a combobox, selects a list row; returns the Setting that changed
 * (None when nothing did, e.g. a list selection or a no-op step). Every rule
 * it applies is the settings table's, so it never invents a value.
 */
OptionsLayout::Setting resolveOptionsClick(const UILayout& layout,
                                           OptionsPanelState& state, int i,
                                           int mouseX, int mouseY);

// --- The Colors page (B993, options_colors.cpp) ---

/** Draw OCP_ColorChooseList (`i`): the 86 [TextColors] rows from colorListTop. */
void renderOptionsColorList(UIRenderer& ui, const UILayout& layout,
                            const OptionsPanelState& state, int i);

/**
 * A click on one of the three Colors-page controls (`i`): select a row /
 * scroll the list, reset the table, or open the picker on the selected row.
 * Returns TextColorReset when the table changed, else None.
 */
OptionsLayout::Setting resolveOptionsColorsClick(const UILayout& layout, OptionsPanelState& state,
                                                 int i, int mouseX, int mouseY);

/** Reset every row to eqclient.ini's table. True if anything moved. */
bool resetOptionsTextColors(OptionsPanelState& state);

/** The VScroll strip of a listbox: the right 12px (fable's, flagged). */
constexpr irr::s32 LIST_VSCROLL_W = 12;

// --- Display > Particles (B995, options_particles.cpp) ---

constexpr int kParticleSubTabs = 3;  // Spell, Env, Player (ODP_ParticlesTabs' <Pages>)

/** Sub-tab `tab`'s header in tab box control `i`: the box's top TAB_H strip, split in three. */
irr::core::rect<irr::s32> particleSubTabRect(const UILayout& layout, int i, int tab);
/** The sub-tab under the point, or -1 (not in the strip). */
int particleSubTabAt(const UILayout& layout, int i, int mouseX, int mouseY);
/** Draw the wired ODP_ParticlesTabs: the box and its three sub-tabs. */
void renderOptionsParticleTabs(UIRenderer& ui, const UILayout& layout,
                               const OptionsPanelState& state, int i);
/** The Spell / Env pages' values as the particle systems' knobs (Actor: identity, N3). */
/** B1165: each float knob is the applied ini float (`iniFloats`) when one is
 *  set, else the shown choice's table float. */
Environment::ParticleKnobs particleKnobsFromOptions(const OptionsLayout::OptionValues& values,
                                                    const OptionsLayout::OptionIniFloats& iniFloats = {});
/** B1165 (#484): the Spell page + ShowSpellEffects as SpellVisualFX's options
 *  (density, opacity, near clip, On For -- the raw int --, the global switch). */
EQ::SpellFxOptions spellFxOptionsFromOptions(const OptionsLayout::OptionValues& values,
                                             const OptionsLayout::OptionIniFloats& iniFloats = {});

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
