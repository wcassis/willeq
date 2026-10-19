// B1099 (GitLab #419) -- LoginWorldComponent's pre-zone connect bound. Its own
// TU (M00). The watchdog and the reason for every choice here are in
// include/client/login_world/login_connect_watchdog.h; what a give-up does to
// the process is Application::serviceConnectFailure() (application_quit.cpp).

#include "client/login_world/login_world_component.h"

#include "client/bridge/bridge_component.h"
#include "client/eq.h"
#include "client/fatal_exit.h"
#include "common/logging.h"

#include <string>

namespace eqt {
namespace login_world {

namespace {

int LoginStageOf(LoadingPhase phase) {
	switch (phase) {
	case LoadingPhase::LOGIN_CONNECTING: return login_stage::kLoginConnecting;
	case LoadingPhase::LOGIN_AUTHENTICATING: return login_stage::kLoginAuthenticating;
	case LoadingPhase::WORLD_CONNECTING: return login_stage::kWorldConnecting;
	case LoadingPhase::DISCONNECTED: return login_stage::kUntracked;
	default: return login_stage::kDone;  // character select and every zone phase
	}
}

// Tear a session down so nothing reconnects it: the state handler is swapped
// for one that only forgets the connection (LoginDisableReconnect's shape),
// then the connection and the manager go. Called from TickNetwork, after the
// managers' own Tick() -- never from inside one of their callbacks.
template <typename Manager, typename Connection>
void CloseWithoutReconnect(Manager& manager, Connection& connection) {
	if (manager) {
		manager->OnConnectionStateChange(
			[&connection](std::shared_ptr<EQ::Net::DaybreakConnection>, EQ::Net::DbProtocolStatus,
			              EQ::Net::DbProtocolStatus to) {
				if (to == EQ::Net::StatusDisconnected) connection.reset();
			});
	}
	// A local reference keeps the connection alive across Close(), whose
	// state change may run the handler above and reset the member.
	if (auto keep = connection) keep->Close();
	connection.reset();
	manager.reset();
}

}  // namespace

void LoginWorldComponent::TickLoginConnect(std::chrono::steady_clock::time_point now) {
	const int stage = LoginStageOf(ctx_.Bridge().GetLoadingPhase());
	const bool tracked = stage != login_stage::kUntracked && stage != login_stage::kDone;
	// An entry into the tracked span from outside it arms the bound:
	// ConnectToLogin, ConnectToWorld, ReconnectWorld (camp return, zone-change
	// world pass) all land here without each arming it by hand.
	if (tracked && !m_login_stage_tracked && !m_login_watchdog.Armed()) {
		m_login_watchdog.Arm(stage, now, kLoginConnectTimeoutMs);
	}
	m_login_stage_tracked = tracked;
	if (m_login_watchdog.Tick(stage, now) == LoginConnectWatchdog::Action::GiveUp) {
		AbandonPreZoneConnect(m_login_watchdog.Stage());
		m_login_stage_tracked = false;
	}
}

void LoginWorldComponent::AbandonPreZoneConnect(int stage) {
	const bool world = stage == login_stage::kWorldConnecting;
	const std::string where = world ? m_world_server_host + ":9000"
	                                : m_host + ":" + std::to_string(m_port);
	const char* text = LoginConnectFailureText(stage);
	LOG_ERROR(MOD_WORLD, "{}: {} sat in {} for {} ms with no answer; giving up (no reconnect)",
	          text, where, LoginStageName(stage), m_login_watchdog.BoundMs());
	if (world) {
		CloseWithoutReconnect(m_world_connection_manager, m_world_connection);
	} else {
		CloseWithoutReconnect(m_login_connection_manager, m_login_connection);
	}
	// The loading screen's line: DISCONNECTED is what the client now is, and
	// the override text is what the person reads (bridge_component.cpp).
	ctx_.Bridge().SetLoadingPhase(LoadingPhase::DISCONNECTED, text);
	eqt::RecordCouldNotConnect(std::string(text) + " (" + where + ")");
}

}  // namespace login_world
}  // namespace eqt
