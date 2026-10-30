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
// became `&owner_`; T366 put them back on `this`.
//
// T366 (B038 CLOSED): the `owner_` TRANSITION handle and the
// `friend class eqt::movement::MovementComponent` line are both GONE. Its two
// last uses were the private m_test_bypass_send_guard (the T88 seam, now on
// eqt::PacketSender, read as ctx_.net.TestBypassSendGuard()) and the 22
// `&owner_` identity keys in the function-static throttle tables, which are
// back on `this` -- see movement_update.cpp for the one-to-one invariant.

#include "client/client_context.h"
#include "client/eq_enums.h"
#include "client/pathfinder_interface.h"
// B893: the ONE jump/fall model (arc constants + the FallTracker + the
// EnvDamage encoder).
#include "client/jump_fall_physics.h"
// B892 (GitLab #208): SyncLevitation reads the buff list, the spell database
// and the self row's flymode.
#include "client/spell/buff_manager.h"
#include "client/spell/levitation.h"
#include "client/spell/spell_database.h"

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
	// T366 (B038): the EverQuest& transition handle is gone -- this component
	// names nothing on EverQuest any more.
	explicit MovementComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Commanded movement API (was src/client/eq_movement.cpp).
	void Move(float x, float y, float z);
	// T356 (B003c): id-keyed, because the caller (ActionDispatcher) has
	// already resolved the name with the nearest-first ranked resolver and
	// a second, unranked, name resolution here could pick a different
	// same-named entity.
	void MoveToEntity(uint16_t spawn_id);
	// T359 (B003d): the same rule one layer on. Every one of these had a
	// caller that already HELD the spawn id and handed over a name, which a
	// second unranked prefix match then re-resolved -- pure drift. The
	// name-taking StartCombatMovement overload had no production caller at all
	// and was deleted with the resolver call it made.
	void MoveToEntityWithinRange(uint16_t spawn_id, float stop_distance);
	void StartCombatMovement(uint16_t entity_id);
	void Follow(uint16_t spawn_id);
	void StopFollow();
	void FaceEntity(uint16_t spawn_id);
	void MoveTo(float x, float y, float z);
	void StopMovement();

	// The per-tick pump and the two geometry helpers other families call
	// (was eq_movement_update.cpp).
	// B893: the ONE fall-damage sender. Real play drives this from the
	// MovementComponent's own fall tracker (UpdateJump / the movement pump) in
	// headless mode and from a PlayerFallDamage intent in graphical mode (the
	// renderer's landing detection, which the bridge turns into this call).
	// Encodes the 31-byte EnvDamage2_Struct (dmgtype 252 = Falling) and queues
	// it to the zone; the server reads `damage` verbatim (akk-stack
	// code/zone/client_packet.cpp:6316) and echoes the chat line. A drop of at
	// most the table's safeHeight sends nothing.
	void SendFallDamage(float fallHeight);
	void UpdateMovement();
	void SendPositionUpdate();

	// B911/B912 (GitLab #230/#231): true while the GAME THREAD is walking the
	// player -- a /move, /moveto or /follow target or path, or an auto-attack
	// chase. The same predicate UpdateMovement() gates its integrator on.
	// Keyboard movement never sets it (the renderer moves the player itself
	// and only reports back through PlayerPositionChanged).
	bool HasCommandMovement() const;
	// B911/B912: hand the renderer the player's current pose from the one
	// store, as the renderer-bridge PlayerMoved every other game-side mover
	// publishes (heading in the EQ 0..512 unit, PlayerMovedData's contract).
	// isMoving = HasCommandMovement(), so the renderer can tell a walk the game
	// thread is driving from a still pose. Game thread only; no-op without a
	// bridge. The per-tick integrator, StopMovement() after a command walk and
	// /turn and /face (EqActionHandler::setHeading) are its callers. dx/dy/dz
	// ride along verbatim (the integrator passes its step direction; nothing
	// on the renderer side reads them today).
	void PublishSelfPoseToRenderer(float dx = 0.0f, float dy = 0.0f, float dz = 0.0f);
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

	// T343 (board B032) DELETED the keyboard-control cluster that stood here:
	// StartMove*/StartTurn*, StopMove*/StopTurn* and UpdateKeyboardMovement().
	// T319 kept them as "the only game-side mover the client has"; T343
	// measured the renderer's per-frame input poll (readMovementInput(),
	// renderer_player_physics.cpp) and found forward/backward/turn driven from
	// there every frame, exactly like swim -- so this was a second, producerless
	// route, not a switch missing its on position. The end of
	// movement_state.cpp carries the full note. stopAllMovement() now calls
	// StopMovement(), which is what /stop always needed.

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
	// B502 (GitLab #177) DELETED WaterStateRef() (the mutable-view twin of
	// GetWaterState()) and IsSwimming() (its one-line predicate): both had
	// zero callers -- BridgeComponent reads GetWaterState() directly.
	WaterState GetWaterState() const { return m_water_state; }

	// B892 (GitLab #208): the player's own levitation, as the GAME THREAD
	// sees it. eqemu sends the caster the levitate appearance with
	// ignore_self (spell_effects.cpp:1452-1455), so the caster's own client
	// never receives AT_FLYMODE 2 from a self-cast; the official client
	// derives its own levitation from its buff list instead. EQ::
	// PlayerIsLevitating() (client/spell/levitation.h) is the predicate --
	// any active buff whose spell carries SPA 57, OR a self flymode 2/5 the
	// server did send.
	bool PlayerLevitating() const {
		return m_levitation == EQ::LevitationState::Levitating;
	}
	// The one sync point. `selfFlymode` is the self row's AT_FLYMODE value,
	// passed in by the caller (the production sites read the entity map; a
	// test rig without a zoned-in self row passes 0) -- this component does
	// not reach into the map itself. Called by the buff apply hook (no
	// exclusion -- the landing buff is already in the list and counts), the
	// buff FADE hook (with the fading spell excluded -- the fade fires while
	// the buff is still in the list, the same pre-erase notify as T339's
	// vision recompute) and on a self flymode change. No-op without a spell
	// database (a client before InitializeSpellSystem: nothing to read) and
	// without a bridge. Publishes LevitationChanged only when the state
	// flips.
	void SyncLevitation(uint8_t selfFlymode,
	                    uint32_t fade_spell_id = EQ::BuffManager::NO_EXCLUDED_SPELL);
	// Silent reset: a state change without the publish. The zone
	// transition clears the entity map the flymode is read from (and the
	// buff list is re-applied from the character profile there), so this
	// just brings the stored state back to its start-of-zone value.
	void ResetLevitation() { m_levitation = EQ::LevitationState::NotLevitating; }

private:
	// B1183 (#502): the shared body (state write, stand byte, bridge event,
	// movement stop) and the stand-byte store (stand_state.h): server-forced
	// callers store the client's direct value; the user path goes through
	// the setter (FUN_00483556's port).
	void applyPositionStateCore(PositionState state, bool userInitiated);
	void storeStandByte(PositionState state, bool userInitiated);
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
	// B502 (GitLab #177) DELETED GetMovementSpeed(): a one-line wrapper over
	// game_state.player().moveSpeed() with zero callers (the keyboard
	// integrator T343 deleted was its only prospective reader).

	ClientContext& ctx_;

	// K13: swimming state. Was EverQuest::m_water_state.
	WaterState m_water_state = WaterState::NotInWater;

	// B892 (GitLab #208): see PlayerLevitating() above.
	EQ::LevitationState m_levitation = EQ::LevitationState::NotLevitating;

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

	// T88 test seam (see eqt::PacketSender::Test_SetBypassSendGuardForSeam /
	// Test_GetLastComputedPositionUpdate above).
	TestSentPositionUpdate m_test_last_sent_update;
};

}  // namespace movement
}  // namespace eqt
