#pragma once

// B1099 (GitLab #419): the bound on the PRE-ZONE connect phases.
//
// Before B1099 nothing bounded LoadingPhase::LOGIN_CONNECTING,
// LOGIN_AUTHENTICATING or WORLD_CONNECTING. ConnectToLogin() /
// ConnectToWorld() (src/client/login_world/login_world_component.cpp) open a
// Daybreak session and wait. Against a dead host the manager drops the
// StatusConnecting connection after connect_stale_ms and moves it to
// StatusDisconnecting AFTER erasing it (the B1072 finding, see
// include/client/zone/zone_connect_watchdog.h), so StatusDisconnected never
// fires, the reconnect handler never runs, and the client sat on "Connecting
// to login server..." forever (Will, 2026-10-19, the XU4 against a stopped
// akk-stack).
//
// This watchdog is the zone bound's sibling for the three phases before
// character select. It is pure (no client include) so
// tests/test_login_connect_timeout.cpp drives it with a fake clock;
// LoginWorldComponent::TickLoginConnect() (login_connect_bound.cpp) maps the
// client's phase onto the stage list below and acts on what Tick() returns.
//
// The bound, kLoginConnectTimeoutMs = 30000 per stage -- a STATED DEFAULT, not
// a measurement (docs/decisions.md, 2026-10-19 B1099): no login-connect
// interval census and no official-client dead-host measurement exist in the
// tree. The login and world connects are the same Daybreak session handshake
// as the zone connect, whose bound (30 s) IS measured
// (review/B1072/zone_connect_interval_census.txt), so the same value is used.
// There is no retry: the Daybreak manager already re-sends the session request
// every connect_delay_ms inside its own connect_stale_ms window, and a server
// that is down for 30 s is not one a person at the loading screen wants to
// keep waiting on -- the screen says so and quit stays available.
//
// How the deadline moves: forward movement to a later stage re-baselines it to
// (now + bound); a backward or untracked phase (the login reconnect handler
// sets LOGIN_CONNECTING again) leaves the running deadline alone, so a server
// that answers and drops cannot extend the budget. Character select or any
// later phase disarms (that wait is the person's, not the network's).

#include <chrono>

namespace eqt {
namespace login_world {

namespace login_stage {
constexpr int kLoginConnecting = 0;
constexpr int kLoginAuthenticating = 1;
constexpr int kWorldConnecting = 2;
constexpr int kUntracked = -1;  // DISCONNECTED: keep the running deadline
constexpr int kDone = 100;      // character select or later: disarm
}

constexpr int kLoginConnectTimeoutMs = 30000;

// The loading-screen line after a give-up, per stage. The login stages share
// one text: "the login server did not answer" is all the person needs.
inline const char* LoginConnectFailureText(int stage) {
	return stage == login_stage::kWorldConnecting ? "Could not connect to world server"
	                                              : "Could not connect to login server";
}

inline const char* LoginStageName(int stage) {
	switch (stage) {
	case login_stage::kLoginConnecting: return "LOGIN_CONNECTING";
	case login_stage::kLoginAuthenticating: return "LOGIN_AUTHENTICATING";
	case login_stage::kWorldConnecting: return "WORLD_CONNECTING";
	default: return "untracked";
	}
}

class LoginConnectWatchdog {
public:
	using Clock = std::chrono::steady_clock;
	enum class Action { None, GiveUp };

	// Start (or restart) the bound at `stage`, now.
	void Arm(int stage, Clock::time_point now, int boundMs) {
		boundMs_ = boundMs;
		stage_ = stage;
		armed_ = true;
		gaveUp_ = false;
		deadline_ = now + std::chrono::milliseconds(boundMs_);
	}

	Action Tick(int stage, Clock::time_point now) {
		if (!armed_) return Action::None;
		if (stage == login_stage::kDone) {
			armed_ = false;
			return Action::None;
		}
		if (stage != login_stage::kUntracked && stage > stage_) {
			stage_ = stage;
			deadline_ = now + std::chrono::milliseconds(boundMs_);
		}
		if (now < deadline_) return Action::None;
		armed_ = false;
		gaveUp_ = true;
		return Action::GiveUp;
	}

	bool Armed() const { return armed_; }
	bool GaveUp() const { return gaveUp_; }
	// The furthest stage reached (the one the give-up names).
	int Stage() const { return stage_; }
	int BoundMs() const { return boundMs_; }

private:
	bool armed_ = false;
	bool gaveUp_ = false;
	int stage_ = login_stage::kUntracked;
	int boundMs_ = kLoginConnectTimeoutMs;
	Clock::time_point deadline_{};
};

}  // namespace login_world
}  // namespace eqt
