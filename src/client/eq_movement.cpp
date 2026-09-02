// EverQuest movement cluster: pathfinding, water state, and keyboard
// control. High entanglement with combat/entity/zone code but a pure
// TU move — no behavior change, no interface change.
//
// Split out of eq.cpp (Batch F, unit F08):
//  - High-level movement: Move, MoveToEntity, MoveToEntityWithinRange,
//    Follow, StopFollow, StartCombatMovement(name), UpdateCombatMovement.
//  - Face/MoveTo/StopMovement/UpdateMovement (UpdateMovement is the core
//    per-tick movement state machine, ~450 lines).
//  - SendPositionUpdate + movement-geometry helpers: CalculateHeading,
//    CalculateDistance, CalculateDistance2D, HasReachedDestination.
//  - Pathfinding: LoadPathfinder, FindPath, MoveToWithPath, FollowPath.
//  - MovementMode / PositionState: SetMovementMode, SetPositionState.
//  - Jump / MovementHistory: SendJump, SendMovementHistory, Jump,
//    UpdateJump.
//  - Water state: UpdateWaterState, OnEnterWater, OnExitWater,
//    GetSwimSpeed.
//  - Keyboard movement: StartMove*/StopMove*/Turn*, UpdateKeyboardMovement,
//    GetMovementSpeed.
// Moved verbatim, no behavior change. See refactor/F08_movement.md.
//
// NOTE: FindEntityByName / ListEntities (immediately following
// HasReachedDestination in eq.cpp) are entity-lookup helpers, not movement
// — left in eq.cpp for F11 (entities).
//
// NOTE: SafeQueueZonePacket (network helper sitting between SendMovementHistory
// and Jump in eq.cpp) is also used by eq_zone.cpp — not exclusively
// movement-used — left in eq.cpp.
//
// NOTE: StartUpdateLoop / StopUpdateLoop (sitting between Jump and
// UpdateJump in eq.cpp) flip the m_update_running flag that gates the
// auto-combat tick below (T245: the thread they used to start is gone) —
// not movement-exclusive — left in eq.cpp.
//
// NOTE: QueuePacket (core networking, sitting between GetMovementSpeed and
// the keyboard-control cluster in eq.cpp) stays in eq.cpp per the plan.
//
// NOTE: StartCombatMovement(uint16_t) was co-located here from eq.cpp by G03.

#include "client/eq.h"
#include "client/combat.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/skill/skill_manager.h"
#include "client/bridge/game_state_bridge.h"
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

// Movement constants
const float DEFAULT_RUN_SPEED = 6.0f;     // Real EQ run speed based on good client testing
const float DEFAULT_WALK_SPEED = 3.0f;    // Normal EQ walk speed (half of run)
const float WALK_SPEED_THRESHOLD = 4.5f;  // Speed threshold for walk vs run animation
const float POSITION_UPDATE_INTERVAL_MS = 200.0f;  // Real clients update every 200ms

// Following constants
const float FOLLOW_CLOSE_DISTANCE = 10.0f;   // Stop following when this close
const float FOLLOW_FAR_DISTANCE = 30.0f;     // Full speed when this far
const float FOLLOW_MIN_SPEED_MULT = 0.5f;    // Minimum speed multiplier when close
const float FOLLOW_MAX_SPEED_MULT = 1.5f;    // Maximum speed multiplier when far

// T88: FRAME CONTRACT -- x, y, z here are CLIENT frame, i.e. the same frame
// as the client position (docs/CLAUDE.md's "Client Internal Coordinates"), NOT
// server/wire frame. The two are axis-swapped (client_x = server_y,
// client_y = server_x -- see ZoneProcessPlayerProfile in eq.cpp and
// SendPositionUpdate below). This was previously undocumented at every
// site on this call path (Move, MoveToWithPath, moveToLocation, and the
// /move command registration in eq_commands_movement.cpp) -- T88 found
// that ambiguity, not a math defect, cost a live capture session: T85 fed
// this function server-frame coordinates (read off another client's own
// wire self-report), which Move() correctly treated as client-frame and
// transmitted axis-mirrored, exactly as designed. See
// refactor/T88_position_frame_determination.md's Review for the full
// round-trip proof (tests/test_position_frame_roundtrip.cpp) establishing
// the encoder/decoder pair is a correct, matched inverse -- there was
// nothing to fix here except this comment and the log line below.
void EverQuest::Move(float x, float y, float z)
{
	// Log both frames up front: what was asked for (client frame) and what
	// will actually hit the wire (server frame, per the swap above) --
	// one line would have made T85's mismatch obvious in seconds instead
	// of costing a capture session and two analysis units (T86, T87).
	LOG_INFO(MOD_MAIN, "Move called: client-frame target=({:.2f}, {:.2f}, {:.2f}) -> "
		"will transmit server-frame position=({:.2f}, {:.2f}, {:.2f})",
		x, y, z, /*server_x=*/y, /*server_y=*/x, z);
	// Use pathfinding by default
	MoveToWithPath(x, y, z);

	// Non-blocking - movement will be handled by the update system
}

void EverQuest::MoveToEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f})", 
				entity->name, entity->x, entity->y, entity->z) << std::endl;
		}
		LOG_DEBUG(MOD_MAIN, "MoveToEntity: Moving to {} at ({:.2f}, {:.2f}, {:.2f})", 
			entity->name, entity->x, entity->y, entity->z);
		Move(entity->x, entity->y, entity->z);  // This will block until complete
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveToEntityWithinRange(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f}), stopping within {:.1f} units", 
				entity->name, entity->x, entity->y, entity->z, stop_distance) << std::endl;
		}
		
		// Calculate current distance
		float dx = entity->x - PosX();
		float dy = entity->y - PosY();
		float dz = entity->z - PosZ();
		float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// If we're already within range, don't move
		if (dist <= stop_distance) {
			if (s_debug_level >= 1) {
				std::cout << fmt::format("Already within range ({:.1f} <= {:.1f})", dist, stop_distance) << std::endl;
			}
			return;
		}
		
		// Predict where the target will be based on its movement
		// Use a prediction time based on how long it would take us to reach them
		float our_speed = 4.3f;  // Approximate run speed
		float time_to_reach = dist / our_speed;
		
		// Limit prediction time to avoid overshooting
		if (time_to_reach > 2.0f) time_to_reach = 2.0f;
		
		// Calculate predicted position
		float predicted_x = entity->x + entity->delta_x * time_to_reach;
		float predicted_y = entity->y + entity->delta_y * time_to_reach;
		float predicted_z = entity->z + entity->delta_z * time_to_reach;
		
		// Calculate direction to predicted position
		dx = predicted_x - PosX();
		dy = predicted_y - PosY();
		dz = predicted_z - PosZ();
		dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Calculate position to stop at (stop_distance units away from predicted position)
		// We want to move to 80% of the stop distance to ensure we're well within range
		float move_ratio = (dist - stop_distance * 0.8f) / dist;
		float target_x = PosX() + dx * move_ratio;
		float target_y = PosY() + dy * move_ratio;
		float target_z = PosZ() + dz * move_ratio;
		
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Predictive targeting: entity at ({:.1f},{:.1f},{:.1f}), velocity ({:.1f},{:.1f},{:.1f})", entity->x, entity->y, entity->z, entity->delta_x, entity->delta_y, entity->delta_z);
			LOG_DEBUG(MOD_MAIN, "Predicted position in {:.1f}s: ({:.1f},{:.1f},{:.1f})", time_to_reach, predicted_x, predicted_y, predicted_z);
		}
		
		LOG_DEBUG(MOD_MAIN, "MoveToEntityWithinRange: Moving to ({:.2f}, {:.2f}, {:.2f}) to be within {:.1f} of {}", 
			target_x, target_y, target_z, stop_distance, entity->name);
		
		// For combat movement, don't block - let UpdateCombatMovement handle continuous tracking
		if (m_game_state.combat().inCombatMovement()) {
			// Just set the target and let the movement system handle it
			m_game_state.player().setMovementTarget(target_x, target_y, target_z);
			m_game_state.player().setMoving(true);
			SetHeadingLocal(CalculateHeading(PosX(), PosY(), predicted_x, predicted_y));
			
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Non-blocking combat movement initiated");
			}
		} else {
			Move(target_x, target_y, target_z);  // This will block until complete
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::Follow(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		m_game_state.player().setFollowTarget(entity->name);  // Store the actual entity name
		std::cout << fmt::format("Following {}", entity->name) << std::endl;
		
		// Calculate initial path if pathfinding is enabled
		if (m_use_pathfinding && m_pathfinder) {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding enabled, m_pathfinder={}", 
				m_pathfinder ? "valid" : "null");
			float dist = CalculateDistance2D(PosX(), PosY(), entity->x, entity->y);
			LOG_DEBUG(MOD_MAIN, "Follow: Distance to target: {:.2f}, follow_distance: {:.2f}", 
				dist, m_game_state.player().followDistance());
			if (dist > m_game_state.player().followDistance()) {
				// Find path from current position to target
				LOG_DEBUG(MOD_MAIN, "Follow: Calculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", PosX(), PosY(), PosZ(), entity->x, entity->y, entity->z);
				if (FindPath(PosX(), PosY(), PosZ(), entity->x, entity->y, entity->z)) {
					// Start following the path
					m_current_path_index = 0;
					m_game_state.player().setMoving(true);
					LOG_DEBUG(MOD_MOVEMENT, "Follow: Path calculated successfully with {} waypoints",
						m_current_path.size());
					for (size_t i = 0; i < std::min(m_current_path.size(), size_t(5)); i++) {
						LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
							i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
					}
				} else {
					// Pathfinding failed, use direct movement
					LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding failed, using direct movement");
					m_game_state.player().setMovementTarget(entity->x, entity->y, entity->z);
					m_game_state.player().setMoving(true);
				}
			}
		} else {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding disabled (m_use_pathfinding={}, m_pathfinder={})", m_use_pathfinding, m_pathfinder ? "valid" : "null");
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::StopFollow()
{
	if (!m_game_state.player().followTarget().empty()) {
		std::cout << fmt::format("Stopped following {}", m_game_state.player().followTarget()) << std::endl;
		m_game_state.player().clearFollowTarget();
	}
	StopMovement();
}

void EverQuest::StartCombatMovement(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		// Phase 7.6 / T286: CombatState is the only store
		m_game_state.combat().setCombatTarget(entity->name);
		m_game_state.combat().setCombatStopDistance(stop_distance);
		m_game_state.combat().setInCombatMovement(true);
		m_game_state.combat().setLastCombatMovementUpdate(std::chrono::steady_clock::now());

		if (s_debug_level >= 1) {
			std::cout << fmt::format("Starting combat movement to '{}' with stop distance {:.1f}",
				entity->name, stop_distance) << std::endl;
		}

		// Start initial movement immediately
		UpdateCombatMovement();
	} else {
		std::cout << fmt::format("Combat target '{}' not found", name) << std::endl;
	}
}

// G03: uint16_t overload co-located from eq.cpp
void EverQuest::StartCombatMovement(uint16_t entity_id)
{
	// Find the entity
	auto it = m_entities.find(entity_id);
	if (it == m_entities.end()) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "StartCombatMovement: Entity {} not found", entity_id);
		}
		return;
	}

	const auto& entity = it->second;
	// Use previously set combat stop distance, or default to 5.0f
	// Phase 7.6 / T286: CombatState is the only store
	if (m_game_state.combat().combatStopDistance() <= 0.0f) {
		m_game_state.combat().setCombatStopDistance(5.0f);
	}
	m_game_state.combat().setCombatTarget(entity.name);
	m_game_state.combat().setInCombatMovement(true);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Starting combat movement to {} (ID: {})", entity.name, entity_id);
	}

	// Start moving to the target
	MoveToEntityWithinRange(entity.name, m_game_state.combat().combatStopDistance());
}

