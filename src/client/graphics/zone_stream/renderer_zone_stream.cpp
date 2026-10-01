#include "client/graphics/zone_stream/zone_stream_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/work_priority.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace EQT {
namespace Graphics {

void ZoneStreamComponent::indexObjectMeshes() {
    if (!ctx_.currentZone) {
        return;
    }

    ctx_.deferredObjects.clear();

    for (size_t i = 0; i < ctx_.currentZone->objects.size(); ++i) {
        const auto& objInstance = ctx_.currentZone->objects[i];
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        // P32: the software-path tree skip (isTreeObject + EDT_BURNINGSVIDEO
        // check) was removed — GLES2 is the only backend now, so
        // hasGpuShaders was always true here and this never actually
        // skipped anything on the surviving path. Trees are always indexed
        // normally for PVS/distance culling.

        DeferredObject deferred;
        deferred.objectIndex = i;
        deferred.meshBuilt = false;

        float x = objInstance.placeable->getX();
        float y = objInstance.placeable->getY();
        float z = objInstance.placeable->getZ();

        // Compute BSP region
        if (ctx_.zoneBspTree) {
            deferred.bspRegion = ctx_.zoneBspTree->findRegionIndexForPoint(x, y, z);
        }

        // Estimate world bounds from geometry extents
        const auto& geom = objInstance.geometry;
        float scaleX = objInstance.placeable->getScaleX();
        float scaleY = objInstance.placeable->getScaleY();
        float scaleZ = objInstance.placeable->getScaleZ();
        // Approximate bounding box in Irrlicht coords (x, z, y)
        float halfW = std::max(std::abs(geom->maxX - geom->minX), std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
        float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
        deferred.worldBounds = irr::core::aabbox3df(
            x - halfW, z - halfH, y - halfW,
            x + halfW, z + halfH, y + halfW);

        ctx_.deferredObjects.push_back(deferred);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Indexed {} deferred objects", ctx_.deferredObjects.size());
}

// T475 (board B670): buildDeferredObject() is deleted. Its only callers were the
// object texture-arrival drains, FrameComponent's Priority 9 and this file's
// P1.7, whose queue had no producer since Batch T's T04 (T473). Step 6
// (ZoneLoadComponent, renderer_zone_load_objects.cpp) builds every deferred
// object eagerly. Recover the body with
//   git show eaead058:src/client/graphics/zone_stream/renderer_zone_stream.cpp
void ZoneStreamComponent::processFrameProgressiveLoad() {
    if (!ctx_.progressiveLoadingActive) return;

    // Promote background-preloaded model data to main cache.
    // This moves RaceModelData from the staging map to loadedModels_ so that
    // getMeshForRace() can find it. Must happen before any buildEntityMesh() calls.
    if (ctx_.entityPrepReady && ctx_.entityRenderer && ctx_.entityRenderer->getRaceModelLoader()) {
        ctx_.entityRenderer->getRaceModelLoader()->promotePreparedModels();
    }

    // --- Critical tasks: always proceed regardless of governor state ---

    // P1 (player's BSP region, deferred/lazy build when the mesh cache was
    // active) removed T67 — regions are always built eagerly (SEQ Step4),
    // so the player's region already has a node by the time this runs.

    // Player entity goes through processOneEntityBuildStep() like other entities
    // (no synchronous build here — avoids 7.6s render thread stall from disk I/O)

    // B973 (GitLab #291): a progressive load re-armed after zone-in (T111: an
    // unbuilt entity entered the visible set) runs WITHOUT the loading screen,
    // in the middle of play. Its only work is entity work, and it takes the
    // same per-frame slice as the post-load path: guaranteed progress in every
    // governor state. The loading-screen path below is unchanged, and so is
    // everything under WILLEQ_ENTITY_BUILD_LEGACY=1 (the old GREEN gate).
    if (!ctx_.loadingScreenVisible && ctx_.entityRenderer &&
        !ctx_.entityRenderer->buildScheduler().legacy()) {
        ctx_.Frame().runEntityBuildSlice();
        checkProgressiveLoadingComplete();
        return;
    }

    // Update loading screen progress during asset loading.
    // --- GREEN gate: exactly ONE non-critical step per frame when GREEN ---
    // Bypass when loading screen is visible (no scene to compete with).
    if (ctx_.governor && !ctx_.loadingScreenVisible && ctx_.governor->getState() != BudgetState::Green) {
        static int progGovLog = 0;
        if (++progGovLog % 300 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "Progressive: GREEN gate blocked — governor={}",
                      ctx_.governor->getStateName());
        }
        // Populate pending queue even when not GREEN (zero-cost, main-thread-only).
        // Do NOT dispatch — worker stays idle during non-GREEN frames.
        if (++ctx_.entityPrepScanCounter % 10 == 0) {
            queueEntityPrepRequests();
        }
        checkProgressiveLoadingComplete();
        return;
    }

    bool didWork = false;
    // During loading screen, never mark didWork so all priorities run each frame
    // (no scene rendering to compete with).
    const bool canMarkWork = !ctx_.loadingScreenVisible;
    auto stepStart = std::chrono::steady_clock::now();

    // Lightweight entity ops (always run under GREEN, no didWork cost):
    if (ctx_.entityPrepReady && ctx_.entityRenderer && !ctx_.config.constrainedConfig.skipEntityBuild) {
        if (++ctx_.entityPrepScanCounter % 10 == 0) {
            queueEntityPrepRequests();
        }
        if (ctx_.entityPrepWorker && ctx_.entityPrepWorker->isIdle()) {
            ctx_.entityPrepWorker->dispatchOne();
        }
    }

    // Priority 1: Heavy entity ops (cost didWork budget)
    if (!didWork && ctx_.entityPrepReady && ctx_.entityRenderer && !ctx_.config.constrainedConfig.skipEntityBuild) {
        // Poll completed prep results — distributing a result consumes the budget
        if (ctx_.entityRenderer->pollAndDistributePrepResults()) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_poll", 0, stepStart);
        }
    }
    if (!didWork && ctx_.entityPrepReady && ctx_.entityRenderer && !ctx_.config.constrainedConfig.skipEntityBuild) {
        if (ctx_.entityRenderer->processOneEntityBuildStep(ctx_.currentPvsRegion)) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_step", 0, stepStart);
            // Report entity progress to chat
            size_t totalEntities = 0, builtEntities = 0;
            for (const auto& [id, vis] : ctx_.entityRenderer->getEntities()) {
                if (vis.inSceneGraph) {
                    totalEntities++;
                    if (vis.meshBuilt) builtEntities++;
                }
            }
            sendLoadProgress(fmt::format("[Load] Entity {}/{}", builtEntities, totalEntities));
        }
    }

