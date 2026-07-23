#include "client/graphics/irrlicht_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/constrained_mesh_cache.h"
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

void IrrlichtRenderer::updatePlayerMovement(float deltaTime) {
    // Check if chat has focus - skip movement keys if so
    bool chatHasFocus = chatInputFocused_;

    // Read keyboard state for EQ-style movement (disabled when chat has focus)
    // Use HotkeyManager to check configured movement bindings
    auto& hotkeyMgr = eqt::input::HotkeyManager::instance();

    // Helper to check if any binding for an action is currently held
    auto isActionHeld = [&](eqt::input::HotkeyAction action) -> bool {
        if (chatHasFocus) return false;
        bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
        bool shiftHeld = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) || eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
        bool altHeld = eventReceiver_->isKeyDown(irr::KEY_LMENU) || eventReceiver_->isKeyDown(irr::KEY_RMENU);

        for (const auto& binding : hotkeyMgr.getBindingsForAction(action)) {
            if (!eventReceiver_->isKeyDown(binding.keyCode)) continue;

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
    bool turnLeft = isActionHeld(eqt::input::HotkeyAction::TurnLeft);
    bool turnRight = isActionHeld(eqt::input::HotkeyAction::TurnRight);
    bool strafeLeft = isActionHeld(eqt::input::HotkeyAction::StrafeLeft);
    bool strafeRight = isActionHeld(eqt::input::HotkeyAction::StrafeRight);
    bool jumpPressed = isActionHeld(eqt::input::HotkeyAction::Jump);
    bool swimUpPressed = isActionHeld(eqt::input::HotkeyAction::SwimUp);
    bool swimDownPressed = isActionHeld(eqt::input::HotkeyAction::SwimDown);

    // Update swimming input state
    playerMovement_.swimUp = swimUpPressed;
    playerMovement_.swimDown = swimDownPressed;

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

    // Get current heading (EQ format: 0-512)
    float heading = playerHeading_;
    float oldHeading = heading;

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
    bool ctrlHeld = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) || eventReceiver_->isKeyDown(irr::KEY_RCONTROL);
    bool mouseLookActive = (eventReceiver_->isLeftButtonDown() ||
                            eventReceiver_->isRightButtonDown() ||
                            (ctrlHeld && eventReceiver_->isLeftButtonDown()));

    if (mouseLookActive) {
        int mouseDeltaX = eventReceiver_->getMouseDeltaX();
        int mouseDeltaY = eventReceiver_->getMouseDeltaY();
        // Convert mouse delta X to heading change (sensitivity adjusted)
        heading += mouseDeltaX * 0.5f;
        // Mouse delta Y controls pitch (vertical look) - inverted so dragging up looks up
        playerPitch_ -= mouseDeltaY * 0.3f;
        // Clamp pitch to prevent camera flipping
        if (playerPitch_ > 89.0f) playerPitch_ = 89.0f;
        if (playerPitch_ < -89.0f) playerPitch_ = -89.0f;
    }

    // Normalize heading to 0-512
    while (heading < 0.0f) heading += 512.0f;
    while (heading >= 512.0f) heading -= 512.0f;

    // Convert heading to radians for movement calculation
    float headingRad = heading / 512.0f * 2.0f * static_cast<float>(M_PI);

    // Calculate movement vector
    float moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;

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

    // Calculate proposed new position
    float newX = playerX_ + moveX * deltaTime;
    float newY = playerY_ + moveY * deltaTime;
    float newZ = playerZ_ + moveZ * deltaTime;

    // Apply jump/fall physics (only when not swimming)
    if (!playerMovement_.isSwimming && playerMovement_.isJumping) {
        // Apply gravity to vertical velocity
        playerMovement_.verticalVelocity -= playerMovement_.gravity * deltaTime;

        // Update Z position based on vertical velocity
        newZ += playerMovement_.verticalVelocity * deltaTime;

        LOG_TRACE(MOD_MOVEMENT, "Jump velocity={}, newZ={}", playerMovement_.verticalVelocity, newZ);
    }

    // Apply collision detection if available and we're actually moving
    bool positionChanged = false;
    bool isMoving = (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);
    bool isJumpMoving = playerMovement_.isJumping && !playerMovement_.isSwimming;
    bool isSwimMoving = playerMovement_.isSwimming && (moveX != 0.0f || moveY != 0.0f || moveZ != 0.0f);

    if (isMoving || isJumpMoving || isSwimMoving) {
        // Debug: show movement attempt
        LOG_TRACE(MOD_MOVEMENT, "Attempting move: delta=({}, {}, {}) from ({}, {}, {}){}{}",
                  moveX * deltaTime, moveY * deltaTime, moveZ * deltaTime, playerX_, playerY_, playerZ_,
                  (isJumpMoving ? " [JUMPING]" : ""),
                  (isSwimMoving ? " [SWIMMING]" : ""));
        LOG_TRACE(MOD_MOVEMENT, "Collision: {}, Irrlicht: {}, Map: {}",
                  (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"),
                  (useIrrlichtCollision_ && !regionWorldTriangles_.empty() ? "YES" : "NO"),
                  (collisionMap_ ? "LOADED" : "NONE"));

        // Determine which collision system to use
        // BSP-filtered uses regionWorldTriangles_ directly, no meta selector needed
        bool useIrrlicht = useIrrlichtCollision_ && !regionWorldTriangles_.empty();
        bool useHCMap = collisionMap_ != nullptr;
        bool hasCollision = playerConfig_.collisionEnabled && (useIrrlicht || useHCMap);

        // Get the player's collision Z offset for ground snapping
        // Server Z represents the CENTER of the model, so when snapping to ground,
        // we need to offset by -collisionZOffset to place feet at ground level
        float modelYOffset = 0.0f;
        if (entityRenderer_) {
            modelYOffset = entityRenderer_->getPlayerCollisionZOffset();
        }

        if (hasCollision && useIrrlicht) {
            // --- Irrlicht-based collision (using actual zone geometry) ---
            // When BSP tree is available, use BSP-filtered collision (tests only source+dest
            // region selectors + doors/objects) instead of the full ~1900-region meta selector.
            bool useBspFiltered = zoneBspTree_ && !regionWorldTriangles_.empty();

            // Check horizontal collision first
            // Convert EQ coords to Irrlicht: EQ(x,y,z) -> Irr(x,z,y)
            float checkHeight = playerConfig_.collisionCheckHeight;
            irr::core::vector3df rayStart(playerX_, playerZ_ + checkHeight, playerY_);
            irr::core::vector3df rayEnd(newX, playerZ_ + checkHeight, newY);

            irr::core::vector3df hitPoint;
            irr::core::triangle3df hitTriangle;
            bool blocked = useBspFiltered
                ? checkCollisionBspFiltered(rayStart, rayEnd, hitPoint, hitTriangle)
                : checkCollisionIrrlicht(rayStart, rayEnd, hitPoint, hitTriangle);

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
                // Path is clear, now find ground at new position
                float groundZ = useBspFiltered
                    ? findGroundZBspFiltered(newX, newY, newZ, modelYOffset)
                    : findGroundZIrrlicht(newX, newY, newZ, modelYOffset);

                LOG_TRACE(MOD_MOVEMENT, "Ground at target: {}, playerZ: {}, newZ: {}", groundZ, playerZ_, newZ);

                if (playerMovement_.isJumping) {
                    // While jumping, check if we've landed
                    // Compare feet position (centerZ - modelYOffset) with ground
                    float feetZ = newZ - modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    // Allow horizontal movement while in air
                    positionChanged = true;
                } else if (playerMovement_.isSwimming) {
                    // Swimming - no ground snap, but stop at floor/ceiling
                    // Player sinks slowly (handled by sinkRate in movement calc)

                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_) {
                        // Cast ray from current head to target head position
                        // Head is approximately modelYOffset above center (same as feet are below)
                        float headHeight = modelYOffset;
                        // Irrlicht coords: (x, z, y) where Irrlicht Y = EQ Z
                        irr::core::vector3df headStart(playerX_, playerZ_ + headHeight, playerY_);
                        irr::core::vector3df headEnd(newX, newZ + headHeight, newY);
                        irr::core::vector3df ceilingHit;
                        irr::core::triangle3df ceilingTri;

                        float deltaZ = newZ - playerZ_;
                        LOG_DEBUG(MOD_MOVEMENT, "[Swim] Ceiling check: from Z={} to Z={} (delta={}), ray ({},{},{}) -> ({},{},{})",
                            playerZ_, newZ, deltaZ, headStart.X, headStart.Y, headStart.Z, headEnd.X, headEnd.Y, headEnd.Z);

                        if ((useBspFiltered ? checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)
                                             : checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri))) {
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

                    // Check if we'd go below ground and clamp if so
                    // Note: findGroundZIrrlicht returns feetZ+1000 as a "blocked" sentinel when hitting a ceiling
                    // We need to ignore that sentinel value for floor clamping
                    float feetZ = newZ - modelYOffset;
                    float maxReasonableGround = playerZ_ + 100.0f;  // Ground can't reasonably be 100+ units above current position
                    if (groundZ < maxReasonableGround && feetZ < groundZ) {
                        // Would sink below ground - stop at ground level
                        newZ = groundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Normal ground movement - check step height
                    // Compare feet positions: current feet vs target ground
                    float currentFeetZ = playerZ_ - modelYOffset;
                    float stepHeight = groundZ - currentFeetZ;

                    // Limit both stepping UP and stepping DOWN
                    // Small steps down (e.g., stairs) are OK, but large drops should trigger falling
                    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;  // Allow stepping down ~2x step height
                    if (stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
                        // Snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        positionChanged = true;
                    } else if (stepHeight < -maxStepDown) {
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
            } else {
                // Horizontal movement blocked - try wall sliding or continue jump/swim vertically
                if (playerMovement_.isJumping) {
                    // Even if horizontal is blocked, continue vertical jump movement
                    float groundZ = useBspFiltered
                        ? findGroundZBspFiltered(playerX_, playerY_, newZ, modelYOffset)
                        : findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
                    float feetZ = newZ - modelYOffset;
                    if (feetZ <= groundZ && playerMovement_.verticalVelocity <= 0) {
                        // Landed - snap center to be modelYOffset above ground
                        newZ = groundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (blocked horizontal) at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    // Keep X/Y at current position but update Z
                    newX = playerX_;
                    newY = playerY_;
                    positionChanged = true;
                } else if (playerMovement_.isSwimming) {
                    // Horizontal blocked while swimming - allow vertical movement at current X/Y
                    newX = playerX_;
                    newY = playerY_;

                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_) {
                        float headHeight = modelYOffset;
                        irr::core::vector3df headStart(playerX_, playerZ_ + headHeight, playerY_);
                        irr::core::vector3df headEnd(playerX_, newZ + headHeight, playerY_);
                        irr::core::vector3df ceilingHit;
                        irr::core::triangle3df ceilingTri;

                        if ((useBspFiltered ? checkCollisionBspFiltered(headStart, headEnd, ceilingHit, ceilingTri)
                                             : checkCollisionIrrlicht(headStart, headEnd, ceilingHit, ceilingTri))) {
                            float maxZ = ceilingHit.Y - headHeight - 0.1f;
                            if (newZ > maxZ) {
                                newZ = maxZ;
                                if (playerConfig_.collisionDebug) {
                                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (blocked horiz) at {}, clamped to serverZ={}", ceilingHit.Y, newZ);
                                }
                            }
                        }
                    }

                    // Check floor collision
                    // Note: findGroundZ returns feetZ+1000 as a "blocked" sentinel when hitting a ceiling
                    float groundZ = useBspFiltered
                        ? findGroundZBspFiltered(playerX_, playerY_, newZ, modelYOffset)
                        : findGroundZIrrlicht(playerX_, playerY_, newZ, modelYOffset);
                    float feetZ = newZ - modelYOffset;
                    float maxReasonableGround = playerZ_ + 100.0f;
                    if (groundZ < maxReasonableGround && feetZ < groundZ) {
                        newZ = groundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (blocked horiz) at groundZ={}, serverZ={}", groundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Try wall sliding - X only
                    irr::core::vector3df rayEndX(newX, playerZ_ + checkHeight, playerY_);
                    if (!(useBspFiltered ? checkCollisionBspFiltered(rayStart, rayEndX, hitPoint, hitTriangle)
                                         : checkCollisionIrrlicht(rayStart, rayEndX, hitPoint, hitTriangle))) {
                        float groundZ = useBspFiltered
                            ? findGroundZBspFiltered(newX, playerY_, playerZ_, modelYOffset)
                            : findGroundZIrrlicht(newX, playerY_, playerZ_, modelYOffset);
                        float currentFeetZ = playerZ_ - modelYOffset;
                        float stepHeight = groundZ - currentFeetZ;
                        // Only limit stepping UP - can always step down
                        if (stepHeight <= playerConfig_.collisionStepHeight) {
                            newY = playerY_;
                            newZ = groundZ + modelYOffset;
                            positionChanged = true;
                            LOG_TRACE(MOD_MOVEMENT, "Wall slide X");
                        }
                    }
                    // Try wall sliding - Y only
                    if (!positionChanged) {
                        irr::core::vector3df rayEndY(playerX_, playerZ_ + checkHeight, newY);
                        if (!(useBspFiltered ? checkCollisionBspFiltered(rayStart, rayEndY, hitPoint, hitTriangle)
                                              : checkCollisionIrrlicht(rayStart, rayEndY, hitPoint, hitTriangle))) {
                            float groundZ = useBspFiltered
                                ? findGroundZBspFiltered(playerX_, newY, playerZ_, modelYOffset)
                                : findGroundZIrrlicht(playerX_, newY, playerZ_, modelYOffset);
                            float currentFeetZ = playerZ_ - modelYOffset;
                            float stepHeight = groundZ - currentFeetZ;
                            // Only limit stepping UP - can always step down
                            if (stepHeight <= playerConfig_.collisionStepHeight) {
                                newX = playerX_;
                                newZ = groundZ + modelYOffset;
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
        } else if (hasCollision && useHCMap) {
            // --- HCMap-based collision (server map file) ---
            float targetGroundZ = findGroundZ(newX, newY, newZ);

            LOG_TRACE(MOD_MOVEMENT, "HCMap Move from ({}, {}, {}) to ({}, {}), groundZ={}", playerX_, playerY_, playerZ_, newX, newY, targetGroundZ);

            if (playerMovement_.isJumping) {
                // Handle jumping with HCMap
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, newZ);
                if (losCheck) {
                    // Check if we've landed (compare feet position with ground)
                    float feetZ = newZ - modelYOffset;
                    if (targetGroundZ != BEST_Z_INVALID && feetZ <= targetGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = targetGroundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap) at groundZ={}, serverZ={}", targetGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Horizontal blocked but continue vertical movement
                    newX = playerX_;
                    newY = playerY_;
                    float currentGroundZ = findGroundZ(playerX_, playerY_, newZ);
                    float feetZ = newZ - modelYOffset;
                    if (currentGroundZ != BEST_Z_INVALID && feetZ <= currentGroundZ && playerMovement_.verticalVelocity <= 0) {
                        newZ = currentGroundZ + modelYOffset;
                        playerMovement_.isJumping = false;
                        playerMovement_.verticalVelocity = 0.0f;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (HCMap, blocked) at groundZ={}, serverZ={}", currentGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                }
            } else if (playerMovement_.isSwimming) {
                // Swimming with HCMap - no ground snap, but stop at floor/ceiling
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, newZ);
                if (losCheck) {
                    // Check ceiling collision when swimming up
                    if (newZ > playerZ_ && collisionMap_) {
                        // Cast ray from current head to target head position
                        float headHeight = modelYOffset;
                        glm::vec3 headStart(playerX_, playerY_, playerZ_ + headHeight);
                        glm::vec3 headEnd(newX, newY, newZ + headHeight);
                        glm::vec3 ceilingHit;

                        if (!collisionMap_->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
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
                    if (targetGroundZ != BEST_Z_INVALID && feetZ < targetGroundZ) {
                        // Would sink below ground - stop at ground level
                        newZ = targetGroundZ + modelYOffset;
                        if (playerConfig_.collisionDebug) {
                            LOG_INFO(MOD_GRAPHICS, "[Swim] Hit bottom (HCMap) at groundZ={}, serverZ={}", targetGroundZ, newZ);
                        }
                    }
                    positionChanged = true;
                } else {
                    // Horizontal blocked, allow vertical movement at current X/Y
                    newX = playerX_;
                    newY = playerY_;

                    // Check ceiling collision for vertical-only movement when swimming up
                    if (newZ > playerZ_ && collisionMap_) {
                        float headHeight = modelYOffset;
                        glm::vec3 headStart(playerX_, playerY_, playerZ_ + headHeight);
                        glm::vec3 headEnd(playerX_, playerY_, newZ + headHeight);
                        glm::vec3 ceilingHit;

                        if (!collisionMap_->CheckLOSWithHit(headStart, headEnd, &ceilingHit)) {
                            float maxZ = ceilingHit.z - headHeight - 0.1f;
                            if (newZ > maxZ) {
                                newZ = maxZ;
                                if (playerConfig_.collisionDebug) {
                                    LOG_INFO(MOD_GRAPHICS, "[Swim] Hit ceiling (HCMap, blocked horiz) at {}, clamped to serverZ={}", ceilingHit.z, newZ);
                                }
                            }
                        }
                    }

                    float currentGroundZ = findGroundZ(playerX_, playerY_, newZ);
                    float feetZ = newZ - modelYOffset;
                    if (currentGroundZ != BEST_Z_INVALID && feetZ < currentGroundZ) {
                        newZ = currentGroundZ + modelYOffset;
                    }
                    positionChanged = true;
                }
            } else if (targetGroundZ != BEST_Z_INVALID) {
                // Compare feet positions for step height
                float currentFeetZ = playerZ_ - modelYOffset;
                float stepHeight = targetGroundZ - currentFeetZ;

                LOG_TRACE(MOD_MOVEMENT, "HCMap Step height: {} (max up: {})", stepHeight, playerConfig_.collisionStepHeight);

                // Limit both stepping UP and stepping DOWN
                float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;
                if (stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {
                    bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, targetGroundZ);

                    LOG_TRACE(MOD_MOVEMENT, "HCMap LOS check: {}", (losCheck ? "CLEAR" : "BLOCKED"));

                    if (losCheck) {
                        newZ = targetGroundZ + modelYOffset;
                        positionChanged = true;
                    } else {
                        // Wall sliding for HCMap
                        float xGroundZ = findGroundZ(newX, playerY_, playerZ_);
                        float xStepHeight = xGroundZ - currentFeetZ;
                        if (xGroundZ != BEST_Z_INVALID &&
                            xStepHeight <= playerConfig_.collisionStepHeight && xStepHeight >= -maxStepDown &&
                            checkMovementCollision(playerX_, playerY_, playerZ_, newX, playerY_, xGroundZ)) {
                            newY = playerY_;
                            newZ = xGroundZ + modelYOffset;
                            positionChanged = true;
                        } else {
                            float yGroundZ = findGroundZ(playerX_, newY, playerZ_);
                            float yStepHeight = yGroundZ - currentFeetZ;
                            if (yGroundZ != BEST_Z_INVALID &&
                                yStepHeight <= playerConfig_.collisionStepHeight && yStepHeight >= -maxStepDown &&
                                checkMovementCollision(playerX_, playerY_, playerZ_, playerX_, newY, yGroundZ)) {
                                newX = playerX_;
                                newZ = yGroundZ + modelYOffset;
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
                bool losCheck = checkMovementCollision(playerX_, playerY_, playerZ_, newX, newY, playerZ_);
                if (losCheck) {
                    positionChanged = true;
                }
            }
        } else {
            // No collision map or collision disabled - allow all movement
            positionChanged = true;

            // Handle jump landing without collision detection
            // Use a simple ground plane at the starting Z or a minimum height
            if (playerMovement_.isJumping && playerMovement_.verticalVelocity <= 0) {
                // Without collision, assume flat ground - land when we return to starting height or below
                // Use a minimum ground level to prevent falling forever
                float minGroundZ = -1000.0f;  // Reasonable minimum
                if (newZ <= playerZ_ || newZ <= minGroundZ) {
                    // Keep the current ground level or use minimum
                    newZ = std::max(playerZ_, minGroundZ);
                    playerMovement_.isJumping = false;
                    playerMovement_.verticalVelocity = 0.0f;
                    if (playerConfig_.collisionDebug) {
                        LOG_INFO(MOD_GRAPHICS, "[Jump] Landed (no collision) at Z={}", newZ);
                    }
                }
            }

            if (playerConfig_.collisionDebug) {
                if (!collisionMap_) {
                    LOG_TRACE(MOD_MOVEMENT, "No collision map - movement allowed");
                } else {
                    LOG_TRACE(MOD_MOVEMENT, "Collision DISABLED - movement allowed");
                }
                // Draw a white line showing the intended movement path (no collision check)
                irr::core::vector3df irrFrom(playerX_, playerZ_ + 3.0f, playerY_);
                irr::core::vector3df irrTo(newX, newZ + 3.0f, newY);
                addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 255), 0.2f);
            }
        }
    }

    // Check if heading changed (with small epsilon for float comparison)
    bool headingChanged = (std::abs(heading - oldHeading) > 0.001f);

    // Update position if movement occurred
    if (positionChanged) {
        playerX_ = newX;
        playerY_ = newY;
        playerZ_ = newZ;
    }

    // Always update heading if it changed (separate from position)
    if (headingChanged) {
        playerHeading_ = heading;
    }

    // Update camera and entity position if anything changed
    if (positionChanged || headingChanged) {
        LOG_TRACE(MOD_MOVEMENT, "Position updated: ({}, {}, {}) heading={}", playerX_, playerY_, playerZ_, playerHeading_);

        // Update player entity position (for third-person view)
        if (entityRenderer_) {
            entityRenderer_->updatePlayerEntityPosition(playerX_, playerY_, playerZ_, playerHeading_);
        }

        // Update camera based on camera mode
        if (camera_ && cameraMode_ == CameraMode::FirstPerson) {
            // First-person: camera at head bone position to track model animation
            float camX = playerX_;
            float camY = playerY_;
            float camZ = playerZ_ + 5.0f;  // Default fallback if no head bone

            // Try to get head bone position from the animated player model
            // This allows the camera to track head movement during animations
            bool gotHeadBone = false;
            if (entityRenderer_) {
                float headX, headY, headZ;
                if (entityRenderer_->getPlayerHeadBonePosition(headX, headY, headZ)) {
                    // Use head bone position for camera (adds head bob/sway from animation)
                    camX = headX;
                    camY = headY;
                    camZ = headZ;
                    gotHeadBone = true;
                }
            }

            // If no head bone, compute eye height from model bounding box
            // playerZ_ is feet position, so camera is at feet + eye height
            if (!gotHeadBone && entityRenderer_) {
                float eyeHeightFromFeet = entityRenderer_->getPlayerEyeHeightFromFeet();
                camZ = playerZ_ + eyeHeightFromFeet;

                // Debug logging
                static int fallbackLogCount = 0;
                if (fallbackLogCount++ % 500 == 0) {
                    LOG_DEBUG(MOD_GRAPHICS, "Camera fallback: playerZ(feet)={:.2f} + eyeHeight={:.2f} => camZ={:.2f} (before adjust)",
                              playerZ_, eyeHeightFromFeet, camZ);
                }
            }

            // Apply eye height offset (user-adjustable with Y/Shift+Y)
            camZ += playerConfig_.eyeHeight;

            // EQ coords: (x, y, z) -> Irrlicht coords: (x, z, y)
            camera_->setPosition(irr::core::vector3df(camX, camZ, camY));

            // Calculate look direction from playerHeading_ (yaw) and playerPitch_ (pitch)
            float lookRad = playerHeading_ / 512.0f * 2.0f * static_cast<float>(M_PI);
            float pitchRad = playerPitch_ * static_cast<float>(M_PI) / 180.0f;
            float cosPitch = std::cos(pitchRad);
            float sinPitch = std::sin(pitchRad);
            irr::core::vector3df target(
                camX + std::sin(lookRad) * cosPitch * 100.0f,
                camZ + sinPitch * 100.0f,
                camY + std::cos(lookRad) * cosPitch * 100.0f
            );
            camera_->setTarget(target);
        } else if (cameraMode_ == CameraMode::Follow && cameraController_) {
            // Follow mode: third-person camera behind player
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_, deltaTime);
        }
    }

    // Movement state tracking and server sync - runs every frame to detect stops
    // This must be OUTSIDE the position/heading changed block to detect when player stops
    bool hasMovementInput = playerMovement_.moveForward || playerMovement_.moveBackward ||
                            playerMovement_.strafeLeft || playerMovement_.strafeRight;

    // Track movement state transitions to detect when player stops
    static bool hadMovementInput = false;
    bool stoppedMoving = hadMovementInput && !hasMovementInput;
    hadMovementInput = hasMovementInput;

    // Track previous position for velocity calculation
    static float prevX = playerX_, prevY = playerY_, prevZ = playerZ_;

    // Throttle callback invocations to ~250ms to match working client behavior
    // This prevents jerky motion when viewed by other players
    static auto lastCallbackTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCallbackTime);

    // Notify EverQuest class of position/heading change (for server sync)
    // Throttle to 250ms minimum between callbacks (working client averages ~275ms)
    // Exception: allow immediate callback when player stops (so others see us stop promptly)
    bool shouldCallback = bridge_ &&
                          (positionChanged || headingChanged || stoppedMoving) &&
                          (stoppedMoving || elapsed.count() >= 250);

    if (shouldCallback) {
        // S2: intent-only — the former synchronous movementCallback_ ran
        // OnGraphicsMovement on the render thread (thread-unsafe) and
        // double-sent position updates alongside this intent.
        eqt::events::PlayerPositionChanged intent;
        intent.x = playerX_;
        intent.y = playerY_;
        intent.z = playerZ_;
        intent.heading = heading;  // EQ format (0-512)

        // When player stops, send zero deltas to ensure server knows we've stopped
        // This triggers anim=0 in OnGraphicsMovement
        if (stoppedMoving) {
            intent.dx = 0.0f;
            intent.dy = 0.0f;
            intent.dz = 0.0f;
        } else {
            intent.dx = playerX_ - prevX;
            intent.dy = playerY_ - prevY;
            intent.dz = playerZ_ - prevZ;
        }
        bridge_->pushIntent(std::move(intent));

        lastCallbackTime = now;

        // Update previous position only when we actually send an update
        prevX = playerX_;
        prevY = playerY_;
        prevZ = playerZ_;
    }

    // Update player entity animation based on movement state (runs every frame)
    if (entityRenderer_) {
        // Jump animation takes priority (playThrough) - but not when swimming
        if (playerMovement_.isJumping && playerMovement_.verticalVelocity > 0 && !playerMovement_.isSwimming) {
            // Only trigger jump animation on the way up (ascending)
            // Use l03 for running jump, l04 for standing jump
            if (hasMovementInput) {
                entityRenderer_->setPlayerEntityAnimation("l03", false, 0.0f, true);  // Running jump
            } else {
                entityRenderer_->setPlayerEntityAnimation("l04", false, 0.0f, true);  // Standing jump
            }
        } else if (playerMovement_.isSwimming) {
            // Swimming animations
            bool hasSwimMovement = hasMovementInput || playerMovement_.swimUp || playerMovement_.swimDown;
            if (hasSwimMovement) {
                entityRenderer_->setPlayerEntityAnimation("l09", true, playerMovement_.swimSpeed);  // Swim treading/moving
            } else {
                entityRenderer_->setPlayerEntityAnimation("p06", true);  // Swim idle
            }
        } else if (hasMovementInput) {
            // Use run animation for forward movement when running, walk for everything else
            // Pass movement speed to match animation speed to actual movement
            float speed = playerMovement_.isRunning ? playerMovement_.runSpeed : playerMovement_.walkSpeed;
            if (playerMovement_.moveForward && playerMovement_.isRunning) {
                entityRenderer_->setPlayerEntityAnimation("l02", true, speed);  // Run
            } else {
                entityRenderer_->setPlayerEntityAnimation("l01", true, speed);  // Walk
            }
        } else {
            entityRenderer_->setPlayerEntityAnimation("p01", true);  // Idle
        }
    }
}

bool IrrlichtRenderer::checkMovementCollision(float fromX, float fromY, float fromZ,
                                               float toX, float toY, float toZ) {
    if (!collisionMap_) {
        return true;  // No collision map = allow movement
    }

    // Check line of sight from current position to new position
    // Use configurable check height for collision detection
    float checkHeight = playerConfig_.collisionCheckHeight;
    glm::vec3 from(fromX, fromY, fromZ + checkHeight);
    glm::vec3 to(toX, toY, toZ + checkHeight);

    glm::vec3 hitLocation;
    bool clear = collisionMap_->CheckLOSWithHit(from, to, &hitLocation);

    // Add debug visualization if enabled
    if (playerConfig_.collisionDebug) {
        // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
        irr::core::vector3df irrFrom(from.x, from.z, from.y);
        irr::core::vector3df irrTo(to.x, to.z, to.y);

        if (clear) {
            // Green line for clear path
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 0), 0.2f);
        } else {
            // Red line from start to hit point
            irr::core::vector3df irrHit(hitLocation.x, hitLocation.z, hitLocation.y);
            addCollisionDebugLine(irrFrom, irrHit, irr::video::SColor(255, 255, 0, 0), 0.5f);

            // Yellow sphere marker at hit point (draw as short lines forming a cross)
            float markerSize = 1.0f;
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X - markerSize, irrHit.Y, irrHit.Z),
                irr::core::vector3df(irrHit.X + markerSize, irrHit.Y, irrHit.Z),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X, irrHit.Y - markerSize, irrHit.Z),
                irr::core::vector3df(irrHit.X, irrHit.Y + markerSize, irrHit.Z),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
            addCollisionDebugLine(
                irr::core::vector3df(irrHit.X, irrHit.Y, irrHit.Z - markerSize),
                irr::core::vector3df(irrHit.X, irrHit.Y, irrHit.Z + markerSize),
                irr::video::SColor(255, 255, 255, 0), 0.5f);
        }
    }

    return clear;
}

float IrrlichtRenderer::findGroundZ(float x, float y, float currentZ) {
    if (!collisionMap_) {
        return currentZ;
    }

    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // PHASE 1: Short raycast to find ground near current level
    // This prevents falling through mesh gaps by looking for nearby ground first
    // Start from max step-up above current position
    glm::vec3 nearPos(x, y, currentZ + maxStepUp);
    glm::vec3 nearResult;
    float nearGroundZ = collisionMap_->FindBestZ(nearPos, &nearResult);

    // Check if we found ground within the nearby range (max step-up to max step-down)
    if (nearGroundZ != BEST_Z_INVALID) {
        float heightDiff = nearGroundZ - currentZ;
        if (heightDiff >= -maxStepDown && heightDiff <= maxStepUp) {
            // Found nearby ground - use it
            if (playerConfig_.collisionDebug) {
                // Green line showing nearby ground hit (preferred)
                irr::core::vector3df irrFrom(x, currentZ + maxStepUp, y);
                irr::core::vector3df irrTo(x, nearGroundZ, y);
                addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 128), 0.2f);
                float markerSize = 0.5f;
                addCollisionDebugLine(
                    irr::core::vector3df(irrTo.X - markerSize, irrTo.Y, irrTo.Z),
                    irr::core::vector3df(irrTo.X + markerSize, irrTo.Y, irrTo.Z),
                    irr::video::SColor(255, 0, 255, 128), 0.2f);
            }
            return nearGroundZ;
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    // This handles cases like jumping off ledges, falling, etc.
    glm::vec3 pos(x, y, currentZ + 10.0f);  // Start slightly above
    glm::vec3 result;
    float groundZ = collisionMap_->FindBestZ(pos, &result);

    // Add debug visualization if enabled
    if (playerConfig_.collisionDebug) {
        // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
        irr::core::vector3df irrFrom(x, currentZ + 10.0f, y);

        if (groundZ != BEST_Z_INVALID) {
            // Cyan line showing ground ray hit
            irr::core::vector3df irrTo(x, groundZ, y);
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 0, 255, 255), 0.2f);

            // Small cyan cross at ground point
            float markerSize = 0.5f;
            addCollisionDebugLine(
                irr::core::vector3df(irrTo.X - markerSize, irrTo.Y, irrTo.Z),
                irr::core::vector3df(irrTo.X + markerSize, irrTo.Y, irrTo.Z),
                irr::video::SColor(255, 0, 255, 255), 0.2f);
            addCollisionDebugLine(
                irr::core::vector3df(irrTo.X, irrTo.Y, irrTo.Z - markerSize),
                irr::core::vector3df(irrTo.X, irrTo.Y, irrTo.Z + markerSize),
                irr::video::SColor(255, 0, 255, 255), 0.2f);
        } else {
            // Magenta line showing no ground found (ray going down)
            irr::core::vector3df irrTo(x, currentZ - 50.0f, y);
            addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 0, 255), 0.2f);
        }
    }

    // Check for boat collision — only when boats exist in zone
    if (entityRenderer_ && entityRenderer_->hasBoatsInZone()) {
        float boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, currentZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (groundZ == BEST_Z_INVALID || boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    // Yellow line showing boat deck hit
                    irr::core::vector3df irrFrom(x, currentZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return boatDeckZ;
            }
        }
    }

    if (groundZ == BEST_Z_INVALID) {
        return currentZ;  // No ground found, keep current Z
    }

    return groundZ;
}

// --- Irrlicht-based Collision Detection (using zone mesh) ---

void IrrlichtRenderer::setupMinimalZoneCollision() {
    // Clean up old collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    // Legacy meta selectors (only used by HCMap fallback path now)
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    if (!smgr_) {
        return;
    }

    // Build player's BSP region mesh synchronously (only if not already loaded)
    size_t playerRegion = SIZE_MAX;
    if (zoneBspTree_) {
        playerRegion = zoneBspTree_->findRegionIndexForPoint(playerX_, playerY_, playerZ_);
        bool alreadyBuilt = regionMeshNodes_.count(playerRegion) && regionMeshNodes_[playerRegion];
        if (playerRegion != SIZE_MAX && !alreadyBuilt &&
            (!constrainedMeshCache_ || !constrainedMeshCache_->isLoaded(playerRegion))) {
            rebuildRegionMesh(playerRegion);
            LOG_INFO(MOD_GRAPHICS, "Built player BSP region {} synchronously for deferred loading", playerRegion);
        }
    }

    // Build pre-transformed world-space triangles per region for BSP-filtered collision
    // All collision paths (movement, detail, footprints, LOS) use these via BSP region lookup
    // Direct Möller–Trumbore ray-triangle intersection — no matrix math per query
    size_t regionsAdded = 0;
    size_t totalRegionTriangles = 0;
    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (!node || !node->getMesh()) continue;
        auto triangles = extractWorldTriangles(smgr_, node);
        if (!triangles.empty()) {
            totalRegionTriangles += triangles.size();
            regionsAdded++;
            regionWorldTriangles_[regionIdx] = std::move(triangles);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Collision setup: {} regions (total {} world-space triangles)",
             regionsAdded, totalRegionTriangles);

    collisionManager_ = smgr_->getSceneCollisionManager();

    // Camera collision uses BSP directly (no triangle selectors)

    // Activate progressive loading
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();

    // NOTE: EntityPrepWorker is NOT created here — it is created during the
    // P08_Entities phase (beginZoneAssetLoad) which runs after P09_Collision.
    // Creating it here would bypass the manual load gate.

    LOG_INFO(MOD_GRAPHICS, "Minimal collision setup complete, progressive loading activated");
}

size_t IrrlichtRenderer::findBspRegionForPoint(float x, float y, float z) {
    if (zoneBspTree_) {
        return zoneBspTree_->findRegionIndexForPoint(x, y, z);
    }
    return SIZE_MAX;
}

void IrrlichtRenderer::addRegionToCollision(size_t regionIdx) {
    if (!smgr_) return;

    auto regionIt = regionMeshNodes_.find(regionIdx);
    if (regionIt == regionMeshNodes_.end() || !regionIt->second || !regionIt->second->getMesh()) return;

    auto triangles = extractWorldTriangles(smgr_, regionIt->second);
    if (!triangles.empty()) {
        regionWorldTriangles_[regionIdx] = std::move(triangles);
    }
}

void IrrlichtRenderer::addDoorToCollision(uint8_t doorId) {
    if (!smgr_ || !doorManager_) return;

    const auto* door = doorManager_->getDoor(doorId);
    if (!door || !door->sceneNode || !door->sceneNode->getMesh() || !door->pivotNode) return;

    DoorCollisionData data;
    data.bspRegion = door->bspRegion;

    // Classify door type — skip collision for spinning, lifts, hazardous, invisible, teleporters
    uint8_t ot = door->opentype;
    bool isSpinning = (ot >= 30 && ot <= 44) || (ot >= 100 && ot <= 107);
    bool isLift = (ot == 59 || ot == 60);
    bool isHazardous = (ot >= 115 && ot <= 152);
    bool isInvisible = (ot == 50 || ot == 53 || ot == 54 || ot == 57);
    if (isSpinning || isLift || isHazardous || isInvisible) {
        data.hasCollision = false;
        doorCollisionData_[doorId] = std::move(data);
        LOG_DEBUG(MOD_GRAPHICS, "Door {} opentype={}: no collision ({})",
                  doorId, ot, isSpinning ? "spinning" : isLift ? "lift" : isHazardous ? "hazardous" : "invisible");
        return;
    }

    // Save current pivot rotation
    irr::core::vector3df savedRotation = door->pivotNode->getRotation();

    // Extract CLOSED state triangles
    float closedIrrRot = -door->openHeading * 360.0f / 512.0f + 90.0f;
    door->pivotNode->setRotation(irr::core::vector3df(0, closedIrrRot, 0));
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();
    data.closedTriangles = extractWorldTriangles(smgr_, door->sceneNode);

    // Extract OPEN state triangles
    float openIrrRot = -door->closedHeading * 360.0f / 512.0f + 90.0f;
    door->pivotNode->setRotation(irr::core::vector3df(0, openIrrRot, 0));
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();
    data.openTriangles = extractWorldTriangles(smgr_, door->sceneNode);

    // Restore original rotation
    door->pivotNode->setRotation(savedRotation);
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();

    LOG_DEBUG(MOD_GRAPHICS, "Door {} opentype={} region={}: {} closed tris, {} open tris",
              doorId, ot, data.bspRegion, data.closedTriangles.size(), data.openTriangles.size());

    doorCollisionData_[doorId] = std::move(data);
}

void IrrlichtRenderer::addObjectToCollision(size_t objIdx) {
    if (!smgr_) return;
    if (objIdx >= objectNodes_.size()) return;

    auto* node = objectNodes_[objIdx];
    if (!node || !node->getMesh()) return;

    auto triangles = extractWorldTriangles(smgr_, node);
    if (triangles.empty()) return;

    // Determine BSP region for this object
    size_t region = SIZE_MAX;
    if (objIdx < objectRegions_.size()) {
        region = objectRegions_[objIdx];
    }

    // Append to per-region object triangles
    auto& regionTris = objectWorldTriangles_[region];
    regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
}

bool IrrlichtRenderer::checkCollisionIrrlicht(const irr::core::vector3df& start,
                                               const irr::core::vector3df& end,
                                               irr::core::vector3df& hitPoint,
                                               irr::core::triangle3df& hitTriangle) {
    // DIAGNOSTIC: This should NOT be called on the hot path anymore.
    // BSP-filtered collision should be used instead for movement.
    LOG_WARN(MOD_GRAPHICS, "checkCollisionIrrlicht CALLED — old meta selector path! "
             "ray ({},{},{}) -> ({},{},{})",
             start.X, start.Y, start.Z, end.X, end.Y, end.Z);
    return false;
}

float IrrlichtRenderer::findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset) {
    // DIAGNOSTIC: This should NOT be called on the hot path anymore.
    // BSP-filtered collision should be used instead for movement.
    LOG_WARN(MOD_GRAPHICS, "findGroundZIrrlicht CALLED — old meta selector path! "
             "pos ({},{},{})", x, y, currentZ);
    return currentZ - modelYOffset;
}

// updateNameTagsWithLOS removed — name tag visibility now computed by SimulationWorker

// --- BSP-Filtered Collision (hot path: movement) ---
// Uses pre-transformed world-space triangles per region — no matrix math.
// Door/object selector still uses Irrlicht (few triangles, dynamic transforms).

std::vector<irr::core::triangle3df> IrrlichtRenderer::extractWorldTriangles(
    irr::scene::ISceneManager* smgr, irr::scene::IMeshSceneNode* node) {
    std::vector<irr::core::triangle3df> result;
    if (!smgr || !node || !node->getMesh()) return result;

    // Create a temporary selector to extract world-space triangles
    irr::scene::ITriangleSelector* sel = smgr->createTriangleSelector(node->getMesh(), node);
    if (!sel) return result;

    int count = sel->getTriangleCount();
    if (count > 0) {
        result.resize(count);
        irr::s32 outCount = 0;
        sel->getTriangles(result.data(), count, outCount);
        result.resize(outCount);
    }
    sel->drop();
    return result;
}

bool IrrlichtRenderer::rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                                              const irr::core::vector3df& rayDir, float rayLen,
                                              const std::vector<irr::core::triangle3df>& triangles,
                                              irr::core::vector3df& hitPoint,
                                              irr::core::triangle3df& hitTriangle,
                                              float& hitDistSq) {
    // Möller–Trumbore ray-triangle intersection — no matrix math
    constexpr float EPSILON = 0.000001f;
    bool anyHit = false;
    float closestT = rayLen + 1.0f;

    for (const auto& tri : triangles) {
        const irr::core::vector3df& v0 = tri.pointA;
        const irr::core::vector3df edge1 = tri.pointB - v0;
        const irr::core::vector3df edge2 = tri.pointC - v0;

        const irr::core::vector3df h = rayDir.crossProduct(edge2);
        float a = edge1.dotProduct(h);
        if (a > -EPSILON && a < EPSILON) continue;  // Parallel

        float f = 1.0f / a;
        const irr::core::vector3df s = rayOrigin - v0;
        float u = f * s.dotProduct(h);
        if (u < 0.0f || u > 1.0f) continue;

        const irr::core::vector3df q = s.crossProduct(edge1);
        float v = f * rayDir.dotProduct(q);
        if (v < 0.0f || u + v > 1.0f) continue;

        float t = f * edge2.dotProduct(q);
        if (t > EPSILON && t < closestT) {
            closestT = t;
            hitPoint = rayOrigin + rayDir * t;
            hitTriangle = tri;
            anyHit = true;
        }
    }

    if (anyHit) {
        hitDistSq = hitPoint.getDistanceFromSQ(rayOrigin);
    }
    return anyHit;
}

