// EverQuest movement: the STATE AND DEPENDENCIES side -- everything
// movement depends on or derives from, as opposed to the commands that
// drive it (eq_movement.cpp) or the tick that runs it
// (eq_movement_update.cpp).
//  - Pathfinding: LoadPathfinder, FindPath, MoveToWithPath, FollowPath.
//  - Movement mode / position state: SetMovementMode, SetPositionState.
//  - Jump and movement history: SendJump, SendMovementHistory, Jump,
//    UpdateJump.
//  - Water state: UpdateWaterState, OnEnterWater, OnExitWater, GetSwimSpeed.
//  - Keyboard control: DELETED by T343 (board B032) -- see the note at the
//    end of this file.
//
// Split out of a 2,139-line eq_movement.cpp by J23
// (refactor/J23_eq_movement_split.md), which itself came out of eq.cpp in
// Batch F unit F08. Move-only: no behavior change, no interface change.
//
// The eight movement/following tuning constants that used to live in the
// pre-split preamble now live in include/client/eq_movement_internal.h;
// this TU reads three of them.

//
// K07 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_movement{,_state,_update}.cpp to src/client/movement/, and the
// bodies rebased from EverQuest onto eqt::movement::MovementComponent. Still
// verbatim -- only the substitutions Mechanism 4 allows. See the K07 Review.

#include "client/movement/movement_component.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pathfinder_interface.h"
#include "client/spell/spell_manager.h"  // B892: SyncLevitation reads the spell DB
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/skill/skill_manager.h"
#include "client/bridge/game_state_bridge.h"
#include "client/core/memory_ledger.h"  // B941: gs.nav_query owner row
#include "client/position_frames.h"
// P28: wld_loader.h (and the zone_bsp.h BspTree/RegionType it pulls in) has no
// renderer dependency — UpdateWaterState() below needs the complete BspTree
// type regardless of graphics.
#include "client/graphics/eq/wld_loader.h"
#ifdef WITH_AUDIO
#include "client/audio/water_sounds.h"
#endif
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>
#include <tuple>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "client/movement/movement_internal.h"

// Pathfinding implementation
void eqt::movement::MovementComponent::LoadPathfinder(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Loading pathfinder for zone '{}'", zone_name);

	// Release previous pathfinder if any - clear renderer's reference via bridge
	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NavmeshChanged,
			eqt::state::NavmeshChangedData{nullptr}));
	}
	m_pathfinder.reset();
	m_current_path.clear();
	m_current_path_index = 0;
	
	// T504 / Will 2026-09-21: the navmesh IS part of the original-parity baseline --
	// follow existed at launch; a different implementation technique of an
	// original capability is still core. MEASURED dreadlands: +10.9 MiB of
	// game_state from a 4.4 MB .nav file (inflated Detour tiles, stored once).
	try {
		// Use the IPathfinder factory method to load the navigation mesh
		m_pathfinder.reset(IPathfinder::Load(zone_name, ctx_.Zone().NavmeshPath()));
		
		if (m_pathfinder) {
			// Check if it's actually a PathfinderNavmesh vs PathfinderNull
			// This is a bit of a hack but helps us understand what type we got
			PathfinderOptions test_opts;
			bool partial = false, stuck = false;
			// B941: this first query allocates the Detour query object and its
			// node pool (kept for the zone) -- its own owner row.
			IPathfinder::IPath test_path;
			{
				eqt::memory::StartupScope queryOwner("gs.nav_query");
				test_path = m_pathfinder->FindPath(
					glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), partial, stuck, test_opts);
			}

			if (EverQuest::GetDebugLevel() >= 2) {
				LOG_DEBUG(MOD_MAIN, "Loaded pathfinder for zone: {} (type: {})",
					zone_name, test_path.empty() ? "NavMesh" : "Null");
			}

#if defined(EQT_HAS_NAVMESH)
			// Pass navmesh to renderer via bridge for visualization overlay
			if (ctx_.bridge) {
				PathfinderNavmesh* navmesh = dynamic_cast<PathfinderNavmesh*>(m_pathfinder.get());
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NavmeshChanged,
					eqt::state::NavmeshChangedData{navmesh}));
			}
#endif
		} else {
			if (EverQuest::GetDebugLevel() >= 1) {
				LOG_DEBUG(MOD_MAIN, "No navigation mesh available for zone: {}", zone_name);
			}
		}
	} catch (const std::exception& e) {
		if (EverQuest::GetDebugLevel() >= 1) {
			LOG_DEBUG(MOD_MAIN, "Failed to load navigation mesh for {}: {}", zone_name, e.what());
		}
	}
}

