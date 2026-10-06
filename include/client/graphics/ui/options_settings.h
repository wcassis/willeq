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
};

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

/** The text of choice `v`, or "" if `v` is not one of `s`'s choices. */
const char* choiceText(Setting s, int v);

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