bool IrrlichtRenderer::rayTestDoorsAndObjects(const irr::core::vector3df& rayOrigin,
                                                const irr::core::vector3df& rayDir, float rayLen,
                                                size_t bspRegion,
                                                irr::core::vector3df& hitPoint,
                                                irr::core::triangle3df& hitTriangle,
                                                float& hitDistSq) {
    bool anyHit = false;

    // Test objects in this BSP region
    auto objIt = objectWorldTriangles_.find(bspRegion);
    if (objIt != objectWorldTriangles_.end()) {
        if (rayIntersectTriangles(rayOrigin, rayDir, rayLen, objIt->second, hitPoint, hitTriangle, hitDistSq)) {
            anyHit = true;
        }
    }

    // Test doors in this BSP region (skip animating doors — walk-through during animation)
    for (const auto& [doorId, doorData] : doorCollisionData_) {
        if (!doorData.hasCollision) continue;
        if (doorData.bspRegion != bspRegion) continue;

        // Get current door state from door manager
        const auto* door = doorManager_ ? doorManager_->getDoor(doorId) : nullptr;
        if (!door) continue;
        if (door->isAnimating) continue;  // Walk-through during animation (original EQ behavior)

        const auto& tris = door->isOpen ? doorData.openTriangles : doorData.closedTriangles;
        if (tris.empty()) continue;

        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayIntersectTriangles(rayOrigin, rayDir, rayLen, tris, hp, ht, distSq)) {
            if (!anyHit || distSq < hitDistSq) {
                hitPoint = hp;
                hitTriangle = ht;
                hitDistSq = distSq;
                anyHit = true;
            }
        }
    }

    return anyHit;
}

