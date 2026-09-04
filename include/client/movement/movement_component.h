#pragma once

// Batch K (refactor/K_component_extraction.md), unit K07.
//
// The movement family -- the commanded movement API, the per-tick pump, and
// the state movement depends on (pathfinding, movement mode / position state,
// jump, movement history, water) -- lifted out of class EverQuest.
//
// Every body in src/client/movement/*.cpp is the body that used to live in
// src/client/eq_movement{,_state,_update}.cpp, moved verbatim; the only edits
// inside a body are the substitutions Mechanism 4 allows (m_x -> ctx_.x /
// owner_.m_x, Foo() -> owner_.Foo(), EverQuest statics and nested types
// qualified), plus the K07 addition recorded in the Review: `this`, where a
// moved body used it purely as an identity key into a function-static map,
// became `&owner_` so the key is bit-for-bit the one it always was.
//
// `owner_` is the TRANSITION handle: the moved code still reaches EverQuest
// members and methods that have not been extracted yet (m_movement_history,
// m_water_state, m_zone_bsp_tree, m_navmesh_path, the zone-out handshake
// flags, PosX()/PosY()/PosZ(), SafeQueueZonePacket(), ...). EverQuest declares
// `friend class eqt::movement::MovementComponent` for exactly that. K-final
// removes both the handle and the friendship.

#include "client/client_context.h"
#include "client/eq_enums.h"
#include "client/pathfinder_interface.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class EverQuest;

namespace eqt {
namespace movement {

class MovementComponent {
public:
	MovementComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

	// Commanded movement API (was src/client/eq_movement.cpp).
	void Move(float x, float y, float z);
	void MoveToEntity(const std::string &name);
	void MoveToEntityWithinRange(const std::string &name, float stop_distance);
	void StartCombatMovement(const std::string &name, float stop_distance);
	void StartCombatMovement(uint16_t entity_id);
	void Follow(const std::string &name);
	void StopFollow();
	void FaceEntity(const std::string &name);
	void MoveTo(float x, float y, float z);
	void StopMovement();

	// The per-tick pump and the two geometry helpers other families call
	// (was eq_movement_update.cpp).
	void UpdateMovement();
	void SendPositionUpdate();
	float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const;
	float CalculateDistance2D(float x1, float y1, float x2, float y2) const;

	// State and dependencies (was eq_movement_state.cpp).
	void LoadPathfinder(const std::string& zone_name);
	void SetMovementMode(MovementMode mode);
	// T344 (B001): SetPositionState is the USER-INITIATED pose change --
	// it writes the state AND tells the server (OP_SpawnAppearance).
	// ApplyPositionState is the same write with NO send, for a pose the
	// SERVER forced on us (our own OP_SpawnAppearance echo, our own
	// OP_Death): re-sending a pose we were just told about is wrong, and
	// T311 Leg A measured the official client never doing it.
	void SetPositionState(PositionState state);
	void ApplyPositionState(PositionState state);
	void Jump();
	void UpdateWaterState();           // Check BSP region and update water state

	// Keyboard control. The four Start* have NO caller anywhere and are kept
	// deliberately: the note above StartMoveForward() in movement_state.cpp
	// records why (they are the only game-side mover the client has, and the
	// Stop* half that IS called would otherwise switch off something nothing
	// can switch on). Public because the wiring named in that note lands on
	// EqActionHandler, outside this component.
	void StartMoveForward();
	void StartMoveBackward();
	void StartTurnLeft();
	void StartTurnRight();
	void StopMoveForward();
	void StopMoveBackward();
	void StopTurnLeft();
	void StopTurnRight();

	// Pathfinding toggle. Was inline on EverQuest (SetPathfinding /
	// IsPathfindingEnabled); moved with m_use_pathfinding (K04 correction f).
	void SetPathfinding(bool enabled) { m_use_pathfinding = enabled; }
	bool IsPathfindingEnabled() const { return m_use_pathfinding; }

