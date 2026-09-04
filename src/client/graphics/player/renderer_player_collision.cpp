// renderer_player_collision.cpp -- IrrlichtRenderer movement collision, raycasts,
// ground-height queries and collision debug lines.
// Split from renderer_player_physics.cpp by
// refactor/J33_renderer_player_physics_split.md (move-only, verbatim relocation).
// updatePlayerMovement() stays in renderer_player_physics.cpp: a filename-keyed
// guard in tests/test_first_person_camera_single_formula.cpp requires it.
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

float PlayerCameraComponent::findGroundZ(float x, float y, float currentZ) {
    if (!ctx_.collisionMap) {
        return currentZ;
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
            return nearGroundZ;
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

void PlayerCameraComponent::setupMinimalZoneCollision() {
    // Clean up old collision data
    ctx_.regionWorldTriangles.clear();
    ctx_.objectWorldTriangles.clear();
    ctx_.doorCollisionData.clear();
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

size_t PlayerCameraComponent::findBspRegionForPoint(float x, float y, float z) {
    if (ctx_.zoneBspTree) {
        return ctx_.zoneBspTree->findRegionIndexForPoint(x, y, z);
    }
    return SIZE_MAX;
}

void PlayerCameraComponent::addRegionToCollision(size_t regionIdx) {
    if (!ctx_.smgr) return;

    auto regionIt = ctx_.regionMeshNodes.find(regionIdx);
    if (regionIt == ctx_.regionMeshNodes.end() || !regionIt->second || !regionIt->second->getMesh()) return;

    auto triangles = extractWorldTriangles(ctx_.smgr, regionIt->second);
    if (!triangles.empty()) {
        ctx_.regionWorldTriangles[regionIdx] = std::move(triangles);
    }
}

void PlayerCameraComponent::addDoorToCollision(uint8_t doorId) {
    if (!ctx_.smgr || !ctx_.doorManager) return;

    const auto* door = ctx_.doorManager->getDoor(doorId);
    if (!door || !door->sceneNode || !door->sceneNode->getMesh() || !door->pivotNode) return;

    IrrlichtRenderer::DoorCollisionData data;
    data.bspRegion = door->bspRegion;

    // Classify door type — skip collision for spinning, lifts, hazardous, invisible, teleporters
    uint8_t ot = door->opentype;
    bool isSpinning = (ot >= 30 && ot <= 44) || (ot >= 100 && ot <= 107);
    bool isLift = (ot == 59 || ot == 60);
    bool isHazardous = (ot >= 115 && ot <= 152);
    bool isInvisible = (ot == 50 || ot == 53 || ot == 54 || ot == 57);
    if (isSpinning || isLift || isHazardous || isInvisible) {
        data.hasCollision = false;
        ctx_.doorCollisionData[doorId] = std::move(data);
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
    data.closedTriangles = extractWorldTriangles(ctx_.smgr, door->sceneNode);

    // Extract OPEN state triangles
    float openIrrRot = -door->closedHeading * 360.0f / 512.0f + 90.0f;
    door->pivotNode->setRotation(irr::core::vector3df(0, openIrrRot, 0));
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();
    data.openTriangles = extractWorldTriangles(ctx_.smgr, door->sceneNode);

    // Restore original rotation
    door->pivotNode->setRotation(savedRotation);
    door->pivotNode->updateAbsolutePosition();
    door->sceneNode->updateAbsolutePosition();

    LOG_DEBUG(MOD_GRAPHICS, "Door {} opentype={} region={}: {} closed tris, {} open tris",
              doorId, ot, data.bspRegion, data.closedTriangles.size(), data.openTriangles.size());

    ctx_.doorCollisionData[doorId] = std::move(data);
}

void PlayerCameraComponent::addObjectToCollision(size_t objIdx) {
    if (!ctx_.smgr) return;
    if (objIdx >= ctx_.objectNodes.size()) return;

    auto* node = ctx_.objectNodes[objIdx];
    if (!node || !node->getMesh()) return;

    auto triangles = extractWorldTriangles(ctx_.smgr, node);
    if (triangles.empty()) return;

    // Determine BSP region for this object
    size_t region = SIZE_MAX;
    if (objIdx < ctx_.objectRegions.size()) {
        region = ctx_.objectRegions[objIdx];
    }

    // Append to per-region object triangles
    auto& regionTris = ctx_.objectWorldTriangles[region];
    regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
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

float PlayerCameraComponent::findGroundZIrrlicht(float x, float y, float currentZ, float modelYOffset) {
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

    // Test doors in this BSP region (skip animating doors — walk-through during animation)
    for (const auto& [doorId, doorData] : ctx_.doorCollisionData) {
        if (!doorData.hasCollision) continue;
        if (doorData.bspRegion != bspRegion) continue;

        // Get current door state from door manager
        const auto* door = ctx_.doorManager ? ctx_.doorManager->getDoor(doorId) : nullptr;
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

float PlayerCameraComponent::findGroundZBspFiltered(float x, float y, float currentZ, float modelYOffset) {
    if (!ctx_.zoneBspTree) {
        return currentZ - modelYOffset;  // Return current feet position
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
            return feetZ + 1000.0f;  // Ceiling sentinel
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
                return boatDeckZ;
            }
        }
    }

    return groundZ;
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
    ctx_.driver->setMaterial(lineMaterial);
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

void PlayerCameraComponent::clearCollisionDebugLines() {
    collisionDebugLines_.clear();
}



} // namespace Graphics
} // namespace EQT
