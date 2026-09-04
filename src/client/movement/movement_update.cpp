// EverQuest movement: the PER-TICK PUMP -- one causal chain from the
// movement state machine to the position update it puts on the wire.
//  - UpdateMovement -- the core per-tick movement state machine.
//  - SendPositionUpdate -- the outbound position packet.
//  - CalculateHeading / CalculateDistance / CalculateDistance2D /
//    HasReachedDestination -- the arrival and geometry helpers it uses.
//
// Split out of a 2,139-line eq_movement.cpp by J23
// (refactor/J23_eq_movement_split.md), which itself came out of eq.cpp in
// Batch F unit F08. Move-only: no behavior change, no interface change.
// Siblings: eq_movement.cpp (the commanded movement API) and
// eq_movement_state.cpp (pathfinding, position state, jump, water,
// keyboard control).
//
// WARNING: UpdateMovement is 499 lines -- by a wide margin the largest
// method in the movement cluster, and a RECORDED Batch-G DECOMPOSITION
// CANDIDATE that J23 deliberately did NOT touch. J23's contract is verbatim
// relocation; decomposing a method inside such a commit would destroy the
// byte-identity proof that is the only cheap evidence the move changed
// nothing. This TU sits far under the 1,500-line warn ceiling, so no
// ceiling pressure forces the question either -- see
// refactor/J_file_size_reclamation.md section Optional.
//
// The eight movement/following tuning constants that used to live in the
// pre-split preamble now live in include/client/eq_movement_internal.h;
// this TU reads five of them.

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

