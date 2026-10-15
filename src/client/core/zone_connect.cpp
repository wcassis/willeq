// B1072 (GitLab #392): the bounded zone-IN connect.
//
// EverQuest::ConnectToZone() (everquest.cpp) arms m_zone_connect_watchdog on
// every ZoneServerInfo and opens the session here. TickZoneConnect() runs
// every game tick (MovementComponent::UpdateMovement, through
// ctx_.lifecycle.TickZoneConnect) and, while the loading phase is still
// ZONE_CONNECTING:
//   * at ZoneComponent::kZoneConnectTimeoutMs re-opens the session to the SAME
//     ZoneServerInfo host:port (kZoneConnectRetries = one retry);
//   * at the second timeout tears the zone side down and goes back to world
//     as a FRESH login (zoning = 0): character select on a run that has the
//     screen (or the scenario lane's stand-in for it), the configured
//     character's automatic Enter World on a headless one -- the same two
//     landings a /camp has (B1005, player_camp.cpp).
// Never a hang, never a process exit. The mechanism and the timeout's
// measurement: refactor/B1072/README.md.

#include "client/eq.h"

#include "client/login_world/login_world_component.h"
#include "client/player/player_component.h"
#include "client/zone/zone_component.h"
#include "common/logging.h"
#include "common/net/posix_udp_transport.h"

#include <functional>
#include <memory>
#include <string>

namespace {

const char* ZoneConnectStage(bool session_established) {
	// The two shapes the bound covers (zone_connect_watchdog.h).
	return session_established ? "zone entry (session up, OP_ZoneEntry sent, no PlayerProfile)"
	                           : "UDP session (no session response)";
}

}  // namespace

void EverQuest::OpenZoneConnection(const std::string& host, uint16_t port)
{
	// A retry drops the old session first. The manager is destroyed here,
	// never from one of its own callbacks (TickZoneConnect runs from
	// UpdateMovement, outside any packet callback), and its state handler is
	// swapped for one that cannot reconnect, as DisconnectFromZone() does.
	if (m_zone_connection_manager) {
		m_zone_connection_manager->OnConnectionStateChange(
			[this](std::shared_ptr<EQ::Net::DaybreakConnection>, EQ::Net::DbProtocolStatus,
			       EQ::Net::DbProtocolStatus to) {
				if (to == EQ::Net::StatusDisconnected) m_zone_connection.reset();
			});
		if (m_zone_connection) m_zone_connection->Close();
		m_zone_connection.reset();
		m_zone_connection_manager.reset();
		m_game_state.world().setZoneConnected(false);
		m_zone_session_established = false;
		m_zone->ZoneEntrySent() = false;
	}

	uint16_t connect_port = port;
	if (m_sim_dead_zone_attempts > 0) {
		--m_sim_dead_zone_attempts;
		connect_port = m_sim_dead_zone_port;
		LOG_WARN(MOD_ZONE, "zone_connect_timeout lever: connecting to {}:{} instead of the ZoneServerInfo port {}",
			host, connect_port, port);
	}

	++m_zone_connect_opens;
	EQ::Net::DaybreakConnectionManagerOptions zone_opts;
	auto transport = std::make_unique<EQ::Net::PosixUdpTransport>();
	m_zone_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(zone_opts, std::move(transport)));
	m_zone_connection_manager->OnNewConnection(std::bind(&EverQuest::ZoneOnNewConnection, this, std::placeholders::_1));
	m_zone_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::ZoneOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_zone_connection_manager->OnPacketRecv(std::bind(&EverQuest::ZoneOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_zone_connection_manager->Connect(host, connect_port);
}

void EverQuest::TickZoneConnect(std::chrono::steady_clock::time_point now)
{
	using Action = eqt::zone::ZoneConnectWatchdog::Action;
	const Action act = m_zone_connect_watchdog.Tick(GetLoadingPhase() == LoadingPhase::ZONE_CONNECTING, now);
	if (act == Action::None) return;

	const auto& w = m_zone_connect_watchdog;
	if (act == Action::Retry) {
		LOG_WARN(MOD_ZONE, "Zone server {}:{} did not complete the zone-in within {} ms, stuck at {}; "
			"retrying the connect (attempt {}/{})",
			w.Host(), w.Port(), w.TimeoutMs(), ZoneConnectStage(m_zone_session_established),
			w.Attempt(), w.Attempts());
		SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);
		OpenZoneConnection(w.Host(), w.Port());
		return;
	}
	AbandonZoneConnect();
}

void EverQuest::AbandonZoneConnect()
{
	const auto& w = m_zone_connect_watchdog;
	const std::string where = w.Host() + ":" + std::to_string(w.Port());
	const bool to_select = m_player->CampReturnsToCharacterSelect();
	LOG_WARN(MOD_ZONE, "Zone server {} did not complete the zone-in after {} attempts of {} ms, stuck at {}; {}",
		where, w.Attempts(), w.TimeoutMs(), ZoneConnectStage(m_zone_session_established),
		to_select ? "returning to character select" : "logging in to world again");
	m_chat_output.System("Could not connect to the zone server at " + where + " (" +
		ZoneConnectStage(m_zone_session_established) + "). " +
		(to_select ? "Returning to character select." : "Logging in to world again."));

	// A fresh world login, not a zoning one: zoning = 1 would make world hand
	// out the same dead zone again instead of the character list.
	m_zone->ZoneToZoneInProgress() = false;
	m_zone->DisconnectFromZone();
	if (to_select) {
		m_login_world->ReturnToCharacterSelect("zone connect failed");
	} else {
		m_login_world->ReconnectWorld();
	}
}
