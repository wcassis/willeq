#pragma once

// B1072 (GitLab #392) + B1079 (GitLab #399): the bound on EVERY zone loading phase.
//
// Before B1072 nothing bounded LoadingPhase::ZONE_CONNECTING: ConnectToZone()
// (src/client/core/everquest.cpp) opened the UDP session and waited; the
// phase ends only when the zone's PlayerProfile arrives
// (ZONE_RECEIVING_PROFILE). Two shapes, both unbounded:
//   * the session never comes up (a dead port): the Daybreak manager drops a
//     StatusConnecting connection after connect_stale_ms and moves it to
//     StatusDisconnecting AFTER erasing it, so StatusDisconnected never fires
//     and the reconnect handler never runs -- silence, forever;
//   * the session comes up, OP_ZoneEntry goes out, and the zone process never
//     answers (the 2026-10-15 XU4 hang: world assigned qeynos2 twice and the
//     client's zone process logged nothing after "New connection").
//
// B1079 (GitLab #399): the second shape has a TAIL that B1072 left unbounded.
// The old watchdog disarmed the moment the phase left ZONE_CONNECTING, so a
// zone that completed the session (and even processed OP_ZoneEntry -- the zone
// process logged it) and then went silent left the client on the loading
// screen forever: ZONE_RECEIVING_PROFILE / SPAWNS / REQUEST_PHASE /
// PLAYER_READY / AWAITING_CONFIRM / GRAPHICS_* were all unbounded, and a
// mid-session zone change (death -> zone-to-bind = zone load #2) made it
// reachable in play, not just in a dead-port drill. This class now carries a
// deadline for the WHOLE loading span from ZONE_CONNECTING through
// GRAPHICS_FINALIZING; Tick() walks the zone phase forward as the load
// progresses and returns the same Actions, with the ONE retry limited to
// ZONE_CONNECTING (a retry is re-opening the zone session; once the session
// is up the zone has already seen our OP_ZoneEntry and re-opening it would
// duplicate the zone entry -- so past the session, escalation is straight to
// give-up). EverQuest::TickZoneConnect() acts on what Tick() returns
// (src/client/core/zone_connect.cpp). Tests drive it with a fake clock
// (tests/test_zone_connect_timeout.cpp).
//
// The deadline table (one value per phase, in milliseconds). Every value
// names its source; none is a measurement except where said.
//   ZONE_CONNECTING 30000 -- MEASURED, B1072: review/B1072/zone_connect_interval_census.txt,
//     1,663 ZONE_CONNECTING -> ZONE_RECEIVING_PROFILE intervals, median 0.598 s,
//     p99 4.641 s, max 14.398 s; 30 s is twice the max.
//   ZONE_RECEIVING_PROFILE .. ZONE_AWAITING_CONFIRM, 10000 each -- ASSUMED, not
//     measured: no per-phase census exists in-tree (the only measured zone-in
//     interval is ZONE_CONNECTING, above; refactor/T186 and the B1072 census
//     span the whole load, not the phase boundaries). 10 s is ~2.2 B1072 p99s
//     and well above the x86 whole-load time T186 measured (7.98 s warm /
//     12.1 s cold), so even the whole network span fits one row on x86; it
//     is a conservative multiple of the one interval that IS measured. If a
//     per-phase census ever exists, the rows are one line each.
//   GRAPHICS_LOADING_ZONE .. GRAPHICS_FINALIZING, 80000 -- from the slowest
//     blessed Pi load, doubled: refactor/T186 measured a cold befallen
//     zone-in at ~37.7 s on the Orange Pi (two runs, 38.65 / 36.66) and a
//     WARM re-zone at 23.39 s (x86 7.98 s) -- "the Pi is unchanged" against
//     the standing perf target. 80 s = 2x the cold Pi worst case; a re-zone
//     (zone load #N) is measurably faster, so the same bound covers load #N
//     with more margin. The four GRAPHICS_* rows share ONE budget: the
//     deadline is set when the FIRST graphics phase is entered and is not
//     re-baselined by later graphics phases (see Tick). The reason is
//     structural, not just convenience: the GRAPHICS_* transitions are driven
//     by the loading thread's percent updates (the loadZoneSequential
//     [N%] steps), so they are a single contiguous span owned by ONE thread
//     that starts asynchronously after the network handshake completes. A
//     stuck load sits in that span and costs the one 80 s budget.
//
// How the deadlines combine. Forward movement to a later tracked phase
// re-baselines the deadline to (now + bound[phase]) for the network phases;
// the graphics span re-baselines once, at its first entry. Backward and
// untracked phases leave the running deadline alone, so a server that
// ping-pongs between two phases cannot extend the budget by regressing.
// ZONE_CONNECTING re-baselines on every entry (it is the retry's own
// window, and a mid-session reconnect re-enters it). Worst case from a
// fresh Arm: 30 s (connect, plus one 30 s retry window) + 5 x 10 s (the
// network tail) + 80 s (the graphics span) = 230 s, i.e. under four
// minutes before the escalation. In practice a stalled load sits in ONE
// phase and costs that phase's own budget.

