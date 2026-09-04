#ifndef EQT_GRAPHICS_PLAYER_PLAYER_CAMERA_COMPONENT_H
#define EQT_GRAPHICS_PLAYER_PLAYER_CAMERA_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L05 -- the
// player family, lifted out of class IrrlichtRenderer: player-mode movement
// and its collision, the world-triangle raycast layer, ground-height queries,
// the collision debug lines, and the camera (mode, projection, first-person
// placement and pitch clamp, player position/vision/swimming state).
//
// Every body in src/client/graphics/player/ is the body that used to live in
// src/client/graphics/renderer_player_physics.cpp,
// src/client/graphics/renderer_player_collision.cpp and
// src/client/graphics/renderer_camera.cpp, moved verbatim. THE THREE BASENAMES
// ARE LOAD-BEARING: tests/test_first_person_camera_single_formula.cpp names
// renderer_camera.cpp and renderer_player_physics.cpp as string literals and
// pins the single-first-person-formula invariant across them. Only the
// directory moved.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, and a method that stayed behind becomes owner_.Method()
// (there are exactly two: isLoading(), and rebuildRegionMesh() -- which L09
// moved, so it is now reached as owner_.ZoneLifecycle().rebuildRegionMesh()).
//
// updatePlayerMovement() is 806 lines and moved WHOLE -- Batch G decides its
// decomposition, not this unit. It needed no substitution beyond the standard
// ones: it uses no `this` as an identity key and captures nothing in a lambda.
//
// CameraMode was a nested type of IrrlichtRenderer. It could move neither with
// the family (cameraMode_ is shared with the entities and frame families and
// stayed behind) nor stay nested (this header cannot include
// irrlicht_renderer.h, which includes this one). L05 hoisted it to
// client/graphics/renderer_types.h beside VisionType; IrrlichtRenderer keeps a
// `using` alias, so IrrlichtRenderer::CameraMode::Follow still resolves for
// application_zone_loading.cpp, renderer_slash_commands.cpp and the tests.
//
// updateObjectLightColors() was in renderer_camera.cpp but is NOT this
// family's: it reads objectLights_, weatherEffects_ and fireEffectsEnabled_
// and nothing else, all RendererContext core, and its one caller is
// irrlicht_renderer.cpp. Its body moved sideways to that TU and its
// declaration stayed on IrrlichtRenderer (L04 correction 5, the measureSection
// case). L09 decides where the lighting family finally puts it.
//
// The component needs friendship on BOTH axes of K13 correction (ii): it
// reaches 20 private IrrlichtRenderer members through owner_, and it names the
// private nested type IrrlichtRenderer::DoorCollisionData.

#include "client/graphics/renderer_context.h"

#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

class PlayerCameraComponent {
public:
	PlayerCameraComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// --- Player-mode movement (was renderer_player_physics.cpp) ---
	// The per-frame local movement path. Called from
	// IrrlichtRenderer::processFrameInput() on the render thread.
	// G06 (refactor/G06_update_player_movement.md): the body was 806 lines and
	// is now the ordered list of the eight phases declared in the private
	// section below. Its exit census found ZERO exits belonging to the method
	// (all four returns/continues are inside the isActionHeld lambda), so no
	// phase needed a bool result and no early return had to be reproduced.
	void updatePlayerMovement(float deltaTime);

	// --- Collision and ground height (was renderer_player_collision.cpp) ---
	void setupMinimalZoneCollision();
	float findGroundZ(float x, float y, float currentZ);
	bool checkCollisionBspFiltered(const irr::core::vector3df& start, const irr::core::vector3df& end,
	                               irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
	bool rayTestDoorsAndObjects(const irr::core::vector3df& rayOrigin,
	                            const irr::core::vector3df& rayDir, float rayLen,
	                            size_t bspRegion,
	                            irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
	                            float& hitDistSq);
	// Direct ray-triangle intersection on pre-transformed world-space triangles (no matrix math)
	static bool rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
	                                  const irr::core::vector3df& rayDir, float rayLen,
	                                  const std::vector<irr::core::triangle3df>& triangles,
	                                  irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
	                                  float& hitDistSq);
	// Extract world-space triangles from a mesh node (called once at build time)
	static std::vector<irr::core::triangle3df> extractWorldTriangles(
		irr::scene::ISceneManager* smgr, irr::scene::IMeshSceneNode* node);
	void drawCollisionDebugLines(float deltaTime);

