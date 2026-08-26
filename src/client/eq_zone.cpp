// EverQuest zone lifecycle cluster: zone change/teardown and zone geometry.
//
// Split out of eq.cpp (Batch F, unit F09):
//  - Zone change / teardown: ZoneProcessRequestClientZoneChange,
//    ZoneProcessZoneChange, RequestZoneChange, CleanupZone,
//    DisconnectFromZone, ProcessDeferredZoneChange.
//  - Zone geometry: LoadZoneMap, LoadZoneLines, LoadZoneSources,
//    CheckZoneLine, GetBestZ, FixZ.
// Moved verbatim, no behavior change. See refactor/F09_zone.md.
//
// NOTE: ZoneProcessLevelUpdate (immediately preceding this cluster in
// eq.cpp) is an unrelated player-stat packet handler, not part of the
// zone-lifecycle cluster, and stays in eq.cpp.
//
// NOTE: The pathfinding cluster (LoadPathfinder, FindPath, MoveToWithPath,
// FollowPath), which sits between ProcessDeferredZoneChange and
// LoadZoneMap in eq.cpp, belongs to a future unit (F08, movement) and is
// intentionally left in eq.cpp.

#include "client/eq.h"
#include "client/hc_map.h"
#include "client/pathfinder_interface.h"
#include "client/spell/buff_manager.h"
#include "client/zone_lines.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/net/posix_udp_transport.h"

#include <chrono>
#include <cmath>
#include <cstring>

// ============================================================================
// Zone change / teardown
// ============================================================================

void EverQuest::ZoneProcessRequestClientZoneChange(const EQ::Net::Packet &p)
{
	// Server is requesting that we initiate a zone change
	// Uses RequestClientZoneChange_Struct (24 bytes)

	if (p.Length() < 26) {  // 2 bytes opcode + 24 bytes struct
		LOG_WARN(MOD_ZONE, "RequestClientZoneChange packet too small: {} bytes", p.Length());
		return;
	}

	// Parse RequestClientZoneChange_Struct
	uint16_t zone_id = p.GetUInt16(2);
	uint16_t instance_id = p.GetUInt16(4);
	float zone_y = p.GetFloat(6);   // Server sends y first
	float zone_x = p.GetFloat(10);  // Then x
	float zone_z = p.GetFloat(14);
	float heading = p.GetFloat(18);
	uint32_t type = p.GetUInt32(22);

	// Swap x/y for our coordinate system
	float target_x = zone_y;
	float target_y = zone_x;
	float target_z = zone_z;

	LOG_INFO(MOD_ZONE, "Server requests zone change: zone={} instance={} pos=({:.1f}, {:.1f}, {:.1f}) heading={:.1f} type={}",
		zone_id, instance_id, target_x, target_y, target_z, heading, type);

	// T231: a request naming the zone we are ALREADY in is a same-zone
	// teleport (GM summon, #goto, /rewind, succor into the same zone, ...),
	// not a zone change. eqemu's Client::ZonePC (akk-stack zone/zoning.cpp
	// 1055-1068) documents the contract: "Clear zonesummon variables if
	// we're zoning to our own zone. Client wont generate a zone change packet
	// to the server in this case" -- it resets zone_mode to ZoneUnsolicited
	// and waits for the client's next OP_ClientUpdate. Replying with
	// OP_ZoneChange anyway (what this handler did before T231) made the
	// server treat it as an unsolicited zone request with no zone point in
	// range -> SendZoneCancel (zoning.cpp 113) -> a full teardown/reconnect
	// that landed the client back where it started (T229's finding,
	// review/T229/). The same-zone test mirrors the server's own
	// (zoneID == zone->GetZoneID() && instance_id == zone->GetInstanceID());
	// this Titanium client only ever occupies instance 0 (RequestZoneChange
	// always sends instance_id 0), so a non-zero instance_id is a genuine
	// cross-instance move and keeps the handshake path. An unknown current
	// zone (m_current_zone_id == 0, no NewZone yet) also keeps the old path
	// rather than guessing.
	const bool same_zone = (m_current_zone_id != 0
		&& zone_id == m_current_zone_id
		&& instance_id == 0);
	if (same_zone) {
		LOG_INFO(MOD_ZONE, "Same-zone relocation (zone {}): applying locally, no OP_ZoneChange reply",
			zone_id);
		RelocateWithinZone(target_x, target_y, target_z, heading);
		return;
	}

	// Respond with OP_ZoneChange to initiate the actual zone change
	// The server will then respond with OP_ZoneChange with success=1
	// T52 ride-along: this is a reply to a SERVER-initiated request
	// (RequestClientZoneChange_Struct, "the server is requesting..." per
	// the comment above), so it is solicited -- zone_reason should read
	// ZoneSolicited (0), not the previous hardcoded ZoneUnsolicited (1).
	// Cosmetic only: eqemu only logs zone_reason, never uses it for logic
	// (verified against the real server source; see this unit's Review).
	RequestZoneChange(zone_id, target_x, target_y, target_z, heading, /*solicited=*/true);
}