	// K07: the zone lifecycle (eq_zone.cpp, K09's territory) resets this
	// family's path state. It used to poke the members directly; now it asks.
	void ResetPath() { m_current_path.clear(); m_current_path_index = 0; }
	void ResetPathfinder() { m_pathfinder.reset(); }
	void ForceNextPositionUpdate() { m_force_next_position_update = true; }

	// T88 test seam: forces immediate arrival at the current movement
	// target, exactly mirroring UpdateMovement()'s own "distance < 2.0f"
	// arrival branch (movement_component.cpp: SetPosLocal(targetX, targetY,
	// targetZ) then StopMovement(), which sends the final
	// position) -- without depending on wall-clock-driven incremental
	// movement steps for targets outside that 2-unit snap radius. Test-only.
	void Test_SnapToMovementTargetForSeam();

	// T88 test seam: the last outbound client position update body
	// fields SendPositionUpdate() computed, captured unconditionally right
	// before the (possibly-suppressed) send -- this is the actual
	// production encoder output, not a re-derivation of the formula in the
	// test. Read-only; test-only.
	struct TestSentPositionUpdate {
		float x_pos = 0.0f;
		float y_pos = 0.0f;
		float z_pos = 0.0f;
		uint16_t heading_scaled = 0;
	};
	TestSentPositionUpdate Test_GetLastComputedPositionUpdate() const { return m_test_last_sent_update; }

	// K13 (Batch K): the swimming/water state, re-homed here from EverQuest.
	// This family is its sole writer (UpdateWaterState, and the reset in
	// CleanupZone's movement half); the only reader outside it is
	// BridgeComponent's movement-animation publish, through the accessor.
	// EverQuest::GetWaterState()/IsSwimming()/SetSwimUp()/SetSwimDown() and
	// m_swim_up/m_swim_down went with it -- all six had ZERO references
	// tree-wide beyond their own definitions and were DELETED (K10
	// correction (s)); see refactor/BOARD.md.
	WaterState& WaterStateRef() { return m_water_state; }
	WaterState GetWaterState() const { return m_water_state; }
	bool IsSwimming() const { return m_water_state != WaterState::NotInWater; }

private:
	// Reached only from inside this family (measured by the compiler, K07).
	void UpdateCombatMovement();
	void Face(float x, float y, float z);
	float CalculateHeading(float x1, float y1, float x2, float y2);
	bool FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z);
	void MoveToWithPath(float x, float y, float z);
	void FollowPath();
	void SendJump();
	void SendMovementHistory();
	void UpdateJump();
	void OnEnterWater();               // Called when entering water
	void OnExitWater();                // Called when exiting water
	float GetSwimSpeed() const;        // Get swim speed based on skill
	float GetMovementSpeed() const;
	void UpdateKeyboardMovement();

	ClientContext& ctx_;
	EverQuest& owner_;

	// K13: swimming state. Was EverQuest::m_water_state.
	WaterState m_water_state = WaterState::NotInWater;

	// Movement target timing.
	std::chrono::steady_clock::time_point m_last_position_update_time;

	// Pathfinding.
	std::unique_ptr<IPathfinder> m_pathfinder;
	std::vector<glm::vec3> m_current_path;
	size_t m_current_path_index = 0;
	bool m_use_pathfinding = true;

	// T231: one-shot flag read by SendPositionUpdate() -- bypass the 250 ms
	// throttle and restart delta tracking at the current position. Set only
	// by RelocateWithinZone().
	bool m_force_next_position_update = false;

	// Z coordinate of water surface when swimming.
	float m_water_surface_z = 0.0f;

	// T88 test seam (see EverQuest::Test_SetBypassSendGuardForSeam /
	// Test_GetLastComputedPositionUpdate above).
	TestSentPositionUpdate m_test_last_sent_update;
};

}  // namespace movement
}  // namespace eqt