void EverQuest::UpdateCombatMovement()
{
	if (!m_game_state.combat().inCombatMovement() || m_game_state.combat().combatTarget().empty()) {
		return;
	}
	
	Entity* entity = FindEntityByName(m_game_state.combat().combatTarget());
	if (!entity) {
		// Target disappeared
		m_game_state.combat().setInCombatMovement(false);  // Phase 7.6 / T286
		StopMovement();
		return;
	}
	
	// Calculate current distance
	float dx = entity->x - PosX();
	float dy = entity->y - PosY();
	float dz = entity->z - PosZ();
	float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
	
	// Check if we're within range
	if (dist <= m_game_state.combat().combatStopDistance()) {
		// We're in range, stop if moving
		if (m_game_state.player().isMoving()) {
			StopMovement();
		}
		// Keep combat movement active to track if target moves away
		return;
	}
	
	// We're out of range, need to move closer
	// Only update movement if target has moved significantly or enough time has passed
	auto now = std::chrono::steady_clock::now();
	auto time_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_game_state.combat().lastCombatMovementUpdate()).count();
	
	// For hunting, always update to track the moving target
	bool should_update = false;
	bool is_hunting = m_combat_manager && m_combat_manager->IsAutoHunting();
	
	if (is_hunting) {
		// When hunting, always update position to track moving targets
		should_update = true;
	} else {
		// Normal combat movement - update less frequently
		int update_interval = 500;
		float movement_threshold = 5.0f;
		
		if (time_since_update >= update_interval) {
			should_update = true;
		} else if (m_game_state.player().isMoving()) {
			// Check if target moved significantly from where we're heading
			if (!m_current_path.empty()) {
				// Check distance from path endpoint to target
				const auto& endpoint = m_current_path.back();
				float path_dx = std::abs(entity->x - endpoint.x);
				float path_dy = std::abs(entity->y - endpoint.y);
				if (path_dx > movement_threshold || path_dy > movement_threshold) {
					should_update = true;
					LOG_DEBUG(MOD_MOVEMENT, "Target moved {:.1f} units from path endpoint, updating",
						std::sqrt(path_dx*path_dx + path_dy*path_dy));
				}
			} else {
				// Direct movement - check against target position
				float target_dx = std::abs(entity->x - m_game_state.player().targetX());
				float target_dy = std::abs(entity->y - m_game_state.player().targetY());
				if (target_dx > movement_threshold || target_dy > movement_threshold) {
					should_update = true;
				}
			}
		}
	}
	
	if (should_update || !m_game_state.player().isMoving()) {
		// Only update direct target if we're not following a path
		bool following_path = !m_current_path.empty() && m_current_path_index < m_current_path.size();
		
		if (!following_path) {
			// Use predictive targeting for hunting
			if (is_hunting) {
				// Predict where the target will be
				float our_speed = 4.3f;
				float time_to_reach = dist / our_speed;
				if (time_to_reach > 1.0f) time_to_reach = 1.0f; // Shorter prediction for updates
				
				float predicted_x = entity->x + entity->delta_x * time_to_reach;
				float predicted_y = entity->y + entity->delta_y * time_to_reach;
				float predicted_z = entity->z + entity->delta_z * time_to_reach;
				
				// Recalculate direction to predicted position
				dx = predicted_x - PosX();
				dy = predicted_y - PosY();
				dz = predicted_z - PosZ();
				dist = std::sqrt(dx*dx + dy*dy + dz*dz);
			}
			
			// Calculate new position to move to
			float move_ratio = (dist - m_game_state.combat().combatStopDistance() * 0.8f) / dist;
			m_game_state.player().setMovementTarget(PosX() + dx * move_ratio, PosY() + dy * move_ratio, PosZ() + dz * move_ratio);
			
			// Update heading and start moving
			SetHeadingLocal(CalculateHeading(PosX(), PosY(), m_game_state.player().targetX(), m_game_state.player().targetY()));
			
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (direct): Set target=({:.1f},{:.1f},{:.1f})", m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ());
		} else {
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (path): Keeping target=({:.1f},{:.1f},{:.1f}), path_index={}/{}",
				m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ(), m_current_path_index, m_current_path.size());
		}
		
		m_game_state.player().setMoving(true);
		m_game_state.combat().setLastCombatMovementUpdate(now);  // T286
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: from ({:.1f},{:.1f},{:.1f}) to target {} at ({:.1f},{:.1f},{:.1f}), dist={:.1f}, stop_dist={:.1f}", PosX(), PosY(), PosZ(), entity->name, entity->x, entity->y, entity->z, dist, m_game_state.combat().combatStopDistance());
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: moving to ({:.1f},{:.1f},{:.1f}), m_game_state.player().isMoving() set to {}",
			m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ(), m_game_state.player().isMoving());
		
		// For now, use direct movement for combat (like the working attack command)
		// We can add pathfinding later once basic movement works
		m_current_path.clear();
		m_current_path_index = 0;
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement using direct approach (no pathfinding)");
	}
}

void EverQuest::Face(float x, float y, float z)
{
	// Calculate heading to face the given position
	float new_heading = CalculateHeading(PosX(), PosY(), x, y);
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Face: current pos ({:.1f},{:.1f}), target ({:.1f},{:.1f}), old heading {:.1f}°, new heading {:.1f}°", PosX(), PosY(), x, y, HeadingDeg(), new_heading);
	}
	
	// Always update heading - even small changes should be sent
	// The previous 0.1f threshold was preventing the face command from working
	SetHeadingLocal(new_heading);
	SendPositionUpdate();
}

void EverQuest::FaceEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 2) {
			std::cout << fmt::format("Facing entity '{}'", entity->name) << std::endl;
		}
		Face(entity->x, entity->y, entity->z);
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveTo(float x, float y, float z)
{
	// Trace who's calling MoveTo
	if (s_debug_level >= 1 && std::abs(x - PosX()) < 0.1f && std::abs(y - PosY()) < 0.1f) {
		LOG_DEBUG(MOD_MAIN, "WARNING: MoveTo called with current position! target=({:.2f},{:.2f},{:.2f}) current=({:.2f},{:.2f},{:.2f})", x, y, z, PosX(), PosY(), PosZ());
	}
	
	m_game_state.player().setMovementTarget(x, y, z);
	m_game_state.player().setMoving(true);
	
	// Calculate initial heading towards target
	float new_heading = CalculateHeading(PosX(), PosY(), x, y);
	SetHeadingLocal(new_heading);
	// Send immediate position update to show the heading change
	SendPositionUpdate();
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoveTo: Setting target=({:.2f}, {:.2f}, {:.2f}) from current=({:.2f}, {:.2f}, {:.2f}), heading={:.1f}, m_game_state.player().isMoving()={}", 
			x, y, z, PosX(), PosY(), PosZ(), HeadingDeg(), m_game_state.player().isMoving());
	}
}

