#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/sky_renderer.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
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
    // T491 lifecycle point: everything the outgoing zone owns is still live.
    eqt::memory::recordPoint("zone_change_before_unload", ctx_.currentZoneName);
    ctx_.sequentialLoadComplete = false;
    storedZoneEnvironment_.pending = false;

    // Stop simulation worker before zone cleanup
    ctx_.SimBridge().stopSimulationWorker();

    // Stop background entity prep worker before zone cleanup
    if (ctx_.entityPrepWorker) {
        ctx_.entityPrepWorker->stop();
        ctx_.entityPrepWorker.reset();
        if (ctx_.entityRenderer) {
            ctx_.entityRenderer->setEntityPrepWorker(nullptr);
        }
    }

    // Reset entity loading state - we're starting a new zone
    networkReady_ = false;
    entitiesLoaded_ = false;
    expectedEntityCount_ = 0;
    loadedEntityCount_ = 0;
    ctx_.zoneReady = false;
    ctx_.entityPrepReady = false;
    ctx_.entityPrepScanCounter = 0;

    // Log texture counts before cleanup
    if (ctx_.constrainedTextureCache) {
        // B944: with the eviction/restore traffic of the zone that is leaving.
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache has {} textures ({} resident), {} bytes "
                 "before cleanup; {} evictions, {} restores, {} restore failures, {} pinned (no restore "
                 "source, B947) this zone",
                 ctx_.constrainedTextureCache->getTextureCount(),
                 ctx_.constrainedTextureCache->getResidentTextureCount(),
                 ctx_.constrainedTextureCache->getCurrentUsage(),
                 ctx_.constrainedTextureCache->getEvictionCount(),
                 ctx_.constrainedTextureCache->getRestoreCount(),
                 ctx_.constrainedTextureCache->getRestoreFailureCount(),
                 ctx_.constrainedTextureCache->getPinnedTextureCount());
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
        for (auto it = ctx_.eqe1Loaders.begin(); it != ctx_.eqe1Loaders.end(); ) {
            if (Eqe1ArchiveComponent::isPersistentEQE1Archive(it->first) && it->second) {
                ++it;
            } else {
                it = ctx_.eqe1Loaders.erase(it);
            }
        }
        // Re-register the survivors: clearEQE1Loaders() emptied the cache's
        // vector, and they must be searchable again before the next lookup.
        if (ctx_.constrainedTextureCache) {
            for (const auto& [name, loader] : ctx_.eqe1Loaders) {
                ctx_.constrainedTextureCache->addEQE1Loader(loader.get());
            }
        }
    }
    // P04: drop the lazy-open queue too so the next zone's Step0 starts fresh.
    {
        std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
        ctx_.pendingGlobalEQE1Archives.clear();
        ctx_.eqe1CandidateRaces.clear();
        ctx_.eqe1LoadTimeCandidateCount = 0;
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

    // Clear weather. T396 (board B211): this call did not exist -- the three
    // managers above were unloaded and the weather controller was not, so
    // rain (and its ambient darkening) survived the zone change.
    if (ctx_.weatherEffects) {
        ctx_.weatherEffects->onZoneLeave();
    }

    // Now safe to remove zone collision data
    ctx_.regionWorldTriangles.clear();
    ctx_.objectCollision.clear();
    ctx_.doorCollisionData.clear();
    // T435 (B425): keep DoorManager's dirty set in step with the map it feeds.
    if (ctx_.doorManager) ctx_.doorManager->markAllCollisionDirty();
    if (zoneTriangleSelector_) {
        zoneTriangleSelector_->drop();
        zoneTriangleSelector_ = nullptr;
    }
    if (terrainOnlySelector_) {
        terrainOnlySelector_->drop();
        terrainOnlySelector_ = nullptr;
    }

    // Remove zone mesh
    if (ctx_.zoneMeshNode) {
        deleteMeshHardwareBuffers(ctx_.zoneMeshNode);
        ctx_.zoneMeshNode->remove();
        ctx_.zoneMeshNode = nullptr;
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

    // Remove collision-only node (used in PVS mode)
    if (zoneCollisionNode_) {
        zoneCollisionNode_->remove();
        zoneCollisionNode_ = nullptr;
    }

    // Reset PVS state
    ctx_.usePvsCulling = false;
    ctx_.zoneBspTree.reset();
    ctx_.currentPvsRegion = SIZE_MAX;

    // Reset manual zone draw state
    ctx_.manualZoneDrawEnabled = false;
    ctx_.sortedZoneDrawList.clear();
    sortedDrawEntries_.clear();

    // P10: reset cross-region zone draw merge state — the merge buffers are
    // scoped to a single zone visit's region set; destroy them and clear the
    // "disabled for this zone" latch so the next zone gets a fresh attempt.
    if (zoneMergeBuffers_) zoneMergeBuffers_->destroy();
    mergeZoneDrawsDisabledForZone_ = false;
    mergeZoneDrawsBuildEvictionCount_ = 0;
    ctx_.portalSystem.reset();
    ctx_.portalOcclusionEnabled = false;
    ctx_.portalOcclusionEligible = false;
    ctx_.portalBuildPending = false;
    ctx_.regionNeighbors.clear();

    // ConstrainedMeshCache and its background-build state (BackgroundMeshResult
    // queue, material swap queue, meshLoadQueue_, etc.) removed T67 — the
    // cache was never constructed in any shipped build; see
    // src/client/graphics/CLAUDE.md for the reference pointer.
    // T292: protectedRegions_ removed entirely -- it was the cache's
    // eviction-protection bitvector, had no writer left after T67, and its
    // remaining readers were permanent no-ops.
    owner_.portalVisibleRegions_ = nullptr;
    ctx_.indoorRegions.clear();
    ctx_.regionPvsDepthMap = nullptr;
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
    ctx_.particleRenderBuffer = nullptr;
    owner_.currentFrameSimResults_ = nullptr;
    ctx_.ZoneStream().pvsNeighborhood().clear();
    ctx_.ZoneStream().pvsNeighborhoodAnchor() = SIZE_MAX;
    // T475: the six pending-texture maps and rebuild queues cleared here were
    // deleted with the dead texture-arrival arm (renderer_gpu_upload.cpp).
#ifdef EQT_HAS_GLES2
    owner_.pendingGPUResults_.clear();
#endif

    // Clear occlusion culler data
    if (ctx_.occlusionCuller) {
        ctx_.occlusionCuller->clearOccluders();
    }
    ctx_.occlusionCulledRegions.clear();

    // Reset zone clip plane so previous zone's cap doesn't linger
    zoneMaxClip_ = 99999.0f;
    owner_.setRenderDistance(userRenderDistance_);

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

    // Remove object records. B938 (#256): the record is the placement. In a
    // PVS zone it is the SOLE mesh holder (its grab() in step 6, needsOwnRef),
    // so dropping rec.mesh releases the mesh. In a non-PVS zone the record
    // holds NO mesh reference (needsOwnRef false) -- the scene-graph node does
    // (addMeshSceneNode grabbed it) -- so remove() + drop() the node and let
    // its destruction release the mesh.
    for (auto& rec : ctx_.objectRecords) {
        if (rec.node) {
            if (rec.inSceneGraph)
                rec.node->remove();
            rec.node->drop();  // release the record's node reference (step 6 grab)
        } else if (rec.mesh) {
            rec.mesh->drop();  // release the record's own mesh reference (PVS zones)
        }
    }
    ctx_.objectRecords.clear();

    // Clear zone light data (no scene nodes to remove)
    ctx_.zoneLightData.clear();
    ctx_.zoneLightPositions.clear();
    // T457 (board B482): release the inventory paperdoll's model BEFORE either
    // of the two owners below destroys what its materials name -- the
    // RaceModelLoader mesh caches on the next line, and the constrained texture
    // cache further down.
    //
    // This call is why the T449 crash cannot happen, and its POSITION is the
    // whole fix. The doll (eqt::ui::CharacterModelView) renders into its own
    // scene manager, created with smgr->createNewSceneManager(false), so the
    // safety net that protects every other surviving node --
    // ConstrainedTextureCache::clearTextureReferences(), which walks the MAIN
    // scene graph nulling material slots before each driver_->removeTexture()
    // (T119) -- structurally cannot reach it. Left alone, the doll survives the
    // unload holding raw ITexture* to deleted COGLES2Textures, and the next
    // frame's renderToTexture() faults inside
    // COpenGLES2Driver::bindAlphaCompanionUnit(). T432's persistence makes that
    // reachable with no player action at all: a saved [InventoryWindow] Show=1
    // reopens the doll at login.
    //
    // The rebuild is the other half and needs no call here --
    // OverlaysComponent::onZoneUnload() resets its appearance latch, so
    // updatePaperdollForFrame() rebuilds the model on the first frame after
    // zone-in that has a player entity.
    ctx_.Overlays().onZoneUnload();

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
            bool isInScene = (i < ctx_.objectLightInSceneGraph.size()) ? ctx_.objectLightInSceneGraph[i] : false;
            if (isInScene) {
                ctx_.objectLights[i].node->remove();
            }
            ctx_.objectLights[i].node->drop();
        }
    }
    ctx_.objectLights.clear();
    ctx_.objectLightInSceneGraph.clear();

    // Clear vertex animated meshes (they reference freed zone mesh buffers)
    ctx_.vertexAnimatedMeshes.clear();

    // Clear zone line visualization boxes
    ctx_.Overlays().clearZoneLineBoundingBoxes();

    // Clear constrained texture cache to free old zone textures from driver
    if (ctx_.constrainedTextureCache) {
        ctx_.constrainedTextureCache->unfreeze();
        ctx_.constrainedTextureCache->clear();
        ctx_.constrainedTextureCache->resetStatistics();
        LOG_INFO(MOD_GRAPHICS, "unloadZone: constrained texture cache cleared");
    }

    ctx_.currentZone.reset();
    ctx_.currentZoneName.clear();
    ctx_.isIndoorZone = false;
    ctx_.zoneBoundsValid = false;
    // T491 lifecycle point: the zone is gone, before the next one loads.
    eqt::memory::recordPoint("zone_change_after_unload", ctx_.currentZoneName);
    // ...and from here the per-category PEAKS measure the NEXT zone's
    // residency, not the whole process history. Without this the peak column
    // is pinned forever by whichever zone was heaviest since launch, which is
    // exactly the question a zone-change report is not asking. The snapshot
    // above is taken FIRST, so the outgoing zone's peaks are still reported.
    eqt::memory::resetPeaks();
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
        ctx_.isIndoorZone = isDungeon;

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
    zoneMaxClip_ = (fogMaxClip[0] > 0.0f) ? fogMaxClip[0] : 99999.0f;
    // Re-apply render distance with the new zone cap
    owner_.setRenderDistance(userRenderDistance_);

    LOG_INFO(MOD_GRAPHICS, "Zone max clip plane: {:.0f} (effective render distance: {:.0f}, user setting: {:.0f})",
             zoneMaxClip_, ctx_.renderDistance, userRenderDistance_);

    // Apply fog color from zone data
    if (ctx_.driver && ctx_.fogEnabled) {
        irr::video::SColor fogColor(255, fogRed[0], fogGreen[0], fogBlue[0]);

        float fogEnd = ctx_.renderDistance;
        float fogStart = ctx_.renderDistance - ctx_.fogThickness;
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
    if (ctx_.zonePlaceholderNode) {
        ctx_.zonePlaceholderNode->remove();
        ctx_.zonePlaceholderNode = nullptr;
    }

    // T504: no cached copy of every terrain triangle. The filter below keeps
    // ~1 % of them (clip 300 of a 5,000-unit zone), and the copy it used to
    // filter from (dreadlands 8.9 MiB) lived from instant-scene to the first
    // frame after the real geometry -- the whole zone-load peak. Stream them
    // from the collision map's own arrays instead; a rebuild on player
    // movement re-walks them (135k triangles, milliseconds).

    // Filter triangles to render distance from player position (Irrlicht Y-up coords)
    float clipDist = ctx_.renderDistance > 0.0f ? ctx_.renderDistance : 300.0f;
    float clipDistSq = clipDist * clipDist;

    // PVS filtering: if BSP tree is available (from BSP preload), skip triangles
    // in BSP regions not visible from the camera's current region.
    // Centroid coords: Irrlicht Y-up (cx, cy, cz) → EQ Z-up (cx, cz, cy)
    bool usePvsFilter = false;
    std::shared_ptr<BspRegion> camRegion;
    PvsBits camPvs;
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
            if (camRegion && camRegion->hasPvs()) {
                camPvs = camRegion->pvs();
                usePvsFilter = true;
            }
        }
    }

    std::vector<HCMap::Triangle> triangles;
    size_t pvsCulledCount = 0;
    const size_t totalTerrainTriangles = ctx_.collisionMap->ForEachTerrainTriangle(
        [&](const HCMap::Triangle& tri) {
        // Use triangle centroid for distance check
        float cx = (tri.v1.x + tri.v2.x + tri.v3.x) / 3.0f;
        float cy = (tri.v1.y + tri.v2.y + tri.v3.y) / 3.0f;
        float cz = (tri.v1.z + tri.v2.z + tri.v3.z) / 3.0f;
        float dx = cx - playerIrrX;
        float dy = cy - playerIrrY;
        float dz = cz - playerIrrZ;
        if (dx * dx + dy * dy + dz * dz > clipDistSq) {
            return;
        }

        // PVS check: convert Irrlicht Y-up centroid to EQ Z-up for BSP lookup
        if (usePvsFilter) {
            size_t triRegion = ctx_.zoneBspTree->findRegionIndexForPoint(cx, cz, cy);
            if (triRegion != SIZE_MAX && triRegion < camPvs.size()
                && !camPvs[triRegion]) {
                pvsCulledCount++;
                return;
            }
        }

        triangles.push_back(tri);
    });
    if (totalTerrainTriangles == 0) return;

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

    ctx_.zonePlaceholderNode = ctx_.smgr->addMeshSceneNode(mesh);
    mesh->drop();

    if (ctx_.zonePlaceholderNode) {
        ctx_.zonePlaceholderNode->setPosition(irr::core::vector3df(0, 0, 0));
        lastPlaceholderBuildPos_ = irr::core::vector3df(playerIrrX, playerIrrY, playerIrrZ);
        if (usePvsFilter) {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles (PVS culled {}, clip {:.0f}, {} total, {:.1f} KB)",
                     triangles.size(), pvsCulledCount, clipDist, totalTerrainTriangles,
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        } else {
            LOG_INFO(MOD_GRAPHICS, "Zone placeholder: {} terrain triangles within clip {:.0f} ({} total, {:.1f} KB)",
                     triangles.size(), clipDist, totalTerrainTriangles,
                     (triangles.size() * 3 * sizeof(irr::video::S3DVertex)) / 1024.0f);
        }
    }
}