    // Priority 1.5 (region rebuild on texture arrival) removed T67 -- gated on
    // `constrainedMeshCache_ &&`, which was never true (the cache was never
    // constructed). See src/client/graphics/CLAUDE.md for the removed cache's
    // reference pointer.

    // Priorities 1.6 and 1.7 (rebuild one door / PVS object whose texture
    // "arrived") removed T475 (board B670): their queues were fed only by the
    // GPU texture-arrival arm, which had no producer since Batch T's T04 made
    // texture loading synchronous (T473). A texture missing at build time is
    // not re-textured later -- board B671.

    // Priority 2 (build one PVS neighbor region from meshLoadQueue_) removed
    // T67 — gated on `constrainedMeshCache_`, never true; regions are
    // always already built eagerly (SEQ Step4). meshLoadQueue_ itself
    // (populated only by the same dead-gated path) was removed too.

    // Priority 3 (progressive door build) and Priority 4 (progressive PVS
    // object build) removed T292. Both were gated on protectedRegions_, the
    // ConstrainedMeshCache eviction-protection bitvector whose only writer
    // T67 deleted with the cache: the vector was permanently all-false, so
    // getDoorsInRegions() always returned an empty candidate set and every
    // deferredObjects_ candidate test was permanently false. Neither block's
    // body could execute, and neither could set didWork. Doors and objects
    // are built eagerly at zone load (SEQ Step7 / renderer_zone_load_objects)
    // -- that is the path that has actually been running. See
    // src/client/graphics/CLAUDE.md for the removed cache's reference
    // pointer.

    // Priority 5: Lazy icon extraction (disabled — WindowManager removed)

    // Always queue background prep requests (free work on background thread)
    queueEntityPrepRequests();

    // Mesh cache eviction removed T67 — gated on `constrainedMeshCache_`,
    // never true (no cache was ever constructed to evict from).

    if (didWork) {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: 1 step this frame ({:.1f}ms remaining, gov=GREEN)",
            ctx_.governor ? ctx_.governor->getRemainingBudgetMs() : 0.0f);
    }

    checkProgressiveLoadingComplete();
}