void EverQuest::Test_SnapToMovementTargetForSeam()
{
	// T88 test seam: see declaration comment in eq.h.
	SetPosLocal(m_game_state.player().targetX(), m_game_state.player().targetY(),
	            m_game_state.player().targetZ());
	StopMovement();
}

void EverQuest::StopMovement()
{
	if (m_game_state.player().isMoving()) {
		m_game_state.player().setMoving(false);
		m_game_state.player().setAnimation(ANIM_STAND);

		// Clear any active path
		m_current_path.clear();
		m_current_path_index = 0;

		// Clear target coordinates so UpdateMovement doesn't keep moving back
		m_game_state.player().clearMovementTarget();

		SendPositionUpdate();

		if (s_debug_level >= 1) {
			std::cout << "Movement stopped" << std::endl;
		}
	}
}

void EverQuest::UpdateMovement()
{
	// H03: Process a deferred post-logout disconnect if the server confirmed our
	// OP_Logout. Must run before anything else, outside of any packet-callback
	// context — see the comment on m_pending_logout_disconnect and
	// ZoneProcessLogoutReply() for why this can't happen synchronously in the
	// packet handler.
	if (m_pending_logout_disconnect) {
		m_pending_logout_disconnect = false;
		LOG_DEBUG(MOD_ZONE, "Processing deferred post-logout disconnect...");
		DisconnectFromZone();
		LOG_DEBUG(MOD_ZONE, "Deferred post-logout disconnect processed");
		return;  // Don't process movement post-logout
	}

	// Process deferred zone change if approved
	// This MUST be at the very start of UpdateMovement to process outside of any callback context
	if (m_game_state.world().zoneChangeApproved()) {
		m_game_state.world().setZoneChangeApproved(false);  // T286: only store
		LOG_DEBUG(MOD_ZONE, "Processing deferred zone change...");
		ProcessDeferredZoneChange();
		LOG_DEBUG(MOD_ZONE, "Deferred zone change processed");
		return;  // Don't process movement while zone changing
	}

	// T245: auto-combat tick, ON THE GAME THREAD. This replaces the 500 ms
	// m_update_thread (eq.cpp StartUpdateLoop) whose only job was this call;
	// CombatManager::Update() reaches m_game_state.combat() writes
	// (SetTarget/ClearTarget from CheckForAggro / ProcessCombatRound /
	// FindNextHuntTarget) and reads m_entities, both game-thread data. Same
	// gate the old loop had (m_update_running && IsFullyZonedIn()); Update()
	// paces itself at kUpdateIntervalMs so the per-tick call is cheap.
	if (m_combat_manager && IsFullyZonedIn()) {
		m_combat_manager->Update();
	}

	// Update camp timer
	UpdateCampTimer();

	// Check distance to banker NPC - close bank if player moved too far
	if (IsBankWindowOpen() && m_game_state.player().bankerNpcId() != 0) {
		auto it = m_entities.find(m_game_state.player().bankerNpcId());
		if (it == m_entities.end()) {
			// Banker NPC no longer exists (despawned, etc.)
			LOG_DEBUG(MOD_INVENTORY, "Banker NPC {} no longer exists, closing bank", m_game_state.player().bankerNpcId());
			CloseBankWindow();
		} else {
			float dist = CalculateDistance2D(PosX(), PosY(), it->second.x, it->second.y);
			if (dist > NPC_INTERACTION_DISTANCE) {
				LOG_DEBUG(MOD_INVENTORY, "Player moved too far from banker ({:.1f} > {:.1f}), closing bank",
					dist, NPC_INTERACTION_DISTANCE);
				CloseBankWindow();
				AddChatSystemMessage("You have moved too far from the banker.");
			}
		}
	}

	// Debug: Log target at start of UpdateMovement
	static auto last_update_debug = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (m_game_state.combat().inCombatMovement() &&
	    std::chrono::duration_cast<std::chrono::seconds>(now - last_update_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement START: target=({:.1f},{:.1f},{:.1f}) is_moving={} path_size={}",
			m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ(), m_game_state.player().isMoving(), m_current_path.size());
		last_update_debug = now;
	}
	
	// Update keyboard-based movement first
	if (m_game_state.player().moveForward() || m_game_state.player().moveBackward() || m_game_state.player().turnLeft() || m_game_state.player().turnRight()) {
		UpdateKeyboardMovement();
		// Check for zone line collision
		CheckZoneLine();
		// SendPositionUpdate has internal 250ms throttling
		SendPositionUpdate();
		// Don't process other movement if using keyboard
		return;
	}
	
	// Update combat movement if active
	if (m_game_state.combat().inCombatMovement()) {
		UpdateCombatMovement();
	}
	
	// Check if we're following someone
	if (!m_game_state.player().followTarget().empty()) {
		// Find the entity we're following
		for (const auto& pair : m_entities) {
			const Entity& entity = pair.second;
			if (entity.name == m_game_state.player().followTarget()) {
				// Check if we're close enough
				float dist = CalculateDistance2D(PosX(), PosY(), entity.x, entity.y);
				float z_diff = std::abs(PosZ() - entity.z);
				
				// Log follow status periodically
				static std::chrono::steady_clock::time_point last_follow_log = std::chrono::steady_clock::now();
				auto now_follow = std::chrono::steady_clock::now();
				auto elapsed_follow = std::chrono::duration_cast<std::chrono::milliseconds>(now_follow - last_follow_log);
				
				if (elapsed_follow.count() >= 1000 && s_debug_level >= 1) {
					LOG_DEBUG(MOD_MAIN, "Following {}: Distance={:.1f} (stop at {:.1f}), Z-diff={:.1f}, Speed={:.1f}", m_game_state.player().followTarget(), dist, m_game_state.player().followDistance(), z_diff, m_game_state.player().moveSpeed());
					last_follow_log = now_follow;
				}
				
				if (dist < m_game_state.player().followDistance()) {
					// We're close enough, stop if we were moving
					if (m_game_state.player().isMoving()) {
						if (s_debug_level >= 1) {
							LOG_DEBUG(MOD_MAIN, "Reached follow distance ({:.1f}), stopping", dist);
						}
						StopMovement();
					}
					return;
				} else if (dist > m_game_state.player().followDistance() * 1.5f) {
					// Target has moved significantly, recalculate path
					// Check if final destination changed significantly
					float final_dest_dist = 0.0f;
					if (!m_current_path.empty() && m_current_path.size() > 0) {
						// Compare with the final waypoint in our path
						const auto& final_waypoint = m_current_path.back();
						final_dest_dist = CalculateDistance2D(final_waypoint.x, final_waypoint.y, entity.x, entity.y);
					} else {
						// No path, compare with our immediate target
						final_dest_dist = CalculateDistance2D(m_game_state.player().targetX(), m_game_state.player().targetY(), entity.x, entity.y);
					}
					
					if (final_dest_dist > 5.0f || m_current_path.empty()) {
						// Recalculate path using pathfinding
						LOG_DEBUG(MOD_MAIN, "UpdateMovement: Target moved significantly (dist={:.2f})", final_dest_dist);
						if (m_use_pathfinding && m_pathfinder) {
							// Find path from current position to target
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Recalculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", PosX(), PosY(), PosZ(), entity.x, entity.y, entity.z);
							if (FindPath(PosX(), PosY(), PosZ(), entity.x, entity.y, entity.z)) {
								// Start following the path
								m_current_path_index = 0;
								m_game_state.player().setMoving(true);
								LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement: Path recalculated with {} waypoints",
									m_current_path.size());
								for (size_t i = 0; i < std::min(m_current_path.size(), size_t(3)); i++) {
									LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
										i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
								}
							} else {
								// Pathfinding failed, use direct movement
								LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding failed, using direct movement");
								m_game_state.player().setMovementTarget(entity.x, entity.y, entity.z);
								m_game_state.player().setMoving(true);
							}
						} else {
							// Fallback to direct movement
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding disabled, using direct movement");
							m_game_state.player().setMovementTarget(entity.x, entity.y, entity.z);
							m_game_state.player().setMoving(true);
						}
					}
				}
				break;
			}
		}
	}
	
	// Always update jump if jumping, even when not moving
	if (m_game_state.player().isJumping()) {
		float old_z = PosZ();
		UpdateJump();
		
		// If Z changed, send position update immediately
		if (PosZ() != old_z) {
			SendPositionUpdate();
		}
	}
	
	if (!m_game_state.player().isMoving()) {
		// Still check for zone lines when stopped (player might have stopped in one)
		CheckZoneLine();

		// Send idle position updates like real clients (every 1-2 seconds)
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_idle_updates;
		if (last_idle_updates.find(this) == last_idle_updates.end()) {
			last_idle_updates[this] = std::chrono::steady_clock::now();
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_idle_updates[this]);

		if (elapsed.count() >= 1500) {  // 1.5 seconds average
			SendPositionUpdate();
			last_idle_updates[this] = now;
		}
		return;
	}
	
	// Check if we're following a path
	if (!m_current_path.empty() && m_current_path_index < m_current_path.size()) {
		// Check if we've reached the current waypoint
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist_to_waypoint = CalculateDistance2D(PosX(), PosY(), waypoint.x, waypoint.y);

		// Always log path progress for debugging coordinate issues
		static auto last_path_debug = std::chrono::steady_clock::now();
		auto now_path_debug = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now_path_debug - last_path_debug).count() >= 500) {
			LOG_INFO(MOD_MAIN, "Path progress: pos=({:.1f},{:.1f},{:.1f}) waypoint[{}]=({:.1f},{:.1f},{:.1f}) dist={:.1f}",
				PosX(), PosY(), PosZ(), m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist_to_waypoint);
			last_path_debug = now_path_debug;
		}

		LOG_DEBUG(MOD_MOVEMENT, "Following path: waypoint {}/{}, dist to waypoint: {:.2f}",
			m_current_path_index, m_current_path.size()-1, dist_to_waypoint);
		
		// Stuck detection - track if we're not making progress
		static std::map<EverQuest*, std::pair<float, std::chrono::steady_clock::time_point>> stuck_detection;
		auto now = std::chrono::steady_clock::now();
		
		if (stuck_detection.find(this) == stuck_detection.end()) {
			stuck_detection[this] = {dist_to_waypoint, now};
		} else {
			auto& [last_dist, last_time] = stuck_detection[this];
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
			
			// If we haven't moved closer to the waypoint in 3 seconds, we're stuck
			if (elapsed >= 3 && std::abs(dist_to_waypoint - last_dist) < 1.0f) {
				LOG_WARN(MOD_MOVEMENT, "Stuck at waypoint {} - distance hasn't changed in {} seconds",
					m_current_path_index, elapsed);

				// Skip to next waypoint or stop
				if (m_current_path_index < m_current_path.size() - 1) {
					m_current_path_index++;
					LOG_DEBUG(MOD_MOVEMENT, "Skipping to next waypoint due to being stuck");
					// Reset stuck detection for the new waypoint
					stuck_detection.erase(this);
				} else {
					LOG_DEBUG(MOD_MOVEMENT, "Stuck on final waypoint, stopping movement");
					StopMovement();
					stuck_detection.erase(this);
					return;
				}
			} else if (elapsed >= 1) {
				// Update every second if we've made progress
				if (std::abs(dist_to_waypoint - last_dist) > 0.5f) {
					stuck_detection[this] = {dist_to_waypoint, now};
				}
			}
		}
		
		// Check both 2D distance and Z difference for waypoint reached
		float z_diff_to_waypoint = std::abs(PosZ() - waypoint.z);
		bool waypoint_reached = (dist_to_waypoint < 5.0f && z_diff_to_waypoint < 10.0f) || dist_to_waypoint < 3.0f;
		
		if (waypoint_reached) { // Within range of waypoint
			// Move to next waypoint
			m_current_path_index++;
			stuck_detection.erase(this); // Clear stuck detection when reaching waypoint
			
			if (m_current_path_index >= m_current_path.size()) {
				// Reached the end of the path
				LOG_DEBUG(MOD_MAIN, "Reached end of path");
				StopMovement();
				return;
			} else {
				// Move to next waypoint
				const auto& next_waypoint = m_current_path[m_current_path_index];
				
				// Check if the next waypoint is different enough from current position
				float dist_to_next = CalculateDistance2D(PosX(), PosY(), next_waypoint.x, next_waypoint.y);
				if (dist_to_next > 2.0f) {  // Only move if it's far enough away
					MoveTo(next_waypoint.x, next_waypoint.y, next_waypoint.z);

					LOG_TRACE(MOD_MOVEMENT, "Reached waypoint {}, moving to waypoint {} of {}",
						m_current_path_index - 1, m_current_path_index, m_current_path.size() - 1);
				} else {
					// Skip this waypoint, it's too close
					LOG_TRACE(MOD_MOVEMENT, "Skipping waypoint {} (too close: {:.2f} units)",
						m_current_path_index, dist_to_next);
					// Continue to check the next waypoint in the next update
				}
			}
		}
	}
	
	// Only process console-based target movement if we have an active target or path
	// Skip this if we're in graphics Player mode without a console movement command
	bool has_console_movement = !m_game_state.player().followTarget().empty() || !m_current_path.empty() ||
	                            m_game_state.combat().inCombatMovement() ||
	                            (m_game_state.player().targetX() != 0.0f || m_game_state.player().targetY() != 0.0f || m_game_state.player().targetZ() != 0.0f);

	if (!has_console_movement) {
		// No console-based movement active, skip target processing
		return;
	}

	// Debug: Check what target we're moving to
	static auto last_target_debug = std::chrono::steady_clock::now();
	auto now_debug = std::chrono::steady_clock::now();
	if (ShouldLog(MOD_MOVEMENT, LOG_DEBUG) && std::chrono::duration_cast<std::chrono::seconds>(now_debug - last_target_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "Movement target check: current=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) path_size={} path_index={}",
			PosX(), PosY(), PosZ(), m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ(), m_current_path.size(), m_current_path_index);
		last_target_debug = now_debug;
	}

	// Calculate distance to target
	float dx = m_game_state.player().targetX() - PosX();
	float dy = m_game_state.player().targetY() - PosY();
	float dz = m_game_state.player().targetZ() - PosZ();
	float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

	// Check if we've reached the target
	if (distance < 2.0f) {  // Stop within 2 units of target
		LOG_DEBUG(MOD_MOVEMENT, "Reached target at distance {:.2f}, target=({:.1f},{:.1f},{:.1f})",
			distance, m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ());

		SetPosLocal(m_game_state.player().targetX(), m_game_state.player().targetY(),
		            m_game_state.player().targetZ());

		// If following a path, check for next waypoint
		if (!m_current_path.empty() && m_current_path_index < m_current_path.size() - 1) {
			// Continue to next waypoint instead of stopping
			return;
		}

		StopMovement();
		return;
	}
	
	// Calculate time delta based on actual elapsed time
	static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_move_times;
	if (last_move_times.find(this) == last_move_times.end()) {
		last_move_times[this] = std::chrono::steady_clock::now();
	}
	auto now_movement = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - last_move_times[this]);
	last_move_times[this] = now_movement;
	
	float delta_time = elapsed.count() / 1000.0f;  // Convert to seconds
	if (delta_time > 0.1f) delta_time = 0.1f;  // Cap at 100ms to prevent large jumps
	
	// Debug delta time issues
	if (m_game_state.combat().inCombatMovement() && delta_time < 0.01f) {
		LOG_WARN(MOD_MOVEMENT, "Very small delta_time: {:.6f}s ({}ms)",
			delta_time, elapsed.count());
	}
	
	// Calculate movement step using current movement speed
	float current_speed = m_game_state.player().moveSpeed();
	
	// Adjust speed when following
	if (!m_game_state.player().followTarget().empty()) {
		// Speed up when far, slow down when close
		if (distance > FOLLOW_FAR_DISTANCE) {
			current_speed *= FOLLOW_MAX_SPEED_MULT;
		} else if (distance < FOLLOW_CLOSE_DISTANCE) {
			current_speed *= FOLLOW_MIN_SPEED_MULT;
		} else {
			// Linear interpolation between min and max speed
			float speed_factor = (distance - FOLLOW_CLOSE_DISTANCE) / (FOLLOW_FAR_DISTANCE - FOLLOW_CLOSE_DISTANCE);
			current_speed *= FOLLOW_MIN_SPEED_MULT + (FOLLOW_MAX_SPEED_MULT - FOLLOW_MIN_SPEED_MULT) * speed_factor;
		}
	}
	
	float step = current_speed * delta_time;
	if (step > distance) {
		step = distance;
	}
	
	// Debug output for combat movement
	static auto last_move_debug = std::chrono::steady_clock::now();
	if (s_debug_level >= 1 && m_game_state.combat().inCombatMovement() && 
	    std::chrono::duration_cast<std::chrono::seconds>(now_movement - last_move_debug).count() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement update: pos=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) dist={:.1f} speed={:.1f} step={:.1f}", PosX(), PosY(), PosZ(), m_game_state.player().targetX(), m_game_state.player().targetY(), m_game_state.player().targetZ(), distance, current_speed, step);
		last_move_debug = now_movement;
	}
	
	// Normalize direction and move
	float factor = step / distance;
	float old_x = PosX();
	float old_y = PosY();
	SetPosLocal(PosX() + dx * factor, PosY() + dy * factor, PosZ() + dz * factor);
	
	// Debug: verify position actually changed
	if (s_debug_level >= 1 && m_game_state.combat().inCombatMovement()) {
		LOG_DEBUG(MOD_MAIN, "Position update: ({:.1f},{:.1f}) -> ({:.1f},{:.1f}) delta=({:.3f},{:.3f})", old_x, old_y, PosX(), PosY(), PosX() - old_x, PosY() - old_y);
	}
	
	// Update heading (from the pre-step position to the post-step position;
	// old_x/old_y ARE that pre-step position -- identical arithmetic to the
	// pre-T288 `PosX() - dx * factor` form, which recomputed it)
	SetHeadingLocal(CalculateHeading(old_x, old_y, PosX(), PosY()));

	// Update bridge position for console-driven movement (moveto, follow, etc.)
	if (m_bridge) {
		eqt::state::PlayerMovedData pdata;
		pdata.x = PosX(); pdata.y = PosY(); pdata.z = PosZ();
		pdata.heading = HeadingDeg();
		pdata.dx = dx; pdata.dy = dy; pdata.dz = dz;
		pdata.isMoving = true;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
	}

	// Set animation based on actual movement distance
	// Don't change animation if we're jumping
	if (!m_game_state.player().isJumping()) {
		// Calculate movement distance in this update
		float move_dist = std::sqrt(dx * dx + dy * dy);
		
		// Determine animation based on movement distance
		uint32_t new_animation = ANIM_STAND;
		
		if (move_dist < 0.1f) {
			// Very small movement (< 0.1 units) - no animation change
			new_animation = ANIM_STAND;
		} else if (move_dist < 2.0f) {
			// Small movement - walk animation
			new_animation = ANIM_WALK;
		} else {
			// Larger movement - run animation
			new_animation = ANIM_RUN;
		}
		
		// Only change animation if it's different
		if (m_game_state.player().animation() != new_animation) {
			m_game_state.player().setAnimation(new_animation);
		}
	}
	
	// Update jump if jumping
	bool was_jumping = m_game_state.player().isJumping();
	float old_z = PosZ();
	UpdateJump();
	
	// If jumping and Z changed, send position update immediately
	if (m_game_state.player().isJumping() && PosZ() != old_z) {
		SendPositionUpdate();
		m_last_position_update_time = now;
	}
	
	// Fix Z position periodically to follow terrain (but not while jumping)
	if (!m_game_state.player().isJumping()) {
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_z_fix_times;
		if (last_z_fix_times.find(this) == last_z_fix_times.end()) {
			last_z_fix_times[this] = now;
		}
		
		auto z_fix_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_z_fix_times[this]);
		// Fix Z every 1000ms while moving (once per second to reduce adjustments)
		if (z_fix_elapsed.count() >= 1000) {
			FixZ();
			last_z_fix_times[this] = now;
		}
	}
	
	// Check for zone line collision
	CheckZoneLine();

	// Send position update with proper throttling per instance
	if (!m_last_position_update_time.time_since_epoch().count()) {
		m_last_position_update_time = std::chrono::steady_clock::now();
	}

	auto update_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - m_last_position_update_time);

	// Send updates at regular intervals when moving
	if (update_elapsed.count() >= POSITION_UPDATE_INTERVAL_MS) {
		SendPositionUpdate();
		m_last_position_update_time = now_movement;
	}
}


