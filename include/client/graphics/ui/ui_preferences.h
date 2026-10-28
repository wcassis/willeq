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
 * NOT HERE (B1175, #494): the Options settings the oracle keeps in its
 * GLOBAL eqclient.ini `[Defaults]` -- the Display > Particles keys and
 * ShowSpellEffects -- live in `<cache dir>/eqclient.ini` (eqclient_ini.h).
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

// T461: device-free, so this header still needs no renderer_types.h.
#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"
// B430: the Options settings table (device-free).
#include "client/graphics/ui/options_settings.h"
// B834: the [Tips] section (device-free).
#include "client/graphics/ui/tip_preferences.h"
// B993: the [TextColors] table (device-free).
#include "client/chat/chat_text_colors.h"

namespace EQT {
namespace Graphics {

struct InventoryPanelState;
struct SpellbookPopupState;
struct SkillsPopupState;
struct GroupPanelState;
struct PetPanelState;
struct BuffBarState;
struct CombatAbilityPanelState;
struct OptionsPanelState;
struct FriendsPanelState;

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
    // T434. `Show=0` is what the real player file records:
    // /mnt/projects/EverQuestP1999/UI_Obian_project1999.ini has
    // `[OptionsWindow] ... Show=0`.
    bool optionsWindow = false;      // [OptionsWindow]
    // B897: the oracle's own section name, /mnt/projects/EverQuestP1999/
    // defaults.ini `[FriendsWindow]` (it records positions there and no
    // Show=). Closed by default, like every other toggled window.
    bool friendsWindow = false;      // [FriendsWindow]
    // [Camera] View= -- the CameraMode enumerator, 1..6 (camera_mode.h: the
    // official client's six cameras, in eqstr_us.txt order). T461 replaced
    // T432's `Mode=` (0 Free, 1 Follow, 2 FirstPerson): the two numberings
    // collide at 1 and 2, so the KEY changed rather than the meaning of a
    // number. ParseUiPreferences() maps an old `Mode=` explicitly
    // (cameraModeFromLegacyIniMode) when the file has no `View=`; the writer
    // emits only `View=`. Held as an int, as before.
    int cameraMode = static_cast<int>(kDefaultCameraMode);
    // [Camera] User1Azimuth / User1Pitch / User1Distance / User1Height, and
    // the same four for User2 (T461). Persisted BECAUSE THE OFFICIAL CLIENT
    // PERSISTS ITS USER CAMERAS: eqclient.ini [Options] carries
    // Camera1-Distance, -DirHeading, -Heading, -Pitch, -Height, -Zoom and
    // -Change, and the same seven for Camera2.
    // DEVIATION, FLAGGED: fable's own key names and units (camera_rig.h --
    // degrees and EQ world units), because no file settles the oracle's
    // DirHeading/Pitch units (board B530); and in this file rather than
    // eqclient.ini, T432's one-writer rule again. The two fixed cameras
    // (Overhead, Tether) are not user-adjustable and are not persisted.
    CameraRig user1Camera = kUser1DefaultRig;
    CameraRig user2Camera = kUser2DefaultRig;

    // T434 -- the three Options-window controls fable can actually wire,
    // under the oracle's OWN key names and on the oracle's OWN 0..20 scale.
    //
    // The names are quoted from /mnt/projects/EverQuestP1999/eqclient.ini:
    // `Music=18` and `SoundVolume=10` (in its `[Defaults]` section) and
    // `ClipPlane=20` (in its `[Options]` section). The scale is MEASURED,
    // not assumed: the same client showing that ini displays Music Volume
    // 90 % and Sound Volume 50 % (review/T377/oracle/
    // T377_oracle_options_alt_o_1024x768.png), and 18/20 = 90 %,
    // 10/20 = 50 %.
    //
    // DEVIATION, FLAGGED: the oracle keeps these in `eqclient.ini`, a file
    // that is per-character but not per-server, and splits them across two
    // sections. fable has ONE persistence writer (T432's rule) so they go in
    // this file, in one `[Options]` section, under those key names. Recorded
    // here so nobody later reads the file as an oracle match. It is the same
    // deviation T432 flagged for `[Camera] Mode=`.
    //
    // Defaults are fable's own shipped values on that scale -- music 0.7,
    // effects 1.0, renderDistance 300 -- and
    // UiPreferencesTest.DefaultsMatchThePanelStructs pins them against
    // OptionsPanelState.
    int musicVolume = 14;    // [Options] Music
    int soundVolume = 20;    // [Options] SoundVolume
    int clipPlane = 5;       // [Options] ClipPlane

    // B430/B433: every RENDERER-owned Options setting since T434 (the
    // settings table in options_settings.h names each one's oracle key, its
    // range and its default). Written under `[Options]` beside the three
    // above, same flagged one-writer deviation. Only Renderer-owned slots
    // are ever collected, parsed or written; the rest stay at the default.
    //
    // B1175 (#494): NOT the Display > Particles keys and ShowSpellEffects
    // (SpellParticle*, EnvironmentParticle*): the oracle keeps those in
    // eqclient.ini `[Defaults]` (loader eqgame.exe.c:132608-132671 and
    // :126007), so fable keeps them in its GLOBAL `<cache dir>/eqclient.ini`
    // [Defaults] (eqclient_ini.h) -- one per install, not per character. This
    // file no longer writes them, and a value an older fable wrote here is
    // ignored on read: NOT migrated (the client has no such key in
    // `[Options]`); the global file or the default wins.
    OptionsLayout::OptionValues optionValues = OptionsLayout::defaultOptionValues();
    // B1165: the applied float of a float key whose ini value the shown
    // choice does not represent (options_settings.h OptionIniFloats). Since
    // B1175 every such key is an eqclient.ini one (EqClientIni carries it), so
    // this stays empty for the per-character file.
    OptionsLayout::OptionIniFloats optionIniFloats{};

    // B834 (#202): `[Tips]` -- the official section and keys (ShowTips,
    // ShowTOTD, <Key>=0), quoted from real player files; tip_preferences.h.
    // This one IS the oracle's own file and section.
    TipPreferences tips;

    // B993 (#313): the Colors page's chat-colour table, under the oracle's
    // own `[TextColors] User_<N>_Red/_Green/_Blue` keys (0..255). The oracle
    // keeps it in eqclient.ini; it is here for T432's one-writer rule -- the
    // same flagged deviation as `[Options]`.
    eqt::ui::ChatTextColors textColors = eqt::ui::defaultChatTextColors();

    bool operator==(const UiPreferences& o) const {
        return inventoryWindow == o.inventoryWindow
            && spellBookWnd == o.spellBookWnd
            && skillsWindow == o.skillsWindow
            && groupWindow == o.groupWindow
            && petInfoWindow == o.petInfoWindow
            && buffWindow == o.buffWindow
            && combatAbilityWnd == o.combatAbilityWnd
            && optionsWindow == o.optionsWindow
            && friendsWindow == o.friendsWindow
            && musicVolume == o.musicVolume
            && soundVolume == o.soundVolume
            && clipPlane == o.clipPlane
            && optionValues == o.optionValues
            && optionIniFloats == o.optionIniFloats
            && tips == o.tips
            && textColors == o.textColors
            && cameraMode == o.cameraMode
            && user1Camera == o.user1Camera
            && user2Camera == o.user2Camera;
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
    OptionsPanelState* options = nullptr;   // T434
    FriendsPanelState* friends = nullptr;   // B897
    TipPreferences* tips = nullptr;         // B834
};

/**
 * Read the bound panel states, plus the caller's camera mode and (T461) its
 * two user cameras, into a value.
 */
UiPreferences CollectUiPreferences(const UiPreferencesBinding& b, int cameraMode,
                                   const CameraRig& user1Camera = kUser1DefaultRig,
                                   const CameraRig& user2Camera = kUser2DefaultRig);

/**
 * Write a value back into the bound panel states. The camera mode and the
 * user cameras are NOT applied here -- they live on PlayerCameraComponent
 * behind setCameraMode() / setUserCameraRigs(), so the caller reads
 * `p.cameraMode`, `p.user1Camera` and `p.user2Camera` and calls those.
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