void EverQuest::RelocateWithinZone(float x, float y, float z, float server_heading)
{
	// T231: local teleport inside the current zone. x/y/z are already in
	// client coordinates (the caller swapped the wire's y/x); server_heading
	// is the wire float, in eqemu's 0-512 heading units -- the same field
	// PlayerProfile carries at offset 13128 (m_pp.heading == m_Position.w).
	//
	// Heading conversion is a verbatim copy of ZoneProcessPlayerProfile's
	// (eq.cpp, "LOAD-BEARING -- DO NOT HARMONIZE"): this is the PLAYER's own
	// heading arriving from the server, exactly the case that comment
	// covers, so it gets the same `90 - server_heading_deg` flip and the
	// same wrap. Deliberately NOT factored into a shared helper -- see
	// docs/CLAUDE.md, Heading/Rotation Conventions.
	float server_heading_deg = server_heading * 360.0f / 512.0f;
	float new_heading = 90.0f - server_heading_deg;
	if (new_heading < 0.0f) new_heading += 360.0f;
	if (new_heading >= 360.0f) new_heading -= 360.0f;

	// Drop any in-progress scripted movement first: a MoveTo/path target
	// left behind would march the character straight back toward the
	// pre-teleport destination on the next UpdateMovement tick. Same fields
	// StopMovement() clears, minus its own SendPositionUpdate() -- the
	// forced update below is the one that must carry the new position.
	m_current_path.clear();
	m_current_path_index = 0;
	m_target_x = m_target_y = m_target_z = 0.0f;
	m_animation = ANIM_STAND;
	SetMoving(false);

	// Same write path a real zone-in uses (T55/T58): both the raw members
	// and m_game_state.player().
	SetPosition(x, y, z);
	SetHeading(new_heading);

	LOG_INFO(MOD_ZONE, "Relocated within zone to client=({:.2f}, {:.2f}, {:.2f}) heading={:.2f}deg (wire heading {:.2f})",
		m_x, m_y, m_z, m_heading, server_heading);

	// Move the renderer's player/camera the same way PlayerProfile does on
	// zone-in: a hand-rolled PlayerMoved on the bridge, heading in the 512
	// format IrrlichtRenderer::setPlayerPosition() expects. Without this, in
	// Player mode the renderer keeps its own copy of the position and its
	// next PlayerPositionChanged intent would drag m_x/m_y/m_z straight
	// back. (SetPosition/SetHeading only fire on the state layer's own
	// EventBus, which never reaches the bridge -- T58 Review trace table.)
	if (m_bridge) {
		eqt::state::PlayerMovedData data;
		data.x = m_x; data.y = m_y; data.z = m_z;
		data.heading = m_heading * 512.0f / 360.0f;
		data.dx = 0; data.dy = 0; data.dz = 0;
		data.isMoving = false;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(data)));
	}

	// Tell the server where we landed. For GMSummon the server already set
	// its own m_Position (zoning.cpp 878) and broadcast it (1033); for a
	// ZoneSolicited same-zone move (#goto and friends) it did NOT -- it
	// relies entirely on this OP_ClientUpdate. Force it past the 250 ms
	// throttle and with zero deltas (a teleport has no velocity; a
	// last-position delta spanning the jump would be sent as one).
	m_force_next_position_update = true;
	SendPositionUpdate();
}