void EverQuest::SendPositionUpdate()
{
	// T88 test seam: m_test_bypass_send_guard skips this guard (and the
	// throttle below) so a headless test can reach the encoder logic
	// without a live m_zone_connection. See Test_SetBypassSendGuardForSeam.
	if (!m_test_bypass_send_guard && (!IsFullyZonedIn() || !m_zone_connection)) {
		LOG_DEBUG(MOD_MOVEMENT, "SendPositionUpdate skipped: zoned_in={} zone_conn={}",
			IsFullyZonedIn(), m_zone_connection != nullptr);
		return;
	}

	// Throttle position updates to ~250ms minimum (working client averages ~275ms)
	// This prevents jerky motion when viewed by other players
	static auto lastSendTime = std::chrono::steady_clock::now();
	static int16_t lastSentAnimation = 0;
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTime);

	// Allow immediate send when stopping (animation goes to 0) for responsiveness
	bool isStoppingNow = (m_game_state.player().animation() == 0 && lastSentAnimation != 0);

	// T231: a forced update (same-zone teleport, RelocateWithinZone) skips
	// the throttle -- the server is waiting on exactly this packet -- and
	// restarts delta tracking from the new position so the jump is not
	// encoded as a velocity. One-shot; consumed here.
	const bool force_update = m_force_next_position_update;
	m_force_next_position_update = false;

	if (!m_test_bypass_send_guard && !force_update && !isStoppingNow && elapsed.count() < 250) {
		// Throttled - skip this update
		return;
	}

	lastSendTime = now;
	lastSentAnimation = m_game_state.player().animation();
	
	// Keep track of last position for delta calculations (per instance, not static)
	static std::map<EverQuest*, std::tuple<float, float, float, float>> last_positions;
	
	// Initialize last position if this is the first update
	if (force_update || last_positions.find(this) == last_positions.end()) {
		last_positions[this] = std::make_tuple(PosX(), PosY(), PosZ(), HeadingDeg());
	}
	
	auto& [last_x, last_y, last_z, last_heading] = last_positions[this];
	
	// Calculate deltas
	float delta_x = PosX() - last_x;
	float delta_y = PosY() - last_y;
	float delta_z = PosZ() - last_z;
	float delta_heading_deg = HeadingDeg() - last_heading;
	
	// Normalize delta heading to -180 to 180
	while (delta_heading_deg > 180.0f) delta_heading_deg -= 360.0f;
	while (delta_heading_deg < -180.0f) delta_heading_deg += 360.0f;
	
	// Convert heading values for the packet
	// EQ uses a different heading system where values might need adjustment
	int delta_heading_scaled = static_cast<int>(delta_heading_deg * 512.0f / 360.0f) & 0x3FF; // 10 bits
	
	// Convert heading to 12-bit value for packet
	// Server uses EQ12toFloat which divides by 4, then converts: degrees = (raw/4) * 360/512
	// This simplifies to: degrees = raw * 360 / 2048
	// So to convert degrees to raw: raw = degrees * 2048 / 360
	//
	// C->S transformation: -(client heading) + 90 to match server convention
	// Inverse of S->C transformation (client heading = 90 - server_h)
	float server_heading = 90.0f - HeadingDeg();
	if (server_heading < 0.0f) server_heading += 360.0f;
	if (server_heading >= 360.0f) server_heading -= 360.0f;
	int heading_raw = static_cast<int>(server_heading * 2048.0f / 360.0f);
	// FloatToEQ12 does modulo 2048, so values wrap
	uint16_t heading_scaled = heading_raw % 2048;
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] heading={:.2f}deg -> server_heading={:.2f}deg -> heading_scaled={} (12-bit)",
			HeadingDeg(), server_heading, heading_scaled);
	}
	
	// Build the client position update body. The wire layout is 36 bytes of
	// bitfields -- eqemu common/patches/titanium_structs.h:1226. T147 deleted
	// the 4-byte EQT::PlayerPositionUpdateClient_Struct that pretended to be it.
	// NOTE: Field order must match server's expected binary layout exactly
	// Server struct has: spawn_id, sequence, y_pos, delta_z, delta_x, delta_y, ...
	struct {
		uint16_t spawn_id;
		uint16_t sequence;
		float y_pos;
		float delta_z;
		float delta_x;   // Server expects delta_x at offset 12
		float delta_y;   // Server expects delta_y at offset 16
		uint32_t anim_and_delta_heading;
		float x_pos;
		float z_pos;
		uint16_t heading_and_padding;
		uint8_t unknown[2];
	} update;
	
	memset(&update, 0, sizeof(update));

	// Use the spawn ID we extracted from PlayerProfile or ZoneSpawns
	// This is what the server expects in ClientUpdate packets
	update.spawn_id = MySpawnId();
	m_game_state.player().incrementMovementSequence();
	update.sequence = m_game_state.player().movementSequence();

	// Validate that we have a valid spawn ID
	if (MySpawnId() == 0) {
		LOG_ERROR(MOD_MOVEMENT, "SendMovementUpdate called with a spawn id of 0! Not sending update.");
		return;
	}

	// Check if we're accidentally using another player's ID
	auto it = m_entities.find(MySpawnId());
	if (it != m_entities.end() && it->second.name != m_character) {
		LOG_WARN(MOD_MOVEMENT, "m_my_spawn_id {} belongs to '{}', not our character '{}'!",
			MySpawnId(), it->second.name, m_character);
	}
	
	// Convert from internal coordinate system back to server coordinate system
	// When receiving from server: client x = server y, client y = server x (see ZoneProcessPlayerProfile)
	// So to send back: server's y_pos = client x, server's x_pos = client y
	update.y_pos = PosX();  // client x holds server's original y
	update.x_pos = PosY();  // client y holds server's original x
	// T311: PosZ() is the RENDER frame (the model node's origin, which the
	// renderer snaps to groundZ + collisionZOffset). The WIRE frame is
	// groundZ + 0.625*size -- MEASURED off the official Titanium client
	// against a live server (T311 Leg A: at a qeynos2 spot whose server
	// raycast reads Best Z = -0.00 the official client transmits 3.751, and
	// the value tracks size exactly linearly with no race term). Convert here,
	// at the packet edge, and nowhere else.
	//
	// The pre-T311 comment here read "Send Z position directly without offset -
	// server stores the actual position". The second half is true (the server
	// applies no transform to a client's z at all -- Mob::FixZ early-returns for
	// Clients) and that is precisely why the first half was a defect: whatever
	// we send is what every other player sees, and we were sending 3.00 where
	// the official client sends 3.751.
	update.z_pos = eqt::renderZToWireZ(PosZ(),
	                                   m_game_state.player().collisionZOffset(),
	                                   m_game_state.player().size());
	// Deltas follow the same transformation:
	// Internal delta_x (change in client x) = change in server's y = server's delta_y
	// Internal delta_y (change in client y) = change in server's x = server's delta_x
	update.delta_x = delta_y;  // server's delta_x = change in client y (server's x)
	update.delta_y = delta_x;  // server's delta_y = change in client x (server's y)
	update.delta_z = delta_z;
	
	// Pack animation and delta_heading into bitfield
	// Use current animation state (which includes jump animation)
	uint32_t anim_value = m_game_state.player().animation();
	update.anim_and_delta_heading = (anim_value & 0x3FF) | (0 << 10) | (1 << 20);
	
	// Pack heading into bitfield (12 bits)
	update.heading_and_padding = heading_scaled;
	
	// Build packet
	EQ::Net::DynamicPacket p;
	p.Resize(sizeof(update) + 2);
	p.PutUInt16(0, HC_OP_ClientUpdate);
	memcpy((char*)p.Data() + 2, &update, sizeof(update));
	
	// Debug: Verify what's in the packet
	if (s_debug_level >= 1) {
		uint16_t packet_spawn_id = p.GetUInt16(2); // spawn_id is first field after opcode
		// LOG_DEBUG(MOD_MAIN, "ClientUpdate packet: spawn_id in struct = {}, spawn_id in packet = {}", 
		// 	update.spawn_id, packet_spawn_id);
	}
	
	// Log C->S position updates at debug level 2+
	if (s_debug_level >= 2) {
		// Log server coordinates (x_pos, y_pos) = (client y, client x) due to coordinate swap
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S spawn_id={} server_pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} server_delta=({:.2f}, {:.2f}, {:.2f})",
			MySpawnId(), update.x_pos, update.y_pos, update.z_pos, HeadingDeg(), m_game_state.player().animation(), update.delta_x, update.delta_y, update.delta_z);
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] client_pos=({:.2f},{:.2f},{:.2f}) -> server_pos=({:.2f},{:.2f},{:.2f})",
			PosX(), PosY(), PosZ(), update.x_pos, update.y_pos, update.z_pos);
	}

	// T88 test seam: capture what was actually computed, unconditionally,
	// before the (possibly test-suppressed) send. See
	// Test_GetLastComputedPositionUpdate.
	m_test_last_sent_update.x_pos = update.x_pos;
	m_test_last_sent_update.y_pos = update.y_pos;
	m_test_last_sent_update.z_pos = update.z_pos;
	m_test_last_sent_update.heading_scaled = update.heading_and_padding;

	DumpPacket("C->S", HC_OP_ClientUpdate, p);
	SafeQueueZonePacket(p, 0, false);  // Send unreliable
	
	// Update last position
	last_x = PosX();
	last_y = PosY();
	last_z = PosZ();
	last_heading = HeadingDeg();
	
	// Track movement history for anti-cheat
	MovementHistoryEntry history_entry;
	history_entry.x = PosX();
	history_entry.y = PosY();
	history_entry.z = PosZ();
	history_entry.type = 1; // Normal movement (Collision type)
	history_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	
	m_movement_history.push_back(history_entry);
	
	// Send movement history every second (matching client behavior)
	uint32_t current_time = static_cast<uint32_t>(std::time(nullptr));
	if (current_time - m_last_movement_history_send >= 1) {
		m_last_movement_history_send = current_time;  // Update timestamp BEFORE sending to prevent duplicates
		try {
			SendMovementHistory();
		}
		catch (const std::exception& e) {
			LOG_ERROR(MOD_MOVEMENT, "Exception in SendMovementHistory: {}", e.what());
		}
		catch (...) {
			LOG_ERROR(MOD_MOVEMENT, "Unknown exception in SendMovementHistory");
		}
	}
}