void ZoneLifecycleComponent::destroyZonePlaceholder() {
    if (ctx_.zonePlaceholderNode) {
        ctx_.zonePlaceholderNode->remove();
        ctx_.zonePlaceholderNode = nullptr;
        LOG_INFO(MOD_GRAPHICS, "Zone placeholder removed");
    }
}

// T468 (board B590): the only place a DoorManager is constructed. Before this
// there were three -- this function's (no cache, no guard), Step 7's (all but the
// guard) and IrrlichtRenderer::loadGlobalAssets()'s (everything), and the last
// had no caller, so ensureDoorTextureGuard() never ran in a session. Measured
// consequence (tests/test_door_texture_eviction.cpp): a PVS-hidden door's pivot
// is detached from the scene graph, clearTextureReferences()'s root-down walk
// cannot reach it, and an LRU eviction left both its node material and its
// cached mesh buffer naming a texture the driver had already freed.
//
// The zone-load steps still re-apply their per-zone values (Step 1 the zone,
// cache and shaders; Step 2 the BSP, PVS region, frustum culler and
// neighbours). What is wired here is only what the context holds right now, so
// a manager built at the instant scene and one built at Step 7 start identical.
void ZoneLifecycleComponent::ensureDoorManager() {
    if (!ctx_.doorManager) {
        if (!ctx_.smgr || !ctx_.driver) return;
        ctx_.doorManager = std::make_unique<DoorManager>(ctx_.smgr, ctx_.driver);
        DoorManager& doors = *ctx_.doorManager;
        if (ctx_.constrainedTextureCache) doors.setConstrainedTextureCache(ctx_.constrainedTextureCache);
        if (ctx_.currentZone) doors.setZone(ctx_.currentZone);
        if (ctx_.zoneBspTree) doors.setBspTree(ctx_.zoneBspTree.get());
        doors.setPvsRegion(ctx_.currentPvsRegion);
        if (ctx_.frustumCuller) doors.setFrustumCuller(ctx_.frustumCuller.get());
        doors.setRegionNeighbors(ctx_.regionNeighbors.empty() ? nullptr : &ctx_.regionNeighbors);
        if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
            doors.setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                         ctx_.zoneShader->getActiveAlphaTest(),
                                         ctx_.zoneShader->getActiveAlphaTestRGBA());
        }
        // B997 (board #317): the per-material client-array batch of PVS doors.
        // ON by default since the ruling (a) final leg (2026-10-10): the
        // DETERMINISTIC x86 door pixel A/B (ctest test_sg_batch_door_pixel_ab,
        // refactor/B997/README.md "The deterministic x86 door pixel A/B")
        // measured leg-on vs leg-off BYTE-IDENTICAL on both zones (qeynos2
        // 60 in-graph doors, 59 batched to 19 per-material draws; gfaydark 78
        // in-graph, 77 batched to 4 draws), with the same active-camera,
        // ILightManager-hook, SOLID-pre point the live path uses.
        // EQT_SG_BATCH=0 is the A/B opt-out.
        const char* sgBatchEnv = std::getenv("EQT_SG_BATCH");
        const bool sgBatchOn = !(sgBatchEnv && sgBatchEnv[0] == '0');
        doors.setSceneGraphBatchEnabled(sgBatchOn);
        LOG_INFO(MOD_GRAPHICS, "Door manager created (zone={}, cache={}, bsp={}, frustum={}, neighbors={}, shader={}, sgBatch={})",
                 (bool)ctx_.currentZone, ctx_.constrainedTextureCache != nullptr, (bool)ctx_.zoneBspTree,
                 (bool)ctx_.frustumCuller, !ctx_.regionNeighbors.empty(),
                 ctx_.zoneShader && ctx_.zoneShader->isAvailable(), sgBatchOn);
    }
    // Idempotent; needs both the cache and the manager, and the cache exists
    // from initLoadingScreen() on.
    ctx_.ZoneStream().ensureDoorTextureGuard();
}

