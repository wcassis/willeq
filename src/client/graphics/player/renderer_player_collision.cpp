// renderer_player_collision.cpp -- IrrlichtRenderer movement collision, raycasts,
// ground-height queries and collision debug lines.
// Split from renderer_player_physics.cpp by
// refactor/J33_renderer_player_physics_split.md (move-only, verbatim relocation).
// updatePlayerMovement() stays in renderer_player_physics.cpp: a filename-keyed
// guard in tests/test_first_person_camera_single_formula.cpp requires it.
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/door_collision.h"
#include "client/graphics/world_line_batch.h"  // T335: the shared post-drawAll() 3D-line guard
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
bool PlayerCameraComponent::checkMovementCollision(float fromX, float fromY, float fromZ,
                                               float toX, float toY, float toZ) {
    if (!ctx_.collisionMap) {
        return true;  // No collision map = allow movement
    }

    // Check line of sight from current position to new position
    // Use configurable check height for collision detection
    float checkHeight = playerConfig_.collisionCheckHeight;
    glm::vec3 from(fromX, fromY, fromZ + checkHeight);
    glm::vec3 to(toX, toY, toZ + checkHeight);

    glm::vec3 hitLocation;
    bool clear = ctx_.collisionMap->CheckLOSWithHit(from, to, &hitLocation);

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

// T437 (B435/B436). This used to return a bare float and rewrite every
// failure to `currentZ` -- so `BEST_Z_INVALID` never reached a caller and every
// `!= BEST_Z_INVALID` guard downstream was dead code. Worse, the phantom floor
// it substituted sat at the player's own CENTRE z, and the landing then snapped
// to `centre + modelYOffset`, lifting the player by one model offset per frame
// over a column the map has no floor for. "No floor" is now an answer.
GroundZ PlayerCameraComponent::findGroundZ(float x, float y, float currentZ) {
    if (!ctx_.collisionMap) {
        return GroundZ::NoFloor();
    }

    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // PHASE 1: Short raycast to find ground near current level
    // This prevents falling through mesh gaps by looking for nearby ground first
    // Start from max step-up above current position
    glm::vec3 nearPos(x, y, currentZ + maxStepUp);
    glm::vec3 nearResult;
    float nearGroundZ = ctx_.collisionMap->FindBestZ(nearPos, &nearResult);

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
            return GroundZ::Floor(nearGroundZ);
        }
    }

    // PHASE 2: Full raycast if no nearby ground found
    // This handles cases like jumping off ledges, falling, etc.
    glm::vec3 pos(x, y, currentZ + 10.0f);  // Start slightly above
    glm::vec3 result;
    float groundZ = ctx_.collisionMap->FindBestZ(pos, &result);

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
    if (ctx_.entityRenderer && ctx_.entityRenderer->hasBoatsInZone()) {
        float boatDeckZ = ctx_.entityRenderer->findBoatDeckZ(x, y, currentZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (groundZ == BEST_Z_INVALID || boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    // Yellow line showing boat deck hit
                    irr::core::vector3df irrFrom(x, currentZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return GroundZ::Floor(boatDeckZ);
            }
        }
    }

    if (groundZ == BEST_Z_INVALID) {
        return GroundZ::NoFloor();  // No floor under this column.
    }

    return GroundZ::Floor(groundZ);
}

// --- Irrlicht-based Collision Detection (using zone mesh) ---