float EverQuest::CalculateHeading(float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	
	// If we're not moving, preserve current heading
	if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
		return HeadingDeg();
	}
	
	// Calculate angle in radians
	// In EQ's coordinate system: +X is East, +Y is North (verified)
	// We want: 0° = North, 90° = East, 180° = South, 270° = West
	// Standard atan2(y,x) gives: 0° = East, 90° = North, 180° = West, 270° = South
	// So we need to use atan2(x,y) and then adjust by 90 degrees
	float angle = std::atan2(dx, dy);  // This gives 0° = North, 90° = East
	
	// Convert radians to degrees
	float degrees = angle * 180.0f / M_PI;
	
	// atan2 returns -180 to 180, convert to 0-360
	if (degrees < 0.0f) {
		degrees += 360.0f;
	}
	
	// No 180° adjustment needed - the known good client shows:
	// North ≈ 3.5°, East ≈ 137.7°, South ≈ 89.5°, West ≈ 45.8°
	// (converted from raw values 14, 1567, 1018, 521)
	
	// Debug output with more detail
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "CalculateHeading: from ({:.1f},{:.1f}) to ({:.1f},{:.1f}), dx={:.1f}, dy={:.1f}, raw angle={:.1f}°, adjusted={:.1f}°", 
			x1, y1, x2, y2, dx, dy, angle * 180.0f / M_PI, degrees);
	}
	
	return degrees;
}

