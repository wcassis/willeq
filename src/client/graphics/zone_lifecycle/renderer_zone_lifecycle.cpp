#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/sky_renderer.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace EQT {
namespace Graphics {

ZoneLifecycleComponent::ZoneLifecycleComponent(RendererContext& ctx, IrrlichtRenderer& owner)
    : ctx_(ctx), owner_(owner) {}

ZoneLifecycleComponent::~ZoneLifecycleComponent() = default;

void ZoneLifecycleComponent::unloadZone() {
    if (owner_.isLoading()) return;
    owner_.sequentialLoadComplete_ = false;
    owner_.storedZoneEnvironment_.pending = false;

    // Stop simulation worker before zone cleanup
    owner_.simBridge_->stopSimulationWorker();

    // Stop background entity prep worker before zone cleanup
    if (ctx_.entityPrepWorker) {
        ctx_.entityPrepWorker->stop();
        ctx_.entityPrepWorker.reset();
        if (ctx_.entityRenderer) {
            ctx_.entityRenderer->setEntityPrepWorker(nullptr);
        }
    }

    // Reset entity loading state - we're starting a new zone
    owner_.networkReady_ = false;
    entitiesLoaded_ = false;
    expectedEntityCount_ = 0;
    owner_.loadedEntityCount_ = 0;
    owner_.zoneReady_ = false;
    ctx_.entityPrepReady = false;
    owner_.entityPrepScanCounter_ = 0;

    // Log texture counts before cleanup
    if (ctx_.constrainedTextureCache) {
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache has {} textures, {} bytes before cleanup",
                 ctx_.constrainedTextureCache->getTextureCount(),
                 ctx_.constrainedTextureCache->getCurrentUsage());
    }

    // Remove zone placeholder mesh if still present
    destroyZonePlaceholder();

    // Unload EQE1 flat packs (clear cache refs before destroying loaders).
    // T214: all but the PERSISTENT ones. willeq.eqe1 is the client's own pack,
    // opened once at renderer init and never reopened -- destroying it here
    // made every name in it unreachable for the rest of the process. The UI
    // survived only because its textures were already uploaded, but
    // detail_atlas is requested per zone load, so the miss resolver opened
    // textures.eqe1 (2.6MB, 64 entries) to serve that single name in EVERY
    // zone. Same shape as T204's UI-atlas hazard, one layer down: a
    // process-lifetime asset torn down on a zone boundary.
    if (ctx_.constrainedTextureCache) {
        ctx_.constrainedTextureCache->clearEQE1Loaders();
    }
    {
        std::lock_guard<std::mutex> lock(owner_.eqe1Mutex_);
        for (auto it = owner_.eqe1Loaders_.begin(); it != owner_.eqe1Loaders_.end(); ) {
            if (Eqe1ArchiveComponent::isPersistentEQE1Archive(it->first) && it->second) {
                ++it;
            } else {
                it = owner_.eqe1Loaders_.erase(it);
            }
        }
        // Re-register the survivors: clearEQE1Loaders() emptied the cache's
        // vector, and they must be searchable again before the next lookup.
        if (ctx_.constrainedTextureCache) {
            for (const auto& [name, loader] : owner_.eqe1Loaders_) {
                ctx_.constrainedTextureCache->addEQE1Loader(loader.get());
            }
        }
    }
    // P04: drop the lazy-open queue too so the next zone's Step0 starts fresh.
    {
        std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
        owner_.pendingGlobalEQE1Archives_.clear();
        owner_.eqe1CandidateRaces_.clear();
        owner_.eqe1LoadTimeCandidateCount_ = 0;
    }

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
    // Release retained GLES2 textures and EGL images after atlas GL textures are deleted.
    // Lima driver requires source textures/images to stay alive while desktop GL textures
    // are in use, so we release them only after unload().
    if (gles2Helper_) {
        gles2Helper_->releaseSharedResources();
    }
#endif

    // Reset animated texture manager (removes its textures from driver)
    ctx_.animatedTextureManager.reset();