bool eqt::movement::MovementComponent::FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z)
{
	LOG_DEBUG(MOD_MAIN, "FindPath called: from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", start_x, start_y, start_z, end_x, end_y, end_z);
	
	if (!m_pathfinder) {
		LOG_DEBUG(MOD_MAIN, "FindPath: No pathfinder object available (m_pathfinder is null)");
		return false;
	}
	
	// Clear previous path
	m_current_path.clear();
	m_current_path_index = 0;
	
	// Set up pathfinding options
	PathfinderOptions opts;
	opts.smooth_path = true;
	opts.step_size = 10.0f; // Distance between waypoints
	opts.offset = 5.0f; // Height offset for character
	
	// Find the path
	bool partial = false;
	bool stuck = false;
	LOG_DEBUG(MOD_MAIN, "FindPath: Calling m_pathfinder->FindPath()...");
	IPathfinder::IPath path = m_pathfinder->FindPath(
		glm::vec3(start_x, start_y, start_z),
		glm::vec3(end_x, end_y, end_z),
		partial,
		stuck,
		opts
	);
	LOG_DEBUG(MOD_PATHFIND, "FindPath: Result - path size: {}, partial: {}, stuck: {}",
		path.size(), partial, stuck);

	if (path.empty()) {
		LOG_DEBUG(MOD_PATHFIND, "No path found from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
			start_x, start_y, start_z, end_x, end_y, end_z);
		if (partial) {
			LOG_DEBUG(MOD_PATHFIND, "  (Partial path available)");
		}
		if (stuck) {
			LOG_DEBUG(MOD_PATHFIND, "  (Path leads back to start - stuck)");
		}
		return false;
	}

	// Convert path nodes to vec3 waypoints
	for (const auto& node : path) {
		// Skip teleport nodes for now
		if (!node.teleport) {
			m_current_path.push_back(node.pos);
		}
	}

	LOG_DEBUG(MOD_PATHFIND, "Found path with {} waypoints", m_current_path.size());
	if (ShouldLog(MOD_PATHFIND, LOG_TRACE)) {
		for (size_t i = 0; i < m_current_path.size() && i < 5; i++) {
			const auto& pos = m_current_path[i];
			LOG_TRACE(MOD_PATHFIND, "  Waypoint {}: ({:.1f}, {:.1f}, {:.1f})",
				i, pos.x, pos.y, pos.z);
		}
		if (m_current_path.size() > 5) {
			LOG_TRACE(MOD_PATHFIND, "  ...");
		}
	}
	
	return true;
}

// T88: FRAME CONTRACT -- CLIENT frame, same as EverQuest::Move() above.
void eqt::movement::MovementComponent::MoveToWithPath(float x, float y, float z)
{
	// T88 test seam: see Test_SetBypassSendGuardForSeam's comment in
	// packet_sender.h.
	if (!ctx_.net.TestBypassSendGuard() && !ctx_.self.IsFullyZonedIn()) {
		LOG_ERROR(MOD_PATHFIND, "Not in zone yet");
		return;
	}
	
	// If pathfinding is disabled or not available, use direct movement
	if (!m_use_pathfinding || !m_pathfinder) {
		MoveTo(x, y, z);
		return;
	}
	
	// Find path from current position to target
	if (FindPath(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), x, y, z)) {
		// Face the final destination immediately
		float new_heading = CalculateHeading(ctx_.self.PosX(), ctx_.self.PosY(), x, y);
		if (std::abs(new_heading - ctx_.self.HeadingDeg()) > 0.1f) {
			ctx_.self.SetHeadingLocal(new_heading);
			SendPositionUpdate();
		}
		
		// Start following the path
		m_current_path_index = 0;
		FollowPath();
	} else {
		// Fallback to direct movement if pathfinding fails
		LOG_WARN(MOD_PATHFIND, "Pathfinding failed, using direct movement");
		MoveTo(x, y, z);
	}
}

void eqt::movement::MovementComponent::FollowPath()
{
	if (m_current_path.empty() || m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// Skip waypoints that are too close to current position (within 1 unit)
	while (m_current_path_index < m_current_path.size()) {
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), waypoint.x, waypoint.y);
		
		if (dist > 1.0f) {
			// This waypoint is far enough, move to it
			MoveTo(waypoint.x, waypoint.y, waypoint.z);
			break;
		} else {
			// Skip this waypoint, it's too close
			if (EverQuest::GetDebugLevel() >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skipping waypoint {} at ({:.1f},{:.1f},{:.1f}) - too close (dist={:.1f})", m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist);
			}
			m_current_path_index++;
		}
	}
	
	// If we've gone through all waypoints, stop
	if (m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// The UpdateMovement function will handle checking when we reach waypoints
}