float EverQuest::CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dz = z2 - z1;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float EverQuest::CalculateDistance2D(float x1, float y1, float x2, float y2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	return std::sqrt(dx * dx + dy * dy);
}

bool EverQuest::HasReachedDestination() const
{
	if (!m_game_state.player().isMoving()) {
		return true;
	}
	
	// Check if we're close enough to the target (within 2 units)
	float dist = CalculateDistance2D(PosX(), PosY(), m_game_state.player().targetX(), m_game_state.player().targetY());
	return dist < 2.0f;
}

// Pathfinding implementation
void EverQuest::LoadPathfinder(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Loading pathfinder for zone '{}'", zone_name);

	// Release previous pathfinder if any - clear renderer's reference via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NavmeshChanged,
			eqt::state::NavmeshChangedData{nullptr}));
	}
	m_pathfinder.reset();
	m_current_path.clear();
	m_current_path_index = 0;
	
	try {
		// Use the IPathfinder factory method to load the navigation mesh
		m_pathfinder.reset(IPathfinder::Load(zone_name, m_navmesh_path));
		
		if (m_pathfinder) {
			// Check if it's actually a PathfinderNavmesh vs PathfinderNull
			// This is a bit of a hack but helps us understand what type we got
			PathfinderOptions test_opts;
			bool partial = false, stuck = false;
			auto test_path = m_pathfinder->FindPath(
				glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), partial, stuck, test_opts);

			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Loaded pathfinder for zone: {} (type: {})",
					zone_name, test_path.empty() ? "NavMesh" : "Null");
			}

#if defined(EQT_HAS_NAVMESH)
			// Pass navmesh to renderer via bridge for visualization overlay
			if (m_bridge) {
				PathfinderNavmesh* navmesh = dynamic_cast<PathfinderNavmesh*>(m_pathfinder.get());
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NavmeshChanged,
					eqt::state::NavmeshChangedData{navmesh}));
			}
#endif
		} else {
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "No navigation mesh available for zone: {}", zone_name);
			}
		}
	} catch (const std::exception& e) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Failed to load navigation mesh for {}: {}", zone_name, e.what());
		}
	}
}