    // Clear camera collision FIRST to prevent use-after-free during zone transitions
    if (ctx_.cameraController) {
        ctx_.cameraController->setBspCollision(nullptr, nullptr);
        ctx_.cameraController->setCollisionManager(nullptr, nullptr);
    }

    // Clear detail system BEFORE dropping collision selector
    if (ctx_.detailManager) {
        ctx_.detailManager->onZoneExit();
    }

    // P32: AnimatedTreeManager::cleanup() (scene nodes/meshes owned by the
    // CPU-instancing animatedTrees_ list, always empty on GLES2) removed
    // along with the class. treeWindController_/treeIdentifier_ own no
    // Irrlicht scene-graph resources, so there is nothing to clear here.

    // Clear environmental particle system
    if (ctx_.particleManager) {
        ctx_.particleManager->onZoneLeave();
    }

    // Clear ambient creatures (boids) system
    if (ctx_.boidsManager) {
        ctx_.boidsManager->onZoneLeave();
    }

    // Clear tumbleweed system
    if (ctx_.tumbleweedManager) {
        ctx_.tumbleweedManager->onZoneLeave();
    }

    // Now safe to remove zone collision data
    owner_.regionWorldTriangles_.clear();
    owner_.objectWorldTriangles_.clear();
    owner_.doorCollisionData_.clear();
    if (owner_.zoneTriangleSelector_) {
        owner_.zoneTriangleSelector_->drop();
        owner_.zoneTriangleSelector_ = nullptr;
    }
    if (owner_.terrainOnlySelector_) {
        owner_.terrainOnlySelector_->drop();
        owner_.terrainOnlySelector_ = nullptr;
    }

    // Remove zone mesh
    if (owner_.zoneMeshNode_) {
        deleteMeshHardwareBuffers(owner_.zoneMeshNode_);
        owner_.zoneMeshNode_->remove();
        owner_.zoneMeshNode_ = nullptr;
    }

    // Remove PVS region mesh nodes (delete VBOs before removing nodes)
    for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
        if (node) {
            deleteMeshHardwareBuffers(node);
            if (node->getParent()) node->remove(); else node->drop();
        }
    }
    ctx_.regionMeshNodes.clear();
    ctx_.regionBoundingBoxes.clear();

    if (owner_.fallbackMeshNode_) {
        deleteMeshHardwareBuffers(owner_.fallbackMeshNode_);
        if (owner_.fallbackMeshNode_->getParent()) owner_.fallbackMeshNode_->remove(); else owner_.fallbackMeshNode_->drop();
        owner_.fallbackMeshNode_ = nullptr;
    }

    // Remove collision-only node (used in PVS mode)
    if (owner_.zoneCollisionNode_) {
        owner_.zoneCollisionNode_->remove();
        owner_.zoneCollisionNode_ = nullptr;
    }

    // Reset PVS state
    ctx_.usePvsCulling = false;
    ctx_.zoneBspTree.reset();
    ctx_.currentPvsRegion = SIZE_MAX;

    // Reset manual zone draw state
    ctx_.manualZoneDrawEnabled = false;
    owner_.sortedZoneDrawList_.clear();
    owner_.sortedDrawEntries_.clear();

    // P10: reset cross-region zone draw merge state — the merge buffers are
    // scoped to a single zone visit's region set; destroy them and clear the
    // "disabled for this zone" latch so the next zone gets a fresh attempt.
    if (owner_.zoneMergeBuffers_) owner_.zoneMergeBuffers_->destroy();
    owner_.mergeZoneDrawsDisabledForZone_ = false;
    owner_.mergeZoneDrawsBuildEvictionCount_ = 0;
    ctx_.portalSystem.reset();
    ctx_.portalOcclusionEnabled = false;
    ctx_.portalOcclusionEligible = false;
    owner_.portalBuildPending_ = false;
    owner_.regionNeighbors_.clear();

    // ConstrainedMeshCache and its background-build state (BackgroundMeshResult
    // queue, material swap queue, meshLoadQueue_, etc.) removed T67 — the
    // cache was never constructed in any shipped build; see
    // src/client/graphics/CLAUDE.md for the reference pointer.
    // T292: protectedRegions_ removed entirely -- it was the cache's
    // eviction-protection bitvector, had no writer left after T67, and its
    // remaining readers were permanent no-ops.
    owner_.portalVisibleRegions_ = nullptr;
    ctx_.indoorRegions.clear();
    owner_.regionPvsDepthMap_ = nullptr;
    // T116: particleRenderBuffer_/currentFrameSimResults_ were the two
    // members of this same "pointer into SimulationOutput" class missing
    // from this block (found while scoping T116). Post-split, all four
    // members can only ever be bound by bindFrameSimulationBuffers() to
    // SimulationWorker's persistent, never-reset output_[2] storage, so
    // leaving them unset here is NOT a dangling-pointer hazard (unlike the
    // pre-split load-path case this whole unit exists to fix) -- the worker
    // outlives every zone. It is a staleness inconsistency: without this,
    // a frame rendered in the unload/load gap could read the previous
    // zone's last particle buffer or occlusion-culled-region set. Nulled
    // here for the same reason the other two already were, not because
    // leaving them would be unsafe.
    owner_.particleRenderBuffer_ = nullptr;
    owner_.currentFrameSimResults_ = nullptr;
    owner_.pvsNeighborhood_.clear();
    owner_.pvsNeighborhoodAnchor_ = SIZE_MAX;
    pendingTextureRegions_.clear();
    owner_.textureRebuildQueue_.clear();
    owner_.pendingTextureDoors_.clear();
    owner_.doorTextureRebuildQueue_.clear();
    owner_.pendingTextureObjects_.clear();
    owner_.objectTextureRebuildQueue_.clear();
