#pragma once

// Batch K (refactor/K_component_extraction.md), unit K10.
//
// The login_world family, lifted out of class EverQuest: the login-server
// handshake (session ready, DES credential exchange, server list, play
// request), the world-server handshake (client auth, approve/CRC/ready,
// character select, expansion info, MOTD, chat-server info) and the zone
// handoff. Every body in src/client/login_world/login_world_component.cpp is
// the body that used to live in src/client/eq_login_world.cpp, moved verbatim;
// the only edits inside a body are the substitutions the batch doc's
// section Mechanism allows (m_x -> ctx_.x / owner_.m_x, Foo() -> owner_.Foo(),
// an EverQuest-scoped name -> EverQuest::Name).
//
// ConnectToLogin() and ConnectToWorld() moved in from src/client/eq.cpp with
// them. They are the sites that CONSTRUCT the two connection managers and bind
// the four Login*/World* callbacks, so leaving them behind would have meant
// std::bind(&LoginWorldComponent::LoginOnPacketRecv, m_login_world.get(), ...)
// written out in eq.cpp -- the managers and the code that wires them belong on
// the same object. Both are the only bodies this unit moved out of a TU other
// than eq_login_world.cpp.
//
// State that moved with them: the two connection managers and their
// connections, the credentials (host/port/user/pass/server/key/dbid), the
// login sequence counter, the world-server list and reconnect address, the
// world handshake flags, the character-select slots, and the UCS chat-server
// info parsed off OP_SetChatServer. EverQuest keeps m_zone_connection /
// m_zone_connection_manager (core), m_session_recorder, m_zone_server_host /
// m_zone_server_port (read by EverQuest::ConnectToZone, the zone family) and
// m_zone_to_zone_in_progress / m_replay_suppressed_sends (shared; K09 decides).
//
// T341: the `owner_` TRANSITION handle and the
// `friend class eqt::login_world::LoginWorldComponent` line are BOTH GONE.
// SetLoadingPhase() became ctx_.Bridge().SetLoadingPhase() (B038 cluster 2),
// ConnectToZone() became ctx_.lifecycle.ConnectToZone() (cluster 3), and
// m_zone_server_host/m_zone_server_port moved here (cluster 4), with
// EverQuest's two T09 getters forwarding. Both axes of the K13 (ii) friend
// test are empty.
//
// The reference-returning getters below (LoginManager(), WorldConnection(),
// WorldServerHost(), ...) are the mirror image: EverQuest::TickNetwork(),
// EverQuest::~EverQuest(), EverQuest::ProcessDeferredZoneChange()
// (eq_zone.cpp, K09's territory) and the /connections command (eq_commands.cpp,
// K12's) still touch this state. They are transition scaffolding too.

#include "client/client_context.h"
#include "client/guild/guild_name_table.h"
#include "client/login_world/world_script.h"
#include "common/net/daybreak_connection.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class EverQuest;

namespace eqt {
namespace login_world {

class LoginWorldComponent {
public:
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit LoginWorldComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Initiate login server connection (call after graphics init to avoid timeout during slow startup)
	void ConnectToLogin();

	// T05: same replay/test seam as ReplayZonePacket, for the login and
	// world opcode switches.
	void ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size);
	void ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size);

	// Read-only observers of login/world state (T09). Trivial inline
	// returns of existing members -- no new storage, no logic.
	// T341 (B038 cluster 4): the zone server address this family parsed out
	// of OP_ZoneServerInfo. EverQuest's GetZoneServerHost()/GetZoneServerPort()
	// (T09) forward here, and so does EverQuest::ConnectToZone().
	const std::string& GetZoneServerHost() const { return m_zone_server_host; }
	uint16_t GetZoneServerPort() const { return m_zone_server_port; }

	uint32_t GetAccountId() const { return m_dbid; }
	const std::string& GetSessionKey() const { return m_key; }
	bool IsWorldReady() const { return m_world_ready; }
	bool HasSentEnterWorld() const { return m_enter_world_sent; }
	const std::map<uint32_t, WorldServer>& GetWorldServers() const { return m_world_servers; }
	int GetCharacterSelectIndex() const { return m_character_select_index; }
	// T14: unlike the T09 observers above, this one is backed by new
	// storage — occupied slots only, rebuilt on every OP_SendCharInfo.
	const std::vector<CharacterSelectEntry>& GetCharacterSelectList() const { return m_character_select_list; }
	const std::string& GetChatServerHost() const { return m_ucs_host; }
	uint16_t GetChatServerPort() const { return m_ucs_port; }
	const std::string& GetMailKey() const { return m_mail_key; }
	char GetChatConnectionType() const { return m_ucs_connection_type; }