void EverQuest::ZoneProcessZoneChange(const EQ::Net::Packet &p)
{
	// Server response to our zone change request (or server-initiated zone change)
	// Uses ZoneChange_Struct (88 bytes)

	if (p.Length() < 90) {  // 2 bytes opcode + 88 bytes struct
		LOG_WARN(MOD_ZONE, "ZoneChange packet too small: {} bytes", p.Length());
		return;
	}

	// Parse ZoneChange_Struct
	char char_name[65] = {0};
	memcpy(char_name, static_cast<const char*>(p.Data()) + 2, 64);
	uint16_t zone_id = p.GetUInt16(66);
	uint16_t instance_id = p.GetUInt16(68);
	float zone_y = p.GetFloat(70);  // Server sends y first
	float zone_x = p.GetFloat(74);  // Then x
	float zone_z = p.GetFloat(78);
	uint32_t zone_reason = p.GetUInt32(82);
	int32_t success = p.GetInt32(86);

	// Swap x/y for our coordinate system
	float target_x = zone_y;
	float target_y = zone_x;
	float target_z = zone_z;

	LOG_DEBUG(MOD_ZONE, "Zone change response: zone={} instance={} pos=({:.1f}, {:.1f}, {:.1f}) reason={} success={}",
		zone_id, instance_id, target_x, target_y, target_z, zone_reason, success);

	// Check success code
	// success == 1 means approved
	// success != 1 means denied (various error codes)
	if (success != 1) {
		LOG_WARN(MOD_ZONE, "Zone change DENIED (success={})", success);
		m_zone_change_requested = false;
		m_zone_line_triggered = false;
		m_pending_zone_id = 0;
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Zone change APPROVED to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, target_x, target_y, target_z);

	// Store the target coordinates for the new zone
	m_pending_zone_id = zone_id;
	m_pending_zone_x = target_x;
	m_pending_zone_y = target_y;
	m_pending_zone_z = target_z;

	// T55: mirror producer B's WorldState publish (CheckZoneLine, further
	// down in this file, "Phase 7.3: Also update WorldState") so the
	// pending-zone state this producer stores is visible from GameState
	// too, not just via the raw EverQuest members. This packet's reply
	// (ZoneChange_Struct) carries no client heading field, so pass 0.0f --
	// unlike producer B, this producer never populates m_pending_zone_heading
	// at all. See refactor/T55_position_state_propagation.md.
	m_game_state.world().setPendingZone(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, 0.0f);

	// Zone-to-zone transition flow:
	// 1. Disconnect from current zone server
	// 2. Reconnect to world server (with zoning=1 flag)
	// 3. World server sends ZoneServerInfo for new zone
	// 4. Connect to new zone server
	// 5. Send WorldComplete, world disconnects (normal)

	// Close vendor window if open (we're leaving the zone)
	if (IsVendorWindowOpen()) {
		LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to zone change");
		CloseVendorWindow();
	}

	// IMPORTANT: We cannot directly call DisconnectFromZone() here because we're
	// inside a packet callback. Destroying the connection manager from within its
	// own callback causes issues with libuv handle cleanup.
	// Instead, we set a flag and let UpdateMovement() handle the disconnect safely.
	m_zone_change_approved = true;
	LOG_TRACE(MOD_ZONE, "Zone change approved, will process disconnect on next update");
}

void EverQuest::RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading, bool solicited)
{
	// T231 test seam: record the attempt before the connection gate, so a
	// headless test can tell "decided to reply with OP_ZoneChange" from
	// "decided not to" -- the send itself is unreachable without a live
	// m_zone_connection. See Test_GetLastZoneChangeRequest.
	m_test_last_zone_change_request.attempts++;
	m_test_last_zone_change_request.zone_id = zone_id;

	if (!IsFullyZonedIn() || !m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Cannot request zone change - not fully zoned in");
		return;
	}

	if (m_zone_change_requested) {
		LOG_DEBUG(MOD_ZONE, "Zone change already pending, ignoring duplicate request");
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Requesting zone change to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, x, y, z);

	// Build ZoneChange_Struct packet
	// The packet is 90 bytes (2 opcode + 88 struct)
	EQ::Net::DynamicPacket p;
	p.Resize(90);  // 2 bytes opcode + 88 bytes ZoneChange_Struct
	memset(p.Data(), 0, 90);

	p.PutUInt16(0, HC_OP_ZoneChange);

	// char_name[64] at offset 2
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 2, m_character.c_str(), name_len);

	// zone_id at offset 66
	p.PutUInt16(66, zone_id);

	// instance_id at offset 68 (0 for non-instanced zones)
	p.PutUInt16(68, 0);

	// Server expects Y, X, Z order (opposite of our internal order).
	// Established convention (T52, verified against live PlayerProfile wire
	// evidence, which uses the identical relationship): our internal
	// m_x = server_y, m_y = server_x. Inverting that to build an outgoing
	// packet: the wire's y-field (offset 70) must carry OUR x, and the
	// wire's x-field (offset 74) must carry OUR y.
	//
	// T52 fix: this used to be backwards (PutFloat(70, y) / PutFloat(74, x)
	// -- our y into the server's y slot, our x into the server's x slot --
	// which is a straight, unswapped copy and contradicts the parse-side
	// convention used everywhere else in this codebase, e.g. eq.cpp's
	// PlayerProfile handler and ZoneProcessZoneChange just above). No live
	// symptom was traced to this: in every path this unit could exercise,
	// the server decides the actual destination itself (ZonePC for a
	// server-solicited teleport, the zone's own zone_points row for a
	// zone-line crossing) without reading back the position fields on this
	// outgoing request, so this was a real but currently-inert bug. Fixed
	// as a correctness hardening, not because it explains any observed
	// misbehavior -- see refactor/T52_zone_position_convention.md Review.
	p.PutFloat(70, x);  // Server's y-field = our x
	p.PutFloat(74, y);  // Server's x-field = our y
	p.PutFloat(78, z);

	// zone_reason at offset 82 (T52 ride-along, cosmetic only -- confirmed
	// against the real eqemu source that zone_reason is a log-only "memo
	// field", never used for server-side logic; see the Review)
	// 0 = ZoneSolicited (server-requested)
	// 1 = ZoneUnsolicited (client-initiated, like zone line)
	p.PutUInt32(82, solicited ? 0 : 1);

	// success at offset 86
	// 0 = request (we're asking)
	// 1 = response (server's reply)
	p.PutInt32(86, 0);  // This is a request

	DumpPacket("C->S", HC_OP_ZoneChange, p);

	if (SafeQueueZonePacket(p)) {
		m_zone_change_requested = true;
		LOG_DEBUG(MOD_ZONE, "Zone change request sent to server");
	} else {
		LOG_ERROR(MOD_ZONE, "Failed to send zone change request");
	}
}

