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
#include "client/graphics/zone_load/zone_load_component.h"
#include "client/graphics/zone_load/zone_load_context.h"

// Decode BMP data to A8R8G8B8 pixel array (used by sky texture loading)
// T14: decodeBMPtoARGB and bilinearUpscaleARGB removed.
// All texture decoding now goes through ConstrainedTextureCache::getOrLoad().

namespace EQT {
namespace Graphics {

// Out-of-line ctor AND dtor (batch correction 35): pendingZoneComputations_ is
// a unique_ptr and the constructor's unwind path destroys already-initialised
// members, so both need the complete type here rather than in the header.
ZoneLoadComponent::ZoneLoadComponent(RendererContext& ctx, IrrlichtRenderer& owner)
    : ctx_(ctx), owner_(owner) {}

ZoneLoadComponent::~ZoneLoadComponent() = default;

void ZoneLoadComponent::loadZoneStep0_EQE1(ZoneLoadContext& ctx) {
    // ── Step 0: EQE1 — open flat pack files (if configured) ────────────────
    if (!ctx_.config.constrainedConfig.eqe1Path.empty()) {
        ctx.updateProgress(48, "Opening EQE1 textures...");

        // P04: open the (small) zone-specific archives eagerly — they're
        // always needed. Global archives (the ~33-file, 139MB-of-mostly-
        // unreferenced-bytes set P03 measured) are NOT opened here anymore;
        // they're queued and opened lazily, one at a time, only when
        // ConstrainedTextureCache actually misses a lookup against what's
        // already open. Most zones only ever touch a handful of them.
        auto archiveList = owner_.Eqe1().resolveEQE1Archives(ctx_.currentZoneName);
        for (const auto& name : archiveList.zoneArchives) {
            owner_.Eqe1().getOrOpenEQE1(name);
        }
        {
            std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
            owner_.pendingGlobalEQE1Archives_ = archiveList.globalArchives;
            owner_.eqe1CandidateRaces_.clear();
            owner_.eqe1LoadTimeCandidateCount_ = 0;
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
        if (ctx_.entityRenderer) {
            std::set<uint16_t> spawnedRaces;
            for (const auto& [spawnId, vis] : ctx_.entityRenderer->getEntities()) {
                spawnedRaces.insert(vis.raceId);
            }
            size_t added = 0;
            for (uint16_t raceId : spawnedRaces) {
                if (owner_.Eqe1().addSpawnedRaceEQE1Candidate(raceId)) ++added;
            }
            LOG_INFO(MOD_GRAPHICS, "EQE1: {} distinct spawned races -> {} archive(s) queued "
                     "beyond the static list", spawnedRaces.size(), added);
        }
        {
            std::lock_guard<std::mutex> lock(owner_.eqe1CandidatesMutex_);
            owner_.eqe1LoadTimeCandidateCount_ = owner_.pendingGlobalEQE1Archives_.size();
        }

        // T207: the name -> archive index turns a lookup miss into at most one
        // open (and a genuinely absent name into zero), replacing the probe that
        // walked this whole list. Loaded once; the candidate set above is what
        // varies per zone.
        if (!owner_.eqe1Index_.isLoaded()) {
            if (!owner_.eqe1Index_.load("data/eqe1_texture_index.json")) {
                LOG_ERROR(MOD_GRAPHICS,
                          "EQE1: no data/eqe1_texture_index.json -- EQE1 lookups "
                          "will resolve nothing. Rebuild it with "
                          "eqe1_texture_index_builder --eqe1-path {}",
                          ctx_.config.constrainedConfig.eqe1Path);
            }
        }
        if (ctx_.constrainedTextureCache) {
            ctx_.constrainedTextureCache->setEQE1ArchiveResolver(
                [this](const std::string& n) { return owner_.Eqe1().openArchiveContaining(n); });
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

bool ZoneLoadComponent::loadZoneStep1_S3D(ZoneLoadContext& ctx) {
    // ── Step 1: S3D — parse zone archive ───────────────────────────────────
    ctx.updateProgress(50, "Loading zone data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        std::string zonePath = ctx.eqPath + ctx_.currentZoneName + ".s3d";
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loading S3D from '{}'", zonePath);

        S3DLoadOptions loadOptions;
        loadOptions.loadCharacters = false;
        loadOptions.computeCombinedGeometry = true;
        loadOptions.loadObjects = !ctx_.config.constrainedConfig.skipObjectBuild;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: loadOptions: chars={}, combinedGeom={}, objects={}",
                  loadOptions.loadCharacters, loadOptions.computeCombinedGeometry, loadOptions.loadObjects);

        S3DLoader loader;
        if (!loader.loadZone(zonePath, loadOptions)) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load zone S3D archive: {} ({})", zonePath, loader.getError());
            pendingZoneComputations_.reset();
            loadFailed_ = true;
            return false;
        }
        ctx_.currentZone = loader.getZone();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: S3D loaded: objects={}, lights={}, textures={}, "
                  "wldLoader={}, geometry={}",
                  ctx_.currentZone ? ctx_.currentZone->objects.size() : 0,
                  ctx_.currentZone ? ctx_.currentZone->lights.size() : 0,
                  ctx_.currentZone ? ctx_.currentZone->textures.size() : 0,
                  ctx_.currentZone ? (bool)ctx_.currentZone->wldLoader : false,
                  ctx_.currentZone ? (bool)ctx_.currentZone->geometry : false);
        owner_.ZoneStream().logAssetBuildTime("seq_s3d_parse", 0, stepStart);
    }
    FlushThreadLog();

    // Install S3D data
    if (!ctx_.currentZone) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneSequential: S3D produced no data");
        pendingZoneComputations_.reset();
        loadFailed_ = true;
        return false;
    }
    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->setCurrentZone(ctx_.currentZoneName);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: entityRenderer_ zone set to '{}'", ctx_.currentZoneName);
    }
    if (ctx_.doorManager) {
        ctx_.doorManager->setZone(ctx_.currentZone);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ zone set");
        if (ctx_.constrainedTextureCache) {
            ctx_.doorManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ constrained texture cache set");
        }
        if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
            ctx_.doorManager->setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                                 ctx_.zoneShader->getActiveAlphaTest(),
                                                 ctx_.zoneShader->getActiveAlphaTestRGBA());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step1: doorManager_ shader materials set (solid={}, alphaTest={})",
                      ctx_.zoneShader->getActiveSolid(), ctx_.zoneShader->getActiveAlphaTest());
        }
    }
    return true;
}

