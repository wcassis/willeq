/*
 * Per-character UI preferences -- the ONE thing fable persists for a player
 * (T432, board B275).
 *
 * Until this existed the client persisted NOTHING a player changed: every
 * window's open/closed state and the camera mode were re-defaulted at every
 * start (B275, found by T402). Will's ruling (docs/decisions.md 2026-09-07)
 * is to persist per character exactly what the user can change -- the window
 * open/closed states and the camera mode. Window POSITIONS are static by
 * design here (UILayout computes every rect from the resolution) so there is
 * nothing to persist for them, and key bindings are global
 * (config/hotkeys.json), not per character.
 *
 * SHAPE: the official client writes `UI_<Char>_<server>.ini` next to its
 * executable and records a window's visibility as `Show=0`/`Show=1` under
 * that window's own section. Measured, not inferred:
 *
 *     /mnt/projects/EverQuestP1999/UI_Obian_project1999.ini
 *       [ActionsWindow] ... Show=1
 *       [OptionsWindow] ... Show=0
 *       [InventoryWindow] ...            <- no Show=, i.e. shown
 *     /mnt/projects/EverQuestP1999/defaults.ini
 *       Show=0 under [HotButtonWnd3], [HotButtonWnd4], [SelectorWindow] only
 *
 * So an ABSENT `Show=` key means "leave the default alone" (T368's rule), and
 * that is exactly how ParseUiPreferences() treats it: it starts from fable's
 * own defaults and only overwrites what the file states. A brand-new
 * character therefore has no file, gets the defaults, and its first toggle
 * writes one.
 *
 * WHERE: `<cache dir>/ui/UI_<Char>_<server>.ini`, resolved through
 * eqt::ResolveCachePathForRead/Write (T402's rule, board B248) -- generated
 * state is written OUTSIDE the checkout, so a plain run never dirties the
 * working tree. It is not shipped data and never goes through
 * ResolveDataPath().
 *
 * The section names below are the official client's own, so a fable file and
 * an official one describe the same windows with the same words. fable's
 * combat-abilities window has no section in `defaults.ini` (T415 flagged
 * that); `[CombatAbilityWnd]` is the official WINDOW name and is used here.
 */

#pragma once

#include <string>

namespace EQT {
namespace Graphics {

struct InventoryPanelState;
struct SpellbookPopupState;
struct SkillsPopupState;
struct GroupPanelState;
struct PetPanelState;
struct BuffBarState;
struct CombatAbilityPanelState;

/**
 * The persisted per-character state. Every field is user-changeable, and the
 * defaults are the ones the panel structs themselves declare -- the two must
 * agree, and UiPreferencesTest.DefaultsMatchThePanelStructs asserts they do.
 */
struct UiPreferences {
    bool inventoryWindow = false;    // [InventoryWindow]
    bool spellBookWnd = false;       // [SpellBookWnd]
    bool skillsWindow = false;       // [SkillsWindow]
    bool groupWindow = true;         // [GroupWindow]
    bool petInfoWindow = true;       // [PetInfoWindow]
    bool buffWindow = true;          // [BuffWindow]
    bool combatAbilityWnd = false;   // [CombatAbilityWnd]
    // [Camera] Mode= -- the CameraMode enumerator (0 Free, 1 Follow,
    // 2 FirstPerson). Held as an int so this header stays free of
    // renderer_types.h; PlayerCameraComponent's default is Follow.
    int cameraMode = 1;

    bool operator==(const UiPreferences& o) const {
        return inventoryWindow == o.inventoryWindow
            && spellBookWnd == o.spellBookWnd
            && skillsWindow == o.skillsWindow
            && groupWindow == o.groupWindow
            && petInfoWindow == o.petInfoWindow
            && buffWindow == o.buffWindow
            && combatAbilityWnd == o.combatAbilityWnd
            && cameraMode == o.cameraMode;
    }
    bool operator!=(const UiPreferences& o) const { return !(*this == o); }
};

/**
 * The renderer state this unit persists, bound by pointer. FrameComponent
 * builds one over its own panel members; a test builds one over plain
 * structs, which is what keeps Collect/Apply device-free. A null member is
 * skipped, so a partial binding is legal.
 */
struct UiPreferencesBinding {
    InventoryPanelState* inventory = nullptr;
    SpellbookPopupState* spellbook = nullptr;
    SkillsPopupState* skills = nullptr;
    GroupPanelState* group = nullptr;
    PetPanelState* pet = nullptr;
    BuffBarState* buff = nullptr;
    CombatAbilityPanelState* combatAbility = nullptr;
};

/** Read the bound panel states (plus the caller's camera mode) into a value. */
UiPreferences CollectUiPreferences(const UiPreferencesBinding& b, int cameraMode);

/**
 * Write a value back into the bound panel states. The camera mode is NOT
 * applied here -- it lives on PlayerCameraComponent behind setCameraMode(),
 * so the caller reads `p.cameraMode` and calls that.
 */
void ApplyUiPreferences(const UiPreferences& p, const UiPreferencesBinding& b);

/**
 * `UI_<Char>_<server>.ini`, the official client's own filename shape. Both
 * parts are sanitised to `[A-Za-z0-9_-]` (anything else becomes `_`, an empty
 * part becomes `unknown`), so a name out of a packet can never walk out of
 * the cache directory.
 */
std::string UiPreferencesFileName(const std::string& character,
                                  const std::string& server);

/** The `ui/<file>` name handed to the cache-path resolvers. */
std::string UiPreferencesCacheName(const std::string& character,
                                   const std::string& server);

/**
 * Parse the official `[Section] Show=0/1` shape. Starts from `defaults` and
 * overwrites only the keys the text states, so an absent key keeps its
 * default exactly as `defaults.ini` states a default by omission. Unknown
 * sections and keys are ignored -- an official file carries dozens of
 * position/size/tint keys fable has no use for and must not choke on.
 */
UiPreferences ParseUiPreferences(const std::string& text,
                                 const UiPreferences& defaults = UiPreferences{});

/** Serialise to the same shape. Round-trips through ParseUiPreferences(). */
std::string SerializeUiPreferences(const UiPreferences& p);

/**
 * THE read. Returns false (leaving `out` untouched) when the character has no
 * file yet -- a fresh character is not an error, it just keeps the defaults.
 */
bool LoadUiPreferences(const std::string& character, const std::string& server,
                       UiPreferences& out);

/**
 * THE write. Returns false when no cache directory is writable, which is not
 * an error either (the same rule the sound/graphics index caches follow).
 */
bool SaveUiPreferences(const std::string& character, const std::string& server,
                       const UiPreferences& p);

}  // namespace Graphics
}  // namespace EQT