void eqt::movement::MovementComponent::SetMovementMode(MovementMode mode)
{
	if (static_cast<MovementMode>(ctx_.game_state.player().movementMode()) == mode) {
		return;
	}

	// T237: write the source BEFORE the bridge publish (docs/decisions.md
	// 2026-08-25, ONE GameState rule) -- the enums are value-identical.
	// T286: and it is now the only store.
	ctx_.game_state.player().setMovementMode(static_cast<eqt::state::MovementMode>(mode));

	if (ctx_.bridge) {
		eqt::state::PlayerMovementModeChangedData data;
		data.mode = static_cast<uint8_t>(mode);
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMovementModeChanged, std::move(data)));
	}

	// Update movement speed based on mode
	switch (mode) {
		case MOVE_MODE_RUN:
			ctx_.game_state.player().setMoveSpeed(DEFAULT_RUN_SPEED);
			break;
		case MOVE_MODE_WALK:
			ctx_.game_state.player().setMoveSpeed(DEFAULT_WALK_SPEED);
			break;
		case MOVE_MODE_SNEAK:
			ctx_.game_state.player().setMoveSpeed(DEFAULT_WALK_SPEED * 0.6f);  // Sneak is 60% of walk speed
			ctx_.lifecycle.SetSneak(true);
			break;
	}
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement mode changed to {} (speed: {:.1f})", 
			mode == MOVE_MODE_RUN ? "RUN" : mode == MOVE_MODE_WALK ? "WALK" : "SNEAK", 
			ctx_.game_state.player().moveSpeed());
	}
}

// T344 (B001): the STATE half of the old SetPositionState -- everything it
// did except the SendSpawnAppearance. A pose the SERVER forces on us (an
// OP_SpawnAppearance for our own spawn id, our own OP_Death) must land here:
// echoing it back is both wrong and unlike the official client, which
// T311 Leg A measured never sending an appearance it was just told about.
// Everything below is the pre-split body verbatim minus the five send
// lines; SetPositionState() re-adds four of them (AT_DIE is gone for good,
// see the POS_DEAD arm there).
// B1183 (#502): the player's own stand byte, stored through the per-spawn
// StandState machine (stand_state.h). `userInitiated` selects the writer
// kind, exactly as the client does:
//   * false -- a pose the SERVER forced (our own OP_SpawnAppearance echo,
//     our own OP_Death). The client's direct writes, no gate (103347/103463
//     the appearance blob; 105881/111059 death -> 120 'x'). -> apply()
//   * true  -- /sit, /stand, the camp sit-down, the feign key, the
//     setPositionState action: the client's duck/sit/stand KEY HANDLERS,
//     which call the setter FUN_00483556 (the gates: the no-op, the
//     mid-cast sit, the crouch the camera would not follow). -> transition()
// POS_DEAD is a direct 120 either way (the client never ducks into death).
void eqt::movement::MovementComponent::storeStandByte(PositionState state, bool userInitiated)
{
	auto self = ctx_.entities.find(ctx_.self.MySpawnId());
	if (self == ctx_.entities.end()) {
		return;
	}
	auto& st = self->second.stand_state;
	using V = eqt::state::StandValue;
	switch (state) {
		case POS_DEAD:
			// 105881/111059: the death byte is the corpse value 120 ('x').
			st.apply(V::Corpse);
			break;
		case POS_STANDING:
			if (userInitiated) {
				eqt::state::StandStateContext ctx;
				ctx.in_world = ctx_.self.IsFullyZonedIn();
				ctx.is_local = true;
				ctx.casting = ctx_.spell_manager != nullptr && ctx_.spell_manager->isCasting();
				st.transition(V::Standing, ctx);
			} else {
				st.apply(V::Standing);
			}
			break;
		case POS_SITTING:
			if (userInitiated) {
				eqt::state::StandStateContext ctx;
				ctx.in_world = ctx_.self.IsFullyZonedIn();
				ctx.is_local = true;
				ctx.casting = ctx_.spell_manager != nullptr && ctx_.spell_manager->isCasting();
				st.transition(V::Sitting, ctx);
			} else {
				st.apply(V::Sitting);
			}
			break;
		case POS_CROUCHING:
			if (userInitiated) {
				eqt::state::StandStateContext ctx;
				ctx.in_world = ctx_.self.IsFullyZonedIn();
				ctx.is_local = true;
				st.transition(V::Crouching, ctx);
			} else {
				st.apply(V::Crouching);
			}
			break;
		case POS_FEIGN_DEATH:
			// 115 ('s'): the setter's plain final write (no 's' branch).
			if (userInitiated) {
				eqt::state::StandStateContext ctx;
				ctx.in_world = ctx_.self.IsFullyZonedIn();
				ctx.is_local = true;
				st.transition(V::Lying, ctx);
			} else {
				st.apply(V::Lying);
			}
			break;
	}
}

