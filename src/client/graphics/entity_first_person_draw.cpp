/*
 * T309 -- the measured first-person draw rule.
 *
 * In first person the official Titanium client draws the player's own model --
 * the WHOLE model, with its equipped weapon -- for the duration of an ATTACK
 * animation, and at no other time (T307;
 * refactor/T307_official_draw_rule.md, review/T307/README.md). The rule
 * itself, its discriminators, and the reason it must hide by WITHHOLDING the
 * node from the render list rather than by setVisible(), are documented on
 * include/client/graphics/first_person_body_draw.h.
 *
 * J03 moved the REST of the first-person family here from
 * entity_renderer.cpp, so that the whole feature lives in one TU: the
 * per-frame driver below, the mode toggle, the player entity position and
 * animation entry points, the player spawn-id binding, the model/collision
 * Y offsets and the first-person eye offset. A grep for first_person lands
 * on all of it.
 *
 * Split out of entity_renderer.cpp, which was over the file-size ceiling
 * (J03 has since split it further).
 */
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
#include "client/graphics/first_person_body_draw.h"

namespace EQT {
namespace Graphics {

// T309: THE MEASURED FIRST-PERSON DRAW RULE, driven once per frame.
//
// The signal is the local player's own animator: fable already carries
// exactly what the rule needs and no parallel state machine was invented.
// EQAnimatedMeshSceneNode::getCurrentAnimation() is the animation code the
// per-instance SkeletalAnimator is running, and getAnimator().getState() is
// whether it is actually PLAYING. Combat animations arrive here through
// setPlayerEntityAnimation()/setEntityAnimation() (the combat-skill
// dispatch, entity_combat_anim.cpp's buffer, the spell and skill managers) as
// NON-LOOPING play-through animations.
//
// *** READ THE STATE, NOT THE CODE. *** A non-looping animation sets
// holdOnComplete_ (SkeletalAnimator::playAnimation: holdOnComplete_ =
// !looping_), so a finished swing FREEZES ON ITS LAST FRAME and KEEPS the
// code "c01" until renderer_player_physics asks for "p01" again. Testing the
// code alone would leave the body drawn permanently after the first swing.
// The animator's own Playing state is the honest edge, so the pair
// "getCurrentAnimation() is an attack code AND the state is Playing" is
// precisely "an attack animation is currently playing" -- with no timer of
// our own and no dependence on /attack state. An out-of-range auto-attack
// that produces no swing never sets an attack code, so it draws nothing,
// which is what T307 measured.
void EntityRenderer::updateFirstPersonBodyDrawRule() {
    auto it = entities_.find(playerSpawnId_);
    if (playerSpawnId_ == 0 || it == entities_.end()) return;
    EntityVisual& visual = it->second;
    if (!visual.isPlayer) return;

    if (!visual.animatedNode) {
        // A static placeholder mesh has no skeleton to freeze, so the
        // OnAnimate trap does not apply to it and setVisible is honest here.
        if (visual.meshNode) {
            visual.meshNode->setVisible(!visual.isFirstPersonMode);
        }
        return;
    }

    bool draw = true;
    if (visual.isFirstPersonMode) {
        const SkeletalAnimator& animator = visual.animatedNode->getAnimator();
        draw = shouldDrawOwnBodyInFirstPerson(
            visual.animatedNode->getCurrentAnimation(),
            animator.getState() == AnimationState::Playing);
    }

    // WITHHOLD, never setVisible(false) -- see first_person_body_draw.h.
    // The node keeps animating while hidden, so the swing that reveals it is
    // already at the right phase.
    const bool withhold = !draw;
    if (visual.animatedNode->isWithheldFromRenderList() != withhold) {
        LOG_DEBUG(MOD_GRAPHICS,
                  "first-person draw rule: player body {} (anim='{}')",
                  withhold ? "hidden" : "DRAWN",
                  visual.animatedNode->getCurrentAnimation());
    }
    visual.animatedNode->setWithheldFromRenderList(withhold);
}

bool EntityRenderer::isPlayerBodyWithheldFromRenderList() const {
    auto it = entities_.find(playerSpawnId_);
    if (playerSpawnId_ == 0 || it == entities_.end()) return false;
    if (!it->second.animatedNode) return false;
    return it->second.animatedNode->isWithheldFromRenderList();
}

void EntityRenderer::setPlayerFirstPersonMode(bool enabled) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            visual.isFirstPersonMode = enabled;

