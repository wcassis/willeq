#include "client/graphics/entity_renderer.h"
#include "client/graphics/corpse_fade.h"
#include "client/graphics/spell_cast_animation.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

bool EntityRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                   float x, float y, float z, float heading, bool isPlayer,
                                   uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                   bool isCorpse, float serverSize) {
    if (!registerEntity(spawnId, raceId, name, x, y, z, heading, isPlayer,
                        gender, appearance, isNPC, isCorpse, serverSize)) {
        return false;
    }
    return buildEntityMesh(spawnId);
}

bool EntityRenderer::isEntityMeshBuilt(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    return it != entities_.end() && it->second.meshBuilt;
}

void EntityRenderer::getUnbuiltEntities(std::vector<uint16_t>& out) const {
    out.clear();
    for (const auto& [id, visual] : entities_) {
        if (!visual.meshBuilt) {
            out.push_back(id);
        }
    }
}

void EntityRenderer::updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                                   float dx, float dy, float dz, int32_t animation) {
    // Queue the update for batched processing
    // Using map automatically deduplicates - only keeps the latest update per entity
    pendingUpdates_[spawnId] = {spawnId, x, y, z, heading, dx, dy, dz, animation};
}

void EntityRenderer::flushPendingUpdates() {
    if (pendingUpdates_.empty()) {
        return;
    }

    // Debug: Check if target entity has an update
    if (debugTargetId_ != 0 && pendingUpdates_.count(debugTargetId_) > 0) {
        LOG_TRACE(MOD_ENTITY, "TARGET ANIM BATCH: processing update for target");
    }

    // Process all pending updates (one per entity due to deduplication)
    for (const auto& [spawnId, update] : pendingUpdates_) {
        processUpdate(update);
    }

    pendingUpdates_.clear();
}

std::vector<DrainedEntityUpdate> EntityRenderer::drainPendingUpdates() {
    std::vector<DrainedEntityUpdate> result;
    if (pendingUpdates_.empty()) return result;

    result.reserve(pendingUpdates_.size());
    for (const auto& [spawnId, pu] : pendingUpdates_) {
        DrainedEntityUpdate epu;
        epu.spawnId = pu.spawnId;
        epu.x = pu.x;
        epu.y = pu.y;
        epu.z = pu.z;
        epu.heading = pu.heading;
        epu.dx = pu.dx;
        epu.dy = pu.dy;
        epu.dz = pu.dz;
        epu.animation = pu.animation;
        result.push_back(epu);
    }
    // Don't clear yet — flushPendingUpdatesForAnimations will process animations
    return result;
}

void EntityRenderer::flushPendingUpdatesForAnimations() {
    if (pendingUpdates_.empty()) return;

    // Process only animation and spatial-grid side effects.
    // Position/velocity math is handled by the worker.
    for (const auto& [spawnId, update] : pendingUpdates_) {
        processUpdate(update);
    }

    pendingUpdates_.clear();
}

