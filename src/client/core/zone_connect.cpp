// B1072 (GitLab #392) + B1079 (GitLab #399): the bounded zone-in, EVERY phase.
//
// EverQuest::ConnectToZone() (everquest.cpp) arms m_zone_connect_watchdog on
// every ZoneServerInfo and opens the session here. TickZoneConnect() runs
// every game tick (MovementComponent::UpdateMovement, through
// ctx_.lifecycle.TickZoneConnect) and walks the whole loading span, from
// ZONE_CONNECTING through GRAPHICS_FINALIZING, against the per-phase table in
// zone_connect_watchdog.h:
//   * while still in ZONE_CONNECTING, at the connect bound, re-opens the
//     session to the SAME ZoneServerInfo host:port (kZoneConnectRetries = one
//     retry) -- the B1072 shape, a session that never came up;
//   * past ZONE_CONNECTING (the zone answered -- even if it then went silent
//     after processing OP_ZoneEntry, the 2026-10-15 XU4 hang) the deadline is
//     that phase's own, and at the end of the budget the escalation runs
//     WITHOUT a retry: the zone has already seen our ZoneEntry, so re-opening
//     the session would duplicate the entry; instead the zone side is torn
//     down and the client goes back to world as a FRESH login (zoning = 0):
//     character select on a run that has the screen (or the scenario lane's
//     stand-in for it), the configured character's automatic Enter World on a
//     headless one -- the same two landings a /camp has (B1005,
//     player_camp.cpp).
// Never a hang, never a process exit. B1072's mechanism and the connect
// bound's measurement: refactor/B1072/README.md. B1079's per-phase table and
// the escalation's shape: zone_connect_watchdog.h and refactor/B1079/README.md.

#include "client/eq.h"

#include "client/login_world/login_world_component.h"
#include "client/player/player_component.h"
#include "client/zone/zone_component.h"
#include "common/logging.h"
#include "common/net/posix_udp_transport.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace {

// B1079 (#399): the loading phase -> watchdog phase_index map. The phases
// ZONE_CONNECTING..GRAPHICS_FINALIZING are exactly LoadingPhase 5..14, in the
// same order the loading screen walks them, so the map is an offset; a phase
// before the zone-in (login, world, character select) or a finished load is
// untracked / done, and the watchdog leaves its running deadline alone
// (kUntracked) or disarms (kDone).
int ZonePhaseIndex(LoadingPhase phase) {
    switch (phase) {
    case LoadingPhase::ZONE_CONNECTING:
        return eqt::zone::phase_index::kZoneConnecting;
    case LoadingPhase::ZONE_RECEIVING_PROFILE:
        return eqt::zone::phase_index::kZoneReceivingProfile;
    case LoadingPhase::ZONE_RECEIVING_SPAWNS:
        return eqt::zone::phase_index::kZoneReceivingSpawns;
    case LoadingPhase::ZONE_REQUEST_PHASE:
        return eqt::zone::phase_index::kZoneRequestPhase;
    case LoadingPhase::ZONE_PLAYER_READY:
        return eqt::zone::phase_index::kZonePlayerReady;
    case LoadingPhase::ZONE_AWAITING_CONFIRM:
        return eqt::zone::phase_index::kZoneAwaitingConfirm;
    case LoadingPhase::GRAPHICS_LOADING_ZONE:
        return eqt::zone::phase_index::kGraphicsLoadingZone;
    case LoadingPhase::GRAPHICS_LOADING_MODELS:
        return eqt::zone::phase_index::kGraphicsLoadingModels;
    case LoadingPhase::GRAPHICS_CREATING_ENTITIES:
        return eqt::zone::phase_index::kGraphicsCreatingEntities;
    case LoadingPhase::GRAPHICS_FINALIZING:
        return eqt::zone::phase_index::kGraphicsFinalizing;
    case LoadingPhase::COMPLETE:
        return eqt::zone::phase_index::kDone;
    default:
        return eqt::zone::phase_index::kUntracked;
    }
}

}  // namespace