void EverQuest::CleanupZone()
{
	LOG_DEBUG(MOD_ZONE, "Cleaning up current zone state");

	// Stop the update loop first - this must happen before zone transition
	// so that ZoneProcessClientUpdate() can restart it for the new zone
	StopUpdateLoop();

	SetLoadingPhase(LoadingPhase::DISCONNECTED, "Leaving zone...");

#ifdef EQT_HAS_GRAPHICS
	// D20e1: Zone is no longer ready - show loading screen via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneUnloading,
			eqt::state::ZoneUnloadingData{}));
	}
#endif

	// Stop any movement
	StopMovement();
	m_is_moving = false;
	m_current_path.clear();
	m_current_path_index = 0;
	m_follow_target.clear();
	m_in_combat_movement = false;
	m_combat_target.clear();

	// Phase 7.6: Sync combat state reset to GameState
	m_game_state.combat().setInCombatMovement(false);
	m_game_state.combat().clearCombatTarget();

	// Clear entity map completely - server will re-send all entity data for the new zone
	// (including the player with a fresh spawn_id)
	m_entities.clear();
	m_game_state.entities().clear();

	// Clear door data (T219: doors live in the write-model GameState)
	m_game_state.doors().clear();

	// Clear pet state (pet won't exist in new zone)
	if (m_pet_spawn_id != 0) {
		m_pet_spawn_id = 0;
		std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
		// Clear pet buffs
		if (m_buff_manager) {
			m_buff_manager->clearPetBuffs();
		}
		OnPetRemoved();
	}

	// Clear world objects (forges, looms, groundspawns)
	ClearWorldObjects();

	// Clear pathfinding, BSP, and zone source data
	m_zone_bsp_tree.reset();
	m_zone_bsp_tree_zone_name.clear();  // P17: keep the tag consistent with the tree
	m_zone_wld.reset();
	m_zone_archive.reset();
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NavmeshChanged,
			eqt::state::NavmeshChangedData{nullptr}));
	}
	m_pathfinder.reset();

	// Clear collision map via bridge, then reset zone map
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{nullptr}));
	}
	m_zone_map.reset();

	// Clear zone lines
	m_zone_lines.reset();

	// Reset zone line detection state
	m_zone_line_triggered = false;
	m_zone_change_requested = false;

#ifdef EQT_HAS_GRAPHICS
	// P17: the "/unloadzone_internal" RendererCommand push that used to live
	// here is REMOVED — it was a real bug, not just redundant. It's only
	// ever applied by PreRenderTick() -> ProcessBridgeEvents(), which
	// Application::render() only reaches on its "normal gameplay" branch;
	// every early-return branch taken during a zone transition (network
	// handshake, loading-thread-active) skips it. So the queued unload sat
	// unapplied until AFTER the NEW zone had already fully loaded and
	// gameplay resumed -- at which point applying it would have wiped the
	// zone that had just finished loading, not the one being left. The old
	// zone's renderer state is now unloaded synchronously and correctly by
	// Application::loadZoneGraphicsOnThread() (application.cpp), right
	// before the new zone's own load begins. See
	// refactor/P17_eviction_hygiene.md Review for the eviction-mystery
	// mechanism this closes.
	// Player entity will need to be recreated on renderer after zoning
	m_player_graphics_entity_pending = true;
#endif

	// Reset zone connection state flags
	m_zone_connected = false;
	m_game_state.world().setZoneConnected(false);
	m_zone_session_established = false;
	m_zone_entry_sent = false;
	m_weather_received = false;
	m_req_new_zone_sent = false;
	m_new_zone_received = false;
	m_aa_table_sent = false;
	m_update_aa_sent = false;
	m_tributes_sent = false;
	m_guild_tributes_sent = false;
	m_req_client_spawn_sent = false;
	m_spawn_appearance_sent = false;
	m_exp_zonein_sent = false;
	m_send_exp_zonein_received = false;
	m_server_filter_sent = false;
	m_client_ready_sent = false;
	m_client_spawned = false;

	// Reset AA/tribute tracking
	m_aa_table_count = 0;
	m_tribute_count = 0;
	m_guild_tribute_count = 0;

	// Clear movement history
	m_movement_history.clear();
	m_last_movement_history_send = 0;

	// Reset zone name and ID (both local and WorldState)
	m_current_zone_name.clear();
	m_current_zone_id = 0;
	m_game_state.world().resetZoneState();

	LOG_DEBUG(MOD_ZONE, "Zone cleanup complete");
}

