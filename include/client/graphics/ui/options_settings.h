/*
 * The Options window's SETTINGS -- what each wired control drives, under
 * which oracle ini key it persists, on which scale, and with what default
 * (B430/B431/B433, issues #144/#145/#146).
 *
 * T434 drew all of EQUI_OptionsWindow.xml (options_layout.h) and wired the
 * three settings fable had; this header is the table every later control
 * is added through. One row per setting, walked by:
 *   - the draw (a checkbox's state, a combobox's text, a list's rows),
 *   - the click resolver (options_input.cpp: toggle / cycle / step),
 *   - the persistence (ui_preferences.cpp walks it for `[Options]` keys),
 *   - `/option` and `/filter` (options_command.cpp).
 * So a new setting is ONE row here plus its arm in
 * FrameComponent::applyOptionsSetting() -- nothing else learns its name.
 *
 * WHO OWNS A VALUE (`Owner`):
 *   Legacy   the three T434 sliders. Their values stay in OptionsPanelState's
 *            own members (musicVolume/soundVolume/clipPlane) and in
 *            UiPreferences' kOptions table, exactly as T434/T439 left them.
 *   Renderer the value lives in OptionsPanelState::values and is persisted
 *            per character in UI_<Char>_<server>.ini `[Options]` under the
 *            oracle's own key (the same flagged one-writer deviation T434
 *            recorded: the oracle keeps these in eqclient.ini).
 *   Game     the SERVER is the authority (player-profile fields). The
 *            renderer asks for a change with GameOptionIntent and shows what
 *            the game thread publishes back; nothing is written locally.
 *   Marker   not a value: the control is wired to a list selection or an
 *            action (e.g. OFP_FilterList selects the row that
 *            OFP_FilterComboBox then changes).
 *
 * Device-free: no Irrlicht type appears here.
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "client/game_options.h"

namespace EQT {
namespace Graphics {
namespace OptionsLayout {

/**
 * The fable setting a control drives. `None` means "fable has no such
 * setting" -- the control is drawn greyed and is not hit-testable (the T363
 * rule), and options_classification.cpp says why.
 */
enum class Setting : uint8_t {
    None = 0,
    // --- T434: the three legacy sliders (Owner::Legacy) ---
    MusicVolume,   // AudioManager music volume, via MusicVolumeChangeIntent
    SoundVolume,   // AudioManager effects volume, via EffectsVolumeChangeIntent
    ClipPlane,     // IrrlichtRenderer::setRenderDistance()

    // --- General page (B430) ---
    Anonymous,         // Game: AnonStatus == 1
    Roleplaying,       // Game: AnonStatus == 2
    AutoSplit,         // Game: PlayerProfile.autosplit
    AutoConsentGroup,  // Game
    AutoConsentRaid,   // Game
    AutoConsentGuild,  // Game
    PetWindowPopup,    // Renderer: open the pet window when a pet arrives

    // --- Filters page (B433): the classic /filter set, eqstr_us.txt 1354-1360 ---
    FilterSelection,   // Marker: OFP_FilterList's selected row / the combobox
    FilterBadWords,
    FilterGuildChat,
    FilterSocials,
    FilterGroupChat,
    FilterShouts,
    FilterAuctions,
    FilterOOC,
    FilterMyMisses,
    FilterOthersMiss,
    FilterOthersHit,
    FilterMissedMe,
    FilterPCSpells,
    FilterNPCSpells,
    FilterBardSongs,
    FilterDamageShields,
    FilterSpellCrits,
    FilterMeleeCrits,