#ifdef EQT_HAS_GLES2
    owner_.pendingGPUResults_.clear();
#endif

    // Clear occlusion culler data
    if (ctx_.occlusionCuller) {
        ctx_.occlusionCuller->clearOccluders();
    }
    owner_.occlusionCulledRegions_.clear();

    // Reset zone clip plane so previous zone's cap doesn't linger
    owner_.zoneMaxClip_ = 99999.0f;
    owner_.setRenderDistance(owner_.userRenderDistance_);

    // Clear BSP tree from entity renderer and door manager
    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->clearBspTree();
    }
    if (ctx_.doorManager) {
        ctx_.doorManager->setBspTree(nullptr);
        ctx_.doorManager->setPvsRegion(SIZE_MAX);
        ctx_.doorManager->setOcclusionCulledRegions(nullptr);
        ctx_.doorManager->setRegionNeighbors(nullptr);
    }

    // Remove object nodes
    for (size_t i = 0; i < ctx_.objectNodes.size(); ++i) {
        if (ctx_.objectNodes[i]) {
            if (i < ctx_.objectInSceneGraph.size() && ctx_.objectInSceneGraph[i]) {
                ctx_.objectNodes[i]->remove();
            }
            ctx_.objectNodes[i]->drop();  // Release our reference
        }
    }
    ctx_.objectNodes.clear();
    ctx_.objectPositions.clear();
    owner_.objectBoundingBoxes_.clear();
    ctx_.objectInSceneGraph.clear();

    // Clear zone light data (no scene nodes to remove)
    owner_.zoneLightData_.clear();
    owner_.zoneLightPositions_.clear();
    // Clear entity renderer
    if (ctx_.entityRenderer) {
        // Clear mesh caches first to force fresh mesh/texture rebuild on next zone-in.
        // Keeps model data (geometry, skeletons, S3D archives) cached for performance.
        // Fixes garbled player textures on re-zone caused by stale texture pointers
        // in cached animated meshes.
        if (auto* rml = ctx_.entityRenderer->getRaceModelLoader()) {
            rml->clearMeshCaches();
        }
        ctx_.entityRenderer->clearEntities();
    }

    // Clear door manager
    if (ctx_.doorManager) {
        ctx_.doorManager->clearDoors();
        ctx_.doorManager->setZone(nullptr);
    }

    // Disable sky when unloading zone (will be re-enabled when new zone loads)
    if (ctx_.skyRenderer) {
        ctx_.skyRenderer->setEnabled(false);
    }

    // Clear world objects (tradeskill containers)
    owner_.clearWorldObjects();

    // Clear object lights (they reference freed zone data)
    for (size_t i = 0; i < ctx_.objectLights.size(); ++i) {
        if (ctx_.objectLights[i].node) {
            bool isInScene = (i < owner_.objectLightInSceneGraph_.size()) ? owner_.objectLightInSceneGraph_[i] : false;
            if (isInScene) {
                ctx_.objectLights[i].node->remove();
            }
            ctx_.objectLights[i].node->drop();
        }
    }
    ctx_.objectLights.clear();
    owner_.objectLightInSceneGraph_.clear();

    // Clear vertex animated meshes (they reference freed zone mesh buffers)
    owner_.vertexAnimatedMeshes_.clear();

    // Clear zone line visualization boxes
    owner_.overlays_->clearZoneLineBoundingBoxes();

    // Clear constrained texture cache to free old zone textures from driver
    if (ctx_.constrainedTextureCache) {
        ctx_.constrainedTextureCache->unfreeze();
        ctx_.constrainedTextureCache->clear();
        ctx_.constrainedTextureCache->resetStatistics();
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache cleared");
    }

    ctx_.currentZone.reset();
    ctx_.currentZoneName.clear();
    owner_.isIndoorZone_ = false;
    owner_.zoneBoundsValid_ = false;
}