void EntityRenderer::updateMainThreadEntityState(float deltaTime) {
    // Handle things that must stay on main thread:
    // - Corpse fading (material mutations)
    // - Corpse animation timing
    // - Equipment transforms for visible entities
    // - Idle animation transitions
    // - Fully-faded corpse removal

    std::vector<uint16_t> toRemove;
    std::vector<uint16_t> toDeactivate;

    // B1148: the fade list's clock runs for every listed corpse, drawn or
    // culled (the client's walk compares now with the stamped start).
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isFading) visual.fadeTimer += deltaTime;
    }

    for (uint16_t spawnId : activeEntities_) {
        auto it = entities_.find(spawnId);
        if (it == entities_.end()) continue;
        EntityVisual& visual = it->second;
        if (!visual.sceneNode || !visual.inSceneGraph) continue;

        // Player equipment transforms
        if (visual.isPlayer) {
            updateEquipmentTransforms(visual);
            continue;
        }

        // B1148 (#468): the client's corpse-fade walk (0x4012bb -> 0x401127,
        // corpse_fade.h): the model's alpha from 1.3 s, removal past 3499 ms
        // (collected below).
        if (visual.isFading) {
            const auto elapsedMs = static_cast<uint32_t>(visual.fadeTimer * 1000.0f);
            const float alpha = CorpseFadeAlpha(elapsedMs);
            if (alpha < visual.fadeAlpha) {
                visual.fadeAlpha = alpha;
                CorpseFadeMaterials mats;
                mats.solid = shaderMaterialSolid_;
                mats.alphaTest = shaderMaterialAlphaTest_;
                mats.alphaTestRGBA = shaderMaterialAlphaTestRGBA_;
                mats.fadeSolid = fadeMaterialSolid_;
                mats.fadeAlphaTest = fadeMaterialAlphaTest_;
                mats.fadeAlphaTestRGBA = fadeMaterialAlphaTestRGBA_;
                // The per-instance skinned mesh carries the alpha; a static
                // meshNode shares its buffers with every instance of the
                // model, so it stays opaque until it is removed.
                ApplyCorpseFadeAlpha(visual.animatedNode, alpha, mats);
            }
            continue;
        }

        // Corpse animation timing
        if (visual.isCorpse && !visual.corpsePositionAdjusted && visual.isAnimated && visual.animatedNode) {
            visual.corpseTime += deltaTime;
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            if (visual.corpseTime >= 0.5f) {
                if (animator.getState() == AnimationState::Stopped ||
                    (animator.getState() == AnimationState::Playing && !animator.isPlayingThrough())) {
                    visual.corpsePositionAdjusted = true;
                    visual.corpseYOffset = 0.0f;
                }
            }
        }

        // Equipment transforms for visible moving entities
        updateEquipmentTransforms(visual);
    }

    // B1065 (#385): EVERY drawn entity's held items follow its bones, moving
    // or not. activeEntities_ holds only entities that have had a velocity
    // (entity_updates.cpp processUpdate) plus the player and fading corpses,
    // so an NPC that stands at its spawn point -- a guard -- never reached the
    // loop above: its weapon and shield were never placed and drew at the
    // model origin with the identity rotation (a blade level at the hip, a
    // shield beside the body), while its body played its idle.
    // B1174 (#493): an OP_Action track (actionEntityCastAnim) that has played
    // out returns the model to the standing idle.
    for (auto& [spawnId, visual] : entities_) {
        if (visual.actionAnimCode.empty()) continue;
        if (actionCastAnimEnded(visual.animatedNode, visual.actionAnimCode)) {
            const bool tookOver = visual.animatedNode &&
                                  visual.animatedNode->getAnimator().getCurrentAnimation() != visual.actionAnimCode;
            visual.actionAnimCode.clear();
            if (tookOver || !visual.animatedNode) continue;
            const char* idle = visual.isPlayer ? "p01" : "o01";
            if (visual.animatedNode->hasAnimation(idle)) {
                visual.animatedNode->playAnimation(idle, /*loop=*/true, /*playThrough=*/false);
                visual.currentAnimation = idle;
            }
        }
    }

    for (auto& [spawnId, visual] : entities_) {
        if (!visual.inSceneGraph || (!visual.primaryEquipNode && !visual.secondaryEquipNode)) continue;
        if (activeEntities_.count(spawnId)) continue;
        updateEquipmentTransforms(visual);
    }

    // B1148: the actor is deleted once more than 3499 ms have passed (0x4012bb).
    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isFading && CorpseFadeExpired(static_cast<uint32_t>(visual.fadeTimer * 1000.0f))) {
            toRemove.push_back(spawnId);
        }
    }
    for (uint16_t spawnId : toRemove) {
        LOG_DEBUG(MOD_ENTITY, "Corpse {} off the fade list: removed", spawnId);
        removeEntity(spawnId);
    }

    // T309: the first-person draw rule. Must run every frame and BEFORE
    // smgr_->drawAll(), which is where OnRegisterSceneNode() reads the flag.
    updateFirstPersonBodyDrawRule();

    // B964: the impostor pass (entity_impostors.cpp) -- tier decision,
    // governor-scheduled captures, billboard batch. Before beginScene().
    updateImpostors();
}