bool EverQuest::FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z)
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
void EverQuest::MoveToWithPath(float x, float y, float z)
{
	// T88 test seam: see Test_SetBypassSendGuardForSeam's comment in eq.h.
	if (!m_test_bypass_send_guard && !IsFullyZonedIn()) {
		LOG_ERROR(MOD_PATHFIND, "Not in zone yet");
		return;
	}
	
	// If pathfinding is disabled or not available, use direct movement
	if (!m_use_pathfinding || !m_pathfinder) {
		MoveTo(x, y, z);
		return;
	}
	
	// Find path from current position to target
	if (FindPath(PosX(), PosY(), PosZ(), x, y, z)) {
		// Face the final destination immediately
		float new_heading = CalculateHeading(PosX(), PosY(), x, y);
		if (std::abs(new_heading - HeadingDeg()) > 0.1f) {
			SetHeadingLocal(new_heading);
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

void EverQuest::FollowPath()
{
	if (m_current_path.empty() || m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// Skip waypoints that are too close to current position (within 1 unit)
	while (m_current_path_index < m_current_path.size()) {
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist = CalculateDistance2D(PosX(), PosY(), waypoint.x, waypoint.y);
		
		if (dist > 1.0f) {
			// This waypoint is far enough, move to it
			MoveTo(waypoint.x, waypoint.y, waypoint.z);
			break;
		} else {
			// Skip this waypoint, it's too close
			if (s_debug_level >= 2) {
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

void EverQuest::SetMovementMode(MovementMode mode)
{
	if (static_cast<MovementMode>(m_game_state.player().movementMode()) == mode) {
		return;
	}

	// T237: write the source BEFORE the bridge publish (docs/decisions.md
	// 2026-08-25, ONE GameState rule) -- the enums are value-identical.
	// T286: and it is now the only store.
	m_game_state.player().setMovementMode(static_cast<eqt::state::MovementMode>(mode));

	if (m_bridge) {
		eqt::state::PlayerMovementModeChangedData data;
		data.mode = static_cast<uint8_t>(mode);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMovementModeChanged, std::move(data)));
	}

	// Update movement speed based on mode
	switch (mode) {
		case MOVE_MODE_RUN:
			m_game_state.player().setMoveSpeed(DEFAULT_RUN_SPEED);
			break;
		case MOVE_MODE_WALK:
			m_game_state.player().setMoveSpeed(DEFAULT_WALK_SPEED);
			break;
		case MOVE_MODE_SNEAK:
			m_game_state.player().setMoveSpeed(DEFAULT_WALK_SPEED * 0.6f);  // Sneak is 60% of walk speed
			SetSneak(true);
			break;
	}
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement mode changed to {} (speed: {:.1f})", 
			mode == MOVE_MODE_RUN ? "RUN" : mode == MOVE_MODE_WALK ? "WALK" : "SNEAK", 
			m_game_state.player().moveSpeed());
	}
}

void EverQuest::SetPositionState(PositionState state)
{
	if (static_cast<PositionState>(m_game_state.player().positionState()) == state) {
		return;
	}

	// T237: source write precedes the bridge guard (see SetMovementMode).
	// T286: and it is now the only store.
	m_game_state.player().setPositionState(static_cast<eqt::state::PositionState>(state));

	if (m_bridge) {
		eqt::state::PlayerPositionStateChangedData data;
		data.state = static_cast<uint8_t>(state);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerPositionStateChanged, std::move(data)));
	}

	switch (state) {
		case POS_STANDING:
			SendSpawnAppearance(AT_ANIMATION, 100);  // Animation::Standing
			if (m_game_state.player().isMoving()) {
				StopMovement();
			}
			// Cancel camp timer if standing up
			if (m_game_state.player().isCamping()) {
				CancelCamp();
			}
			break;
		case POS_SITTING:
			SendSpawnAppearance(AT_ANIMATION, 110);  // Animation::Sitting
			if (m_game_state.player().isMoving()) {
				StopMovement();
			}
			break;
		case POS_CROUCHING:
			SendSpawnAppearance(AT_ANIMATION, 111);  // Animation::Crouching
			break;
		case POS_FEIGN_DEATH:
			SendSpawnAppearance(AT_ANIMATION, ANIM_LYING);  // Use lying animation for feign death
			if (m_game_state.player().isMoving()) {
				StopMovement();
			}
			break;
		case POS_DEAD:
			SendSpawnAppearance(AT_DIE, 0);  // Use AT_DIE for actual death
			if (m_game_state.player().isMoving()) {
				StopMovement();
			}
			break;
	}
	
	if (s_debug_level >= 1) {
		const char* state_names[] = {"STANDING", "SITTING", "CROUCHING", "FEIGN_DEATH", "DEAD"};
		LOG_DEBUG(MOD_MAIN, "Position state changed to {}", state_names[state]);
	}
}

void EverQuest::SendJump()
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(2);  // Jump packet size: 2 bytes opcode only (no data)
	
	p.PutUInt16(0, HC_OP_Jump);  // opcode
	
	m_zone_connection->QueuePacket(p, 0, false);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Jump packet (2 bytes)");
	}
}

void EverQuest::SendMovementHistory()
{
	if (!IsFullyZonedIn() || m_movement_history.empty() || !m_zone_connection) {
		return;
	}

	// Additional safety check
	if (!m_game_state.world().isZoneConnected() || !m_zone_connection_manager) {
		LOG_WARN(MOD_ZONE, "SendMovementHistory called with invalid connection state");
		return;
	}

	// Add debugging info about connection state
	LOG_TRACE(MOD_ZONE, "SendMovementHistory called at time {} - connection ptr: {}, connected: {}, history size: {}",
		static_cast<uint32_t>(std::time(nullptr)),
		static_cast<void*>(m_zone_connection.get()),
		m_game_state.world().isZoneConnected(),
		m_movement_history.size());

	// Limit history to last 70 entries (similar to what client sends)
	while (m_movement_history.size() > 70) {
		m_movement_history.pop_front();
	}

	// For unreliable packets, we have a max of ~250 bytes (255 - CRC)
	// Each entry is 17 bytes, plus 2 bytes for opcode
	// So we can fit about 14 entries per unreliable packet: (250 - 2) / 17 = 14.5
	const size_t MAX_ENTRIES_PER_PACKET = 14;
	(void)MAX_ENTRIES_PER_PACKET; // Unused but kept for documentation

	// Real clients send all entries in one packet, so we must send as reliable
	// to match their behavior (they send 67-70 entries = 1139-1190 bytes + 2 = 1141-1192 total)
	size_t packet_size = 2 + (m_movement_history.size() * sizeof(MovementHistoryEntry));

	LOG_TRACE(MOD_ZONE, "Creating OP_FloatListThing packet: {} entries, {} bytes total",
		m_movement_history.size(), packet_size);

	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);

	p.PutUInt16(0, HC_OP_FloatListThing);

	// Pack movement entries
	size_t offset = 2;
	try {
		for (const auto& entry : m_movement_history) {
			// UpdateMovementEntry struct layout:
			// float Y, float X, float Z, uint8 type, uint32 timestamp
			p.PutFloat(offset, entry.y);      // Y first
			p.PutFloat(offset + 4, entry.x);  // then X
			p.PutFloat(offset + 8, entry.z);  // then Z
			p.PutUInt8(offset + 12, entry.type);
			p.PutUInt32(offset + 13, entry.timestamp);
			offset += 17;
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ZONE, "About to queue OP_FloatListThing as reliable packet");
		}

		// Send as reliable since it exceeds unreliable packet size limit
		if (!SafeQueueZonePacket(p, 0, true)) {  // Send reliable
			LOG_ERROR(MOD_ZONE, "Failed to queue OP_FloatListThing packet - SafeQueueZonePacket returned false");
		} else {
			LOG_TRACE(MOD_ZONE, "Successfully queued OP_FloatListThing with {} movement entries ({} bytes) as reliable",
				m_movement_history.size(), packet_size);
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception packing movement history: {}", e.what());
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception packing movement history");
	}
}

void EverQuest::Jump()
{
	if (m_game_state.player().isJumping()) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump blocked - already jumping");
		}
		return;  // Can't jump while already jumping
	}
	
	// Allow jumping while moving or standing
	m_game_state.player().setJumping(true);
	m_game_state.player().setJumpStartZ(PosZ());
	m_game_state.player().setJumpStartTime(std::chrono::steady_clock::now());
	
	// Real clients have a tiny position shift when jumping (to trigger server broadcasts)
	// Add a small random X/Y movement like real clients do
	static int jump_count = 0;
	jump_count++;
	float tiny_shift = (jump_count % 2 == 0) ? -0.04f : 0.04f;  // Alternate direction
	
	// Apply tiny shift to X or Y position (alternate between them)
	if (jump_count % 4 < 2) {
		SetPosLocal(PosX() + tiny_shift, PosY(), PosZ());
	} else {
		SetPosLocal(PosX(), PosY() + tiny_shift, PosZ());
	}
	
	// Real clients send standing animation before jumping
	SendSpawnAppearance(AT_ANIMATION, ANIM_STANDING);   // 100
	
	// Send OP_Jump packet like real clients do
	SendJump();
	
	// Don't set animation to jump - real clients keep animation at 0 in ClientUpdate
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Jump initiated from z={}", PosZ());
	}
	
	// Don't block - let the update loop handle the jump
	// The jump will be processed by UpdateMovement() calls
}

void EverQuest::UpdateJump()
{
	if (!m_game_state.player().isJumping()) {
		return;
	}
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_game_state.player().jumpStartTime()).count();
	
	// Jump physics: parabolic arc
	// Jump lasts about 1 second, peaks at 0.5 seconds
	const float jump_duration_ms = 1000.0f;
	const float jump_height = 25.0f;  // Maximum height of jump (increased for visibility)
	
	if (elapsed >= jump_duration_ms) {
		// Jump complete
		m_game_state.player().setJumping(false);
		// Return to original height (or use FixZ)
		SetPosLocal(PosX(), PosY(), m_game_state.player().jumpStartZ());
		
		// Return to appropriate animation
		if (m_game_state.player().isMoving()) {
			m_game_state.player().setAnimation((m_game_state.player().moveSpeed() >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK);
		} else {
			m_game_state.player().setAnimation(ANIM_STAND);
		}
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump completed");
		}
	} else {
		// Calculate jump height using parabola
		float t = elapsed / jump_duration_ms;  // 0 to 1
		float height = 4.0f * jump_height * t * (1.0f - t);  // Parabola peaking at t=0.5
		SetPosLocal(PosX(), PosY(), m_game_state.player().jumpStartZ() + height);
		
		if (s_debug_level >= 2) {
			static int jump_frame = 0;
			if (jump_frame % 10 == 0) {  // Log every 10th frame
				LOG_DEBUG(MOD_MAIN, "Jump progress: t={:.2f}, height={:.2f}, z={:.2f}", 
					t, height, PosZ());
			}
			jump_frame++;
		}
	}
}