void eqt::movement::MovementComponent::UpdateMovement()
{
	// H03: Process a deferred post-logout disconnect if the server confirmed our
	// OP_Logout. Must run before anything else, outside of any packet-callback
	// context — see the comment on m_pending_logout_disconnect and
	// ZoneProcessLogoutReply() for why this can't happen synchronously in the
	// packet handler.
	if (ctx_.lifecycle.m_pending_logout_disconnect) {
		ctx_.lifecycle.m_pending_logout_disconnect = false;
		LOG_DEBUG(MOD_ZONE, "Processing deferred post-logout disconnect...");
		ctx_.Zone().DisconnectFromZone();
		LOG_DEBUG(MOD_ZONE, "Deferred post-logout disconnect processed");
		return;  // Don't process movement post-logout
	}

	// Process deferred zone change if approved
	// This MUST be at the very start of UpdateMovement to process outside of any callback context
	//
	// T322: the teardown is no longer immediate. The official client hands
	// the source zone off gracefully (OP_SaveOnZoneReq + OP_DeleteSpawn) and
	// lets the SERVER close the session; closing it ourselves is what made
	// eqemu treat a zone change as a linkdead disconnect and disband the
	// group. See BeginGracefulZoneOut() in eq_zone.cpp for the full
	// mechanism. Note the ordering: the logout branch above still runs
	// FIRST and still calls DisconnectFromZone() directly -- a logout
	// genuinely should disconnect, and this unit does not touch it.
	if (ctx_.game_state.world().zoneChangeApproved()) {
		ctx_.game_state.world().setZoneChangeApproved(false);  // T286: only store
		if (ctx_.Zone().BeginGracefulZoneOut()) {
			return;  // Wait for the source zone's acknowledgement (below)
		}
		// Nothing to hand off (no live zone connection) -- fall straight
		// through to the hard teardown rather than stalling the transition.
		LOG_DEBUG(MOD_ZONE, "Processing deferred zone change...");
		ctx_.Zone().ProcessDeferredZoneChange();
		LOG_DEBUG(MOD_ZONE, "Deferred zone change processed");
		return;  // Don't process movement while zone changing
	}

	// T322: waiting on the source zone's OP_LogoutReply acknowledgement of
	// our graceful zone-out. Bounded: a lost or never-sent reply must not
	// wedge zoning, so the teardown runs on the deadline regardless. Either
	// way DisconnectFromZone() runs exactly once, so the old connection can
	// never be leaked.
	if (ctx_.Zone().ZoneOutHandshakePending()) {
		const bool timed_out = std::chrono::steady_clock::now() >= ctx_.Zone().ZoneOutHandshakeDeadline();
		if (ctx_.Zone().ZoneOutAcknowledged() || timed_out) {
			if (timed_out && !ctx_.Zone().ZoneOutAcknowledged()) {
				LOG_WARN(MOD_ZONE, "Source zone did not acknowledge zone-out within {} ms; tearing down anyway",
					eqt::zone::ZoneComponent::kZoneOutHandshakeTimeoutMs);
			}
			LOG_DEBUG(MOD_ZONE, "Processing deferred zone change...");
			ctx_.Zone().ProcessDeferredZoneChange();  // clears the handshake flags
			LOG_DEBUG(MOD_ZONE, "Deferred zone change processed");
		}
		return;  // Don't process movement while zone changing
	}

	// T245: auto-combat tick, ON THE GAME THREAD. This replaces the 500 ms
	// m_update_thread (eq.cpp StartUpdateLoop) whose only job was this call;
	// CombatManager::Update() reaches m_game_state.combat() writes
	// (SetTarget/ClearTarget from CheckForAggro / ProcessCombatRound /
	// FindNextHuntTarget) and reads m_entities, both game-thread data. Same
	// gate the old loop had (m_update_running && IsFullyZonedIn()); Update()
	// paces itself at kUpdateIntervalMs so the per-tick call is cheap.
	if (ctx_.combat_manager && ctx_.self.IsFullyZonedIn()) {
		ctx_.combat_manager->Update();
	}

	// Update camp timer
	ctx_.Player().UpdateCampTimer();

	// Check distance to banker NPC - close bank if player moved too far
	if (ctx_.self.IsBankWindowOpen() && ctx_.game_state.player().bankerNpcId() != 0) {
		auto it = ctx_.entities.find(ctx_.game_state.player().bankerNpcId());
		if (it == ctx_.entities.end()) {
			// Banker NPC no longer exists (despawned, etc.)
			LOG_DEBUG(MOD_INVENTORY, "Banker NPC {} no longer exists, closing bank", ctx_.game_state.player().bankerNpcId());
			ctx_.Merchant().CloseBankWindow();
		} else {
			float dist = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), it->second.x, it->second.y);
			if (dist > NPC_INTERACTION_DISTANCE) {
				LOG_DEBUG(MOD_INVENTORY, "Player moved too far from banker ({:.1f} > {:.1f}), closing bank",
					dist, NPC_INTERACTION_DISTANCE);
				ctx_.Merchant().CloseBankWindow();
				ctx_.out.System("You have moved too far from the banker.");
			}
		}
	}

	// Debug: Log target at start of UpdateMovement
	static auto last_update_debug = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (ctx_.game_state.combat().inCombatMovement() &&
	    std::chrono::duration_cast<std::chrono::seconds>(now - last_update_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement START: target=({:.1f},{:.1f},{:.1f}) is_moving={} path_size={}",
			ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ(), ctx_.game_state.player().isMoving(), m_current_path.size());
		last_update_debug = now;
	}
	
	// Update keyboard-based movement first
	if (ctx_.game_state.player().moveForward() || ctx_.game_state.player().moveBackward() || ctx_.game_state.player().turnLeft() || ctx_.game_state.player().turnRight()) {
		UpdateKeyboardMovement();
		// Check for zone line collision
		ctx_.Zone().CheckZoneLine();
		// SendPositionUpdate has internal 250ms throttling
		SendPositionUpdate();
		// Don't process other movement if using keyboard
		return;
	}
	
	// Update combat movement if active
	if (ctx_.game_state.combat().inCombatMovement()) {
		UpdateCombatMovement();
	}
	
	// Check if we're following someone
	if (!ctx_.game_state.player().followTarget().empty()) {
		// Find the entity we're following
		for (const auto& pair : ctx_.entities) {
			const Entity& entity = pair.second;
			if (entity.name == ctx_.game_state.player().followTarget()) {
				// Check if we're close enough
				float dist = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), entity.x, entity.y);
				float z_diff = std::abs(ctx_.self.PosZ() - entity.z);
				
				// Log follow status periodically
				static std::chrono::steady_clock::time_point last_follow_log = std::chrono::steady_clock::now();
				auto now_follow = std::chrono::steady_clock::now();
				auto elapsed_follow = std::chrono::duration_cast<std::chrono::milliseconds>(now_follow - last_follow_log);
				
				if (elapsed_follow.count() >= 1000 && EverQuest::GetDebugLevel() >= 1) {
					LOG_DEBUG(MOD_MAIN, "Following {}: Distance={:.1f} (stop at {:.1f}), Z-diff={:.1f}, Speed={:.1f}", ctx_.game_state.player().followTarget(), dist, ctx_.game_state.player().followDistance(), z_diff, ctx_.game_state.player().moveSpeed());
					last_follow_log = now_follow;
				}
				
				if (dist < ctx_.game_state.player().followDistance()) {
					// We're close enough, stop if we were moving
					if (ctx_.game_state.player().isMoving()) {
						if (EverQuest::GetDebugLevel() >= 1) {
							LOG_DEBUG(MOD_MAIN, "Reached follow distance ({:.1f}), stopping", dist);
						}
						StopMovement();
					}
					return;
				} else if (dist > ctx_.game_state.player().followDistance() * 1.5f) {
					// Target has moved significantly, recalculate path
					// Check if final destination changed significantly
					float final_dest_dist = 0.0f;
					if (!m_current_path.empty() && m_current_path.size() > 0) {
						// Compare with the final waypoint in our path
						const auto& final_waypoint = m_current_path.back();
						final_dest_dist = CalculateDistance2D(final_waypoint.x, final_waypoint.y, entity.x, entity.y);
					} else {
						// No path, compare with our immediate target
						final_dest_dist = CalculateDistance2D(ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), entity.x, entity.y);
					}
					
					if (final_dest_dist > 5.0f || m_current_path.empty()) {
						// Recalculate path using pathfinding
						LOG_DEBUG(MOD_MAIN, "UpdateMovement: Target moved significantly (dist={:.2f})", final_dest_dist);
						if (m_use_pathfinding && m_pathfinder) {
							// Find path from current position to target
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Recalculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity.x, entity.y, entity.z);
							if (FindPath(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity.x, entity.y, entity.z)) {
								// Start following the path
								m_current_path_index = 0;
								ctx_.game_state.player().setMoving(true);
								LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement: Path recalculated with {} waypoints",
									m_current_path.size());
								for (size_t i = 0; i < std::min(m_current_path.size(), size_t(3)); i++) {
									LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
										i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
								}
							} else {
								// Pathfinding failed, use direct movement
								LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding failed, using direct movement");
								ctx_.game_state.player().setMovementTarget(entity.x, entity.y, entity.z);
								ctx_.game_state.player().setMoving(true);
							}
						} else {
							// Fallback to direct movement
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding disabled, using direct movement");
							ctx_.game_state.player().setMovementTarget(entity.x, entity.y, entity.z);
							ctx_.game_state.player().setMoving(true);
						}
					}
				}
				break;
			}
		}
	}
	
	// Always update jump if jumping, even when not moving
	if (ctx_.game_state.player().isJumping()) {
		float old_z = ctx_.self.PosZ();
		UpdateJump();
		
		// If Z changed, send position update immediately
		if (ctx_.self.PosZ() != old_z) {
			SendPositionUpdate();
		}
	}
	
	if (!ctx_.game_state.player().isMoving()) {
		// Still check for zone lines when stopped (player might have stopped in one)
		ctx_.Zone().CheckZoneLine();

		// Send idle position updates like real clients (every 1-2 seconds)
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_idle_updates;
		if (last_idle_updates.find(&owner_) == last_idle_updates.end()) {
			last_idle_updates[&owner_] = std::chrono::steady_clock::now();
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_idle_updates[&owner_]);

		if (elapsed.count() >= 1500) {  // 1.5 seconds average
			SendPositionUpdate();
			last_idle_updates[&owner_] = now;
		}
		return;
	}
	
	// Check if we're following a path
	if (!m_current_path.empty() && m_current_path_index < m_current_path.size()) {
		// Check if we've reached the current waypoint
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist_to_waypoint = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), waypoint.x, waypoint.y);

		// Always log path progress for debugging coordinate issues
		static auto last_path_debug = std::chrono::steady_clock::now();
		auto now_path_debug = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now_path_debug - last_path_debug).count() >= 500) {
			LOG_INFO(MOD_MAIN, "Path progress: pos=({:.1f},{:.1f},{:.1f}) waypoint[{}]=({:.1f},{:.1f},{:.1f}) dist={:.1f}",
				ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist_to_waypoint);
			last_path_debug = now_path_debug;
		}

		LOG_DEBUG(MOD_MOVEMENT, "Following path: waypoint {}/{}, dist to waypoint: {:.2f}",
			m_current_path_index, m_current_path.size()-1, dist_to_waypoint);
		
		// Stuck detection - track if we're not making progress
		static std::map<EverQuest*, std::pair<float, std::chrono::steady_clock::time_point>> stuck_detection;
		auto now = std::chrono::steady_clock::now();
		
		if (stuck_detection.find(&owner_) == stuck_detection.end()) {
			stuck_detection[&owner_] = {dist_to_waypoint, now};
		} else {
			auto& [last_dist, last_time] = stuck_detection[&owner_];
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
					stuck_detection.erase(&owner_);
				} else {
					LOG_DEBUG(MOD_MOVEMENT, "Stuck on final waypoint, stopping movement");
					StopMovement();
					stuck_detection.erase(&owner_);
					return;
				}
			} else if (elapsed >= 1) {
				// Update every second if we've made progress
				if (std::abs(dist_to_waypoint - last_dist) > 0.5f) {
					stuck_detection[&owner_] = {dist_to_waypoint, now};
				}
			}
		}
		
		// Check both 2D distance and Z difference for waypoint reached
		float z_diff_to_waypoint = std::abs(ctx_.self.PosZ() - waypoint.z);
		bool waypoint_reached = (dist_to_waypoint < 5.0f && z_diff_to_waypoint < 10.0f) || dist_to_waypoint < 3.0f;
		
		if (waypoint_reached) { // Within range of waypoint
			// Move to next waypoint
			m_current_path_index++;
			stuck_detection.erase(&owner_); // Clear stuck detection when reaching waypoint
			
			if (m_current_path_index >= m_current_path.size()) {
				// Reached the end of the path
				LOG_DEBUG(MOD_MAIN, "Reached end of path");
				StopMovement();
				return;
			} else {
				// Move to next waypoint
				const auto& next_waypoint = m_current_path[m_current_path_index];
				
				// Check if the next waypoint is different enough from current position
				float dist_to_next = CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), next_waypoint.x, next_waypoint.y);
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
	bool has_console_movement = !ctx_.game_state.player().followTarget().empty() || !m_current_path.empty() ||
	                            ctx_.game_state.combat().inCombatMovement() ||
	                            (ctx_.game_state.player().targetX() != 0.0f || ctx_.game_state.player().targetY() != 0.0f || ctx_.game_state.player().targetZ() != 0.0f);

	if (!has_console_movement) {
		// No console-based movement active, skip target processing
		return;
	}

	// Debug: Check what target we're moving to
	static auto last_target_debug = std::chrono::steady_clock::now();
	auto now_debug = std::chrono::steady_clock::now();
	if (ShouldLog(MOD_MOVEMENT, LOG_DEBUG) && std::chrono::duration_cast<std::chrono::seconds>(now_debug - last_target_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "Movement target check: current=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) path_size={} path_index={}",
			ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ(), m_current_path.size(), m_current_path_index);
		last_target_debug = now_debug;
	}

	// Calculate distance to target
	float dx = ctx_.game_state.player().targetX() - ctx_.self.PosX();
	float dy = ctx_.game_state.player().targetY() - ctx_.self.PosY();
	float dz = ctx_.game_state.player().targetZ() - ctx_.self.PosZ();
	float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

	// Check if we've reached the target
	if (distance < 2.0f) {  // Stop within 2 units of target
		LOG_DEBUG(MOD_MOVEMENT, "Reached target at distance {:.2f}, target=({:.1f},{:.1f},{:.1f})",
			distance, ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ());

		ctx_.self.SetPosLocal(ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(),
		            ctx_.game_state.player().targetZ());

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
	if (last_move_times.find(&owner_) == last_move_times.end()) {
		last_move_times[&owner_] = std::chrono::steady_clock::now();
	}
	auto now_movement = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - last_move_times[&owner_]);
	last_move_times[&owner_] = now_movement;
	
	float delta_time = elapsed.count() / 1000.0f;  // Convert to seconds
	if (delta_time > 0.1f) delta_time = 0.1f;  // Cap at 100ms to prevent large jumps
	
	// Debug delta time issues
	if (ctx_.game_state.combat().inCombatMovement() && delta_time < 0.01f) {
		LOG_WARN(MOD_MOVEMENT, "Very small delta_time: {:.6f}s ({}ms)",
			delta_time, elapsed.count());
	}
	
	// Calculate movement step using current movement speed
	float current_speed = ctx_.game_state.player().moveSpeed();
	
	// Adjust speed when following
	if (!ctx_.game_state.player().followTarget().empty()) {
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
	if (EverQuest::GetDebugLevel() >= 1 && ctx_.game_state.combat().inCombatMovement() && 
	    std::chrono::duration_cast<std::chrono::seconds>(now_movement - last_move_debug).count() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement update: pos=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) dist={:.1f} speed={:.1f} step={:.1f}", ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.game_state.player().targetX(), ctx_.game_state.player().targetY(), ctx_.game_state.player().targetZ(), distance, current_speed, step);
		last_move_debug = now_movement;
	}
	
	// Normalize direction and move
	float factor = step / distance;
	float old_x = ctx_.self.PosX();
	float old_y = ctx_.self.PosY();
	ctx_.self.SetPosLocal(ctx_.self.PosX() + dx * factor, ctx_.self.PosY() + dy * factor, ctx_.self.PosZ() + dz * factor);
	
	// Debug: verify position actually changed
	if (EverQuest::GetDebugLevel() >= 1 && ctx_.game_state.combat().inCombatMovement()) {
		LOG_DEBUG(MOD_MAIN, "Position update: ({:.1f},{:.1f}) -> ({:.1f},{:.1f}) delta=({:.3f},{:.3f})", old_x, old_y, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosX() - old_x, ctx_.self.PosY() - old_y);
	}
	
	// Update heading (from the pre-step position to the post-step position;
	// old_x/old_y ARE that pre-step position -- identical arithmetic to the
	// pre-T288 `PosX() - dx * factor` form, which recomputed it)
	ctx_.self.SetHeadingLocal(CalculateHeading(old_x, old_y, ctx_.self.PosX(), ctx_.self.PosY()));

	// Update bridge position for console-driven movement (moveto, follow, etc.)
	if (ctx_.bridge) {
		eqt::state::PlayerMovedData pdata;
		pdata.x = ctx_.self.PosX(); pdata.y = ctx_.self.PosY(); pdata.z = ctx_.self.PosZ();
		pdata.heading = ctx_.self.HeadingDeg();
		pdata.dx = dx; pdata.dy = dy; pdata.dz = dz;
		pdata.isMoving = true;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
	}

	// Set animation based on actual movement distance
	// Don't change animation if we're jumping
	if (!ctx_.game_state.player().isJumping()) {
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
		if (ctx_.game_state.player().animation() != new_animation) {
			ctx_.game_state.player().setAnimation(new_animation);
		}
	}
	
	// Update jump if jumping
	bool was_jumping = ctx_.game_state.player().isJumping();
	float old_z = ctx_.self.PosZ();
	UpdateJump();
	
	// If jumping and Z changed, send position update immediately
	if (ctx_.game_state.player().isJumping() && ctx_.self.PosZ() != old_z) {
		SendPositionUpdate();
		m_last_position_update_time = now;
	}
	
	// Fix Z position periodically to follow terrain (but not while jumping)
	if (!ctx_.game_state.player().isJumping()) {
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_z_fix_times;
		if (last_z_fix_times.find(&owner_) == last_z_fix_times.end()) {
			last_z_fix_times[&owner_] = now;
		}
		
		auto z_fix_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_z_fix_times[&owner_]);
		// Fix Z every 1000ms while moving (once per second to reduce adjustments)
		if (z_fix_elapsed.count() >= 1000) {
			ctx_.Zone().FixZ();
			last_z_fix_times[&owner_] = now;
		}
	}
	
	// Check for zone line collision
	ctx_.Zone().CheckZoneLine();

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


