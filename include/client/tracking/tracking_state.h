/*
 * B895 (GitLab #211) -- the Tracking skill's client-side rules, device-free.
 *
 * WHAT WAS MISSING. `/useskill tracking` sent OP_Track and the reply was
 * printed as "name - N units" chat lines; there was no Tracking Window, no
 * filter/sort/players setting, and the client never said where the tracked
 * target was. Everything here is pure logic over plain values so a test drives
 * it with no server, no bridge and no device (the T421/T429 composer idiom);
 * eqt::tracking::TrackingController (tracking_controller.h) is the one place
 * that binds it to the live game.
 *
 * WHERE EACH RULE COMES FROM.
 *
 *  - THE WIRE. A Titanium OP_Track reply is a bare array of
 *    `Track_Struct {uint32 entityid; float distance;}`
 *    (akk-stack/code/common/patches/titanium_structs.h), built by
 *    EntityList::MakeTrackPacket (zone/entity.cpp) in SERVER order: newest
 *    spawn first, every trackable mob within skill*class-multiplier (min 300)
 *    units, distance measured without Z. The client resolves names and levels
 *    from its OWN spawn list -- the reply carries neither.
 *
 *  - THE DIRECTION LINES are the client's, not the server's. eqemu runs
 *    Client::DoTracking() only for SoD and later (zone/client_process.cpp:105,
 *    `ClientVersion() >= SoD`), so a Titanium client is expected to say them
 *    itself. The strings are its own eqstr_us.txt rows 12676-12680, with
 *    12674 "right" / 12675 "left" for `%2`. The BUCKETS are DoTracking's,
 *    transcribed: relative heading on the 512-unit circle, >480 or <=32
 *    straight ahead, then 64-unit sectors. DirectionTo() works in CLIENT
 *    coordinates (docs/CLAUDE.md): the player's heading is HeadingDeg()
 *    (client degrees, = 90 - server degrees, zone_entry.cpp's load-bearing
 *    flip) and the bearing is atan2(dx, dy) on client x/y, the same formula
 *    MovementComponent::CalculateHeading() and /face use. Substituting the
 *    flip into DoTracking's `my_server - target_server` gives
 *    `target_client - my_client`, which is what is computed.
 *
 *  - THE CADENCE is MEASURED: the official client printed a line every
 *    ~2 s (review/UIGAP-ORACLE/211/cadence.txt: n=36, min 2, max 3, mean
 *    2.09 s; tracking.tsv row 7), and one IMMEDIATELY after "You begin
 *    tracking" (row 6). eqemu's TrackingTimer(2000) agrees.
 *
 *  - THE SETTINGS. /trackfilter, /tracksort and /trackplayers and their
 *    confirmation and usage lines are eqstr 13140-13164; the combobox items
 *    are 13120 "normal", 13144-13147, and 12947 "on" / 12946 "off" / 13148
 *    "Group". Filters are NPC filters ("You will see %1 NPCs when tracking",
 *    13163), so they never hide a player; players obey /trackplayers alone.
 *
 * CON COLOURS come from eqt::ConTierForLevels() -- the client's one level
 * table (include/client/con_tier.h), not a second copy.
 */

#pragma once

#include "client/con_tier.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace EQT {
class StringDatabase;
}