bool IrrlichtRenderer::checkCollisionBspFiltered(const irr::core::vector3df& start,
                                                  const irr::core::vector3df& end,
                                                  irr::core::vector3df& hitPoint,
                                                  irr::core::triangle3df& hitTriangle) {
    if (!zoneBspTree_) return false;

    irr::core::vector3df rayDir = end - start;
    float rayLen = rayDir.getLength();
    if (rayLen < 0.0001f) return false;
    rayDir /= rayLen;  // Normalize

    bool anyHit = false;
    float closestDistSq = std::numeric_limits<float>::max();
    irr::core::vector3df bestHit;
    irr::core::triangle3df bestTri;

    // Find source and destination BSP regions
    // Irrlicht coords (x, z, y) -> EQ coords (x, y, z): eqX=irrX, eqY=irrZ, eqZ=irrY
    size_t srcRegion = zoneBspTree_->findRegionIndexForPoint(start.X, start.Z, start.Y);
    size_t dstRegion = zoneBspTree_->findRegionIndexForPoint(end.X, end.Z, end.Y);

    // Test source region triangles (direct, no matrix math)
    if (srcRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(srcRegion);
        if (it != regionWorldTriangles_.end()) {
            irr::core::vector3df hp;
            irr::core::triangle3df ht;
            float distSq;
            if (rayIntersectTriangles(start, rayDir, rayLen, it->second, hp, ht, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hp;
                    bestTri = ht;
                    anyHit = true;
                }
            }
        }
    }

    // Test destination region triangles (if different from source)
    if (dstRegion != SIZE_MAX && dstRegion != srcRegion) {
        auto it = regionWorldTriangles_.find(dstRegion);
        if (it != regionWorldTriangles_.end()) {
            irr::core::vector3df hp;
            irr::core::triangle3df ht;
            float distSq;
            if (rayIntersectTriangles(start, rayDir, rayLen, it->second, hp, ht, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hp;
                    bestTri = ht;
                    anyHit = true;
                }
            }
        }
    }

    // Test doors and objects in source region (direct Möller–Trumbore, no matrix math)
    if (srcRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(start, rayDir, rayLen, srcRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                bestTri = ht;
                anyHit = true;
            }
        }
    }

    // Test doors and objects in destination region (if different)
    if (dstRegion != SIZE_MAX && dstRegion != srcRegion) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(start, rayDir, rayLen, dstRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                bestTri = ht;
                anyHit = true;
            }
        }
    }

    if (anyHit) {
        hitPoint = bestHit;
        hitTriangle = bestTri;
    }
    return anyHit;
}