void ZoneLifecycleComponent::setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                          const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                          const float fogMinClip[4], const float fogMaxClip[4]) {
    // Set sky type if sky renderer is available
    if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized()) {
        ctx_.skyRenderer->setSkyType(skyType, ctx_.currentZoneName);

        // Determine if sky should be shown based on zone type and user settings
        // zoneType values from server: 1=Outdoors, 2=Dungeons, 255(0xFF)=Any/default
        // Only disable sky for explicit dungeon zones (type 2)
        bool isDungeon = (zoneType == 2);
        owner_.isIndoorZone_ = isDungeon;

        // Check user setting and constrained config
        bool skySettingEnabled = owner_.getDisplaySettings().skyEnabled;
        bool skyAllowed = !isDungeon && skySettingEnabled && ctx_.config.constrainedConfig.skyRendering;
        ctx_.skyRenderer->setEnabled(skyAllowed);

        LOG_DEBUG(MOD_GRAPHICS, "Zone environment: sky type {}, zone type {} ({}), sky {} (setting={}, constrained={})",
                  skyType, zoneType, isDungeon ? "dungeon" : "outdoor",
                  skyAllowed ? "enabled" : "disabled",
                  skySettingEnabled ? "on" : "off",
                  ctx_.config.constrainedConfig.skyRendering ? "on" : "off");
    }

    // Apply server-provided max clip plane as ceiling on render distance
    // fogMaxClip[0] is the primary clip distance for the zone
    owner_.zoneMaxClip_ = (fogMaxClip[0] > 0.0f) ? fogMaxClip[0] : 99999.0f;
    // Re-apply render distance with the new zone cap
    owner_.setRenderDistance(owner_.userRenderDistance_);

    LOG_INFO(MOD_GRAPHICS, "Zone max clip plane: {:.0f} (effective render distance: {:.0f}, user setting: {:.0f})",
             owner_.zoneMaxClip_, ctx_.renderDistance, owner_.userRenderDistance_);

    // Apply fog color from zone data
    if (ctx_.driver && ctx_.fogEnabled) {
        irr::video::SColor fogColor(255, fogRed[0], fogGreen[0], fogBlue[0]);

        float fogEnd = ctx_.renderDistance;
        float fogStart = ctx_.renderDistance - owner_.fogThickness_;
        fogStart = std::max(0.0f, fogStart);

        ctx_.driver->setFog(
            fogColor,
            irr::video::EFT_FOG_LINEAR,
            fogStart,
            fogEnd,
            0.0f,  // Density (unused for linear fog)
            true,  // Pixel fog
            false  // Range fog
        );

        LOG_DEBUG(MOD_GRAPHICS, "Zone fog color: RGB({},{},{}), distances: {:.0f}-{:.0f} (renderDistance={:.0f})",
                  fogRed[0], fogGreen[0], fogBlue[0], fogStart, fogEnd, ctx_.renderDistance);
    }
}