void ZoneLoadComponent::loadZoneStep2_BSP(ZoneLoadContext& ctx) {
    // ── Step 2: BSP — compute spatial data ─────────────────────────────────
    ctx.updateProgress(55, "Computing spatial data...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        auto zone = ctx_.currentZone;
        bool skipObjectBuild = ctx_.config.constrainedConfig.skipObjectBuild;
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
        owner_.ZoneStream().logAssetBuildTime("seq_bsp_compute", 0, stepStart);
    }
    FlushThreadLog();

    // Install BSP data
    {
        auto stepStart = std::chrono::steady_clock::now();
        if (!ctx_.currentZone || !ctx_.currentZone->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no wldLoader, fallback to single zone mesh");
            owner_.ZoneLifecycle().createZoneMesh();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-wldLoader fallback), zoneMeshNode_={}",
                      (bool)owner_.zoneMeshNode_);
        } else {
            auto wldLoader = ctx_.currentZone->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            if (!bspTree || bspTree->regions.empty() || !wldLoader->hasPvsData()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: no BSP/PVS data, fallback to single zone mesh "
                          "(bspTree={}, regions={}, hasPvs={})",
                          (bool)bspTree, bspTree ? bspTree->regions.size() : 0, wldLoader->hasPvsData());
                if (ctx_.currentZone && !ctx_.currentZone->geometry && ctx_.currentZone->wldLoader)
                    ctx_.currentZone->geometry = ctx_.currentZone->wldLoader->getCombinedGeometry();
                owner_.ZoneLifecycle().createZoneMesh();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: createZoneMesh() complete (no-BSP/PVS fallback), zoneMeshNode_={}",
                          (bool)owner_.zoneMeshNode_);
            } else {
                // Clean up existing mesh nodes
                size_t oldRegionNodes = ctx_.regionMeshNodes.size();
                if (owner_.zoneMeshNode_) { owner_.zoneMeshNode_->remove(); owner_.zoneMeshNode_ = nullptr; }
                for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
                    if (node) { if (node->getParent()) node->remove(); else node->drop(); }
                }
                ctx_.regionMeshNodes.clear();
                ctx_.regionBoundingBoxes.clear();
                if (owner_.fallbackMeshNode_) {
                    if (owner_.fallbackMeshNode_->getParent()) owner_.fallbackMeshNode_->remove(); else owner_.fallbackMeshNode_->drop();
                    owner_.fallbackMeshNode_ = nullptr;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: cleaned up existing mesh nodes "
                          "(zoneMeshNode removed, {} region nodes removed, fallback removed)",
                          oldRegionNodes);

                // Install BSP tree
                if (!ctx_.zoneBspTree) {
                    ctx_.zoneBspTree = bspTree;
                    ctx_.usePvsCulling = true;
                    ctx_.currentPvsRegion = SIZE_MAX;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: BSP tree installed, usePvsCulling=true, "
                              "regions={}",
                              bspTree->regions.size());
                }

                // Install pre-computed bounding boxes
                if (ctx_.regionBoundingBoxes.empty() && pendingZoneComputations_ &&
                    !pendingZoneComputations_->regionBoundingBoxes.empty()) {
                    ctx_.regionBoundingBoxes = std::move(pendingZoneComputations_->regionBoundingBoxes);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: installed {} region bounding boxes",
                              ctx_.regionBoundingBoxes.size());
                }

                if (ctx_.entityRenderer) {
                    ctx_.entityRenderer->setBspTree(ctx_.zoneBspTree);
                    ctx_.entityRenderer->setFrustumCuller(ctx_.frustumCuller.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: entityRenderer BSP tree and frustum culler set");
                }
                if (ctx_.doorManager) {
                    ctx_.doorManager->setBspTree(ctx_.zoneBspTree.get());
                    ctx_.doorManager->setPvsRegion(ctx_.currentPvsRegion);
                    ctx_.doorManager->setFrustumCuller(ctx_.frustumCuller.get());
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager BSP tree, PVS region, and frustum culler set");
                }
            }
        }

        // Install portal system
        if (!ctx_.portalSystem && pendingZoneComputations_ && pendingZoneComputations_->portalSystem) {
            ctx_.portalSystem = std::move(pendingZoneComputations_->portalSystem);
            ctx_.portalOcclusionEligible = pendingZoneComputations_->portalOcclusionEligible;
            owner_.portalBuildPending_ = false;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system installed, occlusionEligible={}",
                      ctx_.portalOcclusionEligible);

            // Auto-enable portal occlusion from config.
            // When per-region indoor map is loaded, portal occlusion is dynamically
            // toggled per-frame based on camera region (see simulation output apply).
            // Otherwise, use zone-level portal-to-region ratio heuristic.
            if (ctx_.portalOcclusionEligible && ctx_.config.constrainedConfig.portalOcclusion &&
                ctx_.config.constrainedConfig.enableStencilBuffer) {
                if (!ctx_.indoorRegions.empty()) {
                    // Per-region mode: start disabled, will auto-enable when camera enters indoor region
                    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode ({} indoor regions loaded, will toggle dynamically)",
                             ctx_.indoorRegions.size());
                } else {
                    // Fallback: zone-level heuristic. Outdoor zones have ~7+ portals/region
                    // (BSP adjacency artifacts); indoor zones have ~1-2 (actual doorways).
                    size_t portalCount = ctx_.portalSystem->getData().portals.size();
                    size_t regionCount = ctx_.regionBoundingBoxes.size();
                    float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                    if (portalRatio <= 3.0f) {
                        ctx_.portalOcclusionEnabled = true;
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled: {}/{} portals/regions = {:.1f} ratio (indoor zone)",
                                 portalCount, regionCount, portalRatio);
                    } else {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled: {}/{} portals/regions = {:.1f} ratio (outdoor zone, threshold 3.0)",
                                 portalCount, regionCount, portalRatio);
                    }
                }
            }
        } else if (ctx_.zoneBspTree && !ctx_.regionBoundingBoxes.empty() && !ctx_.portalSystem) {
            owner_.portalBuildPending_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: portal system deferred (portalBuildPending=true)");
        }
        if (owner_.regionNeighbors_.empty()) {
            owner_.ZoneLifecycle().buildRegionNeighborMap();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: built region neighbor map, {} entries", owner_.regionNeighbors_.size());
        }
        if (ctx_.doorManager) {
            ctx_.doorManager->setRegionNeighbors(owner_.regionNeighbors_.empty() ? nullptr : &owner_.regionNeighbors_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: doorManager region neighbors set ({})",
                      owner_.regionNeighbors_.empty() ? "null" : std::to_string(owner_.regionNeighbors_.size()));
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: zoneBspTree_={}, usePvsCulling_={}, "
                  "regionBoundingBoxes_={}, portalSystem_={}, portalOcclusionEligible_={}, regionNeighbors_={}",
                  (bool)ctx_.zoneBspTree, ctx_.usePvsCulling, ctx_.regionBoundingBoxes.size(),
                  (bool)ctx_.portalSystem, ctx_.portalOcclusionEligible, owner_.regionNeighbors_.size());
        owner_.ZoneStream().logAssetBuildTime("seq_bsp_install", 0, stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep4_Regions(ZoneLoadContext& ctx) {
    // ── Step 4: Regions — build ALL zone meshes eagerly ────────────────────
    ctx.updateProgress(63, "Building zone meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: currentZone_={}, wldLoader={}, zoneBspTree_={}",
                  (bool)ctx_.currentZone, ctx_.currentZone ? (bool)ctx_.currentZone->wldLoader : false, (bool)ctx_.zoneBspTree);

        if (ctx_.currentZone && ctx_.currentZone->wldLoader && ctx_.zoneBspTree) {
            auto wldLoader = ctx_.currentZone->wldLoader;
            auto bspTree = wldLoader->getBspTree();

            ZoneMeshBuilder builder(ctx_.smgr, ctx_.driver, ctx_.device->getFileSystem());
            builder.setUploadCategory("zone_region");  // P13
            if (ctx_.constrainedTextureCache)
                builder.setConstrainedTextureCache(ctx_.constrainedTextureCache);
            if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
                builder.setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                               ctx_.zoneShader->getActiveAlphaTest(),
                                               ctx_.zoneShader->getActiveAlphaTestRGBA());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: builder shader materials set (solid={}, alphaTest={})",
                          ctx_.zoneShader->getActiveSolid(), ctx_.zoneShader->getActiveAlphaTest());
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
                if (!ctx_.currentZone->textures.empty() && !geom->textureNames().empty()) {
                    mesh = builder.buildTexturedMesh(*geom, ctx_.currentZone->textures);
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

                irr::scene::IMeshSceneNode* node = ctx_.smgr->addMeshSceneNode(mesh);
                if (node) {
                    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
                    // Force AbsoluteTransformation update — same as rebuildRegionMesh().
                    // Without this, triangle selectors in setupMinimalZoneCollision() use
                    // stale identity transforms, placing collision geometry at origin.
                    node->updateAbsolutePosition();
                    for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
                        node->getMaterial(m).Lighting = ctx_.lightingEnabled;
                        node->getMaterial(m).BackfaceCulling = false;
                        node->getMaterial(m).GouraudShading = true;
                        node->getMaterial(m).FogEnable = ctx_.fogEnabled;
                        node->getMaterial(m).Wireframe = ctx_.wireframeMode;
                        node->getMaterial(m).NormalizeNormals = true;
                        node->getMaterial(m).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        node->getMaterial(m).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                    ctx_.regionMeshNodes[i] = node;
                    owner_.ZoneLifecycle().uploadMeshHardwareBuffers(node);
                    if (ctx_.regionBoundingBoxes.find(i) == ctx_.regionBoundingBoxes.end())
                        ctx_.regionBoundingBoxes[i] = node->getTransformedBoundingBox();
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
                     ctx_.regionMeshNodes.size(), regionsSkipped, meshBuildFailed, nodeFailed);

            // Extract occluder triangles for software occlusion culling
            if (ctx_.occlusionCuller) {
                auto occStart = std::chrono::steady_clock::now();
                int totalOccluders = 0;
                int regionsWithOccluders = 0;
                float minArea = ctx_.occlusionCuller->getConfig().minOccluderArea;
                int maxPerRegion = ctx_.occlusionCuller->getConfig().maxTrianglesPerRegion;

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
                        ctx_.occlusionCuller->setRegionOccluders(ri, std::move(occluders));
                    }
                }

                auto occEnd = std::chrono::steady_clock::now();
                LOG_INFO(MOD_GRAPHICS, "Sequential: extracted {} occluder triangles from {} regions ({}x{} depth buffer, {:.0f} ms)",
                         totalOccluders, regionsWithOccluders,
                         ctx_.occlusionCuller->getConfig().width, ctx_.occlusionCuller->getConfig().height,
                         std::chrono::duration<double, std::milli>(occEnd - occStart).count());
            }

            // Initialize animated texture manager for PVS region meshes
            // Discover animations from combined geometry, then register each region mesh's buffers
            if (ctx_.currentZone && ctx_.currentZone->geometry && !ctx_.currentZone->textures.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: creating AnimatedTextureManager for PVS region meshes "
                          "(geometry texNames={}, texAnimations={}, zoneTextures={})",
                          ctx_.currentZone->geometry->textureNames().size(),
                          ctx_.currentZone->geometry->textureAnimations().size(),
                          ctx_.currentZone->textures.size());

                ctx_.animatedTextureManager = std::make_unique<AnimatedTextureManager>(ctx_.driver, ctx_.device->getFileSystem());
                if (ctx_.constrainedTextureCache) {
                    ctx_.animatedTextureManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
                    LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ constrained texture cache set");
                }

                // Discover animated textures from combined geometry (nullptr mesh — buffers added per-region below)
                int animCount = ctx_.animatedTextureManager->initialize(
                    *ctx_.currentZone->geometry, ctx_.currentZone->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ initialize found {} animated textures from geometry",
                          animCount);

                int waterAnimCount = ctx_.animatedTextureManager->detectWaterAnimations(
                    ctx_.currentZone->textures, nullptr);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ detectWaterAnimations found {} water textures",
                          waterAnimCount);
                animCount += waterAnimCount;

                // Register each region mesh's buffers with the animated texture manager.
                // PVS region meshes use addMesh (buffer materials) because they're drawn
                // via manual draw path, not through Irrlicht's scene graph.
                size_t registeredMeshes = 0;
                for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
                    if (node && node->getMesh()) {
                        auto geom = wldLoader->getGeometryForRegion(regionIdx);
                        if (geom) {
                            ctx_.animatedTextureManager->addMesh(*geom, ctx_.currentZone->textures, node->getMesh());
                            registeredMeshes++;
                            LOG_TRACE(MOD_GRAPHICS, "SEQ Step4: region[{}] addMesh (buffers={}, texNames={})",
                                      regionIdx, node->getMesh()->getMeshBufferCount(), geom->textureNames().size());
                        }
                    }
                }

                LOG_INFO(MOD_GRAPHICS, "Sequential: initialized {} animated textures ({} water) for {} region meshes "
                         "({} meshes scanned)",
                         animCount, waterAnimCount, ctx_.regionMeshNodes.size(), registeredMeshes);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: animatedTextureManager_ skipped (currentZone_={}, geometry={}, textures={})",
                          (bool)ctx_.currentZone,
                          ctx_.currentZone ? (bool)ctx_.currentZone->geometry : false,
                          ctx_.currentZone ? ctx_.currentZone->textures.size() : 0);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: region mesh building skipped (currentZone_={}, wldLoader={}, zoneBspTree_={})",
                      (bool)ctx_.currentZone, ctx_.currentZone ? (bool)ctx_.currentZone->wldLoader : false, (bool)ctx_.zoneBspTree);
        }

        // P10: initialize cross-region zone draw merge from config (default
        // false everywhere; runtime-toggleable via /mergedraws regardless of
        // this initial value). renderer_zone_lifecycle.cpp resets this +
        // destroys any built merge buffers on zone teardown, so each zone
        // visit starts from the config default again.
        owner_.mergeZoneDrawsEnabled_ = ctx_.config.constrainedConfig.mergeZoneDraws;

        // Setup front-to-back sorting
        if (ctx_.usePvsCulling && !ctx_.regionMeshNodes.empty() && !ctx_.config.constrainedConfig.skipManualZoneDraw) {
            // P32: GLES2 is the only backend now, so this was always true —
            // collapsed from an #ifdef EQT_HAS_GLES2 / EDT_BURNINGSVIDEO check.
            ctx_.manualZoneDrawEnabled = true;
            if (ctx_.manualZoneDrawEnabled) {
                // S05: renderPassTimer_ already created in initLoadingScreen()
                if (ctx_.smgr && owner_.renderPassTimer_) {
                    owner_.renderPassTimer_->setRenderer(&owner_);
                    ctx_.smgr->setLightManager(owner_.renderPassTimer_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: installed RenderPassTimer as light manager");
                }
                for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
                    if (node && node->getParent()) { node->grab(); node->remove(); }
                }
                if (owner_.fallbackMeshNode_ && owner_.fallbackMeshNode_->getParent()) {
                    owner_.fallbackMeshNode_->grab(); owner_.fallbackMeshNode_->remove();
                }
                LOG_INFO(MOD_GRAPHICS, "Sequential: front-to-back sorting enabled ({} regions)",
                         ctx_.regionMeshNodes.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: manual zone draw disabled (software renderer)");
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step4: front-to-back sorting skipped (usePvsCulling_={}, regionMeshNodes_={}, skipManualZoneDraw={})",
                      ctx_.usePvsCulling, ctx_.regionMeshNodes.size(), ctx_.config.constrainedConfig.skipManualZoneDraw);
        }
        owner_.ZoneStream().logAssetBuildTime("seq_regions", ctx_.regionMeshNodes.size(), stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep5_Assets(ZoneLoadContext& ctx) {
    // ── Step 5: Assets — build indexes, create subsystems ──────────────────
    ctx.updateProgress(71, "Building asset indexes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: deferredAssetLoading={}, entityRenderer_={}",
                  ctx_.config.constrainedConfig.deferredAssetLoading, (bool)ctx_.entityRenderer);

        // Build archive index inline (was background thread)
        if (computations && ctx_.config.constrainedConfig.deferredAssetLoading) {
            computations->archiveIndex = std::make_unique<GraphicsArchiveIndex>();
            if (computations->archiveIndex->buildIndex(ctx.eqPath, ctx_.config.constrainedConfig.lazyPfsLoading)) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: archive index built ({} race entries, {} archives)",
                         computations->archiveIndex->getRaceEntryCount(),
                         computations->archiveIndex->getArchiveCount());
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step5: archive index build failed, discarded");
                computations->archiveIndex.reset();
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index build skipped (deferredAssetLoading={}, computations={})",
                      ctx_.config.constrainedConfig.deferredAssetLoading, (bool)computations);
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
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: entityRenderer_={}", (bool)ctx_.entityRenderer);

        // Install archive index
        if (computations && computations->archiveIndex) {
            owner_.graphicsArchiveIndex_ = std::move(computations->archiveIndex);
            bool setOnRml = false;
            if (ctx_.entityRenderer && ctx_.entityRenderer->getRaceModelLoader()) {
                ctx_.entityRenderer->getRaceModelLoader()->setGraphicsArchiveIndex(owner_.graphicsArchiveIndex_.get());
                setOnRml = true;
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index installed, setOnRaceModelLoader={}", setOnRml);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: archive index not installed (computations={}, archiveIndex={})",
                      (bool)computations, computations ? (bool)computations->archiveIndex : false);
        }

        // Install equipment index
        if (computations && computations->equipmentIndex &&
            computations->equipmentIndex->loaded && ctx_.entityRenderer) {
            if (auto* eml = ctx_.entityRenderer->getEquipmentModelLoader()) {
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
                      (bool)ctx_.entityRenderer);
        }

        // Set zone on RaceModelLoader
        if (ctx_.entityRenderer && ctx_.entityRenderer->getRaceModelLoader()) {
            ctx_.entityRenderer->getRaceModelLoader()->setCurrentZone(ctx_.currentZoneName);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step5: RaceModelLoader zone set to '{}'", ctx_.currentZoneName);
        }

        owner_.ZoneStream().logAssetBuildTime("seq_assets", 0, stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneSequential(const std::string& eqClientPath,
                                           LoadingStatus& status) {
    ZoneLoadContext ctx;
    ctx.totalStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: starting for zone '{}'", ctx_.currentZoneName);
    loadFailed_ = false;

    ctx.updateProgress = [&](int percent, const std::string& text) {
        status.setProgress(percent, text);
        LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: [{}%] {}", percent, text);
        // Render a loading frame so the progress bar updates visually
        std::wstring wtext(text.begin(), text.end());
        owner_.Overlays().drawLoadingScreen(percent / 100.0f, wtext);
    };

    // ── Setup ────────────────────────────────────────────────────────────────
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: zone='{}', eqClientPath='{}'", ctx_.currentZoneName, eqClientPath);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: particleManager_={}, zoneShader_={}, "
              "simulationWorker_={}, entityRenderer_={}, doorManager_={}",
              (bool)ctx_.particleManager, (bool)ctx_.zoneShader,
              (bool)owner_.simulationWorker_, (bool)ctx_.entityRenderer, (bool)ctx_.doorManager);
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, "
              "maxFireGlowLights_={}, fireEffectsEnabled_={}, fireGlowLights_.size={}",
              owner_.fireGlowLightingEnabled_, owner_.fireGlowIcospheresEnabled_,
              owner_.maxFireGlowLights_, ctx_.fireEffectsEnabled, owner_.fireGlowLights_.size());
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: constrainedConfig: skipObjectBuild={}, "
              "eqe1Path='{}', deferredAssetLoading={}",
              ctx_.config.constrainedConfig.skipObjectBuild,
              ctx_.config.constrainedConfig.eqe1Path,
              ctx_.config.constrainedConfig.deferredAssetLoading);

    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: stopping simulation worker (running={})",
              owner_.simulationWorker_ ? owner_.simulationWorker_->isRunning() : false);
    owner_.SimBridge().stopSimulationWorker();
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: simulation worker stopped");
    ctx_.progressiveLoadingActive = true;
    owner_.progressiveLoadStartTime_ = std::chrono::steady_clock::now();
    ctx_.entityPrepReady = false;
    owner_.entityPrepScanCounter_ = 0;
    pendingZoneComputations_ = std::make_unique<PendingZoneComputations>();

    ctx.eqPath = eqClientPath;
    if (!ctx.eqPath.empty() && ctx.eqPath.back() != '/' && ctx.eqPath.back() != '\\')
        ctx.eqPath += '/';

    // P04: --load-mem-audit — logs one [LOAD-MEM-AUDIT] line after each step
    // with RSS plus the known-pool byte counts, so the "untracked" gap is a
    // number at every step boundary instead of just at the end. entityprep
    // bytes are 0 here — the prep-worker buffers this could name don't have a
    // stable byte-count accessor yet (see review's audit note on this gap).
    const bool memAudit = ctx_.config.constrainedConfig.loadMemAudit;
    auto sampleStep = [&](const char* stepName) {
        if (!memAudit) return;
        size_t texBytes = ctx_.constrainedTextureCache ? ctx_.constrainedTextureCache->getCurrentUsage() : 0;
        // meshBytes was constrainedMeshCache_'s usage, always 0 (T67: cache
        // removed, was never constructed in any shipped build).
        constexpr size_t meshBytes = 0;
        logLoadMemAuditStep(stepName, owner_.Eqe1().getEQE1MappedBytesTotal(), texBytes, meshBytes, 0);
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

    owner_.sequentialLoadComplete_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ DONE: sequentialLoadComplete_=true, fireGlowLights_={}, "
              "fireGlowLightingEnabled_={}, fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}",
              owner_.fireGlowLights_.size(), owner_.fireGlowLightingEnabled_,
              owner_.fireGlowIcospheresEnabled_, owner_.maxFireGlowLights_);
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx.totalStart).count();
    LOG_INFO(MOD_GRAPHICS, "loadZoneSequential: complete for zone '{}' in {}ms", ctx_.currentZoneName, totalMs);

    // T207: report which EQE1 archives actually served a lookup. Always on --
    // it is a handful of INFO lines once per zone-in, and the whole point is
    // that the waste was invisible until it was counted.
    owner_.Eqe1().logEQE1ArchiveUsage();

    // T214: dump the COMPLETE per-name EQE1 request list for this zone load.
    // logEQE1ArchiveUsage() above says which archives went unused; this says
    // exactly which assets were asked for and who served each one, so the
    // open set can be audited against the needed set rather than inferred.
    // Env-gated so it costs nothing in normal runs.
    if (ctx_.constrainedTextureCache) {
        if (const char* dir = std::getenv("EQT_EQE1_REQUEST_LOG_DIR")) {
            ctx_.constrainedTextureCache->dumpEQE1RequestLog(
                std::string(dir) + "/eqe1_requests_" + ctx_.currentZoneName + ".tsv");
        }
    }

    // P13: dump the texture upload/eviction ledger at the zone-in-complete
    // marker (no-op unless --texture-ledger was passed — see
    // ConstrainedRendererConfig::textureUploadLedgerPath).
    if (ctx_.constrainedTextureCache && ctx_.constrainedTextureCache->isLedgerEnabled()) {
        ctx_.constrainedTextureCache->dumpLedger(ctx_.config.constrainedConfig.textureUploadLedgerPath,
                                              ctx_.currentZoneName);
    }

    owner_.Diagnostics().dumpScene();
    FlushThreadLog();
}

} // namespace Graphics
} // namespace EQT