	// ⚠ L05 measured these five as having ZERO callers tree-wide, in-family
	// included (K03 correction a), and they are NOT dead code -- they are the
	// incremental collision-registration API that
	// src/client/graphics/renderer_zone_draw.cpp:197-198 asserts in a comment
	// is used "during the background zone loading phases". Only
	// setupMinimalZoneCollision() is actually called
	// (renderer_zone_load_objects.cpp:544). Deleting them under §5 would turn a
	// findable defect into an invisible one (L03 correction 5), so they are
	// kept, made reachable through PlayerCamera(), and BOARDED. No forwarder:
	// writing one would keep irrlicht_renderer.h as the API surface for
	// surface nothing calls, which is what Batch L exists to remove.
	void addRegionToCollision(size_t regionIdx);
	void addDoorToCollision(uint8_t doorId);
	void addObjectToCollision(size_t objIdx);
	size_t findBspRegionForPoint(float x, float y, float z);
	void clearCollisionDebugLines();

	// T335 test seam. drawCollisionDebugLines()'s only production filler is
	// addCollisionDebugLine(), which is private and is called from the movement
	// collision path -- that needs a loaded zone's collision map.
	// tests/test_collision_debug_draw.cpp has a real GLES2 device but no zone,
	// so it seeds the line list directly. Production code keeps using the
	// private filler.
	void seedCollisionDebugLineForTest(const irr::core::vector3df& start,
	                                   const irr::core::vector3df& end,
	                                   const irr::video::SColor& color,
	                                   float duration);
	size_t collisionDebugLineCount() const { return collisionDebugLines_.size(); }

	// --- Camera (was renderer_camera.cpp) ---
	void applyCameraProjectionForMode();
	void applyFirstPersonPitchClamp();
	// T74/T300: snap the camera to the player's own eye position and facing.
	// THE ONE first-person camera formula -- see
	// tests/test_first_person_camera_single_formula.cpp, which parses this
	// body by name and asserts it still applies both tunables and the stored
	// mouse-look pitch.
	void repositionFirstPersonCamera();
	// T310 Leg A: `/fpcam dump` -- the RESOLVED first-person pose as parseable
	// [FPCAM-DUMP] key=value lines.
	std::vector<std::string> buildFirstPersonCameraDumpLines() const;

	// setPlayerSpawnId, setPlayerPosition, setCameraMode, getCameraModeString,
	// setSwimmingState and setVisionType ALSO survive as one-line forwarders on
	// IrrlichtRenderer, because irrlicht_bridge.cpp, application_zone_loading.cpp
	// and two tests call them on an IrrlichtRenderer receiver and must not learn
	// a component exists (the K11 rule). Each forwarder names its caller in
	// irrlicht_renderer.h. cycleCameraMode() is NOT one of them: L05 measured
	// zero callers on an IrrlichtRenderer receiver -- graphical_mode.cpp reaches
	// IOutputRenderer's empty default body, because IrrlichtRenderer has no base
	// class (L03 correction 13, L04 correction 2).
	void setPlayerSpawnId(uint16_t spawnId);
	void setPlayerRace(uint16_t raceId);
	void setVisionType(VisionType vision);
	void setPlayerPosition(float x, float y, float z, float heading);
	void setSwimmingState(bool swimming, float swimSpeed = 20.0f, bool levitating = false);
	void setCameraMode(CameraMode mode);
	void cycleCameraMode();
	std::string getCameraModeString() const;

