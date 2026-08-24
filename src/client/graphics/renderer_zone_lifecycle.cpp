#include "client/graphics/irrlicht_renderer.h"
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

void IrrlichtRenderer::unloadZone() {
    if (isLoading()) return;
    sequentialLoadComplete_ = false;
    storedZoneEnvironment_.pending = false;

    // Stop simulation worker before zone cleanup
    stopSimulationWorker();

    // Stop background entity prep worker before zone cleanup
    if (entityPrepWorker_) {
        entityPrepWorker_->stop();
        entityPrepWorker_.reset();
        if (entityRenderer_) {
            entityRenderer_->setEntityPrepWorker(nullptr);
        }
    }

    // Reset entity loading state - we're starting a new zone
    networkReady_ = false;
    entitiesLoaded_ = false;
    expectedEntityCount_ = 0;
    loadedEntityCount_ = 0;
    zoneReady_ = false;
    entityPrepReady_ = false;
    entityPrepScanCounter_ = 0;

    // Log texture counts before cleanup
    if (constrainedTextureCache_) {
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache has {} textures, {} bytes before cleanup",
                 constrainedTextureCache_->getTextureCount(),
                 constrainedTextureCache_->getCurrentUsage());
    }

    // Remove zone placeholder mesh if still present
    destroyZonePlaceholder();

    // Unload EQE1 flat packs (clear cache refs before destroying loaders)
    if (constrainedTextureCache_) {
        constrainedTextureCache_->clearEQE1Loaders();
    }
    {
        std::lock_guard<std::mutex> lock(eqe1Mutex_);
        eqe1Loaders_.clear();
    }
    // P04: drop the lazy-open queue too so the next zone's Step0 starts fresh.
    pendingGlobalEQE1Archives_.clear();

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
    // Release retained GLES2 textures and EGL images after atlas GL textures are deleted.
    // Lima driver requires source textures/images to stay alive while desktop GL textures
    // are in use, so we release them only after unload().
    if (gles2Helper_) {
        gles2Helper_->releaseSharedResources();
    }
#endif

    // Reset animated texture manager (removes its textures from driver)
    animatedTextureManager_.reset();

    // Clear camera collision FIRST to prevent use-after-free during zone transitions
    if (cameraController_) {
        cameraController_->setBspCollision(nullptr, nullptr);
        cameraController_->setCollisionManager(nullptr, nullptr);
    }

    // Clear detail system BEFORE dropping collision selector
    if (detailManager_) {
        detailManager_->onZoneExit();
    }

    // P32: AnimatedTreeManager::cleanup() (scene nodes/meshes owned by the
    // CPU-instancing animatedTrees_ list, always empty on GLES2) removed
    // along with the class. treeWindController_/treeIdentifier_ own no
    // Irrlicht scene-graph resources, so there is nothing to clear here.

    // Clear environmental particle system
    if (particleManager_) {
        particleManager_->onZoneLeave();
    }

    // Clear ambient creatures (boids) system
    if (boidsManager_) {
        boidsManager_->onZoneLeave();
    }

    // Clear tumbleweed system
    if (tumbleweedManager_) {
        tumbleweedManager_->onZoneLeave();
    }

    // Now safe to remove zone collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    // Remove zone mesh
    if (zoneMeshNode_) {
        deleteMeshHardwareBuffers(zoneMeshNode_);
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    // Remove PVS region mesh nodes (delete VBOs before removing nodes)
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        if (node) {
            deleteMeshHardwareBuffers(node);
            if (node->getParent()) node->remove(); else node->drop();
        }
    }
    regionMeshNodes_.clear();
    regionBoundingBoxes_.clear();

    if (fallbackMeshNode_) {
        deleteMeshHardwareBuffers(fallbackMeshNode_);
        if (fallbackMeshNode_->getParent()) fallbackMeshNode_->remove(); else fallbackMeshNode_->drop();
        fallbackMeshNode_ = nullptr;
    }

    // Remove collision-only node (used in PVS mode)
    if (zoneCollisionNode_) {
        zoneCollisionNode_->remove();
        zoneCollisionNode_ = nullptr;
    }

    // Reset PVS state
    usePvsCulling_ = false;
    zoneBspTree_.reset();
    currentPvsRegion_ = SIZE_MAX;

    // Reset manual zone draw state
    manualZoneDrawEnabled_ = false;
    sortedZoneDrawList_.clear();
    sortedDrawEntries_.clear();

    // P10: reset cross-region zone draw merge state — the merge buffers are
    // scoped to a single zone visit's region set; destroy them and clear the
    // "disabled for this zone" latch so the next zone gets a fresh attempt.
    if (zoneMergeBuffers_) zoneMergeBuffers_->destroy();
    mergeZoneDrawsDisabledForZone_ = false;
    mergeZoneDrawsBuildEvictionCount_ = 0;
    portalSystem_.reset();
    portalOcclusionEnabled_ = false;
    portalOcclusionEligible_ = false;
    portalBuildPending_ = false;
    regionNeighbors_.clear();

    // ConstrainedMeshCache and its background-build state (BackgroundMeshResult
    // queue, material swap queue, meshLoadQueue_, etc.) removed T67 — the
    // cache was never constructed in any shipped build; see
    // src/client/graphics/CLAUDE.md for the reference pointer.
    // protectedRegions_ is kept (still read by cache-independent progressive
    // load code) but has no writer left, so clearing it here is now a no-op.
    protectedRegions_.clear();
    portalVisibleRegions_ = nullptr;
    indoorRegions_.clear();
    regionPvsDepthMap_ = nullptr;
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
    particleRenderBuffer_ = nullptr;
    currentFrameSimResults_ = nullptr;
    pvsNeighborhood_.clear();
    pvsNeighborhoodAnchor_ = SIZE_MAX;
    pendingTextureRegions_.clear();
    textureRebuildQueue_.clear();
    pendingTextureDoors_.clear();
    doorTextureRebuildQueue_.clear();
    pendingTextureObjects_.clear();
    objectTextureRebuildQueue_.clear();