	// T443 (board B418): the guild-id -> name lookup parsed off
	// OP_GuildsList. World sends it ONCE, in the character-select block, and
	// skips it entirely on a zoning world pass (world/client.cpp 516-523,
	// 847 -- the same condition WorldProcessPostEnterWorld documents), so it
	// is login-time state that survives zoning, exactly like m_dbid and
	// m_key. It lives here because this is the family that receives the
	// packet; see include/client/guild/guild_name_table.h for the wire shape.
	const eqt::guild::GuildNameTable& GuildNames() const { return m_guild_names; }

	// K10 transition scaffolding: EverQuest's constructor sets the credentials
	// it was built with, and eq.cpp / eq_zone.cpp / eq_commands.cpp still reach
	// the two connection managers, their connections and the world handshake
	// flags. K-final folds these call sites into the component.
	void SetCredentials(const std::string& host, int port, const std::string& user,
	                    const std::string& pass, const std::string& server);
	// B897: the configured server name, the `<server>` of the per-character
	// `<Char>_<server>.ini` the friends/ignore lists persist in.
	const std::string& ServerName() const { return m_server; }
	std::unique_ptr<EQ::Net::DaybreakConnectionManager>& LoginManager() { return m_login_connection_manager; }
	std::shared_ptr<EQ::Net::DaybreakConnection>& LoginConnection() { return m_login_connection; }
	std::unique_ptr<EQ::Net::DaybreakConnectionManager>& WorldManager() { return m_world_connection_manager; }
	std::shared_ptr<EQ::Net::DaybreakConnection>& WorldConnection() { return m_world_connection; }
	std::string& WorldServerHost() { return m_world_server_host; }
	bool& WorldReady() { return m_world_ready; }
	bool& EnterWorldSent() { return m_enter_world_sent; }

	// Public because EverQuest::ProcessDeferredZoneChange (eq_zone.cpp) rebuilds
	// the world connection manager on a zone-to-zone transition and re-binds
	// these three itself. Everything else in the family is private.
	void WorldOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void WorldOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void WorldOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);

	// ------------------------------------------------------------------
	// B888 (GitLab #204): character creation / deletion at character select.
	// Bodies: src/client/login_world/login_world_char_create.cpp.
	//
	// A world SCRIPT (--create-character / --delete-character /
	// --list-characters, world_script.h) replaces the automatic OP_EnterWorld
	// at OP_ExpansionInfo: the script runs off the character list instead,
	// because world sends OP_ExpansionInfo BEFORE OP_SendCharInfo
	// (world/client.cpp:552-553) and an OP_EnterWorld for a name the account
	// does not hold closes the connection (:809-812). With no script the
	// handshake is byte-for-byte what it was.
	struct CharCreateStatus {
		enum class Stage : uint8_t { Idle, AwaitingNameApproval, AwaitingCreate,
		                             AwaitingDelete, AwaitingRandomName };
		enum class Result : uint8_t { None, Created, NameRefused, CreateRefused,
		                              Deleted, DeleteFailed };
		Stage stage = Stage::Idle;
		charcreate::CharCreateRequest pending;   // name approval / create
		std::string pendingDelete;
		Result last = Result::None;
		std::string lastMessage;   // eqstr text, as the screen shows it
		std::string randomName;    // the last OP_RandomNameGenerator reply
	};
	void SetWorldScript(const charcreate::WorldScript& script) { m_world_script = script; }
	const charcreate::WorldScript& GetWorldScript() const { return m_world_script; }
	const CharCreateStatus& GetCharCreateStatus() const { return m_create_status; }
	/** OP_ApproveName now; OP_CharacterCreate when world approves. False if
	 *  refused before sending (a request in flight, or a name the server's
	 *  own rules refuse -- eqstr 1576 "Name is not valid."). */
	bool RequestCreateCharacter(const charcreate::CharCreateRequest& req);
	/** CC1_Get_Name_Button. */
	bool RequestRandomName(uint32_t race, uint32_t gender);
	/** Delete_Button + Yes. False if the name is not on the list. */
	bool RequestDeleteCharacter(const std::string& name);

	// ------------------------------------------------------------------
	// B998 (GitLab #318): the character-select SCREEN. While it is on, the
	// automatic OP_EnterWorld at OP_ExpansionInfo is withheld (exactly as a
	// world script withholds it) and the user's Enter World sends it instead.
	// Off -- every run with a configured character -- the handshake is byte
	// for byte what it was. Bodies: login_world_char_select.cpp.
	void SetCharacterSelectScreen(bool on) { m_select_screen = on; }
	bool CharacterSelectScreen() const { return m_select_screen; }
	/** Bumped by every OP_SendCharInfo (0 = no list yet). */
	uint32_t CharacterListGeneration() const { return m_character_list_generation; }
	/** Enter_World_Button: OP_EnterWorld for a listed character. False if the
	 *  name is not listed, a request is in flight, or it was already sent. */
	bool EnterWorldFromSelect(const std::string& name);

