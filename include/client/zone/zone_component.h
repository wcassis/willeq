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
// T366 (B038 CLOSED): the `owner_` TRANSITION handle and the
// `friend class eqt::zone::ZoneComponent` line are both GONE. Its last use was
// std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, &owner_, ...) --
// a PRIVATE member function taken by address, and the only axis-2 use left
// tree-wide. That function is zone territory and moved here.
//
// The reference-returning getters below are the mirror image: the entities,
// movement, player and login_world components, eq.cpp and eq_bridge.cpp still
// touch zone state that now lives here. Transition scaffolding too.

#include "client/client_context.h"
#include "client/game_options.h"  // B433: ServerFilterPayload
#include "client/player/profile_appearance.h"  // T341: m_profile_appearance
#include "client/zone_lines.h"
#include "common/net/daybreak_connection.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

class EverQuest;
class HCMap;

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
	// T366 (B038): the EverQuest& transition handle is gone -- this component
	// names nothing on EverQuest any more.
	explicit ZoneComponent(ClientContext& ctx) : ctx_(ctx) {}

	// T341 (B038 cluster 4): the player's own PlayerProfile appearance.
	// ZoneProcessPlayerProfile is the only writer; EverQuest forwards
	// GetProfileAppearance()/SetProfileAppearance() here for the action layer
	// and the tests. CHECK .valid BEFORE USE (T167).
	const EQT::ProfileAppearance& GetProfileAppearance() const { return m_profile_appearance; }
	void SetProfileAppearance(const EQT::ProfileAppearance& appearance) { m_profile_appearance = appearance; }


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
	// B760 (#45): `reliable` is whether the packet came off the reliable,
	// in-order stream (DaybreakConnection::DeliveringReliable()). A replayed
	// or test packet has no wire and defaults to reliable.
	void DispatchZonePacket(const EQ::Net::Packet &p, bool reliable = true);

	// G01 (refactor/G01_dispatch_zone_packet_table.md): the opcode table
	// DispatchZonePacket dispatches through, and the deliberate no-ops.
	//
	// A captureless lambda per arm, so ONE handler type covers both the
	// in-family handlers below and the sibling components' ZoneProcess*
	// entry points, with no per-sibling forwarder in this header. Both are
	// public so tests/test_zone_dispatch_table.cpp can assert the coverage
	// invariant: every case label the pre-G01 switch had is a key of exactly
	// one of these two maps, and neither map has a key that was not a label.
	using ZoneArmHandler = void (*)(ZoneComponent&, const EQ::Net::Packet&);
	static const std::unordered_map<uint16_t, ZoneArmHandler>& ZoneOpcodeTable();
	// opcode -> the reason its former arm gave for doing nothing.
	static const std::unordered_map<uint16_t, const char*>& IgnoredZoneOpcodes();

	// T366 (B038 cluster 3): moved here from EverQuest, whose only two callers
	// were the std::binds below in this file. Installed on the zone connection
	// manager by SendGracefulZoneOut() and DisconnectFromZone() so a close we
	// initiated is not treated as a fault worth reconnecting from.
	void ZoneOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn,
	                                         EQ::Net::DbProtocolStatus from,
	                                         EQ::Net::DbProtocolStatus to);

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
	// B433: the Options window's Filters page. Stores the payload every later
	// OP_SetServerFilter carries (zone-in included) and, once the zone-in one
	// has gone out, sends it again now -- eqemu accepts the opcode while
	// connected (Client::Handle_OP_SetServerFilter).
	void SetServerFilters(const eqt::ServerFilterPayload& filters);
	const eqt::ServerFilterPayload& ServerFilters() const { return m_server_filters; }
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

	// K13 (Batch K): the ZONE ENVIRONMENT, re-homed here from EverQuest.
	// K11 measured the shape and left the move to this unit: every one of
	// these is WRITTEN by this family (ZoneProcessNewZone parses the fog/sky/
	// zone-type block; LoadZoneSources/LoadZoneLines/CleanupZone own the map
	// and the BSP tree's whole lifecycle) and READ by exactly one bridge body
	// (BridgeComponent::CreateZoneLoadSnapshot) plus one movement body (water
	// detection). The reference-returning accessors below are the same shape
	// K09 gave MovementHistory() and K12 gave VendorItems().
	std::shared_ptr<const HCMap>& ZoneMap() { return m_zone_map; }
	// T246: the zone's collision map (null until a zone's .map is loaded);
	// game-thread readers (SpellManager::checkLineOfSight) take a copy of
	// the shared_ptr so a zone change cannot free it mid-query (P35).
	std::shared_ptr<const HCMap> GetZoneMap() const { return m_zone_map; }
	std::shared_ptr<EQT::Graphics::BspTree>& ZoneBspTree() { return m_zone_bsp_tree; }
	std::string& ZoneBspTreeZoneName() { return m_zone_bsp_tree_zone_name; }
	uint8_t& ZoneSkyType() { return m_zone_sky_type; }
	uint8_t& ZoneTypeCode() { return m_zone_type; }
	uint8_t* ZoneFogRed() { return m_zone_fog_red; }
	uint8_t* ZoneFogGreen() { return m_zone_fog_green; }
	uint8_t* ZoneFogBlue() { return m_zone_fog_blue; }
	float* ZoneFogMinClip() { return m_zone_fog_minclip; }
	float* ZoneFogMaxClip() { return m_zone_fog_maxclip; }

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


	// G01: one named handler per former switch arm that carried local logic
	// (the arms that were a single call go straight into the table). Bodies are
	// the arm bodies verbatim; reached only through ZoneOpcodeTable().
	void OnSessionReady(const EQ::Net::Packet &p);
	void OnZoneEntry(const EQ::Net::Packet &p);
	void OnTaskDescription(const EQ::Net::Packet &p);
	void OnTaskActivity(const EQ::Net::Packet &p);
	void OnClearLeadershipAbilities(const EQ::Net::Packet &p);
	void OnClearAA(const EQ::Net::Packet &p);
	void OnAAExpUpdate(const EQ::Net::Packet &p);
	void OnLootRequest(const EQ::Net::Packet &p);
	void OnLootItem(const EQ::Net::Packet &p);
	void OnLootComplete(const EQ::Net::Packet &p);
	void OnItemPacket(const EQ::Net::Packet &p);
	void OnTargetCommandResponse(const EQ::Net::Packet &p);  // HC_OP_TargetCommandReply (T374; was the bare 0x61f9)
	void OnBecomeCorpse(const EQ::Net::Packet &p);
	void OnTargetHoTT(const EQ::Net::Packet &p);
	void OnSkillUpdate(const EQ::Net::Packet &p);
	void OnCancelTrade(const EQ::Net::Packet &p);
	void OnTradeRequest(const EQ::Net::Packet &p);
	void OnTradeRequestAck(const EQ::Net::Packet &p);
	void OnTradeCoins(const EQ::Net::Packet &p);
	void OnTradeAcceptClick(const EQ::Net::Packet &p);
	void OnFinishTrade(const EQ::Net::Packet &p);
	void OnPreLogoutReply(const EQ::Net::Packet &p);
	void OnMobRename(const EQ::Net::Packet &p);
	void OnStun(const EQ::Net::Packet &p);
	void OnInterruptCast(const EQ::Net::Packet &p);  // T469 (B604)
	void OnMemorizeSpell(const EQ::Net::Packet &p);
	void OnSpecialMesg(const EQ::Net::Packet &p);
	void OnBegging(const EQ::Net::Packet &p);
	void OnHide(const EQ::Net::Packet &p);
	void OnSneak(const EQ::Net::Packet &p);
	void OnSenseHeading(const EQ::Net::Packet &p);
	void OnForage(const EQ::Net::Packet &p);
	void OnFishing(const EQ::Net::Packet &p);
	void OnMend(const EQ::Net::Packet &p);
	void OnFeignDeath(const EQ::Net::Packet &p);
	void OnPickPocket(const EQ::Net::Packet &p);
	void OnSenseTraps(const EQ::Net::Packet &p);
	void OnDisarmTraps(const EQ::Net::Packet &p);
	void OnInstillDoubt(const EQ::Net::Packet &p);

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

	// K13: the zone environment (was EverQuest's).
	//
	// Map for Z-height calculations. shared_ptr<const HCMap> (P35): the
	// renderer/SimulationWorker boundary holds copies of this same
	// shared_ptr (via CollisionMapChangedData / SimulationZoneData::hcMap),
	// so a zone-change reset() here only drops the game thread's
	// reference -- the object survives until every renderer-side holder
	// (collisionMap_, SimulationWorker's zoneData_.hcMap) also releases
	// it, closing the P34-adjacent CRITICAL UAF where the game thread
	// freed the old HCMap out from under the SimulationWorker thread
	// mid-zone-transition. HCMap is immutable zone data (all read paths
	// are const methods) so const-ness costs nothing on the game-thread
	// side.
	std::shared_ptr<const HCMap> m_zone_map;
	// D20f2: Typed BSP tree for water detection, zone lines, collision
	std::shared_ptr<EQT::Graphics::BspTree> m_zone_bsp_tree;
	// P17: zone name m_zone_bsp_tree was built for (set by LoadZoneSources()
	// and SetZoneBspTree()). Empty whenever m_zone_bsp_tree is null. Used by
	// CreateZoneLoadSnapshot() to detect a stale tree that outlived a
	// CleanupZone() reset. T260: LoadZoneSources() did not set it, so every
	// zone-in since P17 skipped the region lookup with a spurious "stale"
	// WARN (captured for '') -- see refactor/T260_bsp_region_size_max.md.
	std::string m_zone_bsp_tree_zone_name;
	// B1068: CheckZoneLine's per-zone state. The BSP zone-line region count
	// is cached against the tree it was taken from; the sweep is primed by
	// the first check after LoadZoneLines (the zone-in warp is never swept).
	const EQT::Graphics::BspTree* m_zone_line_count_tree = nullptr;
	size_t m_bsp_zone_line_regions = 0;
	bool m_zone_line_sweep_primed = false;
	size_t m_skipped_same_zone_region = SIZE_MAX;
	// Zone environment data from NewZone packet
	uint8_t m_zone_sky_type = 0;           // Sky type for rendering (0-255)
	uint8_t m_zone_type = 0;               // Zone type (0=outdoor, 1=dungeon, etc.)
	uint8_t m_zone_fog_red[4] = {0};       // Fog colors for 4 fog ranges
	uint8_t m_zone_fog_green[4] = {0};
	uint8_t m_zone_fog_blue[4] = {0};
	float m_zone_fog_minclip[4] = {0};     // Fog min clip distances
	float m_zone_fog_maxclip[4] = {0};     // Fog max clip distances

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
	// K15 (B035): m_spawn_appearance_sent and m_client_spawned DELETED. K09
	// deleted their only readers (ZoneSendSpawnAppearance, ZoneSendSessionReady
	// -- both unreachable and unguarded), leaving two flags CleanupZone reset
	// and nothing consulted. The live spawn-ready state is
	// game_state.world().clientReady(), reset alongside them.
	bool m_exp_zonein_sent = false;
	bool m_send_exp_zonein_received = false;
	bool m_server_filter_sent = false;
	eqt::ServerFilterPayload m_server_filters = eqt::defaultServerFilterPayload();  // B433
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

	// T341 (B038 cluster 4): m_profile_appearance moved here from EverQuest.
	// ZoneProcessPlayerProfile (zone_entry.cpp) is its ONLY writer; EverQuest
	// keeps GetProfileAppearance()/SetProfileAppearance() as forwarders for
	// eq_action_handler_spells_trade.cpp's /appearance round trip and the
	// six test sites in tests/test_profile_appearance.cpp.
	// T167: the seven PlayerProfile appearance fields. CHECK .valid BEFORE
	// USE -- zero is a legal value for every field, so it is the only thing
	// separating an unknown appearance from a real all-zero one.
	EQT::ProfileAppearance m_profile_appearance;

	std::deque<MovementHistoryEntry> m_movement_history;
	uint32_t m_last_movement_history_send = 0;

	TestZoneChangeRequest m_test_last_zone_change_request;

	ClientContext& ctx_;
};

}  // namespace zone
}  // namespace eqt