            // T300/T309: the player model is the ORDINARY model -- full
            // body, unfiltered, with its equipment attached to its hand
            // bones. Nothing here filters geometry and nothing defers it to
            // a second pass:
            //   * T74's arm-bone index filter (setFirstPersonMode() on the
            //     node) drew arms only, and fell back to hiding the player
            //     when a skeleton yielded no arm bones;
            //   * T296 additionally withheld the node from the render list
            //     (setDeferredToViewmodelPass()) and hid the equipment, so a
            //     separate viewmodel pass could draw them in its own view
            //     space. T299 refuted the pass.
            //
            // T300 then claimed the DOWN-PITCH CLAMP was what hid the body.
            // *** THAT WAS WRONG AND IS SUPERSEDED HERE. *** T301 Leg B
            // showed no clamp value can empty the down-frustum band and that
            // the measured clamp reaches PAST the nadir; T307 then measured
            // the real rule against the official client. The clamp survives
            // as a control-feel limit only.
            //
            // The model stays VISIBLE in the Irrlicht sense at all times --
            // IsVisible gates OnAnimate(), so clearing it freezes the
            // skeleton. Hiding is done by withholding it from the render
            // list, per frame, in updateFirstPersonBodyDrawRule().
            if (visual.animatedNode) {
                visual.animatedNode->setVisible(true);
            } else if (visual.meshNode) {
                visual.meshNode->setVisible(true);
            }

            // The floating name tag is the one thing that must still go: it
            // is a camera-facing 2D billboard drawn in screen space, so no
            // camera angle moves it off screen and the player would read his
            // own name across the middle of the frame.
            if (visual.nameNode) {
                visual.nameNode->setVisible(!enabled);
            }

            if (visual.primaryEquipNode) {
                visual.primaryEquipNode->setVisible(true);
            }
            if (visual.secondaryEquipNode) {
                visual.secondaryEquipNode->setVisible(true);
            }

            LOG_DEBUG(MOD_GRAPHICS, "setPlayerFirstPersonMode: enabled={} (draw rule: the model is drawn only while an attack animation plays)",
                      enabled);
            break;
        }
    }

    // Apply the rule at once, so entering first person mid-swing does not
    // draw a frame it should not (or drop one it should).
    updateFirstPersonBodyDrawRule();
}


bool EntityRenderer::isPlayerInFirstPersonMode() const {
    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.isFirstPersonMode;
        }
    }
    return false;
}

void EntityRenderer::updatePlayerEntityPosition(float x, float y, float z, float heading) {
    // Convert heading from 0-512 to degrees (0-360) for processUpdate
    float headingDegrees = heading * 360.0f / 512.0f;

    // Use stored playerSpawnId_ instead of searching for isPlayer flag
    uint16_t playerSpawnId = playerSpawnId_;

    if (playerSpawnId != 0 && entities_.find(playerSpawnId) != entities_.end()) {
        // Queue update to go through processUpdate() like NPCs do
        PendingUpdate update;
        update.spawnId = playerSpawnId;
        update.x = x;
        update.y = y;
        update.z = z;
        update.heading = headingDegrees;
        update.dx = 0;
        update.dy = 0;
        update.dz = 0;
        update.animation = 0;

        // Log player position updates
        static int playerUpdateCounter = 0;
        if (++playerUpdateCounter % 60 == 0) {
            LOG_TRACE(MOD_GRAPHICS, "[PLAYER-UPDATE] processUpdate for spawn={} pos=({:.1f},{:.1f},{:.1f}) heading={:.1f}",
                     playerSpawnId, x, y, z, headingDegrees);
        }

        processUpdate(update);
    } else {
        static int notFoundCounter = 0;
        if (++notFoundCounter % 60 == 0) {
            LOG_WARN(MOD_GRAPHICS, "[PLAYER-UPDATE] Player entity not found! spawnId={}", playerSpawnId);
        }
    }
}