	// T334 (board B046) DELETED setSwimUp(), setSwimDown() and
	// setLevitating(). L03 correction 17 called the first two "the sole
	// writers" of playerMovement_.swimUp/.swimDown and concluded vertical swim
	// control had no producer. It has one, and always did: readInputState()
	// (renderer_player_physics.cpp, G06 phase 1) polls HotkeyAction::SwimUp /
	// SwimDown -- bound to [ and ] in hotkey_manager.cpp and in
	// config/hotkeys.json -- and assigns both flags every frame, exactly as it
	// does for WASD. Adding an event-driven setter beside a per-frame poll
	// could only lose the race: the poll overwrites it the next frame, and a
	// key-DOWN action has nothing to clear the flag on key-up. setLevitating()
	// was the same shape, duplicated by setSwimmingState()'s levitating
	// parameter (live, irrlicht_bridge.cpp:629). None had a pushEvent.
	//
	// resetVisionToBase() is no longer zero-caller either: setVisionType()
	// calls it, so a recomputed buff set applies over the racial base
	// (T334, B046).
	bool isSwimming() const { return playerMovement_.isSwimming; }

	// T343 (board B002): AUTORUN. readMovementInput() (G06 phase 1) has always
	// computed `playerMovement_.moveForward = forward || playerMovement_.autorun`,
	// but nothing in the tree ever wrote that flag, so the three bound keys
	// (grave, numpad +, Num Lock) travelled the whole action chain to
	// EqActionHandler::toggleAutorun()'s LOG_DEBUG stub and the player never
	// moved. The flag is RENDERER-OWNED for exactly the reason T334 recorded
	// for swimUp/swimDown: phase 1 polls the movement bindings every frame, so
	// a game-side writer would be overwritten the next frame, and there is no
	// key-up to clear it on. The toggle is the only writer besides phase 1's
	// cancel-on-movement-key.
	void toggleAutorun() { playerMovement_.autorun = !playerMovement_.autorun; }
	bool autorun() const { return playerMovement_.autorun; }
	void resetVisionToBase();
	VisionType getBaseVision() const { return baseVision_; }

	// T334 (board B045) DELETED getCameraTransform(). Its comment said "for
	// audio listener", but the audio listener has had a producer since D20c2
	// and it deliberately uses the PLAYER position, not the camera's: in third
	// person the camera can sit far behind, which would make a nearby NPC
	// sound distant. See BridgeComponent::UpdateAudioListener()
	// (bridge_component_ui_audio.cpp). A camera-based listener would be a
	// listening measurement against the official client, not this accessor.
	// ⚠ Zero callers. The vertical FOV the first-person camera is actually
	// using, in radians -- one answer, so the clamp derivation and the
	// projection cannot disagree. Kept and boarded.
	float getFirstPersonVerticalFov() const;


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.PlayerCamera().<accessor>();
	// the renderer's own init/teardown reaches them through PlayerCamera().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	CameraMode const& cameraMode() const { return cameraMode_; }
	PlayerModeConfig& playerConfig() { return playerConfig_; }
	PlayerModeConfig const& playerConfig() const { return playerConfig_; }
	uint16_t const& playerSpawnId() const { return playerSpawnId_; }

	// T332 (B049): true while ctx_.playerX/Y/Z still hold a position this
	// component was HANDED (setPlayerPosition -- the zone-load snapshot seed or
	// a PlayerMoved relocation) rather than one the movement integrator
	// produced. It rides out on every PlayerPositionChanged intent as
	// seededPosition so the game thread can tell an echo from a real move.
	bool playerPositionIsSeed() const { return playerPositionIsSeed_; }

private:
	// ------------------------------------------------------------------
	// G06: the per-frame movement phases.
	//
	// MovementFrame carries EXACTLY the locals of the old 806-line body that
	// crossed one of the cuts -- measured by declaration line vs last-use line
	// before the struct was designed (Batch G correction 19/25). Nothing else
	// moved: every other local stayed inside the phase that declares it, so no
	// lifetime changed. Each phase re-binds its fields by reference under the
	// original names, which is why the sliced code is byte-identical to the
	// pre-G06 body (the line-multiset proof in the unit's Review).
	//
	// Nested and private: nothing outside this family ever names it (K05 j),
	// exactly like CollisionDebugLine below.
	struct MovementFrame {
		float deltaTime = 0.0f;
		// Phase 1 -> 2
		bool turnLeft = false;
		bool turnRight = false;
		// Phase 2 -> 3/6/7
		float heading = 0.0f;
		float oldHeading = 0.0f;
		float oldPitch = 0.0f;
		float headingRad = 0.0f;
		// Phase 3 -> 4/5
		float moveX = 0.0f;
		float moveY = 0.0f;
		float moveZ = 0.0f;
		// Phase 4 -> 5/6
		float newX = 0.0f;
		float newY = 0.0f;
		float newZ = 0.0f;
		// Phase 5 -> 6/7, and 5 -> its own arms
		bool positionChanged = false;
		float modelYOffset = 0.0f;
		// Phase 6 -> 7
		bool headingChanged = false;
		// Phase 7 -> 8
		bool hasMovementInput = false;
	};

