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
// T448 moved the following two paragraphs here out of irrlicht_renderer.h,
// where they annotated the forwarders this family left behind.
//
// L05 (Batch L): the four methods that stay as one-line forwarders on
// IrrlichtRenderer do so ONLY because these callers hold an IrrlichtRenderer
// and must not learn a component exists (the K11 rule):
//   setPlayerSpawnId  -- application_zone_loading.cpp:249,359;
//                        bridge/irrlicht_bridge.cpp:69,173
//   setVisionType     -- bridge/irrlicht_bridge.cpp:582
//   setPlayerPosition -- application_zone_loading.cpp:270,380;
//                        bridge/irrlicht_bridge.cpp:24
//   setSwimmingState  -- bridge/irrlicht_bridge.cpp:629
// setPlayerRace(), resetVisionToBase(), getBaseVision() and isSwimming() got
// NO forwarder: each has zero callers on an IrrlichtRenderer receiver, and a
// forwarder for surface nothing calls is exactly what this batch removes.
// Reach them through PlayerCamera(). T334 (B046) DELETED setSwimUp(),
// setSwimDown() and setLevitating(): the first two were never the sole writers
// of playerMovement_.swimUp/.swimDown -- the G06 input poll assigns both every
// frame from the [ and ] bindings -- and the third duplicated
// setSwimmingState()'s then-levitating parameter (itself removed by B1011;
// setLevitationState() is the one writer now). resetVisionToBase() is no longer
// a dead end either: setVisionType() calls it (B046).
//
// The camera forwarders that stayed, each with a real caller on an
// IrrlichtRenderer receiver:
//   setCameraMode       -- application_zone_loading.cpp:268,378;
//                          tests/test_zoning_graphics_integration.cpp:729
//   getCameraModeString -- tests/test_irrlicht_camera_command.cpp:68,106
// cycleCameraMode() is NOT among them: L05 measured zero callers on that
// class. Its live callers are both in the renderer, on the component: the
// F9 arm (FrameComponent::processCommonInput, renderer_frame_input.cpp) and
// /camera cycle (renderer_slash_commands.cpp) -- T461. (The output-layer
// route that used to reach IOutputRenderer's empty default body through a
// GraphicalMode arm is gone -- B534 deleted the whole route.)
// getCameraTransform() was gone from that header for the same reason and T334
// (B045) deleted it outright: the audio listener it was written for has had a
// producer since D20c2, and that producer uses the PLAYER position by design
// (BridgeComponent::UpdateAudioListener).
//
// The component needs friendship on BOTH axes of K13 correction (ii): it
// reaches 20 private IrrlichtRenderer members through owner_, and it names the
// private nested type IrrlichtRenderer::DoorCollisionData.

