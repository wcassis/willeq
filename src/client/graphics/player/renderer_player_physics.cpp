// renderer_player_physics.cpp -- IrrlichtRenderer player-mode movement.
// Split from the original 1,546-line TU by refactor/J33_renderer_player_physics_split.md
// (move-only); the collision/raycast/ground-height family moved to
// renderer_player_collision.cpp.
// DO NOT RENAME THIS FILE, and DO NOT MOVE updatePlayerMovement() OR ITS PHASES
// OUT OF IT: tests/test_first_person_camera_single_formula.cpp names
// "renderer_player_physics.cpp" as a string literal and allows only this file and
// renderer_camera.cpp to read the first-person down-pitch floor accessor
// (deliberately not spelled out here: correction 40 -- a comment naming a scanned
// token can weaken the guard that scans for it). A rename or a move fails that
// test's tunable-reader assertion, and G06 widened it with a REACHABILITY clause:
// the phase that reads the floor must also be CALLED by updatePlayerMovement().
//
// G06 (refactor/G06_update_player_movement.md): updatePlayerMovement() was an
// 806-line straight-line body. Its exit census found ZERO returns/continues/breaks
// belonging to the method (all four sit inside the isActionHeld lambda), so it is a
// PIPELINE, not a search: it is now the ordered list of eight phases below, each a
// verbatim slice cut at this file's own blank-line seams. The locals that cross a
// cut -- and only those -- live in MovementFrame (player_camera_component.h); every
// phase re-binds them by reference under their original names so the sliced code is
// byte-identical. tests/test_movement_order.cpp pins the composed statement order
// against tests/goldens/movement_order_g06.txt, extracted before the split.
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/hc_map.h"
#include "client/input/hotkey_manager.h"
#include "common/logging.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace EQT {
namespace Graphics {

// --- Player Mode Movement Implementation ---

void PlayerCameraComponent::updatePlayerMovement(float deltaTime) {
    // The ordered movement phases. G06: this body is the order and nothing else.
    MovementFrame frame;
    frame.deltaTime = deltaTime;

    readMovementInput(frame);
    updateHeadingAndPitch(frame);
    computeMovementVector(frame);
    applyProposedPositionAndJumpPhysics(frame);
    resolveMovementCollision(frame);
    publishPositionAndCamera(frame);
    sendPositionUpdateToServer(frame);
    updatePlayerMovementAnimation(frame);
}

// Phase 1 -- input read (was lines 33-97).
void PlayerCameraComponent::readMovementInput(MovementFrame& frame) {
    bool& turnLeft = frame.turnLeft;
    bool& turnRight = frame.turnRight;

    // Check if chat has focus - skip movement keys if so
    bool chatHasFocus = ctx_.Frame().chatInputFocused();

    // Read keyboard state for EQ-style movement (disabled when chat has focus)
    // Use HotkeyManager to check configured movement bindings
    auto& hotkeyMgr = eqt::input::HotkeyManager::instance();

    // Helper to check if any binding for an action is currently held
    auto isActionHeld = [&](eqt::input::HotkeyAction action) -> bool {
        if (chatHasFocus) return false;
        bool ctrlHeld = ctx_.eventReceiver->isKeyDown(irr::KEY_LCONTROL) || ctx_.eventReceiver->isKeyDown(irr::KEY_RCONTROL);
        bool shiftHeld = ctx_.eventReceiver->isKeyDown(irr::KEY_LSHIFT) || ctx_.eventReceiver->isKeyDown(irr::KEY_RSHIFT);
        bool altHeld = ctx_.eventReceiver->isKeyDown(irr::KEY_LMENU) || ctx_.eventReceiver->isKeyDown(irr::KEY_RMENU);

        for (const auto& binding : hotkeyMgr.getBindingsForAction(action)) {
            if (!ctx_.eventReceiver->isKeyDown(binding.keyCode)) continue;

            bool needsCtrl = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Ctrl);
            bool needsShift = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Shift);
            bool needsAlt = eqt::input::hasModifier(binding.modifiers, eqt::input::ModifierFlags::Alt);

            if (ctrlHeld == needsCtrl && shiftHeld == needsShift && altHeld == needsAlt) {
                return true;
            }
        }
        return false;
    };

    bool forward = isActionHeld(eqt::input::HotkeyAction::MoveForward);
    bool backward = isActionHeld(eqt::input::HotkeyAction::MoveBackward);
    turnLeft = isActionHeld(eqt::input::HotkeyAction::TurnLeft);
    turnRight = isActionHeld(eqt::input::HotkeyAction::TurnRight);
    bool strafeLeft = isActionHeld(eqt::input::HotkeyAction::StrafeLeft);
    bool strafeRight = isActionHeld(eqt::input::HotkeyAction::StrafeRight);
    bool jumpPressed = isActionHeld(eqt::input::HotkeyAction::Jump);
    bool swimUpPressed = isActionHeld(eqt::input::HotkeyAction::SwimUp);
    bool swimDownPressed = isActionHeld(eqt::input::HotkeyAction::SwimDown);

    // Update swimming input state
    playerMovement_.swimUp = swimUpPressed;
    playerMovement_.swimDown = swimDownPressed;

    // T343 (B002): autorun's cancel half. The toggle arm in
    // processCommonInput() is the only other writer of this flag; here a
    // TRANSLATIONAL movement key drops it, so autorun is "forward held until
    // any movement key or the toggle again". Turning is deliberately excluded:
    // steering while autorunning is the whole point of the feature, and the
    // official client keeps running through a turn.
    if (playerMovement_.autorun && (forward || backward || strafeLeft || strafeRight)) {
        playerMovement_.autorun = false;
    }

    // Update movement state
    playerMovement_.moveForward = forward || playerMovement_.autorun;
    playerMovement_.moveBackward = backward;
    // Strafing is disabled when swimming (EQ behavior)
    playerMovement_.strafeLeft = playerMovement_.isSwimming ? false : strafeLeft;
    playerMovement_.strafeRight = playerMovement_.isSwimming ? false : strafeRight;
    playerMovement_.turnLeft = turnLeft;
    playerMovement_.turnRight = turnRight;

    // Handle jump/swim up initiation
    if (playerMovement_.isSwimming) {
        // In water: Space = swim up, no jump
        // Vertical movement is handled in the swimming movement calculation below
    } else {
        // On land: Space = jump (only when on ground)
        if (jumpPressed && !playerMovement_.isJumping) {
            playerMovement_.isJumping = true;
            playerMovement_.verticalVelocity = playerMovement_.jumpVelocity;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Jump] Started jump with velocity {}", playerMovement_.verticalVelocity);
            }
        }
    }
}