uint8_t ZoneStreamComponent::getRegionPvsDepth(size_t regionIdx) const {
    if (!ctx_.regionPvsDepthMap) return 255;
    auto it = ctx_.regionPvsDepthMap->find(regionIdx);
    return (it != ctx_.regionPvsDepthMap->end()) ? it->second : 255;
}

void ZoneStreamComponent::rebuildPvsNeighborhood(size_t anchorRegion) {
    pvsNeighborhood_.clear();
    pvsNeighborhoodAnchor_ = anchorRegion;
    if (anchorRegion == SIZE_MAX || ctx_.regionNeighbors.empty()) return;

    const int K = ctx_.config.constrainedConfig.pvsNeighborhoodHops;
    if (K <= 0) return;  // Disabled: fire onPvsRegionChanged on every crossing

    struct Entry { size_t region; int depth; };
    std::vector<Entry> queue;
    queue.push_back({anchorRegion, 0});
    pvsNeighborhood_.insert(anchorRegion);

    size_t head = 0;
    while (head < queue.size()) {
        auto [fromRegion, depth] = queue[head++];
        if (depth >= K) continue;
        auto it = ctx_.regionNeighbors.find(fromRegion);
        if (it == ctx_.regionNeighbors.end()) continue;
        for (size_t neighbor : it->second) {
            if (pvsNeighborhood_.insert(neighbor).second) {
                queue.push_back({neighbor, depth + 1});
            }
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "PVS neighborhood rebuilt: anchor={}, {} regions (K={})",
              anchorRegion, pvsNeighborhood_.size(), K);
}

bool ZoneStreamComponent::isInPvsNeighborhood(size_t regionIdx) const {
    return pvsNeighborhoodAnchor_ != SIZE_MAX &&
           pvsNeighborhood_.count(regionIdx) > 0;
}

void ZoneStreamComponent::onPvsRegionChanged() {
    // Re-sort entity prep pending queue with updated PVS depths
    if (ctx_.entityPrepWorker) {
        ctx_.entityPrepWorker->updateDepths([this](size_t bspRegion) {
            return getRegionPvsDepth(bspRegion);
        });
    }

#ifdef EQT_HAS_GLES2
    // Re-prioritize GPU upload queue
    if (ctx_.gpuUploadThread && ctx_.gpuUploadThread->isAvailable()) {
        ctx_.gpuUploadThread->reprioritize([this](const UploadRequest& req) -> uint32_t {
            // T475: every request is a region VBO now (GPUUploadThread lost its
            // texture types with the dead arrival path), so this routes on the
            // request TYPE. It used to route on the key's high byte, which also
            // carried the deleted constrained-cache texture branch (byte 3).
            if (req.type == UploadRequestType::VertexBuffer) {
                // VBO upload: low 48 bits = region index
                size_t regionIdx = req.callbackKey & 0xFFFFFFFFFFFFULL;
                uint8_t depth = getRegionPvsDepth(regionIdx);
                return WorkPriorityKey::make(depth, AssetType::ZoneMesh).value;
            }
            return WorkPriorityKey::makeNonSpatial(AssetType::Icon).value;
        });
    }
#endif
    // Note: camera BSP collision needs no region update here -- since T464 the
    // CameraController looks up the player's eye region itself
    // on every region change, not gated by the neighborhood.
}

void ZoneStreamComponent::queueEntityPrepRequests() {
    if (!ctx_.entityPrepWorker || !ctx_.entityRenderer || !ctx_.entityPrepReady) return;

    std::vector<uint16_t> unbuilt;
    ctx_.entityRenderer->getUnbuiltEntities(unbuilt);
    if (unbuilt.empty()) return;

    const bool pvsValid = (ctx_.currentPvsRegion != SIZE_MAX);

    const auto& entities = ctx_.entityRenderer->getEntities();
    for (auto spawnId : unbuilt) {
        if (ctx_.entityRenderer->isEntityMeshBuilt(spawnId)) continue;
        auto it = entities.find(spawnId);
        if (it == entities.end()) continue;
        const auto& vis = it->second;
        // Don't prep invisible entities — matches build step visibility filter
        if (!vis.inSceneGraph) continue;
        // Don't re-queue entities whose background prep already completed
        if (vis.entityPrepComplete) continue;

        // When player is outside all BSP regions (SIZE_MAX), only build the player entity
        if (!pvsValid && spawnId != ctx_.PlayerCamera().playerSpawnId()) continue;

        // PVS depth gate: queue entities within configured portal hop distance
        uint8_t depth = 255;
        if (spawnId == ctx_.PlayerCamera().playerSpawnId()) {
            depth = 0;
        } else {
            depth = getRegionPvsDepth(vis.cachedBspRegion);
        }
        if (depth > static_cast<uint8_t>(ctx_.config.constrainedConfig.entityPrepMaxPvsDepth)) continue;

        // T266: a race that was not in the load-time spawn list (roamer, pet,
        // GM/summoned NPC, illusion) gets its archive queued as an EQE1
        // candidate BEFORE its prep job can ask for a texture. Deduped per
        // race inside; a no-op for every race zone load already queued.
        if (ctx_.Eqe1().addSpawnedRaceEQE1Candidate(vis.raceId)) {
            LOG_INFO(MOD_GRAPHICS, "EQE1: race {} spawned after zone load -> its archive "
                     "queued as a late candidate (spawn={})", vis.raceId, spawnId);
        }

        // Per-entity dedup: each entity gets its own prep job (equipment/variant work differs)
        if (!ctx_.entityPrepWorker->isPendingForEntity(spawnId)) {
            EntityPrepWorker::PrepRequest req;
            req.spawnId = spawnId;
            req.raceId = vis.raceId;
            req.gender = vis.gender;
            req.appearance = vis.appearance;
            req.pvsDepth = depth;
            req.bspRegion = vis.cachedBspRegion;
            ctx_.entityPrepWorker->requestPrep(req);
        }
    }
}

void ZoneStreamComponent::logAssetBuildTime(const char* type, size_t id,
    std::chrono::steady_clock::time_point start) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() / 1000.0f;
    if (elapsed > 10.0f) {
        LOG_WARN(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms (budget warning)", type, id, elapsed);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms", type, id, elapsed);
    }
}