private:
	// B888: the reply handlers and the script driver.
	void WorldProcessApproveName(const EQ::Net::Packet &p);
	void WorldProcessRandomName(const EQ::Net::Packet &p);
	void OnCharacterListReceived();
	void RunWorldScript();
	void FinishWorldScript(bool ok, const std::string& reason);
	void EnterWorldAfterCreate(const std::string& name);
	bool CharacterListHas(const std::string& name) const;

	// Login
	void LoginOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void LoginOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by LoginOnPacketRecv and the ReplayLoginPacket test seam.
	void DispatchLoginPacket(const EQ::Net::Packet &p);

	void LoginSendSessionReady();
	void LoginSendLogin();
	void LoginSendServerRequest();
	void LoginSendPlayRequest(uint32_t id);
	void LoginProcessLoginResponse(const EQ::Net::Packet &p);
	void LoginProcessServerPacketList(const EQ::Net::Packet &p);
	void LoginProcessServerPlayResponse(const EQ::Net::Packet &p);

	void LoginDisableReconnect();

	// World
	void ConnectToWorld(const std::string& world_address);

	void WorldOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	// Opcode switch shared by WorldOnPacketRecv and the ReplayWorldPacket test seam.
	void DispatchWorldPacket(const EQ::Net::Packet &p);

	void WorldSendClientAuth();
	void WorldSendEnterWorld(const std::string &character);
	void WorldSendApproveWorld();
	void WorldSendWorldClientCRC();
	void WorldSendWorldClientReady();
	void WorldSendWorldComplete();

	void WorldProcessCharacterSelect(const EQ::Net::Packet &p);
	void WorldProcessGuildsList(const EQ::Net::Packet &p);
	void WorldProcessLogServer(const EQ::Net::Packet &p);
	void WorldProcessApproveWorld(const EQ::Net::Packet &p);
	void WorldProcessEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessPostEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessExpansionInfo(const EQ::Net::Packet &p);
	void WorldProcessMOTD(const EQ::Net::Packet &p);
	void WorldProcessSetChatServer(const EQ::Net::Packet &p);
	void WorldProcessZoneServerInfo(const EQ::Net::Packet &p);

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_login_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_login_connection;
	std::map<uint32_t, WorldServer> m_world_servers;

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_world_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_world_connection;

	// Variables
	std::string m_host;
	int m_port = 0;
	std::string m_user;
	std::string m_pass;
	std::string m_server;

	std::string m_key;
	uint32_t m_dbid = 0;

	// Sequence numbers for packets
	uint32_t m_login_sequence = 2;

	// World server state
	bool m_world_ready = false;
	bool m_enter_world_sent = false;

	// T341 (B038 cluster 4): m_zone_server_host/m_zone_server_port moved here
	// from EverQuest. This family's ZoneServerInfo handler is their only
	// writer; EverQuest::ConnectToZone() and the two T09 read-only accessors
	// (4 test sites) read them back through the getters above.
	std::string m_zone_server_host;
	uint16_t m_zone_server_port = 0;

	// World server state for reconnection
	std::string m_world_server_host;

	int m_character_select_index = -1;
	std::vector<CharacterSelectEntry> m_character_select_list;

	// UCS
	std::string m_ucs_host;
	uint16_t m_ucs_port = 0;
	std::string m_mail_key;
	// T13: the connection-type character the world server concatenates onto
	// the mail key. Titanium sends 'B' on OP_SetChatServer (chat) and 'b' on
	// OP_SetChatServer2 (mail) — see emu_versions.h ucsTitaniumChat/Mail. A
	// UCS implementation needs it to tell the two connections apart, so it is
	// stored rather than discarded. 0 = nothing parsed yet.
	char m_ucs_connection_type = 0;

	// T443 (board B418): OP_GuildsList, parsed. Empty until world sends it.
	eqt::guild::GuildNameTable m_guild_names;

	// B888: the world script and the creation flow's state.
	charcreate::WorldScript m_world_script;
	CharCreateStatus m_create_status;
	bool m_world_script_started = false;
	// Titanium world sets StartInTutorial after EVERY create
	// (world/client.cpp:769-770) and clears it only on OP_World_Client_CRC1
	// (:1134-1138), so an OP_EnterWorld straight after a create would put the
	// character in the tutorial zone (:891-907). The CRC pair is re-sent first.
	bool m_created_this_session = false;

	// B998: the character-select screen is up (see SetCharacterSelectScreen).
	bool m_select_screen = false;
	uint32_t m_character_list_generation = 0;

	ClientContext& ctx_;
};

}  // namespace login_world
}  // namespace eqt