void eqt::movement::MovementComponent::ApplyPositionState(PositionState state)
{
	applyPositionStateCore(state, /*userInitiated=*/false);
}

// T344 (B001): the shared body of the pose change (everything except the
// OP_SpawnAppearance SetPositionState re-adds). `userInitiated` selects how
// the stand byte is stored (see storeStandByte above).
void eqt::movement::MovementComponent::applyPositionStateCore(PositionState state, bool userInitiated)
{
	if (static_cast<PositionState>(ctx_.game_state.player().positionState()) == state) {
		return;
	}

	// T237: source write precedes the bridge guard (see SetMovementMode).
	// T286: and it is now the only store.
	ctx_.game_state.player().setPositionState(static_cast<eqt::state::PositionState>(state));

	// B1174 (#493) / B1183 (#502): the player's own stand byte, now owned by
	// the per-spawn StandState machine (storeStandByte) -- the value the pose
	// sends, the stun byte, the death byte, ... -- the server does not echo
	// our own appearance back, so this is where the OP_Action cast-anim gate
	// (spell_action_anim.h) learns that we sat down.
	storeStandByte(state, userInitiated);

	if (ctx_.bridge) {
		eqt::state::PlayerPositionStateChangedData data;
		data.state = static_cast<uint8_t>(state);
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerPositionStateChanged, std::move(data)));
	}

	switch (state) {
		case POS_STANDING:
			if (ctx_.game_state.player().isMoving()) {
				StopMovement();
			}
			// Cancel camp timer if standing up
			if (ctx_.game_state.player().isCamping()) {
				ctx_.Player().CancelCamp();
			}
			break;
		case POS_SITTING:
			if (ctx_.game_state.player().isMoving()) {
				StopMovement();
			}
			break;
		case POS_CROUCHING:
			break;
		case POS_FEIGN_DEATH:
			if (ctx_.game_state.player().isMoving()) {
				StopMovement();
			}
			break;
		case POS_DEAD:
			if (ctx_.game_state.player().isMoving()) {
				StopMovement();
			}
			break;
	}

	if (EverQuest::GetDebugLevel() >= 1) {
		const char* state_names[] = {"STANDING", "SITTING", "CROUCHING", "FEIGN_DEATH", "DEAD"};
		LOG_DEBUG(MOD_MAIN, "Position state changed to {}", state_names[state]);
	}
}

// The USER-INITIATED half: /sit, /stand, the camp sit-down, the feign key,
// the setPositionState action. Identical to ApplyPositionState plus the one
// OP_SpawnAppearance that tells the server about the pose WE chose.
//
// T344: the guard is repeated here so a no-op change (already in `state`)
// still sends nothing, exactly as before the split. The one ordering
// difference the split introduces: when the new pose also stops movement,
// StopMovement()'s OP_ClientUpdate now precedes this appearance instead of
// following it. The multiset of packets is unchanged and neither eqemu
// handler reads the other's state.
void eqt::movement::MovementComponent::SetPositionState(PositionState state)
{
	if (static_cast<PositionState>(ctx_.game_state.player().positionState()) == state) {
		return;
	}

	// B1183 (#502): the USER-INITIATED half stores the stand byte through the
	// setter (storeStandByte's userInitiated=true), the client's key-handler
	// path; everything else is identical to ApplyPositionState.
	applyPositionStateCore(state, /*userInitiated=*/true);

	switch (state) {
		case POS_STANDING:
			ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, 100);  // Animation::Standing
			break;
		case POS_SITTING:
			ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, 110);  // Animation::Sitting
			break;
		case POS_CROUCHING:
			ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, 111);  // Animation::Crouching
			break;
		case POS_FEIGN_DEATH:
			ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, ANIM_LYING);  // Use lying animation for feign death
			break;
		case POS_DEAD:
			// T344 (B001): the AT_DIE send is DELETED. T311 Leg A's capture
			// of the official client has AT_DIE (type 0) appearing zero
			// times in either direction; a dying official client sends
			// AT_ANIMATION param 115 (Lying) and then OP_ZoneChange. The arm
			// is KEPT because POS_DEAD is still reachable -- through the
			// setPositionState(4) action -- and ApplyPositionState() above
			// still does the state write and the movement stop for it.
			break;
	}
}

void eqt::movement::MovementComponent::SendJump()
{
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(2);  // Jump packet size: 2 bytes opcode only (no data)
	
	p.PutUInt16(0, HC_OP_Jump);  // opcode
	
	ctx_.net.SafeQueueZone(p, 0, false);
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Jump packet (2 bytes)");
	}
}