// Phase 2 -- heading, mouse-look and the stored pitch (was lines 99-158).
void PlayerCameraComponent::updateHeadingAndPitch(MovementFrame& frame) {
    const float deltaTime = frame.deltaTime;
    const bool turnLeft = frame.turnLeft;
    const bool turnRight = frame.turnRight;
    float& heading = frame.heading;
    float& oldHeading = frame.oldHeading;
    float& oldPitch = frame.oldPitch;
    float& headingRad = frame.headingRad;

    // Get current heading (EQ format: 0-512)
    heading = ctx_.playerHeading;
    oldHeading = heading;

    // Handle turning (smooth)
    if (turnLeft && !turnRight) {
        heading -= playerMovement_.turnSpeed * deltaTime * (512.0f / 360.0f);
    } else if (turnRight && !turnLeft) {
        heading += playerMovement_.turnSpeed * deltaTime * (512.0f / 360.0f);
    }

    // Mouse look - enabled when any of these conditions are met and UI doesn't have capture:
    // 1. Left mouse button held (current WillEQ behavior)
    // 2. Right mouse button held (traditional 3D game control)
    // 3. Ctrl+Left mouse button (single-button mouse workaround - Ctrl+Click = Right Click on Mac)
    bool ctrlHeld = ctx_.eventReceiver->isKeyDown(irr::KEY_LCONTROL) || ctx_.eventReceiver->isKeyDown(irr::KEY_RCONTROL);
    bool mouseLookActive = (ctx_.eventReceiver->isLeftButtonDown() ||
                            ctx_.eventReceiver->isRightButtonDown() ||
                            (ctrlHeld && ctx_.eventReceiver->isLeftButtonDown()));

    // T295: remembered so a PURE vertical mouse-look (pitch changes, heading
    // and position do not) still repositions the first-person camera --
    // the old block keyed only off position/heading and dropped it.
    oldPitch = playerPitch_;
    if (mouseLookActive) {
        int mouseDeltaX = ctx_.eventReceiver->getMouseDeltaX();
        int mouseDeltaY = ctx_.eventReceiver->getMouseDeltaY();
        // Convert mouse delta X to heading change (sensitivity adjusted)
        heading += mouseDeltaX * 0.5f;
        // Mouse delta Y controls pitch (vertical look) - inverted so dragging up looks up
        playerPitch_ -= mouseDeltaY * 0.3f;
        // Clamp pitch to prevent camera flipping.
        if (playerPitch_ > kFirstPersonMaxUpPitchDegrees) playerPitch_ = kFirstPersonMaxUpPitchDegrees;
        if (playerPitch_ < -kFirstPersonMaxUpPitchDegrees) playerPitch_ = -kFirstPersonMaxUpPitchDegrees;
        // T300: THE HIDING MECHANISM, applied on the LIVE path. This function
        // (updatePlayerMovement()) is the only writer of playerPitch_ in the
        // tree and is what runs every frame the mouse moves -- T295's finding
        // that the camera formula had a private duplicate here is exactly why
        // the clamp is applied where the value is produced, not only where it
        // is consumed. computeFirstPersonCamera() clamps the total pitch again
        // at the point of use; this one keeps the STORED value in range so the
        // control does not accumulate an invisible dead zone that has to be
        // dragged back through before the view moves again.
        //
        // Down side only. The body hangs BELOW the eye, so looking UP moves it
        // further out of frame; the +/-89 above is a gimbal guard for the
        // look-at target, not part of the hiding, and Leg A measured only the
        // down side.
        if (cameraMode_ == CameraMode::FirstPerson) {
            const float minLook = firstPersonMinLookPitchDegrees();
            if (playerPitch_ < minLook) playerPitch_ = minLook;
        }
    }

    // Normalize heading to 0-512
    while (heading < 0.0f) heading += 512.0f;
    while (heading >= 512.0f) heading -= 512.0f;

    // Convert heading to radians for movement calculation
    headingRad = heading / 512.0f * 2.0f * static_cast<float>(M_PI);
}