void eqt::movement::MovementComponent::SendPositionUpdate()
{
	// T88 test seam: m_test_bypass_send_guard skips this guard (and the
	// throttle below) so a headless test can reach the encoder logic
	// without a live m_zone_connection. See Test_SetBypassSendGuardForSeam.
	if (!owner_.m_test_bypass_send_guard && (!ctx_.self.IsFullyZonedIn() || !ctx_.zone_connection)) {
		LOG_DEBUG(MOD_MOVEMENT, "SendPositionUpdate skipped: zoned_in={} zone_conn={}",
			ctx_.self.IsFullyZonedIn(), ctx_.zone_connection != nullptr);
		return;
	}

	// Throttle position updates to ~250ms minimum (working client averages ~275ms)
	// This prevents jerky motion when viewed by other players
	static auto lastSendTime = std::chrono::steady_clock::now();
	static int16_t lastSentAnimation = 0;
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTime);

	// Allow immediate send when stopping (animation goes to 0) for responsiveness
	bool isStoppingNow = (ctx_.game_state.player().animation() == 0 && lastSentAnimation != 0);

	// T231: a forced update (same-zone teleport, RelocateWithinZone) skips
	// the throttle -- the server is waiting on exactly this packet -- and
	// restarts delta tracking from the new position so the jump is not
	// encoded as a velocity. One-shot; consumed here.
	const bool force_update = m_force_next_position_update;
	m_force_next_position_update = false;

	if (!owner_.m_test_bypass_send_guard && !force_update && !isStoppingNow && elapsed.count() < 250) {
		// Throttled - skip this update
		return;
	}

	lastSendTime = now;
	lastSentAnimation = ctx_.game_state.player().animation();
	
	// Keep track of last position for delta calculations (per instance, not static)
	static std::map<EverQuest*, std::tuple<float, float, float, float>> last_positions;
	
	// Initialize last position if this is the first update
	if (force_update || last_positions.find(&owner_) == last_positions.end()) {
		last_positions[&owner_] = std::make_tuple(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg());
	}
	
	auto& [last_x, last_y, last_z, last_heading] = last_positions[&owner_];
	
	// Calculate deltas
	float delta_x = ctx_.self.PosX() - last_x;
	float delta_y = ctx_.self.PosY() - last_y;
	float delta_z = ctx_.self.PosZ() - last_z;
	float delta_heading_deg = ctx_.self.HeadingDeg() - last_heading;
	
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
	float server_heading = 90.0f - ctx_.self.HeadingDeg();
	if (server_heading < 0.0f) server_heading += 360.0f;
	if (server_heading >= 360.0f) server_heading -= 360.0f;
	int heading_raw = static_cast<int>(server_heading * 2048.0f / 360.0f);
	// FloatToEQ12 does modulo 2048, so values wrap
	uint16_t heading_scaled = heading_raw % 2048;
	
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] heading={:.2f}deg -> server_heading={:.2f}deg -> heading_scaled={} (12-bit)",
			ctx_.self.HeadingDeg(), server_heading, heading_scaled);
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
	update.spawn_id = ctx_.self.MySpawnId();
	ctx_.game_state.player().incrementMovementSequence();
	update.sequence = ctx_.game_state.player().movementSequence();

	// Validate that we have a valid spawn ID
	if (ctx_.self.MySpawnId() == 0) {
		LOG_ERROR(MOD_MOVEMENT, "SendMovementUpdate called with a spawn id of 0! Not sending update.");
		return;
	}

	// Check if we're accidentally using another player's ID
	auto it = ctx_.entities.find(ctx_.self.MySpawnId());
	if (it != ctx_.entities.end() && it->second.name != ctx_.character) {
		LOG_WARN(MOD_MOVEMENT, "m_my_spawn_id {} belongs to '{}', not our character '{}'!",
			ctx_.self.MySpawnId(), it->second.name, ctx_.character);
	}
	
	// Convert from internal coordinate system back to server coordinate system
	// When receiving from server: client x = server y, client y = server x (see ZoneProcessPlayerProfile)
	// So to send back: server's y_pos = client x, server's x_pos = client y
	update.y_pos = ctx_.self.PosX();  // client x holds server's original y
	update.x_pos = ctx_.self.PosY();  // client y holds server's original x
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
	//
	// zAwaitingFrameConversion means PosZ() is still the raw wire value the
	// self-spawn delivered (the model had not loaded, so nothing converted it).
	// Converting it here would add the offset a SECOND time, and since the
	// server saves what we send and hands it back next login, that error
	// compounds 0.625 per zone-in -- measured 3.63 -> 4.25 -> 4.88 over three
	// logins. Send it back verbatim until the renderer has told us the offset.
	update.z_pos = ctx_.game_state.player().zAwaitingFrameConversion()
		? ctx_.self.PosZ()
		: eqt::renderZToWireZ(ctx_.self.PosZ(),
		                      ctx_.game_state.player().collisionZOffset(),
		                      ctx_.game_state.player().size());
	// Deltas follow the same transformation:
	// Internal delta_x (change in client x) = change in server's y = server's delta_y
	// Internal delta_y (change in client y) = change in server's x = server's delta_x
	update.delta_x = delta_y;  // server's delta_x = change in client y (server's x)
	update.delta_y = delta_x;  // server's delta_y = change in client x (server's y)
	update.delta_z = delta_z;
	
	// Pack animation and delta_heading into bitfield
	// Use current animation state (which includes jump animation)
	uint32_t anim_value = ctx_.game_state.player().animation();
	update.anim_and_delta_heading = (anim_value & 0x3FF) | (0 << 10) | (1 << 20);
	
	// Pack heading into bitfield (12 bits)
	update.heading_and_padding = heading_scaled;
	
	// Build packet
	EQ::Net::DynamicPacket p;
	p.Resize(sizeof(update) + 2);
	p.PutUInt16(0, HC_OP_ClientUpdate);
	memcpy((char*)p.Data() + 2, &update, sizeof(update));
	
	// Debug: Verify what's in the packet
	if (EverQuest::GetDebugLevel() >= 1) {
		uint16_t packet_spawn_id = p.GetUInt16(2); // spawn_id is first field after opcode
		// LOG_DEBUG(MOD_MAIN, "ClientUpdate packet: spawn_id in struct = {}, spawn_id in packet = {}", 
		// 	update.spawn_id, packet_spawn_id);
	}
	
	// Log C->S position updates at debug level 2+
	if (EverQuest::GetDebugLevel() >= 2) {
		// Log server coordinates (x_pos, y_pos) = (client y, client x) due to coordinate swap
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S spawn_id={} server_pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} server_delta=({:.2f}, {:.2f}, {:.2f})",
			ctx_.self.MySpawnId(), update.x_pos, update.y_pos, update.z_pos, ctx_.self.HeadingDeg(), ctx_.game_state.player().animation(), update.delta_x, update.delta_y, update.delta_z);
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] client_pos=({:.2f},{:.2f},{:.2f}) -> server_pos=({:.2f},{:.2f},{:.2f})",
			ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), update.x_pos, update.y_pos, update.z_pos);
	}

	// T88 test seam: capture what was actually computed, unconditionally,
	// before the (possibly test-suppressed) send. See
	// Test_GetLastComputedPositionUpdate.
	m_test_last_sent_update.x_pos = update.x_pos;
	m_test_last_sent_update.y_pos = update.y_pos;
	m_test_last_sent_update.z_pos = update.z_pos;
	m_test_last_sent_update.heading_scaled = update.heading_and_padding;

	eqt::strings::DumpPacket("C->S", HC_OP_ClientUpdate, p);
	ctx_.net.SafeQueueZone(p, 0, false);  // Send unreliable
	
	// Update last position
	last_x = ctx_.self.PosX();
	last_y = ctx_.self.PosY();
	last_z = ctx_.self.PosZ();
	last_heading = ctx_.self.HeadingDeg();
	
	// Track movement history for anti-cheat
	eqt::zone::ZoneComponent::MovementHistoryEntry history_entry;
	history_entry.x = ctx_.self.PosX();
	history_entry.y = ctx_.self.PosY();
	history_entry.z = ctx_.self.PosZ();
	history_entry.type = 1; // Normal movement (Collision type)
	history_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	
	ctx_.Zone().MovementHistory().push_back(history_entry);
	
	// Send movement history every second (matching client behavior)
	uint32_t current_time = static_cast<uint32_t>(std::time(nullptr));
	if (current_time - ctx_.Zone().LastMovementHistorySend() >= 1) {
		ctx_.Zone().LastMovementHistorySend() = current_time;  // Update timestamp BEFORE sending to prevent duplicates
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

float eqt::movement::MovementComponent::CalculateHeading(float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	
	// If we're not moving, preserve current heading
	if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
		return ctx_.self.HeadingDeg();
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
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "CalculateHeading: from ({:.1f},{:.1f}) to ({:.1f},{:.1f}), dx={:.1f}, dy={:.1f}, raw angle={:.1f}°, adjusted={:.1f}°", 
			x1, y1, x2, y2, dx, dy, angle * 180.0f / M_PI, degrees);
	}
	
	return degrees;
}

float eqt::movement::MovementComponent::CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dz = z2 - z1;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float eqt::movement::MovementComponent::CalculateDistance2D(float x1, float y1, float x2, float y2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	return std::sqrt(dx * dx + dy * dy);
}