// B1079: the production table, in phase_index order, straight off the header's
// phase_bound constants (whose comment names the source of every value). One
// place builds it so the arm sites in everquest.cpp and the unit test read the
// same ten rows; test_zone_connect_timeout pins the values against the header.
const int* EverQuest::ZonePhaseBoundsMs() {
    static const int kBounds[eqt::zone::phase_index::kCount] = {
        eqt::zone::phase_bound::kZoneConnectingMs,
        eqt::zone::phase_bound::kZoneReceivingProfileMs,
        eqt::zone::phase_bound::kZoneReceivingSpawnsMs,
        eqt::zone::phase_bound::kZoneRequestPhaseMs,
        eqt::zone::phase_bound::kZonePlayerReadyMs,
        eqt::zone::phase_bound::kZoneAwaitingConfirmMs,
        eqt::zone::phase_bound::kGraphicsLoadingZoneMs,
        eqt::zone::phase_bound::kGraphicsLoadingModelsMs,
        eqt::zone::phase_bound::kGraphicsCreatingEntitiesMs,
        eqt::zone::phase_bound::kGraphicsFinalizingMs };
    return kBounds;
}

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

// B1079: the escalation's "last opcode seen" label. The handful of opcodes a
// stalled zone-in can end on (the session and the zone-in handshake) get
// names; everything else prints its hex value, so a NEW opcode that matters
// is still identifiable in the line.
const char* LastZoneOpcodeName(int opcode) {
	if (opcode < 0) return "none";
	switch (static_cast<unsigned>(opcode)) {
	case TitaniumZoneOpcodes::HC_OP_ZoneEntry: return "ZoneEntry";
	case TitaniumZoneOpcodes::HC_OP_NewZone: return "NewZone";
	case TitaniumZoneOpcodes::HC_OP_ReqClientSpawn: return "ReqClientSpawn";
	case TitaniumZoneOpcodes::HC_OP_ZoneSpawns: return "ZoneSpawns";
	case TitaniumZoneOpcodes::HC_OP_PlayerProfile: return "PlayerProfile";
	case TitaniumZoneOpcodes::HC_OP_ClientReady: return "ClientReady";
	case TitaniumZoneOpcodes::HC_OP_ClientUpdate: return "ClientUpdate";
	case TitaniumZoneOpcodes::HC_OP_TimeOfDay: return "TimeOfDay";
	default: {
		static thread_local char buf[16];
		snprintf(buf, sizeof(buf), "0x%04x", opcode);
		return buf;
	}
	}
}

void EverQuest::TickZoneConnect(std::chrono::steady_clock::time_point now)
{
	using Action = eqt::zone::ZoneConnectWatchdog::Action;
	// B1079 (#399): walk the CURRENT loading phase, not just "still in
	// ZONE_CONNECTING". The watchdog re-baselines on forward movement (its
	// own table: per-phase for the network tail, one shared budget for the
	// graphics span) and escalates when that budget runs out -- with the one
	// retry still limited to ZONE_CONNECTING.
	const Action act = m_zone_connect_watchdog.Tick(ZonePhaseIndex(GetLoadingPhase()), now);
	if (act == Action::None) return;

	const auto& w = m_zone_connect_watchdog;
	if (act == Action::Retry) {
		// The B1072 shape: the session never came up, so the connect itself
		// is what is re-tried (OpenZoneConnection re-arms the same host:port
		// and re-baselines the connect window).
		LOG_WARN(MOD_ZONE, "Zone server {}:{} did not answer the session within {} ms "
			"{}; retrying the connect (attempt {}/{})",
			w.Host(), w.Port(), w.PhaseBoundMs(),
			m_zone_session_established
				? "(the session is up but OP_ZoneEntry got no PlayerProfile)"
				: "(no session response)",
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
	const int phase = w.PhaseIndex();
	// B1079: the escalation names WHERE the load sat. The last opcode the
	// zone sent is the one fact a play box without --log-file still leaves
	// (the 2026-10-15 XU4 hang: the zone logged OP_ZoneEntry and nothing
	// else -- now the client says so too).
	LOG_WARN(MOD_ZONE, "Zone-in to {} stalled in {} ({} ms budget); last opcode from the zone: {} ({}){}; "
		"{}", where, w.PhaseName(phase), w.PhaseBoundMs(),
		LastZoneOpcodeName(LastZoneOpcode()), LastZoneOpcode(),
		m_zone_session_established ? " the session is up -- no connect retry (it would duplicate OP_ZoneEntry)" : "",
		to_select ? "returning to character select" : "logging in to world again");
	m_chat_output.System("Could not complete the zone-in to " + where + " (" +
		w.PhaseName(phase) + "). " +
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
