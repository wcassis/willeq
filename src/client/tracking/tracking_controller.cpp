// B895 (GitLab #211): the Tracking skill bound to the live game. The rules are
// in tracking_state.cpp; this TU only gathers inputs from ClientContext, speaks
// the eqstr lines and publishes the window's feed. See tracking_controller.h.

#include "client/tracking/tracking_controller.h"

#include "client/bridge/game_state_bridge.h"
#include "client/client_context.h"
#include "client/display_eqstr.h"
#include "client/events/renderer_intents.h"
#include "client/player/player_component.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/net/packet.h"
#include "common/packet_structs.h"

#include <fmt/format.h>

#include <cmath>
#include <utility>

namespace eqt {
namespace tracking {

namespace {

// fable's own words for /tracklist (a fable command, so fable's wording).
const char* ConWord(ConTier t) {
    switch (t) {
        case ConTier::Gray:      return "gray";
        case ConTier::Green:     return "green";
        case ConTier::LightBlue: return "light blue";
        case ConTier::White:     return "white";
        case ConTier::Yellow:    return "yellow";
        case ConTier::Red:       return "red";
    }
    return "white";
}

}  // namespace

void TrackingController::Say(const std::string& line) {
    // An eqstr miss composes "" -- the oracle has no such line, print nothing.
    if (!line.empty()) ctx_.out.System(line);
}

void TrackingController::OnTrackReply(const EQ::Net::Packet& p) {
    std::vector<std::pair<uint32_t, float>> raw;
    if (p.Length() >= 2) {
        const size_t count = (p.Length() - 2) / sizeof(EQT::Track_Struct);
        raw.reserve(count);
        size_t offset = 2;  // skip the opcode
        for (size_t i = 0; i < count; ++i, offset += sizeof(EQT::Track_Struct)) {
            raw.emplace_back(p.GetUInt32(offset), p.GetFloat(offset + 4));
        }
    }
    LOG_DEBUG(MOD_ENTITY, "Track response: {} bytes, {} entries", p.Length(), raw.size());
    OnTrackEntries(raw);
}

void TrackingController::OnTrackEntries(const std::vector<std::pair<uint32_t, float>>& raw) {
    entries_.clear();
    entries_.reserve(raw.size());
    uint32_t order = 0;
    for (const auto& [id, distance] : raw) {
        TrackEntry e;
        e.spawnId = static_cast<uint16_t>(id);
        e.distance = distance;
        e.serverOrder = order++;
        entries_.push_back(e);
    }
    Refresh();
    Publish(/*openWindow=*/true);
}

void TrackingController::Refresh() {
    const int playerLevel = ctx_.game_state.player().level();
    const float px = ctx_.self.PosX();
    const float py = ctx_.self.PosY();
    std::vector<TrackEntry> kept;
    kept.reserve(entries_.size());
    for (auto e : entries_) {
        auto it = ctx_.entities.find(e.spawnId);
        // The reply carries ids only; a row the client cannot resolve from its
        // own spawn list has no name to show and nothing to track. Dropped.
        if (it == ctx_.entities.end()) continue;
        const Entity& ent = it->second;
        e.name = EQT::toDisplayName(ent.name);
        e.level = ent.level;
        e.isPlayer = (ent.npc_type == 0);
        e.isGroupMember = ctx_.game_state.group().findMemberBySpawnId(e.spawnId) >= 0;
        e.con = ConTierForLevels(playerLevel, ent.level);
        // Distance recomputed locally as the player moves, without Z -- the
        // server's own DistanceNoZ (zone/entity.cpp MakeTrackPacket).
        e.distance = std::hypot(ent.x - px, ent.y - py);
        kept.push_back(std::move(e));
    }
    entries_ = std::move(kept);
}

std::vector<TrackEntry> TrackingController::CurrentRows() {
    Refresh();
    return VisibleRows(entries_, settings_);
}

void TrackingController::Publish(bool openWindow) {
    if (!ctx_.bridge) return;
    const auto& strings = ctx_.strings.Db();
    eqt::state::TrackingListChangedData data;
    data.openWindow = openWindow;
    for (const auto& e : VisibleRows(entries_, settings_)) {
        eqt::state::TrackingRowData row;
        row.spawnId = e.spawnId;
        row.name = e.name;
        const ConRgb c = ConTierRgb(e.con);
        row.r = c.r;
        row.g = c.g;
        row.b = c.b;
        row.isPlayer = e.isPlayer;
        data.rows.push_back(std::move(row));
    }
    data.filters = settings_.filters;
    data.sort = static_cast<uint8_t>(settings_.sort);
    data.players = static_cast<uint8_t>(settings_.players);
    for (int i = 0; i < kSortCount; ++i) {
        data.sortOptions.push_back(
            EqStrLine(strings, SortOptionStringId(static_cast<TrackSort>(i))));
    }
    for (int i = 0; i < kPlayersCount; ++i) {
        data.playersOptions.push_back(
            EqStrLine(strings, PlayersOptionStringId(static_cast<TrackPlayers>(i))));
    }
    data.trackedSpawnId = tracked_;
    ctx_.bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::TrackingListChanged, std::move(data)));
}