#ifdef EQT_HAS_GLES2
    pendingGPUResults_.clear();
#endif

    // Clear occlusion culler data
    if (occlusionCuller_) {
        occlusionCuller_->clearOccluders();
    }
    occlusionCulledRegions_.clear();

    // Reset zone clip plane so previous zone's cap doesn't linger
    zoneMaxClip_ = 99999.0f;
    setRenderDistance(userRenderDistance_);

    // Clear BSP tree from entity renderer and door manager
    if (entityRenderer_) {
        entityRenderer_->clearBspTree();
    }
    if (doorManager_) {
        doorManager_->setBspTree(nullptr);
        doorManager_->setPvsRegion(SIZE_MAX);
        doorManager_->setOcclusionCulledRegions(nullptr);
        doorManager_->setRegionNeighbors(nullptr);
    }

    // Remove object nodes
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        if (objectNodes_[i]) {
            if (i < objectInSceneGraph_.size() && objectInSceneGraph_[i]) {
                objectNodes_[i]->remove();
            }
            objectNodes_[i]->drop();  // Release our reference
        }
    }
    objectNodes_.clear();
    objectPositions_.clear();
    objectBoundingBoxes_.clear();
    objectInSceneGraph_.clear();

    // Clear zone light data (no scene nodes to remove)
    zoneLightData_.clear();
    zoneLightPositions_.clear();
    // Clear entity renderer
    if (entityRenderer_) {
        // Clear mesh caches first to force fresh mesh/texture rebuild on next zone-in.
        // Keeps model data (geometry, skeletons, S3D archives) cached for performance.
        // Fixes garbled player textures on re-zone caused by stale texture pointers
        // in cached animated meshes.
        if (auto* rml = entityRenderer_->getRaceModelLoader()) {
            rml->clearMeshCaches();
        }
        entityRenderer_->clearEntities();
    }

    // Clear door manager
    if (doorManager_) {
        doorManager_->clearDoors();
        doorManager_->setZone(nullptr);
    }

    // Disable sky when unloading zone (will be re-enabled when new zone loads)
    if (skyRenderer_) {
        skyRenderer_->setEnabled(false);
    }

    // Clear world objects (tradeskill containers)
    clearWorldObjects();

    // Clear object lights (they reference freed zone data)
    for (size_t i = 0; i < objectLights_.size(); ++i) {
        if (objectLights_[i].node) {
            bool isInScene = (i < objectLightInSceneGraph_.size()) ? objectLightInSceneGraph_[i] : false;
            if (isInScene) {
                objectLights_[i].node->remove();
            }
            objectLights_[i].node->drop();
        }
    }
    objectLights_.clear();
    objectLightInSceneGraph_.clear();

    // Clear vertex animated meshes (they reference freed zone mesh buffers)
    vertexAnimatedMeshes_.clear();

    // Clear zone line visualization boxes
    clearZoneLineBoundingBoxes();

    // Clear constrained texture cache to free old zone textures from driver
    if (constrainedTextureCache_) {
        constrainedTextureCache_->unfreeze();
        constrainedTextureCache_->clear();
        constrainedTextureCache_->resetStatistics();
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache cleared");
    }

    currentZone_.reset();
    currentZoneName_.clear();
    isIndoorZone_ = false;
    zoneBoundsValid_ = false;
}