void EverQuest::UpdateWaterState()
{
	if (!IsFullyZonedIn()) {
		return;
	}

	WaterState newState = WaterState::NotInWater;
	float waterSurfaceZ = 0.0f;

	// Fully underwater zones - these don't have BSP water regions
	// The entire zone is submerged so we hardcode them here
	bool isFullyUnderwaterZone = (CurrentZoneName() == "kedge");

	if (isFullyUnderwaterZone) {
		newState = WaterState::Submerged;
	} else {
		// Normal water detection via BSP regions (D20f3: reads typed game-state BSP tree)
		const auto& bspTree = m_zone_bsp_tree;
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
			float headZ = PosZ() + HEAD_HEIGHT_OFFSET;

			auto region = bspTree->findRegionForPoint(PosX(), PosY(), PosZ());

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
					auto headRegion = bspTree->findRegionForPoint(PosX(), PosY(), headZ);
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
						// Estimate water surface Z (head is above water, feet are in)
						waterSurfaceZ = PosZ() + (HEAD_HEIGHT_OFFSET / 2.0f);
					}
				}
			}
		}
	}

	// Handle state transitions
	if (newState != m_water_state) {
		WaterState oldState = m_water_state;
		m_water_state = newState;
		m_water_surface_z = waterSurfaceZ;

		if (oldState == WaterState::NotInWater && newState != WaterState::NotInWater) {
			OnEnterWater();
		} else if (oldState != WaterState::NotInWater && newState == WaterState::NotInWater) {
			OnExitWater();
		}

		// Publish SwimmingStateChanged event to bridge (only on state change)
		if (m_bridge) {
			float swimSpd = GetSwimSpeed();
			bool lev = false;
			auto me = m_entities.find(MySpawnId());
			if (me != m_entities.end() && me->second.flymode == 2) {
				lev = true;
			}
			eqt::state::SwimmingStateChangedData data;
			data.isSwimming = (newState != WaterState::NotInWater);
			data.swimSpeed = swimSpd;
			data.isLevitating = lev;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SwimmingStateChanged, std::move(data)));
		}
	}

}

void EverQuest::OnEnterWater()
{
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Entering water at ({:.2f}, {:.2f}, {:.2f})", PosX(), PosY(), PosZ());
	}

	// Cancel any jump in progress
	m_game_state.player().setJumping(false);

	// Send flymode=3 (water) to server
	SendSpawnAppearance(AT_FLYMODE, 3);

#ifdef WITH_AUDIO
	// Play water entry sound
	if (m_audio_manager) {
		PlaySound(EQT::Audio::WaterSoundIds::WaterIn);
	}
#endif
}

void EverQuest::OnExitWater()
{
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Exiting water at ({:.2f}, {:.2f}, {:.2f})", PosX(), PosY(), PosZ());
	}

	// Check if player is levitating (flymode 2 from buffs)
	// If levitating, keep flymode 2, otherwise reset to 0
	auto myEntity = m_entities.find(MySpawnId());
	uint8_t newFlymode = 0;
	if (myEntity != m_entities.end() && myEntity->second.flymode == 2) {
		newFlymode = 2;  // Keep levitating
	}

	SendSpawnAppearance(AT_FLYMODE, newFlymode);

	m_water_state = WaterState::NotInWater;
}

float EverQuest::GetSwimSpeed() const
{
	// Base swim speed is slower than running
	constexpr float BASE_SWIM_SPEED = 20.0f;

	// Skill bonus: up to 50% at max skill (200)
	uint32_t swimSkill = 0;
	if (m_skill_manager) {
		swimSkill = m_skill_manager->getSkillValue(50);  // Skill ID 50 = Swimming
	}

	// Cap skill at 200 for calculation
	if (swimSkill > 200) swimSkill = 200;

	// Formula: BASE_SPEED * (1.0 + (skill / 200) * 0.5)
	// At skill 0: 20 units/sec
	// At skill 200: 30 units/sec
	float skillBonus = (static_cast<float>(swimSkill) / 200.0f) * 0.5f;
	return BASE_SWIM_SPEED * (1.0f + skillBonus);
}

float EverQuest::GetMovementSpeed() const
{
	// Return current movement speed, which may be modified by buffs, encumbrance, etc.
	return m_game_state.player().moveSpeed();
}

// Keyboard control methods
void EverQuest::StartMoveForward()
{
	if (!IsFullyZonedIn()) return;

	// Clear any console-based movement (moveto, follow) when user takes manual control
	if (!m_current_path.empty() || m_game_state.player().targetX() != 0.0f || m_game_state.player().targetY() != 0.0f || m_game_state.player().targetZ() != 0.0f) {
		m_current_path.clear();
		m_current_path_index = 0;
		m_game_state.player().clearMovementTarget();
		m_game_state.player().clearFollowTarget();
		LOG_DEBUG(MOD_MAIN, "Cleared pathfinding state due to manual movement");
	}

	m_game_state.player().setMoveForward(true);
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting forward movement");
	}
}

void EverQuest::StartMoveBackward()
{
	if (!IsFullyZonedIn()) return;
	m_game_state.player().setMoveBackward(true);
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting backward movement");
	}
}

void EverQuest::StartTurnLeft()
{
	if (!IsFullyZonedIn()) return;
	m_game_state.player().setTurnLeft(true);
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting left turn");
	}
}

void EverQuest::StartTurnRight()
{
	if (!IsFullyZonedIn()) return;
	m_game_state.player().setTurnRight(true);
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting right turn");
	}
}

void EverQuest::StopMoveForward()
{
	m_game_state.player().setMoveForward(false);
	if (!m_game_state.player().moveBackward() && !m_game_state.player().turnLeft() && !m_game_state.player().turnRight()) {
		m_game_state.player().setMoving(false);
		m_game_state.player().setAnimation(ANIM_STAND);
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopMoveBackward()
{
	m_game_state.player().setMoveBackward(false);
	if (!m_game_state.player().moveForward() && !m_game_state.player().turnLeft() && !m_game_state.player().turnRight()) {
		m_game_state.player().setMoving(false);
		m_game_state.player().setAnimation(ANIM_STAND);
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopTurnLeft()
{
	m_game_state.player().setTurnLeft(false);
	if (!m_game_state.player().moveForward() && !m_game_state.player().moveBackward() && !m_game_state.player().turnRight()) {
		m_game_state.player().setMoving(false);
		m_game_state.player().setAnimation(ANIM_STAND);
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::StopTurnRight()
{
	m_game_state.player().setTurnRight(false);
	if (!m_game_state.player().moveForward() && !m_game_state.player().moveBackward() && !m_game_state.player().turnLeft()) {
		m_game_state.player().setMoving(false);
		m_game_state.player().setAnimation(ANIM_STAND);
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::UpdateKeyboardMovement()
{
	if (!IsFullyZonedIn()) return;
	
	// Handle turning
	const float turn_speed = 90.0f; // degrees per second (4 seconds for full rotation)
	float delta_heading = 0.0f;
	
	if (m_game_state.player().turnLeft() && !m_game_state.player().turnRight()) {
		delta_heading = -turn_speed * 0.5f; // 500ms tick rate
	} else if (m_game_state.player().turnRight() && !m_game_state.player().turnLeft()) {
		delta_heading = turn_speed * 0.5f;
	}
	
	if (delta_heading != 0.0f) {
		float h = HeadingDeg() + delta_heading;
		// Normalize heading to 0-360
		while (h < 0.0f) h += 360.0f;
		while (h >= 360.0f) h -= 360.0f;
		SetHeadingLocal(h);
	}
	
	// Handle forward/backward movement
	float move_distance = 0.0f;
	
	if (m_game_state.player().moveForward() && !m_game_state.player().moveBackward()) {
		move_distance = GetMovementSpeed() * 0.5f; // 500ms tick rate
		m_game_state.player().setMoving(true);
		m_game_state.player().setAnimation((m_game_state.player().moveSpeed() >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK);
	} else if (m_game_state.player().moveBackward() && !m_game_state.player().moveForward()) {
		move_distance = -DEFAULT_WALK_SPEED * 0.5f; // Backward uses walk speed, 500ms tick
		m_game_state.player().setMoving(true);
		m_game_state.player().setAnimation(ANIM_WALK); // Always walk when moving backward
	} else if (!m_game_state.player().moveForward() && !m_game_state.player().moveBackward() && !m_game_state.player().turnLeft() && !m_game_state.player().turnRight()) {
		// Not moving
		if (m_game_state.player().isMoving()) {
			m_game_state.player().setMoving(false);
			m_game_state.player().setAnimation(ANIM_STAND);
		}
	}
	
	// Apply movement
	if (move_distance != 0.0f) {
		// Convert heading to radians for calculations
		float heading_rad = HeadingDeg() * M_PI / 180.0f;

		// Calculate new position
		// In EQ coordinates: +X = East, +Y = North
		// Heading 0 = North, 90 = East, 180 = South, 270 = West
		float new_x = PosX() + move_distance * sin(heading_rad);
		float new_y = PosY() + move_distance * cos(heading_rad);

		// Update position
		SetPosLocal(new_x, new_y, PosZ());

		// Fix Z position if we have map data
		FixZ();
	}
}