#include <chrono>
#include <cstdint>
#include <string>

namespace eqt {
namespace zone {

// The ordered phase list the bound walks. The enum values in
// include/client/eq_enums.h are not used here on purpose: the watchdog is
// pure (no client include), and Tick() is handed the CLIENT's zone phase
// mapped to this order by EverQuest (zone_connect.cpp). Order is the
// loading screen's order; a phase the client never visits (a headless run
// skips GRAPHICS_*) is simply never ticked.
namespace phase_index {
constexpr int kZoneConnecting = 0;
constexpr int kZoneReceivingProfile = 1;
constexpr int kZoneReceivingSpawns = 2;
constexpr int kZoneRequestPhase = 3;
constexpr int kZonePlayerReady = 4;
constexpr int kZoneAwaitingConfirm = 5;
constexpr int kGraphicsLoadingZone = 6;
constexpr int kGraphicsLoadingModels = 7;
constexpr int kGraphicsCreatingEntities = 8;
constexpr int kGraphicsFinalizing = 9;
constexpr int kCount = 10;
constexpr int kUntracked = -1;  // a phase the bound does not cover (login/world)
constexpr int kDone = 100;      // the load finished: disarm, no escalation
}

// The production deadline table (see the header comment for the source of
// every value). One value per phase, in the order of phase_index.
namespace phase_bound {
constexpr int kZoneConnectingMs = 30000;
constexpr int kZoneReceivingProfileMs = 10000;
constexpr int kZoneReceivingSpawnsMs = 10000;
constexpr int kZoneRequestPhaseMs = 10000;
constexpr int kZonePlayerReadyMs = 10000;
constexpr int kZoneAwaitingConfirmMs = 10000;
constexpr int kGraphicsLoadingZoneMs = 80000;
constexpr int kGraphicsLoadingModelsMs = 80000;
constexpr int kGraphicsCreatingEntitiesMs = 80000;
constexpr int kGraphicsFinalizingMs = 80000;
}

class ZoneConnectWatchdog {
public:
	using Clock = std::chrono::steady_clock;
	enum class Action { None, Retry, GiveUp };

	// B1079 (#399) arm: one deadline per phase, in phase_index order (the
	// production table is zone_connect.cpp's ZonePhaseBoundsMs(), straight off
	// the phase_bound constants). The deadline starts at the first tracked
	// phase (ZONE_CONNECTING in production); `retries` applies to
	// ZONE_CONNECTING only. A test that only exercises the B1072 shape can
	// pass a flat table of one value.
	void Arm(const std::string& host, uint16_t port, Clock::time_point now,
	         const int* timeoutsMs, int retries) {
		for (int i = 0; i < phase_index::kCount; ++i) boundsMs_[i] = timeoutsMs[i];
		retries_ = retries;
		host_ = host;
		port_ = port;
		attempt_ = 1;
		maxPhase_ = phase_index::kZoneConnecting;
		currentPhase_ = phase_index::kUntracked;
		sessionUp_ = false;
		graphicsDeadlineSet_ = false;
		armed_ = true;
		deadline_ = now + std::chrono::milliseconds(boundsMs_[phase_index::kZoneConnecting]);
	}

	// B1072 shape: ONE value for the whole connect bound (the ZONE_CONNECTING
	// row the connect/retry shape reads). Kept so the B1072 test compiles
	// unchanged; production arms the per-phase table above.
	void Arm(const std::string& host, uint16_t port, Clock::time_point now,
	         int scalarMs, int retries) {
		const int one[phase_index::kCount] = { scalarMs, scalarMs, scalarMs, scalarMs,
			scalarMs, scalarMs, scalarMs, scalarMs, scalarMs, scalarMs };
		Arm(host, port, now, one, retries);
	}

	void Disarm() { armed_ = false; }

	// B1072 shape, kept as a thin compat entry: `connecting` is the client
	// still sitting in ZONE_CONNECTING, else it moved past the session and the
	// bound is done. (Production arms the per-phase table and walks phases
	// with Tick(int, now) below; the two never mix on one instance.)
	Action Tick(bool connecting, Clock::time_point now) {
		if (!armed_) return Action::None;
		if (!connecting) {
			armed_ = false;
			return Action::None;
		}
		if (now < deadline_) return Action::None;
		if (attempt_ <= retries_) {
			++attempt_;
			deadline_ = now + std::chrono::milliseconds(boundsMs_[phase_index::kZoneConnecting]);
			return Action::Retry;
		}
		armed_ = false;
		return Action::GiveUp;
	}