// Phase 3 -- the movement vector (was lines 160-214).
void PlayerCameraComponent::computeMovementVector(MovementFrame& frame) {
    const float headingRad = frame.headingRad;
    float& moveX = frame.moveX;
    float& moveY = frame.moveY;
    float& moveZ = frame.moveZ;

    // Calculate movement vector
    moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;

    if (playerMovement_.isSwimming) {
        // Swimming movement - use swim speeds, no strafing
        if (playerMovement_.moveForward) {
            float speed = playerMovement_.swimSpeed;
            moveX += std::sin(headingRad) * speed;
            moveY += std::cos(headingRad) * speed;
        }
        if (playerMovement_.moveBackward) {
            float speed = playerMovement_.swimBackwardSpeed;
            moveX -= std::sin(headingRad) * speed;
            moveY -= std::cos(headingRad) * speed;
        }
        // Strafing disabled in water (already handled above but being explicit)

        // Vertical movement in water
        if (playerMovement_.swimUp) {
            moveZ += playerMovement_.swimVerticalSpeed;
        }
        if (playerMovement_.swimDown) {
            moveZ -= playerMovement_.swimVerticalSpeed;
        }

        // Apply sinking when idle (unless swimming up or levitating)
        bool hasHorizontalMovement = playerMovement_.moveForward || playerMovement_.moveBackward;
        bool hasVerticalInput = playerMovement_.swimUp || playerMovement_.swimDown;
        if (!hasHorizontalMovement && !hasVerticalInput && !playerMovement_.isLevitating) {
            // Sink slowly when idle in water (unless levitating)
            moveZ -= playerMovement_.sinkRate;
        }
    } else {
        // Normal ground movement
        if (playerMovement_.moveForward) {
            float speed = playerMovement_.isRunning ? playerMovement_.runSpeed : playerMovement_.walkSpeed;
            moveX += std::sin(headingRad) * speed;
            moveY += std::cos(headingRad) * speed;
        }
        if (playerMovement_.moveBackward) {
            float speed = playerMovement_.backwardSpeed;
            moveX -= std::sin(headingRad) * speed;
            moveY -= std::cos(headingRad) * speed;
        }
        if (playerMovement_.strafeLeft) {
            float strafeRad = headingRad - static_cast<float>(M_PI) / 2.0f;
            moveX += std::sin(strafeRad) * playerMovement_.strafeSpeed;
            moveY += std::cos(strafeRad) * playerMovement_.strafeSpeed;
        }
        if (playerMovement_.strafeRight) {
            float strafeRad = headingRad + static_cast<float>(M_PI) / 2.0f;
            moveX += std::sin(strafeRad) * playerMovement_.strafeSpeed;
            moveY += std::cos(strafeRad) * playerMovement_.strafeSpeed;
        }
    }
}

// Phase 4 -- the proposed position and the jump/fall integration (was lines 216-230).
void PlayerCameraComponent::applyProposedPositionAndJumpPhysics(MovementFrame& frame) {
    const float deltaTime = frame.deltaTime;
    const float moveX = frame.moveX;
    const float moveY = frame.moveY;
    const float moveZ = frame.moveZ;
    float& newX = frame.newX;
    float& newY = frame.newY;
    float& newZ = frame.newZ;

    // Calculate proposed new position
    newX = ctx_.playerX + moveX * deltaTime;
    newY = ctx_.playerY + moveY * deltaTime;
    newZ = ctx_.playerZ + moveZ * deltaTime;

    // Apply jump/fall physics (only when not swimming)
    if (!playerMovement_.isSwimming && playerMovement_.isJumping) {
        // Apply gravity to vertical velocity
        playerMovement_.verticalVelocity -= playerMovement_.gravity * deltaTime;

        // Update Z position based on vertical velocity
        newZ += playerMovement_.verticalVelocity * deltaTime;

        LOG_TRACE(MOD_MOVEMENT, "Jump velocity={}, newZ={}", playerMovement_.verticalVelocity, newZ);
    }
}

// Phase 5 -- collision resolution: pick the system and dispatch (was lines 232-700).
void PlayerCameraComponent::resolveMovementCollision(MovementFrame& frame) {
    const float deltaTime = frame.deltaTime;
    const float moveX = frame.moveX;
    const float moveY = frame.moveY;
    const float moveZ = frame.moveZ;
    bool& positionChanged = frame.positionChanged;
    float& modelYOffset = frame.modelYOffset;

    // Apply collision detection if available and we're actually moving
    positionChanged = false;
    bool isMoving = (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);
    bool isJumpMoving = playerMovement_.isJumping && !playerMovement_.isSwimming;
    bool isSwimMoving = playerMovement_.isSwimming && (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);

    if (isMoving || isJumpMoving || isSwimMoving) {
        // Debug: show movement attempt
        LOG_TRACE(MOD_MOVEMENT, "Attempting move: delta=({}, {}, {}) from ({}, {}, {}){}{}",
                  moveX * deltaTime, moveY * deltaTime, moveZ * deltaTime, ctx_.playerX, ctx_.playerY, ctx_.playerZ,
                  (isJumpMoving ? " [JUMPING]" : ""),
                  (isSwimMoving ? " [SWIMMING]" : ""));
        LOG_TRACE(MOD_MOVEMENT, "Collision: {}, Irrlicht: {}, Map: {}",
                  (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"),
                  (useIrrlichtCollision_ && !ctx_.regionWorldTriangles.empty() ? "YES" : "NO"),
                  (ctx_.collisionMap ? "LOADED" : "NONE"));

        // Determine which collision system to use
        // BSP-filtered uses regionWorldTriangles_ directly, no meta selector needed
        bool useIrrlicht = useIrrlichtCollision_ && !ctx_.regionWorldTriangles.empty();
        bool useHCMap = ctx_.collisionMap != nullptr;
        bool hasCollision = playerConfig_.collisionEnabled && (useIrrlicht || useHCMap);

        // Get the player's collision Z offset for ground snapping.
        // playerZ_ is the server Z, which is where the model's node ORIGIN sits;
        // that origin is inside the body, collisionZOffset above the mesh's feet
        // (see the Z-convention note in entity_renderer.h). Snapping to ground
        // therefore means playerZ_ = groundZ + collisionZOffset.
        modelYOffset = 0.0f;
        if (ctx_.entityRenderer) {
            modelYOffset = ctx_.entityRenderer->getPlayerCollisionZOffset();
        }

        if (hasCollision && useIrrlicht) {
            resolveIrrlichtCollision(frame);
        } else if (hasCollision && useHCMap) {
            resolveHCMapCollision(frame);
        } else {
            resolveUncollidedMovement(frame);
        }
    }
}

// Phase 5a -- Irrlicht collision: the horizontal ray and its debug lines
// (was lines 266-313), then the clear/blocked split.
void PlayerCameraComponent::resolveIrrlichtCollision(MovementFrame& frame) {
    const float newX = frame.newX;
    const float newY = frame.newY;

    // --- BSP-filtered collision (using actual zone geometry) ---
    // T442 (board B451): the `useBspFiltered` selector is gone with the
    // checkCollisionIrrlicht() stub it chose against. It was
    // `ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()`, and this body
    // is entered only when ctx_.regionWorldTriangles is already non-empty --
    // so it could never be false and the Irrlicht arm never ran.

    // Check horizontal collision first
    // Convert EQ coords to Irrlicht: EQ(x,y,z) -> Irr(x,z,y)
    float checkHeight = playerConfig_.collisionCheckHeight;
    irr::core::vector3df rayStart(ctx_.playerX, ctx_.playerZ + checkHeight, ctx_.playerY);
    irr::core::vector3df rayEnd(newX, ctx_.playerZ + checkHeight, newY);

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    bool blocked = checkCollisionBspFiltered(rayStart, rayEnd, hitPoint, hitTriangle);

    if (playerConfig_.collisionDebug) {
        // Extend the ray visually so it's easier to see (movement per frame is tiny)
        irr::core::vector3df direction = rayEnd - rayStart;
        float len = direction.getLength();
        if (len > 0.001f) {
            direction.normalize();
            // Extend to at least 10 units for visibility
            irr::core::vector3df extendedEnd = rayStart + direction * std::max(len, 10.0f);

            if (blocked) {
                // Red line to hit point
                addCollisionDebugLine(rayStart, hitPoint, irr::video::SColor(255, 255, 0, 0), 0.5f);
                // Yellow cross at hit
                float ms = 2.0f;
                addCollisionDebugLine(irr::core::vector3df(hitPoint.X-ms, hitPoint.Y, hitPoint.Z),
                                      irr::core::vector3df(hitPoint.X+ms, hitPoint.Y, hitPoint.Z),
                                      irr::video::SColor(255, 255, 255, 0), 0.5f);
                addCollisionDebugLine(irr::core::vector3df(hitPoint.X, hitPoint.Y-ms, hitPoint.Z),
                                      irr::core::vector3df(hitPoint.X, hitPoint.Y+ms, hitPoint.Z),
                                      irr::video::SColor(255, 255, 255, 0), 0.5f);
                addCollisionDebugLine(irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z-ms),
                                      irr::core::vector3df(hitPoint.X, hitPoint.Y, hitPoint.Z+ms),
                                      irr::video::SColor(255, 255, 255, 0), 0.5f);
                LOG_TRACE(MOD_MOVEMENT, "Horizontal BLOCKED at ({}, {}, {})", hitPoint.X, hitPoint.Y, hitPoint.Z);
            } else {
                // Green line for clear path (extended for visibility)
                addCollisionDebugLine(rayStart, extendedEnd, irr::video::SColor(255, 0, 255, 0), 0.3f);
            }
        }
    }

    if (!blocked) {
        resolveIrrlichtClearPath(frame);
    } else {
        resolveIrrlichtBlockedPath(frame, checkHeight, rayStart, hitPoint, hitTriangle);
    }
}