void EverQuest::DisconnectFromZone()
{
	LOG_DEBUG(MOD_ZONE, "Disconnecting from current zone server");

	// Cleanup zone state first
	CleanupZone();

	// Disable reconnect on the zone connection before disconnecting
	if (m_zone_connection_manager) {
		m_zone_connection_manager->OnConnectionStateChange(
			std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, this,
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	// Close zone connection
	if (m_zone_connection) {
		m_zone_connection->Close();
		m_zone_connection.reset();
	}

	// Reset zone connection manager (will be recreated when connecting to new zone)
	m_zone_connection_manager.reset();

	LOG_DEBUG(MOD_ZONE, "Disconnected from zone server");
}

void EverQuest::ProcessDeferredZoneChange()
{
	// This method is called from UpdateMovement, which is outside of any callback context.
	// This ensures safe destruction of connection managers.
	LOG_DEBUG(MOD_ZONE, "Processing deferred zone change");

	LOG_TRACE(MOD_ZONE, "Step 1: Disconnecting from current zone");
	// NOTE: DisconnectFromZone() calls CleanupZone() which does unloadZone() and
	// sets DISCONNECTED phase. Main thread still owns GL at this point.
	DisconnectFromZone();
	LOG_TRACE(MOD_ZONE, "Step 1 complete: Zone disconnected");

	// P15 fix: do NOT signal a zone-load request here. CleanupZone() (inside
	// DisconnectFromZone(), just above) clears m_current_zone_name, so a
	// snapshot created off of live game state at this point
	// (CreateZoneLoadSnapshot() reads m_current_zone_name) would always be
	// empty — gameThreadLoop consumes m_zone_load_requested in the same tick,
	// well before the world/zone reconnect handshake below can repopulate the
	// new zone's name. That produced "loadZoneGraphicsOnThread: snapshot has
	// empty zone name" (GM #zone teleport and, in principle, any zone-line
	// crossing — both funnel through this same re-zone path). The correct,
	// single signal point is OnGameStateComplete() (eq.cpp), which only fires
	// once the new zone's NewZone packet (and thus m_current_zone_name) has
	// actually been received — exactly how the initial zone-in already
	// works. Was previously D20e2's early pre-start signal; removed rather
	// than guarded, since it was always redundant with OnGameStateComplete's
	// later, correct signal for the same re-zone.

	// Reconnect to world server to get new zone info
	if (!m_world_server_host.empty()) {
		LOG_TRACE(MOD_ZONE, "Step 2: Creating new world connection manager");

		SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);

		// Reset world connection state flags so we go through the full handshake again
		m_world_ready = false;
		m_enter_world_sent = false;

		// Reset old connections first
		m_world_connection.reset();
		LOG_TRACE(MOD_ZONE, "Step 2a: Old world connection reset");

		m_world_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(std::make_unique<EQ::Net::PosixUdpTransport>()));
		LOG_TRACE(MOD_ZONE, "Step 2b: New world connection manager created");

		m_world_connection_manager->OnNewConnection(std::bind(&EverQuest::WorldOnNewConnection, this, std::placeholders::_1));
		m_world_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		m_world_connection_manager->OnPacketRecv(std::bind(&EverQuest::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
		LOG_TRACE(MOD_ZONE, "Step 2c: Callbacks registered");

		LOG_DEBUG(MOD_ZONE, "Step 3: Connecting to world server at {}:9000", m_world_server_host);
		m_world_connection_manager->Connect(m_world_server_host, 9000);
		LOG_TRACE(MOD_ZONE, "Step 3 complete: Connect() called, waiting for callbacks");
	} else {
		LOG_ERROR(MOD_ZONE, "No world server address stored for reconnection!");
	}

	LOG_TRACE(MOD_ZONE, "ProcessDeferredZoneChange returning");
}

// NOTE: The pathfinding cluster (LoadPathfinder, FindPath, MoveToWithPath,
// FollowPath) sits here in eq.cpp between the zone change/teardown cluster
// and the zone geometry cluster below. It belongs to a future unit (F08,
// movement) and is intentionally left in eq.cpp.

// ============================================================================
// Zone geometry
// ============================================================================

void EverQuest::LoadZoneMap(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Zone name is empty, skipping");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Loading map for zone '{}'", zone_name);

	// Clear collision map via bridge, then reset zone map
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{nullptr}));
	}
	m_zone_map.reset();

	// Use custom maps path if set, otherwise try common locations
	std::string maps_path = m_maps_path;
	if (maps_path.empty()) {
		// Try to use the same path as navmesh
		if (!m_navmesh_path.empty()) {
			// Navigate from nav directory to maps directory
			size_t nav_pos = m_navmesh_path.find("/nav");
			if (nav_pos != std::string::npos) {
				maps_path = m_navmesh_path.substr(0, nav_pos);
			}
		}

		// Fallback to default location (current directory)
		if (maps_path.empty()) {
			maps_path = "./maps";
		}
	}

	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Using maps path: {}", maps_path);

	m_zone_map.reset(HCMap::LoadMapFile(zone_name, maps_path));

	if (!m_zone_map) {
		LOG_WARN(MOD_MAP, "Failed to load map for zone: {}", zone_name);
	}

	// Publish CollisionMapChanged event to bridge. Shares ownership
	// (shared_ptr copy, not a raw pointer) so the renderer/SimulationWorker
	// side can keep the map alive past the point this game thread later
	// resets m_zone_map on the next zone change (P35).
	if (m_bridge) {
		eqt::state::CollisionMapChangedData data;
		data.map = m_zone_map;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged, std::move(data)));
	}
}

