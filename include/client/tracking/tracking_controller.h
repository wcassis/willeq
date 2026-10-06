/*
 * B895 (GitLab #211) -- the Tracking skill, bound to the live game.
 *
 * ONE owner of the tracking list, the three window settings and the tracked
 * target, on the GAME thread (PlayerComponent holds it; Tracking() reaches
 * it). Every path into the feature lands here, so a slash command and a click
 * share one body:
 *
 *   OP_Track reply  --------------------> OnTrackReply()    (opens the window)
 *   /track [name], TRW_TrackButton -----> BeginTracking()   (OP_TrackTarget + 12040)
 *   /trackfilter, TRW_Filter*Button ----> SetFilter()       (13163 / 13164)
 *   /tracksort, TRW_TrackSortCombobox --> SetSort()         (13149..13153)
 *   /trackplayers, TRW_TrackPlayers... -> SetPlayers()      (13140..13142)
 *   /stoptracking ----------------------> StopTracking()
 *   /tracklist -------------------------> PrintList()       (fable's own; headless)
 *   the game tick ----------------------> Tick()            (12676..12680, 12102)
 *
 * Each change publishes GameEventType::TrackingListChanged with the whole
 * filtered, sorted, con-coloured list. The rules themselves are the pure
 * functions in tracking_state.h.
 */

#pragma once

#include "client/tracking/tracking_state.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {
namespace Net {
class Packet;
}
}

struct Entity;

namespace eqt {
struct ClientContext;
namespace events {
struct TrackingIntent;
}

namespace tracking {

class TrackingController {
public:
    using Clock = std::chrono::steady_clock;

    explicit TrackingController(eqt::ClientContext& ctx) : ctx_(ctx) {}

    /** The OP_Track reply: Track_Struct[] {uint32 entityid; float distance;}. */
    void OnTrackReply(const EQ::Net::Packet& p);
    /** The same, already decoded (entity id, server distance) in server order. */
    void OnTrackEntries(const std::vector<std::pair<uint32_t, float>>& raw);

    /** Start tracking `spawnId`: OP_TrackTarget, then "You begin tracking %1." */
    void BeginTracking(uint16_t spawnId);
    /** /stoptracking. */
    void StopTracking();

    void SetFilter(TrackFilter f, bool shown);
    void SetSort(TrackSort s);
    void SetPlayers(TrackPlayers p);
    /** /tracklist: the visible rows as chat lines. */
    void PrintList();

    /** The action layer's entry: parse `arg` and route (usage line on a bad arg). */
    void Command(TrackingCommand cmd, const std::string& arg);
    /** The window's entry (TrackingIntent). */
    void ApplyIntent(const eqt::events::TrackingIntent& intent);

    /** Game tick: one direction line per kDirectionIntervalSec while tracking. */
    void Tick(Clock::time_point now);
    void Tick() { Tick(Clock::now()); }

    // Read-only views, for /tracklist and the tests.
    uint16_t TrackedSpawnId() const { return tracked_; }
    const TrackingSettings& Settings() const { return settings_; }
    /** The rows the window would show right now (distances recomputed). */
    std::vector<TrackEntry> CurrentRows();

private:
    void Refresh();               // re-resolve entries against the spawn list
    void Publish(bool openWindow);
    void Say(const std::string& line);
    void SayDirection(const Entity& target);

    eqt::ClientContext& ctx_;
    TrackingSettings settings_;
    std::vector<TrackEntry> entries_;
    uint16_t tracked_ = 0;
    std::string trackedName_;
    Clock::time_point nextDirection_{};
};

}  // namespace tracking
}  // namespace eqt