float IrrlichtRenderer::findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset) {
    if (!zoneBspTree_) {
        return currentZ - modelYOffset;  // Return current feet position
    }

    float feetZ = currentZ - modelYOffset;
    float headZ = currentZ + modelYOffset;
    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // Find BSP region at query point
    size_t queryRegion = zoneBspTree_->findRegionIndexForPoint(x, y, currentZ);

    // PHASE 1: Short raycast near current level
    irr::core::vector3df nearStart(x, feetZ + maxStepUp, y);
    irr::core::vector3df rayDir(0.0f, -1.0f, 0.0f);  // Cast down
    float nearRayLen = maxStepUp + maxStepDown;

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    bool nearHit = false;
    float closestDistSq = std::numeric_limits<float>::max();
    irr::core::vector3df bestHit;

    // Test query region world triangles (direct, no matrix math)
    if (queryRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(queryRegion);
        if (it != regionWorldTriangles_.end()) {
            float distSq;
            if (rayIntersectTriangles(nearStart, rayDir, nearRayLen, it->second, hitPoint, hitTriangle, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hitPoint;
                    nearHit = true;
                }
            }
        }
    }

    // Test doors and objects in query region (direct Möller–Trumbore)
    if (queryRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(nearStart, rayDir, nearRayLen, queryRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                nearHit = true;
            }
        }
    }

    if (nearHit) {
        float floorZ = bestHit.Y;
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            if (playerConfig_.collisionDebug) {
                addCollisionDebugLine(nearStart, bestHit, irr::video::SColor(255, 0, 255, 128), 0.2f);
            }
            return floorZ;
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    irr::core::vector3df rayStart(x, headZ + 2.0f, y);
    irr::core::vector3df rayEnd(x, feetZ - 500.0f, y);
    float fullRayLen = (headZ + 2.0f) - (feetZ - 500.0f);

    bool hit = false;
    closestDistSq = std::numeric_limits<float>::max();
    bestHit = irr::core::vector3df();

    if (queryRegion != SIZE_MAX) {
        auto it = regionWorldTriangles_.find(queryRegion);
        if (it != regionWorldTriangles_.end()) {
            float distSq;
            if (rayIntersectTriangles(rayStart, rayDir, fullRayLen, it->second, hitPoint, hitTriangle, distSq)) {
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    bestHit = hitPoint;
                    hit = true;
                }
            }
        }
    }

    if (queryRegion != SIZE_MAX) {
        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq;
        if (rayTestDoorsAndObjects(rayStart, rayDir, fullRayLen, queryRegion, hp, ht, distSq)) {
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                bestHit = hp;
                hit = true;
            }
        }
    }

    // Debug visualization
    if (playerConfig_.collisionDebug) {
        if (hit) {
            float floorZ = bestHit.Y;
            bool validFloor = (floorZ <= feetZ + maxStepUp + 0.1f);
            if (validFloor) {
                addCollisionDebugLine(rayStart, bestHit, irr::video::SColor(255, 0, 255, 255), 0.2f);
            } else {
                addCollisionDebugLine(rayStart, bestHit, irr::video::SColor(255, 255, 165, 0), 0.2f);
                LOG_TRACE(MOD_MOVEMENT, "BSP ray hit obstruction at {} (head at {}, feet at {})", bestHit.Y, headZ, feetZ);
            }
        } else {
            addCollisionDebugLine(rayStart, rayEnd, irr::video::SColor(255, 255, 0, 255), 0.2f);
        }
    }

    float groundZ = feetZ;  // Default to current feet position
    if (hit) {
        float floorZ = bestHit.Y;
        if (floorZ <= feetZ + maxStepUp + 0.1f) {
            groundZ = floorZ;
        } else {
            return feetZ + 1000.0f;  // Ceiling sentinel
        }
    }

    // Check for boat collision — only when boats exist in zone
    if (entityRenderer_ && entityRenderer_->hasBoatsInZone()) {
        float boatDeckZ = entityRenderer_->findBoatDeckZ(x, y, feetZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    irr::core::vector3df irrFrom(x, feetZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return boatDeckZ;
            }
        }
    }

    return groundZ;
}