void EverQuest::LoadZoneLines(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Zone name is empty, skipping");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Loading zone lines for '{}'", zone_name);

	// Create new ZoneLines instance
	m_zone_lines = std::make_unique<EQT::ZoneLines>();

	// Reset zone line detection state (both local and WorldState)
	m_zone_line_triggered = false;
	m_zone_change_requested = false;
	m_pending_zone_id = 0;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;
	m_game_state.world().setZoneLineTriggered(false);
	m_game_state.world().setZoneChangeRequested(false);
	m_game_state.world().clearPendingZone();
	m_game_state.world().setLastZoneCheckPosition(m_x, m_y, m_z);

	// Try to load zone lines from the zone's S3D file
	if (!m_eq_client_path.empty()) {
		if (m_zone_lines->loadFromZone(zone_name, m_eq_client_path)) {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: Loaded {} zone lines from WLD", m_zone_lines->getZoneLineCount());
		} else {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: No zone lines found in WLD for '{}'", zone_name);
		}
	} else {
		LOG_DEBUG(MOD_MAP, "LoadZoneLines: No EQ client path set, zone lines from WLD unavailable");
	}

	// NOTE: Zone line visualization boxes are sent to the renderer in LoadZoneGraphics()
	// AFTER zone loading completes. If done here, the boxes get cleared when
	// unloadZone() runs at the start of zone loading.
}

void EverQuest::LoadZoneSources(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Zone name is empty");
		RequestQuit();
		return;
	}

	if (m_eq_client_path.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: EQ client path is empty");
		RequestQuit();
		return;
	}

	auto startTime = std::chrono::steady_clock::now();

	// Clear previous zone sources
	m_zone_archive.reset();
	m_zone_wld.reset();
	m_zone_bsp_tree.reset();
	m_zone_bsp_tree_zone_name.clear();  // T260: tag follows the tree

	// Open zone S3D archive
	std::string archivePath = m_eq_client_path;
	if (!archivePath.empty() && archivePath.back() != '/' && archivePath.back() != '\\')
		archivePath += '/';
	archivePath += zone_name + ".s3d";

	m_zone_archive = std::make_shared<EQT::Graphics::PfsArchive>();
	if (!m_zone_archive->open(archivePath)) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Failed to open zone archive: {}", archivePath);
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Find main WLD file (skip _obj, _chr, lights.wld)
	std::vector<std::string> wldFiles;
	m_zone_archive->getFilenames(".wld", wldFiles);

	std::string mainWld;
	for (const auto& wld : wldFiles) {
		if (wld.find("_obj") == std::string::npos &&
		    wld.find("_chr") == std::string::npos &&
		    wld != "objects.wld" &&
		    wld != "lights.wld") {
			mainWld = wld;
			break;
		}
	}
	if (mainWld.empty() && !wldFiles.empty()) {
		mainWld = wldFiles[0];
	}
	if (mainWld.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: No WLD file found in archive: {}", archivePath);
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Parse WLD
	m_zone_wld = std::make_shared<EQT::Graphics::WldLoader>();
	if (!m_zone_wld->parseFromOpenArchive(*m_zone_archive, mainWld)) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Failed to parse WLD '{}' from archive: {}", mainWld, archivePath);
		m_zone_wld.reset();
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Extract BSP tree
	m_zone_bsp_tree = m_zone_wld->getBspTree();
	if (!m_zone_bsp_tree || m_zone_bsp_tree->regions.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: BSP tree is null or has no regions for zone '{}'", zone_name);
		m_zone_bsp_tree.reset();
		m_zone_bsp_tree_zone_name.clear();
		m_zone_wld.reset();
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// P04 (memory audit fix): the archive and WldLoader have now served their
	// only game-state purpose — producing the BSP tree (self-owned shared_ptr,
	// independent of WldLoader per zone_bsp.h). Nothing on the game side reads
	// them after this point (the renderer parses its own S3D copy in
	// loadZoneStep1_S3D; the former ZoneLoadSnapshot::zoneArchive/zoneWld
	// hand-off fields were never read and have been removed). Keeping them
	// pinned the whole compressed .s3d plus all WLD parse artifacts in RSS for
	// the entire in-zone duration — measured 7.5-12.7 MB per golden zone
	// (see refactor/P04_loader_memory_audit.md review).
	m_zone_archive.reset();
	m_zone_wld.reset();
	// T260: this tree was built from zone_name's own WLD -- tag it so
	// CreateZoneLoadSnapshot() trusts it. P17 introduced the tag but only
	// SetZoneBspTree() wrote it, so the tree built here always carried an
	// empty tag and the snapshot's region lookup was skipped on every
	// zone-in (live logs: "stale (captured for '', current zone 'befallen')").
	m_zone_bsp_tree_zone_name = zone_name;

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - startTime).count();
	LOG_INFO(MOD_MAIN, "LoadZoneSources: Loaded zone '{}' in {}ms ({} BSP regions, {} nodes)",
	         zone_name, elapsed,
	         m_zone_bsp_tree->regions.size(), m_zone_bsp_tree->nodes.size());
}