void ZoneLifecycleComponent::buildZonePlaceholder(float playerIrrX, float playerIrrY, float playerIrrZ) {
    if (!ctx_.collisionMap || !ctx_.collisionMap->IsLoaded() || !ctx_.smgr || !ctx_.driver) return;

    // Destroy existing node for rebuild
    if (owner_.zonePlaceholderNode_) {
        owner_.zonePlaceholderNode_->remove();
        owner_.zonePlaceholderNode_ = nullptr;
    }

    // Cache terrain triangles on first call (GetAllTerrainTriangles is expensive)
    if (cachedPlaceholderTriangles_.empty()) {
        cachedPlaceholderTriangles_ = ctx_.collisionMap->GetAllTerrainTriangles();
        if (cachedPlaceholderTriangles_.empty()) return;
    }

    const auto& allTriangles = cachedPlaceholderTriangles_;

    // Filter triangles to render distance from player position (Irrlicht Y-up coords)
    float clipDist = ctx_.renderDistance > 0.0f ? ctx_.renderDistance : 300.0f;
    float clipDistSq = clipDist * clipDist;

    // PVS filtering: if BSP tree is available (from BSP preload), skip triangles
    // in BSP regions not visible from the camera's current region.
    // Centroid coords: Irrlicht Y-up (cx, cy, cz) → EQ Z-up (cx, cz, cy)
    bool usePvsFilter = false;
    std::shared_ptr<BspRegion> camRegion;
    if (ctx_.zoneBspTree) {
        // Use currentPvsRegion_ if already computed, otherwise derive from player position
        // (handles the case where BSP preload just completed but PVS update hasn't run yet)
        size_t cameraRegion = ctx_.currentPvsRegion;
        if (cameraRegion == SIZE_MAX) {
            // Irrlicht Y-up (x, y, z) → EQ Z-up (x, z, y)
            cameraRegion = ctx_.zoneBspTree->findRegionIndexForPoint(playerIrrX, playerIrrZ, playerIrrY);
        }
        if (cameraRegion != SIZE_MAX && cameraRegion < ctx_.zoneBspTree->regions.size()) {
            camRegion = ctx_.zoneBspTree->regions[cameraRegion];
            if (camRegion && !camRegion->visibleRegions.empty()) {
                usePvsFilter = true;
            }
        }
    }

    std::vector<HCMap::Triangle> triangles;
    triangles.reserve(allTriangles.size() / 2);
    size_t pvsCulledCount = 0;
    for (const auto& tri : allTriangles) {
        // Use triangle centroid for distance check
        float cx = (tri.v1.x + tri.v2.x + tri.v3.x) / 3.0f;
        float cy = (tri.v1.y + tri.v2.y + tri.v3.y) / 3.0f;
        float cz = (tri.v1.z + tri.v2.z + tri.v3.z) / 3.0f;
        float dx = cx - playerIrrX;
        float dy = cy - playerIrrY;
        float dz = cz - playerIrrZ;
        if (dx * dx + dy * dy + dz * dz > clipDistSq) {
            continue;
        }

        // PVS check: convert Irrlicht Y-up centroid to EQ Z-up for BSP lookup
        if (usePvsFilter) {
            size_t triRegion = ctx_.zoneBspTree->findRegionIndexForPoint(cx, cz, cy);
            if (triRegion != SIZE_MAX && triRegion < camRegion->visibleRegions.size()
                && !camRegion->visibleRegions[triRegion]) {
                pvsCulledCount++;
                continue;
            }
        }

        triangles.push_back(tri);
    }

    if (triangles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Zone placeholder: 0 triangles within render distance {:.0f} of player ({:.0f},{:.0f},{:.0f})",
                 clipDist, playerIrrX, playerIrrY, playerIrrZ);
        return;
    }

    // Build a single mesh buffer from filtered terrain triangles
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    buf->Vertices.set_used(triangles.size() * 3);
    buf->Indices.set_used(triangles.size() * 3);

    for (size_t i = 0; i < triangles.size(); ++i) {
        const auto& tri = triangles[i];
        const glm::vec3* verts[3] = { &tri.v1, &tri.v2, &tri.v3 };

        // Face normal orientation determines base color:
        // - Floors (normal.y > 0.7): green/brown terrain tones
        // - Walls (|normal.y| < 0.3): gray/blue stone tones
        // - Ceilings (normal.y < -0.7): dark purple
        // - Slopes: blended between categories
        float ny = tri.normal.y;  // Y-up: vertical component
        uint8_t r, g, b;

        if (ny > 0.7f) {
            // Floor: earthy green-brown, brighter = more horizontal
            uint8_t base = static_cast<uint8_t>(100 + ny * 60);
            r = static_cast<uint8_t>(base * 0.7f);
            g = base;
            b = static_cast<uint8_t>(base * 0.5f);
        } else if (ny < -0.7f) {
            // Ceiling: dark muted purple
            uint8_t base = static_cast<uint8_t>(60 + (-ny - 0.7f) * 40);
            r = static_cast<uint8_t>(base * 0.8f);
            g = static_cast<uint8_t>(base * 0.5f);
            b = base;
        } else {
            // Wall: gray-blue, vary by which horizontal direction the wall faces
            float nx = std::abs(tri.normal.x);
            float nz = std::abs(tri.normal.z);
            uint8_t base = static_cast<uint8_t>(90 + (nx + nz) * 50);
            // X-facing walls slightly warmer, Z-facing walls slightly cooler
            r = static_cast<uint8_t>(base * (0.7f + nx * 0.2f));
            g = static_cast<uint8_t>(base * 0.75f);
            b = static_cast<uint8_t>(base * (0.8f + nz * 0.2f));
        }

        // Per-vertex height variation within each triangle for edge visibility
        float minVY = std::min({verts[0]->y, verts[1]->y, verts[2]->y});
        float maxVY = std::max({verts[0]->y, verts[1]->y, verts[2]->y});
        float triYRange = maxVY - minVY;

        for (int j = 0; j < 3; ++j) {
            uint32_t idx = static_cast<uint32_t>(i * 3 + j);
            auto& vert = buf->Vertices[idx];
            vert.Pos.X = verts[j]->x;
            vert.Pos.Y = verts[j]->y;
            vert.Pos.Z = verts[j]->z;
            vert.Normal.X = tri.normal.x;
            vert.Normal.Y = tri.normal.y;
            vert.Normal.Z = tri.normal.z;
            vert.TCoords.X = 0;
            vert.TCoords.Y = 0;

            // Darken lower vertices within each triangle for edge definition
            float heightMod = 1.0f;
            if (triYRange > 0.1f) {
                heightMod = 0.85f + 0.15f * ((verts[j]->y - minVY) / triYRange);
            }

            vert.Color = irr::video::SColor(255,
                static_cast<uint8_t>(std::min(255.0f, r * heightMod)),
                static_cast<uint8_t>(std::min(255.0f, g * heightMod)),
                static_cast<uint8_t>(std::min(255.0f, b * heightMod)));

            buf->Indices[idx] = static_cast<irr::u16>(idx);
        }
    }

    buf->recalculateBoundingBox();
    buf->setHardwareMappingHint(irr::scene::EHM_STATIC);

    // Material: unlit, solid, no backface culling
    buf->Material.Lighting = false;
    buf->Material.MaterialType = irr::video::EMT_SOLID;
    buf->Material.BackfaceCulling = false;
