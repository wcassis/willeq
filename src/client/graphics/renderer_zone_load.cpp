// loadZoneSequential() and the zone-geometry half of its step sequence:
// step 0 (EQE1), 1 (S3D), 2 (BSP), 4 (Regions), 5 (Assets).
//
// T166 split this file at 2996 lines -- it sat exactly on
// check_file_sizes.sh's 3000-line FAIL ceiling and had begun distorting the
// code inside it (T115's fix had to be compressed onto a shared line to
// fit). The remaining steps live in the sibling renderer_zone_load_*.cpp
// files; ZoneLoadContext moved to include/client/graphics/zone_load_context.h
// so all four TUs can see it. The driver stays here.

#include "client/graphics/irrlicht_renderer.h"
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/load_mem_audit.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/inventory/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"
#include <fmt/format.h>
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <sstream>

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif
#include "client/graphics/zone_load_context.h"

// Decode BMP data to A8R8G8B8 pixel array (used by sky texture loading)
// T14: decodeBMPtoARGB and bilinearUpscaleARGB removed.
// All texture decoding now goes through ConstrainedTextureCache::getOrLoad().

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::loadZoneStep0_EQE1(ZoneLoadContext& ctx) {
    // ── Step 0: EQE1 — open flat pack files (if configured) ────────────────
    if (!config_.constrainedConfig.eqe1Path.empty()) {
        ctx.updateProgress(48, "Opening EQE1 textures...");

        // P04: open the (small) zone-specific archives eagerly — they're
        // always needed. Global archives (the ~33-file, 139MB-of-mostly-
        // unreferenced-bytes set P03 measured) are NOT opened here anymore;
        // they're queued and opened lazily, one at a time, only when
        // ConstrainedTextureCache actually misses a lookup against what's
        // already open. Most zones only ever touch a handful of them.
        auto archiveList = eqe1_->resolveEQE1Archives(currentZoneName_);
        for (const auto& name : archiveList.zoneArchives) {
            eqe1_->getOrOpenEQE1(name);
        }
        {
            std::lock_guard<std::mutex> lock(eqe1CandidatesMutex_);
            pendingGlobalEQE1Archives_ = archiveList.globalArchives;
            eqe1CandidateRaces_.clear();
            eqe1LoadTimeCandidateCount_ = 0;
        }

        // T199: ADD THE ARCHIVES THE ZONE'S ACTUAL SPAWNS NEED.
        //
        // eqe1_archives.json's global list is static, and a static list cannot
        // be right: what spawns is decided server-side and varies per zone.
        // It was simultaneously too wide (every global*_chr and gequip*) and
        // too narrow — a race whose model lives in ANOTHER zone's _chr pack was
        // unreachable. Befallen spawns race 24 (Fish), whose model is in
        // akanon_chr (race_models.json's s3d_file); the client opened
        // befallen*/global* only, so `fishe0301.bmp` was not found and the
        // all-or-nothing rule turned that into a fatal exit (T198).
        //
        // The spawn list is already available here:
        // Application::loadZoneGraphicsOnThread() (application_zone_loading.cpp)
        // registered every snapshot entity into entityRenderer_ before calling
        // loadZoneSequential, so the distinct race set is known before a single
        // archive is opened.
        //
        // These are APPENDED TO THE LAZY LIST, not opened. Entries are mmap'd
        // one at a time only when ConstrainedTextureCache actually misses, so
        // a race whose textures are already served by an open archive costs
        // nothing — which is the "prefer what is already open" rule, obtained
        // for free from the existing lazy mechanism rather than by consulting
        // race_models.json's `also_in` (73 entries for race 24 alone; opening
        // them would be exactly the over-loading this exists to prevent).
        //
        // T266: the same extension now also runs for every race that reaches
        // the entity prep queue AFTER load (queueEntityPrepRequests), and a
        // name no candidate holds opens its owner (openArchiveContaining).
        // Both are tagged late-open in the request log; this block is the
        // load-time set.
        if (entityRenderer_) {
            std::set<uint16_t> spawnedRaces;
            for (const auto& [spawnId, vis] : entityRenderer_->getEntities()) {
                spawnedRaces.insert(vis.raceId);
            }
            size_t added = 0;
            for (uint16_t raceId : spawnedRaces) {
                if (eqe1_->addSpawnedRaceEQE1Candidate(raceId)) ++added;
            }
            LOG_INFO(MOD_GRAPHICS, "EQE1: {} distinct spawned races -> {} archive(s) queued "
                     "beyond the static list", spawnedRaces.size(), added);
        }
        {
            std::lock_guard<std::mutex> lock(eqe1CandidatesMutex_);
            eqe1LoadTimeCandidateCount_ = pendingGlobalEQE1Archives_.size();
        }

        // T207: the name -> archive index turns a lookup miss into at most one
        // open (and a genuinely absent name into zero), replacing the probe that
        // walked this whole list. Loaded once; the candidate set above is what
        // varies per zone.
        if (!eqe1Index_.isLoaded()) {
            if (!eqe1Index_.load("data/eqe1_texture_index.json")) {
                LOG_ERROR(MOD_GRAPHICS,
                          "EQE1: no data/eqe1_texture_index.json -- EQE1 lookups "
                          "will resolve nothing. Rebuild it with "
                          "eqe1_texture_index_builder --eqe1-path {}",
                          config_.constrainedConfig.eqe1Path);
            }
        }
        if (constrainedTextureCache_) {
            constrainedTextureCache_->setEQE1ArchiveResolver(
                [this](const std::string& n) { return eqe1_->openArchiveContaining(n); });
        }

        // T284: composite entity sheets are NOT bulk-opened.
        //
        // This used to scan config_.constrainedConfig.compositeEntityPath and
        // open EVERY .eqe1 in it. The shipped arrangement puts the sheet JSONs
        // beside the packs in the ETC1 tree (compositeEntityPath == eqe1Path),
        // so that scan mapped all 1506 packs / 4.44 GiB of the tree at step 0
        // to reach the handful of sheets a zone's races actually use. On the
        // 32-bit ARM Pi that scan was T195's address-space exhaustion (VmSize
        // peaked at 3.13 GB of a ~3 GB ceiling, std::bad_alloc from mmap, not
        // from a shortage of RAM).
        //
        // NOTE: that exhaustion is NOT why composite entities are off on the
        // orangepi preset any more. Deleting this scan killed the
        // address-space argument outright (T289 measured VmPeak 3,128 MB ->
        // 575 MB on the Pi). Composites stay off for their RESIDENT cost
        // instead -- see docs/decisions.md 2026-08-27, "Composite entities stay
        // OFF on the orangepi". Do not cite the T195/T198 rationale again.
        //
        // Nothing replaces it here, because nothing has to: a composite sheet
        // name is requested by RaceModelLoader::preloadModelData() through
        // ConstrainedTextureCache::getOrLoad(), which misses on the open set
        // and asks the resolver installed just above. That is
        // openArchiveContaining() -- T207's index lookup plus T266's rule-R2
        // late-open -- and it opens exactly the ONE pack that holds the name,
        // or none at all when the name is in no pack (which stays the
        // all-or-nothing FATAL, decision #2). T281 made the index know all 498
        // sheet packs, which is what made this possible.
        //
        // The one case the index cannot answer is a compositeEntityPath that
        // is NOT the indexed eqe1Path; openCompositeSheetEQE1() covers it (see
        // openArchiveContaining()).
    }
}