void ZoneStreamComponent::sendLoadProgress(const std::string& msg) {
    // WindowManager removed — load progress is logged only
    LOG_DEBUG(MOD_GRAPHICS, "LoadProgress: {}", msg);
}

bool ZoneStreamComponent::isEntityBlockingProgressiveLoad(bool meshBuilt, bool inSceneGraph,
                                                        bool isPlayerEntity, bool pvsValid,
                                                        uint8_t regionPvsDepth,
                                                        uint8_t entityPrepMaxPvsDepth) {
    // inSceneGraph is NOT "has a scene node" — applySimulationOutput()
    // (renderer_sim_bridge.cpp) toggles it every frame to reflect the
    // 40-entity render cap / render-distance culling result, independent of
    // whether a mesh has ever been built. An entity T109's load-distance
    // gate deferred sits with inSceneGraph=false until the player gets close
    // enough to bring it into the visible set — completion correctly does
    // NOT wait on it before then (queueEntityPrepRequests() wouldn't queue
    // it either — same gate). See applySimulationOutput()'s matching T111
    // comment for the re-arm this implies is needed on the OTHER side: when
    // inSceneGraph flips false->true for a still-unbuilt entity.
    if (meshBuilt || !inSceneGraph) return false;
    if (isPlayerEntity) return true;
    // When the player's own BSP region is unknown, queueEntityPrepRequests()
    // won't queue this entity yet either (it has the identical early-continue)
    // — but that means we cannot yet rule it OUT as blocking. Treat it as
    // blocking (not "doesn't count") until pvsValid is known.
    //
    // Live-caught during this unit's own proof session: sequentialLoadComplete_
    // can flip true (and this function starts being called) in the single
    // frame BEFORE the SimulationWorker background thread's first real
    // swapAndGetResults() ever establishes currentPvsRegion_ — the
    // synchronous Step12b "first-frame" computation logs
    // currentPvsRegion_=SIZE_MAX at that exact instant. Returning false here
    // (as an earlier version of this fix did) made checkProgressiveLoadingComplete()
    // declare victory one frame after Step 8, before a single deferred
    // entity had a chance to be queued — a NEW, faster way to reproduce
    // T109's original bug (89/178 Befallen entities never picked up),
    // confirmed live via /pmem staying at a fixed "modeled" count across
    // repeated snapshots and multiple region-crossing /move commands. See
    // refactor/T111_progressive_loading_rearm.md's Review.
    if (!pvsValid) return true;
    // Same gate queueEntityPrepRequests() uses. entityPrepMaxPvsDepth ships
    // at 255 (unbounded) on both presets today, so in practice this counts
    // every unbuilt, in-scene-graph entity zone-wide — deliberately NOT
    // scoped to the player's current region (that scoping was T109's bug:
    // entities deferred by the T109 load-distance gate outside the landing
    // region were invisible to this count, so completion declared itself
    // done — and permanently stopped — while 82-89/178 entities in Befallen
    // sat forever in Placeholder phase).
    return regionPvsDepth <= entityPrepMaxPvsDepth;
}