void ZoneLifecycleComponent::setupInstantScene(const std::string& zoneName, float playerX, float playerY, float playerZ) {
    ctx_.currentZoneName = zoneName;
    ctx_.Diagnostics().loadIndoorRegionMap(zoneName);

    // S05: entityRenderer_ already created in initLoadingScreen()

    // Door manager for placeholder cubes (no model loading yet), with its
    // texture guard -- T468: through the one construction body.
    ensureDoorManager();

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
    ctx_.zoneReady = true;

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
    ctx_.regionWorldTriangles.clear();
    ctx_.objectCollision.clear();
    ctx_.doorCollisionData.clear();
    // T435 (B425): keep DoorManager's dirty set in step with the map it feeds.
    if (ctx_.doorManager) ctx_.doorManager->markAllCollisionDirty();
    if (zoneTriangleSelector_) { zoneTriangleSelector_->drop(); zoneTriangleSelector_ = nullptr; }
    if (terrainOnlySelector_) { terrainOnlySelector_->drop(); terrainOnlySelector_ = nullptr; }

    irr::scene::IMetaTriangleSelector* metaSelector = ctx_.smgr->createMetaTriangleSelector();
    if (!metaSelector) return;

    if (ctx_.zonePlaceholderNode && ctx_.zonePlaceholderNode->getMesh()) {
        auto* sel = ctx_.smgr->createTriangleSelector(ctx_.zonePlaceholderNode->getMesh(), ctx_.zonePlaceholderNode);
        if (sel) { metaSelector->addTriangleSelector(sel); sel->drop(); }
    }

    zoneTriangleSelector_ = metaSelector;

    terrainOnlySelector_ = ctx_.smgr->createMetaTriangleSelector();
    if (terrainOnlySelector_ && ctx_.zonePlaceholderNode && ctx_.zonePlaceholderNode->getMesh()) {
        auto* terrainMeta = static_cast<irr::scene::IMetaTriangleSelector*>(terrainOnlySelector_);
        auto* sel = ctx_.smgr->createTriangleSelector(ctx_.zonePlaceholderNode->getMesh(), ctx_.zonePlaceholderNode);
        if (sel) { terrainMeta->addTriangleSelector(sel); sel->drop(); }
    }

    ctx_.collisionManager = ctx_.smgr->getSceneCollisionManager();
    if (ctx_.cameraController && ctx_.collisionManager && zoneTriangleSelector_) {
        ctx_.cameraController->setCollisionManager(ctx_.collisionManager, zoneTriangleSelector_);
    }

    LOG_INFO(MOD_GRAPHICS, "HCMap collision setup complete");
}