void eqt::movement::MovementComponent::SendFallDamage(float fallHeight)
{
	// B893: the client owns the height->damage curve -- eqemu reads
	// EnvDamage2_Struct::damage VERBATIM (akk-stack code/zone/
	// client_packet.cpp:6316, Handle_OP_EnvDamage: drops the packet in liquid,
	// applies ReduceFallDamage, then for a GM prints the would-be damage and
	// does 1 HP; for a non-GM applies it). So the whole curve lives in the one
	// table (include/client/jump_fall_physics.h): the measured lane-2 oracle
	// points, interpolated (refactor/B893/evidence/oracle_measurements.md).
	const uint32_t damage = eqt::jumpfall::damageForFallHeight(fallHeight);
	if (damage == 0) {
		return;  // at or below the table's safe height: a step down, no packet
	}
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}

	EQT::EnvDamage2_Struct env = {};
	unsigned char buf[31];
	eqt::jumpfall::encodeEnvDamage(buf, damage, eqt::jumpfall::kEnvDamageFalling);
	std::memcpy(&env, buf, sizeof(env));

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::EnvDamage2_Struct));
	p.PutUInt16(0, HC_OP_EnvDamage);
	p.PutData(2, &env, sizeof(env));

	ctx_.net.SafeQueueZone(p, 0, true);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "Sent OP_EnvDamage fall damage={} for a {}-unit drop",
			damage, fallHeight);
	}
}

void eqt::movement::MovementComponent::SendMovementHistory()
{
	if (!ctx_.self.IsFullyZonedIn() || ctx_.Zone().MovementHistory().empty() || !ctx_.zone_connection) {
		return;
	}

	// Additional safety check
	if (!ctx_.game_state.world().isZoneConnected() || !ctx_.zone_connection_manager) {
		LOG_WARN(MOD_ZONE, "SendMovementHistory called with invalid connection state");
		return;
	}

	// Add debugging info about connection state
	LOG_TRACE(MOD_ZONE, "SendMovementHistory called at time {} - connection ptr: {}, connected: {}, history size: {}",
		static_cast<uint32_t>(std::time(nullptr)),
		static_cast<void*>(ctx_.zone_connection.get()),
		ctx_.game_state.world().isZoneConnected(),
		ctx_.Zone().MovementHistory().size());

	// Limit history to last 70 entries (similar to what client sends)
	while (ctx_.Zone().MovementHistory().size() > 70) {
		ctx_.Zone().MovementHistory().pop_front();
	}

	// For unreliable packets, we have a max of ~250 bytes (255 - CRC)
	// Each entry is 17 bytes, plus 2 bytes for opcode
	// So we can fit about 14 entries per unreliable packet: (250 - 2) / 17 = 14.5
	const size_t MAX_ENTRIES_PER_PACKET = 14;
	(void)MAX_ENTRIES_PER_PACKET; // Unused but kept for documentation

	// Real clients send all entries in one packet, so we must send as reliable
	// to match their behavior (they send 67-70 entries = 1139-1190 bytes + 2 = 1141-1192 total)
	size_t packet_size = 2 + (ctx_.Zone().MovementHistory().size() * sizeof(eqt::zone::ZoneComponent::MovementHistoryEntry));

	LOG_TRACE(MOD_ZONE, "Creating OP_FloatListThing packet: {} entries, {} bytes total",
		ctx_.Zone().MovementHistory().size(), packet_size);

	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);

	p.PutUInt16(0, HC_OP_FloatListThing);

	// Pack movement entries
	size_t offset = 2;
	try {
		for (const auto& entry : ctx_.Zone().MovementHistory()) {
			// UpdateMovementEntry struct layout:
			// float Y, float X, float Z, uint8 type, uint32 timestamp
			p.PutFloat(offset, entry.y);      // Y first
			p.PutFloat(offset + 4, entry.x);  // then X
			p.PutFloat(offset + 8, entry.z);  // then Z
			p.PutUInt8(offset + 12, entry.type);
			p.PutUInt32(offset + 13, entry.timestamp);
			offset += 17;
		}

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_ZONE, "About to queue OP_FloatListThing as reliable packet");
		}

		// Send as reliable since it exceeds unreliable packet size limit
		if (!ctx_.net.SafeQueueZone(p, 0, true)) {  // Send reliable
			LOG_ERROR(MOD_ZONE, "Failed to queue OP_FloatListThing packet - SafeQueueZonePacket returned false");
		} else {
			LOG_TRACE(MOD_ZONE, "Successfully queued OP_FloatListThing with {} movement entries ({} bytes) as reliable",
				ctx_.Zone().MovementHistory().size(), packet_size);
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception packing movement history: {}", e.what());
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception packing movement history");
	}
}