	// The ordered phases, in call order. updatePlayerMovement() is the list.
	void readMovementInput(MovementFrame& frame);
	void updateHeadingAndPitch(MovementFrame& frame);
	void computeMovementVector(MovementFrame& frame);
	void applyProposedPositionAndJumpPhysics(MovementFrame& frame);
	void resolveMovementCollision(MovementFrame& frame);
	// Phase 5's three collision systems -- this file's own `else if` seams.
	void resolveIrrlichtCollision(MovementFrame& frame);
	void resolveIrrlichtClearPath(MovementFrame& frame, bool useBspFiltered);
	void resolveIrrlichtBlockedPath(MovementFrame& frame, bool useBspFiltered, float checkHeight,
	                                const irr::core::vector3df& rayStart,
	                                irr::core::vector3df& hitPoint,
	                                irr::core::triangle3df& hitTriangle);
	void resolveHCMapCollision(MovementFrame& frame);
	void resolveUncollidedMovement(MovementFrame& frame);
	void publishPositionAndCamera(MovementFrame& frame);
	void sendPositionUpdateToServer(MovementFrame& frame);
	void updatePlayerMovementAnimation(MovementFrame& frame);
	// ------------------------------------------------------------------

	// In-family callers only (K03 correction a) -- never called from outside
	// these three TUs, so they need no public surface and are not §5 candidates.
	bool checkMovementCollision(float fromX, float fromY, float fromZ,
	                            float toX, float toY, float toZ);
	// Legacy meta-selector path -- cold, HCMap fallback/debug only.
	bool checkCollisionIrrlicht(const irr::core::vector3df& start, const irr::core::vector3df& end,
	                            irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle);
	// Find ground Z at position. currentZ is the node origin (server Z);
	// modelYOffset is the origin-to-feet distance.
	float findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset);
	float findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset);
	void addCollisionDebugLine(const irr::core::vector3df& start, const irr::core::vector3df& end,
	                           const irr::video::SColor& color, float duration = 0.5f);

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's five single-family members. Two of them
	// (playerMovement_, baseVision_) are invisible to any "count
	// IrrlichtRenderer:: bodies" measurement -- their only other users were
	// inline accessors in irrlicht_renderer.h, which moved with them
	// (K04 correction f / L03 correction 2, now the third L unit in a row).
	PlayerMovementState playerMovement_;
	float playerPitch_ = 0;  // Vertical look angle in degrees (-89 to 89)
	VisionType baseVision_ = VisionType::Normal;    // Base vision from race
	bool useIrrlichtCollision_ = true;  // Use zone mesh for collision instead of HCMap
	// T332: set by setPlayerPosition() (the only external writer of
	// ctx_.playerX/Y/Z), cleared by updatePlayerMovement() the moment the
	// integrator writes them itself. See playerPositionIsSeed() above.
	bool playerPositionIsSeed_ = false;

	// Debug visualization for collision rays. The nested type moved with the
	// member: nothing outside this family ever named either (K05 j).
	struct CollisionDebugLine {
		irr::core::vector3df start;
		irr::core::vector3df end;
		irr::video::SColor color;
		float timeRemaining;  // How long to display (seconds)
	};
	std::vector<CollisionDebugLine> collisionDebugLines_;

	// L13: owned state, see the accessors above.
	CameraMode cameraMode_ = CameraMode::Follow;  // Default to third-person follow camera
	PlayerModeConfig playerConfig_;
	uint16_t playerSpawnId_ = 0;  // Spawn ID of the player entity
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PLAYER_PLAYER_CAMERA_COMPONENT_H