void IrrlichtRenderer::setZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                          const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                          const float fogMinClip[4], const float fogMaxClip[4]) {
    // Set sky type if sky renderer is available
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->setSkyType(skyType, currentZoneName_);

        // Determine if sky should be shown based on zone type and user settings
        // zoneType values from server: 1=Outdoors, 2=Dungeons, 255(0xFF)=Any/default
        // Only disable sky for explicit dungeon zones (type 2)
        bool isDungeon = (zoneType == 2);
        isIndoorZone_ = isDungeon;

        // Check user setting and constrained config
        bool skySettingEnabled = getDisplaySettings().skyEnabled;
        bool skyAllowed = !isDungeon && skySettingEnabled && config_.constrainedConfig.skyRendering;
        skyRenderer_->setEnabled(skyAllowed);

        LOG_DEBUG(MOD_GRAPHICS, "Zone environment: sky type {}, zone type {} ({}), sky {} (setting={}, constrained={})",
                  skyType, zoneType, isDungeon ? "dungeon" : "outdoor",
                  skyAllowed ? "enabled" : "disabled",
                  skySettingEnabled ? "on" : "off",
                  config_.constrainedConfig.skyRendering ? "on" : "off");
    }

    // Apply server-provided max clip plane as ceiling on render distance
    // fogMaxClip[0] is the primary clip distance for the zone
    zoneMaxClip_ = (fogMaxClip[0] > 0.0f) ? fogMaxClip[0] : 99999.0f;
    // Re-apply render distance with the new zone cap
    setRenderDistance(userRenderDistance_);

    LOG_INFO(MOD_GRAPHICS, "Zone max clip plane: {:.0f} (effective render distance: {:.0f}, user setting: {:.0f})",
             zoneMaxClip_, renderDistance_, userRenderDistance_);

    // Apply fog color from zone data
    if (driver_ && fogEnabled_) {
        irr::video::SColor fogColor(255, fogRed[0], fogGreen[0], fogBlue[0]);

        float fogEnd = renderDistance_;
        float fogStart = renderDistance_ - fogThickness_;
        fogStart = std::max(0.0f, fogStart);

        driver_->setFog(
            fogColor,
            irr::video::EFT_FOG_LINEAR,
            fogStart,
            fogEnd,
            0.0f,  // Density (unused for linear fog)
            true,  // Pixel fog
            false  // Range fog
        );

        LOG_DEBUG(MOD_GRAPHICS, "Zone fog color: RGB({},{},{}), distances: {:.0f}-{:.0f} (renderDistance={:.0f})",
                  fogRed[0], fogGreen[0], fogBlue[0], fogStart, fogEnd, renderDistance_);
    }
}