void EntityRenderer::setPlayerEntityAnimation(const std::string& animCode, bool loop, float movementSpeed, bool playThrough) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            // B1157 (#476): the casting player holds the CastingAnim pose
            // (the client's state machine does not re-request the idle while
            // the spawn is casting); idle/walk requests resume it only once
            // the cast state ends (completeEntityCast / cancelEntityCast).
            // A play-through request (a swing, a jump) still gets through --
            // the server ends the cast on it.
            if (visual.isCasting && !visual.castAnimCode.empty() &&
                visual.currentAnimation == visual.castAnimCode && !playThrough) {
                break;
            }
            if (visual.isAnimated && visual.animatedNode) {
                // Don't interrupt playThrough animations (combat, skills, jump, etc.)
                // They must complete before returning to movement/idle animations
                // Exception: a new playThrough animation CAN interrupt an existing one
                if (visual.animatedNode->isPlayingThrough() && !playThrough) {
                    break;  // Let the playThrough animation finish
                }

                // Only change animation if it's different from current
                if (visual.currentAnimation != animCode) {
                    if (visual.animatedNode->hasAnimation(animCode)) {
                        visual.animatedNode->playAnimation(animCode, loop, playThrough);
                        visual.currentAnimation = animCode;
                    }
                }

                // Match animation speed to movement speed (like NPCs)
                // Base speeds: walk animation ~10 units/sec, run animation ~23 units/sec
                if (movementSpeed > 0.0f && (animCode == "l01" || animCode == "l02")) {
                    float baseSpeed = (animCode == "l02") ? 23.0f : 10.0f;
                    visual.animatedNode->getAnimator().matchMovementSpeed(baseSpeed, movementSpeed);
                } else {
                    // Reset to normal speed for non-movement animations (jump, idle, etc.)
                    visual.animatedNode->getAnimator().matchMovementSpeed(0.0f, 0.0f);
                }
            }
            break; // Only one player entity
        }
    }
}

// T472 (board B587): the player's node had TWO heading writers, and they did
// not agree. updatePlayerEntityPosition() (above) passes the renderer's own
// heading -- 90 - server, ZoneProcessPlayerProfile's load-bearing flip -- but
// only when the player moves, turns or rides a lift. The node is BUILT from the
// self spawn row at zone-in, and ZoneProcessClientUpdate's self echo re-sends
// that row, and both carry the RAW server heading (by design: the self row is
// not flipped, see refactor/BOARD.md T404's note). So a player who had not
// moved since zone-in wore the player bake (+90) over an unflipped heading --
// 135 degrees wrong at T466's pose (server 160) -- and the camera behind him
// showed his face. No conversion site changes here: the renderer's heading is
// simply made the only one the player's node wears.
void EntityRenderer::syncLocalPlayerHeading(float heading512) {
    if (playerSpawnId_ == 0) return;
    auto it = entities_.find(playerSpawnId_);
    if (it == entities_.end() || !it->second.sceneNode) return;
    const float headingDegrees = heading512 * 360.0f / 512.0f;
    const float nodeYaw = -headingDegrees;  // processUpdate()'s own EQ -> Irrlicht step
    if (std::abs(it->second.sceneNode->getRotation().Y - nodeYaw) < 0.001f) return;
    it->second.sceneNode->setRotation(irr::core::vector3df(0.0f, nodeYaw, 0.0f));
    it->second.lastHeading = headingDegrees;
}

void EntityRenderer::setPlayerSpawnId(uint16_t spawnId) {
    // Store the player spawn ID for filtering
    playerSpawnId_ = spawnId;

    // First, unmark any existing player entity
    for (auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            visual.isPlayer = false;
            // Also unmark the node for debug logging
            if (visual.animatedNode) {
                visual.animatedNode->setIsPlayerNode(false);
            }
        }
    }

    // Now mark the new entity as player
    auto it = entities_.find(spawnId);
    if (it != entities_.end()) {
        // Mark entity as player
        it->second.isPlayer = true;
        // Also mark the node for debug logging
        if (it->second.animatedNode) {
            it->second.animatedNode->setIsPlayerNode(true);
        }

        // Player always needs to be in active set for equipment transform updates
        activeEntities_.insert(spawnId);

        LOG_DEBUG(MOD_GRAPHICS, "setPlayerSpawnId: spawn={} name={} modelYOffset={:.2f} sceneNode={} animatedNode={}",
                  spawnId, it->second.name, it->second.modelYOffset,
                  (void*)it->second.sceneNode, (void*)it->second.animatedNode);
    }
}

float EntityRenderer::getPlayerModelYOffset() const {
    // Find the player entity
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.modelYOffset;
        }
    }
    return 0.0f;
}

float EntityRenderer::getPlayerCollisionZOffset() const {
    // Find the player entity
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.collisionZOffset;
        }
    }
    return 0.0f;
}

