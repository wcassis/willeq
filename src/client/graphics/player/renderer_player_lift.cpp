// renderer_player_lift.cpp -- the player rides a moving lift.
//
// T438, board B423's second half. A translating door is not just a moving wall:
// the official client CARRIES the player standing on it. Measured on the
// gfaydark Kelethin lift 69 with the rig character parked on the platform --
// its server-side z climbed 5.81 -> 10.93 -> 19.98 -> 29.30 -> 39.47 -> 49.87
// -> 58.65 in step with the platform, at the same 8.67 units/s the platform
// travels (review/T438/logs/trace_lift69_ride.tsv).
//
// This runs once per frame from FrameComponent, immediately after
// DoorManager::update() and PlayerCameraComponent::syncDoorCollision(), so the
// collision triangles it queries are the ones the platform has THIS frame.
#include "client/graphics/player/player_camera_component.h"

#include "client/graphics/door_collision.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/door_motion.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/camera_controller.h"
#include "client/bridge/game_state_bridge.h"
#include "client/zone_bsp.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"

#include <cmath>

namespace EQT {
namespace Graphics {

void PlayerCameraComponent::carryPlayerOnLifts(float deltaTime) {
    (void)deltaTime;
    DoorManager* doorManager = ctx_.doorManager.get();
    if (!doorManager) return;
    const auto& movers = doorManager->liftMovers();
    if (movers.empty()) return;

    // Where the player's feet are. playerZ is the model ORIGIN (the server z),
    // and the feet sit collisionZOffset below it -- the same conversion
    // findGroundZBspFiltered()'s callers make.
    const float modelYOffset = ctx_.entityRenderer
        ? ctx_.entityRenderer->getPlayerCollisionZOffset() : 0.0f;
    const float feetZ = ctx_.playerZ - modelYOffset;

    // A short ray straight down from just above the feet. Only the door map is
    // queried: zone geometry cannot move, so it can never be the thing that
    // carries you, and asking about it would just cost a traversal.
    const float probeUp = 2.0f;
    const float probeLen = probeUp + DoorMotion::kLiftRideTolerance;
    irr::core::vector3df origin(ctx_.playerX, feetZ + probeUp, ctx_.playerY);
    irr::core::vector3df dir(0.0f, -1.0f, 0.0f);

    // The BSP region the ray is allowed to look in must be the one the PLAYER
    // stands in, derived exactly as findGroundZBspFiltered() derives it
    // (renderer_player_collision.cpp). ctx_.currentPvsRegion is the CAMERA's
    // PVS region and is routinely a different region -- or SIZE_MAX -- so a
    // probe keyed on it silently finds nothing. Measured: the gfaydark lift 69
    // sits in region 1883 and the first version of this body never matched it.
    const size_t probeRegion = ctx_.zoneBspTree
        ? ctx_.zoneBspTree->findRegionIndexForPoint(ctx_.playerX, ctx_.playerY, feetZ)
        : ctx_.currentPvsRegion;
    if (probeRegion == SIZE_MAX) return;

    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    float hitDistSq = 0.0f;
    uint8_t hitDoorId = 0;
    const bool hit = RayTestDoorCollision(
        ctx_.doorCollisionData,
        [doorManager](uint8_t doorId) {
            DoorRuntimeState state;
            const DoorVisual* door = doorManager ? doorManager->getDoor(doorId) : nullptr;
            if (door) {
                state.known = true;
                state.isOpen = door->isOpen;
                state.isAnimating = door->isAnimating;
                state.isLift = door->isLift;
            }
            return state;
        },
        origin, dir, probeLen, probeRegion,
        hitPoint, hitTriangle, hitDistSq, false, &hitDoorId);
    if (!hit) return;

    float delta = 0.0f;
    for (const auto& [doorId, moved] : movers) {
        if (doorId == hitDoorId) {
            delta = moved;
            break;
        }
    }
    if (delta == 0.0f) return;
    if (!PlayerRidesLift(feetZ, hitPoint.Y)) return;

    ctx_.playerZ += delta;
    playerPositionIsSeed_ = false;

    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->updatePlayerEntityPosition(ctx_.playerX, ctx_.playerY,
                                                        ctx_.playerZ, ctx_.playerHeading);
    }
    if (cameraMode_ == CameraMode::Chase && ctx_.cameraController) {
        ctx_.cameraController->setFollowPosition(ctx_.playerX, ctx_.playerY, ctx_.playerZ,
                                                 ctx_.playerHeading, deltaTime);
    } else if (cameraMode_ == CameraMode::FirstPerson) {
        repositionFirstPersonCamera();
    }

    // The server has to be told, and the ordinary movement sync will not do it:
    // it only fires on a frame with movement INPUT, and a rider is standing
    // still. Throttled to the same ~250 ms that path uses.
    liftRideSyncAccum_ += deltaTime;
    if (ctx_.bridge && liftRideSyncAccum_ >= 0.25f) {
        liftRideSyncAccum_ = 0.0f;
        eqt::events::PlayerPositionChanged intent;
        intent.x = ctx_.playerX;
        intent.y = ctx_.playerY;
        intent.z = ctx_.playerZ;
        intent.heading = ctx_.playerHeading;
        intent.seededPosition = false;
        intent.dx = 0.0f;
        intent.dy = 0.0f;
        intent.dz = delta;
        ctx_.bridge->pushIntent(std::move(intent));
    }
    LOG_TRACE(MOD_MOVEMENT, "Riding lift {}: dz={:.3f} -> playerZ={:.3f}",
              hitDoorId, delta, ctx_.playerZ);
}

}  // namespace Graphics
}  // namespace EQT