void IrrlichtRenderer::buildZonePlaceholder(float playerIrrX, float playerIrrY, float playerIrrZ) {
    if (!collisionMap_ || !collisionMap_->IsLoaded() || !smgr_ || !driver_) return;

    // Destroy existing node for rebuild
    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->remove();
        zonePlaceholderNode_ = nullptr;
    }

    // Cache terrain triangles on first call (GetAllTerrainTriangles is expensive)
    if (cachedPlaceholderTriangles_.empty()) {
        cachedPlaceholderTriangles_ = collisionMap_->GetAllTerrainTriangles();
        if (cachedPlaceholderTriangles_.empty()) return;
    }

    const auto& allTriangles = cachedPlaceholderTriangles_;

    // Filter triangles to render distance from player position (Irrlicht Y-up coords)
    float clipDist = renderDistance_ > 0.0f ? renderDistance_ : 300.0f;
    float clipDistSq = clipDist * clipDist;

    // PVS filtering: if BSP tree is available (from BSP preload), skip triangles
    // in BSP regions not visible from the camera's current region.
    // Centroid coords: Irrlicht Y-up (cx, cy, cz) → EQ Z-up (cx, cz, cy)
    bool usePvsFilter = false;
    std::shared_ptr<BspRegion> camRegion;
    if (zoneBspTree_) {
        // Use currentPvsRegion_ if already computed, otherwise derive from player position
        // (handles the case where BSP preload just completed but PVS update hasn't run yet)
        size_t cameraRegion = currentPvsRegion_;
        if (cameraRegion == SIZE_MAX) {
            // Irrlicht Y-up (x, y, z) → EQ Z-up (x, z, y)
            cameraRegion = zoneBspTree_->findRegionIndexForPoint(playerIrrX, playerIrrZ, playerIrrY);
        }
        if (cameraRegion != SIZE_MAX && cameraRegion < zoneBspTree_->regions.size()) {
            camRegion = zoneBspTree_->regions[cameraRegion];
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
            size_t triRegion = zoneBspTree_->findRegionIndexForPoint(cx, cz, cy);
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

    zonePlaceholderNode_ = smgr_->addMeshSceneNode(mesh);
    mesh->drop();

    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->setPosition(irr::core::vector3df(0, 0, 0));
        lastPlaceholderBuildPos_ = irr::core::vector3df(playerIrrX, playerIrrY, playerIrrZ);
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

void IrrlichtRenderer::destroyZonePlaceholder() {
    if (zonePlaceholderNode_) {
        zonePlaceholderNode_->remove();
        zonePlaceholderNode_ = nullptr;
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

void IrrlichtRenderer::setupInstantScene(const std::string& zoneName, float playerX, float playerY, float playerZ) {
    currentZoneName_ = zoneName;
    loadIndoorRegionMap(zoneName);

    // S05: entityRenderer_ already created in initLoadingScreen()

    // Create door manager for placeholder cubes (no model loading yet)
    if (!doorManager_ && smgr_ && driver_) {
        doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
        LOG_INFO(MOD_GRAPHICS, "Door manager created for instant scene (placeholder cubes only)");
    }

    // Notify entity renderer of zone (for zone-specific model search paths)
    if (entityRenderer_) {
        entityRenderer_->setCurrentZone(zoneName);
    }

    // Build HCMap placeholder around player position, clipped to render distance
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        // Convert EQ coords (Z-up) to Irrlicht (Y-up) for the builder
        buildZonePlaceholder(playerX, playerZ, playerY);
    }

    // Minimal collision from HCMap for player movement
    setupHCMapCollision();

    // Set zone ready flags
    zoneReady_ = true;

    // In automatic mode, asset loading is handled by the loading thread
    // via loadZoneSequential() after setupInstantScene() returns.
    // In manual mode, loading is deferred until /loadzone command.
    if (config_.constrainedConfig.deferredAssetLoading && !config_.eqClientPath.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — automatic mode, loading thread handles asset load",
                 zoneName);
    } else {
        LOG_INFO(MOD_GRAPHICS, "Instant scene ready for zone '{}' — HCMap placeholder + collision, awaiting /loadzone",
                 zoneName);
    }
}

void IrrlichtRenderer::setupHCMapCollision() {
    if (!smgr_) return;

    // Clean up all collision data
    regionWorldTriangles_.clear();
    objectWorldTriangles_.clear();
    doorCollisionData_.clear();
    if (zoneTriangleSelector_) { zoneTriangleSelector_->drop(); zoneTriangleSelector_ = nullptr; }
    if (terrainOnlySelector_) { terrainOnlySelector_->drop(); terrainOnlySelector_ = nullptr; }

    irr::scene::IMetaTriangleSelector* metaSelector = smgr_->createMetaTriangleSelector();
    if (!metaSelector) return;

    if (zonePlaceholderNode_ && zonePlaceholderNode_->getMesh()) {
        auto* sel = smgr_->createTriangleSelector(zonePlaceholderNode_->getMesh(), zonePlaceholderNode_);
        if (sel) { metaSelector->addTriangleSelector(sel); sel->drop(); }
    }

    zoneTriangleSelector_ = metaSelector;

    terrainOnlySelector_ = smgr_->createMetaTriangleSelector();
    if (terrainOnlySelector_ && zonePlaceholderNode_ && zonePlaceholderNode_->getMesh()) {
        auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(terrainOnlySelector_);
        auto* sel = smgr_->createTriangleSelector(zonePlaceholderNode_->getMesh(), zonePlaceholderNode_);
        if (sel) { terrainMeta->addTriangleSelector(sel); sel->drop(); }
    }

    collisionManager_ = smgr_->getSceneCollisionManager();
    if (cameraController_ && collisionManager_ && zoneTriangleSelector_) {
        cameraController_->setCollisionManager(collisionManager_, zoneTriangleSelector_);
    }

    LOG_INFO(MOD_GRAPHICS, "HCMap collision setup complete");
}

void IrrlichtRenderer::storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
                                              const uint8_t fogRed[4], const uint8_t fogGreen[4], const uint8_t fogBlue[4],
                                              const float fogMinClip[4], const float fogMaxClip[4]) {
    storedZoneEnvironment_.skyType = skyType;
    storedZoneEnvironment_.zoneType = zoneType;
    for (int i = 0; i < 4; ++i) {
        storedZoneEnvironment_.fogR[i] = fogRed[i];
        storedZoneEnvironment_.fogG[i] = fogGreen[i];
        storedZoneEnvironment_.fogB[i] = fogBlue[i];
        storedZoneEnvironment_.fogMinClip[i] = fogMinClip[i];
        storedZoneEnvironment_.fogMaxClip[i] = fogMaxClip[i];
    }
    storedZoneEnvironment_.pending = true;
}

void IrrlichtRenderer::applyStoredZoneEnvironment() {
    if (storedZoneEnvironment_.pending) {
        setZoneEnvironment(storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType,
                           storedZoneEnvironment_.fogR, storedZoneEnvironment_.fogG, storedZoneEnvironment_.fogB,
                           storedZoneEnvironment_.fogMinClip, storedZoneEnvironment_.fogMaxClip);
        storedZoneEnvironment_.pending = false;
        LOG_INFO(MOD_GRAPHICS, "Applied stored zone environment (sky={}, ztype={})",
                 storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType);
    }
}

// ============================================================================
// Entity Loading State Management
// ============================================================================

void IrrlichtRenderer::setExpectedEntityCount(size_t count) {
    if (isLoading()) return;
    expectedEntityCount_ = count;
    // Note: Entity count is tracked for informational purposes only.
    // Zone ready is triggered when the player entity is created (isPlayer=true in createEntity).
    LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, already loaded: {}", count, loadedEntityCount_);
}

void IrrlichtRenderer::notifyEntityLoaded() {
    // Note: Entity counting is now done in createEntity() since entities are created
    // synchronously during ZoneSpawns processing. This method is kept for interface
    // compatibility but is not currently used for counting.
    // The expected/loaded entity count comparison happens in setExpectedEntityCount().
}

void IrrlichtRenderer::setNetworkReady(bool ready) {
    if (isLoading()) return;
    networkReady_ = ready;
    LOG_DEBUG(MOD_GRAPHICS, "Network ready: {}", ready);

    if (!ready) {
        // Network not ready means we're zoning, reset entity loading state
        entitiesLoaded_ = false;
        expectedEntityCount_ = 0;
        loadedEntityCount_ = 0;
        zoneReady_ = false;
    }
    // Note: Zone ready is NOT set here. It will be set when the player entity
    // is created in createEntity() with isPlayer=true. This ensures the loading
    // screen stays visible until the player model is fully loaded.
}

void IrrlichtRenderer::setWeather(uint8_t type, uint8_t intensity) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "Weather update: type={}, intensity={}", type, intensity);
    if (weatherEffects_) {
        weatherEffects_->setWeather(type, intensity);
    }
}

void IrrlichtRenderer::checkAndSetZoneReady() {
    // This method is kept for interface compatibility but zone ready
    // is now triggered by player entity creation in createEntity().
    // See createEntity() where isPlayer=true triggers zoneReady_ = true.
}


} // namespace Graphics
} // namespace EQT