void eqt::movement::MovementComponent::Jump()
{
	if (ctx_.game_state.player().isJumping()) {
		if (EverQuest::GetDebugLevel() >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump blocked - already jumping");
		}
		return;  // Can't jump while already jumping
	}
	
	// Allow jumping while moving or standing
	ctx_.game_state.player().setJumping(true);
	ctx_.game_state.player().setJumpStartZ(ctx_.self.PosZ());
	ctx_.game_state.player().setJumpStartTime(std::chrono::steady_clock::now());
	
	// Real clients have a tiny position shift when jumping (to trigger server broadcasts)
	// Add a small random X/Y movement like real clients do
	static int jump_count = 0;
	jump_count++;
	float tiny_shift = (jump_count % 2 == 0) ? -0.04f : 0.04f;  // Alternate direction
	
	// Apply tiny shift to X or Y position (alternate between them)
	if (jump_count % 4 < 2) {
		ctx_.self.SetPosLocal(ctx_.self.PosX() + tiny_shift, ctx_.self.PosY(), ctx_.self.PosZ());
	} else {
		ctx_.self.SetPosLocal(ctx_.self.PosX(), ctx_.self.PosY() + tiny_shift, ctx_.self.PosZ());
	}
	
	// Real clients send standing animation before jumping
	ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, ANIM_STANDING);   // 100
	
	// Send OP_Jump packet like real clients do
	SendJump();
	
	// Don't set animation to jump - real clients keep animation at 0 in ClientUpdate
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Jump initiated from z={}", ctx_.self.PosZ());
	}
	
	// Don't block - let the update loop handle the jump
	// The jump will be processed by UpdateMovement() calls
}

void eqt::movement::MovementComponent::UpdateJump()
{
	if (!ctx_.game_state.player().isJumping()) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx_.game_state.player().jumpStartTime()).count();
	// B893: the arc constants live in ONE table (include/client/jump_fall_
	// physics.h), shared with the renderer's PlayerCameraComponent. The old
	// local `jump_duration_ms = 1000.0f` / `jump_height = 25.0f` pair (the
	// "increased for visibility" B463 numbers) is deleted -- tests/
	// test_jump_fall_arc.cpp fails if a second writer reappears.
	const auto& arc = eqt::jumpfall::arc();
	const float t_s = static_cast<float>(elapsed) / 1000.0f;

	if (arc.elapsed(t_s)) {
		// Jump complete
		ctx_.game_state.player().setJumping(false);
		// Return to original height (or use FixZ)
		ctx_.self.SetPosLocal(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.game_state.player().jumpStartZ());
		
		// Return to appropriate animation
		if (ctx_.game_state.player().isMoving()) {
			ctx_.game_state.player().setAnimation((ctx_.game_state.player().moveSpeed() >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK);
		} else {
			ctx_.game_state.player().setAnimation(ANIM_STAND);
		}
		
		if (EverQuest::GetDebugLevel() >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump completed");
		}
	} else {
		// Closed-form parabola from the shared table (peak at t = duration/2).
		float height = arc.jumpHeightAt(t_s);
		ctx_.self.SetPosLocal(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.game_state.player().jumpStartZ() + height);
		
		if (EverQuest::GetDebugLevel() >= 2) {
			static int jump_frame = 0;
			if (jump_frame % 10 == 0) {  // Log every 10th frame
				LOG_DEBUG(MOD_MAIN, "Jump progress: t={:.2f}, height={:.2f}, z={:.2f}", 
					t_s, height, ctx_.self.PosZ());
			}
			jump_frame++;
		}
	}
}