#ifndef EQT_HAS_GLES2
    // Desktop GL: wireframe for classic look
    buf->Material.Wireframe = true;
#endif

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();

    owner_.zonePlaceholderNode_ = ctx_.smgr->addMeshSceneNode(mesh);
    mesh->drop();

    if (owner_.zonePlaceholderNode_) {
        owner_.zonePlaceholderNode_->setPosition(irr::core::vector3df(0, 0, 0));
        owner_.lastPlaceholderBuildPos_ = irr::core::vector3df(playerIrrX, playerIrrY, playerIrrZ);
        if (usePvsFilter) {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles (PVS culled {}, clip {:.0f}, {} total, {:.1f} KB)",
                     triangles.size(), pvsCulledCount, clipDist, cachedPlaceholderTriangles_.size(),
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        } else {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles within clip {:.0f} ({} total, {:.1f} KB)",
                     triangles.size(), clipDist, cachedPlaceholderTriangles_.size(),
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        }
    }
}

void ZoneLifecycleComponent::destroyZonePlaceholder() {
    if (owner_.zonePlaceholderNode_) {
        owner_.zonePlaceholderNode_->remove();
        owner_.zonePlaceholderNode_ = nullptr;
        LOG_INFO(MOD_GRAPHICS, "Zone placeholder removed");
    }
    // Free cached triangles — no longer needed once real geometry takes over
    if (!cachedPlaceholderTriangles_.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Releasing {} cached placeholder triangles ({:.1f} KB)",
                  cachedPlaceholderTriangles_.size(),
                  (cachedPlaceholderTriangles_.size() * sizeof(HCMap::Triangle)) / 1024.0f);
        cachedPlaceholderTriangles_.clear();
        cachedPlaceholderTriangles_.shrink_to_fit();
    }
}