// Phase 5b -- Irrlicht, path clear: ground snap / land / swim clamp (was lines 315-408).
void PlayerCameraComponent::resolveIrrlichtClearPath(MovementFrame& frame) {
    float& newX = frame.newX;
    float& newY = frame.newY;
    float& newZ = frame.newZ;
    bool& positionChanged = frame.positionChanged;
    const float modelYOffset = frame.modelYOffset;

    // Path is clear, now find ground at new position
    GroundZ groundZ = findGroundZBspFiltered(newX, newY, newZ, modelYOffset);

    LOG_TRACE(MOD_MOVEMENT, "Ground at target: {}, playerZ: {}, newZ: {}", groundZ.zOr(newZ), ctx_.playerZ, newZ);

    if (playerMovement_.isJumping) {
        // While jumping, check if we've landed
        // Compare feet position (centerZ - modelYOffset) with ground
        float feetZ = newZ - modelYOffset;
        if (ShouldLandJump(groundZ, feetZ, playerMovement_.verticalVelocity)) {
            // Landed - snap center to be modelYOffset above ground
            newZ = groundZ.z() + modelYOffset;
            playerMovement_.isJumping = false;
            playerMovement_.verticalVelocity = 0.0f;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Jump] Landed at groundZ={}, serverZ={}", groundZ.z(), newZ);
            }
        }
        // Allow horizontal movement while in air
        positionChanged = true;
    } else if (playerMovement_.isSwimming) {
        // Swimming - no ground snap, but stop at floor/ceiling
        // Player sinks slowly (handled by sinkRate in movement calc)

        // Check ceiling collision when swimming up
        if (newZ > ctx_.playerZ) {
            // Cast ray from current head to target head position
            // Head is approximately modelYOffset above center (same as feet are below)
            float headHeight = modelYOffset;
            // Irrlicht coords: (x, z, y) where Irrlicht Y = EQ Z
            irr::core::vector3df headStart(ctx_.playerX, ctx_.playerZ + headHeight, ctx_.playerY);
            irr::core::vector3df headEnd(newX, newZ + headHeight, newY);
            irr::core::vector3df ceilingHit;
            irr::core::triangle3df ceilingTri;

            float deltaZ = newZ - ctx_.playerZ;
            LOG_DEBUG(MOD_MOVEMENT, "[Swim] Ceiling check: from Z={} to Z={} (delta={}), ray ({},{},{}) -> ({},{},{})",
                ctx_.playerZ, newZ, deltaZ, headStart.X, headStart.Y, headStart.Z, headEnd.X, headEnd.Y, headEnd.Z);

            if (checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)) {
                // Hit ceiling - clamp newZ to just below the hit point
                // ceilingHit.Y is in Irrlicht coords (EQ Z)
                // Player center should be headHeight below the ceiling hit
                float maxZ = ceilingHit.Y - headHeight - 0.1f;  // Small buffer
                LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling at {}, clamping newZ from {} to {}", ceilingHit.Y, newZ, maxZ);
                if (newZ > maxZ) {
                    newZ = maxZ;
                }
            } else {
                LOG_DEBUG(MOD_MOVEMENT, "[Swim] Ceiling check: no collision detected");
            }
        }

        // Check if we'd go below ground and clamp if so.
        // T437 (B435): the `ctx_.playerZ + 100.0f` heuristic that used to guard
        // this line is GONE. It existed only to keep the `feetZ + 1000` ceiling
        // sentinel out of the clamp; a GroundZ that hit a ceiling now says
        // hasFloor() == false, so the guard has nothing left to exclude -- and
        // it also wrongly excluded a genuine floor more than 100 units up.
        float feetZ = newZ - modelYOffset;
        if (ShouldClampSwimToFloor(groundZ, feetZ)) {
            // Would sink below ground - stop at ground level
            newZ = groundZ.z() + modelYOffset;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom at groundZ={}, serverZ={}", groundZ.z(), newZ);
            }
        }
        positionChanged = true;
    } else {
        // Normal ground movement - check step height
        // Compare feet positions: current feet vs target ground
        float currentFeetZ = ctx_.playerZ - modelYOffset;
        // T437 (B435): with no floor there is nothing to step onto. Pre-T437
        // the ceiling sentinel made stepHeight ~1000, which fell through to
        // "step up too high" and blocked the move; NoFloor blocks it too, but
        // by saying so instead of by arithmetic accident.
        float stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;

        // Limit both stepping UP and stepping DOWN
        // Small steps down (e.g., stairs) are OK, but large drops should trigger falling
        float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;  // Allow stepping down ~2x step height
        if (groundZ.hasFloor() && stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
            // Snap center to be modelYOffset above ground
            newZ = groundZ.z() + modelYOffset;
            positionChanged = true;
        } else if (groundZ.hasFloor() && stepHeight < -maxStepDown) {
            // Large drop detected - start falling instead of snapping
            playerMovement_.isJumping = true;
            playerMovement_.verticalVelocity = 0.0f;  // Start with 0 velocity (just walked off edge)
            positionChanged = true;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Irrlicht] Walked off edge, drop={}, starting fall", -stepHeight);
            }
        } else if (playerConfig_.collisionDebug) {
            LOG_INFO(MOD_GRAPHICS, "[Irrlicht] Step up too high: {}", stepHeight);
        }
    }
}