void TrackingController::BeginTracking(uint16_t spawnId) {
    auto it = ctx_.entities.find(spawnId);
    if (spawnId == 0 || it == ctx_.entities.end()) {
        Say(EqStrLine(ctx_.strings.Db(), kStrNoTrackingTarget));
        return;
    }
    // The server only records the id (Handle_OP_TrackTarget -> SetTrackingID);
    // everything the player sees from here on is the client's own.
    ctx_.Player().SendTrackTarget(spawnId);
    tracked_ = spawnId;
    trackedName_ = EQT::toDisplayName(it->second.name);
    nextDirection_ = Clock::now() + std::chrono::milliseconds(
        static_cast<int>(kDirectionIntervalSec * 1000.0f));
    Say(EqStrLine(ctx_.strings.Db(), kStrBeginTracking, {trackedName_}));
    // MEASURED (tracking.tsv row 6): the first direction line follows
    // "You begin tracking" at once; the ~2 s cadence starts from here.
    SayDirection(it->second);
    Publish(/*openWindow=*/false);
}

void TrackingController::SayDirection(const Entity& t) {
    const TrackDirection dir =
        DirectionTo(ctx_.self.HeadingDeg(), ctx_.self.PosX(), ctx_.self.PosY(), t.x, t.y);
    Say(DirectionLine(ctx_.strings.Db(), trackedName_, dir));
}

void TrackingController::StopTracking() {
    if (tracked_ == 0) {
        Say(EqStrLine(ctx_.strings.Db(), kStrNoTrackingTarget));
        return;
    }
    // ASSUMPTION (awaiting the oracle's pcap): the stop is told to the server
    // as OP_TrackTarget 0, which Client::SetTrackingID(0) handles explicitly.
    ctx_.Player().SendTrackTarget(0);
    tracked_ = 0;
    trackedName_.clear();
    Publish(/*openWindow=*/false);
}

void TrackingController::SetFilter(TrackFilter f, bool shown) {
    settings_.filters[static_cast<size_t>(f)] = shown;
    const auto& strings = ctx_.strings.Db();
    Say(EqStrLine(strings, shown ? kStrWillSee : kStrWillNotSee,
                  {EqStrLine(strings, FilterWordStringId(f))}));
    Publish(/*openWindow=*/false);
}

void TrackingController::SetSort(TrackSort s) {
    settings_.sort = s;
    Say(EqStrLine(ctx_.strings.Db(), SortConfirmStringId(s)));
    Publish(/*openWindow=*/false);
}

void TrackingController::SetPlayers(TrackPlayers p) {
    settings_.players = p;
    Say(EqStrLine(ctx_.strings.Db(), PlayersConfirmStringId(p)));
    Publish(/*openWindow=*/false);
}

void TrackingController::PrintList() {
    const auto rows = CurrentRows();
    ctx_.out.System(fmt::format("=== Tracking ({} targets) ===", rows.size()));
    for (const auto& r : rows) {
        ctx_.out.System(fmt::format("  {} - {:.0f} units - {}{}{}", r.name, r.distance,
                                    ConWord(r.con), r.isPlayer ? " (player)" : "",
                                    r.spawnId == tracked_ ? " [tracking]" : ""));
    }
}

void TrackingController::Command(TrackingCommand cmd, const std::string& arg) {
    const auto& strings = ctx_.strings.Db();
    switch (cmd) {
        case TrackingCommand::Filter: {
            const auto f = ParseTrackFilter(arg);
            if (!f) { Say(EqStrLine(strings, kStrFilterUsage)); return; }
            // Each /trackfilter <colour> flips that colour (13163 / 13164).
            SetFilter(*f, !settings_.filters[static_cast<size_t>(*f)]);
            return;
        }
        case TrackingCommand::Sort: {
            const auto s = ParseTrackSort(arg);
            if (!s) { Say(EqStrLine(strings, kStrSortUsage)); return; }
            SetSort(*s);
            return;
        }
        case TrackingCommand::Players: {
            const auto p = ParseTrackPlayers(arg);
            if (!p) { Say(EqStrLine(strings, kStrPlayersUsage)); return; }
            SetPlayers(*p);
            return;
        }
        case TrackingCommand::Stop:
            StopTracking();
            return;
        case TrackingCommand::List:
            PrintList();
            return;
    }
}

void TrackingController::ApplyIntent(const eqt::events::TrackingIntent& i) {
    using Op = eqt::events::TrackingIntentOp;
    switch (i.op) {
        case Op::Track:
            BeginTracking(i.spawnId);
            return;
        case Op::ToggleFilter:
            if (i.value < kFilterCount) {
                SetFilter(static_cast<TrackFilter>(i.value), !settings_.filters[i.value]);
            }
            return;
        case Op::SetSort:
            if (i.value < kSortCount) SetSort(static_cast<TrackSort>(i.value));
            return;
        case Op::SetPlayers:
            if (i.value < kPlayersCount) SetPlayers(static_cast<TrackPlayers>(i.value));
            return;
    }
}

void TrackingController::Tick(Clock::time_point now) {
    if (tracked_ == 0 || now < nextDirection_) return;
    nextDirection_ = now + std::chrono::milliseconds(
        static_cast<int>(kDirectionIntervalSec * 1000.0f));

    auto it = ctx_.entities.find(tracked_);
    if (it == ctx_.entities.end() || it->second.is_corpse) {
        // The target despawned or died: eqstr 12102 (tracking.tsv row 8).
        Say(EqStrLine(ctx_.strings.Db(), kStrLostTarget));
        tracked_ = 0;
        trackedName_.clear();
        Publish(/*openWindow=*/false);
        return;
    }
    SayDirection(it->second);
}

}  // namespace tracking
}  // namespace eqt