    // --- Display page (B431) ---
    PCNames,           // Renderer: EntityRenderer::setNameTagKindsVisible
    NPCNames,          // Renderer
    TargetRing,        // Renderer: OverlaysComponent's 3D target ring (B994)
    // B995: Display > Particles (particle_options.h). Spell and Env pages;
    // the Player (Actor) page has no particles behind it (N3).
    ParticlesTab,          // Marker: ODP_ParticlesTabs' sub-tab strip
    SpellParticleOpacity,  // Renderer, 0..20 -> SpellParticleOpacity 0..1
    SpellParticleDensity,  // Renderer, Off/Low/Medium/High
    SpellParticleNearClip, // Renderer, Near/Medium/Far/Farthest
    // B1165 (#484): "On For:" -- SpellParticleCastFilter, the value table at
    // eqgame DAT_00793b4c (1/2/4/8/17/18/20/24), decoded by FUN_00491cff
    // (include/client/spell/spell_fx_options.h).
    SpellParticleCastFilter,
    // B1165: [Defaults] ShowSpellEffects, the /showspelleffects switch (no
    // Options-window control in the oracle either); 0 = no spell particles.
    ShowSpellEffects,
    EnvParticleOpacity,
    EnvParticleDensity,
    EnvParticleNearClip,

    // --- Mouse page (B433) ---
    MouseInvertY,      // Renderer: PlayerCameraComponent MouseOptions
    MouseLookSpring,
    MouseLook,
    MouseWheelZoom,
    MouseSensitivity,

    // --- Keys page (B433): Markers over HotkeyManager (options_keys.h) ---
    KeysCategory,        // OKP_KeyboardFilterCombobox: which category is listed
    KeysAssignment,      // OKP_KeyboardAssignmentList: click a slot to rebind
    KeysReloadDefaults,  // OKP_DefaultKeysButton: back to the shipped bindings

    // --- Colors page (B993): Markers over OptionsPanelState::textColors ---
    TextColorList,       // OCP_ColorChooseList: selects the row Change edits
    TextColorReset,      // OCP_UserColorDefault: back to eqclient.ini's table
    TextColorChange,     // OCP_UserColorChange: opens the ColorPickerWnd
    TextColors,          // no control: "the table changed" (picker Accept, /option textcolor)

    Count
};

constexpr int kSettingCount = static_cast<int>(Setting::Count);

enum class Owner : uint8_t { None, Legacy, Renderer, Game, Marker };

/** How a value is shown and changed. */
enum class ValueKind : uint8_t {
    None,
    Step,    // a 0..maxValue slider step
    Bool,    // a checkbox, 0/1
    Choice,  // a combobox: one of `choices[].value`, cycled in list order
};

struct Choice {
    int value;
    const char* text;
};

struct SettingInfo {
    Setting setting;
    Owner owner;
    ValueKind kind;
    /** `/option` name (and `/filter` name for a filter). Lower case. */
    const char* name;
    /** The `[Options]` key it persists under (Renderer owner), else nullptr. */
    const char* iniKey;
    int defaultValue;
    int maxValue;                 // Step: max step; Bool: 1; Choice: unused
    const Choice* choices;        // Choice only
    int choiceCount;              // Choice only
    /** OP_SetServerFilter slot for a filter (EQT::ServerFilterSlot), else -1. */
    int serverFilterSlot;
    /** The game option a Game-owned setting maps onto. */
    eqt::GameOption gameOption;
    /** Display text: a filter's Options-list description, else the caption. */
    const char* displayName;
    /** Where the key / scale / default / text is quoted from. */
    const char* source;
    /**
     * B995: the oracle keeps some settings as FLOATS (`1.000000`). A Step
     * setting with iniDivisor > 0 persists value / iniDivisor; a Choice
     * setting with iniFloats persists iniFloats[the choice's list index].
     * Both read back to the nearest value. Neither set = an integer key.
     */
    float iniDivisor = 0.0f;
    const float* iniFloats = nullptr;
    /**
     * B1165: a Choice setting whose int the oracle keeps RAW from the ini
     * (no validation; SpellParticleCastFilter, eqgame.exe.c:132638): any int
     * is a legal value, and the combobox shows the index of the value in the
     * list, the LAST entry when it is not listed (0x53b9e9..0x53ba07).
     */
    bool rawIniInt = false;
};

/** B995: the ini text of `v` for setting `s` (a float in the oracle's format, or an int). */
std::string settingIniText(Setting s, int v);
/** B995: parse ini text for `s`; false (and `out` untouched) if it names no legal value. */
bool parseSettingIniText(Setting s, const std::string& text, int& out);