void EverQuest::CheckZoneLine()
{
	// Skip if not fully zoned in or no zone lines loaded
	if (!IsFullyZonedIn() || !m_zone_lines || !m_zone_lines->hasZoneLines()) {
		return;
	}

	// Skip if a zone change is already in progress
	if (m_zone_change_requested) {
		return;
	}

	// Debug: periodically log zone line check status
	static int checkCount = 0;
	checkCount++;
	if (checkCount % 100 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check #{}: pos=({:.1f}, {:.1f}, {:.1f}) zone_lines={} regions={}",
			checkCount, m_x, m_y, m_z,
			m_zone_lines->hasZoneLines() ? "yes" : "no",
			m_zone_lines->getZoneLineCount());
	}

	// Skip if we haven't moved since last check
	constexpr float MIN_MOVE_THRESHOLD = 0.1f;
	float dx = m_x - m_last_zone_check_x;
	float dy = m_y - m_last_zone_check_y;
	float dz = m_z - m_last_zone_check_z;
	float dist_sq = dx * dx + dy * dy + dz * dz;

	if (dist_sq < MIN_MOVE_THRESHOLD * MIN_MOVE_THRESHOLD) {
		return;
	}

	// Update last checked position
	float old_x = m_last_zone_check_x;
	float old_y = m_last_zone_check_y;
	float old_z = m_last_zone_check_z;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;

	// Check if currently in a zone line region
	// The extracted zone lines use BSP coordinates which need to be checked against player position
	// m_x = server_y, m_y = server_x (from spawn struct)
	// Testing with player position directly (coordinates may need adjustment based on zone)
	float check_x = m_x;
	float check_y = m_y;
	float check_z = m_z;

	// Debug: log position being checked
	static int posLogCount = 0;
	posLogCount++;
	if (posLogCount % 100 == 1) {
		LOG_DEBUG(MOD_ZONE, "CheckZoneLine: m_x={:.1f} m_y={:.1f} m_z={:.1f} (server: x={:.1f} y={:.1f})",
			m_x, m_y, m_z, m_y, m_x);
	}

	EQT::ZoneLineResult result = m_zone_lines->checkPosition(check_x, check_y, check_z, check_x, check_y, check_z);

	// Debug output for every position check (throttled)
	if (checkCount % 50 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check: check=({:.1f}, {:.1f}, {:.1f}) m_pos=({:.1f}, {:.1f}, {:.1f}) -> isZoneLine={}",
			check_x, check_y, check_z, m_x, m_y, m_z, result.isZoneLine ? "YES" : "no");
	}

	if (result.isZoneLine) {
		// We're in a zone line
		auto now = std::chrono::steady_clock::now();

		// Skip actual zone trigger if zoning is disabled
		if (!m_zoning_enabled) {
			return;
		}

		// Debounce: require 500ms cooldown after a trigger before allowing another
		if (m_zone_line_triggered) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() < 500) {
				// Still in cooldown period, ignore
				return;
			}
		}

		// Check if this is a reference-type zone line that needs server lookup
		if (result.needsServerLookup) {
			// Log warning - the server should have sent us the zone point data
			LOG_WARN(MOD_ZONE, "Zone line triggered but missing zone point data for index {}", result.zonePointIndex);
			// Still trigger the zone line, the server will handle it
		}

		// Trigger zone line!
		//
		// T52: Producer B's convention, stated explicitly (nobody had
		// written it down before): result.targetX/Y/Z are stored straight
		// through, in CLIENT format -- they come from this zone's own
		// WLD-derived zone-line data (m_zone_lines, loaded from the S3D/WLD
		// geometry via LoadZoneLines), which is compared directly against
		// check_x/check_y (== m_x/m_y) a few lines above with no swap. This
		// matches docs/CLAUDE.md's convention #3 (S3D/WLD files are Z-up,
		// identity vs. client internal coords, no transform needed). So
		// m_pending_zone_x/y here are in the SAME format producer A stores
		// (client format) -- both producers agree; only the consumer's
		// comment was wrong (fixed in eq_login_world.cpp, see this unit's
		// Review).
		m_zone_line_triggered = true;
		m_zone_line_trigger_time = now;
		m_pending_zone_id = result.targetZoneId;
		m_pending_zone_x = result.targetX;
		m_pending_zone_y = result.targetY;
		m_pending_zone_z = result.targetZ;
		m_pending_zone_heading = result.heading;

		// Phase 7.3: Also update WorldState
		m_game_state.world().setZoneLineTriggered(true);
		m_game_state.world().setZoneLineTriggerTime(now);
		m_game_state.world().setPendingZone(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);

		LOG_INFO(MOD_ZONE, "Zone line triggered! Target zone: {}, coords: ({:.1f}, {:.1f}, {:.1f}), heading: {:.1f}",
		        m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);

		// Send zone change request to server. Client-initiated (we detected
		// the zone line ourselves) -- solicited=false (ZoneUnsolicited),
		// the existing/default behavior; stated explicitly per T52's
		// ride-along fix at the other call site.
		RequestZoneChange(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading, /*solicited=*/false);
	} else {
		// Not in a zone line - clear the triggered flag if we were previously in one
		if (m_zone_line_triggered) {
			// Check if enough time has passed and we're no longer in a zone line
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() >= 500) {
				// Cooldown expired and we're not in a zone line, reset state
				m_zone_line_triggered = false;
				m_pending_zone_id = 0;
				// Phase 7.3: Also update WorldState
				m_game_state.world().setZoneLineTriggered(false);
				m_game_state.world().clearPendingZone();
			}
		}
	}
}

