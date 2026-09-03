#pragma once

// Batch K (refactor/K_component_extraction.md), unit K09.
//
// The zone family, lifted out of class EverQuest: the zone-server handshake
// (stream identify, session ready, zone entry, req-new-zone, AA/tribute
// requests, client ready, server filter), zone entry (NewZone, PlayerProfile,
// Animation, GuildMOTD and the request-phase gate), the zone-stream opcode
// switch DispatchZonePacket, and the zone lifecycle (zone change, graceful
// zone-out, teardown, map/zone-line/S3D source loading, zone-line detection,
// GetBestZ/FixZ).
//
// Every body in src/client/zone/*.cpp is the body that used to live in
// src/client/eq_zone.cpp / eq_zone_dispatch.cpp / eq_zone_entry.cpp /
// eq_zone_handshake.cpp, moved verbatim; the only edits inside a body are the
// substitutions the batch doc's section Mechanism allows (m_x -> ctx_.x /
// owner_.m_x, Foo() -> owner_.Foo() or owner_.<Family>().Foo(), an
// EverQuest-scoped name -> EverQuest::Name, and `this` -> &owner_ where a
// std::bind rebases a handler that stayed on EverQuest).
//
// DispatchZonePacket moves WHOLE (880+ lines). Its arms now read
// owner_.<Family>().ZoneProcess*(p) for every already-extracted family and
// owner_.ZoneProcess*(p) for the five combat handlers K12 still owns. Batch G
// decides its decomposition, not this unit.
//
// The zone connection itself stays on EverQuest: ConnectToZone(), the four
// ZoneOn* callbacks, ReplayZonePacket() and SafeQueueZonePacket() are in
// eq.cpp and are reached by every family, so DispatchZonePacket and the three
// senders those callbacks drive are PUBLIC here (friendship runs
// EverQuest -> component, not the other way; K10 correction 1).
//
// `owner_` is the TRANSITION handle for the zone environment that stays with
// EverQuest for the bridge/renderer (m_zone_map, m_zone_bsp_tree*, the fog and
// sky/zone-type fields, m_zone_audio_manager, m_eq_client_path) and for the
// EverQuest primitives the moved bodies still call (PosX/PosY/PosZ,
// MySpawnId, SetLoadingPhase, QueuePacket, SafeQueueZonePacket,
// AddChatSystemMessage, ...). EverQuest declares
// `friend class eqt::zone::ZoneComponent` for exactly that; K-final removes
// both.
//
// The reference-returning getters below are the mirror image: the entities,
// movement, player and login_world components, eq.cpp and eq_bridge.cpp still
// touch zone state that now lives here. Transition scaffolding too.

#include "client/client_context.h"
#include "client/zone_lines.h"
#include "common/net/daybreak_connection.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

class EverQuest;

namespace EQT {
namespace Graphics {
struct BspTree;
class PfsArchive;
class WldLoader;
}
}

namespace eqt {
namespace zone {

class ZoneComponent {
public:
	ZoneComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

	// Movement history for anti-cheat. Built by the movement family
	// (MovementComponent::UpdateMovement) and seeded by ZoneSendZoneEntry.
	struct MovementHistoryEntry {
		float x, y, z;
		uint8_t type;
		uint32_t timestamp;
	};

	// T231 test seam -- see Test_GetLastZoneChangeRequest().
	struct TestZoneChangeRequest {
		int attempts = 0;
		uint16_t zone_id = 0;
	};

	// Opcode switch shared by EverQuest::ZoneOnPacketRecv and the
	// EverQuest::ReplayZonePacket test seam (both in eq.cpp).
	void DispatchZonePacket(const EQ::Net::Packet &p);

	// Driven by EverQuest::ZoneOnNewConnection / ZoneOnStatusChangeReconnectEnabled
	// (eq.cpp), which own the zone socket.
	void ZoneSendStreamIdentify();
	void ZoneSendAckPacket();
	void ZoneSendZoneEntry();

	// Driven by the entities family (OP_ZoneEntry / OP_NewZone follow-ups) and
	// by the player family's AA/tribute counters.
	void ZoneSendReqNewZone();
	void ZoneSendClientReady();
	void ZoneSendSetServerFilter();
	void ZoneSendSendExpZonein();
	void CheckZoneRequestPhaseComplete();

