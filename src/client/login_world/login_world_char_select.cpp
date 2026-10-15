// B998 (GitLab #318) -- the character-select screen's half of
// LoginWorldComponent: Enter World from the screen. Its own TU (M00). The
// list itself is T14's (WorldProcessCharacterSelect); creation and deletion
// are B888's (login_world_char_create.cpp) and the screen reaches them
// through the same RequestCreateCharacter / RequestDeleteCharacter /
// RequestRandomName bodies the world script uses.

#include "client/eq.h"
#include "client/login_world/login_world_component.h"

#include "client/bridge/bridge_component.h"
#include "common/logging.h"
#include "common/net/posix_udp_transport.h"

#include <functional>
#include <memory>

namespace eqt {
namespace login_world {

bool LoginWorldComponent::EnterWorldFromSelect(const std::string& name) {
	if (m_enter_world_sent) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': OP_EnterWorld was already sent", name);
		return false;
	}
	if (m_create_status.stage != CharCreateStatus::Stage::Idle) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': a create/delete is in flight", name);
		return false;
	}
	if (!CharacterListHas(name)) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': not on this account's character list", name);
		return false;
	}
	// The selected character IS the configured one from here on, exactly as
	// after a create (EnterWorldAfterCreate): the zoning world pass
	// (WorldProcessPostEnterWorld) and every per-character file key off it.
	ctx_.character = name;
	for (const auto& e : m_character_select_list) {
		if (e.name == name) m_character_select_index = e.slot;
	}
	if (m_created_this_session) {
		// World set StartInTutorial on the create and clears it only on CRC1
		// (see m_created_this_session): never enter the tutorial by accident.
		WorldSendWorldClientCRC();
	}
	LOG_INFO(MOD_WORLD, "[CHARSELECT] Enter World: {}", name);
	WorldSendEnterWorld(name);
	return true;
}

void LoginWorldComponent::ReconnectWorld() {
	if (m_world_server_host.empty()) {
		LOG_ERROR(MOD_WORLD, "No world server address stored for reconnection!");
		return;
	}
	ctx_.Bridge().SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);

	// The full handshake again: WorldClientReady and OP_EnterWorld are re-sent.
	m_world_ready = false;
	m_enter_world_sent = false;

	m_world_connection.reset();
	m_world_connection_manager.reset(
		new EQ::Net::DaybreakConnectionManager(std::make_unique<EQ::Net::PosixUdpTransport>()));
	m_world_connection_manager->OnNewConnection(
		std::bind(&LoginWorldComponent::WorldOnNewConnection, this, std::placeholders::_1));
	m_world_connection_manager->OnConnectionStateChange(
		std::bind(&LoginWorldComponent::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1,
		          std::placeholders::_2, std::placeholders::_3));
	m_world_connection_manager->OnPacketRecv(
		std::bind(&LoginWorldComponent::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	LOG_DEBUG(MOD_WORLD, "Connecting to world server at {}:9000", m_world_server_host);
	m_world_connection_manager->Connect(m_world_server_host, 9000);
}

void LoginWorldComponent::ReturnToCharacterSelect(const char* why) {
	// Server authoritative for a camp: this runs only after the zone's
	// OP_LogoutReply (PlayerComponent::ZoneProcessLogoutReply -> the deferred
	// teardown in MovementComponent::UpdateMovement), never on the client's own
	// clock. B1072: the one clock-driven caller is the zone-in bound
	// (EverQuest::AbandonZoneConnect), where no zone session ever came up.
	m_select_screen = true;
	m_awaiting_fresh_list = true;
	m_character_select_list.clear();
	m_create_status = CharCreateStatus{};
	++m_select_returns;
	LOG_INFO(MOD_WORLD, "[CHARSELECT] {}: back to character select (return #{}), reconnecting to world",
	         why, m_select_returns);
	// ctx_.Zone().ZoneToZoneInProgress() is false here (the logout reply
	// clears it), so OP_SendLoginInfo goes out with zoning = 0: a fresh
	// login, which is what makes world send the character list.
	ReconnectWorld();
}

}  // namespace login_world
}  // namespace eqt