void eqt::movement::MovementComponent::UpdateWaterState()
{
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}

	WaterState newState = WaterState::NotInWater;

	// Fully underwater zones - these don't have BSP water regions
	// The entire zone is submerged so we hardcode them here
	bool isFullyUnderwaterZone = (ctx_.self.CurrentZoneName() == "kedge");

	if (isFullyUnderwaterZone) {
		newState = WaterState::Submerged;
	} else {
		// Normal water detection via BSP regions (D20f3: reads typed game-state BSP tree)
		const auto& bspTree = ctx_.Zone().ZoneBspTree();
		if (bspTree) {
			// Check the region at current player position
			//
			// T311 CORRECTION: T311 Leg A's Review lists this headZ as "computed
			// and never used -- a live dead local". THAT IS A MISREADING: headZ
			// is read below, at the findRegionForPoint() that decides OnSurface
			// vs Submerged. It is not dead and nothing was deleted here.
			//
			// The 6.0 itself IS questionable -- PosZ() is the model node's
			// ORIGIN (render frame), not the feet, and for a Human male the
			// crown is only ~3.65 above that origin, so this samples ~2.3 units
			// ABOVE the head. Correcting it moves the OnSurface/Submerged
			// threshold by about three units, which is a behavioural change in
			// water detection with no live coverage; it is NOT this unit's
			// change (T311 is the wire z) and is boarded instead.
			constexpr float HEAD_HEIGHT_OFFSET = 6.0f;
			float headZ = ctx_.self.PosZ() + HEAD_HEIGHT_OFFSET;

			auto region = bspTree->findRegionForPoint(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());

			if (region) {
				// Check if this region is water
				bool isWaterRegion = false;
				for (auto type : region->regionTypes) {
					if (type == EQT::Graphics::RegionType::Water ||
					    type == EQT::Graphics::RegionType::FreezingWater ||
					    type == EQT::Graphics::RegionType::WaterBlockLOS) {
						isWaterRegion = true;
						break;
					}
				}

				if (isWaterRegion) {
					// Check if head is also in water (determines OnSurface vs Submerged)
					auto headRegion = bspTree->findRegionForPoint(ctx_.self.PosX(), ctx_.self.PosY(), headZ);
					bool headInWater = false;
					if (headRegion) {
						for (auto type : headRegion->regionTypes) {
							if (type == EQT::Graphics::RegionType::Water ||
							    type == EQT::Graphics::RegionType::FreezingWater ||
							    type == EQT::Graphics::RegionType::WaterBlockLOS) {
								headInWater = true;
								break;
							}
						}
					}

					if (headInWater) {
						newState = WaterState::Submerged;
					} else {
						newState = WaterState::OnSurface;
						// T347 (B029): the water-surface Z estimate that used to
						// be computed here (PosZ + HEAD_HEIGHT_OFFSET/2) and
						// stashed in m_water_surface_z is gone. The member had
						// ZERO readers tree-wide, and the SwimmingStateChanged
						// event this function publishes carries only
						// {isSwimming, swimSpeed} (B1011 dropped isLevitating) -- no surface
						// height -- so nothing downstream could see it either.
						// Swim height has no consumer; add one before adding
						// the estimate back.
					}
				}
			}
		}
	}

	// Handle state transitions
	if (newState != m_water_state) {
		WaterState oldState = m_water_state;
		m_water_state = newState;

		if (oldState == WaterState::NotInWater && newState != WaterState::NotInWater) {
			OnEnterWater();
		} else if (oldState != WaterState::NotInWater && newState == WaterState::NotInWater) {
			OnExitWater();
		}

		// Publish SwimmingStateChanged event to bridge (only on state change)
		if (ctx_.bridge) {
			// B1011: no levitation here. The flymode-derived flag this event
			// used to carry overwrote the renderer's hover flag on every water
			// transition; SyncLevitation() (below) is the one publisher.
			float swimSpd = GetSwimSpeed();
			eqt::state::SwimmingStateChangedData data;
			data.isSwimming = (newState != WaterState::NotInWater);
			data.swimSpeed = swimSpd;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SwimmingStateChanged, std::move(data)));
		}
	}

}

void eqt::movement::MovementComponent::OnEnterWater()
{
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Entering water at ({:.2f}, {:.2f}, {:.2f})", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
	}

	// Cancel any jump in progress
	ctx_.game_state.player().setJumping(false);

	// Send flymode=3 (water) to server
	ctx_.Entities().SendSpawnAppearance(AT_FLYMODE, 3);

#ifdef WITH_AUDIO
	// Play water entry sound
	if (ctx_.audio_manager) {
		ctx_.Bridge().PlaySound(EQT::Audio::WaterSoundIds::WaterIn);
	}
#endif
}

void eqt::movement::MovementComponent::OnExitWater()
{
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Exiting water at ({:.2f}, {:.2f}, {:.2f})", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
	}

	// Check if player is levitating (flymode 2 from buffs)
	// If levitating, keep flymode 2, otherwise reset to 0
	auto myEntity = ctx_.entities.find(ctx_.self.MySpawnId());
	uint8_t newFlymode = 0;
	if (myEntity != ctx_.entities.end() && myEntity->second.flymode == 2) {
		newFlymode = 2;  // Keep levitating
	}

	ctx_.Entities().SendSpawnAppearance(AT_FLYMODE, newFlymode);

	m_water_state = WaterState::NotInWater;
}