// --- Collision Debug Visualization ---

void IrrlichtRenderer::addCollisionDebugLine(const irr::core::vector3df& start,
                                              const irr::core::vector3df& end,
                                              const irr::video::SColor& color,
                                              float duration) {
    CollisionDebugLine line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.timeRemaining = duration;
    collisionDebugLines_.push_back(line);
}

void IrrlichtRenderer::drawCollisionDebugLines(float deltaTime) {
    if (!driver_ || collisionDebugLines_.empty()) {
        return;
    }

    // Set up material for line drawing (no lighting, no textures)
    irr::video::SMaterial lineMaterial;
    lineMaterial.Lighting = false;
    lineMaterial.Thickness = 3.0f;  // Thicker lines for visibility
    lineMaterial.AntiAliasing = irr::video::EAAM_LINE_SMOOTH;
    lineMaterial.MaterialType = irr::video::EMT_SOLID;
    lineMaterial.ZBuffer = irr::video::ECFN_ALWAYS;  // Always draw on top
    lineMaterial.ZWriteEnable = false;
    driver_->setMaterial(lineMaterial);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Draw all lines and update their timers
    auto it = collisionDebugLines_.begin();
    while (it != collisionDebugLines_.end()) {
        driver_->draw3DLine(it->start, it->end, it->color);

        // Update timer
        it->timeRemaining -= deltaTime;
        if (it->timeRemaining <= 0) {
            it = collisionDebugLines_.erase(it);
        } else {
            ++it;
        }
    }
}

void IrrlichtRenderer::clearCollisionDebugLines() {
    collisionDebugLines_.clear();
}



} // namespace Graphics
} // namespace EQT