	// Zone transition methods.
	// T322: zone_reason is the value echoed into OP_ZoneChange's zone_reason
	// field. The official Titanium client echoes back the `type` byte the
	// server handed it in OP_RequestClientZoneChange (always 0x01 in eqemu,
	// zoning.cpp 983/1022); for a client-initiated zone-line crossing there
	// is nothing to echo and 1 (ZoneUnsolicited) is the right default. It is
	// a log-only memo field server-side -- matched for wire fidelity, not
	// because any server logic reads it.
	void RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading, uint32_t zone_reason = 1);
	// T322: the graceful source-zone hand-off the official client performs
	// before its zone-server session goes away. Returns false if it could
	// not be sent (no zone connection), in which case the caller must fall
	// straight through to the hard teardown.
	bool BeginGracefulZoneOut();
	void CleanupZone();  // Cleanup current zone state for zone transition
	void DisconnectFromZone();  // Disconnect from current zone server
	void ProcessDeferredZoneChange();  // Process zone change outside of callback context

	void CheckZoneLine();  // Check if player is in a zone line and handle transition
	void FixZ();

	void SetNavmeshPath(const std::string& path) { m_navmesh_path = path; }
	void SetMapsPath(const std::string& path) { m_maps_path = path; }
	void SetZoningEnabled(bool enabled) { m_zoning_enabled = enabled; }
	bool IsZoningEnabled() const { return m_zoning_enabled; }
	// K09: the member is not graphics-guarded, so neither is this setter (its
	// declaration sat inside eq.h's EQT_HAS_GRAPHICS block; the only callers,
	// in application_zone_loading.cpp, are graphics-only anyway).
	void SetPlayerGraphicsEntityPending(bool v) { m_player_graphics_entity_pending = v; }

	// T148: "GM mode was ON at login", NOT "this account has GM privileges".
	//
	// Populated once, from PlayerProfile.gm at offset 13054, in
	// ZoneProcessPlayerProfile (zone_entry.cpp) -- the SAME parse site and the
	// same once-per-login shape as EverQuest::m_guild_id. Its only server-side
	// writer is Client::SetGM(bool) (eqemu zone/client.cpp:2659), i.e. the `#gm
	// on|off` toggle, which Save()s so the value persists in the character
	// profile.
	//
	// Account status -- what actually authorises GM commands SERVER-side, via
	// Client::Admin() -- never reaches the client. There is no account-status
	// field anywhere in the Titanium protocol. So this flag is the only
	// GM-shaped signal a Titanium client can have, and it is a mode, not a
	// permission. Name any user-facing string accordingly.
	//
	// DELIBERATELY STALE, by Will's decision of 2026-08-19: the check happens
	// ONCE AT LOGIN. A GM who toggles `#gm` mid-session must log out and back
	// in for the client to notice. The AT_GM live-update path (an AT_GM case
	// in ZoneProcessSpawnAppearance, mirroring what T133 did for AT_GUILD_ID)
	// was considered and EXPLICITLY DECLINED -- do not "fix" this by adding
	// one. See refactor/T148_gm_status_gate.md.
	//
	// Offset 13054 is not taken on faith: eqemu's own header comments it
	// `// 0=no, 1=yes (guessing!)`. It was captured off the wire across three
	// logins with the flag toggled between them, with the neighbouring
	// pvp@13052/anon@13053 bytes and birthday@13040 used as controls --
	// review/T148/playerprofile_gm_offset_annotated.txt.
	bool GetGMMode() const { return m_gm_mode; }

	// Bound on how long the transition waits for the acknowledgement. A lost
	// or never-sent reply must not wedge zoning, so the teardown runs anyway.
	// Public: MovementComponent::UpdateMovement() names it in the timeout WARN.
	static constexpr int kZoneOutHandshakeTimeoutMs = 2000;

	// T231 test seam: every RequestZoneChange() call is counted here BEFORE
	// its live-connection gate, so a headless test can pin whether a server
	// packet made the client decide to reply with OP_ZoneChange at all
	// (same-zone: must not; cross-zone: must). Read-only; test-only.
	TestZoneChangeRequest Test_GetLastZoneChangeRequest() const { return m_test_last_zone_change_request; }

