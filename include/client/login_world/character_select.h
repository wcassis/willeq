#pragma once

// B998 (GitLab #318, #204 Legs S/M): the CHARACTER SELECT screen's model --
// device-free and thread-agnostic. It is the seam between the two threads:
//
//   game thread    LoginWorldComponent holds the list (OP_SendCharInfo, T14)
//                  and the creation flow (B888). Application's
//                  serviceCharacterSelect() turns that into a
//                  CharacterSelectView and publishes it, and drains the
//                  render thread's CharacterSelectRequests into the
//                  component's own Request* bodies.
//   render thread  the screen (graphics/ui/char_select_screen.h) reads the
//                  newest view and posts requests; nothing else crosses.
//
// WHEN THE SCREEN IS SHOWN (CharacterSelectScreenWanted): in graphics mode,
// with no world script, when NO character is configured or on
// --character-select. A configured character keeps the T14 auto-select:
// OP_EnterWorld at OP_ExpansionInfo, byte for byte what it was, which is what
// every scenario, the visual gate and CI run. A --scenario run shows it only
// on the explicit flag (the scenario runner has no pre-zone input -- it does
// not Step() before IsFullyZonedIn()).
//
// What the official client shows (oracle: eq-oracle/logs/shots/cc_13_delete_confirm.png,
// diag_manual_dialog.png; review/UIGAP-ORACLE/204/00_character_select.png): an
// occupied slot's button carries the NAME; the selected character's line
// "Name [level Class]" and its zone's long name float above the 3D model.
// ComposeCharacterInfoLine() is that line's format. The race is shown only by
// the 3D model, which is OUT of this unit, so a row carries it for that leg
// and nothing draws it.

#include "client/eq_types.h"
#include "client/login_world/char_create_packets.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eqt {
namespace charcreate {
class CharCreateData;
}

namespace login_world {

/** One occupied slot, as the screen draws it. */
struct CharacterSelectRow {
    int slot = -1;              // the OP_SendCharInfo slot (0-9)
    std::string name;
    uint8_t level = 0;
    uint8_t cls = 0;
    uint32_t race = 0;          // for the 3D preview leg; drawn nowhere today
    uint32_t zone = 0;
    std::string infoLine;       // "Name [1 Warrior]"
    std::string zoneLine;       // "South Qeynos"

    bool operator==(const CharacterSelectRow& o) const {
        return slot == o.slot && name == o.name && level == o.level && cls == o.cls &&
               race == o.race && zone == o.zone && infoLine == o.infoLine && zoneLine == o.zoneLine;
    }
};

/** eqstr id -> text, resolved on the GAME thread (the string table is its). */
using EqstrTable = std::unordered_map<uint32_t, std::string>;

/** LoginWorldComponent::CharCreateStatus::Result, as the screen reads it. */
enum class CharacterSelectResult : uint8_t { None, Created, NameRefused, CreateRefused, Deleted, DeleteFailed };

/** Game thread -> render thread: everything the screen draws. */
struct CharacterSelectView {
    std::vector<CharacterSelectRow> rows;
    bool busy = false;                    // a create / delete / Get Name in flight
    CharacterSelectResult lastResult = CharacterSelectResult::None;
    std::string lastMessage;              // its eqstr text (1576 / 1581 ...)
    std::string randomName;               // the last Get Name reply
    uint32_t requestsHandled = 0;         // requests the game thread has applied
    std::shared_ptr<const EqstrTable> strings;   // built once per activation

    bool operator==(const CharacterSelectView& o) const {
        return rows == o.rows && busy == o.busy && lastResult == o.lastResult &&
               lastMessage == o.lastMessage && randomName == o.randomName &&
               requestsHandled == o.requestsHandled && strings == o.strings;
    }
    bool operator!=(const CharacterSelectView& o) const { return !(*this == o); }
};

/** Render thread -> game thread: a button the user pressed. */
struct CharacterSelectRequest {
    enum class Kind : uint8_t { EnterWorld, Delete, Create, GetName };
    Kind kind = Kind::EnterWorld;
    std::string name;                     // EnterWorld / Delete
    charcreate::CharCreateRequest create;   // Create
    uint32_t race = 0, gender = 0;        // GetName
};

/**
 * The thread-safe mailbox between the two. One mutex, held only to copy.
 * `active` says whether the render thread should draw the screen at all: the
 * game thread raises it when the first list arrives and drops it for good
 * once OP_EnterWorld is sent.
 */
class CharacterSelectChannel {
public:
    void setActive(bool on) { active_.store(on, std::memory_order_release); }
    bool active() const { return active_.load(std::memory_order_acquire); }

    void publish(const CharacterSelectView& view);
    /** Copy the view out if it is newer than `seen` (then `seen` advances). */
    bool takeViewIfNewer(uint64_t& seen, CharacterSelectView& out) const;

    void post(const CharacterSelectRequest& req);
    std::vector<CharacterSelectRequest> drain();

private:
    mutable std::mutex mutex_;
    CharacterSelectView view_;
    uint64_t generation_ = 0;
    std::vector<CharacterSelectRequest> requests_;
    std::atomic<bool> active_{false};
};

/**
 * Whether this run shows the screen. Graphics only, never under a world
 * script (--create/--delete/--list-characters own character select), and in
 * a --scenario run only on the explicit flag.
 */
bool CharacterSelectScreenWanted(const std::string& configuredCharacter, bool explicitFlag,
                                 bool graphicsEnabled, bool worldScriptRequested, bool scenarioRun);

/**
 * B1005 (#324): whether /camp (not /camp desktop) ends on this screen. Only
 * where the screen can be drawn: graphics, and no world script (a
 * --create/--delete/--list-characters run owns character select). Headless,
 * /camp keeps its exit 0 -- there is no screen to return to. The scenario
 * lane opts a run in on its own (ApplicationScenarioWorld): its scenario
 * plays the screen's part through the same game-thread service.
 */
bool CampReturnsToCharacterSelect(bool graphicsEnabled, bool worldScriptRequested);

/** "Name [level Class]" -- the line the oracle floats over the model. */
std::string ComposeCharacterInfoLine(const std::string& name, int level, const std::string& className);

/** data/era_zones.tsv: zone id -> long name (empty map if the file is missing). */
std::map<uint32_t, std::string> LoadEraZoneLongNames();

/** The rows for `list`, composed with the zone names (an unknown zone: "Zone <id>"). */
std::vector<CharacterSelectRow> BuildCharacterSelectRows(
    const std::vector<CharacterSelectEntry>& list, const std::map<uint32_t, std::string>& zoneNames);

/** Every eqstr id the select AND creation screens draw (the table to pre-resolve). */
std::vector<uint32_t> CharacterSelectEqstrIds(const charcreate::CharCreateData& data);

}  // namespace login_world
}  // namespace eqt