// Phase 5c -- Irrlicht, horizontal blocked: wall slide / vertical-only (was lines 410-509).
void PlayerCameraComponent::resolveIrrlichtBlockedPath(
        MovementFrame& frame, float checkHeight,
        const irr::core::vector3df& rayStart,
        irr::core::vector3df& hitPoint, irr::core::triangle3df& hitTriangle) {
    float& newX = frame.newX;
    float& newY = frame.newY;
    float& newZ = frame.newZ;
    bool& positionChanged = frame.positionChanged;
    const float modelYOffset = frame.modelYOffset;

    // Horizontal movement blocked - try wall sliding or continue jump/swim vertically
    if (playerMovement_.isJumping) {
        // Even if horizontal is blocked, continue vertical jump movement
        GroundZ groundZ = findGroundZBspFiltered(ctx_.playerX, ctx_.playerY, newZ, modelYOffset);
        float feetZ = newZ - modelYOffset;
        if (ShouldLandJump(groundZ, feetZ, playerMovement_.verticalVelocity)) {
            // Landed - snap center to be modelYOffset above ground
            newZ = groundZ.z() + modelYOffset;
            playerMovement_.isJumping = false;
            playerMovement_.verticalVelocity = 0.0f;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (blocked horizontal) at groundZ={}, serverZ={}", groundZ.z(), newZ);
            }
        }
        // Keep X/Y at current position but update Z
        newX = ctx_.playerX;
        newY = ctx_.playerY;
        positionChanged = true;
    } else if (playerMovement_.isSwimming) {
        // Horizontal blocked while swimming - allow vertical movement at current X/Y
        newX = ctx_.playerX;
        newY = ctx_.playerY;

        // Check ceiling collision when swimming up
        if (newZ > ctx_.playerZ) {
            float headHeight = modelYOffset;
            irr::core::vector3df headStart(ctx_.playerX, ctx_.playerZ + headHeight, ctx_.playerY);
            irr::core::vector3df headEnd(ctx_.playerX, newZ + headHeight, ctx_.playerY);
            irr::core::vector3df ceilingHit;
            irr::core::triangle3df ceilingTri;

            if (checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)) {
                float maxZ = ceilingHit.Y - headHeight - 0.1f;
                if (newZ > maxZ) {
                    newZ = maxZ;
                    if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (blocked horiz) at {}, clamped to serverZ={}", ceilingHit.Y, newZ);
                    }
                }
            }
        }

        // Check floor collision. T437 (B435): the `+ 100.0f` heuristic is gone
        // for the same reason as its twin on the clear path.
        GroundZ groundZ = findGroundZBspFiltered(ctx_.playerX, ctx_.playerY, newZ, modelYOffset);
        float feetZ = newZ - modelYOffset;
        if (ShouldClampSwimToFloor(groundZ, feetZ)) {
            newZ = groundZ.z() + modelYOffset;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (blocked horiz) at groundZ={}, serverZ={}", groundZ.z(), newZ);
            }
        }
        positionChanged = true;
    } else {
        // Try wall sliding - X only
        irr::core::vector3df rayEndX(newX, ctx_.playerZ + checkHeight, ctx_.playerY);
        if (!checkCollisionBspFiltered(rayStart, rayEndX, hitPoint, hitTriangle)) {
            GroundZ groundZ = findGroundZBspFiltered(newX, ctx_.playerY, ctx_.playerZ, modelYOffset);
            float currentFeetZ = ctx_.playerZ - modelYOffset;
            float stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;
            // Only limit stepping UP - can always step down. T437 (B435): a
            // ceiling is no longer a candidate to slide onto.
            if (groundZ.hasFloor() && stepHeight <= playerConfig_.collisionStepHeight) {
                newY = ctx_.playerY;
                newZ = groundZ.z() + modelYOffset;
                positionChanged = true;
                LOG_TRACE(MOD_MOVEMENT, "Wall slide X");
            }
        }
        // Try wall sliding - Y only
        if (!positionChanged) {
            irr::core::vector3df rayEndY(ctx_.playerX, ctx_.playerZ + checkHeight, newY);
            if (!checkCollisionBspFiltered(rayStart, rayEndY, hitPoint, hitTriangle)) {
                GroundZ groundZ = findGroundZBspFiltered(ctx_.playerX, newY, ctx_.playerZ, modelYOffset);
                float currentFeetZ = ctx_.playerZ - modelYOffset;
                float stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;
                // Only limit stepping UP - can always step down. T437 (B435).
                if (groundZ.hasFloor() && stepHeight <= playerConfig_.collisionStepHeight) {
                    newX = ctx_.playerX;
                    newZ = groundZ.z() + modelYOffset;
                    positionChanged = true;
                    LOG_TRACE(MOD_MOVEMENT, "Wall slide Y");
                }
            }
        }
    }
    if (!positionChanged && playerConfig_.collisionDebug) {
        LOG_TRACE(MOD_MOVEMENT, "BLOCKED - no movement");
    }
}