	// B1079 (#399) arm: `phase` is the client's current zone phase in
	// phase_index order (kUntracked = a phase the bound does not cover;
	// kDone = the load finished). The deadline re-baselines ONLY on a
	// transition to a later tracked phase, never on repeated calls at the
	// same phase -- a 60 Hz tick that re-baselined every call would push the
	// deadline forward forever and the bound could not fire (that is the
	// defect the transition tracking exists to prevent). The graphics span
	// (the phases at/after kGraphicsLoadingZone) re-baselines once, at its
	// first entry, and later GRAPHICS_* phases must not extend it (they are
	// the loading thread's own contiguous progress). A transition BACK to
	// ZONE_CONNECTING (a mid-session reconnect) re-baselines only while the
	// attempt is fresh; otherwise it keeps the running deadline, exactly as a
	// forward regression would. At the deadline: Retry while in
	// ZONE_CONNECTING and retries remain (re-open the session), else GiveUp
	// (the escalation).
	Action Tick(int phase, Clock::time_point now) {
		if (!armed_) return Action::None;
		if (phase == phase_index::kDone) {
			armed_ = false;
			return Action::None;
		}
		if (phase == phase_index::kUntracked)
			;  // a phase the bound does not cover: leave the deadline alone
		else if (phase != currentPhase_) {
			const bool forward = phase > currentPhase_;
			currentPhase_ = phase;
			if (forward) {
				maxPhase_ = phase;
				if (phase > phase_index::kZoneConnecting) sessionUp_ = true;
				// The connect deadline is set by Arm (and re-set by the Retry
				// action), never by entering the connect phase: the initial
				// kUntracked -> kZoneConnecting tick would otherwise extend
				// the window by the time to the first tick. Only movement
				// PAST the connect phase re-baselines here.
				if (phase > phase_index::kZoneConnecting) {
					if (phase >= phase_index::kGraphicsLoadingZone) {
						if (!graphicsDeadlineSet_) {
							graphicsDeadlineSet_ = true;
							deadline_ = now + std::chrono::milliseconds(boundsMs_[phase_index::kGraphicsLoadingZone]);
						}
					} else {
						deadline_ = now + std::chrono::milliseconds(boundsMs_[phase]);
					}
				}
			}
			// A backward move (the ping-pong shape) never extends the budget:
			// the running deadline is left alone. A mid-session reconnect that
			// drops the phase back to ZONE_CONNECTING therefore keeps the
			// deadline the load had; it is not a fresh connect window (a fresh
			// zone-in is a fresh Arm, and the reconnect path in everquest.cpp
			// re-arms if the bound is down).
		}
		if (now < deadline_) return Action::None;
		if (!sessionUp_ && maxPhase_ == phase_index::kZoneConnecting && attempt_ <= retries_) {
			++attempt_;
			deadline_ = now + std::chrono::milliseconds(boundsMs_[phase_index::kZoneConnecting]);
			return Action::Retry;
		}
		armed_ = false;
		return Action::GiveUp;
	}

	bool Armed() const { return armed_; }
	int Attempt() const { return attempt_; }
	int Attempts() const { return 1 + retries_; }
	const std::string& Host() const { return host_; }
	uint16_t Port() const { return port_; }
	// The furthest phase reached (the escalation's WARN names it).
	int PhaseIndex() const { return maxPhase_; }
	// The budget that is running, for the escalation's WARN line.
	int PhaseBoundMs() const {
		if (maxPhase_ >= phase_index::kGraphicsLoadingZone)
			return boundsMs_[phase_index::kGraphicsLoadingZone];
		return boundsMs_[maxPhase_];
	}
	// The phase table, for the escalation's "stuck at <phase>" label.
	static const char* PhaseName(int index) {
		switch (index) {
		case phase_index::kZoneConnecting: return "zone connect";
		case phase_index::kZoneReceivingProfile: return "PlayerProfile";
		case phase_index::kZoneReceivingSpawns: return "spawns";
		case phase_index::kZoneRequestPhase: return "zone request phase";
		case phase_index::kZonePlayerReady: return "ClientReady";
		case phase_index::kZoneAwaitingConfirm: return "ClientUpdate confirm";
		case phase_index::kGraphicsLoadingZone: return "graphics: zone geometry";
		case phase_index::kGraphicsLoadingModels: return "graphics: character models";
		case phase_index::kGraphicsCreatingEntities: return "graphics: entities";
		case phase_index::kGraphicsFinalizing: return "graphics: finalizing";
		default: return "zone connect";
		}
	}

private:
	int boundsMs_[phase_index::kCount] = {
		phase_bound::kZoneConnectingMs, phase_bound::kZoneReceivingProfileMs,
		phase_bound::kZoneReceivingSpawnsMs, phase_bound::kZoneRequestPhaseMs,
		phase_bound::kZonePlayerReadyMs, phase_bound::kZoneAwaitingConfirmMs,
		phase_bound::kGraphicsLoadingZoneMs, phase_bound::kGraphicsLoadingModelsMs,
		phase_bound::kGraphicsCreatingEntitiesMs, phase_bound::kGraphicsFinalizingMs };
	int retries_ = 0;
	std::string host_;
	uint16_t port_ = 0;
	int attempt_ = 1;
	int maxPhase_ = phase_index::kZoneConnecting;
	int currentPhase_ = phase_index::kUntracked;
	bool sessionUp_ = false;
	bool graphicsDeadlineSet_ = false;
	bool armed_ = false;
	Clock::time_point deadline_{};
};

}  // namespace zone
}  // namespace eqt