	// T322 test seams: the zone-transition connection lifecycle. The
	// hand-off itself is gated on a live zone connection (there is nobody
	// to hand off to without one), which no headless test can supply, so
	// Test_BeginGracefulZoneOutForSeam() lifts exactly that one gate and
	// nothing else -- the packets it queues, the flags it arms and the
	// OP_LogoutReply routing that follows are the production code paths.
	// Read-only otherwise; test-only. See tests/test_zone_out_handshake.cpp.
	// force_gate=false leaves the live-connection gate in place, so the
	// no-connection fallback can be pinned too.
	bool Test_BeginGracefulZoneOutForSeam(bool force_gate = true) {
		m_test_force_zone_out_handshake = force_gate;
		const bool armed = BeginGracefulZoneOut();
		m_test_force_zone_out_handshake = false;
		return armed;
	}
	void Test_DisconnectFromZoneForSeam() { DisconnectFromZone(); }
	bool Test_IsZoneOutHandshakePending() const { return m_zone_out_handshake_pending; }
	bool Test_IsZoneOutAcknowledged() const { return m_zone_out_acknowledged; }
	bool Test_IsZoneToZoneInProgress() const { return m_zone_to_zone_in_progress; }
	void Test_SetZoneToZoneInProgressForSeam(bool v) { m_zone_to_zone_in_progress = v; }

	// K09 transition scaffolding: zone state whose remaining readers/writers
	// live in the entities, movement, player and login_world components, in
	// eq.cpp or in eq_bridge.cpp. Reference-returning so a moved call site is
	// a name substitution and nothing else. K-final folds them in.
	bool& ZoneEntrySent() { return m_zone_entry_sent; }
	bool& WeatherReceived() { return m_weather_received; }
	bool& ReqNewZoneSent() { return m_req_new_zone_sent; }
	bool& ServerFilterSent() { return m_server_filter_sent; }
	bool& ExpZoneinSent() { return m_exp_zonein_sent; }
	bool& SendExpZoneinReceived() { return m_send_exp_zonein_received; }
	bool& PlayerGraphicsEntityPending() { return m_player_graphics_entity_pending; }
	bool& ZoneOutHandshakePending() { return m_zone_out_handshake_pending; }
	bool& ZoneOutAcknowledged() { return m_zone_out_acknowledged; }
	std::chrono::steady_clock::time_point& ZoneOutHandshakeDeadline() { return m_zone_out_handshake_deadline; }
	bool& ZoneToZoneInProgress() { return m_zone_to_zone_in_progress; }
	std::unique_ptr<EQT::ZoneLines>& ZoneLinesPtr() { return m_zone_lines; }
	std::deque<MovementHistoryEntry>& MovementHistory() { return m_movement_history; }
	uint32_t& LastMovementHistorySend() { return m_last_movement_history_send; }
	std::string& NavmeshPath() { return m_navmesh_path; }
	uint32_t& Exp() { return m_exp; }
	uint32_t& ExpAA() { return m_exp_aa; }
	int& AATableCount() { return m_aa_table_count; }
	int& TributeCount() { return m_tribute_count; }
	int& GuildTributeCount() { return m_guild_tribute_count; }
	uint32_t& PendingScribeSpellId() { return m_pending_scribe_spell_id; }
	uint16_t& PendingScribeBookSlot() { return m_pending_scribe_book_slot; }
	int16_t& PendingScribeSourceSlot() { return m_pending_scribe_source_slot; }

private:
	// Zone handshake senders reached only from inside the family.
	void ZoneSendReqClientSpawn();
	void ZoneSendSendAATable();
	void ZoneSendUpdateAA();
	void ZoneSendSendTributes();
	void ZoneSendRequestGuildTributes();

	// Zone-stream packet handlers, all reached from DispatchZonePacket.
	void ZoneProcessNewZone(const EQ::Net::Packet &p);
	void ZoneProcessPlayerProfile(const EQ::Net::Packet &p);
	void ZoneProcessAnimation(const EQ::Net::Packet &p);  // OP_Animation (T243 rename)
	void ZoneProcessGuildMOTD(const EQ::Net::Packet &p);
	void ZoneProcessZoneChange(const EQ::Net::Packet &p);
	void ZoneProcessRequestClientZoneChange(const EQ::Net::Packet &p);

	// T231: same-zone OP_RequestClientZoneChange -- apply the destination
	// locally (state + renderer + one forced OP_ClientUpdate), no zone cycle.
	void RelocateWithinZone(float x, float y, float z, float server_heading);

	// Map / zone-source / zone-line loading.
	void LoadZoneMap(const std::string& zone_name);
	float GetBestZ(float x, float y, float z);
	// Zone source data (S3D/WLD) -- opens archive and parses WLD once for game
	// state + graphics.
	void LoadZoneSources(const std::string& zone_name);
	void LoadZoneLines(const std::string& zone_name);