void ZoneLifecycleComponent::setupInstantScene(const std::string& zoneName, float playerX, float playerY, float playerZ) {
    ctx_.currentZoneName = zoneName;
    owner_.diagnostics_->loadIndoorRegionMap(zoneName);

    // S05: entityRenderer_ already created in initLoadingScreen()

    // Create door manager for placeholder cubes (no model loading yet)
    if (!ctx_.doorManager && ctx_.smgr && ctx_.driver) {
        ctx_.doorManager = std::make_unique<DoorManager>(ctx_.smgr, ctx_.driver);
        LOG_INFO(MOD_GRAPHICS, "Door manager created for instant scene (placeholder cubes only)");
    }

    // Notify entity renderer of zone (for zone-specific model search paths)
    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->setCurrentZone(zoneName);
    }

    // Build HCMap placeholder around player position, clipped to render distance
    if (ctx_.collisionMap && ctx_.collisionMap->IsLoaded()) {
        // Convert EQ coords (Z-up) to Irrlicht (Y-up) for the builder
        buildZonePlaceholder(playerX, playerZ, playerY);
    }

    // Minimal collision from HCMap for player movement
    setupHCMapCollision();

    // Set zone ready flags
    owner_.zoneReady_ = true;

    // In automatic mode, asset loading is handled by the loading thread
    // via loadZoneSequential() after setupInstantScene() returns.
    // In manual mode, loading is deferred until /loadzone command.
    if (ctx_.config.constrainedConfig.deferredAssetLoading && !ctx_.config.eqClientPath.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — automatic mode, loading thread handles asset load",
                 zoneName);
    } else {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — HCMap placeholder + collision, awaiting /loadzone",
                 zoneName);
    }
}

void ZoneLifecycleComponent::setupHCMapCollision() {
    if (!ctx_.smgr) return;

    // Clean up all collision data
    owner_.regionWorldTriangles_.clear();
    owner_.objectWorldTriangles_.clear();
    owner_.doorCollisionData_.clear();
    if (owner_.zoneTriangleSelector_) { owner_.zoneTriangleSelector_->drop(); owner_.zoneTriangleSelector_ = nullptr; }
    if (owner_.terrainOnlySelector_) { owner_.terrainOnlySelector_->drop(); owner_.terrainOnlySelector_ = nullptr; }

    irr::scene::IMetaTriangleSelector* metaSelector = ctx_.smgr->createMetaTriangleSelector();
    if (!metaSelector) return;

    if (owner_.zonePlaceholderNode_ && owner_.zonePlaceholderNode_->getMesh()) {
        auto* sel = ctx_.smgr->createTriangleSelector(owner_.zonePlaceholderNode_->getMesh(), owner_.zonePlaceholderNode_);
        if (sel) { metaSelector->addTriangleSelector(sel); sel->drop(); }
    }

    owner_.zoneTriangleSelector_ = metaSelector;

    owner_.terrainOnlySelector_ = ctx_.smgr->createMetaTriangleSelector();
    if (owner_.terrainOnlySelector_ && owner_.zonePlaceholderNode_ && owner_.zonePlaceholderNode_->getMesh()) {
        auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(owner_.terrainOnlySelector_);
        auto* sel = ctx_.smgr->createTriangleSelector(owner_.zonePlaceholderNode_->getMesh(), owner_.zonePlaceholderNode_);
        if (sel) { terrainMeta->addTriangleSelector(sel); sel->drop(); }
    }

    owner_.collisionManager_ = ctx_.smgr->getSceneCollisionManager();
    if (ctx_.cameraController && owner_.collisionManager_ && owner_.zoneTriangleSelector_) {
        ctx_.cameraController->setCollisionManager(owner_.collisionManager_, owner_.zoneTriangleSelector_);
    }

    LOG_INFO(MOD_GRAPHICS, "HCMap collision setup complete");
}