void EntityRenderer::processUpdate(const PendingUpdate& update) {
    auto it = entities_.find(update.spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Log when processing the player's spawn ID
    if (update.spawnId == playerSpawnId_) {
        static int processPlayerCounter = 0;
        if (++processPlayerCounter % 30 == 0) {
            LOG_TRACE(MOD_GRAPHICS, "[PROCESS-PLAYER] processUpdate called for player spawn={} heading={:.1f}",
                     update.spawnId, update.heading);
        }
    }

    uint16_t spawnId = update.spawnId;
    float x = update.x, y = update.y, z = update.z, heading = update.heading;
    float dx = update.dx, dy = update.dy, dz = update.dz;
    int32_t animation = update.animation;

    // Check if position actually changed (ignore heading-only updates)
    float serverDeltaX = x - visual.serverX;
    float serverDeltaY = y - visual.serverY;
    float serverDeltaZ = z - visual.serverZ;
    bool positionChanged = (std::abs(serverDeltaX) > 0.01f ||
                            std::abs(serverDeltaY) > 0.01f ||
                            std::abs(serverDeltaZ) > 0.01f);

    // Invalidate cached BSP region when server position changes
    if (positionChanged) {
        visual.bspRegionDirty = true;
    }

    // For NPCs, use heading-based velocity when animation (speed) is non-zero
    // The animation field for NPCs is actually "SpeedRun" - a movement speed value
    // Server sends: anim=0 (stop), turn heading, anim=18 (start moving)
    // IMPORTANT: Trust the server's animation value directly for NPCs
    // anim=0 means stopped, anim>0 means walking/running at that speed
    // The server may send multiple packets during heading turns with the same position
    // but we should still animate based on the animation value, not position change
    bool isDebugTarget = (update.spawnId == debugTargetId_);

    // Debug: Log interpolation accuracy for targeted entity when position changes
    if (isDebugTarget && positionChanged && visual.timeSinceUpdate > 0.01f) {
        float interpErrorX = visual.lastX - x;
        float interpErrorY = visual.lastY - y;
        float interpErrorDist = std::sqrt(interpErrorX * interpErrorX + interpErrorY * interpErrorY);
        LOG_TRACE(MOD_ENTITY, "TARGET UPDATE spawn={} interpolatedPos=({},{}) serverPos=({},{}) error=({},{}) errorDist={} dt={}s",
                  spawnId, visual.lastX, visual.lastY, x, y, interpErrorX, interpErrorY, interpErrorDist, visual.timeSinceUpdate);
    }

    if (visual.isNPC) {
        if (animation != 0) {
            // NPC is moving - calculate velocity from heading and speed
            // Negative animation = moving backward (animation played in reverse)
            // Convert heading to radians: heading * π/180 (no *2, heading is full resolution from 11-bit)
            float headingRad = heading * 3.14159265f / 180.0f;

            // Speed conversion: animation value ~18 = walking speed
            // Empirically measured: anim=18 gives ~10.4 units/sec server movement
            // Multiplier: 10.4 / 18 ≈ 0.58
            // Use absolute value for speed magnitude
            float speed = static_cast<float>(std::abs(animation)) * 0.58f;

            // Calculate velocity components from heading
            // If animation is negative, reverse the direction (moving backward)
            float direction = (animation < 0) ? -1.0f : 1.0f;
            visual.velocityX = std::cos(headingRad) * speed * direction;
            visual.velocityY = std::sin(headingRad) * speed * direction;
            visual.velocityZ = 0;  // NPCs generally don't have vertical velocity while walking

            // Debug: Log velocity calculation for targeted entity
            if (isDebugTarget) {
                float predictedX = x + visual.velocityX * visual.lastUpdateInterval;
                float predictedY = y + visual.velocityY * visual.lastUpdateInterval;
                LOG_TRACE(MOD_ENTITY, "TARGET MOVE spawn={} serverPos=({},{},{}) heading={}deg anim={} vel=({},{}) predicted@{}s=({},{})",
                          update.spawnId, x, y, z, heading, animation, visual.velocityX, visual.velocityY, visual.lastUpdateInterval, predictedX, predictedY);
            }
        } else {
            // Server explicitly sent anim=0 - NPC is stopped
            visual.velocityX = 0;
            visual.velocityY = 0;
            visual.velocityZ = 0;

            // Debug: Log stop for targeted entity
            if (isDebugTarget) {
                LOG_TRACE(MOD_ENTITY, "TARGET STOP spawn={} serverPos=({},{},{}) heading={}deg anim=0 (stopped)",
                          update.spawnId, x, y, z, heading);
            }
        }
    } else {
        // For players, use server-provided delta or calculate from position change
        if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f || std::abs(dz) > 0.01f) {
            // Server provided velocity - use it directly
            visual.velocityX = dx;
            visual.velocityY = dy;
            visual.velocityZ = dz;
        } else if (positionChanged && visual.timeSinceUpdate > 0.05f) {
            // Calculate velocity from position change over time
            float invTime = 1.0f / visual.timeSinceUpdate;
            visual.velocityX = serverDeltaX * invTime;
            visual.velocityY = serverDeltaY * invTime;
            visual.velocityZ = serverDeltaZ * invTime;
        } else if (!positionChanged) {
            // Position didn't change - entity is stationary, clear velocity
            visual.velocityX = 0;
            visual.velocityY = 0;
            visual.velocityZ = 0;
        }
        // If positionChanged but timeSinceUpdate is too small, keep previous velocity
    }

    // Update server position tracking
    visual.serverX = x;
    visual.serverY = y;
    visual.serverZ = z;
    visual.serverHeading = heading;

    // Update spatial grid position if position changed significantly
    if (positionChanged) {
        updateEntityGridPosition(update.spawnId, x, y);
    }

    // Snap interpolated position to server position on update
    visual.lastX = x;
    visual.lastY = y;
    visual.lastZ = z;
    visual.lastHeading = heading;

    // Track update interval for interpolation timing
    if (visual.timeSinceUpdate > 0.05f && visual.timeSinceUpdate < 2.0f) {
        visual.lastUpdateInterval = visual.lastUpdateInterval * 0.7f + visual.timeSinceUpdate * 0.3f;
    }
    visual.timeSinceUpdate = 0;
    visual.serverAnimation = animation;

    // Performance optimization: track entities that need interpolation updates
    // Add to active set if entity has velocity (will be removed when stationary)
    if (std::abs(visual.velocityX) > 0.01f ||
        std::abs(visual.velocityY) > 0.01f ||
        std::abs(visual.velocityZ) > 0.01f) {
        activeEntities_.insert(update.spawnId);
    }

    // Track last non-zero animation (0 means "no change" from server)
    if (animation != 0) {
        visual.lastNonZeroAnimation = animation;
    }

    if (visual.sceneNode) {
        // Convert EQ Z-up to Irrlicht Y-up, add modelYOffset for correct positioning
        visual.sceneNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

        // Convert EQ heading to Irrlicht Y rotation
        // Heading arrives already in degrees (0-360) from eq_entities.cpp 11-bit conversion
        // EQ heading: 0=North(+Y), increases clockwise
        // Irrlicht Y rotation: 0=+Z, increases counter-clockwise
        // Must negate to convert between coordinate systems
        float visualHeading = -heading;
        visual.sceneNode->setRotation(irr::core::vector3df(0, visualHeading, 0));

        // Log rotation for player
        if (spawnId == playerSpawnId_) {
            static int rotLogCounter = 0;
            if (++rotLogCounter % 30 == 0) {
                LOG_TRACE(MOD_GRAPHICS, "[PROCESS-ROT] Player setRotation: spawn={} heading={:.1f} visualHeading={:.1f}",
                          spawnId, heading, visualHeading);
            }
        }

        // Debug: Compare rotation vs velocity direction for targeted NPC
        if (isDebugTarget && visual.isNPC) {
            float velAngle = std::atan2(visual.velocityY, visual.velocityX) * 180.0f / 3.14159265f;
            float rotAngle = visualHeading;
            float diff = rotAngle - velAngle;
            // Normalize diff to -180..180
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            LOG_TRACE(MOD_ENTITY, "TARGET ROT vs VEL spawn={} heading={} rotation={} velAngle={} diff={} vel=({},{})",
                      spawnId, heading, rotAngle, velAngle, diff, visual.velocityX, visual.velocityY);
        }
    }

    // Set animation based on server data
    // For NPCs: animation field is "SpeedRun" - a movement speed value (0=idle, >0=moving)
    // For Players: animation field is an animation ID
    // Skip animation updates for corpses - they should stay in death pose
    if (visual.isCorpse && spawnId == debugTargetId_) {
        LOG_TRACE(MOD_ENTITY, "TARGET CORPSE Skipping animation update for corpse spawn={} currentAnim='{}'", spawnId, visual.currentAnimation);
    }
    // Skip animation updates for the local player - controlled by setPlayerEntityAnimation()
    if (visual.isAnimated && visual.animatedNode && !visual.isCorpse && !visual.isPlayer) {
        // B1157 (#476): a casting entity holds the CastingAnim pose. The
        // client's state machine does not re-request a movement animation
        // while the spawn is casting (the cast setter stores the id on the
        // actor and the per-frame no-op path keeps it), so a movement update
        // arriving mid-channel must not pull the model back to walk/idle --
        // the pose ends with the cast state (completeEntityCast /
        // cancelEntityCast), never with a stray server update.
        if (visual.isCasting && !visual.castAnimCode.empty() &&
            visual.currentAnimation == visual.castAnimCode) {
            return;
        }
        // B1174 (#493): the same for an OP_Action track (a /disc's swing):
        // it plays out, then the per-frame walk takes the idle.
        if (!visual.actionAnimCode.empty() && visual.currentAnimation == visual.actionAnimCode) {
            return;
        }
        std::string targetAnim;
        bool playThrough = false;  // True for one-shot animations (jumps, attacks, emotes)
        bool loopAnim = true;

        // EQ Model animation codes:
        // l01 = Walk
        // l02 = Run
        // l05 = Falling
        // l06 = Crouch walk
        // l07 = Climbing
        // l09 = Swim
        // o01 = Stand idle
        // p02 = Sitting
        // c01-c09 = Combat animations
        // d01-d02 = Death animations
        // o01-o09 = One-shot animations (emotes, etc.)
        // t01-t09 = Skill animations

        // Check if entity is in a pose state (sitting, crouching, etc.) - don't override with movement
        if (visual.poseState == EntityVisual::PoseState::Sitting) {
            // Entity is sitting - keep sitting animation, don't override
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "p02";  // Sitting idle
            }
        } else if (visual.poseState == EntityVisual::PoseState::Crouching) {
            // Entity is crouching - keep crouch animation
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "l08";  // Crouching
            }
        } else if (visual.poseState == EntityVisual::PoseState::Lying) {
            // Entity is lying down - keep lying animation
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "d05";  // Lying down
            }
        } else if (visual.isNPC) {
            // For NPCs, the animation field is actually "SpeedRun" - a movement speed value
            // Typical values: 0=idle, 12=walk, 15-18=slow patrol, 22-28=run, 50=fast
            // The server sends the NPC's movement speed, not an animation ID
            // 0 = stationary/idle (NOT "no change" like for players)

            if (animation > 0) {
                // NPC is moving - choose walk or run based on speed
                // Speed ~12-18 = walk, ~20+ = run
                if (animation > 20) {
                    targetAnim = "l02";  // Run
                } else {
                    targetAnim = "l01";  // Walk
                }
            } else {
                // Speed is 0 - NPC is idle
                targetAnim = "o01";  // Idle/stand
            }
        } else {
            // For other players, the animation field is an actual animation ID
            // Player Animation IDs:
            // 0 = idle/stand (or no change)
            // 1 = walk
            // 3 = crouch walk
            // 20 = jump (playThrough)
            // 27 = run
            // 22 = fly/swim
            // 16 = death (playThrough)
            // 17 = looting
            // 5 = attack (playThrough)
            // 6 = attack2 (playThrough)
            // etc.

            uint32_t effectiveAnim = (animation != 0) ? animation : visual.lastNonZeroAnimation;
            bool isMoving = (std::abs(visual.velocityX) > 0.5f || std::abs(visual.velocityY) > 0.5f);

            // Player Animation IDs to Animation Codes mapping:
            // 0 = idle/stand -> o01
            // 1 = walk -> l01
            // 3 = crouch walk -> l06 (sneaking)
            // 5/6 = attack -> weapon-based
            // 16 = death -> d05
            // 17 = looting -> p05
            // 20 = jump -> l04 (standing) or l03 (running) based on movement
            // 22 = fly/swim -> l09 (swim treading)
            // 27 = run -> l02

            // Check for playThrough animations (one-shot animations that must complete)
            if (animation == 20) {
                // Jump animation - use l04 (standing jump) or l03 (running jump)
                if (isMoving) {
                    targetAnim = "l03";  // l03 = Jump (Running)
                } else {
                    targetAnim = "l04";  // l04 = Jump (Standing)
                }
                playThrough = true;
                loopAnim = false;
            } else if (animation == 16) {
                // Death animation - playThrough, don't loop
                targetAnim = "d05";  // Death/Dying animation
                playThrough = true;
                loopAnim = false;
            } else if (animation == 5) {
                // Attack animation - use weapon-based animation, playThrough, don't loop
                targetAnim = EQ::getWeaponAttackAnimation(visual.primaryWeaponSkill, false, false);
                playThrough = true;
                loopAnim = false;
                // B1022 (#342): the swing plays at the model's NATIVE animation
                // length (speed byte 10 = 1.0x -- constant for every swing, the
                // deployed server's zone logs show body `06 00 0A 05` across all
                // weapons/spawns). The swing RATE is the server's OP_Animation
                // cadence (100 x item delay ms); it never reaches the renderer
                // through a per-entity weapon-delay. The old stretch to
                // weaponDelayMs * 0.5 (1.5 s at the 3000 ms default) is gone
                // -- see the tombstone on setEntityWeaponDelay in
                // entity_renderer.cpp.
            } else if (animation == 6 || animation == 7) {
                // Swimming animations: 6 = swim idle (treading), 7 = swimming forward
                targetAnim = "l09";  // l09 = Swim Treading
                playThrough = false;
                loopAnim = true;
            } else if (animation == 8) {
                // Swim attack - use swim attack animation
                targetAnim = EQ::getWeaponAttackAnimation(visual.primaryWeaponSkill, false, true);
                playThrough = true;
                loopAnim = false;
            } else if (animation == 17) {
                // Looting animation - kneeling
                targetAnim = "p05";  // p05 = Kneel (Loot)
                playThrough = true;
                loopAnim = false;
            } else if (!isMoving && animation == 0) {
                // Entity is NOT moving and animation is 0 - return to idle
                // This takes priority over effectiveAnim (lastNonZeroAnimation)
                targetAnim = "o01";
            } else if (isMoving && effectiveAnim == 22) {
                // Swim/fly animation (only if actually moving)
                targetAnim = "l09";  // l09 = Swim Treading
            } else if (isMoving && effectiveAnim == 27) {
                // Run animation (only if actually moving)
                targetAnim = "l02";  // l02 = Run
            } else if (isMoving && effectiveAnim == 3) {
                // Crouch walk (sneaking) - only if actually moving
                targetAnim = "l06";  // l06 = Crouch Walk
            } else if (isMoving && effectiveAnim == 1) {
                // Walk animation (only if actually moving)
                targetAnim = "l01";  // l01 = Walk
            } else if (isMoving) {
                // Moving but no specific animation - use velocity to determine
                float speed = std::sqrt(visual.velocityX*visual.velocityX + visual.velocityY*visual.velocityY);
                if (speed > 15.0f) {
                    targetAnim = "l02";  // Run
                } else {
                    targetAnim = "l01";  // Walk
                }

                // Phase 6.2: Match animation speed to movement speed
                if (visual.animatedNode) {
                    // Base speeds: walk ~10, run ~23
                    float baseSpeed = (speed > 15.0f) ? 23.0f : 10.0f;
                    visual.animatedNode->getAnimator().matchMovementSpeed(baseSpeed, speed);
                }
            } else if ((effectiveAnim == 22 || effectiveAnim == 6 || effectiveAnim == 7) && !isMoving) {
                // Swimming but not moving - treading water
                targetAnim = "l09";  // l09 = Swim Treading
            } else if (animation != 0) {
                // Some other animation - default to idle for now
                targetAnim = "o01";
            } else {
                // Fallback - return to idle
                targetAnim = "o01";
            }
        }

        // Only change animation if different from current (unless it's a playThrough)
        // playThrough animations will be queued if another playThrough is active
        if (visual.currentAnimation != targetAnim || playThrough) {
            if (visual.animatedNode->hasAnimation(targetAnim)) {
                if (visual.animatedNode->playAnimation(targetAnim, loopAnim, playThrough)) {
                    visual.currentAnimation = targetAnim;
                }
            } else {
                // Animation not found - try fallback
                auto availableAnims = visual.animatedNode->getAnimationList();
                std::string fallbackAnim;
                char animClass = !targetAnim.empty() ? std::tolower(targetAnim[0]) : 'o';

                for (const auto& anim : availableAnims) {
                    if (!anim.empty() && std::tolower(anim[0]) == animClass) {
                        fallbackAnim = anim;
                        break;
                    }
                }

                if (!fallbackAnim.empty() && visual.animatedNode->playAnimation(fallbackAnim, loopAnim, playThrough)) {
                    LOG_TRACE(MOD_GRAPHICS, "updateEntity: spawn={} animation '{}' not found, using fallback '{}' (available: {})",
                              spawnId, targetAnim, fallbackAnim, fmt::join(availableAnims, ","));
                    visual.currentAnimation = fallbackAnim;
                } else if (visual.isNPC && animation > 0) {
                    // Only log warning for moving NPCs that have no animation
                    static std::unordered_set<uint32_t> loggedEntities;
                    if (loggedEntities.find(spawnId) == loggedEntities.end()) {
                        LOG_WARN(MOD_GRAPHICS, "updateEntity: spawn={} animation '{}' not found for moving NPC (available: {})",
                                  spawnId, targetAnim, fmt::join(availableAnims, ","));
                        loggedEntities.insert(spawnId);
                    }
                }
            }
        }

        // Debug output for targeted entity - show animation state every update
        if (spawnId == debugTargetId_) {
            // Get animator state from scene node's per-instance animator
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            const char* stateStr = "Unknown";
            switch (animator.getState()) {
                case AnimationState::Playing: stateStr = "Playing"; break;
                case AnimationState::Stopped: stateStr = "Stopped"; break;
                case AnimationState::Paused: stateStr = "Paused"; break;
            }
            LOG_TRACE(MOD_ENTITY, "TARGET ANIM updateEntity spawn={} posChanged={} serverAnim={} current='{}' target='{}' animatorCurrent='{}' state={} frame={} playThrough={} queued='{}' isNPC={}",
                      spawnId, positionChanged, animation, visual.currentAnimation, targetAnim, animator.getCurrentAnimation(), stateStr, animator.getCurrentFrame(), animator.isPlayingThrough(), animator.getQueuedAnimation(), visual.isNPC);
        }
    }
}

} // namespace Graphics
} // namespace EQT