	// Zone S3D/WLD sources -- opened/parsed once at game state layer, shared
	// with the sequential loader.
	std::shared_ptr<EQT::Graphics::PfsArchive> m_zone_archive;
	std::shared_ptr<EQT::Graphics::WldLoader> m_zone_wld;

	// Zone state
	bool m_zone_entry_sent = false;
	bool m_weather_received = false;
	bool m_req_new_zone_sent = false;

	bool m_new_zone_received = false;
	bool m_aa_table_sent = false;
	bool m_update_aa_sent = false;
	bool m_tributes_sent = false;
	bool m_guild_tributes_sent = false;
	bool m_req_client_spawn_sent = false;
	// K09: written by CleanupZone and read by nothing -- ZoneSendSpawnAppearance,
	// its only reader, had zero callers tree-wide and was deleted here
	// (section Mechanism 5). Same shape as m_client_spawned. Boarded.
	bool m_spawn_appearance_sent = false;
	bool m_exp_zonein_sent = false;
	bool m_send_exp_zonein_received = false;
	bool m_server_filter_sent = false;
	bool m_client_spawned = false;
	int m_aa_table_count = 0;
	int m_tribute_count = 0;
	int m_guild_tribute_count = 0;

	bool m_player_graphics_entity_pending = false;  // True when player entity needs to be created on renderer after zoning

	// Character experience. T286: everything PlayerState carries lives in
	// m_game_state.player(); only what it does not carry is here.
	uint32_t m_exp = 0;          // Current experience points
	uint32_t m_exp_aa = 0;       // Current AA experience points

	float m_safe_x = 0, m_safe_y = 0, m_safe_z = 0;  // Zone safe coordinates (client coords)

	std::string m_navmesh_path;
	std::string m_maps_path;

	// Zone lines for zone transitions
	std::unique_ptr<EQT::ZoneLines> m_zone_lines;

	// Zone line detection state
	bool m_zoning_enabled = false;                                   // Whether zone line detection triggers zoning (Ctrl+Z toggles)

	// T322: graceful zone-out hand-off state. The official Titanium client
	// does NOT close its source-zone socket on a zone change: it sends
	// OP_SaveOnZoneReq then OP_DeleteSpawn(own entity id) and lets the SERVER
	// close the session (eqemu Handle_OP_DeleteSpawn -> Disconnect()). That
	// difference decides whether the group survives -- see the block comment
	// on BeginGracefulZoneOut() in zone_component.cpp.
	//
	// m_zone_out_handshake_pending: hand-off sent, waiting for the source
	// zone's OP_LogoutReply. While set, PlayerComponent::ZoneProcessLogoutReply()
	// treats that reply as a zone-out acknowledgement and NOT as a logout --
	// the logout path (SendLogout/camp, and the world-side disconnect) is
	// untouched whenever this flag is clear.
	bool m_zone_out_handshake_pending = false;
	bool m_zone_out_acknowledged = false;
	std::chrono::steady_clock::time_point m_zone_out_handshake_deadline{};
	// T322 test seam (see Test_BeginGracefulZoneOutForSeam): lifts the
	// live-connection gate in BeginGracefulZoneOut() only. Never set in
	// production.
	bool m_test_force_zone_out_handshake = false;

	// T322: "this world reconnect is a ZONE TRANSITION, not a fresh login."
	// It cannot live in WorldState: CleanupZone() -> resetZoneState() clears
	// every zone flag, and it runs (inside DisconnectFromZone) BEFORE the
	// world reconnect that has to read it -- which is why the zoning byte in
	// OP_SendLoginInfo was always 0. Set in ProcessDeferredZoneChange(),
	// cleared once the destination zone session is up (or on a logout).
	bool m_zone_to_zone_in_progress = false;

	// Pending spell scribe state (for handling server confirmation). Written
	// by MerchantComponent::RequestScribeSpell, cleared by the OP_MemorizeSpell
	// arm of DispatchZonePacket.
	uint32_t m_pending_scribe_spell_id = 0;
	uint16_t m_pending_scribe_book_slot = 0;
	int16_t m_pending_scribe_source_slot = -1;

	// T148: PlayerProfile.gm @13054 -- "GM mode was on at login". See
	// GetGMMode() for the full contract (login-time only, deliberately
	// stale, NOT an account-privilege flag).
	bool m_gm_mode = false;

	std::deque<MovementHistoryEntry> m_movement_history;
	uint32_t m_last_movement_history_send = 0;

	TestZoneChangeRequest m_test_last_zone_change_request;

	ClientContext& ctx_;
	EverQuest& owner_;
};

}  // namespace zone
}  // namespace eqt