namespace eqt {
namespace tracking {

// TRW_Filter*Button, in EQUI_TrackingWnd.xml's element order (R Y W B L G).
enum class TrackFilter : uint8_t {
    Red = 0, Yellow, White, Blue, LightBlue, Green,
};
constexpr int kFilterCount = 6;

// TRW_TrackSortCombobox items, in /tracksort's own FORMAT order (13154).
enum class TrackSort : uint8_t {
    Normal = 0, Distance, Consider, RDistance, RConsider,
};
constexpr int kSortCount = 5;

// TRW_TrackPlayersCombobox items, in /trackplayers' FORMAT order (13143).
enum class TrackPlayers : uint8_t {
    On = 0, Off, Group,
};
constexpr int kPlayersCount = 3;

// The slash vocabulary the action layer forwards (registry 2).
enum class TrackingCommand : uint8_t {
    Filter = 0,   // /trackfilter <colour>
    Sort,         // /tracksort <type>
    Players,      // /trackplayers <on|off|group>
    Stop,         // /stoptracking
    List,         // /tracklist (fable's own, headless-assertable)
};

// DoTracking's eight sectors.
enum class TrackDirection : uint8_t {
    StraightAhead = 0, AheadRight, Right, BehindRight,
    Behind, BehindLeft, Left, AheadLeft,
};

// --- eqstr ids -------------------------------------------------------------
constexpr uint32_t kStrBeginTracking = 12040;    // You begin tracking %1.
constexpr uint32_t kStrNoTrackingTarget = 12499; // You have lost or do not have a tracking target.
constexpr uint32_t kStrRight = 12674;
constexpr uint32_t kStrLeft = 12675;
constexpr uint32_t kStrStraightAhead = 12676;    // %1 is straight ahead.
constexpr uint32_t kStrAheadAndTo = 12677;       // %1 is ahead and to the %2.
constexpr uint32_t kStrToThe = 12678;            // %1 is to the %2.
constexpr uint32_t kStrBehindAndTo = 12679;      // %1 is behind and to the %2.
constexpr uint32_t kStrBehindYou = 12680;        // %1 is behind you.
// MEASURED (review/UIGAP-ORACLE/211/tracking.tsv row 8): losing the target
// printed 12102 "You have lost your target." -- NOT 12681 "You have lost your
// tracking target.", which the issue named and the official client did not
// print. 12681 is therefore not drawn.
constexpr uint32_t kStrLostTarget = 12102;       // You have lost your target.
constexpr uint32_t kStrPlayersUsage = 13143;
constexpr uint32_t kStrSortUsage = 13154;
constexpr uint32_t kStrFilterUsage = 13162;
constexpr uint32_t kStrWillSee = 13163;          // You will see %1 NPCs when tracking.
constexpr uint32_t kStrWillNotSee = 13164;       // You will NOT see %1 NPCs when tracking.

/** 13140..13142 "Track players * ON * / OFF / GROUP ONLY". */
uint32_t PlayersConfirmStringId(TrackPlayers p);
/** 13149..13153 "Track Sort type set to ...". */
uint32_t SortConfirmStringId(TrackSort s);
/** The colour word for %1 of 13163/13164 (13156..13161). */
uint32_t FilterWordStringId(TrackFilter f);
/** Combobox item text: 13120, 13144..13147. */
uint32_t SortOptionStringId(TrackSort s);
/** Combobox item text: 12947 on, 12946 off, 13148 Group. */
uint32_t PlayersOptionStringId(TrackPlayers p);
/** Every eqstr id this feature draws, for the shipped-db census test. */
const std::vector<uint32_t>& TrackingEqStrIds();

// --- the list ----------------------------------------------------------------

/** One OP_Track row, resolved against the client's spawn list. */
struct TrackEntry {
    uint16_t spawnId = 0;
    std::string name;
    uint8_t level = 0;
    float distance = 0.0f;       // recomputed locally (2D, as the server does)
    bool isPlayer = false;
    bool isGroupMember = false;
    ConTier con = ConTier::White;
    uint32_t serverOrder = 0;    // position in the reply: "normal" sort
};

/**
 * Which filter button governs a con tier. The shared table has no dark-blue
 * tier, so TrackFilter::Blue governs nothing it produces; GRAY has no button
 * of its own and is governed by Green (ASSUMPTION, awaiting oracle).
 */
TrackFilter FilterForConTier(ConTier tier);

struct TrackingSettings {
    // true = the colour is shown. Default: all shown (assumption).
    std::array<bool, kFilterCount> filters{{true, true, true, true, true, true}};
    TrackSort sort = TrackSort::Normal;
    TrackPlayers players = TrackPlayers::On;
};

/**
 * The rows TRW_TrackingList shows: NPCs pass their con colour's filter,
 * players pass /trackplayers (Group = group members only), then the sort.
 * Every sort is STABLE over the server order, so ties keep "normal" order.
 * consider = hardest first (red .. gray) -- MEASURED, tracking.tsv row 3;
 * rconsider the reverse; distance = nearest first; rdistance the reverse
 * (the distance direction and both r* sorts: assumption, not captured).
 */
std::vector<TrackEntry> VisibleRows(const std::vector<TrackEntry>& entries,
                                    const TrackingSettings& settings);

// --- slash-argument parsing (case-insensitive, the FORMAT lines' words) -----
std::optional<TrackSort> ParseTrackSort(const std::string& arg);
std::optional<TrackPlayers> ParseTrackPlayers(const std::string& arg);
std::optional<TrackFilter> ParseTrackFilter(const std::string& arg);

// --- direction -----------------------------------------------------------------

/** Interval between direction lines: measured ~2 s (cadence.txt, mean 2.09). */
constexpr float kDirectionIntervalSec = 2.0f;

/**
 * DoTracking's relative heading, 0..512, in client coordinates: player at
 * (px,py) facing headingDeg (client degrees), target at (tx,ty).
 */
float RelativeHeading512(float headingDeg, float px, float py, float tx, float ty);

/** The sector for a relative heading (DoTracking's thresholds, verbatim). */
TrackDirection DirectionForRelative(float relative512);

inline TrackDirection DirectionTo(float headingDeg, float px, float py, float tx, float ty) {
    return DirectionForRelative(RelativeHeading512(headingDeg, px, py, tx, ty));
}

/** The chat line for `dir` (12676-12680 with 12674/12675), "" on a table miss. */
std::string DirectionLine(const EQT::StringDatabase& strings, const std::string& name,
                          TrackDirection dir);

}  // namespace tracking
}  // namespace eqt