float EverQuest::GetBestZ(float x, float y, float z)
{
	if (!m_zone_map) {
		return z; // Return current Z if no map loaded
	}

	glm::vec3 pos(x, y, z);
	glm::vec3 result;
	float best_z = m_zone_map->FindBestZ(pos, &result);

	// BEST_Z_INVALID from hc_map.h is a sentinel value indicating no valid ground found
	if (best_z == (float)BEST_Z_INVALID) {
		return z; // Return current Z if no valid ground found
	}

	return best_z;
}

void EverQuest::FixZ()
{
	if (!m_zone_map) {
		// Log once when following starts
		static bool logged_no_map = false;
		if (!logged_no_map && !m_follow_target.empty()) {
			LOG_DEBUG(MOD_MAIN, "FixZ: No zone map loaded - Z-height fixing disabled");
			logged_no_map = true;
		}
		return;
	}

	// Get the best Z for our current position
	float new_z = GetBestZ(m_x, m_y, m_z);
	float z_diff = new_z - m_z;
	float abs_diff = std::abs(z_diff);

	// Always log Z-height info when following for debugging
	if (!m_follow_target.empty() && s_debug_level >= 1) {
		// Find the entity we're following to compare Z values
		Entity* target = nullptr;
		for (const auto& pair : m_entities) {
			if (pair.second.name == m_follow_target) {
				target = const_cast<Entity*>(&pair.second);
				break;
			}
		}

		if (target) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Following {} - My Z: {:.2f}, Target Z: {:.2f}, Map thinks Z should be: {:.2f}, Diff: {:.2f}", m_follow_target, m_z, target->z, new_z, z_diff);
		}
	}

	// Only update if the difference is significant
	if (abs_diff > 1.5f && abs_diff < 20.0f) {
		// Much more conservative Z adjustment to prevent bouncing
		float adjustment;

		if (abs_diff < 3.0f) {
			// Small differences: move only 20% of the way
			adjustment = z_diff * 0.2f;
		} else if (abs_diff < 5.0f) {
			// Medium differences: move 15% of the way
			adjustment = z_diff * 0.15f;
		} else {
			// Large differences: move 10% of the way, capped at 1.0 units
			adjustment = z_diff * 0.1f;
			if (std::abs(adjustment) > 1.0f) {
				adjustment = (adjustment > 0) ? 1.0f : -1.0f;
			}
		}

		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Adjusting Z from {:.2f} to {:.2f} (adjustment: {:.2f}, diff was: {:.2f})",
				m_z, m_z + adjustment, adjustment, z_diff);
		}

		m_z += adjustment;
	} else if (s_debug_level >= 2 && abs_diff > 0.01f) {
		LOG_DEBUG(MOD_MAIN, "FixZ: Z difference too small to adjust: {:.2f} (current: {:.2f}, best: {:.2f})",
			z_diff, m_z, new_z);
	}
}
