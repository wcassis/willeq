// EverQuest movement: the COMMANDED movement API -- the entry points that
// other TUs and the command layer call to start, redirect, or stop motion.
//  - Move / MoveTo / MoveToEntity / MoveToEntityWithinRange
//  - Follow / StopFollow
//  - StartCombatMovement (both overloads) / UpdateCombatMovement
//  - Face / FaceEntity
//  - Test_SnapToMovementTargetForSeam / StopMovement
//
// Split out of a 2,139-line eq_movement.cpp by J23
// (refactor/J23_eq_movement_split.md), which itself came out of eq.cpp in
// Batch F unit F08 (refactor/F08_movement.md). Move-only: no behavior
// change, no interface change. The sibling TUs are:
//  - eq_movement_update.cpp -- the per-tick pump: UpdateMovement,
//    SendPositionUpdate, and the arrival/geometry helpers.
//  - eq_movement_state.cpp -- what movement depends on or derives:
//    pathfinding, movement mode / position state, jump, movement history,
//    water state, and the keyboard-control cluster.
// The eight movement/following tuning constants that used to live in this
// file's preamble now live in include/client/eq_movement_internal.h, which
// all three TUs include.
//
// NOTE (carried from the pre-split header, still true): FindEntityByName /
// ListEntities, SafeQueueZonePacket, StartUpdateLoop / StopUpdateLoop and
// QueuePacket all sit between movement methods in eq.cpp and were
// deliberately left there by F08 -- none of them is movement-exclusive.
// NOTE: StartCombatMovement(uint16_t) was co-located here from eq.cpp by G03.
//
// K07 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_movement{,_state,_update}.cpp to src/client/movement/, and the
// bodies rebased from EverQuest onto eqt::movement::MovementComponent. Still
// verbatim -- only the substitutions Mechanism 4 allows. See the K07 Review.

#include "client/movement/movement_component.h"
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

#include "client/movement/movement_internal.h"

// T88: FRAME CONTRACT -- x, y, z here are CLIENT frame, i.e. the same frame
// as the client position (docs/CLAUDE.md's "Client Internal Coordinates"), NOT
// server/wire frame. The two are axis-swapped (client_x = server_y,
// client_y = server_x -- see ZoneProcessPlayerProfile in eq_zone_entry.cpp and
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
void eqt::movement::MovementComponent::Move(float x, float y, float z)
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