// Phase 5d -- HCMap collision, the server map-file path (was lines 512-666).
void PlayerCameraComponent::resolveHCMapCollision(MovementFrame& frame) {
    float& newX = frame.newX;
    float& newY = frame.newY;
    float& newZ = frame.newZ;
    bool& positionChanged = frame.positionChanged;
    const float modelYOffset = frame.modelYOffset;

    // --- HCMap-based collision (server map file) ---
    GroundZ targetGroundZ = findGroundZ(newX, newY, newZ);

    LOG_TRACE(MOD_MOVEMENT, "HCMap Move from ({}, {}, {}) to ({}, {}), groundZ={}", ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, newY, targetGroundZ.zOr(newZ));

    if (playerMovement_.isJumping) {
        // Handle jumping with HCMap
        bool losCheck = checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, newY, newZ);
        if (losCheck) {
            // Check if we've landed (compare feet position with ground)
            float feetZ = newZ - modelYOffset;
            if (ShouldLandJump(targetGroundZ, feetZ, playerMovement_.verticalVelocity)) {
                newZ = targetGroundZ.z() + modelYOffset;
                playerMovement_.isJumping = false;
                playerMovement_.verticalVelocity = 0.0f;
                if (playerConfig_.collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap) at groundZ={}, serverZ={}", targetGroundZ.z(), newZ);
                }
            }
            positionChanged = true;
        } else {
            // Horizontal blocked but continue vertical movement
            newX = ctx_.playerX;
            newY = ctx_.playerY;
            GroundZ currentGroundZ = findGroundZ(ctx_.playerX, ctx_.playerY, newZ);
            float feetZ = newZ - modelYOffset;
            if (ShouldLandJump(currentGroundZ, feetZ, playerMovement_.verticalVelocity)) {
                newZ = currentGroundZ.z() + modelYOffset;
                playerMovement_.isJumping = false;
                playerMovement_.verticalVelocity = 0.0f;
                if (playerConfig_.collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap, blocked) at groundZ={}, serverZ={}", currentGroundZ.z(), newZ);
                }
            }
            positionChanged = true;
        }
    } else if (playerMovement_.isSwimming) {
        // Swimming with HCMap - no ground snap, but stop at floor/ceiling
        bool losCheck = checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, newY, newZ);
        if (losCheck) {
            // Check ceiling collision when swimming up
            if (newZ > ctx_.playerZ && ctx_.collisionMap) {
                // Cast ray from current head to target head position
                float headHeight = modelYOffset;
                glm::vec3 headStart(ctx_.playerX, ctx_.playerY, ctx_.playerZ + headHeight);
                glm::vec3 headEnd(newX, newY, newZ + headHeight);
                glm::vec3 ceilingHit;

                if (!ctx_.collisionMap->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
                    // Hit ceiling - clamp newZ to just below the hit point
                    float maxZ = ceilingHit.z - headHeight - 0.1f;  // Small buffer
                    if (newZ > maxZ) {
                        newZ = maxZ;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (HCMap) at {}, clamped to serverZ={}", ceilingHit.z, newZ);
                        }
                    }
                }
            }

            float feetZ = newZ - modelYOffset;
            if (ShouldClampSwimToFloor(targetGroundZ, feetZ)) {
                // Would sink below ground - stop at ground level
                newZ = targetGroundZ.z() + modelYOffset;
                if (playerConfig_.collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (HCMap) at groundZ={}, serverZ={}", targetGroundZ.z(), newZ);
                }
            }
            positionChanged = true;
        } else {
            // Horizontal blocked, allow vertical movement at current X/Y
            newX = ctx_.playerX;
            newY = ctx_.playerY;

            // Check ceiling collision for vertical-only movement when swimming up
            if (newZ > ctx_.playerZ && ctx_.collisionMap) {
                float headHeight = modelYOffset;
                glm::vec3 headStart(ctx_.playerX, ctx_.playerY, ctx_.playerZ + headHeight);
                glm::vec3 headEnd(ctx_.playerX, ctx_.playerY, newZ + headHeight);
                glm::vec3 ceilingHit;

                if (!ctx_.collisionMap->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
                    float maxZ = ceilingHit.z - headHeight - 0.1f;
                    if (newZ > maxZ) {
                        newZ = maxZ;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (HCMap, blocked horiz) at {}, clamped to serverZ={}", ceilingHit.z, newZ);
                        }
                    }
                }
            }

            GroundZ currentGroundZ = findGroundZ(ctx_.playerX, ctx_.playerY, newZ);
            float feetZ = newZ - modelYOffset;
            if (ShouldClampSwimToFloor(currentGroundZ, feetZ)) {
                newZ = currentGroundZ.z() + modelYOffset;
            }
            positionChanged = true;
        }
    } else if (targetGroundZ.hasFloor()) {
        // Compare feet positions for step height
        float currentFeetZ = ctx_.playerZ - modelYOffset;
        float stepHeight = targetGroundZ.z() - currentFeetZ;

        LOG_TRACE(MOD_MOVEMENT, "HCMap Step height: {} (max up: {})", stepHeight, playerConfig_.collisionStepHeight);

        // Limit both stepping UP and stepping DOWN
        float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;
        if (stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
            bool losCheck = checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, newY, targetGroundZ.z());

            LOG_TRACE(MOD_MOVEMENT, "HCMap LOS check: {}", (losCheck ? "CLEAR" : "BLOCKED"));

            if (losCheck) {
                newZ = targetGroundZ.z() + modelYOffset;
                positionChanged = true;
            } else {
                // Wall sliding for HCMap
                GroundZ xGroundZ = findGroundZ(newX, ctx_.playerY, ctx_.playerZ);
                float xStepHeight = xGroundZ.zOr(currentFeetZ) - currentFeetZ;
                if (xGroundZ.hasFloor() &&
                    xStepHeight <= playerConfig_.collisionStepHeight && xStepHeight >= -maxStepDown &&
                    checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, ctx_.playerY, xGroundZ.z())) {
                    newY = ctx_.playerY;
                    newZ = xGroundZ.z() + modelYOffset;
                    positionChanged = true;
                } else {
                    GroundZ yGroundZ = findGroundZ(ctx_.playerX, newY, ctx_.playerZ);
                    float yStepHeight = yGroundZ.zOr(currentFeetZ) - currentFeetZ;
                    if (yGroundZ.hasFloor() &&
                        yStepHeight <= playerConfig_.collisionStepHeight && yStepHeight >= -maxStepDown &&
                        checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, ctx_.playerX, newY, yGroundZ.z())) {
                        newX = ctx_.playerX;
                        newZ = yGroundZ.z() + modelYOffset;
                        positionChanged = true;
                    }
                }
            }
        } else if (stepHeight < -maxStepDown) {
            // Large drop detected - start falling instead of snapping
            playerMovement_.isJumping = true;
            playerMovement_.verticalVelocity = 0.0f;
            positionChanged = true;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[HCMap] Walked off edge, drop={}, starting fall", -stepHeight);
            }
        } else {
            LOG_TRACE(MOD_MOVEMENT, "HCMap Step up too high ({}) - blocked", stepHeight);
        }
    } else {
        bool losCheck = checkMovementCollision(ctx_.playerX, ctx_.playerY, ctx_.playerZ, newX, newY, ctx_.playerZ);
        if (losCheck) {
            positionChanged = true;
        }
    }
}