void ZoneLifecycleComponent::storeZoneEnvironment(uint8_t skyType, uint8_t zoneType,
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

void ZoneLifecycleComponent::applyStoredZoneEnvironment() {
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

void ZoneLifecycleComponent::setExpectedEntityCount(size_t count) {
    if (owner_.isLoading()) return;
    expectedEntityCount_ = count;
    // Note: Entity count is tracked for informational purposes only.
    // Zone ready is triggered when the player entity is created (isPlayer=true in createEntity).
    LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, already loaded: {}", count, loadedEntityCount_);
}

void ZoneLifecycleComponent::setNetworkReady(bool ready) {
    if (owner_.isLoading()) return;
    networkReady_ = ready;
    LOG_DEBUG(MOD_GRAPHICS, "Network ready: {}", ready);

    if (!ready) {
        // Network not ready means we're zoning, reset entity loading state
        entitiesLoaded_ = false;
        expectedEntityCount_ = 0;
        loadedEntityCount_ = 0;
        ctx_.zoneReady = false;
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
    // T423 (board B362): OP_Weather is one of the four inputs to the scene
    // ambient, and it is the input that made B362 visible -- T418's first live
    // session forced rain at four intensities and the client never once
    // recomposed, because the recompute hung off the CLOCK packet alone.
    // Marked unconditionally: the recompute is idempotent, and gating it on
    // ctx_.weatherEffects would make the mark depend on a pointer the weather
    // input has nothing to do with.
    owner_.markSceneAmbientDirty();
}


} // namespace Graphics
} // namespace EQT
