#include "client/graphics/entity_renderer.h"
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

void EntityRenderer::updateInterpolation(float deltaTime) {
    // Flush any pending updates in reverse order before interpolation
    flushPendingUpdates();

    static int targetDebugCounter = 0;

    // Performance optimization: only iterate active entities for interpolation
    // First pass: update time tracking for all entities (cheap operation)
    for (auto& [spawnId, visual] : entities_) {
        visual.timeSinceUpdate += deltaTime;
    }

    // Second pass: process only active entities (entities with velocity or special states)
    // Also process player entity and fading entities
    std::vector<uint16_t> toDeactivate;  // Entities that became stationary

    for (uint16_t spawnId : activeEntities_) {
        auto it = entities_.find(spawnId);
        if (it == entities_.end()) continue;
        EntityVisual& visual = it->second;

        // Skip entities with no scene node at all (not yet created)
        if (!visual.sceneNode) continue;
        // Skip entities that were culled (not in scene graph)
        if (!visual.inSceneGraph) continue;

        bool isDebugTarget = (spawnId == debugTargetId_);

        // Skip player entity position interpolation - controlled by camera/input
        // But still update equipment transforms
        if (visual.isPlayer) {
            updateEquipmentTransforms(visual);
            continue;
        }

        // B1148: a corpse on the fade list is faded and removed by
        // updateMainThreadEntityState() (corpse_fade.h); nothing here.
        if (visual.isFading) {
            continue;
        }

        // Check if corpse needs position adjustment (mid-game death animation just finished)
        if (visual.isCorpse && !visual.corpsePositionAdjusted && visual.isAnimated && visual.animatedNode) {
            // Increment corpse timer
            visual.corpseTime += deltaTime;

            SkeletalAnimator& animator = visual.animatedNode->getAnimator();

            // Wait for at least 0.5 seconds before checking if death animation finished
            // This gives the animation time to start playing
            if (visual.corpseTime < 0.5f) {
                // Still waiting for animation to play
            } else if (animator.getState() == AnimationState::Stopped ||
                       (animator.getState() == AnimationState::Playing && !animator.isPlayingThrough())) {
                // Animation finished - mark as adjusted (no Z offset needed)
                visual.corpsePositionAdjusted = true;
                visual.corpseYOffset = 0.0f;
            }
        }

        // Skip if no velocity (stationary) - mark for deactivation
        if (std::abs(visual.velocityX) < 0.01f &&
            std::abs(visual.velocityY) < 0.01f &&
            std::abs(visual.velocityZ) < 0.01f) {
            // Debug: Log periodically for stationary target (about every second at 60fps)
            if (isDebugTarget && ++targetDebugCounter % 60 == 0) {
                std::string stateStr = "N/A";
                std::string animatorCurrent = "N/A";
                std::string queuedAnim = "";
                int frame = 0;
                bool playingThrough = false;

                if (visual.isAnimated && visual.animatedNode) {
                    SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                    switch (animator.getState()) {
                        case AnimationState::Playing: stateStr = "Playing"; break;
                        case AnimationState::Stopped: stateStr = "Stopped"; break;
                        case AnimationState::Paused: stateStr = "Paused"; break;
                    }
                    animatorCurrent = animator.getCurrentAnimation();
                    queuedAnim = animator.getQueuedAnimation();
                    frame = animator.getCurrentFrame();
                    playingThrough = animator.isPlayingThrough();
                }

                LOG_TRACE(MOD_ENTITY, "TARGET ANIM STATIONARY spawn={} serverAnim={} visualAnim='{}' animatorAnim='{}' state={} frame={} playThrough={} queued='{}' timeSinceUpdate={}",
                          spawnId, visual.serverAnimation, visual.currentAnimation, animatorCurrent, stateStr, frame, playingThrough, queuedAnim, visual.timeSinceUpdate);
            }
            // Still update equipment transforms for stationary entities (they still animate)
            updateEquipmentTransforms(visual);
            // Mark for deactivation (can't modify set during iteration)
            toDeactivate.push_back(spawnId);
            continue;
        }

        // Corpses should never move - skip interpolation entirely
        if (visual.isCorpse) {
            updateEquipmentTransforms(visual);
            toDeactivate.push_back(spawnId);
            continue;
        }

        // Don't interpolate too far past the expected update time
        // This prevents entities from running off into infinity if updates stop
        if (visual.timeSinceUpdate > visual.lastUpdateInterval * 1.5f) {
            // For NPCs that are still supposed to be moving (serverAnimation != 0),
            // recalculate velocity from heading instead of stopping
            // Negative animation = moving backward
            if (visual.isNPC && !visual.isCorpse && visual.serverAnimation != 0) {
                // Recalculate velocity from heading - NPC should keep moving
                // Convert heading to radians: heading * π/180 (no *2, heading is full resolution from 11-bit)
                float headingRad = visual.serverHeading * 3.14159265f / 180.0f;
                // Use same speed multiplier as in processUpdate (0.58)
                // Apply slight damping since we're past the expected update time
                float dampingFactor = 0.85f;
                float speed = static_cast<float>(std::abs(visual.serverAnimation)) * 0.58f * dampingFactor;
                // Negative animation = moving backward (reverse direction)
                float direction = (visual.serverAnimation < 0) ? -1.0f : 1.0f;
                visual.velocityX = std::cos(headingRad) * speed * direction;
                visual.velocityY = std::sin(headingRad) * speed * direction;
                visual.velocityZ = 0;

                // Debug output throttled to ~1/sec (using targetDebugCounter from outer scope)
                if (isDebugTarget && targetDebugCounter % 60 == 0) {
                    std::string animatorCurrent = visual.currentAnimation;
                    std::string stateStr = "N/A";
                    if (visual.isAnimated && visual.animatedNode) {
                        SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                        animatorCurrent = animator.getCurrentAnimation();
                        switch (animator.getState()) {
                            case AnimationState::Playing: stateStr = "Playing"; break;
                            case AnimationState::Stopped: stateStr = "Stopped"; break;
                            case AnimationState::Paused: stateStr = "Paused"; break;
                        }
                    }
                    LOG_TRACE(MOD_ENTITY, "TARGET ANIM TIMEOUT_KEEP_MOVING spawn={} serverAnim={} animatorAnim='{}' state={} recalcVel=({},{}) heading={}",
                              spawnId, visual.serverAnimation, animatorCurrent, stateStr, visual.velocityX, visual.velocityY, visual.serverHeading);
                }
                // Don't skip - continue to interpolate
            } else {
                // Stop movement until next update
                visual.velocityX = 0;
                visual.velocityY = 0;
                visual.velocityZ = 0;

                // Mark for deactivation since velocity is now zero
                toDeactivate.push_back(spawnId);

                // Determine if we should switch to idle or keep current animation
                bool keepMovingAnim = false;
                if (visual.isNPC) {
                    // For NPCs, serverAnimation is a speed value (non-zero = moving)
                    keepMovingAnim = (visual.serverAnimation != 0);
                } else {
                    // For players, check if the animation ID indicates movement
                    // Use absolute value since negative = same animation in reverse
                    int32_t absAnim = std::abs(static_cast<int32_t>(visual.lastNonZeroAnimation));
                    keepMovingAnim = (absAnim == 1 ||   // walk
                                      absAnim == 12 ||  // walk (actual)
                                      absAnim == 27 ||  // run
                                      absAnim == 22);   // fly/swim
                }

                // Don't interrupt playThrough animations (jumps, attacks, emotes)
                bool isPlayingThrough = visual.animatedNode && visual.animatedNode->isPlayingThrough();

                // Debug output throttled to ~1/sec
                if (isDebugTarget && targetDebugCounter % 60 == 0) {
                    std::string animatorCurrent = visual.currentAnimation;
                    std::string stateStr = "N/A";
                    std::string queuedAnim = "";
                    int frame = 0;
                    if (visual.isAnimated && visual.animatedNode) {
                        SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                        animatorCurrent = animator.getCurrentAnimation();
                        queuedAnim = animator.getQueuedAnimation();
                        frame = animator.getCurrentFrame();
                        switch (animator.getState()) {
                            case AnimationState::Playing: stateStr = "Playing"; break;
                            case AnimationState::Stopped: stateStr = "Stopped"; break;
                            case AnimationState::Paused: stateStr = "Paused"; break;
                        }
                    }
                    LOG_TRACE(MOD_ENTITY, "TARGET ANIM TIMEOUT_STOP spawn={} serverAnim={} visualAnim='{}' animatorAnim='{}' state={} frame={} playThrough={} queued='{}' keepMovingAnim={}",
                              spawnId, visual.serverAnimation, visual.currentAnimation, animatorCurrent, stateStr, frame, isPlayingThrough, queuedAnim, keepMovingAnim);
                }

                if (!keepMovingAnim && !isPlayingThrough && visual.isAnimated && visual.animatedNode && visual.currentAnimation != "o01") {
                    if (isDebugTarget) {
                        LOG_TRACE(MOD_ENTITY, "TARGET ANIM SWITCH_TO_IDLE spawn={} from='{}'", spawnId, visual.currentAnimation);
                    }
                    if (visual.animatedNode->hasAnimation("o01")) {
                        visual.animatedNode->playAnimation("o01", true, false);
                        visual.currentAnimation = "o01";
                    }
                }
                continue;
            }
        }

        // Interpolate position based on velocity
        // Apply gentle damping only after significantly exceeding expected update interval
        // This prevents runaway if NPC changes direction, but doesn't hurt straight-line movement
        float damping = 1.0f;
        if (visual.timeSinceUpdate > visual.lastUpdateInterval * 3.0f) {
            // Well past expected update time - start slowing down gently
            float overshootTime = visual.timeSinceUpdate - visual.lastUpdateInterval * 3.0f;
            // Dampen by 50% every 5 seconds past the threshold
            damping = std::exp(-0.14f * overshootTime);
            damping = std::max(0.3f, damping);  // Don't go below 30% speed
        }
        visual.lastX += visual.velocityX * deltaTime * damping;
        visual.lastY += visual.velocityY * deltaTime * damping;
        visual.lastZ += visual.velocityZ * deltaTime * damping;

        // Snap NPC to ground during interpolation to follow terrain in hilly zones
        // Only snap if moving horizontally (has XY velocity) and we have a ground finder
        if (visual.isNPC && groundFinderCallback_ &&
            (std::abs(visual.velocityX) > 0.01f || std::abs(visual.velocityY) > 0.01f)) {
            float groundZ = groundFinderCallback_(visual.lastX, visual.lastY, visual.lastZ);
            // The node origin IS the server Z and sits inside the body; groundZ is
            // the floor level. Placing the origin at groundZ + collisionZOffset
            // (the origin-to-feet distance) puts the mesh's feet ON the floor.
            float targetZ = groundZ + visual.collisionZOffset;
            // Only snap if ground is within reasonable range (avoid teleporting through floors/ceilings)
            float heightDiff = targetZ - visual.lastZ;
            if (std::abs(heightDiff) < 20.0f) {
                visual.lastZ = targetZ;
            }
        }

        // Debug moving target periodically
        if (isDebugTarget && targetDebugCounter % 60 == 0) {
            std::string animatorCurrent = visual.currentAnimation;
            std::string stateStr = "N/A";
            int frame = 0;
            if (visual.isAnimated && visual.animatedNode) {
                SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                animatorCurrent = animator.getCurrentAnimation();
                frame = animator.getCurrentFrame();
                switch (animator.getState()) {
                    case AnimationState::Playing: stateStr = "Playing"; break;
                    case AnimationState::Stopped: stateStr = "Stopped"; break;
                    case AnimationState::Paused: stateStr = "Paused"; break;
                }
            }
            LOG_TRACE(MOD_ENTITY, "TARGET ANIM MOVING spawn={} serverAnim={} animatorAnim='{}' state={} frame={} vel=({},{})",
                      spawnId, visual.serverAnimation, animatorCurrent, stateStr, frame, visual.velocityX, visual.velocityY);
        }

        if (visual.sceneNode) {
            // Convert EQ Z-up to Irrlicht Y-up, apply model height offset
            visual.sceneNode->setPosition(irr::core::vector3df(visual.lastX, visual.lastZ + visual.modelYOffset, visual.lastY));
        }

        // Update light position to follow entity
        if (visual.lightNode) {
            visual.lightNode->setPosition(irr::core::vector3df(visual.lastX, visual.lastZ + visual.modelYOffset + 3.0f, visual.lastY));
        }

        // Update equipment positions to follow bone attachment points
        updateEquipmentTransforms(visual);
    }

    // Remove deactivated entities from active set (they became stationary)
    for (uint16_t spawnId : toDeactivate) {
        activeEntities_.erase(spawnId);
    }
}

} // namespace Graphics
} // namespace EQT