// Phase 5e -- no collision map, or collision disabled (was lines 668-698).
void PlayerCameraComponent::resolveUncollidedMovement(MovementFrame& frame) {
    float& newX = frame.newX;
    float& newY = frame.newY;
    float& newZ = frame.newZ;
    bool& positionChanged = frame.positionChanged;

    // No collision map or collision disabled - allow all movement
    positionChanged = true;

    // Handle jump landing without collision detection
    // Use a simple ground plane at the starting Z or a minimum height
    if (playerMovement_.isJumping && playerMovement_.verticalVelocity <= 0) {
        // Without collision, assume flat ground - land when we return to starting height or below
        // Use a minimum ground level to prevent falling forever
        float minGroundZ = -1000.0f;  // Reasonable minimum
        if (newZ <= ctx_.playerZ || newZ <= minGroundZ) {
            // Keep the current ground level or use minimum
            newZ = std::max(ctx_.playerZ, minGroundZ);
            playerMovement_.isJumping = false;
            playerMovement_.verticalVelocity = 0.0f;
            if (playerConfig_.collisionDebug) {
                LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (no collision) at Z={}", newZ);
            }
        }
    }

    if (playerConfig_.collisionDebug) {
        if (!ctx_.collisionMap) {
            LOG_TRACE(MOD_MOVEMENT, "No collision map - movement allowed");
        } else {
            LOG_TRACE(MOD_MOVEMENT, "Collision DISABLED - movement allowed");
        }
        // Draw a white line showing the intended movement path (no collision check)
        irr::core::vector3df irrFrom(ctx_.playerX, ctx_.playerZ + 3.0f, ctx_.playerY);
        irr::core::vector3df irrTo(newX, newZ + 3.0f, newY);
        addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 255), 0.2f);
    }
}

// Phase 6 -- publish the new pose and drive the camera (was lines 702-746).
void PlayerCameraComponent::publishPositionAndCamera(MovementFrame& frame) {
    const float deltaTime = frame.deltaTime;
    const float heading = frame.heading;
    const float oldHeading = frame.oldHeading;
    const float oldPitch = frame.oldPitch;
    const float newX = frame.newX;
    const float newY = frame.newY;
    const float newZ = frame.newZ;
    const bool positionChanged = frame.positionChanged;
    bool& headingChanged = frame.headingChanged;

    // Check if heading changed (with small epsilon for float comparison)
    headingChanged = (std::abs(heading - oldHeading) > 0.001f);

    // Update position if movement occurred
    if (positionChanged) {
        ctx_.playerX = newX;
        ctx_.playerY = newY;
        ctx_.playerZ = newZ;
        // T332 (B049): the integrator produced this one, so it really is in the
        // render frame and it is no longer an echo of what we were handed.
        playerPositionIsSeed_ = false;
    }

    // Always update heading if it changed (separate from position)
    if (headingChanged) {
        ctx_.playerHeading = heading;
    }

    bool pitchChanged = (std::abs(playerPitch_ - oldPitch) > 0.001f);

    // Update camera and entity position if anything changed
    if (positionChanged || headingChanged) {
        LOG_TRACE(MOD_MOVEMENT, "Position updated: ({}, {}, {}) heading={}", ctx_.playerX, ctx_.playerY, ctx_.playerZ, ctx_.playerHeading);

        // Update player entity position (for third-person view)
        if (ctx_.entityRenderer) {
            ctx_.entityRenderer->updatePlayerEntityPosition(ctx_.playerX, ctx_.playerY, ctx_.playerZ, ctx_.playerHeading);
        }

        // T474 (board B650): no Chase placement here any more. Chase is a rig
        // (T466's fit), and FrameComponent re-places every rig camera with
        // PlayerCameraComponent::updateRigCamera() right after this body returns.
    }

    // T295: ONE first-person camera formula, and this is NOT it -- this
    // path used to carry a fourth private copy that set the camera straight
    // from the "head bone"/bbox eye height with NEITHER T76's 0.3-unit
    // standoff NOR T75's 12-degree down-pitch, so every frame in which the
    // player moved or turned overwrote both fixes. It also read
    // getPlayerHeadBonePosition(), which since T75 did not read a head bone
    // at all. T300 replaced both tunables (the standoff is gone, the pitch
    // is 3.65 degrees) and moved the formula into
    // include/client/graphics/first_person_camera.h; the reason this path
    // must keep delegating is unchanged.
    if (cameraMode_ == CameraMode::FirstPerson && (positionChanged || headingChanged || pitchChanged)) {
        repositionFirstPersonCamera();
    }
}