void ZoneStreamComponent::checkProgressiveLoadingComplete() {
    if (!ctx_.progressiveLoadingActive) return;

    // Phase pipeline must reach Complete before we can declare progressive loading done.
    // Otherwise zero counts just mean "nothing queued yet" (pre-zone-load).
    if (!ctx_.sequentialLoadComplete) return;

    // Count unbuilt assets
    size_t unbuiltEntities = 0;

    if (ctx_.entityRenderer) {
        const bool pvsValid = (ctx_.currentPvsRegion != SIZE_MAX);
        const uint8_t maxPrepDepth = static_cast<uint8_t>(ctx_.config.constrainedConfig.entityPrepMaxPvsDepth);
        for (const auto& [id, vis] : ctx_.entityRenderer->getEntities()) {
            const bool isPlayerEntity = (id == ctx_.PlayerCamera().playerSpawnId());
            const uint8_t depth = isPlayerEntity ? 0 : getRegionPvsDepth(vis.cachedBspRegion);
            if (isEntityBlockingProgressiveLoad(vis.meshBuilt, vis.inSceneGraph, isPlayerEntity,
                                                 pvsValid, depth, maxPrepDepth)) {
                unbuiltEntities++;
            }
        }
    }

    // T292: the unbuiltDoors/unbuiltObjects counters that used to live here
    // both read protectedRegions_ (permanently all-false since T67 removed
    // the ConstrainedMeshCache) and were therefore pinned to 0 regardless of
    // real pending state. Removed along with the bitvector; the completion
    // test below is exactly what has been evaluated in every shipped build.

    // Periodic status log for progressive loading (every 5 seconds)
    static int progCheckLog = 0;
    if (++progCheckLog % 150 == 1) {
        // unbuiltRegions was mesh-cache-derived (meshLoadQueue_ vs. cache's
        // isLoaded()) and always 0 in production — removed T67 along with
        // the cache. Regions are always built eagerly (SEQ Step4).
        const size_t unbuiltRegions = 0;
        bool pendingTextures = ctx_.constrainedTextureCache && ctx_.constrainedTextureCache->hasPendingWork();
        size_t pendingVBOs = 0;
#ifdef EQT_HAS_GLES2
        pendingVBOs = owner_.pendingVBOUploads_.size();
#endif
        LOG_INFO(MOD_GRAPHICS, "Progressive status: entities={} "
                 "queuedRegions={} pendingVBOs={} pendingTextures={} "
                 "| governor={} | progressive={}",
                 unbuiltEntities, unbuiltRegions,
                 pendingVBOs, pendingTextures,
                 ctx_.governor ? ctx_.governor->getStateName() : "N/A",
                 ctx_.progressiveLoadingActive);
    }

    if (unbuiltEntities == 0) {
        // Also wait for async GPU work (decoded-not-uploaded textures, VBOs)
        // before declaring progressive loading complete. Without this, the loading
        // screen hides while textures are still in flight → garbled first frame.
        bool gpuWorkPending = false;
#ifdef EQT_HAS_GLES2
        if (!owner_.pendingVBOUploads_.empty())
            gpuWorkPending = true;
#endif
        if (ctx_.constrainedTextureCache && ctx_.constrainedTextureCache->hasPendingWork())
            gpuWorkPending = true;
        // T475: the object/region texture rebuild queues this also waited on
        // were never filled (board B670) and are deleted.
        if (gpuWorkPending) return;

        ctx_.progressiveLoadingActive = false;

        // EntityPrepWorker stays alive for the entire session — entities move
        // in and out of the scene graph during gameplay and need lazy prep.

        // Release object texture pixel data — all deferred objects and doors are built.
        if (ctx_.currentZone && !ctx_.currentZone->objectTextures.empty()) {
            size_t freed = 0;
            for (auto& [name, texInfo] : ctx_.currentZone->objectTextures) {
                if (!texInfo) continue;
                freed += texInfo->data.capacity();
                texInfo->data.clear();
                texInfo->data.shrink_to_fit();
                for (auto& frame : texInfo->frames) {
                    freed += frame.data.capacity();
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
            if (freed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f}KB object texture pixel data (progressive complete)",
                         freed / 1024.0f);
            }
        }

        // Remove zone placeholder mesh — real geometry is fully loaded
        ctx_.ZoneLifecycle().destroyZonePlaceholder();

        // Build portal system now that all geometry is loaded. Portal occlusion
        // is an optimization (~472ms build cost) — deferred from loadZone to avoid
        // poisoning the governor during progressive loading.
        if (ctx_.portalBuildPending && ctx_.zoneBspTree && !ctx_.regionBoundingBoxes.empty()) {
            // Compute zone bounds from region bounding boxes (EQ coords)
            float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
            float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;
            for (const auto& [idx, bbox] : ctx_.regionBoundingBoxes) {
                zbMinX = std::min(zbMinX, bbox.MinEdge.X); zbMinY = std::min(zbMinY, bbox.MinEdge.Y); zbMinZ = std::min(zbMinZ, bbox.MinEdge.Z);
                zbMaxX = std::max(zbMaxX, bbox.MaxEdge.X); zbMaxY = std::max(zbMaxY, bbox.MaxEdge.Y); zbMaxZ = std::max(zbMaxZ, bbox.MaxEdge.Z);
            }
            ctx_.portalSystem = std::make_unique<PortalSystem>();
            ctx_.portalSystem->buildFromBsp(*ctx_.zoneBspTree, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
            ctx_.portalOcclusionEligible = ctx_.portalSystem->hasPortals() &&
                                       (ctx_.portalSystem->getData().portals.size() > 10);
            if (ctx_.portalOcclusionEligible) {
                LOG_INFO(MOD_GRAPHICS, "Portal occlusion eligible: {} portals (post-load build)",
                         ctx_.portalSystem->getData().portals.size());
                // Auto-enable portal occlusion (post-load path)
                if (ctx_.config.constrainedConfig.portalOcclusion &&
                    ctx_.config.constrainedConfig.enableStencilBuffer) {
                    if (!ctx_.indoorRegions.empty()) {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode (post-load, {} indoor regions)",
                                 ctx_.indoorRegions.size());
                    } else {
                        size_t portalCount = ctx_.portalSystem->getData().portals.size();
                        size_t regionCount = ctx_.regionBoundingBoxes.size();
                        float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                        if (portalRatio <= 3.0f) {
                            ctx_.portalOcclusionEnabled = true;
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled (post-load): {:.1f} ratio (indoor)", portalRatio);
                        } else {
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled (post-load): {:.1f} ratio (outdoor)", portalRatio);
                        }
                    }
                }
            }
            ctx_.portalBuildPending = false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - ctx_.progressiveLoadStartTime).count();
        LOG_INFO(MOD_GRAPHICS, "Progressive loading complete: {}ms total streaming time", elapsed);
        sendLoadProgress(fmt::format("[Load] Complete ({:.1f}s)", elapsed / 1000.0f));

        // Regenerate detail chunks now that all terrain is in the selector
        // (chunks generated during progressive loading had incomplete terrain raycasts)
        if (ctx_.detailManager && ctx_.detailManager->isEnabled()) {
            ctx_.detailManager->setEnabled(false);
            ctx_.detailManager->setEnabled(true);
            LOG_INFO(MOD_GRAPHICS, "Regenerating detail chunks with complete terrain");
        }

        // Release raw texture data now that all models are built
        if (ctx_.config.constrainedConfig.releaseTextureDataAfterUpload && ctx_.entityRenderer) {
            size_t totalFreed = 0;
            if (auto* eml = ctx_.entityRenderer->getEquipmentModelLoader()) {
                totalFreed += eml->releaseRawTextureData();
            }
            if (auto* rml = ctx_.entityRenderer->getRaceModelLoader()) {
                totalFreed += rml->releaseRawTextureData();
            }
            if (totalFreed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f} KB of raw texture data after progressive load",
                    totalFreed / 1024.0f);
            }
        }
    }
}

} // namespace Graphics
} // namespace EQT