void ZoneLifecycleComponent::storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                              const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                              const float fogMinClip[4], const float fogMaxClip[4]) {
    owner_.storedZoneEnvironment_.skyType = skyType;
    owner_.storedZoneEnvironment_.zoneType = zoneType;
    for (int i = 0; i < 4; ++i) {
        owner_.storedZoneEnvironment_.fogR[i] = fogRed[i];
        owner_.storedZoneEnvironment_.fogG[i] = fogGreen[i];
        owner_.storedZoneEnvironment_.fogB[i] = fogBlue[i];
        owner_.storedZoneEnvironment_.fogMinClip[i] = fogMinClip[i];
        owner_.storedZoneEnvironment_.fogMaxClip[i] = fogMaxClip[i];
    }
    owner_.storedZoneEnvironment_.pending = true;
}

void ZoneLifecycleComponent::applyStoredZoneEnvironment() {
    if (owner_.storedZoneEnvironment_.pending) {
        setZoneEnvironment(owner_.storedZoneEnvironment_.skyType, owner_.storedZoneEnvironment_.zoneType,
                           owner_.storedZoneEnvironment_.fogR, owner_.storedZoneEnvironment_.fogG, owner_.storedZoneEnvironment_.fogB,
                           owner_.storedZoneEnvironment_.fogMinClip, owner_.storedZoneEnvironment_.fogMaxClip);
        owner_.storedZoneEnvironment_.pending = false;
        LOG_INFO(MOD_GRAPHICS, "Applied stored zone environment (sky={}, ztype={})",
                 owner_.storedZoneEnvironment_.skyType, owner_.storedZoneEnvironment_.zoneType);
    }
}

// ============================================================================
// Entity Loading State Management
// ============================================================================

void ZoneLifecycleComponent::setExpectedEntityCount(size_t count) {
    if (owner_.isLoading()) return;
    expectedEntityCount_ = count;
    // Note: Entity count is tracked for informational purposes only.
    // Zone ready is triggered when the player entity is created (isPlayer=true in createEntity).
    LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, already loaded: {}", count, owner_.loadedEntityCount_);
}

void ZoneLifecycleComponent::setNetworkReady(bool ready) {
    if (owner_.isLoading()) return;
    owner_.networkReady_ = ready;
    LOG_DEBUG(MOD_GRAPHICS, "Network ready: {}", ready);

    if (!ready) {
        // Network not ready means we're zoning, reset entity loading state
        entitiesLoaded_ = false;
        expectedEntityCount_ = 0;
        owner_.loadedEntityCount_ = 0;
        owner_.zoneReady_ = false;
    }
    // Note: Zone ready is NOT set here. It will be set when the player entity
    // is created in createEntity() with isPlayer=true. This ensures the loading
    // screen stays visible until the player model is fully loaded.
}

void ZoneLifecycleComponent::setWeather(uint8_t type, uint8_t intensity) {
    if (owner_.isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "Weather update: type={}, intensity={}", type, intensity);
    if (ctx_.weatherEffects) {
        ctx_.weatherEffects->setWeather(type, intensity);
    }
}


} // namespace Graphics
} // namespace EQT