void PlayerCameraComponent::setupMinimalZoneCollision() {
    // Clean up old collision data
    ctx_.regionWorldTriangles.clear();
    ctx_.objectWorldTriangles.clear();
    ctx_.doorCollisionData.clear();
    // T435 (B425): a clear without this silently strands every already-created
    // door outside collision for the rest of the zone.
    if (ctx_.doorManager) ctx_.doorManager->markAllCollisionDirty();
    // Legacy meta selectors (only used by HCMap fallback path now)
    if (ctx_.ZoneLifecycle().zoneTriangleSelector()) {
        ctx_.ZoneLifecycle().zoneTriangleSelector()->drop();
        ctx_.ZoneLifecycle().zoneTriangleSelector() = nullptr;
    }
    if (ctx_.ZoneLifecycle().terrainOnlySelector()) {
        ctx_.ZoneLifecycle().terrainOnlySelector()->drop();
        ctx_.ZoneLifecycle().terrainOnlySelector() = nullptr;
    }

    if (!ctx_.smgr) {
        return;
    }

    // Build player's BSP region mesh synchronously (only if not already loaded)
    size_t playerRegion = SIZE_MAX;
    if (ctx_.zoneBspTree) {
        playerRegion = ctx_.zoneBspTree->findRegionIndexForPoint(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
        bool alreadyBuilt = ctx_.regionMeshNodes.count(playerRegion) && ctx_.regionMeshNodes[playerRegion];
        if (playerRegion != SIZE_MAX && !alreadyBuilt) {
            ctx_.ZoneLifecycle().rebuildRegionMesh(playerRegion);
            LOG_INFO(MOD_GRAPHICS, "Built player BSP region {} synchronously for deferred loading", playerRegion);
        }
    }

    // Build pre-transformed world-space triangles per region for BSP-filtered collision
    // All collision paths (movement, detail, footprints, LOS) use these via BSP region lookup
    // Direct Möller–Trumbore ray-triangle intersection — no matrix math per query
    size_t regionsAdded = 0;
    size_t totalRegionTriangles = 0;
    for (const auto& [regionIdx, node] : ctx_.regionMeshNodes) {
        if (!node || !node->getMesh()) continue;
        auto triangles = extractWorldTriangles(ctx_.smgr, node);
        if (!triangles.empty()) {
            totalRegionTriangles += triangles.size();
            regionsAdded++;
            ctx_.regionWorldTriangles[regionIdx] = std::move(triangles);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Collision setup: {} regions (total {} world-space triangles)",
             regionsAdded, totalRegionTriangles);

    ctx_.collisionManager = ctx_.smgr->getSceneCollisionManager();

    // Camera collision uses BSP directly (no triangle selectors)

    // Activate progressive loading
    ctx_.progressiveLoadingActive = true;
    ctx_.progressiveLoadStartTime = std::chrono::steady_clock::now();

    // NOTE: EntityPrepWorker is NOT created here — it is created during the
    // P08_Entities phase (beginZoneAssetLoad) which runs after P09_Collision.
    // Creating it here would bypass the manual load gate.

    LOG_INFO(MOD_GRAPHICS, "Minimal collision setup complete, progressive loading activated");
}

bool PlayerCameraComponent::checkCollisionIrrlicht(const irr::core::vector3df& start,
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

GroundZ PlayerCameraComponent::findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset) {
    // DIAGNOSTIC: This should NOT be called on the hot path anymore.
    // BSP-filtered collision should be used instead for movement.
    LOG_WARN(MOD_GRAPHICS, "findGroundZIrrlicht CALLED — old meta selector path! "
             "pos ({},{},{})", x, y, currentZ);
    return GroundZ::Floor(currentZ - modelYOffset);
}

// updateNameTagsWithLOS removed — name tag visibility now computed by SimulationWorker

// --- BSP-Filtered Collision (hot path: movement) ---
// Uses pre-transformed world-space triangles per region — no matrix math.
// Door/object selector still uses Irrlicht (few triangles, dynamic transforms).

std::vector<irr::core::triangle3df> PlayerCameraComponent::extractWorldTriangles(
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

bool PlayerCameraComponent::rayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                                              const irr::core::vector3df& rayDir, float rayLen,
                                              const std::vector<irr::core::triangle3df>& triangles,
                                              irr::core::vector3df& hitPoint,
                                              irr::core::triangle3df& hitTriangle,
                                              float& hitDistSq) {
    // T435: the Moller-Trumbore body moved to door_collision.cpp so the door
    // query can run without a renderer. This static member is kept because
    // renderer_zone_load_env.cpp calls it by that name.
    return RayIntersectTriangles(rayOrigin, rayDir, rayLen, triangles,
                                 hitPoint, hitTriangle, hitDistSq);
}

bool PlayerCameraComponent::rayTestDoorsAndObjects(const irr::core::vector3df& rayOrigin,
                                                const irr::core::vector3df& rayDir, float rayLen,
                                                size_t bspRegion,
                                                irr::core::vector3df& hitPoint,
                                                irr::core::triangle3df& hitTriangle,
                                                float& hitDistSq) {
    bool anyHit = false;

    // Test objects in this BSP region
    auto objIt = ctx_.objectWorldTriangles.find(bspRegion);
    if (objIt != ctx_.objectWorldTriangles.end()) {
        if (rayIntersectTriangles(rayOrigin, rayDir, rayLen, objIt->second, hitPoint, hitTriangle, hitDistSq)) {
            anyHit = true;
        }
    }

    // Test doors in this BSP region. T435 (board B425): until this unit
    // ctx_.doorCollisionData had no writer anywhere in the tree, so this loop
    // ran over an always-empty map and every door in the game was
    // walk-through and non-standable. syncDoorCollision() is the writer.
    DoorManager* doorManager = ctx_.doorManager.get();
    anyHit = RayTestDoorCollision(
        ctx_.doorCollisionData,
        [doorManager](uint8_t doorId) {
            DoorRuntimeState state;
            const DoorVisual* door = doorManager ? doorManager->getDoor(doorId) : nullptr;
            if (door) {
                state.known = true;
                state.isOpen = door->isOpen;
                state.isAnimating = door->isAnimating;
                state.isLift = door->isLift;  // T438 (B423): rides, not swings
            }
            return state;
        },
        rayOrigin, rayDir, rayLen, bspRegion, hitPoint, hitTriangle, hitDistSq, anyHit);

    return anyHit;
}

// T435 (board B425). The writer ctx_.doorCollisionData never had. Called once
// per frame right after DoorManager::update(), and again wherever the map is
// cleared (each of those calls markAllCollisionDirty() so the next drain
// re-offers every door).
void PlayerCameraComponent::syncDoorCollision() {
    if (!ctx_.doorManager) return;

    std::vector<uint8_t> changed;
    std::vector<uint8_t> removed;
    ctx_.doorManager->drainCollisionUpdates(changed, removed);
    if (changed.empty() && removed.empty()) return;

    for (uint8_t doorId : removed) {
        ctx_.doorCollisionData.erase(doorId);
    }
    for (uint8_t doorId : changed) {
        DoorCollisionData data;
        if (ctx_.doorManager->computeDoorCollision(doorId, data)) {
            ctx_.doorCollisionData[doorId] = std::move(data);
        } else {
            // No geometry yet (registered, mesh not built). It will be offered
            // again by buildDoorMesh().
            ctx_.doorCollisionData.erase(doorId);
        }
    }
    LOG_TRACE(MOD_GRAPHICS, "syncDoorCollision: +{} -{} (map now {})",
              changed.size(), removed.size(), ctx_.doorCollisionData.size());
}

bool PlayerCameraComponent::checkCollisionBspFiltered(const irr::core::vector3df& start,
                                                  const irr::core::vector3df& end,
                                                  irr::core::vector3df& hitPoint,
                                                  irr::core::triangle3df& hitTriangle) {
    if (!ctx_.zoneBspTree) return false;

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
    size_t srcRegion = ctx_.zoneBspTree->findRegionIndexForPoint(start.X, start.Z, start.Y);
    size_t dstRegion = ctx_.zoneBspTree->findRegionIndexForPoint(end.X, end.Z, end.Y);

    // Test source region triangles (direct, no matrix math)
    if (srcRegion != SIZE_MAX) {
        auto it = ctx_.regionWorldTriangles.find(srcRegion);
        if (it != ctx_.regionWorldTriangles.end()) {
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
        auto it = ctx_.regionWorldTriangles.find(dstRegion);
        if (it != ctx_.regionWorldTriangles.end()) {
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

GroundZ PlayerCameraComponent::findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset) {
    if (!ctx_.zoneBspTree) {
        // No BSP data to ask: "the floor is right under your feet" is what this
        // has always answered, and it keeps a standing player standing.
        return GroundZ::Floor(currentZ - modelYOffset);
    }

    float feetZ = currentZ - modelYOffset;
    float headZ = currentZ + modelYOffset;
    float maxStepUp = playerConfig_.collisionStepHeight;
    float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;

    // Find BSP region at query point
    size_t queryRegion = ctx_.zoneBspTree->findRegionIndexForPoint(x, y, currentZ);

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
        auto it = ctx_.regionWorldTriangles.find(queryRegion);
        if (it != ctx_.regionWorldTriangles.end()) {
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
            return GroundZ::Floor(floorZ);
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
        auto it = ctx_.regionWorldTriangles.find(queryRegion);
        if (it != ctx_.regionWorldTriangles.end()) {
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
            // T437 (B435): the ray met geometry ABOVE the step-up limit -- a
            // ceiling, an overhang, the underside of a platform. This used to
            // return `feetZ + 1000.0f`, a float the four jump landings happily
            // accepted as ground.
            return GroundZ::NoFloor();
        }
    }

    // Check for boat collision — only when boats exist in zone
    if (ctx_.entityRenderer && ctx_.entityRenderer->hasBoatsInZone()) {
        float boatDeckZ = ctx_.entityRenderer->findBoatDeckZ(x, y, feetZ);
        if (boatDeckZ != BEST_Z_INVALID) {
            if (boatDeckZ > groundZ) {
                if (playerConfig_.collisionDebug) {
                    irr::core::vector3df irrFrom(x, feetZ, y);
                    irr::core::vector3df irrTo(x, boatDeckZ, y);
                    addCollisionDebugLine(irrFrom, irrTo, irr::video::SColor(255, 255, 255, 0), 0.3f);
                }
                return GroundZ::Floor(boatDeckZ);
            }
        }
    }

    return GroundZ::Floor(groundZ);
}

// --- Collision Debug Visualization ---

void PlayerCameraComponent::addCollisionDebugLine(const irr::core::vector3df& start,
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

// T335 test seam -- see the declaration in player_camera_component.h.
void PlayerCameraComponent::seedCollisionDebugLineForTest(const irr::core::vector3df& start,
                                                          const irr::core::vector3df& end,
                                                          const irr::video::SColor& color,
                                                          float duration) {
    addCollisionDebugLine(start, end, color, duration);
}

void PlayerCameraComponent::drawCollisionDebugLines(float deltaTime) {
    if (!ctx_.driver || collisionDebugLines_.empty()) {
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
    // T335 (B054): these lines are drawn in the debug-overlay pass, after
    // smgr->drawAll(), with the GLES2 driver latched in 2D mode -- B048's
    // defect verbatim. Without the batch the world-space segments are
    // multiplied by the 2D ortho matrix (nothing draws) and the 3D material is
    // left installed (the UI drawn afterwards is back-face culled away). See
    // client/graphics/world_line_batch.h.
    WorldLineBatch batch(ctx_, lineMaterial);
    ctx_.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Draw all lines and update their timers
    auto it = collisionDebugLines_.begin();
    while (it != collisionDebugLines_.end()) {
        ctx_.driver->draw3DLine(it->start, it->end, it->color);

        // Update timer
        it->timeRemaining -= deltaTime;
        if (it->timeRemaining <= 0) {
            it = collisionDebugLines_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Graphics
} // namespace EQT