/** The row for `s`. Every Setting has exactly one (pinned by a test). */
const SettingInfo& settingInfo(Setting s);

/** All rows, in Setting order. */
const SettingInfo* allSettings();

/** Find by `/option` name, case-insensitive; nullptr if none. */
const SettingInfo* findSettingByName(const std::string& name);

/** The values of every Renderer- and Game-owned setting, indexed by Setting. */
using OptionValues = std::array<int, kSettingCount>;

/** Every setting at its table default. */
OptionValues defaultOptionValues();

/** Is `v` a legal value for `s` (range for Step/Bool, a listed choice for Choice)? */
bool isValidSettingValue(Setting s, int v);

/** The next choice after `v` in list order (wrapping); `v` itself if not Choice. */
int nextChoiceValue(Setting s, int v);

/** The text of choice `v`, or "" if `v` is not one of `s`'s choices
 *  (a rawIniInt setting: the text of choiceDisplayIndex). */
const char* choiceText(Setting s, int v);

/** B1165: the list index the combobox shows for `v`: its index, else -1; a
 *  rawIniInt setting falls back to the LAST index (0x53b9e9..0x53ba07). */
int choiceDisplayIndex(Setting s, int v);

// --- B1165 (MR !318 note 7756): the oracle APPLIES the loader's clamped ini
// float (SpellParticleDensity / Opacity / NearClipPlane and the Env page's,
// eqgame.exe.c:132608-132636 store it in _DAT_00985810.. as read; 0x495f29
// pushes it unrounded into CreateEmitter); only the combobox DISPLAY rounds it
// to a choice (0x53b93f..0x53b9da). The int slot of such a setting is that
// display choice; this array carries the applied float when the ini gave one
// the choice does not represent (nullopt = the choice's own table float). A
// change through the window or /option stores the table float again, as the
// oracle's handler does (eqgame.exe.c:245163-245204), so it clears the slot.
using OptionIniFloats = std::array<std::optional<float>, kSettingCount>;

/** Does `s` persist as a float (Step with iniDivisor, Choice with iniFloats)? */
bool isIniFloatSetting(Setting s);
/** The table float of value `v` (step / divisor, or the choice's iniFloats entry). */
float settingTableFloat(Setting s, int v);
/** The float the client APPLIES for `s`: `raw` when set, else settingTableFloat(s, v). */
float appliedSettingFloat(Setting s, int v, const std::optional<float>& raw);
/** The loader's clamp for `s` (eqgame.exe.c:132608-132637): near clip to [1, 8], else [0, 1]. */
float clampSettingIniFloat(Setting s, float f);
/**
 * Parse a float setting's ini text: `display` = the choice / step the window
 * shows, `applied` = the clamped float itself (nullopt when it equals the
 * display's table float). false (outputs untouched) for junk or a non-float setting.
 */
bool parseSettingIniFloat(Setting s, const std::string& text, int& display,
                          std::optional<float>& applied);
/** The ini text for `s`: the raw float when set, else settingIniText(s, v). */
std::string settingIniText(Setting s, int v, const std::optional<float>& raw);

// --- The Filters page ---

/** The era filters, in the order OFP_FilterList draws them. */
constexpr int kFilterCount = 17;
Setting filterAt(int row);   // Setting::None out of range
int filterRow(Setting s);    // -1 if `s` is not a filter

/**
 * Is a filter's current value a HIDE value? `/filter <name>` toggles between
 * the filter's Show value and its Hide value (assumption awaiting oracle,
 * see options_settings.cpp).
 */
bool filterHides(Setting s, int value);
/** The eqstr 1316-1353 name `/filter` replies with ("Bad Words Filter", ...). */
const char* filterSlashText(Setting s);
int filterShowValue(Setting s);
int filterHideValue(Setting s);

/**
 * The OP_SetServerFilter payload for these values: every era filter's slot
 * carries its value, every other slot its "show" value (T243's payload).
 */
eqt::ServerFilterPayload serverFilterPayload(const OptionValues& v);

}  // namespace OptionsLayout
}  // namespace Graphics
}  // namespace EQT