EntityRenderer::FirstPersonEyeOffset
EntityRenderer::getPlayerFirstPersonEyeOffset() const {
    // T301 -- READ include/client/graphics/first_person_eye.h BEFORE CHANGING
    // ANY NUMBER HERE.
    //
    // The offset is measured from the player NODE ORIGIN, which is where
    // createEntity() puts the node. B1176 (#495) moved that origin onto the
    // client's own placement -- ground + ROffset x scale, 3.125 x s
    // (entityNodeCollisionZOffset, actor_scale.h) -- so the node origin is
    // the z the client draws and transmits, and the offset below is added
    // to exactly that z.
    //
    // B1176 (#495) -- THE VERTICAL IS THE CLIENT'S OWN DERIVATION. The eye
    // HEIGHT is no longer the head cluster's: it is 0x234 - 0x238 =
    // 6.2 - 3.125 model units above the node origin, scaled by the actor
    // scale -- the client's first-person camera (005701c0:284496-284510 via
    // FUN_00489bf5; the third-person orbit centre is the same number,
    // 005703a0:284622-284641). Standing eye = ground + 6.2 x s, as in the
    // client. The node (EQAnimatedMeshSceneNode::getFirstPersonEyeOffset)
    // computes it; the HEAD BONE cluster still supplies the HORIZONTAL
    // component (the client's unmounted first-person camera is on the
    // player's own axis -- 005701c0 -- and fable keeps its small measured
    // head-centroid offset; the live leg measures the effect). The T301/T431
    // cluster-height history in first_person_eye.h applies to the fallback
    // paths below, not to the player's standing eye any more.
    //
    // T76's getBoneIndexByName("HEAD") is gone for a better reason than T300
    // gave: NO EQ SKELETON HAS EVER CONTAINED A BONE CALLED "HEAD". The head
    // is <modelCode>he (humhe, trmhe, ikmhe; frmhehead for the Luclin-era
    // Froglok), which is why T295 logged source=BBOX_FALLBACK headBoneIdx=-1
    // in every live session. The lever was not disabled, it was misspelled.
    FirstPersonEyeOffset out;

    for (const auto& [id, visual] : entities_) {
        if (!visual.isPlayer || !visual.isAnimated || !visual.animatedNode) continue;

        // POSTURE, not animation. T301 Leg A measured the official eye as
        // driven by the discrete posture state (sitting drops it 3.6 units)
        // and NOT by the animated skeleton. visual.poseState is exactly that
        // state: it arrives on SpawnAppearance and is deliberately never
        // written by movement updates, which is why it is the right input and
        // the animator is not.
        EyePosture posture = EyePosture::Standing;
        switch (visual.poseState) {
            case EntityVisual::PoseState::Standing:  posture = EyePosture::Standing;  break;
            case EntityVisual::PoseState::Sitting:   posture = EyePosture::Sitting;   break;
            case EntityVisual::PoseState::Crouching: posture = EyePosture::Crouching; break;
            case EntityVisual::PoseState::Lying:     posture = EyePosture::Lying;     break;
        }

        irr::core::vector3df offset;
        if (visual.animatedNode->getFirstPersonEyeOffset(offset, posture)) {
            out.irrX = offset.X;
            out.irrY = offset.Y;
            out.irrZ = offset.Z;
            out.derivedFromHead = true;
            static int eyeLogCount = 0;
            if (eyeLogCount++ % 500 == 0) {
                LOG_DEBUG(MOD_GRAPHICS,
                          "[FP-EYE] source=HEAD_BONE offset=({:.4f},{:.4f},{:.4f}) pose={} posture={}",
                          out.irrX, out.irrY, out.irrZ,
                          static_cast<int>(kFirstPersonEyeSource),
                          static_cast<int>(posture));
            }
            return out;
        }

        // FALLBACK ONLY. This is the T300 behaviour and it is the defect;
        // it survives solely so a model with no head anchor still produces a
        // camera rather than a camera at the player's navel. If this fires
        // for a playable race, that race's skeleton needs investigating --
        // all 30 shipped race/gender combinations resolve a head bone.
        EQAnimatedMesh* eqMesh = visual.animatedNode->getEQMesh();
        if (!eqMesh) continue;
        irr::core::aabbox3df bbox = eqMesh->getBoundingBox();
        out.irrY = bbox.MaxEdge.Y;
        out.derivedFromHead = false;
        static int fallbackLogCount = 0;
        if (fallbackLogCount++ % 500 == 0) {
            LOG_WARN(MOD_GRAPHICS,
                     "[FP-EYE] source=BBOX_CROWN_FALLBACK eyeAboveNodeOrigin={:.4f} bboxY=[{:.4f},{:.4f}] -- no head anchor on this model",
                     out.irrY, bbox.MinEdge.Y, bbox.MaxEdge.Y);
        }
        return out;
    }

    // No player entity yet (pre-zone-in). Node-origin-relative default for a
    // human-sized body -- NOT a feet-relative 6.x. Replaced the moment the
    // model loads.
    out.irrY = 3.65f;
    return out;
}

} // namespace Graphics
} // namespace EQT
