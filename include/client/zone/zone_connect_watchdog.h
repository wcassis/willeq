#pragma once

// B1072 (GitLab #392): the bound on the zone-IN connect.
//
// Before B1072 nothing bounded LoadingPhase::ZONE_CONNECTING. ConnectToZone()
// (src/client/core/everquest.cpp) opened the UDP session and waited; the
// phase ends only when the zone's PlayerProfile arrives
// (ZONE_RECEIVING_PROFILE). Two ways that never happens, both unbounded:
//   * the session never comes up (a dead port): the Daybreak manager drops a
//     StatusConnecting connection after connect_stale_ms and moves it to
//     StatusDisconnecting AFTER erasing it, so StatusDisconnected never fires
//     and the reconnect handler never runs -- silence, forever;
//   * the session comes up, OP_ZoneEntry goes out, and the zone process never
//     answers (the 2026-10-15 XU4 hang: world assigned qeynos2 twice and the
//     client's zone process logged nothing after "New connection").
//
// This is the pure half: a deadline per attempt, one retry, then give up.
// It knows nothing about sockets; EverQuest::TickZoneConnect() acts on what
// Tick() returns (src/client/core/zone_connect.cpp). Tests drive it with a
// fake clock (tests/test_zone_connect_timeout.cpp).

#include <chrono>
#include <cstdint>
#include <string>

namespace eqt {
namespace zone {

class ZoneConnectWatchdog {
public:
	using Clock = std::chrono::steady_clock;
	enum class Action { None, Retry, GiveUp };

	// A fresh ZoneServerInfo: attempt 1 of 1 + retries, deadline now + timeout.
	// The bound is passed per arm (ZoneComponent::kZoneConnectTimeoutMs /
	// kZoneConnectRetries in production) so the class needs no configuration.
	void Arm(const std::string& host, uint16_t port, Clock::time_point now, int timeoutMs, int retries) {
		timeout_ = std::chrono::milliseconds(timeoutMs);
		retries_ = retries;
		host_ = host;
		port_ = port;
		attempt_ = 1;
		armed_ = true;
		deadline_ = now + timeout_;
	}
	void Disarm() { armed_ = false; }

	// `connecting` = the loading phase is still ZONE_CONNECTING. Any other
	// phase means the zone answered (or something else took over): disarm.
	Action Tick(bool connecting, Clock::time_point now) {
		if (!armed_) return Action::None;
		if (!connecting) {
			armed_ = false;
			return Action::None;
		}
		if (now < deadline_) return Action::None;
		if (attempt_ <= retries_) {
			++attempt_;
			deadline_ = now + timeout_;
			return Action::Retry;
		}
		armed_ = false;
		return Action::GiveUp;
	}

	bool Armed() const { return armed_; }
	int Attempt() const { return attempt_; }
	int Attempts() const { return 1 + retries_; }
	int TimeoutMs() const { return static_cast<int>(timeout_.count()); }
	const std::string& Host() const { return host_; }
	uint16_t Port() const { return port_; }

private:
	std::chrono::milliseconds timeout_{0};
	int retries_ = 0;
	std::string host_;
	uint16_t port_ = 0;
	int attempt_ = 0;
	bool armed_ = false;
	Clock::time_point deadline_{};
};

}  // namespace zone
}  // namespace eqt