float eqt::movement::MovementComponent::GetSwimSpeed() const
{
	// Base swim speed is slower than running
	constexpr float BASE_SWIM_SPEED = 20.0f;

	// Skill bonus: up to 50% at max skill (200)
	uint32_t swimSkill = 0;
	if (ctx_.skill_manager) {
		swimSkill = ctx_.skill_manager->getSkillValue(50);  // Skill ID 50 = Swimming
	}

	// Cap skill at 200 for calculation
	if (swimSkill > 200) swimSkill = 200;

	// Formula: BASE_SPEED * (1.0 + (skill / 200) * 0.5)
	// At skill 0: 20 units/sec
	// At skill 200: 30 units/sec
	float skillBonus = (static_cast<float>(swimSkill) / 200.0f) * 0.5f;
	return BASE_SWIM_SPEED * (1.0f + skillBonus);
}

// B892 (GitLab #208): the sync point for the player's own levitation.
//
// Why the game thread owns this: the two INPUTS to the predicate are both
// game-thread state -- the buff list (BuffManager, touched by the OP_Action
// landing and the local expiry timer) and the self row's flymode (the entity
// map, written by the AT_FLYMODE handler). The renderer's
// playerMovement_.isLevitating (renderer_types.h:327) was fed from the
// swimming event and only suppressed idle sinking in water; it floated
// nothing on land. So the state lives here, and the event it publishes
// (LevitationChanged, absolute) is what tells the renderer to hover.
//
// The apply hook calls this with no exclusion (the landing buff is already
// in the list and counts); the FADE hook passes the fading spell, because
// the fade fires while that buff is still in the list (removeBuffBySlot and
// checkBuffExpiration notify before erasing) -- the same pre-erase shape T339's
// computeVisionType() excludes for. `selfFlymode` is an explicit input: the
// production call sites read it off the entity map (0 before the self row
// exists), and a test rig without a zone passes 0.
void eqt::movement::MovementComponent::SyncLevitation(uint8_t selfFlymode,
                                                      uint32_t fade_spell_id)
{
	if (!ctx_.spell_manager || !ctx_.buff_manager || !ctx_.bridge) {
		return;  // spell database not up yet, or nothing to publish to
	}

	const EQ::LevitationState next = EQ::PlayerIsLevitating(
		ctx_.buff_manager->getPlayerBuffs(),
		&ctx_.spell_manager->getDatabase(),
		selfFlymode,
		fade_spell_id);
	if (next == m_levitation) {
		return;  // no flip, no event -- SyncLevitation() may run per buff
	}
	m_levitation = next;
	LOG_INFO(MOD_SPELL, "Levitation {}: buffs={}, selfFlymode={}",
	         next == EQ::LevitationState::Levitating ? "up" : "down",
	         ctx_.buff_manager->getPlayerBuffCount(), selfFlymode);

	eqt::state::LevitationChangedData data;
	data.isLevitating = (next == EQ::LevitationState::Levitating);
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::LevitationChanged, std::move(data)));
}

// B502 (GitLab #177) DELETED GetMovementSpeed(): a one-line wrapper over
// game_state.player().moveSpeed() with zero callers anywhere in the tree.

// T343 (board B032): THE GAME-SIDE KEYBOARD-MOVEMENT ROUTE WAS DELETED HERE.
//
// Deleted: StartMoveForward/StartMoveBackward/StartTurnLeft/StartTurnRight,
// StopMoveForward/StopMoveBackward/StopTurnLeft/StopTurnRight, and
// UpdateKeyboardMovement() together with the four PlayerState flags they were
// the only writers of (moveForward/moveBackward/turnLeft/turnRight) and the
// UpdateMovement() branch that was their only reader.
//
// T319's keep note argued these were "the only game-side mover the client
// has". T343 MEASURED the other end and that premise does not hold. Phase 1 of
// the renderer's per-frame movement pipeline (readMovementInput(),
// renderer_player_physics.cpp) polls HotkeyManager for MoveForward,
// MoveBackward, TurnLeft, TurnRight, StrafeLeft, StrafeRight, Jump, SwimUp and
// SwimDown every single frame and writes the whole of PlayerMovementState from
// them. Forward, backward and turning are driven by that poll exactly the way
// swim is -- which is the finding T334 recorded when it refuted the "vertical
// swim has no producer" board item. So this half was never the switch that was
// missing an on position; it was a SECOND, parallel input route, and the only
// mode that could have produced its key-down/key-up pairs is the graphical
// one, which does not use it. The console handler is line-based and has no
// continuous movement keys to bind, so no producer was ever possible.
//
// The Stop* half was reachable, from EqActionHandler::stopAllMovement(), and
// that call site is STRICTLY BETTER off: it now calls StopMovement(), which
// clears the path and the movement target as well, so /stop stops a MoveTo or
// a Follow instead of leaving the next UpdateMovement() tick to resume it.