// Phase 7 -- movement-state tracking and the throttled server sync (was lines 748-803).
void PlayerCameraComponent::sendPositionUpdateToServer(MovementFrame& frame) {
    const float heading = frame.heading;
    const bool positionChanged = frame.positionChanged;
    const bool headingChanged = frame.headingChanged;
    bool& hasMovementInput = frame.hasMovementInput;

    // Movement state tracking and server sync - runs every frame to detect stops
    // This must be OUTSIDE the position/heading changed block to detect when player stops
    hasMovementInput = playerMovement_.moveForward || playerMovement_.moveBackward ||
                            playerMovement_.strafeLeft || playerMovement_.strafeRight;

    // Track movement state transitions to detect when player stops
    static bool hadMovementInput = false;
    bool stoppedMoving = hadMovementInput && !hasMovementInput;
    hadMovementInput = hasMovementInput;

    // Track previous position for velocity calculation
    static float prevX = ctx_.playerX, prevY = ctx_.playerY, prevZ = ctx_.playerZ;

    // Throttle callback invocations to ~250ms to match working client behavior
    // This prevents jerky motion when viewed by other players
    static auto lastCallbackTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCallbackTime);

    // Notify EverQuest class of position/heading change (for server sync)
    // Throttle to 250ms minimum between callbacks (working client averages ~275ms)
    // Exception: allow immediate callback when player stops (so others see us stop promptly)
    bool shouldCallback = ctx_.bridge &&
                          (positionChanged || headingChanged || stoppedMoving) &&
                          (stoppedMoving || elapsed.count() >= 250);

    if (shouldCallback) {
        // S2: intent-only — the former synchronous movementCallback_ ran
        // OnGraphicsMovement on the render thread (thread-unsafe) and
        // double-sent position updates alongside this intent.
        eqt::events::PlayerPositionChanged intent;
        intent.x = ctx_.playerX;
        intent.y = ctx_.playerY;
        intent.z = ctx_.playerZ;
        intent.heading = heading;  // EQ format (0-512)
        // T332 (B049): provenance, not a value -- see renderer_intents.h.
        intent.seededPosition = playerPositionIsSeed_;

        // When player stops, send zero deltas to ensure server knows we've stopped
        // This triggers anim=0 in OnGraphicsMovement
        if (stoppedMoving) {
            intent.dx = 0.0f;
            intent.dy = 0.0f;
            intent.dz = 0.0f;
        } else {
            intent.dx = ctx_.playerX - prevX;
            intent.dy = ctx_.playerY - prevY;
            intent.dz = ctx_.playerZ - prevZ;
        }
        ctx_.bridge->pushIntent(std::move(intent));

        lastCallbackTime = now;

        // Update previous position only when we actually send an update
        prevX = ctx_.playerX;
        prevY = ctx_.playerY;
        prevZ = ctx_.playerZ;
    }
}

// Phase 8 -- the player entity's animation (was lines 805-836).
void PlayerCameraComponent::updatePlayerMovementAnimation(MovementFrame& frame) {
    const bool hasMovementInput = frame.hasMovementInput;

    // Update player entity animation based on movement state (runs every frame)
    if (ctx_.entityRenderer) {
        // Jump animation takes priority (playThrough) - but not when swimming
        if (playerMovement_.isJumping && playerMovement_.verticalVelocity > 0 && !playerMovement_.isSwimming) {
            // Only trigger jump animation on the way up (ascending)
            // Use l03 for running jump, l04 for standing jump
            if (hasMovementInput) {
                ctx_.entityRenderer->setPlayerEntityAnimation("l03", false, 0.0f, true);  // Running jump
            } else {
                ctx_.entityRenderer->setPlayerEntityAnimation("l04", false, 0.0f, true);  // Standing jump
            }
        } else if (playerMovement_.isSwimming) {
            // Swimming animations
            bool hasSwimMovement = hasMovementInput || playerMovement_.swimUp || playerMovement_.swimDown;
            if (hasSwimMovement) {
                ctx_.entityRenderer->setPlayerEntityAnimation("l09", true, playerMovement_.swimSpeed);  // Swim treading/moving
            } else {
                ctx_.entityRenderer->setPlayerEntityAnimation("p06", true);  // Swim idle
            }
        } else if (hasMovementInput) {
            // Use run animation for forward movement when running, walk for everything else
            // Pass movement speed to match animation speed to actual movement
            float speed = playerMovement_.isRunning ? playerMovement_.runSpeed : playerMovement_.walkSpeed;
            if (playerMovement_.moveForward && playerMovement_.isRunning) {
                ctx_.entityRenderer->setPlayerEntityAnimation("l02", true, speed);  // Run
            } else {
                ctx_.entityRenderer->setPlayerEntityAnimation("l01", true, speed);  // Walk
            }
        } else {
            ctx_.entityRenderer->setPlayerEntityAnimation("p01", true);  // Idle
        }
    }
}

} // namespace Graphics
} // namespace EQT