bool IrrlichtRenderer::loadZoneStep1_S3D(ZoneLoadContext& ctx) {
    // ── Step 1: S3D — parse zone archive ───────────────────────────────────
    ctx.updateProgress(50, "Loading zone data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        std::string zonePath = ctx.eqPath + currentZoneName_ + ".s3d";
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loading S3D from '{}'", zonePath);

        S3DLoadOptions loadOptions;
        loadOptions.loadCharacters = false;
        loadOptions.computeCombinedGeometry = true;
        loadOptions.loadObjects = !config_.constrainedConfig.skipObjectBuild;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loadOptions: chars={}, combinedGeom={}, objects={}",
                  loadOptions.loadCharacters, loadOptions.computeCombinedGeometry, loadOptions.loadObjects);

        S3DLoader loader;
        if (!loader.loadZone(zonePath, loadOptions)) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load zone S3D archive: {} ({})", zonePath, loader.getError());
            pendingZoneComputations_.reset();
            loadFailed_ = true;
            return false;
        }
        currentZone_ = loader.getZone();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: S3D loaded: objects={}, lights={}, textures={}, "
                  "wldLoader={}, geometry={}",
                  currentZone_ ? currentZone_->objects.size() : 0,
                  currentZone_ ? currentZone_->lights.size() : 0,
                  currentZone_ ? currentZone_->textures.size() : 0,
                  currentZone_ ? (bool)currentZone_->wldLoader : false,
                  currentZone_ ? (bool)currentZone_->geometry : false);
        zoneStream_->logAssetBuildTime("seq_s3d_parse", 0, stepStart);
    }
    FlushThreadLog();

    // Install S3D data
    if (!currentZone_) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneSequential: S3D produced no data");
        pendingZoneComputations_.reset();
        loadFailed_ = true;
        return false;
    }
    if (entityRenderer_) {
        entityRenderer_->setCurrentZone(currentZoneName_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: entityRenderer_ zone set to '{}'", currentZoneName_);
    }
    if (doorManager_) {
        doorManager_->setZone(currentZone_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ zone set");
        if (constrainedTextureCache_) {
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ constrained texture cache set");
        }
        if (zoneShader_ && zoneShader_->isAvailable()) {
            doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest(),
                                                 zoneShader_->getActiveAlphaTestRGBA());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ shader materials set (solid={}, alphaTest={})",
                      zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
        }
    }
    return true;
}