void eqt::movement::MovementComponent::MoveToEntity(const std::string &name)
{
	Entity* entity = ctx_.Entities().FindEntityByName(name);
	if (entity) {
		if (EverQuest::s_debug_level >= 1) {
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

void eqt::movement::MovementComponent::MoveToEntityWithinRange(const std::string &name, float stop_distance)
{
	Entity* entity = ctx_.Entities().FindEntityByName(name);
	if (entity) {
		if (EverQuest::s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f}), stopping within {:.1f} units", 
				entity->name, entity->x, entity->y, entity->z, stop_distance) << std::endl;
		}
		
		// Calculate current distance
		float dx = entity->x - ctx_.self.PosX();
		float dy = entity->y - ctx_.self.PosY();
		float dz = entity->z - ctx_.self.PosZ();
		float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// If we're already within range, don't move
		if (dist <= stop_distance) {
			if (EverQuest::s_debug_level >= 1) {
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
		dx = predicted_x - ctx_.self.PosX();
		dy = predicted_y - ctx_.self.PosY();
		dz = predicted_z - ctx_.self.PosZ();
		dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Calculate position to stop at (stop_distance units away from predicted position)
		// We want to move to 80% of the stop distance to ensure we're well within range
		float move_ratio = (dist - stop_distance * 0.8f) / dist;
		float target_x = ctx_.self.PosX() + dx * move_ratio;
		float target_y = ctx_.self.PosY() + dy * move_ratio;
		float target_z = ctx_.self.PosZ() + dz * move_ratio;
		
		if (EverQuest::s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Predictive targeting: entity at ({:.1f},{:.1f},{:.1f}), velocity ({:.1f},{:.1f},{:.1f})", entity->x, entity->y, entity->z, entity->delta_x, entity->delta_y, entity->delta_z);
			LOG_DEBUG(MOD_MAIN, "Predicted position in {:.1f}s: ({:.1f},{:.1f},{:.1f})", time_to_reach, predicted_x, predicted_y, predicted_z);
		}
		
		LOG_DEBUG(MOD_MAIN, "MoveToEntityWithinRange: Moving to ({:.2f}, {:.2f}, {:.2f}) to be within {:.1f} of {}", 
			target_x, target_y, target_z, stop_distance, entity->name);
		
		// For combat movement, don't block - let UpdateCombatMovement handle continuous tracking
		if (ctx_.game_state.combat().inCombatMovement()) {
			// Just set the target and let the movement system handle it
			ctx_.game_state.player().setMovementTarget(target_x, target_y, target_z);
			ctx_.game_state.player().setMoving(true);
			ctx_.self.SetHeadingLocal(CalculateHeading(ctx_.self.PosX(), ctx_.self.PosY(), predicted_x, predicted_y));
			
			if (EverQuest::s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Non-blocking combat movement initiated");
			}
		} else {
			Move(target_x, target_y, target_z);  // This will block until complete
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void eqt::movement::MovementComponent::Follow(const std::string &name)
{
	Entity* entity = ctx_.Entities().FindEntityByName(name);
	if (entity) {
		ctx_.game_state.player().setFollowTarget(entity->name);  // Store the actual entity name
		std::cout << fmt::format("Following {}", entity->name) << std::endl;
		
		// Calculate initial path if pathfinding is enabled
		if (m_use_pathfinding && m_pathfinder) {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding enabled, m_pathfinder={}", 
				m_pathfinder ? "valid" : "null");
			float dist = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), entity->x, entity->y);
			LOG_DEBUG(MOD_MAIN, "Follow: Distance to target: {:.2f}, follow_distance: {:.2f}", 
				dist, ctx_.game_state.player().followDistance());
			if (dist > ctx_.game_state.player().followDistance()) {
				// Find path from current position to target
				LOG_DEBUG(MOD_MAIN, "Follow: Calculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity->x, entity->y, entity->z);
				if (FindPath(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity->x, entity->y, entity->z)) {
					// Start following the path
					m_current_path_index = 0;
					ctx_.game_state.player().setMoving(true);
					LOG_DEBUG(MOD_MOVEMENT, "Follow: Path calculated successfully with {} waypoints",
						m_current_path.size());
					for (size_t i = 0; i < std::min(m_current_path.size(), size_t(5)); i++) {
						LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
							i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
					}
				} else {
					// Pathfinding failed, use direct movement
					LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding failed, using direct movement");
					ctx_.game_state.player().setMovementTarget(entity->x, entity->y, entity->z);
					ctx_.game_state.player().setMoving(true);
				}
			}
		} else {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding disabled (m_use_pathfinding={}, m_pathfinder={})", m_use_pathfinding, m_pathfinder ? "valid" : "null");
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void eqt::movement::MovementComponent::StopFollow()
{
	if (!ctx_.game_state.player().followTarget().empty()) {
		std::cout << fmt::format("Stopped following {}", ctx_.game_state.player().followTarget()) << std::endl;
		ctx_.game_state.player().clearFollowTarget();
	}
	StopMovement();
}

void eqt::movement::MovementComponent::StartCombatMovement(const std::string &name, float stop_distance)
{
	Entity* entity = ctx_.Entities().FindEntityByName(name);
	if (entity) {
		// Phase 7.6 / T286: CombatState is the only store
		ctx_.game_state.combat().setCombatTarget(entity->name);
		ctx_.game_state.combat().setCombatStopDistance(stop_distance);
		ctx_.game_state.combat().setInCombatMovement(true);
		ctx_.game_state.combat().setLastCombatMovementUpdate(std::chrono::steady_clock::now());

		if (EverQuest::s_debug_level >= 1) {
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
void eqt::movement::MovementComponent::StartCombatMovement(uint16_t entity_id)
{
	// Find the entity
	auto it = ctx_.entities.find(entity_id);
	if (it == ctx_.entities.end()) {
		if (EverQuest::s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "StartCombatMovement: Entity {} not found", entity_id);
		}
		return;
	}

	const auto& entity = it->second;
	// Use previously set combat stop distance, or default to 5.0f
	// Phase 7.6 / T286: CombatState is the only store
	if (ctx_.game_state.combat().combatStopDistance() <= 0.0f) {
		ctx_.game_state.combat().setCombatStopDistance(5.0f);
	}
	ctx_.game_state.combat().setCombatTarget(entity.name);
	ctx_.game_state.combat().setInCombatMovement(true);

	if (EverQuest::s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Starting combat movement to {} (ID: {})", entity.name, entity_id);
	}

	// Start moving to the target
	MoveToEntityWithinRange(entity.name, ctx_.game_state.combat().combatStopDistance());
}

void eqt::movement::MovementComponent::UpdateCombatMovement()
{
	if (!ctx_.game_state.combat().inCombatMovement() || ctx_.game_state.combat().combatTarget().empty()) {
		return;
	}
	
	Entity* entity = ctx_.Entities().FindEntityByName(ctx_.game_state.combat().combatTarget());
	if (!entity) {
		// Target disappeared
		ctx_.game_state.combat().setInCombatMovement(false);  // Phase 7.6 / T286
		StopMovement();
		return;
	}
	
	// Calculate current distance
	float dx = entity->x - ctx_.self.PosX();
	float dy = entity->y - ctx_.self.PosY();
	float dz = entity->z - ctx_.self.PosZ();
	float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
	
	// Check if we're within range
	if (dist <= ctx_.game_state.combat().combatStopDistance()) {
		// We're in range, stop if moving
		if (ctx_.game_state.player().isMoving()) {
			StopMovement();
		}
		// Keep combat movement active to track if target moves away
		return;
	}
	
	// We're out of range, need to move closer
	// Only update movement if target has moved significantly or enough time has passed
	auto now = std::chrono::steady_clock::now();
	auto time_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - ctx_.game_state.combat().lastCombatMovementUpdate()).count();
	
	// For hunting, always update to track the moving target
	bool should_update = false;
	bool is_hunting = ctx_.combat_manager && ctx_.combat_manager->IsAutoHunting();
	
	if (is_hunting) {
		// When hunting, always update position to track moving targets
		should_update = true;
	} else {
		// Normal combat movement - update less frequently
		int update_interval = 500;
		float movement_threshold = 5.0f;
		
		if (time_since_update >= update_interval) {
			should_update = true;
		} else if (ctx_.game_state.player().isMoving()) {
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
				float target_dx = std::abs(entity->x - ctx_.game_state.player().targetX());
				float target_dy = std::abs(entity->y - ctx_.game_state.player().targetY());
				if (target_dx > movement_threshold || target_dy > movement_threshold) {
					should_update = true;
				}
			}
		}
	}
	
	if (should_update || !ctx_.game_state.player().isMoving()) {
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
				dx = predicted_x - ctx_.self.PosX();
				dy = predicted_y - ctx_.self.PosY();
				dz = predicted_z - ctx_.self.PosZ();
				dist = std::sqrt(dx*dx + dy*dy + dz*dz);
			}
			
			// Calculate new position to move to
			float move_ratio = (dist - ctx_.game_state.combat().combatStopDistance() * 0.8f) / dist;
			ctx_.game_state.player().setMovementTarget(ctx_.self.PosX() + dx * move_ratio, ctx_.self.PosY() + dy * move_ratio, ctx_.self.PosZ() + dz * move_ratio);
			
			// Update heading and start moving
			ctx_.self.SetHeadingLocal(CalculateHeading(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY()));
			
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (direct): Set target=({:.1f},{:.1f},{:.1f})", ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ());
		} else {
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (path): Keeping target=({:.1f},{:.1f},{:.1f}), path_index={}/{}",
				ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ(), m_current_path_index, m_current_path.size());
		}
		
		ctx_.game_state.player().setMoving(true);
		ctx_.game_state.combat().setLastCombatMovementUpdate(now);  // T286
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: from ({:.1f},{:.1f},{:.1f}) to target {} at ({:.1f},{:.1f},{:.1f}), dist={:.1f}, stop_dist={:.1f}", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity->name, entity->x, entity->y, entity->z, dist, ctx_.game_state.combat().combatStopDistance());
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: moving to ({:.1f},{:.1f},{:.1f}), m_game_state.player().isMoving() set to {}",
			ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ(), ctx_.game_state.player().isMoving());
		
		// For now, use direct movement for combat (like the working attack command)
		// We can add pathfinding later once basic movement works
		m_current_path.clear();
		m_current_path_index = 0;
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement using direct approach (no pathfinding)");
	}
}

void eqt::movement::MovementComponent::Face(float x, float y, float z)
{
	// Calculate heading to face the given position
	float new_heading = CalculateHeading(ctx_.self.PosX(), ctx_.self.PosY(), x, y);
	
	if (EverQuest::s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Face: current pos ({:.1f},{:.1f}), target ({:.1f},{:.1f}), old heading {:.1f}°, new heading {:.1f}°", ctx_.self.PosX(), ctx_.self.PosY(), x, y, ctx_.self.HeadingDeg(), new_heading);
	}
	
	// Always update heading - even small changes should be sent
	// The previous 0.1f threshold was preventing the face command from working
	ctx_.self.SetHeadingLocal(new_heading);
	SendPositionUpdate();
}

void eqt::movement::MovementComponent::FaceEntity(const std::string &name)
{
	Entity* entity = ctx_.Entities().FindEntityByName(name);
	if (entity) {
		if (EverQuest::s_debug_level >= 2) {
			std::cout << fmt::format("Facing entity '{}'", entity->name) << std::endl;
		}
		Face(entity->x, entity->y, entity->z);
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void eqt::movement::MovementComponent::MoveTo(float x, float y, float z)
{
	// Trace who's calling MoveTo
	if (EverQuest::s_debug_level >= 1 && std::abs(x - ctx_.self.PosX()) < 0.1f && std::abs(y - ctx_.self.PosY()) < 0.1f) {
		LOG_DEBUG(MOD_MAIN, "WARNING: MoveTo called with current position! target=({:.2f},{:.2f},{:.2f}) current=({:.2f},{:.2f},{:.2f})", x, y, z, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
	}
	
	ctx_.game_state.player().setMovementTarget(x, y, z);
	ctx_.game_state.player().setMoving(true);
	
	// Calculate initial heading towards target
	float new_heading = CalculateHeading(ctx_.self.PosX(), ctx_.self.PosY(), x, y);
	ctx_.self.SetHeadingLocal(new_heading);
	// Send immediate position update to show the heading change
	SendPositionUpdate();
	
	if (EverQuest::s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoveTo: Setting target=({:.2f}, {:.2f}, {:.2f}) from current=({:.2f}, {:.2f}, {:.2f}), heading={:.1f}, m_game_state.player().isMoving()={}", 
			x, y, z, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg(), ctx_.game_state.player().isMoving());
	}
}

void eqt::movement::MovementComponent::Test_SnapToMovementTargetForSeam()
{
	// T88 test seam: see declaration comment in eq.h.
	ctx_.self.SetPosLocal(ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(),
	            ctx_.game_state.player().targetZ());
	StopMovement();
}

void eqt::movement::MovementComponent::StopMovement()
{
	if (ctx_.game_state.player().isMoving()) {
		ctx_.game_state.player().setMoving(false);
		ctx_.game_state.player().setAnimation(ANIM_STAND);

		// Clear any active path
		m_current_path.clear();
		m_current_path_index = 0;

		// Clear target coordinates so UpdateMovement doesn't keep moving back
		ctx_.game_state.player().clearMovementTarget();

		SendPositionUpdate();

		if (EverQuest::s_debug_level >= 1) {
			std::cout << "Movement stopped" << std::endl;
		}
	}
}

