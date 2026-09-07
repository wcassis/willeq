// door_manager_animation.cpp -- DoorManager::update(), the per-frame door
// animation.
//
// Split out of door_manager.cpp by T438, which pushed that TU past the
// 1,500-line ceiling: the lift cycle (board B422), the client-side auto-close
// and the measured rotation/spin rates all live in this one body, and it is
// the only part of DoorManager that has a clock. Every constant it reads comes
// from include/client/graphics/door_motion.h, where each one carries the
// oracle session that produced it.
#include "client/graphics/door_manager.h"

#include "client/graphics/door_motion.h"
#include "common/logging.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

void DoorManager::update(float deltaTime)
{
    liftMovers_.clear();

    // T438: the clocks run for EVERY door, before the PVS gate, because both
    // of them change the world and not just the picture. A lift that is out of
    // sight is still carrying its rider and still has to arrive on time, and a
    // door that auto-closes off-screen still has to stop being a hole in a
    // wall. The old body skipped hidden doors entirely and had no clocks.
    for (auto& [id, visual] : doors_) {
        if (visual.isLift) {
            const float before = visual.lift.offset();
            visual.lift.advance(deltaTime);
            const float delta = visual.lift.offset() - before;
            visual.isAnimating = visual.lift.moving();
            if (delta != 0.0f) {
                liftMovers_.emplace_back(id, delta);
                if (visual.pivotNode) {
                    visual.pivotNode->setPosition(irr::core::vector3df(
                        visual.x, visual.z + visual.lift.offset(), visual.y));
                    if (visual.inSceneGraph) {
                        visual.pivotNode->updateAbsolutePosition();
                        if (visual.sceneNode) visual.sceneNode->updateAbsolutePosition();
                    }
                }
                // The platform moved, so its collision triangles did.
                markCollisionDirty(id);
            }
            continue;
        }
        if (visual.autoCloseTimer > 0.0f) {
            visual.autoCloseTimer -= deltaTime;
            if (visual.autoCloseTimer <= 0.0f) {
                visual.autoCloseTimer = -1.0f;
                if (visual.isOpen) {
                    visual.isOpen = false;
                    visual.isAnimating = true;
                    LOG_TRACE(MOD_GRAPHICS, "Door {} auto-closing (client-side, T438)", id);
                }
            }
        }
    }

    for (auto& [id, visual] : doors_) {
        if (!visual.sceneNode) {
            continue;
        }

        bool doorVisible = isRegionPvsVisible(visual.bspRegion);

        // Occlusion culling: region-level stencil portal occlusion
        if (doorVisible && visual.bspRegion != SIZE_MAX && occlusionCulledRegions_
            && occlusionCulledRegions_->count(visual.bspRegion)) {
            doorVisible = false;
        }

        if (!doorVisible) {
            if (visual.inSceneGraph && visual.pivotNode) {
                visual.pivotNode->remove();
                visual.inSceneGraph = false;
            }
            continue;
        } else {
            if (!visual.inSceneGraph && visual.pivotNode) {
                smgr_->getRootSceneNode()->addChild(visual.pivotNode);
                visual.inSceneGraph = true;
                visual.pivotNode->updateAbsolutePosition();
                if (visual.sceneNode) visual.sceneNode->updateAbsolutePosition();
            }
        }

        // Handle spinning objects (opentype 100 = Y-spin, 105 = Z-spin)
        if (visual.isSpinning) {
            visual.spinAngle += DoorMotion::kSpinDegreesPerSecond * deltaTime;
            if (visual.spinAngle >= 360.0f) {
                visual.spinAngle -= 360.0f;
            }

            // Spinning objects rotate the pivot node
            irr::scene::ISceneNode* rotNode = visual.pivotNode ? visual.pivotNode : visual.sceneNode;
            irr::core::vector3df rot = rotNode->getRotation();
            if (visual.opentype == 100) {
                // Y-axis spin (lamps, gems, etc.) - add spin to base heading rotation
                // Heading is in 512 format, convert to degrees, add 90 for alignment
                rot.Y = -visual.closedHeading * 360.0f / 512.0f + 90.0f + visual.spinAngle;
            } else if (visual.opentype == 105) {
                // Z-axis spin (blades, etc.) - spin around local Z
                rot.Z = visual.spinAngle;
            }
            rotNode->setRotation(rot);
            continue;  // Spinning objects don't use open/close animations
        }

        // T438 (board B422): a lift TRANSLATES; its pose was applied in the
        // clock loop above and it must not also be lerped between a heading
        // and itself, which is all the old body did for an opentype 59.
        if (visual.isLift) {
            continue;
        }

        // Handle door open/close animations
        if (!visual.isAnimating) {
            continue;
        }

        // Update animation progress
        float targetProgress = visual.isOpen ? 1.0f : 0.0f;
        float direction = visual.isOpen ? 1.0f : -1.0f;

        // T438: MEASURED against the official client, and the two directions
        // are not the same speed -- opening takes 0.95 s and closing 0.60 s
        // (five cycles each). fable's single ANIM_SPEED 2.0f ("~0.5 seconds")
        // was invented and too fast in both directions. See door_motion.h.
        const float animSpeed = 1.0f / (visual.isOpen ? DoorMotion::kHingedOpenSeconds
                                                      : DoorMotion::kHingedCloseSeconds);
        visual.animProgress += direction * animSpeed * deltaTime;
        visual.animProgress = std::max(0.0f, std::min(1.0f, visual.animProgress));

        // Interpolate heading between closed and open (both in 512 format)
        // When closed (progress=0): openHeading (spawn + incline, appears closed)
        // When open (progress=1): closedHeading (spawn heading, appears open)
        float currentHeading = visual.openHeading +
            (visual.closedHeading - visual.openHeading) * visual.animProgress;

        // Apply rotation to pivot node (rotates door around hinge)
        // Heading is in 512 format, convert to degrees, add 90 for alignment
        float irrRotation = -currentHeading * 360.0f / 512.0f + 90.0f;
        irr::scene::ISceneNode* rotNode = visual.pivotNode ? visual.pivotNode : visual.sceneNode;
        rotNode->setRotation(irr::core::vector3df(0, irrRotation, 0));

        // Update bounding box for interaction
        visual.boundingBox = visual.sceneNode->getTransformedBoundingBox();

        // T435 (B425): the map holds the door's CURRENT geometry, so a door
        // re-bakes on every frame it moves as well as when it settles. Today
        // both poses are a pure function of the two headings, which do not
        // change mid-animation; a door that TRANSLATES (board B422's lifts)
        // moves its pivot, and this is the hook that keeps it honest.
        markCollisionDirty(id);

        // Check if animation complete
        if (visual.animProgress == targetProgress) {
            visual.isAnimating = false;
            LOG_DEBUG(MOD_GRAPHICS, "Door {} animation complete ({})",
                      id, visual.isOpen ? "open" : "closed");
        }
    }
}

}  // namespace Graphics
}  // namespace EQT