void IrrlichtRenderer::loadZoneStep2_BSP(ZoneLoadContext& ctx) {
    // ── Step 2: BSP — compute spatial data ─────────────────────────────────
    ctx.updateProgress(55, "Computing spatial data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        auto zone = currentZone_;
        bool skipObjectBuild = config_.constrainedConfig.skipObjectBuild;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: zone={}, wldLoader={}, skipObjectBuild={}",
                  (bool)zone, zone ? (bool)zone->wldLoader : false, skipObjectBuild);

        if (zone && zone->wldLoader) {
            auto wldLoader = zone->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: bspTree={}, regions={}, hasPvsData={}",
                      (bool)bspTree, bspTree ? bspTree->regions.size() : 0,
                      wldLoader->hasPvsData());
            if (bspTree && !bspTree->regions.empty() && wldLoader->hasPvsData()) {
                // Compute region bounding boxes
                size_t regionsSkipped = 0;
                for (size_t i = 0; i < bspTree->regions.size(); ++i) {
                    auto geom = wldLoader->getGeometryForRegion(i);
                    if (!geom || geom->vertices.empty()) { ++regionsSkipped; continue; }

                    float vMinX = std::numeric_limits<float>::max();
                    float vMinY = vMinX, vMinZ = vMinX;
                    float vMaxX = std::numeric_limits<float>::lowest();
                    float vMaxY = vMaxX, vMaxZ = vMaxX;
                    for (const auto& v : geom->vertices) {
                        float wx = geom->centerX + v.x;
                        float wy = geom->centerY + v.y;
                        float wz = geom->centerZ + v.z;
                        if (wx < vMinX) vMinX = wx; if (wy < vMinY) vMinY = wy; if (wz < vMinZ) vMinZ = wz;
                        if (wx > vMaxX) vMaxX = wx; if (wy > vMaxY) vMaxY = wy; if (wz > vMaxZ) vMaxZ = wz;
                    }
                    irr::core::aabbox3df worldBounds;
                    worldBounds.MinEdge = irr::core::vector3df(vMinX, vMinY, vMinZ);
                    worldBounds.MaxEdge = irr::core::vector3df(vMaxX, vMaxY, vMaxZ);
                    computations->regionBoundingBoxes[i] = worldBounds;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: computed {} region bounding boxes ({} regions skipped, no geometry)",
                         computations->regionBoundingBoxes.size(), regionsSkipped);

                // Compute zone bounds from region bounding boxes (EQ coords)
                float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
                float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;
                for (const auto& [idx, bbox] : computations->regionBoundingBoxes) {
                    zbMinX = std::min(zbMinX, bbox.MinEdge.X); zbMinY = std::min(zbMinY, bbox.MinEdge.Y); zbMinZ = std::min(zbMinZ, bbox.MinEdge.Z);
                    zbMaxX = std::max(zbMaxX, bbox.MaxEdge.X); zbMaxY = std::max(zbMaxY, bbox.MaxEdge.Y); zbMaxZ = std::max(zbMaxZ, bbox.MaxEdge.Z);
                }

                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: zone bounds: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}] Z=[{:.1f},{:.1f}]",
                          zbMinX, zbMaxX, zbMinY, zbMaxY, zbMinZ, zbMaxZ);

                // Build portal system from BSP split planes
                computations->portalSystem = std::make_unique<PortalSystem>();
                computations->portalSystem->buildFromBsp(*bspTree, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
                computations->portalOcclusionEligible = computations->portalSystem->hasPortals() &&
                    (computations->portalSystem->getData().portals.size() > 10);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: portal system: hasPortals={}, portalCount={}, occlusionEligible={}",
                          computations->portalSystem->hasPortals(),
                          computations->portalSystem->getData().portals.size(),
                          computations->portalOcclusionEligible);

                // Cache zone light BSP regions
                for (size_t i = 0; i < zone->lights.size(); ++i) {
                    const auto& light = zone->lights[i];
                    size_t regionIdx = bspTree->findRegionIndexForPoint(light->x, light->y, light->z);
                    computations->zoneLightRegions.push_back(regionIdx);
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: cached {} zone light BSP regions", computations->zoneLightRegions.size());
            }

            // Index objects with BSP regions
            if (!skipObjectBuild) {
                auto bspTree2 = wldLoader->getBspTree();
                if (bspTree2) {
                    for (size_t i = 0; i < zone->objects.size(); ++i) {
                        const auto& objInstance = zone->objects[i];
                        if (!objInstance.geometry || !objInstance.placeable) continue;
                        float x = objInstance.placeable->getX();
                        float y = objInstance.placeable->getY();
                        float z = objInstance.placeable->getZ();
                        size_t bspRegion = bspTree2->findRegionIndexForPoint(x, y, z);
                        computations->deferredObjectEntries.emplace_back(i, bspRegion);
                    }
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: object BSP indexing: {} objects, {} entries (bspTree={})",
                          zone->objects.size(), computations->deferredObjectEntries.size(), (bool)bspTree2);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: object BSP indexing skipped (skipObjectBuild=true)");
            }

            // Pre-build deferred objects with tree filtering + world bounds
            if (!computations->deferredObjectEntries.empty()) {
                for (auto& [objIdx, bspRegion] : computations->deferredObjectEntries) {
                    if (objIdx >= zone->objects.size()) continue;
                    const auto& objInstance = zone->objects[objIdx];
                    if (!objInstance.geometry || !objInstance.placeable) continue;

                    DeferredObject deferred;
                    deferred.objectIndex = objIdx;
                    deferred.bspRegion = bspRegion;
                    deferred.meshBuilt = false;
                    float x = objInstance.placeable->getX();
                    float y = objInstance.placeable->getY();
                    float z = objInstance.placeable->getZ();
                    const auto& geom = objInstance.geometry;
                    float scaleX = objInstance.placeable->getScaleX();
                    float scaleY = objInstance.placeable->getScaleY();
                    float scaleZ = objInstance.placeable->getScaleZ();
                    float halfW = std::max(std::abs(geom->maxX - geom->minX),
                                           std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
                    float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
                    deferred.worldBounds = irr::core::aabbox3df(
                        x - halfW, z - halfH, y - halfW,
                        x + halfW, z + halfH, y + halfW);
                    computations->prebuiltDeferredObjects.push_back(deferred);
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: {} deferred object entries, {} prebuilt",
                          computations->deferredObjectEntries.size(), computations->prebuiltDeferredObjects.size());
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2: BSP compute skipped (zone={}, wldLoader={})",
                      (bool)zone, zone ? (bool)zone->wldLoader : false);
        }
        zoneStream_->logAssetBuildTime("seq_bsp_compute", 0, stepStart);
    }
    FlushThreadLog();

    // Install BSP data
    {
        auto stepStart = std::chrono::steady_clock::now();
        if (!currentZone_ || !currentZone_->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no wldLoader, fallback to single zone mesh");
            createZoneMesh();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-wldLoader fallback), zoneMeshNode_={}",
                      (bool)zoneMeshNode_);
        } else {
            auto wldLoader = currentZone_->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            if (!bspTree || bspTree->regions.empty() || !wldLoader->hasPvsData()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no BSP/PVS data, fallback to single zone mesh "
                          "(bspTree={}, regions={}, hasPvs={})",
                          (bool)bspTree, bspTree ? bspTree->regions.size() : 0, wldLoader->hasPvsData());
                if (currentZone_ && !currentZone_->geometry && currentZone_->wldLoader)
                    currentZone_->geometry = currentZone_->wldLoader->getCombinedGeometry();
                createZoneMesh();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-BSP/PVS fallback), zoneMeshNode_={}",
                          (bool)zoneMeshNode_);
            } else {
                // Clean up existing mesh nodes
                size_t oldRegionNodes = regionMeshNodes_.size();
                if (zoneMeshNode_) { zoneMeshNode_->remove(); zoneMeshNode_ = nullptr; }
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node) { if (node->getParent()) node->remove(); else node->drop(); }
                }
                regionMeshNodes_.clear();
                regionBoundingBoxes_.clear();
                if (fallbackMeshNode_) {
                    if (fallbackMeshNode_->getParent()) fallbackMeshNode_->remove(); else fallbackMeshNode_->drop();
                    fallbackMeshNode_ = nullptr;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: cleaned up existing mesh nodes "
                          "(zoneMeshNode removed, {} region nodes removed, fallback removed)",
                          oldRegionNodes);

                // Install BSP tree
                if (!zoneBspTree_) {
                    zoneBspTree_ = bspTree;
                    usePvsCulling_ = true;
                    currentPvsRegion_ = SIZE_MAX;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: BSP tree installed, usePvsCulling=true, "
                              "regions={}",
                              bspTree->regions.size());
                }

                // Install pre-computed bounding boxes
                if (regionBoundingBoxes_.empty() && pendingZoneComputations_ &&
                    !pendingZoneComputations_->regionBoundingBoxes.empty()) {
                    regionBoundingBoxes_ = std::move(pendingZoneComputations_->regionBoundingBoxes);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: installed {} region bounding boxes",
                              regionBoundingBoxes_.size());
                }

                if (entityRenderer_) {
                    entityRenderer_->setBspTree(zoneBspTree_);
                    entityRenderer_->setFrustumCuller(frustumCuller_.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: entityRenderer BSP tree and frustum culler set");
                }
                if (doorManager_) {
                    doorManager_->setBspTree(zoneBspTree_.get());
                    doorManager_->setPvsRegion(currentPvsRegion_);
                    doorManager_->setFrustumCuller(frustumCuller_.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager BSP tree, PVS region, and frustum culler set");
                }
            }
        }

        // Install portal system
        if (!portalSystem_ && pendingZoneComputations_ && pendingZoneComputations_->portalSystem) {
            portalSystem_ = std::move(pendingZoneComputations_->portalSystem);
            portalOcclusionEligible_ = pendingZoneComputations_->portalOcclusionEligible;
            portalBuildPending_ = false;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system installed, occlusionEligible={}",
                      portalOcclusionEligible_);

            // Auto-enable portal occlusion from config.
            // When per-region indoor map is loaded, portal occlusion is dynamically
            // toggled per-frame based on camera region (see simulation output apply).
            // Otherwise, use zone-level portal-to-region ratio heuristic.
            if (portalOcclusionEligible_ && config_.constrainedConfig.portalOcclusion &&
                config_.constrainedConfig.enableStencilBuffer) {
                if (!indoorRegions_.empty()) {
                    // Per-region mode: start disabled, will auto-enable when camera enters indoor region
                    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode ({} indoor regions loaded, will toggle dynamically)",
                             indoorRegions_.size());
                } else {
                    // Fallback: zone-level heuristic. Outdoor zones have ~7+ portals/region
                    // (BSP adjacency artifacts); indoor zones have ~1-2 (actual doorways).
                    size_t portalCount = portalSystem_->getData().portals.size();
                    size_t regionCount = regionBoundingBoxes_.size();
                    float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                    if (portalRatio <= 3.0f) {
                        portalOcclusionEnabled_ = true;
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled: {}/{} portals/regions = {:.1f} ratio (indoor zone)",
                                 portalCount, regionCount, portalRatio);
                    } else {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled: {}/{} portals/regions = {:.1f} ratio (outdoor zone, threshold 3.0)",
                                 portalCount, regionCount, portalRatio);
                    }
                }
            }
        } else if (zoneBspTree_ && !regionBoundingBoxes_.empty() && !portalSystem_) {
            portalBuildPending_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system deferred (portalBuildPending=true)");
        }
        if (regionNeighbors_.empty()) {
            buildRegionNeighborMap();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: built region neighbor map, {} entries", regionNeighbors_.size());
        }
        if (doorManager_) {
            doorManager_->setRegionNeighbors(regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager region neighbors set ({})",
                      regionNeighbors_.empty() ? "null" : std::to_string(regionNeighbors_.size()));
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: zoneBspTree_={}, usePvsCulling_={}, "
                  "regionBoundingBoxes_={}, portalSystem_={}, portalOcclusionEligible_={}, regionNeighbors_={}",
                  (bool)zoneBspTree_, usePvsCulling_, regionBoundingBoxes_.size(),
                  (bool)portalSystem_, portalOcclusionEligible_, regionNeighbors_.size());
        zoneStream_->logAssetBuildTime("seq_bsp_install", 0, stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep4_Regions(ZoneLoadContext& ctx) {
    // ── Step 4: Regions — build ALL zone meshes eagerly ────────────────────
    ctx.updateProgress(63, "Building zone meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: currentZone_={}, wldLoader={}, zoneBspTree_={}",
                  (bool)currentZone_, currentZone_ ? (bool)currentZone_->wldLoader : false, (bool)zoneBspTree_);

        if (currentZone_ && currentZone_->wldLoader && zoneBspTree_) {
            auto wldLoader = currentZone_->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
            builder.setUploadCategory("zone_region");  // P13
            if (constrainedTextureCache_)
                builder.setConstrainedTextureCache(constrainedTextureCache_);
            if (zoneShader_ && zoneShader_->isAvailable()) {
                builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                               zoneShader_->getActiveAlphaTest(),
                                               zoneShader_->getActiveAlphaTestRGBA());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: builder shader materials set (solid={}, alphaTest={})",
                          zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: totalRegions={}", bspTree->regions.size());

            size_t totalRegions = bspTree->regions.size();
            size_t regionsBuilt = 0;
            size_t regionsSkipped = 0;
            size_t meshBuildFailed = 0;
            size_t nodeFailed = 0;
            for (size_t i = 0; i < totalRegions; ++i) {
                auto geom = wldLoader->getGeometryForRegion(i);
                if (!geom || geom->vertices.empty()) { ++regionsBuilt; ++regionsSkipped; continue; }

                irr::scene::IMesh* mesh = nullptr;
                if (!currentZone_->textures.empty() && !geom->textureNames().empty()) {
                    mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built textured mesh, texNames={}, verts={}",
                              i, geom->textureNames().size(), geom->vertices.size());
                } else {
                    mesh = builder.buildColoredMesh(*geom);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] built colored mesh (no textures), verts={}",
                              i, geom->vertices.size());
                }
                if (!mesh) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step4: region[{}] mesh build returned null — skipping", i);
                    ++meshBuildFailed;
                    continue;
                }

                irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
                if (node) {
                    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
                    // Force AbsoluteTransformation update — same as rebuildRegionMesh().
                    // Without this, triangle selectors in setupMinimalZoneCollision() use
                    // stale identity transforms, placing collision geometry at origin.
                    node->updateAbsolutePosition();
                    for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
                        node->getMaterial(m).Lighting = lightingEnabled_;
                        node->getMaterial(m).BackfaceCulling = false;
                        node->getMaterial(m).GouraudShading = true;
                        node->getMaterial(m).FogEnable = fogEnabled_;
                        node->getMaterial(m).Wireframe = wireframeMode_;
                        node->getMaterial(m).NormalizeNormals = true;
                        node->getMaterial(m).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        node->getMaterial(m).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                    regionMeshNodes_[i] = node;
                    uploadMeshHardwareBuffers(node);
                    if (regionBoundingBoxes_.find(i) == regionBoundingBoxes_.end())
                        regionBoundingBoxes_[i] = node->getTransformedBoundingBox();
                } else {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step4: region[{}] addMeshSceneNode failed — mesh dropped", i);
                    ++nodeFailed;
                }
                mesh->drop();
                ++regionsBuilt;
                // Update progress every 20 regions (63-70%)
                if (regionsBuilt % 20 == 0 && totalRegions > 0) {
                    int pct = 63 + static_cast<int>(7.0f * regionsBuilt / totalRegions);
                    ctx.updateProgress(pct, "Building zone meshes [" + std::to_string(regionsBuilt)
                                        + "/" + std::to_string(totalRegions) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: built {} region meshes eagerly ({} skipped, {} mesh failures, {} node failures)",
                     regionMeshNodes_.size(), regionsSkipped, meshBuildFailed, nodeFailed);

            // Extract occluder triangles for software occlusion culling
            if (occlusionCuller_) {
                auto occStart = std::chrono::steady_clock::now();
                int totalOccluders = 0;
                int regionsWithOccluders = 0;
                float minArea = occlusionCuller_->getConfig().minOccluderArea;
                int maxPerRegion = occlusionCuller_->getConfig().maxTrianglesPerRegion;

                for (size_t ri = 0; ri < totalRegions; ++ri) {
                    auto geom = wldLoader->getGeometryForRegion(ri);
                    if (!geom || geom->vertices.empty() || geom->triangles.empty()) continue;

                    std::vector<OccluderTriangle> occluders;
                    for (const auto& tri : geom->triangles) {
                        if (tri.v1 >= geom->vertices.size() ||
                            tri.v2 >= geom->vertices.size() ||
                            tri.v3 >= geom->vertices.size()) continue;

                        const auto& va = geom->vertices[tri.v1];
                        const auto& vb = geom->vertices[tri.v2];
                        const auto& vc = geom->vertices[tri.v3];

                        // World-space positions (add region center offset)
                        float ax = geom->centerX + va.x, ay = geom->centerY + va.y, az = geom->centerZ + va.z;
                        float bx = geom->centerX + vb.x, by = geom->centerY + vb.y, bz = geom->centerZ + vb.z;
                        float cx = geom->centerX + vc.x, cy = geom->centerY + vc.y, cz = geom->centerZ + vc.z;

                        // Cross product for area
                        float ex = bx - ax, ey = by - ay, ez = bz - az;
                        float fx = cx - ax, fy = cy - ay, fz = cz - az;
                        float nx = ey * fz - ez * fy;
                        float ny = ez * fx - ex * fz;
                        float nz = ex * fy - ey * fx;
                        float area = 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz);

                        if (area < minArea) continue;

                        OccluderTriangle ot;
                        ot.v0[0] = ax; ot.v0[1] = ay; ot.v0[2] = az;
                        ot.v1[0] = bx; ot.v1[1] = by; ot.v1[2] = bz;
                        ot.v2[0] = cx; ot.v2[1] = cy; ot.v2[2] = cz;
                        ot.area = area;
                        occluders.push_back(ot);
                    }

                    // Keep only the largest triangles per region
                    if (static_cast<int>(occluders.size()) > maxPerRegion) {
                        std::sort(occluders.begin(), occluders.end(),
                                  [](const OccluderTriangle& a, const OccluderTriangle& b) {
                                      return a.area > b.area;
                                  });
                        occluders.resize(maxPerRegion);
                    }

                    if (!occluders.empty()) {
                        totalOccluders += occluders.size();
                        regionsWithOccluders++;
                        occlusionCuller_->setRegionOccluders(ri, std::move(occluders));
                    }
                }

                auto occEnd = std::chrono::steady_clock::now();
                LOG_INFO(MOD_GRAPHICS, "Sequential: extracted {} occluder triangles from {} regions ({}x{} depth buffer, {:.0f} ms)",
                         totalOccluders, regionsWithOccluders,
                         occlusionCuller_->getConfig().width, occlusionCuller_->getConfig().height,
                         std::chrono::duration<double, std::milli>(occEnd - occStart).count());
            }

            // Initialize animated texture manager for PVS region meshes
            // Discover animations from combined geometry, then register each region mesh's buffers
            if (currentZone_ && currentZone_->geometry && !currentZone_->textures.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: creating AnimatedTextureManager for PVS region meshes "
                          "(geometry texNames={}, texAnimations={}, zoneTextures={})",
                          currentZone_->geometry->textureNames().size(),
                          currentZone_->geometry->textureAnimations().size(),
                          currentZone_->textures.size());

                animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());
                if (constrainedTextureCache_) {
                    animatedTextureManager_->setConstrainedTextureCache(constrainedTextureCache_);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ constrained texture cache set");
                }

                // Discover animated textures from combined geometry (nullptr mesh — buffers added per-region below)
                int animCount = animatedTextureManager_->initialize(
                    *currentZone_->geometry, currentZone_->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ initialize found {} animated textures from geometry",
                          animCount);

                int waterAnimCount = animatedTextureManager_->detectWaterAnimations(
                    currentZone_->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ detectWaterAnimations found {} water textures",
                          waterAnimCount);
                animCount += waterAnimCount;

                // Register each region mesh's buffers with the animated texture manager.
                // PVS region meshes use addMesh (buffer materials) because they're drawn
                // via manual draw path, not through Irrlicht's scene graph.
                size_t registeredMeshes = 0;
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getMesh()) {
                        auto geom = wldLoader->getGeometryForRegion(regionIdx);
                        if (geom) {
                            animatedTextureManager_->addMesh(*geom, currentZone_->textures, node->getMesh());
                            registeredMeshes++;
                            LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] addMesh (buffers={}, texNames={})",
                                      regionIdx, node->getMesh()->getMeshBufferCount(), geom->textureNames().size());
                        }
                    }
                }

                LOG_INFO(MOD_GRAPHICS, "Sequential: initialized {} animated textures ({} water) for {} region meshes "
                         "({} meshes scanned)",
                         animCount, waterAnimCount, regionMeshNodes_.size(), registeredMeshes);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ skipped (currentZone_={}, geometry={}, textures={})",
                          (bool)currentZone_,
                          currentZone_ ? (bool)currentZone_->geometry : false,
                          currentZone_ ? currentZone_->textures.size() : 0);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: region mesh building skipped (currentZone_={}, wldLoader={}, zoneBspTree_={})",
                      (bool)currentZone_, currentZone_ ? (bool)currentZone_->wldLoader : false, (bool)zoneBspTree_);
        }

        // P10: initialize cross-region zone draw merge from config (default
        // false everywhere; runtime-toggleable via /mergedraws regardless of
        // this initial value). renderer_zone_lifecycle.cpp resets this +
        // destroys any built merge buffers on zone teardown, so each zone
        // visit starts from the config default again.
        mergeZoneDrawsEnabled_ = config_.constrainedConfig.mergeZoneDraws;

        // Setup front-to-back sorting
        if (usePvsCulling_ && !regionMeshNodes_.empty() && !config_.constrainedConfig.skipManualZoneDraw) {
            // P32: GLES2 is the only backend now, so this was always true —
            // collapsed from an #ifdef EQT_HAS_GLES2 / EDT_BURNINGSVIDEO check.
            manualZoneDrawEnabled_ = true;
            if (manualZoneDrawEnabled_) {
                // S05: renderPassTimer_ already created in initLoadingScreen()
                if (smgr_ && renderPassTimer_) {
                    renderPassTimer_->setRenderer(this);
                    smgr_->setLightManager(renderPassTimer_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: installed RenderPassTimer as light manager");
                }
                for (auto& [regionIdx, node] : regionMeshNodes_) {
                    if (node && node->getParent()) { node->grab(); node->remove(); }
                }
                if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) {
                    fallbackMeshNode_->grab(); fallbackMeshNode_->remove();
                }
                LOG_INFO(MOD_GRAPHICS, "Sequential: front-to-back sorting enabled ({} regions)",
                         regionMeshNodes_.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: manual zone draw disabled (software renderer)");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: front-to-back sorting skipped (usePvsCulling_={}, regionMeshNodes_={}, skipManualZoneDraw={})",
                      usePvsCulling_, regionMeshNodes_.size(), config_.constrainedConfig.skipManualZoneDraw);
        }
        zoneStream_->logAssetBuildTime("seq_regions", regionMeshNodes_.size(), stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep5_Assets(ZoneLoadContext& ctx) {
    // ── Step 5: Assets — build indexes, create subsystems ──────────────────
    ctx.updateProgress(71, "Building asset indexes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: deferredAssetLoading={}, entityRenderer_={}",
                  config_.constrainedConfig.deferredAssetLoading, (bool)entityRenderer_);

        // Build archive index inline (was background thread)
        if (computations && config_.constrainedConfig.deferredAssetLoading) {
            computations->archiveIndex = std::make_unique<GraphicsArchiveIndex>();
            if (computations->archiveIndex->buildIndex(ctx.eqPath, config_.constrainedConfig.lazyPfsLoading)) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: archive index built ({} race entries, {} archives)",
                         computations->archiveIndex->getRaceEntryCount(),
                         computations->archiveIndex->getArchiveCount());
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: archive index build failed, discarded");
                computations->archiveIndex.reset();
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index build skipped (deferredAssetLoading={}, computations={})",
                      config_.constrainedConfig.deferredAssetLoading, (bool)computations);
        }

        // Build equipment index inline (was background thread)
        if (computations) {
            auto eqIdx = std::make_unique<PendingZoneComputations::EquipmentIndexData>();
            if (EquipmentModelLoader::buildEquipmentIndex(ctx.eqPath,
                    eqIdx->modelIndex, eqIdx->textureIndex)) {
                eqIdx->loaded = true;
                LOG_INFO(MOD_GRAPHICS, "Sequential: equipment index ({} models, {} textures)",
                         eqIdx->modelIndex.size(), eqIdx->textureIndex.size());
            }
            if (!eqIdx->loaded) {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: equipment index build failed");
            }
            computations->equipmentIndex = std::move(eqIdx);
        }

        // S05: entityRenderer_ already created in initLoadingScreen()
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: entityRenderer_={}", (bool)entityRenderer_);

        // Install archive index
        if (computations && computations->archiveIndex) {
            graphicsArchiveIndex_ = std::move(computations->archiveIndex);
            bool setOnRml = false;
            if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
                entityRenderer_->getRaceModelLoader()->setGraphicsArchiveIndex(graphicsArchiveIndex_.get());
                setOnRml = true;
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index installed, setOnRaceModelLoader={}", setOnRml);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index not installed (computations={}, archiveIndex={})",
                      (bool)computations, computations ? (bool)computations->archiveIndex : false);
        }

        // Install equipment index
        if (computations && computations->equipmentIndex &&
            computations->equipmentIndex->loaded && entityRenderer_) {
            if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                eml->adoptIndex(
                    std::move(computations->equipmentIndex->modelIndex),
                    std::move(computations->equipmentIndex->textureIndex),
                    std::map<uint32_t, int>());  // Item models via on-demand SQLite
                // P04: consumed — free the wrapper now instead of at step 13
                // (skyLoadData precedent: release each computation as soon as
                // its consumer step is done with it).
                computations->equipmentIndex.reset();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: equipment index installed on EquipmentModelLoader");
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: equipment index built but EquipmentModelLoader is null — cannot install");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: equipment index not installed (computations={}, loaded={}, entityRenderer_={})",
                      (bool)computations,
                      (computations && computations->equipmentIndex) ? computations->equipmentIndex->loaded : false,
                      (bool)entityRenderer_);
        }

        // Set zone on RaceModelLoader
        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
            entityRenderer_->getRaceModelLoader()->setCurrentZone(currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: RaceModelLoader zone set to '{}'", currentZoneName_);
        }

        zoneStream_->logAssetBuildTime("seq_assets", 0, stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneSequential(const std::string& eqClientPath,
                                           LoadingStatus& status) {
    ZoneLoadContext ctx;
    ctx.totalStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: starting for zone '{}'", currentZoneName_);
    loadFailed_ = false;

    ctx.updateProgress = [&](int percent, const std::string& text) {
        status.setProgress(percent, text);
        LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: [{}%] {}", percent, text);
        // Render a loading frame so the progress bar updates visually
        std::wstring wtext(text.begin(), text.end());
        overlays_->drawLoadingScreen(percent / 100.0f, wtext);
    };

    // ── Setup ────────────────────────────────────────────────────────────────
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: zone='{}', eqClientPath='{}'", currentZoneName_, eqClientPath);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: particleManager_={}, zoneShader_={}, "
              "simulationWorker_={}, entityRenderer_={}, doorManager_={}",
              (bool)particleManager_, (bool)zoneShader_,
              (bool)simulationWorker_, (bool)entityRenderer_, (bool)doorManager_);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, "
              "maxFireGlowLights_={}, fireEffectsEnabled_={}, fireGlowLights_.size={}",
              fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
              maxFireGlowLights_, fireEffectsEnabled_, fireGlowLights_.size());
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: constrainedConfig: skipObjectBuild={}, "
              "eqe1Path='{}', deferredAssetLoading={}",
              config_.constrainedConfig.skipObjectBuild,
              config_.constrainedConfig.eqe1Path,
              config_.constrainedConfig.deferredAssetLoading);

    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: stopping simulation worker (running={})",
              simulationWorker_ ? simulationWorker_->isRunning() : false);
    simBridge_->stopSimulationWorker();
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: simulation worker stopped");
    progressiveLoadingActive_ = true;
    progressiveLoadStartTime_ = std::chrono::steady_clock::now();
    entityPrepReady_ = false;
    entityPrepScanCounter_ = 0;
    pendingZoneComputations_ = std::make_unique<PendingZoneComputations>();

    ctx.eqPath = eqClientPath;
    if (!ctx.eqPath.empty() && ctx.eqPath.back() != '/' && ctx.eqPath.back() != '\\')
        ctx.eqPath += '/';

    // P04: --load-mem-audit — logs one [LOAD-MEM-AUDIT] line after each step
    // with RSS plus the known-pool byte counts, so the "untracked" gap is a
    // number at every step boundary instead of just at the end. entityprep
    // bytes are 0 here — the prep-worker buffers this could name don't have a
    // stable byte-count accessor yet (see review's audit note on this gap).
    const bool memAudit = config_.constrainedConfig.loadMemAudit;
    auto sampleStep = [&](const char* stepName) {
        if (!memAudit) return;
        size_t texBytes = constrainedTextureCache_ ? constrainedTextureCache_->getCurrentUsage() : 0;
        // meshBytes was constrainedMeshCache_'s usage, always 0 (T67: cache
        // removed, was never constructed in any shipped build).
        constexpr size_t meshBytes = 0;
        logLoadMemAuditStep(stepName, eqe1_->getEQE1MappedBytesTotal(), texBytes, meshBytes, 0);
    };

    sampleStep("00_start");

    loadZoneStep0_EQE1(ctx);
    sampleStep("00_EQE1");

    if (!loadZoneStep1_S3D(ctx)) return;
    sampleStep("01_S3D");

    loadZoneStep2_BSP(ctx);
    sampleStep("02_BSP");

    // Step 3 (zone texture atlas) removed — E10: EQE1 flat packs replace atlas system.
    // Zone textures are loaded as individual ETC1 textures via constrained cache.
    FlushThreadLog();

    loadZoneStep4_Regions(ctx);
    sampleStep("04_Regions");

    loadZoneStep5_Assets(ctx);
    sampleStep("05_Assets");

    loadZoneStep6_Objects(ctx);
    sampleStep("06_Objects");

    loadZoneStep7_Doors(ctx);
    sampleStep("07_Doors");

    if (!loadZoneStep8_Entities(ctx)) return;
    sampleStep("08_Entities");

    loadZoneStep9_Collision(ctx);
    sampleStep("09_Collision");

    if (!loadZoneStep10_Sky(ctx)) return;
    sampleStep("10_Sky");

    loadZoneStep11_Env(ctx);
    sampleStep("11_Env");

    loadZoneStep12_Lights(ctx);
    sampleStep("12_Lights");

    loadZoneStep12b_Simulation(ctx);
    sampleStep("12b_Simulation");

    loadZoneStep13_Cleanup(ctx);
    sampleStep("13_Cleanup");

    sequentialLoadComplete_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ DONE: sequentialLoadComplete_=true, fireGlowLights_={}, "
              "fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}",
              fireGlowLights_.size(), fireGlowLightingEnabled_,
              fireGlowIcospheresEnabled_, maxFireGlowLights_);
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx.totalStart).count();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: complete for zone '{}' in {}ms", currentZoneName_, totalMs);

    // T207: report which EQE1 archives actually served a lookup. Always on --
    // it is a handful of INFO lines once per zone-in, and the whole point is
    // that the waste was invisible until it was counted.
    eqe1_->logEQE1ArchiveUsage();

    // T214: dump the COMPLETE per-name EQE1 request list for this zone load.
    // logEQE1ArchiveUsage() above says which archives went unused; this says
    // exactly which assets were asked for and who served each one, so the
    // open set can be audited against the needed set rather than inferred.
    // Env-gated so it costs nothing in normal runs.
    if (constrainedTextureCache_) {
        if (const char* dir = std::getenv("EQT_EQE1_REQUEST_LOG_DIR")) {
            constrainedTextureCache_->dumpEQE1RequestLog(
                std::string(dir) + "/eqe1_requests_" + currentZoneName_ + ".tsv");
        }
    }

    // P13: dump the texture upload/eviction ledger at the zone-in-complete
    // marker (no-op unless --texture-ledger was passed — see
    // ConstrainedRendererConfig::textureUploadLedgerPath).
    if (constrainedTextureCache_ && constrainedTextureCache_->isLedgerEnabled()) {
        constrainedTextureCache_->dumpLedger(config_.constrainedConfig.textureUploadLedgerPath,
                                              currentZoneName_);
    }

    diagnostics_->dumpScene();
    FlushThreadLog();
}

} // namespace Graphics
} // namespace EQT