#include "client/graphics/renderer_context.h"
#include "client/graphics/player/ground_query.h"  // T437 (B435): the ground answer shape
#include "client/graphics/player/camera_rig.h"    // T461: the orbit rig
#include "client/graphics/player/mouse_options.h" // B433: the Options Mouse page

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
	// T437 (B435/B436): returns a GroundZ, not a float -- "no floor here"
	// is a distinct answer that no landing or clamp can mistake for ground.
	GroundZ findGroundZ(float x, float y, float currentZ);
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
	// B909: the same test over one region of RegionWorldTriangles.
	static bool rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
	                                  const irr::core::vector3df& rayDir, float rayLen,
	                                  const RegionTrianglesView& triangles,
	                                  irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
	                                  float& hitDistSq);
	// Extract world-space triangles from a mesh node (called once at build time)
	static std::vector<irr::core::triangle3df> extractWorldTriangles(
		irr::scene::ISceneManager* smgr, irr::scene::IMeshSceneNode* node);
	// T435 (board B425): the writer ctx_.doorCollisionData never had. Drains
	// DoorManager's changed/removed ids and re-bakes their world-space
	// triangles. Called once per frame after DoorManager::update().
	void syncDoorCollision();

	// T438 (board B423): carry the player standing on a lift that moved
	// this frame. Called right after syncDoorCollision(); defined in
	// src/client/graphics/player/renderer_player_lift.cpp.
	void carryPlayerOnLifts(float deltaTime);
	// B892 (GitLab #208): the per-frame levitation hover clamp, called by
	// FrameComponent after the lift carry (renderer_frame_input.cpp), once
	// per frame. A levitating player's feet rest kLevitationHoverHeight
	// above the floor; from height they descend kLevitationDescentRate
	// units/s instead of free-falling. Both numbers live in
	// client/spell/levitation.h ([assumed] until the lane-1 capture lands).
	// A no-op not levitating, in water or mid-jump (those own the z), and
	// without collision geometry.
	void applyLevitationClamp(float deltaTime);
	void drawCollisionDebugLines(float deltaTime);

	// T354 (B042/B047 batch): the incremental collision-registration API
	// (addRegionToCollision, addDoorToCollision, addObjectToCollision,
	// findBspRegionForPoint, clearCollisionDebugLines) is GONE. L05 measured
	// zero callers tree-wide and kept the five so the decision could be taken
	// with evidence (B047); T354 re-measured zero and deleted them. Collision
	// is built ONCE, in setupMinimalZoneCollision() at load step 9 -- the
	// "progressive" comment in renderer_zone_draw.cpp that motivated keeping
	// them was false and is now corrected. Nothing streamed in after that step
	// registers collision; that gap is real and stays on the board.


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
	// setSwimmingState, setVisionType and (B912) setPlayerGameDrivenMoving ALSO
	// survive as one-line forwarders on
	// IrrlichtRenderer, because irrlicht_bridge.cpp, application_zone_loading.cpp
	// and two tests call them on an IrrlichtRenderer receiver and must not learn
	// a component exists (the K11 rule). Each forwarder names its caller in
	// irrlicht_renderer.h. cycleCameraMode() is NOT one of them: L05 measured
	// zero callers on an IrrlichtRenderer receiver (B534 removed the output-
	// layer route that reached IOutputRenderer's empty default body; the F9
	// cycle and /camera cycle reach the component directly -- T461, T464).
	void setPlayerSpawnId(uint16_t spawnId);
	void setPlayerRace(uint16_t raceId);
	void setVisionType(VisionType vision);

	// T361 (B067): re-runs the day/night lighting so a vision change reaches
	// the global ambient immediately instead of at the next server time tick.
	void refreshVisionLighting();
	void setPlayerPosition(float x, float y, float z, float heading);
	void setSwimmingState(bool swimming, float swimSpeed = 20.0f);
	// B892 (GitLab #208): the LevitationChanged arm (irrlicht_bridge.cpp).
	// playerMovement_.isLevitating is fed here and ONLY here (B1011 removed
	// the swimming event's write): that field's original job (suppressing the idle water sink in
	// computeMovementVector) still reads it, but the HOVER it no longer does
	// -- the game thread's LevitationChanged event is the writer the hover
	// trusts, and the frame's per-frame carry (applyLevitationClamp, called
	// after the lift carry) does the physics from this flag.
	void setLevitationState(bool levitating);
	void setCameraMode(CameraMode mode);
	// T461: F9 (HotkeyAction::ToggleCameraMode, drained in
	// FrameComponent::processCommonInput) and `/camera cycle`. The order is
	// nextCameraMode()'s, eqstr_us.txt 9500..9505.
	void cycleCameraMode();
	// T464 (board B532): THE zone-in camera step -- both of Application's
	// zone-graphics loaders call it (application_zone_loading.cpp, step 7).
	void placeCameraForZoneIn(float x, float y, float z, float heading512);
	// The official display name (camera_mode.h), e.g. "Chase Camera".
	std::string getCameraModeString() const;

	// --- T461: the official camera set's rig modes and `/camera` ---
	// Bodies in src/client/graphics/player/renderer_camera_modes.cpp.
	//
	// Overhead, Chase (since T474), User Defined 1/2 and Tether are each a
	// CameraRig (camera_rig.h) owned HERE, so nothing that moves the player can
	// overwrite them: setPlayerPosition() and the movement integrator only
	// move the player the rig is expressed around.
	//
	// Re-place the rig camera around the player's current pose. A no-op in
	// first person (its own formula places it) and without a camera controller
	// (a device-less renderer). FrameComponent calls it every frame, after
	// movement and again after the lift carry. T474 (board B650): Chase is
	// T466's oracle fit, scaled by the player's eye height
	// (scaleChaseRigForEye()) and placed through
	// CameraController::placeChaseCamera(), which keeps T464's eye-region
	// ceiling clamp on top of the rig wall pull-in.
	void updateRigCamera();
	// One zoom step (EQ units, + = farther): the active rig's distance --
	// Chase's included since T474. First person does not zoom.
	void zoomCamera(float deltaUnits, float deltaTime);

	// B433: the Options window's Mouse page (mouse_options.h). Set by
	// FrameComponent::applyOptionsSetting(); read by the mouse-look phase
	// of updatePlayerMovement() and by the wheel's zoom gate.
	void setMouseOptions(const MouseOptions& o) { mouseOptions_ = o; }
	const MouseOptions& mouseOptions() const { return mouseOptions_; }
	// Adds to the ACTIVE USER camera only; a no-op in any other mode. The
	// Pitch up / down keys (HotkeyAction::CameraPitchUp/Down) reach it with a
	// pitch delta -- Pitch up is a NEGATIVE delta, which lowers the camera and
	// tilts the view up, as the official key does (T466, checked by T471).
	void orbitCamera(float deltaAzimuthDeg, float deltaPitchDeg);
	// T471: the official "Tether-slide camera right/left" (LEFT_CAM/RIGHT_CAM,
	// HotkeyAction::CameraOrbitRight/Left): slides the TETHER camera by
	// `rightDeg` of world azimuth towards the viewer's right (negative = left).
	// A no-op in any other mode -- T466 pressed it in User 1 and nothing moved.
	void tetherSlideCamera(float rightDeg);
	// T471: the official "Raise camera" / "Lower camera"
	// (HotkeyAction::CameraRaise/Lower): the Tether camera's look-at height, EQ
	// units. A no-op in any other mode (T466: Raise in User 1 moved nothing).
	// What the official key moves, and by how much, is ORACLE-OWED (B653).
	void raiseCamera(float deltaUnits);
	// `/camera [...]` -- parse (slash/camera_command.h) and execute; returns
	// the chat reply lines.
	std::vector<std::string> runCameraCommand(const std::string& args);
	// `/camera get` -- mode, the four rig values, player pose, the ideal rig
	// pose and the camera node's actual pose, as parseable [CAMERA-GET] lines.
	std::vector<std::string> buildCameraGetLines() const;
	// The rig behind a rig mode; nullptr for first person. For Chase it is the
	// UNSCALED fit (the fitted character's numbers); updateRigCamera() applies
	// the eye-height scale.
	const CameraRig* cameraRigFor(CameraMode mode) const;
	// Persistence (FrameComponent::syncUiPreferences): the two user cameras,
	// the only rigs the official client persists (eqclient.ini Camera1-*/2-*).
	void setUserCameraRigs(const CameraRig& user1, const CameraRig& user2);
	// T486 (item 7): part of T467's identity reset (FrameComponent::
	// syncUiPreferences()). The Chase race scale caches the player's measured
	// eye height (chaseEyeAboveFeet_) and is refreshed only once the player
	// model is built and standing, so a change of character would place its
	// first Chase frames with the previous character's eye. Back to the fitted
	// character's value; the new model overwrites it as before.
	void resetChaseEyeScale();
	// The player's feet: node origin minus the player's collisionZOffset.
	float playerFeetZ() const;

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
	// was the same shape, duplicated by setSwimmingState()'s then-levitating
	// parameter (B1011 removed that parameter too). None had a pushEvent.
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

	// B874: pose-log public accessors for PlayerMovementState fields.
	bool isJumping() const { return playerMovement_.isJumping; }
	// B912 (GitLab #231): the player is moving if the KEYBOARD is moving it
	// (the per-frame input poll's flags, unchanged) OR the GAME thread is
	// walking it (/move, /moveto, /follow -- setPlayerGameDrivenMoving()).
	bool isMoving() const {
		return playerMovement_.moveForward || playerMovement_.moveBackward ||
			playerMovement_.strafeLeft || playerMovement_.strafeRight ||
			playerMovement_.turnLeft || playerMovement_.turnRight ||
			gameDrivenMoving_;
	}
	// B912: written ONLY by the renderer-bridge PlayerMoved arm
	// (IrrlichtBridge -> IrrlichtRenderer forwarder) from
	// PlayerMovedData::isMoving, and latched until the next PlayerMoved: the
	// game thread publishes true on every integrator tick of a command walk and
	// false when StopMovement() ends it; every discrete relocation publishes
	// false. It moves nothing -- the pose itself arrives via setPlayerPosition().
	void setPlayerGameDrivenMoving(bool moving) { gameDrivenMoving_ = moving; }

	void resetVisionToBase();
	VisionType getBaseVision() const { return baseVision_; }
	// B448 (#29): the race setPlayerRace() last applied (0 = never told).
	// IrrlichtRenderer::updatePlayerAppearance() compares against it so only a
	// real race change on self (OP_Illusion / `#race N`) moves the floor, not
	// every OP_WearChange re-publish of the same race.
	uint16_t playerRace() const { return playerRace_; }

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
	void resolveIrrlichtClearPath(MovementFrame& frame);
	void resolveIrrlichtBlockedPath(MovementFrame& frame, float checkHeight,
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
	// Find ground Z at position. currentZ is the node origin (server Z);
	// modelYOffset is the origin-to-feet distance.
	// T440 (B438) deleted the findGroundZIrrlicht() companion: it was a warn-and-
	// return-your-own-feet stub whose five call sites were all unreachable.
	GroundZ findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset);
	void addCollisionDebugLine(const irr::core::vector3df& start, const irr::core::vector3df& end,
	                           const irr::video::SColor& color, float duration = 0.5f);

	// T461: the mutable form of cameraRigFor().
	CameraRig* rigFor(CameraMode mode);
	// T474: the player's eye above its own feet, the Chase race scale's input
	// (camera_rig.h scaleChaseRigForEye()). Taken from the head-bone eye
	// EntityRenderer derives per race (T301/T431) -- only while the player is
	// STANDING, so a sit does not rescale the camera (an unmeasured posture
	// case, board B681) -- and cached in chaseEyeAboveFeet_. Without a player
	// model it is the fitted character's, so the fit is placed exactly.
	float chaseEyeAboveFeet();

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's five single-family members. Two of them
	// (playerMovement_, baseVision_) are invisible to any "count
	// IrrlichtRenderer:: bodies" measurement -- their only other users were
	// inline accessors in irrlicht_renderer.h, which moved with them
	// (K04 correction f / L03 correction 2, now the third L unit in a row).
	PlayerMovementState playerMovement_;
	float playerPitch_ = 0;  // Vertical look angle in degrees (-89 to 89)
	MouseOptions mouseOptions_;  // B433
	VisionType baseVision_ = VisionType::Normal;    // Base vision from race
	// B448 (#29): the floor and the buffs are two inputs, so a race change
	// recomposes current = max(base, buff) instead of resetting to the base and
	// wiping a vision buff that is still up. buffVision_ is the last absolute
	// VisionChanged value (setVisionType); playerRace_ the last race applied.
	VisionType buffVision_ = VisionType::Normal;
	uint16_t playerRace_ = 0;
	bool useIrrlichtCollision_ = true;  // Use zone mesh for collision instead of HCMap
	// T332: set by setPlayerPosition() (the only external writer of
	// ctx_.playerX/Y/Z), cleared by updatePlayerMovement() the moment the
	// integrator writes them itself. See playerPositionIsSeed() above.
	// T438: throttle for the rider's own position sync (the movement path
	// only fires on a frame with movement INPUT, and a rider stands still).
	float liftRideSyncAccum_ = 0.0f;
	// B892 (GitLab #208): the throttled server sync inside
	// applyLevitationClamp() (same ~250 ms rhythm as the lift ride's).
	float levitationSyncAccum_ = 0.0f;

	bool playerPositionIsSeed_ = false;
	// B912: see setPlayerGameDrivenMoving().
	bool gameDrivenMoving_ = false;

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
	CameraMode cameraMode_ = kDefaultCameraMode;  // T471: First Person, the official login camera
	// T461: one rig per rig mode. User 1/2 are persisted. T466 measured Overhead
	// against the official client; Tether is still a placeholder (B530). User
	// 1/2 and Tether hold a WORLD azimuth since T471 (B580).
	CameraRig overheadRig_ = kOverheadRig;
	// T474 (board B650): T466 measured Chase too; applied as a rig. Not
	// persisted -- the official client persists only the two user cameras.
	CameraRig chaseRig_ = kChaseRig;
	float chaseEyeAboveFeet_ = kChaseFitEyeAboveFeet;  // see chaseEyeAboveFeet()
	CameraRig user1Rig_ = kUser1DefaultRig;
	CameraRig user2Rig_ = kUser2DefaultRig;
	CameraRig tetherRig_ = kTetherPlaceholderRig;
	PlayerModeConfig playerConfig_;
	uint16_t playerSpawnId_ = 0;  // Spawn ID of the player entity
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PLAYER_PLAYER_CAMERA_COMPONENT_H
