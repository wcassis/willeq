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
#include "client/graphics/constrained_mesh_cache.h"
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
#include <filesystem>
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

namespace EQT {
namespace Graphics {

// Decode BMP data to A8R8G8B8 pixel array (used by sky texture loading)
// T14: decodeBMPtoARGB and bilinearUpscaleARGB removed.
// All texture decoding now goes through ConstrainedTextureCache::getOrLoad().


// ── ZoneLoadContext ─────────────────────────────────────────────────────────
// Holds every local that crosses a loadZoneSequential() step boundary (G01
// decomposition). Defined here (not in the header) since it is purely an
// implementation detail of the step methods; the header only forward-declares
// it so the private step-method declarations can take it by reference.
struct IrrlichtRenderer::ZoneLoadContext {
    // Setup-scope locals used across many steps.
    std::string eqPath;
    std::chrono::steady_clock::time_point totalStart;
    std::function<void(int, const std::string&)> updateProgress;

    // Step 8 (Entities) per-entity-loop state. These are set up once before the
    // loop (modelLoader/equipLoader/totalEntities/*Count accumulators) or once
    // per iteration (spawnId/vis/entitySuccess/variantTextureCount/equipmentData)
    // and threaded through the 8a-8f sub-step calls that make up one iteration.
    RaceModelLoader* modelLoader = nullptr;
    EquipmentModelLoader* equipLoader = nullptr;
    size_t totalEntities = 0;
    size_t prepCount = 0;
    size_t buildCount = 0;
    size_t modelCacheHits = 0;
    size_t modelDiskLoads = 0;

    uint16_t spawnId = 0;
    const EntityVisual* vis = nullptr;
    bool entitySuccess = false;       // set by 8a, consumed by 8b-8e guards
    size_t variantTextureCount = 0;   // set by 8c, consumed by 8e (warn log)

    // T38: entities skipped because their base model could not be loaded
    // (e.g. race=415 absent from race_models). Accumulated across the whole
    // entity loop, not reset per-iteration; consumed by loadZoneStep8_Entities'
    // end-of-zone summary line. A missing model is now a per-entity WARN +
    // skip, not a zone-fatal LOG_FATAL — see loadZoneStep8a_BaseModelPrep.
    size_t entitiesSkipped = 0;
    std::set<std::pair<uint16_t, uint8_t>> skippedRaceGenderPairs;  // {raceId, gender}

    struct EquipPrepData {
        int modelId = 0;
        uint32_t equipmentId = 0;
        bool isPrimary = true;
        std::shared_ptr<ZoneGeometry> geometry;
        std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
    };
    std::vector<EquipPrepData> equipmentData;  // set by 8d, consumed by 8e
};

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
        auto archiveList = resolveEQE1Archives(currentZoneName_);
        for (const auto& name : archiveList.zoneArchives) {
            getOrOpenEQE1(name);
        }
        pendingGlobalEQE1Archives_ = archiveList.globalArchives;
        nextGlobalEQE1Idx_ = 0;
        if (constrainedTextureCache_) {
            constrainedTextureCache_->setEQE1LazyOpener(
                [this]() { return tryOpenNextGlobalEQE1(); });
        }

        // Load composite entity EQE1 files (from entity_composite_builder --eqe1 output)
        if (config_.constrainedConfig.enableCompositeEntities &&
            !config_.constrainedConfig.compositeEntityPath.empty()) {
            namespace fs = std::filesystem;
            const auto& compositeDir = config_.constrainedConfig.compositeEntityPath;
            if (fs::is_directory(compositeDir)) {
                size_t compositeCount = 0;
                for (const auto& entry : fs::directory_iterator(compositeDir)) {
                    if (entry.path().extension() == ".eqe1") {
                        std::string baseName = entry.path().stem().string();
                        // Skip if already loaded (shouldn't happen, but be safe)
                        std::lock_guard<std::mutex> lock(eqe1Mutex_);
                        if (eqe1Loaders_.find(baseName) != eqe1Loaders_.end()) continue;

                        auto loader = std::make_unique<EQE1Loader>();
                        if (loader->open(entry.path().string())) {
                            const EQE1Loader* ptr = loader.get();
                            eqe1Loaders_[baseName] = std::move(loader);
                            if (constrainedTextureCache_) {
                                constrainedTextureCache_->addEQE1Loader(ptr);
                            }
                            ++compositeCount;
                        }
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "EQE1: loaded {} composite entity archives from '{}'",
                         compositeCount, compositeDir);
            }
        }
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
        logAssetBuildTime("seq_s3d_parse", 0, stepStart);
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
        logAssetBuildTime("seq_bsp_compute", 0, stepStart);
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
                    // Size protectedRegions bitvector to cover all BSP regions
                    protectedRegions_.resize(bspTree->regions.size(), false);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step2 Install: BSP tree installed, usePvsCulling=true, "
                              "regions={}, protectedRegions size={}",
                              bspTree->regions.size(), protectedRegions_.size());
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
        logAssetBuildTime("seq_bsp_install", 0, stepStart);
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
        logAssetBuildTime("seq_regions", regionMeshNodes_.size(), stepStart);
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

        logAssetBuildTime("seq_assets", 0, stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep6_Objects(ZoneLoadContext& ctx) {
    // ── Step 6: Objects — zone objects ──────────────────────────────────────
    ctx.updateProgress(75, "Installing zone objects...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ before={}", objectLights_.size());
        deferredObjects_.clear();

        if (pendingZoneComputations_ && !pendingZoneComputations_->prebuiltDeferredObjects.empty()) {
            deferredObjects_ = std::move(pendingZoneComputations_->prebuiltDeferredObjects);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: using prebuilt deferred objects, count={}", deferredObjects_.size());
        } else if (pendingZoneComputations_ && !pendingZoneComputations_->deferredObjectEntries.empty() && currentZone_) {
            for (auto& [objIdx, bspRegion] : pendingZoneComputations_->deferredObjectEntries) {
                if (objIdx >= currentZone_->objects.size()) continue;
                const auto& objInstance = currentZone_->objects[objIdx];
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
                deferredObjects_.push_back(deferred);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: built deferred objects from entries, count={}", deferredObjects_.size());
        } else {
            indexObjectMeshes();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: used indexObjectMeshes() fallback");
        }

        // Zone bounds
        if (currentZone_ && currentZone_->geometry) {
            zoneBoundsMinX_ = currentZone_->geometry->minX;
            zoneBoundsMaxX_ = currentZone_->geometry->maxX;
            zoneBoundsMinY_ = currentZone_->geometry->minY;
            zoneBoundsMaxY_ = currentZone_->geometry->maxY;
            zoneBoundsValid_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from geometry: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      zoneBoundsMinX_, zoneBoundsMaxX_, zoneBoundsMinY_, zoneBoundsMaxY_);
        } else if (!regionBoundingBoxes_.empty()) {
            float rMinX = std::numeric_limits<float>::max(), rMaxX = std::numeric_limits<float>::lowest();
            float rMinY = std::numeric_limits<float>::max(), rMaxY = std::numeric_limits<float>::lowest();
            for (const auto& [idx, bb] : regionBoundingBoxes_) {
                rMinX = std::min(rMinX, bb.MinEdge.X); rMaxX = std::max(rMaxX, bb.MaxEdge.X);
                rMinY = std::min(rMinY, bb.MinEdge.Y); rMaxY = std::max(rMaxY, bb.MaxEdge.Y);
            }
            zoneBoundsMinX_ = rMinX; zoneBoundsMaxX_ = rMaxX;
            zoneBoundsMinY_ = rMinY; zoneBoundsMaxY_ = rMaxY;
            zoneBoundsValid_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from regionBoundingBoxes: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      zoneBoundsMinX_, zoneBoundsMaxX_, zoneBoundsMinY_, zoneBoundsMaxY_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds not available (no combined geometry and no region bounding boxes)");
        }

        // Build all object meshes eagerly inline (no deferred/PVS-gated build at runtime)
        {
            ZoneMeshBuilder objBuilder(smgr_, driver_, device_->getFileSystem());
            objBuilder.setUploadCategory("object");  // P13
            if (constrainedTextureCache_)
                objBuilder.setConstrainedTextureCache(constrainedTextureCache_);
            if (zoneShader_ && zoneShader_->isAvailable()) {
                objBuilder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                   zoneShader_->getActiveAlphaTest(),
                                                   zoneShader_->getActiveAlphaTestRGBA());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder shader materials set (solid={}, alphaTest={})",
                          zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest());
            }
            if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
                objBuilder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                                         zoneShader_->getActiveAtlasAlpha());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder atlas shader materials set (solid={}, alpha={})",
                          zoneShader_->getActiveAtlasSolid(), zoneShader_->getActiveAtlasAlpha());
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: totalObjects={}", deferredObjects_.size());

            size_t totalObjects = deferredObjects_.size();
            size_t objectsBuilt = 0;

            for (size_t oi = 0; oi < totalObjects; ++oi) {
            
                DeferredObject& deferred = deferredObjects_[oi];
                if (deferred.meshBuilt) { ++objectsBuilt; continue; }
                if (deferred.objectIndex >= currentZone_->objects.size()) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const auto& objInstance = currentZone_->objects[deferred.objectIndex];
                if (!objInstance.geometry || !objInstance.placeable) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const std::string& objName = objInstance.placeable->getName();

                // Build mesh
                irr::scene::IMesh* objMesh = nullptr;
                if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                    objMesh = objBuilder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built textured mesh, texNames={}",
                              oi, objName, objInstance.geometry->textureNames().size());
                } else {
                    objMesh = objBuilder.buildColoredMesh(*objInstance.geometry);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built colored mesh (no textures)", oi, objName);
                }

                // Track missing textures for rebuild when they arrive
                const auto& missingObjTex = objBuilder.getMissingTextures();
                if (!missingObjTex.empty()) {
                    for (const auto& texName : missingObjTex)
                        pendingTextureObjects_[texName].insert(oi);
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' has {} missing textures queued for later rebuild",
                             oi, objName, missingObjTex.size());
                }

                if (!objMesh) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' mesh build returned null — skipping", oi, objName);
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                irr::scene::IMeshSceneNode* objNode = smgr_->addMeshSceneNode(objMesh);
                if (!objNode) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' addMeshSceneNode failed — mesh dropped", oi, objName);
                    objMesh->drop(); deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                // Transform
                float scaleX = objInstance.placeable->getScaleX();
                float scaleY = objInstance.placeable->getScaleY();
                float scaleZ = objInstance.placeable->getScaleZ();
                objNode->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

                float ox = objInstance.placeable->getX();
                float oy = objInstance.placeable->getY();
                float oz = objInstance.placeable->getZ();
                objNode->setPosition(irr::core::vector3df(ox, oz, oy));
                objNode->setRotation(irr::core::vector3df(
                    objInstance.placeable->getRotateX(),
                    objInstance.placeable->getRotateY(),
                    objInstance.placeable->getRotateZ()));

                // Materials
                for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                    objNode->getMaterial(mi).Lighting = lightingEnabled_;
                    objNode->getMaterial(mi).BackfaceCulling = false;
                    objNode->getMaterial(mi).GouraudShading = true;
                    objNode->getMaterial(mi).FogEnable = fogEnabled_;
                    objNode->getMaterial(mi).Wireframe = wireframeMode_;
                    objNode->getMaterial(mi).NormalizeNormals = true;
                    objNode->getMaterial(mi).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    objNode->getMaterial(mi).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }

                // Note: tree wind shaders (treeIdentifier_/zoneShader_ config)
                // and animatedTextureManager_ are not yet created (Step 11).
                // A fixup pass after Step 11 applies these to objects.

                // Register vertex-animated objects (flags, banners)
                if (objInstance.geometry && objInstance.geometry->animatedVertices &&
                    objInstance.geometry->animatedVertices->frames.size() >= 2) {
                    auto& animData = objInstance.geometry->animatedVertices;
                    irr::scene::IMesh* objMesh = objNode->getMesh();
                    if (objMesh) {
                        VertexAnimatedMesh vam;
                        vam.node = objNode;
                        vam.mesh = objMesh;
                        vam.animData = animData;
                        vam.objectName = objName;

                        // Compute center offset: geometry vertices have center baked in,
                        // animation frame positions are raw. Offset = geomVertex - animFrame0.
                        const auto& frame0 = animData->frames[0];
                        size_t animVertCount = frame0.positions.size() / 3;
                        if (!objInstance.geometry->vertices.empty() && animVertCount > 0) {
                            const auto& gv = objInstance.geometry->vertices[0];
                            vam.centerOffsetX = gv.x - frame0.positions[0];
                            vam.centerOffsetY = gv.y - frame0.positions[1];
                            vam.centerOffsetZ = gv.z - frame0.positions[2];
                            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj '{}' vertex anim center offset: "
                                      "({:.2f},{:.2f},{:.2f}) from geom({:.2f},{:.2f},{:.2f}) - anim({:.2f},{:.2f},{:.2f})",
                                      objName, vam.centerOffsetX, vam.centerOffsetY, vam.centerOffsetZ,
                                      gv.x, gv.y, gv.z,
                                      frame0.positions[0], frame0.positions[1], frame0.positions[2]);
                        } else {
                            vam.centerOffsetX = 0;
                            vam.centerOffsetY = 0;
                            vam.centerOffsetZ = 0;
                            LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj '{}' vertex anim: cannot compute center offset "
                                     "(verts={}, animVerts={})", objName,
                                     objInstance.geometry->vertices.size(), animVertCount);
                        }

                        // Build vertex mapping: for each mesh buffer vertex, find the
                        // corresponding geometry vertex index (= animation vertex index)
                        // by matching positions (EQ coords → Irrlicht coords: x,z,y)
                        size_t expectedVerts = animData->frames[0].positions.size() / 3;
                        vam.vertexMapping.resize(objMesh->getMeshBufferCount());
                        size_t mappedVerts = 0;
                        for (irr::u32 b = 0; b < objMesh->getMeshBufferCount(); ++b) {
                            irr::scene::IMeshBuffer* buf = objMesh->getMeshBuffer(b);
                            auto* vertices = static_cast<irr::video::S3DVertex*>(buf->getVertices());
                            irr::u32 vertCount = buf->getVertexCount();
                            vam.vertexMapping[b].resize(vertCount, SIZE_MAX);

                            for (irr::u32 v = 0; v < vertCount; ++v) {
                                // Match by position: mesh stores (x, z, y) from geometry (x, y, z)
                                float mx = vertices[v].Pos.X;
                                float my = vertices[v].Pos.Y;  // = geom z
                                float mz = vertices[v].Pos.Z;  // = geom y
                                for (size_t gi = 0; gi < objInstance.geometry->vertices.size(); ++gi) {
                                    const auto& gv = objInstance.geometry->vertices[gi];
                                    if (std::abs(gv.x - mx) < 0.01f &&
                                        std::abs(gv.z - my) < 0.01f &&
                                        std::abs(gv.y - mz) < 0.01f) {
                                        if (gi < expectedVerts) {
                                            vam.vertexMapping[b][v] = gi;
                                            mappedVerts++;
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        vertexAnimatedMeshes_.push_back(std::move(vam));
                        LOG_INFO(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' registered as vertex-animated "
                                 "({} frames, {}ms delay, {} mapped/{} total verts)",
                                 oi, objName, animData->frames.size(), animData->delayMs,
                                 mappedVerts, objInstance.geometry->vertices.size());
                    }
                }

                objNode->setName(objName.c_str());
                objNode->grab();
                size_t newNodeIndex = objectNodes_.size();
                objectNodes_.push_back(objNode);
                objectPositions_.push_back(irr::core::vector3df(ox, oz, oy));
                if (zoneBspTree_)
                    objectRegions_.push_back(zoneBspTree_->findRegionIndexForPoint(ox, oy, oz));
                else
                    objectRegions_.push_back(SIZE_MAX);
                objNode->updateAbsolutePosition();
                objectBoundingBoxes_.push_back(objNode->getTransformedBoundingBox());
                deferred.nodeIndex = newNodeIndex;

                // PVS visibility (no SimWorker yet, so all visible).
                // When manualZoneDrawEnabled_, objects are always out of scene graph
                // and rendered via drawZoneGeometrySorted() instead.
                bool pvsVisible = isRegionPvsVisible(objectRegions_.back());
                if (!pvsVisible || manualZoneDrawEnabled_) {
                    objNode->remove();
                    if (!pvsVisible) {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' removed from scene graph (PVS hidden, region={})",
                                  oi, objName, objectRegions_.back());
                    }
                }
                objectInSceneGraph_.push_back(pvsVisible && !manualZoneDrawEnabled_);

                // Object lights (torch, lantern, fire, candle, etc.)
                std::string upperName = objName;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

                bool hasFireTexture = false;
                bool hasLanternTexture = false;
                if (objInstance.geometry) {
                    for (const auto& texName : objInstance.geometry->textureNames()) {
                        std::string upperTex = texName;
                        std::transform(upperTex.begin(), upperTex.end(), upperTex.begin(), ::toupper);
                        if (upperTex.find("FIRE") != std::string::npos ||
                            upperTex.find("COAL") != std::string::npos ||
                            upperTex.find("TORCH") != std::string::npos)
                            hasFireTexture = true;
                        if (upperTex.find("LANTERN") != std::string::npos ||
                            upperTex.find("LANT") != std::string::npos)
                            hasLanternTexture = true;
                    }
                }

                bool isLightSource = false;
                irr::video::SColorf lightColor(1.0f, 0.6f, 0.2f, 1.0f);
                float lightRadius = 100.0f;

                if (upperName.find("TORCH") != std::string::npos ||
                    upperName.find("FIRE") != std::string::npos ||
                    upperName.find("BRAZIER") != std::string::npos ||
                    upperName.find("FLAME") != std::string::npos ||
                    hasFireTexture) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(1.0f, 0.5f, 0.15f, 1.0f);
                    lightRadius = 120.0f;
                } else if (upperName.find("LANTERN") != std::string::npos ||
                           upperName.find("LANT") != std::string::npos ||
                           upperName.find("LAMP") != std::string::npos ||
                           upperName.find("LIGHT") != std::string::npos ||
                           hasLanternTexture) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(0.25f, 0.21f, 0.15f, 1.0f);
                    lightRadius = 100.0f;
                } else if (upperName.find("CANDLE") != std::string::npos) {
                    isLightSource = true;
                    lightColor = irr::video::SColorf(1.0f, 0.9f, 0.7f, 1.0f);
                    lightRadius = 50.0f;
                }

                if (isLightSource) {
                    irr::core::vector3df lightPos(ox, oz, oy);

                    // Find nearby zone light with elevated position
                    if (currentZone_ && !currentZone_->lights.empty()) {
                        float bestDist = 50.0f;
                        for (const auto& zoneLight : currentZone_->lights) {
                            float dx = zoneLight->x - ox;
                            float dy = zoneLight->y - oy;
                            float hDist = std::sqrt(dx * dx + dy * dy);
                            if (hDist < bestDist) {
                                bestDist = hDist;
                                lightPos = irr::core::vector3df(zoneLight->x, zoneLight->z, zoneLight->y);
                            }
                        }
                    }

                    irr::scene::ILightSceneNode* lightNode = smgr_->addLightSceneNode(
                        nullptr, lightPos, lightColor, lightRadius * 1.5f);

                    if (lightNode) {
                        irr::video::SLight& lightData = lightNode->getLightData();
                        lightData.Type = irr::video::ELT_POINT;
                        lightData.Attenuation = irr::core::vector3df(1.0f, 0.007f, 0.0002f);

                        lightNode->grab();
                        lightNode->remove();

                        ObjectLight objLight;
                        objLight.node = lightNode;
                        objLight.position = lightPos;
                        objLight.objectName = objName;
                        objLight.originalColor = lightColor;
                        objLight.bspRegion = objectRegions_.back();

                        if (upperName.find("TORCH") != std::string::npos ||
                            upperName.find("FIRE") != std::string::npos ||
                            upperName.find("BRAZIER") != std::string::npos ||
                            upperName.find("FLAME") != std::string::npos ||
                            upperName.find("CANDLE") != std::string::npos ||
                            hasFireTexture) {
                            objLight.isFireSource = true;
                            objLight.flickerPhase = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
                            objLight.flickerSpeed = 0.8f + static_cast<float>(rand()) / RAND_MAX * 0.4f;
                        }

                        bool lightPvsVis = isRegionPvsVisible(objLight.bspRegion);
                        if (lightPvsVis)
                            smgr_->getRootSceneNode()->addChild(lightNode);
                        objectLightInSceneGraph_.push_back(lightPvsVis);

                        objectLights_.push_back(objLight);
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' light created: pos=({:.1f},{:.1f},{:.1f}) "
                                  "radius={:.0f} fire={} pvsVisible={} region={}",
                                  oi, objName, lightPos.X, lightPos.Y, lightPos.Z,
                                  lightRadius, objLight.isFireSource, lightPvsVis, objLight.bspRegion);
                    } else {
                        LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' addLightSceneNode failed", oi, objName);
                    }
                }

                objMesh->drop();
                deferred.meshBuilt = true;
                ++objectsBuilt;

                if (objectsBuilt % 20 == 0 && totalObjects > 0) {
                    int pct = 75 + static_cast<int>(3.0f * objectsBuilt / totalObjects);
                    ctx.updateProgress(pct, "Building objects [" + std::to_string(objectsBuilt)
                                        + "/" + std::to_string(totalObjects) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: built all {} object meshes eagerly", objectsBuilt);
        }
        {
            int fireSrcCount = 0;
            for (const auto& ol : objectLights_) { if (ol.isFireSource) ++fireSrcCount; }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ after={}, fire sources from objects={}",
                      objectLights_.size(), fireSrcCount);
        }
        logAssetBuildTime("seq_objects", deferredObjects_.size(), stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep7_Doors(ZoneLoadContext& ctx) {
    // ── Step 7: Doors — door meshes ────────────────────────────────────────
    ctx.updateProgress(78, "Building door meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: doorManager_={}", (bool)doorManager_);
        if (!doorManager_) {
            doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
            if (constrainedTextureCache_)
                doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
            if (currentZone_) doorManager_->setZone(currentZone_);
            if (zoneBspTree_) doorManager_->setBspTree(zoneBspTree_.get());
            doorManager_->setPvsRegion(currentPvsRegion_);
            if (frustumCuller_) doorManager_->setFrustumCuller(frustumCuller_.get());
            doorManager_->setRegionNeighbors(regionNeighbors_.empty() ? nullptr : &regionNeighbors_);
            if (zoneShader_ && zoneShader_->isAvailable())
                doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                     zoneShader_->getActiveAlphaTest(),
                                                     zoneShader_->getActiveAlphaTestRGBA());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: created DoorManager (zone={}, bsp={}, frustum={}, neighbors={}, shader={})",
                      (bool)currentZone_, (bool)zoneBspTree_, (bool)frustumCuller_,
                      !regionNeighbors_.empty(), zoneShader_ && zoneShader_->isAvailable());
        }

        // Rebuild all doors at once
        if (doorManager_) {
            std::vector<uint8_t> doorList;
            doorManager_->getUnbuiltDoors(doorList);
            size_t unbuiltCount = doorList.size();
            size_t placeholderCount = 0;
            for (uint8_t id = 0; id < 255; ++id) {
                const auto* door = doorManager_->getDoor(id);
                if (door && door->meshBuilt && door->usePlaceholder) {
                    doorList.push_back(id);
                    ++placeholderCount;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: door rebuild list: {} unbuilt + {} placeholder = {} total",
                      unbuiltCount, placeholderCount, doorList.size());
            size_t totalDoors = doorList.size();
            size_t doorsBuilt = 0;
            size_t doorsFailed = 0;
            size_t doorsWithMissingTex = 0;
            for (uint8_t doorId : doorList) {
                bool rebuilt = doorManager_->rebuildSingleDoor(doorId);
                if (!rebuilt) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} rebuildSingleDoor failed", doorId);
                    ++doorsFailed;
                }
                const auto& missing = doorManager_->getLastMissingTextures();
                if (!missing.empty()) {
                    for (const auto& texName : missing)
                        pendingTextureDoors_[texName].insert(doorId);
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} has {} missing textures queued for later rebuild",
                             doorId, missing.size());
                    ++doorsWithMissingTex;
                }
                ++doorsBuilt;
                // Update progress every 10 doors (78-80%)
                if (doorsBuilt % 10 == 0 && totalDoors > 0) {
                    int pct = 78 + static_cast<int>(2.0f * doorsBuilt / totalDoors);
                    ctx.updateProgress(pct, "Building doors [" + std::to_string(doorsBuilt)
                                        + "/" + std::to_string(totalDoors) + "]");
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: rebuilt {} doors ({} failed, {} with missing textures)",
                     totalDoors, doorsFailed, doorsWithMissingTex);
        }
        // Constrained cache was set at creation above — no separate runtime set needed.
        logAssetBuildTime("seq_doors", 0, stepStart);
    }
    FlushThreadLog();
}

bool IrrlichtRenderer::loadZoneStep8_Entities(ZoneLoadContext& ctx) {
    // ── Step 8: Entities — inline prep + build (no background threads) ────
    ctx.updateProgress(80, "Preparing entity models...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityRenderer_={}, skipEntityBuild={}",
                  (bool)entityRenderer_, config_.constrainedConfig.skipEntityBuild);

        if (entityRenderer_ && entityRenderer_->getRaceModelLoader()
            && !config_.constrainedConfig.skipEntityBuild) {

            ctx.modelLoader = entityRenderer_->getRaceModelLoader();
            ctx.equipLoader = entityRenderer_->getEquipmentModelLoader();
            const auto& entities = entityRenderer_->getEntities();
            ctx.totalEntities = entities.size();
            ctx.prepCount = 0;
            ctx.buildCount = 0;
            ctx.modelCacheHits = 0;
            ctx.modelDiskLoads = 0;

            LOG_INFO(MOD_GRAPHICS, "Sequential: preparing {} entities inline (chrCacheMaxEntries={})",
                     ctx.totalEntities, config_.constrainedConfig.chrCacheMaxEntries);

            // Collect spawn IDs (iterate copy since we modify visuals)
            std::vector<uint16_t> spawnIds;
            spawnIds.reserve(ctx.totalEntities);
            for (const auto& [spawnId, vis] : entities) {
                spawnIds.push_back(spawnId);
            }

            for (uint16_t spawnId : spawnIds) {

                const auto& entities2 = entityRenderer_->getEntities();
                auto it = entities2.find(spawnId);
                if (it == entities2.end()) continue;
                ctx.spawnId = spawnId;
                ctx.vis = &it->second;

                // Update progress within entity phase (80-88%)
                if (ctx.totalEntities > 0) {
                    int pct = 80 + static_cast<int>(8.0f * ctx.prepCount / ctx.totalEntities);
                    ctx.updateProgress(pct, "Preparing entities [" + std::to_string(ctx.prepCount + 1)
                                        + "/" + std::to_string(ctx.totalEntities) + "]");
                }

                // ── Inline entity prep (EntityPrepWorker::processRequest logic) ──

                if (!loadZoneStep8a_BaseModelPrep(ctx)) return false;

                // T38: a missing base model (ctx.entitySuccess == false) means
                // there is no model data for 8b (variant model)/8c (variant
                // textures)/8d (equipment — keyed off the base model's staging
                // data)/8e (distributes 8b-8d's prep results) to act on. Only
                // 8a itself and 8f (mesh build, which independently falls back
                // to a placeholder cube via EntityRenderer::getMeshForRace when
                // no model is cached) run for a skipped entity — see
                // loadZoneStep8a_BaseModelPrep for where the skip is decided
                // and counted.
                if (ctx.entitySuccess) {
                    loadZoneStep8b_VariantModelPrep(ctx);

                    loadZoneStep8c_VariantTextures(ctx);

                    if (!loadZoneStep8d_EquipmentPrep(ctx)) return false;

                    loadZoneStep8e_DistributePrepResults(ctx);
                }

                // T04: processUploadQueue removed. Textures uploaded via uploadTexture() in T09.

                // Promote prepared models so buildEntityMesh finds them
                ctx.modelLoader->promotePreparedModels();

                // Upload all pending textures (including composite EQE1 entries queued in Step 8a)
                // so they're available as GPU textures when buildEntityMesh builds composite meshes.
                if (constrainedTextureCache_) {
                    constrainedTextureCache_->uploadAllPending();
                }

                loadZoneStep8f_BuildEntityMesh(ctx);

                ++ctx.prepCount;
            }

            bool hasBoats = entityRenderer_->hasBoatsInZone();
            LOG_INFO(MOD_GRAPHICS, "Sequential: prepped {} entities, built {} meshes (model cache hits={}, disk loads={}, hasBoats={})",
                     ctx.prepCount, ctx.buildCount, ctx.modelCacheHits, ctx.modelDiskLoads, hasBoats);
            if (hasBoats) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: zone has boats — boat collision checks enabled");
            } else {
                LOG_INFO(MOD_GRAPHICS, "Sequential: no boats in zone — boat collision checks disabled (zero per-frame cost)");
            }

            // T38: make missing-model skips visible, not a silent omission — a zone
            // that rendered with placeholder-cube NPCs must say so plainly, loudly,
            // and every time (not just on first occurrence), so this is unconditional
            // (always logged, even when entitiesSkipped == 0, so its absence in a log
            // is itself confirmation nothing was skipped rather than evidence the
            // check didn't run).
            if (ctx.entitiesSkipped > 0) {
                std::string pairsStr;
                for (const auto& [raceId, gender] : ctx.skippedRaceGenderPairs) {
                    if (!pairsStr.empty()) pairsStr += ", ";
                    pairsStr += "race=" + std::to_string(raceId) + " gender=" + std::to_string(static_cast<int>(gender));
                }
                LOG_WARN(MOD_GRAPHICS, "Sequential: SKIPPED {} entities with no loadable base model "
                         "({} distinct race/gender pair(s): {}) — rendered as placeholder cubes, "
                         "zone load continued",
                         ctx.entitiesSkipped, ctx.skippedRaceGenderPairs.size(), pairsStr);
            } else {
                LOG_INFO(MOD_GRAPHICS, "Sequential: 0 entities skipped for missing base models — all {} entities had loadable models",
                         ctx.prepCount);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entity prep skipped (entityRenderer_={}, raceModelLoader={}, skipEntityBuild={})",
                      (bool)entityRenderer_,
                      entityRenderer_ ? (bool)entityRenderer_->getRaceModelLoader() : false,
                      config_.constrainedConfig.skipEntityBuild);
        }
        // T09: Upload all decoded textures from Steps 8a-8d to GPU.
        // getOrLoad() decoded them; uploadAllPending() promotes DecodedEntry → CachedTexture.
        if (constrainedTextureCache_) {
            int uploaded = constrainedTextureCache_->uploadAllPending();
            LOG_INFO(MOD_GRAPHICS, "SEQ Step8: uploadAllPending uploaded {} entity textures", uploaded);
        }

        entityPrepReady_ = true;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityPrepReady_=true");
        logAssetBuildTime("seq_entities", 0, stepStart);
        LOG_INFO(MOD_GRAPHICS, "SEQ Step8: about to exit scope");
    }
    LOG_INFO(MOD_GRAPHICS, "SEQ Step8: scope exited, flushing log...");
    FlushThreadLog();
    LOG_INFO(MOD_GRAPHICS, "SEQ: Step 8 complete, entering Step 9...");
    FlushThreadLog();
    return true;
}

bool IrrlichtRenderer::loadZoneStep8a_BaseModelPrep(ZoneLoadContext& ctx) {
    // Step 8a: Base race model prep (S3D load, WLD parse, animation merge)
    uint32_t key = (static_cast<uint32_t>(ctx.vis->raceId) << 8) | ctx.vis->gender;
    bool alreadyCached = ctx.modelLoader->isModelDataCached(key);
    bool success = false;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8a: entity {} ({}) race={} gender={} cacheKey={} cached={}",
              ctx.spawnId, ctx.vis->name, ctx.vis->raceId, ctx.vis->gender, key, alreadyCached);

    if (alreadyCached) {
        success = true;
        ++ctx.modelCacheHits;
    } else {
        auto prepStart = std::chrono::steady_clock::now();
        success = ctx.modelLoader->preloadModelData(ctx.vis->raceId, ctx.vis->gender);
        auto prepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - prepStart).count();
        if (success) {
            ++ctx.modelDiskLoads;
        } else {
            // T38: a single entity's missing base model is NOT a zone-fatal
            // condition (was: LOG_FATAL + loadFailed_=true + abort the whole
            // zone load — see deferred.md "race=415 missing model data still
            // crashes graphics mode"). Downgraded to WARN + skip just this
            // entity: the caller loop (loadZoneStep8_Entities) checks
            // ctx.entitySuccess and skips 8b-8e (which all depend on base
            // model data existing), but still runs 8f, whose
            // EntityRenderer::getMeshForRace() already has a designed
            // fallback to a colored placeholder cube (src/client/graphics/
            // CLAUDE.md, "Model Loading Order" step 5) — this is what lets
            // that fallback actually be reached instead of the zone dying
            // first. Counted here (not just logged) so the zone-load summary
            // can name exactly how many entities and which race/gender pairs
            // were affected — see the end of loadZoneStep8_Entities.
            LOG_WARN(MOD_GRAPHICS, "Skipping entity {} ({}): base race model not found: race={} gender={} "
                     "(not found in any S3D archive) — will render as a placeholder cube, zone load continues",
                     ctx.spawnId, ctx.vis->name, ctx.vis->raceId, ctx.vis->gender);
            ++ctx.entitiesSkipped;
            ctx.skippedRaceGenderPairs.insert({ctx.vis->raceId, ctx.vis->gender});
        }
        LOG_INFO(MOD_GRAPHICS, "Sequential: preload race={} gender={} took {}ms success={}",
                 ctx.vis->raceId, ctx.vis->gender, prepMs, success);
    }
    ctx.entitySuccess = success;
    return true;
}

void IrrlichtRenderer::loadZoneStep8b_VariantModelPrep(ZoneLoadContext& ctx) {
    // Step 8b: Variant model prep (zone-specific S3D for head/body variants)
    if (ctx.entitySuccess) {
        uint8_t headVariant = ctx.vis->appearance.helm;
        uint8_t bodyVariant = 0;
        uint8_t chestMaterial = static_cast<uint8_t>(
            ctx.vis->appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
        if (isRobeTexture(ctx.vis->appearance.texture) || isRobeTexture(chestMaterial)) {
            bodyVariant = 1;
        }
        if (headVariant != 0 || bodyVariant != 0) {
            ctx.modelLoader->preloadVariantModel(ctx.vis->raceId, ctx.vis->gender, headVariant, bodyVariant);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8b: entity {} ({}) preloadVariantModel head={} body={}",
                      ctx.spawnId, ctx.vis->name, headVariant, bodyVariant);
        }
    }
}

void IrrlichtRenderer::loadZoneStep8c_VariantTextures(ZoneLoadContext& ctx) {
    // Step 8c: Load variant textures via constrained cache
    // T06: Replaced inline DDS/BMP decoder with getOrLoad() calls.
    ctx.variantTextureCount = 0;
    {
        auto modelData = ctx.modelLoader->getRaceModelData(ctx.vis->raceId, ctx.vis->gender);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) appearance.texture={} helm={} modelData={}",
                  ctx.spawnId, ctx.vis->name, ctx.vis->appearance.texture, ctx.vis->appearance.helm, (bool)modelData);
        if (ctx.vis->appearance.texture != 0 || ctx.vis->appearance.helm != 0) {
            std::string raceCode = RaceModelLoader::getRaceCode(ctx.vis->raceId);
            std::string lowerRaceCode = raceCode;
            std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            std::set<std::string> loadedNames;
            auto loadVariantTexture = [&](const std::string& texName, const std::shared_ptr<TextureInfo>& texInfo) {
                if (!texInfo || texInfo->data.empty()) return;
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (loadedNames.count(lowerName) > 0) return;
                constrainedTextureCache_->getOrLoad(lowerName, texInfo->data,
                                                     "entity_perpart", "loadZoneStep8c_VariantTextures");
                loadedNames.insert(lowerName);
                ++ctx.variantTextureCount;
            };

            // Build variant prefixes
            std::vector<std::string> variantPrefixes;
            if (ctx.vis->appearance.texture >= 10) {
                char buf[32];
                snprintf(buf, sizeof(buf), "clk%02d", ctx.vis->appearance.texture - 10);
                variantPrefixes.push_back(buf);
            } else if (ctx.vis->appearance.texture > 0) {
                char buf[32];
                const char* slots[] = {"ch", "ua", "fa", "hn", "lg", "ft"};
                for (const char* slot : slots) {
                    snprintf(buf, sizeof(buf), "%s%s%02d", lowerRaceCode.c_str(), slot, ctx.vis->appearance.texture);
                    variantPrefixes.push_back(buf);
                }
            }
            if (ctx.vis->appearance.helm > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%she%02d", lowerRaceCode.c_str(), ctx.vis->appearance.helm);
                variantPrefixes.push_back(buf);
            }

            auto searchTextures = [&](const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
                for (const auto& [texName, texInfo] : textures) {
                    std::string ln = texName;
                    std::transform(ln.begin(), ln.end(), ln.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    for (const auto& prefix : variantPrefixes) {
                        if (ln.find(prefix) != std::string::npos) {
                            loadVariantTexture(texName, texInfo);
                            break;
                        }
                    }
                }
            };

            if (modelData && !modelData->textures.empty())
                searchTextures(modelData->textures);

            // Search variant model textures
            uint8_t hv = ctx.vis->appearance.helm;
            uint8_t bv2 = 0;
            uint8_t cm = static_cast<uint8_t>(
                ctx.vis->appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
            if (isRobeTexture(ctx.vis->appearance.texture) || isRobeTexture(cm))
                bv2 = 1;
            if (hv != 0 || bv2 != 0) {
                auto variantModelData = ctx.modelLoader->getVariantModelData(
                    ctx.vis->raceId, ctx.vis->gender, hv, bv2);
                if (variantModelData && !variantModelData->textures.empty())
                    searchTextures(variantModelData->textures);
            }
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) loaded {} variant textures via cache",
              ctx.spawnId, ctx.vis->name, ctx.variantTextureCount);
}

bool IrrlichtRenderer::loadZoneStep8d_EquipmentPrep(ZoneLoadContext& ctx) {
    // Step 8d: Equipment model extraction + texture decode via constrained cache
    // T07: Replaced inline DDS decoder with getOrLoad() calls.
    using EquipPrepData = ZoneLoadContext::EquipPrepData;
    ctx.equipmentData.clear();

    bool equipError = false;
    if (ctx.equipLoader) {
        uint32_t primaryId = ctx.vis->appearance.equipment[7];
        uint32_t secondaryId = ctx.vis->appearance.equipment[8];

        auto prepOneEquip = [&](uint32_t equipmentId, bool isPrimary) {
            if (equipError) return;
            if (equipmentId == 0) return;
            int modelId = ctx.equipLoader->getModelIdForItem(equipmentId);
            if (modelId < 0) modelId = static_cast<int>(equipmentId);
            const EquipmentModelLoader::EquipmentModelRef* modelRef = ctx.equipLoader->getModelRef(modelId);
            if (!modelRef) {
                // P10b (borrowed verbatim from task/anim-video-capture's
                // already-documented, already-validated workaround, applied
                // here because this pre-existing FATAL blocked P10b's own
                // befallen verification matrix in this environment — a
                // single NPC's missing equipment-model data-completeness gap
                // (e.g. a giant rat with a primary-weapon modelId not present
                // in item_models.json) previously aborted the ENTIRE zone
                // load. Not root-caused here either (out of scope for P10b;
                // item_models.json coverage is a separate data-completeness
                // question) — degraded to WARN + skip this one equipment
                // slot (entity renders unequipped for that slot).
                LOG_WARN(MOD_GRAPHICS, "Skipping equipment model ref: entity {} ({}) equipment {} ({}) modelId={} (not found in item_models.json) — entity will render unequipped for this slot",
                          ctx.spawnId, ctx.vis->name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                return;
            }
            auto equipData2 = EquipmentModelLoader::extractEquipmentModelOffThread(*modelRef, modelId);
            if (!equipData2) {
                LOG_WARN(MOD_GRAPHICS, "Skipping equipment model: entity {} ({}) equipment {} ({}) modelId={} — entity will render unequipped for this slot",
                          ctx.spawnId, ctx.vis->name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                return;
            }

            EquipPrepData prepData;
            prepData.modelId = modelId;
            prepData.equipmentId = equipmentId;
            prepData.isPrimary = isPrimary;
            prepData.geometry = equipData2->geometry;
            prepData.rawTextures = equipData2->textures;

            // Load equipment textures into constrained cache
            for (const auto& texName : equipData2->textureNames) {
                std::string ln = texName;
                std::transform(ln.begin(), ln.end(), ln.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                auto texIt = equipData2->textures.find(ln);
                if (texIt == equipData2->textures.end() || !texIt->second) continue;
                const auto& texInfo = texIt->second;
                if (texInfo->data.empty()) continue;

                constrainedTextureCache_->getOrLoad(ln, texInfo->data,
                                                     "entity_perpart", "loadZoneStep8d_EquipmentPrep");
            }
            ctx.equipmentData.push_back(std::move(prepData));
        };

        prepOneEquip(primaryId, true);
        prepOneEquip(secondaryId, false);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8d: entity {} ({}) equipment: primary={} secondary={} extracted={}",
                  ctx.spawnId, ctx.vis->name, ctx.vis->appearance.equipment[7], ctx.vis->appearance.equipment[8], ctx.equipmentData.size());
    }

    if (equipError) {
        pendingZoneComputations_.reset();
        loadFailed_ = true;
        return false;
    }
    return true;
}

void IrrlichtRenderer::loadZoneStep8e_DistributePrepResults(ZoneLoadContext& ctx) {
    // Step 8e: Distribute prep results to EntityVisual
    // (equivalent to pollAndDistributePrepResults inline)
    {
        // We need a mutable reference — use const_cast since we own the renderer
        auto& mutableEntities = const_cast<std::map<uint16_t, EntityVisual>&>(entityRenderer_->getEntities());
        auto visIt = mutableEntities.find(ctx.spawnId);
        if (visIt == mutableEntities.end()) {
            LOG_WARN(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) vanished before distribution — prep work discarded "
                     "(variantTex={}, equipData={})",
                     ctx.spawnId, ctx.vis->name, ctx.variantTextureCount, ctx.equipmentData.size());
        } else {
            auto& mvis = visIt->second;
            // T06: variantTextures no longer stored on EntityVisual — textures are in constrained cache.

            for (auto& eq : ctx.equipmentData) {
                EntityVisual::EquipmentStaging staging;
                staging.modelId = eq.modelId;
                staging.equipmentId = eq.equipmentId;
                staging.isPrimary = eq.isPrimary;
                staging.geometry = eq.geometry;
                staging.rawTextures = eq.rawTextures;
                mvis.equipmentStaging.push_back(std::move(staging));
            }

            // Textures already in constrained cache from T05 (base) + T06 (variant) + T07 (equipment).
            mvis.entityPrepComplete = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) distributed: equipStaging={}",
                      ctx.spawnId, ctx.vis->name, mvis.equipmentStaging.size());
        }
    }
}

void IrrlichtRenderer::loadZoneStep8f_BuildEntityMesh(ZoneLoadContext& ctx) {
    // Step 8f: Build entity mesh (GL work — scene node, materials, name tag)
    bool meshBuilt = entityRenderer_->buildEntityMesh(ctx.spawnId);
    if (meshBuilt) ++ctx.buildCount;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8f: entity {} ({}) buildEntityMesh result={}", ctx.spawnId, ctx.vis->name, meshBuilt);
}

void IrrlichtRenderer::loadZoneStep9_Collision(ZoneLoadContext& ctx) {
    // ── Step 9: Collision ──────────────────────────────────────────────────
    ctx.updateProgress(88, "Setting up collision...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: smgr_={}, regionWorldTriangles_={}, objectNodes_={}, zoneBspTree_={}",
                  (bool)smgr_, regionWorldTriangles_.size(), objectNodes_.size(), (bool)zoneBspTree_);
        setupMinimalZoneCollision();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: setupMinimalZoneCollision complete, regionWorldTriangles_={}",
                  regionWorldTriangles_.size());

        // Add eagerly-built objects as pre-transformed world-space triangles per BSP region
        size_t objectsAdded = 0;
        size_t totalObjectTriangles = 0;
        if (smgr_) {
            for (size_t oi = 0; oi < objectNodes_.size(); ++oi) {
                auto* node = objectNodes_[oi];
                if (node && node->getMesh()) {
                    auto triangles = extractWorldTriangles(smgr_, node);
                    if (!triangles.empty()) {
                        size_t region = (oi < objectRegions_.size()) ? objectRegions_[oi] : SIZE_MAX;
                        auto& regionTris = objectWorldTriangles_[region];
                        totalObjectTriangles += triangles.size();
                        regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
                        objectsAdded++;
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: {} objects -> {} world-space triangles across {} regions",
                     objectsAdded, totalObjectTriangles, objectWorldTriangles_.size());
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: collision setup complete, "
                  "regionWorldTriangles_={}, objectWorldTriangles_={}, doorCollisionData_={}, bspFiltered={}",
                  regionWorldTriangles_.size(), objectWorldTriangles_.size(),
                  doorCollisionData_.size(), (bool)(zoneBspTree_ && !regionWorldTriangles_.empty()));
        logAssetBuildTime("seq_collision", 0, stepStart);
    }
    FlushThreadLog();
}

bool IrrlichtRenderer::loadZoneStep10_Sky(ZoneLoadContext& ctx) {
    // ── Step 10: Sky — all sky, fog, weather ───────────────────────────────
    ctx.updateProgress(90, "Loading sky and weather...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        bool skyRendering = config_.constrainedConfig.skyRendering;
        auto skyDomeMode = config_.constrainedConfig.skyDomeMode;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: computations={}, skyRendering={}, skyDomeMode={}, "
                  "storedZoneEnv.pending={}, skyRenderer_={}, weatherSystem_={}",
                  (bool)computations, skyRendering, (int)skyDomeMode,
                  storedZoneEnvironment_.pending, (bool)skyRenderer_, (bool)weatherSystem_);

        // Names of sky textures loaded into constrained cache (used by upload step after skyLoader is moved)
        std::vector<std::string> loadedSkyTextureNames;

        // Load sky data inline (was background thread)
        if (computations) {
            if (skyRendering) {
                auto skyData = std::make_unique<PendingZoneComputations::SkyLoadData>();
                skyData->skyLoader = std::make_unique<SkyLoader>();
                skyData->skyConfig = std::make_unique<SkyConfig>();

                if (skyData->skyLoader->load(ctx.eqPath)) {
                    LOG_INFO(MOD_GRAPHICS, "Sequential: sky.s3d loaded ({} textures)",
                             skyData->skyLoader->getSkyData()->textures.size());
                } else {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load sky S3D archive: {}sky.s3d (sky rendering enabled in preset)", ctx.eqPath);
                    pendingZoneComputations_.reset();
                    loadFailed_ = true;
                    return false;
                }
                std::string skyIniPath = ctx.eqPath + "sky.ini";
                bool skyIniLoaded = skyData->skyConfig->loadFromFile(skyIniPath);
                if (!skyIniLoaded) {
                    LOG_FATAL(MOD_GRAPHICS, "Failed to load sky config: {} (sky rendering enabled in preset)", skyIniPath);
                    pendingZoneComputations_.reset();
                    loadFailed_ = true;
                    return false;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky.ini '{}': loaded={}", skyIniPath, skyIniLoaded);

                // Pre-decode sky textures
                if (skyData->skyLoader && skyData->skyLoader->getSkyData()) {
                    std::set<std::string> neededSet;
                    if (skyData->skyConfig && skyData->skyConfig->isLoaded()) {
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(currentZoneName_);
                        int skyTypeId = skyData->skyConfig->getSkyTypeIdForWeather(weatherType);
                        auto neededTextures = skyData->skyLoader->getTextureNamesForSkyType(skyTypeId);
                        neededSet.insert(neededTextures.begin(), neededTextures.end());
                    }
                    // T08: Load sky textures via constrained cache (replaces inline decodeBMPtoARGB + upscale)
                    const auto& textures = skyData->skyLoader->getSkyData()->textures;
                    size_t skyTexLoaded = 0;
                    size_t skyTexSkipped = 0;
                    for (const auto& [texName, texInfo] : textures) {
                        if (!texInfo || texInfo->data.empty()) continue;
                        if (!neededSet.empty() && neededSet.find(texName) == neededSet.end()) {
                            ++skyTexSkipped;
                            continue;
                        }
                        std::string lowerName = texName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        constrainedTextureCache_->getOrLoad(lowerName, texInfo->data, "sky", "loadZoneStep10_Sky");
                        loadedSkyTextureNames.push_back(texName);  // Save for upload step after skyLoader is moved
                        ++skyTexLoaded;
                    }
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: loaded {} sky textures via cache ({} skipped, {} total in archive)",
                              skyTexLoaded, skyTexSkipped, textures.size());
                }

                // Pre-compute dome mesh
                using SkyDomeMode = ConstrainedRendererConfig::SkyDomeMode;
                bool usedWldDome = false;
                if (skyDomeMode == SkyDomeMode::Original &&
                    skyData->skyLoader && skyData->skyLoader->isLoaded()) {
                    int skyTypeId = 0;
                    if (skyData->skyConfig && skyData->skyConfig->isLoaded()) {
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(currentZoneName_);
                        skyTypeId = skyData->skyConfig->getSkyTypeIdForWeather(weatherType);
                    }
                    auto layers = skyData->skyLoader->getLayersForSkyType(skyTypeId);
                    std::shared_ptr<SkyLayer> bgLayer;
                    for (const auto& layer : layers) {
                        if (layer && layer->type == SkyLayerType::Background && layer->geometry) {
                            bgLayer = layer;
                            break;
                        }
                    }
                    if (bgLayer && bgLayer->geometry && !bgLayer->geometry->vertices.empty()) {
                        const auto& geom = *bgLayer->geometry;
                        float maxExtent = 0.0f;
                        for (const auto& v : geom.vertices) {
                            float ext = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                            if (ext > maxExtent) maxExtent = ext;
                        }
                        float scale = (maxExtent > 0.001f) ? (1800.0f / maxExtent) : 1.0f;

                        auto wldDome = std::make_unique<PendingZoneComputations::SkyLoadData::PrecomputedWldDome>();
                        wldDome->vertices.resize(geom.vertices.size());
                        for (size_t i = 0; i < geom.vertices.size(); ++i) {
                            const auto& v = geom.vertices[i];
                            auto& iv = wldDome->vertices[i];
                            iv.Pos = irr::core::vector3df(v.x * scale, v.z * scale, v.y * scale);
                            iv.Normal = irr::core::vector3df(-v.nx, -v.nz, -v.ny);
                            iv.TCoords = irr::core::vector2df(v.u, v.v);
                            iv.Color = irr::video::SColor(255, 255, 255, 255);
                        }
                        wldDome->indices.reserve(geom.triangles.size() * 3);
                        for (const auto& tri : geom.triangles) {
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v1));
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v2));
                            wldDome->indices.push_back(static_cast<irr::u16>(tri.v3));
                        }
                        skyData->precomputedWldDome = std::move(wldDome);
                        usedWldDome = true;
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: precomputed WLD dome mesh: {} verts, {} indices",
                                  skyData->precomputedWldDome->vertices.size(),
                                  skyData->precomputedWldDome->indices.size());
                    } else {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: WLD dome mode requested but no background layer geometry found "
                                  "(bgLayer={}, hasGeom={}) — will use procedural dome",
                                  (bool)bgLayer, bgLayer ? (bool)bgLayer->geometry : false);
                    }
                }
                if (!usedWldDome) {
                    skyData->precomputedDome = std::make_unique<PendingZoneComputations::SkyLoadData::PrecomputedSkyDome>();
                    SkyRenderer::precomputeDomeMesh(skyData->precomputedDome->vertices,
                                                    skyData->precomputedDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: precomputed procedural dome mesh: {} verts, {} indices",
                              skyData->precomputedDome->vertices.size(),
                              skyData->precomputedDome->indices.size());
                }

                computations->skyLoadData = std::move(skyData);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky loading skipped (skyRendering=false)");
            }

            // Load weather config inline
            {
                auto weatherData = std::make_unique<PendingZoneComputations::WeatherConfigData>();
                ZoneWeatherConfig wconfig;
                if (loadZoneWeatherConfig(currentZoneName_, wconfig)) {
                    weatherData->config = wconfig;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: loaded zone weather config for '{}'", currentZoneName_);
                } else {
                    weatherData->config.zoneName = currentZoneName_;
                    weatherData->config.defaultWeather = WeatherType::Normal;
                    weatherData->config.enabled = true;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: using default weather config for '{}'", currentZoneName_);
                }
                computations->weatherConfig = std::move(weatherData);
            }
        }

        // S05: skyRenderer_ already created in initLoadingScreen() — apply zone-specific data
        bool hasBgData = computations && computations->skyLoadData && computations->skyLoadData->skyLoader;
        if (skyRenderer_ && hasBgData) {
            skyRenderer_->initializeFromPreloaded(
                std::move(computations->skyLoadData->skyLoader),
                std::move(computations->skyLoadData->skyConfig));
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applied preloaded sky data");
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: preloaded sky data not applied (skyRenderer_={}, hasBgData={})",
                      (bool)skyRenderer_, hasBgData);
        }

        if (storedZoneEnvironment_.pending) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applying stored zone env: skyType={}, zoneType={}, "
                      "fogR={}, fogG={}, fogB={}, fogMaxClip={}",
                      storedZoneEnvironment_.skyType, storedZoneEnvironment_.zoneType,
                      storedZoneEnvironment_.fogR[0], storedZoneEnvironment_.fogG[0],
                      storedZoneEnvironment_.fogB[0], storedZoneEnvironment_.fogMaxClip[0]);
            if (skyRenderer_ && skyRenderer_->isInitialized()) {
                skyRenderer_->prepareSkyType(storedZoneEnvironment_.skyType, currentZoneName_);
                bool isDungeon = (storedZoneEnvironment_.zoneType == 2);
                isIndoorZone_ = isDungeon;
                bool skySettingEnabled = getDisplaySettings().skyEnabled;
                skyRenderer_->setEnabled(!isDungeon && skySettingEnabled && config_.constrainedConfig.skyRendering);
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step10: stored zone env pending but skyRenderer not ready "
                         "(skyRenderer_={}, initialized={}) — sky type/dungeon detection skipped",
                         (bool)skyRenderer_, skyRenderer_ ? skyRenderer_->isInitialized() : false);
            }

            zoneMaxClip_ = (storedZoneEnvironment_.fogMaxClip[0] > 0.0f)
                ? storedZoneEnvironment_.fogMaxClip[0] : 99999.0f;
            setRenderDistance(userRenderDistance_);

            if (driver_ && fogEnabled_) {
                irr::video::SColor fogColor(255,
                    storedZoneEnvironment_.fogR[0], storedZoneEnvironment_.fogG[0], storedZoneEnvironment_.fogB[0]);
                float fogEnd = renderDistance_;
                float fogStart = std::max(0.0f, renderDistance_ - fogThickness_);
                driver_->setFog(fogColor, irr::video::EFT_FOG_LINEAR, fogStart, fogEnd, 0.0f, true, false);
            }

            if (weatherSystem_) {
                if (computations && computations->weatherConfig && computations->weatherConfig->loaded) {
                    weatherSystem_->setZoneConfig(computations->weatherConfig->config);
                    // P04: consumed (setZoneConfig copies) — free the wrapper
                    // now instead of at step 13 (skyLoadData precedent).
                    computations->weatherConfig.reset();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem zone config applied");
                } else {
                    weatherSystem_->setWeatherFromZone(currentZoneName_);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem fallback to zone name '{}'", currentZoneName_);
                }
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: stored zone environment not pending — sky type, dungeon detection, fog, and weather skipped");
        }

        // T08: Upload sky textures via constrained cache uploadTexture()
        // Sky textures were decoded by getOrLoad() above; now promote to GPU.
        // Uses loadedSkyTextureNames (collected before skyLoader was moved).
        if (!config_.constrainedConfig.skipSkyTextureUpload && skyRenderer_ &&
            constrainedTextureCache_ && !loadedSkyTextureNames.empty()) {
            size_t skyUploaded = 0;
            for (const auto& texName : loadedSkyTextureNames) {
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                irr::video::ITexture* tex = constrainedTextureCache_->uploadTexture(lowerName);
                if (tex && skyRenderer_) {
                    skyRenderer_->registerUploadedTexture(texName, tex);
                    ++skyUploaded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: uploaded {} sky textures via constrained cache", skyUploaded);
        }
        if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isSkyPrepared()) {
            skyRenderer_->clearSkyForRebuild();
            if (computations && computations->skyLoadData) {
                auto& skyData = *computations->skyLoadData;
                if (skyData.precomputedWldDome) {
                    skyRenderer_->createSkyDomeFromWldGeometry(
                        skyData.precomputedWldDome->vertices, skyData.precomputedWldDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from WLD geometry");
                } else if (skyData.precomputedDome) {
                    skyRenderer_->createSkyDomeFromPrecomputed(
                        skyData.precomputedDome->vertices, skyData.precomputedDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from precomputed mesh");
                } else {
                    skyRenderer_->applySkyType();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no precomputed data)");
                }
            } else {
                skyRenderer_->applySkyType();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no skyLoadData)");
            }
        }
        if (computations && computations->skyLoadData) {
            computations->skyLoadData->precomputedDome.reset();
            computations->skyLoadData->precomputedWldDome.reset();
        }
        if (skyRenderer_ && skyRenderer_->isInitialized()) {
            skyRenderer_->createCelestialBodiesOnly();
            skyRenderer_->applyInitialColors();
            skyRenderer_->consumeSkyPrepared();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky celestial bodies created, initial colors applied, sky consumed");
        }
        if (storedZoneEnvironment_.pending) storedZoneEnvironment_.pending = false;
        if (computations && computations->skyLoadData) computations->skyLoadData.reset();
        setupFog();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: complete, skyRenderer_={}, skyInit={}, skyEnabled={}, "
                  "isIndoorZone_={}, fogEnabled_={}, renderDistance_={:.0f}, weatherSystem_={}",
                  (bool)skyRenderer_,
                  skyRenderer_ ? skyRenderer_->isInitialized() : false,
                  skyRenderer_ ? skyRenderer_->isEnabled() : false,
                  isIndoorZone_, fogEnabled_, renderDistance_, (bool)weatherSystem_);
        logAssetBuildTime("seq_sky", 0, stepStart);
    }
    FlushThreadLog();
    return true;
}

void IrrlichtRenderer::loadZoneStep11_Env(ZoneLoadContext& ctx) {
    // ── Step 11: Env — environment subsystems ──────────────────────────────
    ctx.updateProgress(93, "Initializing environment...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_={}, particleManager_={}, "
                  "boidsManager_={}, tumbleweedManager_={}, weatherEffects_={}",
                  (bool)detailManager_, (bool)particleManager_,
                  (bool)boidsManager_, (bool)tumbleweedManager_, (bool)weatherEffects_);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE fire glow flags: lightingEnabled={}, icospheresEnabled={}, "
                  "maxLights={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);

        // Trees — always the GPU path now (P32: the EDT_BURNINGSVIDEO
        // CPU-instancing branch and AnimatedTreeManager were removed along
        // with RenderingBackend::Software). Trees are normal objects with
        // wind shaders applied in the fixup pass below; only the wind +
        // classification config needs (re)loading per zone.
        if (currentZone_ && !currentZone_->objects.empty()) {
            treeWindController_.loadConfig("", currentZoneName_);
            treeIdentifier_.loadZoneOverrides(currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree wind config loaded for zone '{}'", currentZoneName_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: GPU renderer path, trees handled as normal objects with wind shaders");
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree init SKIPPED: currentZone_={}, objects={}",
                      (bool)currentZone_,
                      currentZone_ ? currentZone_->objects.size() : 0);
        }

        // S05: detailManager_ already created in initLoadingScreen()
        if (detailManager_ && zoneBspTree_ && !regionWorldTriangles_.empty()) {
            // Create BSP-filtered ground raycast callback for detail/footprint systems
            // Uses pre-transformed world-space triangles with direct Möller–Trumbore intersection
            auto groundRaycast = [this](float x, float z, float startY,
                                        float& outY, irr::core::vector3df& outNormal,
                                        irr::core::triangle3df& outTriangle) -> bool {
                // Find BSP region at query point (Irrlicht x,z → EQ x,y)
                // startY is in Irrlicht Y-up coords = EQ Z
                size_t queryRegion = zoneBspTree_->findRegionIndexForPoint(x, z, startY);

                irr::core::vector3df rayStart(x, startY, z);
                irr::core::vector3df rayDir(0.0f, -1.0f, 0.0f);  // Cast down
                float rayLen = 560.0f;

                irr::core::vector3df hitPoint;
                irr::core::triangle3df hitTri;
                bool hit = false;
                float closestDistSq = std::numeric_limits<float>::max();

                // Test query region world triangles (direct, no matrix math)
                if (queryRegion != SIZE_MAX) {
                    auto it = regionWorldTriangles_.find(queryRegion);
                    if (it != regionWorldTriangles_.end()) {
                        float distSq;
                        if (rayIntersectTriangles(rayStart, rayDir, rayLen, it->second, hitPoint, hitTri, distSq)) {
                            if (distSq < closestDistSq) {
                                closestDistSq = distSq;
                                outY = hitPoint.Y;
                                outNormal = hitTri.getNormal();
                                outNormal.normalize();
                                outTriangle = hitTri;
                                hit = true;
                            }
                        }
                    }
                }

                // Test doors and objects in query region (direct Möller–Trumbore)
                if (queryRegion != SIZE_MAX) {
                    irr::core::vector3df hp;
                    irr::core::triangle3df ht;
                    float distSq;
                    if (rayTestDoorsAndObjects(rayStart, rayDir, rayLen, queryRegion, hp, ht, distSq)) {
                        if (distSq < closestDistSq) {
                            outY = hp.Y;
                            outNormal = ht.getNormal();
                            outNormal.normalize();
                            outTriangle = ht;
                            hit = true;
                        }
                    }
                }

                return hit;
            };

            auto wldLoader = currentZone_ ? currentZone_->wldLoader : nullptr;
            auto zoneGeom = currentZone_ ? currentZone_->geometry : nullptr;
            detailManager_->onZoneEnter(currentZoneName_, groundRaycast,
                                        zoneMeshNode_, wldLoader, zoneGeom);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter called for zone '{}'", currentZoneName_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter skipped (detailManager_={}, zoneBspTree_={}, regionWorldTriangles_={})",
                      (bool)detailManager_, (bool)zoneBspTree_, regionWorldTriangles_.size());
        }
        if (detailManager_ && !regionMeshNodes_.empty()) {
            size_t nodesAdded = 0;
            for (auto& [regionIdx, node] : regionMeshNodes_) {
                if (node && node->getMesh()) {
                    detailManager_->addMeshNodeForTextureLookup(node);
                    ++nodesAdded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: added {} mesh nodes for detail texture lookup (of {} total)",
                      nodesAdded, regionMeshNodes_.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_={}, regionWorldTriangles_={}, regionMeshNodes_={}",
                  (bool)detailManager_, regionWorldTriangles_.size(), regionMeshNodes_.size());

        // Particles
        if (particleManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            particleManager_->onZoneEnter(currentZoneName_, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ onZoneEnter done, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));

            // Fire sources
            std::vector<glm::vec3> fireSources;
            std::vector<float> fireRadii;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: objectLights_ count={}", objectLights_.size());
            for (const auto& objLight : objectLights_) {
                if (objLight.isFireSource) {
                    fireSources.emplace_back(objLight.position.X, objLight.position.Z, objLight.position.Y);
                    fireRadii.push_back(objLight.node ? objLight.node->getRadius() : 120.0f);
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fire sources from objectLights_: {} sources, {} radii",
                      fireSources.size(), fireRadii.size());
            if (currentZone_) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: currentZone_->lights count={}", currentZone_->lights.size());
                for (const auto& zl : currentZone_->lights) {
                    std::string upperName = zl->name;
                    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                    bool nameMatch = upperName.find("TORCH") != std::string::npos ||
                                     upperName.find("FIRE") != std::string::npos ||
                                     upperName.find("BRAZIER") != std::string::npos ||
                                     upperName.find("FLAME") != std::string::npos ||
                                     upperName.find("CANDLE") != std::string::npos;
                    bool colorMatch = (zl->r > 0.5f && zl->g > 0.2f && zl->b < 0.3f);
                    if (nameMatch || colorMatch) {
                        fireSources.emplace_back(zl->x, zl->y, zl->z);
                        fireRadii.push_back(zl->radius);
                    }
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: total fireSources={} (objectLights + zone lights)",
                      fireSources.size());
            if (!fireSources.empty()) {
                particleManager_->setFireSources(fireSources);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setFireSources count={}", fireSources.size());
            }
            if (detailManager_ && detailManager_->hasSurfaceMap()) {
                particleManager_->setSurfaceMap(detailManager_->getSurfaceMap());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setSurfaceMap from detailManager_");
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ no surface map (detailManager_={}, hasSurfaceMap={})",
                          (bool)detailManager_, detailManager_ ? detailManager_->hasSurfaceMap() : false);
            }

            particleManager_->initUnifiedRenderer();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ initUnifiedRenderer complete");
            if (!fireSources.empty()) {
                particleManager_->createFireEmitters(fireSources, fireRadii);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ createFireEmitters called, sources={}, radii={}",
                          fireSources.size(), fireRadii.size());
            }

            // Collect fire glow lights (same sources as fire particles)
            // Positions in fireSources are EQ coords (Z-up); convert to Irrlicht (Y-up)
            fireGlowLights_.clear();
            uint32_t rngState = 0x12345678u;
            for (size_t i = 0; i < fireSources.size(); ++i) {
                FireGlowLight e;
                // EQ(x,y,z) -> Irrlicht(x,z,y)
                e.position = irr::core::vector3df(fireSources[i].x, fireSources[i].z, fireSources[i].y);
                e.r = 0.15f; e.g = 0.09f; e.b = 0.02f;  // warm fire color (subtle)
                e.radius = fireRadii[i] * 0.3f;  // compact glow around fire source
                // Random flicker phase/speed per emitter
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerPhase = static_cast<float>(rngState & 0xFFFF) / 65535.0f * 6.28f;
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerSpeed = 0.8f + static_cast<float>(rngState & 0xFFFF) / 65535.0f * 0.4f;
                fireGlowLights_.push_back(e);
            }
            LOG_INFO(MOD_GRAPHICS, "Collected {} fire glow lights for zone", fireGlowLights_.size());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fireGlowLights_ collected={}", fireGlowLights_.size());
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ is NULL — skipping fire source collection entirely");
        }

        // Boids
        if (boidsManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            if (zoneBoundsValid_) {
                boidsManager_->onZoneEnter(currentZoneName_, biome,
                    glm::vec3(zoneBoundsMinX_, zoneBoundsMinY_, -1000.0f),
                    glm::vec3(zoneBoundsMaxX_, zoneBoundsMaxY_, 1000.0f));
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter with bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            } else {
                boidsManager_->onZoneEnter(currentZoneName_, biome);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter without bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            }
        }

        // Tumbleweed
        if (tumbleweedManager_) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            tumbleweedManager_->onZoneEnter(currentZoneName_, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tumbleweedManager_ onZoneEnter, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
        }

        // Weather surface + display settings
        if (weatherEffects_ && detailManager_ && detailManager_->hasSurfaceMap()) {
            weatherEffects_->setSurfaceMap(detailManager_->getSurfaceMap());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: weatherEffects_ setSurfaceMap from detailManager_");
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvironmentalDisplaySettings");
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);
        applyEnvironmentalDisplaySettings();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: POST applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                  maxFireGlowLights_, fireEffectsEnabled_);

        // Build icosphere mesh + shader for fire glow volumes
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere check: fireEffectsEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, fireGlowLights_.size()={}, icosphereVertices_.empty()={}",
                  fireEffectsEnabled_, fireGlowIcospheresEnabled_,
                  fireGlowLights_.size(), icosphereVertices_.empty());
#ifdef EQT_HAS_DRM
        if (fireEffectsEnabled_ && fireGlowIcospheresEnabled_ && !fireGlowLights_.empty() && icosphereVertices_.empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: building icosphere mesh + shader");
            buildIcosphereMesh();
            compileIcosphereShader();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build done: vertices={}, indices={}, "
                      "VBO={}, IBO={}, program={}",
                      icosphereVertices_.size(), icosphereIndices_.size(),
                      icosphereVBO_, icosphereIBO_, icosphereProgram_);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build SKIPPED");
        }
#else
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: EQT_HAS_DRM not defined, icosphere build skipped");
#endif

        // Release combined zone geometry (if detail system disabled)
        if (currentZone_ && currentZone_->geometry && !detailManager_) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: releasing combined zone geometry (detailManager_ inactive), "
                      "vertices={}, triangles={}",
                      currentZone_->geometry->vertices.size(), currentZone_->geometry->triangles.size());
            currentZone_->geometry->vertices.clear();
            currentZone_->geometry->vertices.shrink_to_fit();
            currentZone_->geometry->triangles.clear();
            currentZone_->geometry->triangles.shrink_to_fit();
        }

        // Governor reset
        if (governor_) {
            governor_->requestReset();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: governor_ reset requested");
        }

        // Fixup pass: apply tree wind shaders and animated textures to objects
        // built eagerly in Step 6 (before treeIdentifier_'s per-zone config /
        // animatedTextureManager_ existed).
        // P32: this pass always ran previously (treeManager_ was
        // unconditionally constructed in InitGraphics, so the old
        // "if (treeManager_ || animatedTextureManager_)" was always true) —
        // now unconditional, matching that exact prior behavior.
        {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup pass starting, deferredObjects_={}, "
                      "animatedTextureManager_={}, zoneShader_={}, windAvailable={}",
                      deferredObjects_.size(), (bool)animatedTextureManager_,
                      (bool)zoneShader_, zoneShader_ ? zoneShader_->isWindAvailable() : false);
            size_t fixupProcessed = 0, fixupSkipNotBuilt = 0, fixupSkipBadIndex = 0;
            size_t fixupSkipNullNode = 0, fixupSkipNoGeomPlaceable = 0;
            size_t treesFound = 0, treesWindApplied = 0, treesNullMesh = 0;
            size_t animatedApplied = 0;
            bool windCheckEnabled = zoneShader_ && zoneShader_->isWindAvailable();
            if (!windCheckEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup wind shader check DISABLED: "
                          "zoneShader_={}, windAvailable={}",
                          (bool)zoneShader_,
                          zoneShader_ ? zoneShader_->isWindAvailable() : false);
            }
            for (size_t oi = 0; oi < deferredObjects_.size(); ++oi) {
                const auto& deferred = deferredObjects_[oi];
                if (!deferred.meshBuilt || deferred.nodeIndex == SIZE_MAX) { ++fixupSkipNotBuilt; continue; }
                if (deferred.nodeIndex >= objectNodes_.size()) { ++fixupSkipBadIndex; continue; }
                if (deferred.objectIndex >= currentZone_->objects.size()) { ++fixupSkipBadIndex; continue; }

                auto* objNode = objectNodes_[deferred.nodeIndex];
                if (!objNode) { ++fixupSkipNullNode; continue; }
                const auto& objInstance = currentZone_->objects[deferred.objectIndex];
                if (!objInstance.geometry || !objInstance.placeable) { ++fixupSkipNoGeomPlaceable; continue; }

                ++fixupProcessed;
                const std::string& objName = objInstance.placeable->getName();

                // Wind shader for trees.
                // H02d (Option A): classify per-material instead of blanket-
                // assigning the ETC1-companion alpha-test variant. Masked
                // RGBA/DDS-decoded foliage (e.g. gfaydark's nekpine.bmp) has
                // no companion alpha texture, so an unconditional override
                // put it on a shader sampling an unbound unit 1 — the GLES2
                // driver's opaque-white 1x1 fallback never discards, so the
                // foliage rendered as solid opaque quads (P20b root cause).
                if (windCheckEnabled) {
                    std::string primaryTexture;
                    if (!objInstance.geometry->textureNames().empty())
                        primaryTexture = objInstance.geometry->textureNames()[0];
                    bool isTree = treeIdentifier_.isTreeMesh(objName, primaryTexture);
                    if (isTree) {
                        ++treesFound;
                        irr::scene::IMesh* treeMesh = objNode->getMesh();
                        if (treeMesh) {
                            irr::core::aabbox3df meshBbox = treeMesh->getBoundingBox();
                            const auto& textureNames = objInstance.geometry->textureNames();
                            const auto& textureMasked = objInstance.geometry->textureMasked();
                            // Classify one texture (by name) into the correct wind
                            // material type: masked+ETC1 -> companion-alpha wind
                            // variant; masked+RGBA -> wind variant reading
                            // uTexture.a; non-masked -> wind/solid variant.
                            auto classify = [&](const std::string& texName) -> irr::s32 {
                                bool isMaskedMaterial = false;
                                for (size_t ti = 0; ti < textureNames.size(); ++ti) {
                                    if (textureNames[ti] == texName) {
                                        isMaskedMaterial = (ti < textureMasked.size()) && textureMasked[ti];
                                        break;
                                    }
                                }
                                if (!isMaskedMaterial) {
                                    irr::s32 windSolidType = zoneShader_->getActiveWindSolid();
                                    return windSolidType >= 0 ? windSolidType : zoneShader_->getActiveWindAlphaTest();
                                }
                                bool isETC1 = constrainedTextureCache_ && !texName.empty() &&
                                    constrainedTextureCache_->isETC1Texture(texName);
                                if (isETC1) {
                                    return zoneShader_->getActiveWindAlphaTest();
                                }
                                irr::s32 windRGBAType = zoneShader_->getActiveWindAlphaTestRGBA();
                                return windRGBAType >= 0 ? windRGBAType : zoneShader_->getActiveWindAlphaTest();
                            };
                            // Update BOTH node materials AND mesh buffer materials.
                            // Node materials are used by Irrlicht's scene graph rendering.
                            // Mesh buffer materials are used by drawZoneGeometrySorted().
                            for (irr::u32 mi = 0; mi < treeMesh->getMeshBufferCount(); ++mi) {
                                auto& bufMat = treeMesh->getMeshBuffer(mi)->getMaterial();
                                std::string texName = bufMat.TextureLayer[0].Texture
                                    ? std::string(bufMat.TextureLayer[0].Texture->getName().getPath().c_str())
                                    : primaryTexture;
                                irr::s32 windMatType = classify(texName);
                                bufMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(windMatType);
                                bufMat.MaterialTypeParam = meshBbox.MinEdge.Y;
                                bufMat.MaterialTypeParam2 = meshBbox.MaxEdge.Y;
                                bufMat.BackfaceCulling = false;
                                if (constrainedTextureCache_ && bufMat.TextureLayer[0].Texture && !bufMat.TextureLayer[1].Texture) {
                                    irr::core::stringc texNameStr = bufMat.TextureLayer[0].Texture->getName().getPath();
                                    irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(
                                        std::string(texNameStr.c_str()));
                                    if (alphaTex) bufMat.setTexture(1, alphaTex);
                                }
                            }
                            for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                                auto& mat = objNode->getMaterial(mi);
                                irr::s32 prevMatType = mat.MaterialType;
                                irr::video::ITexture* tex0 = mat.TextureLayer[0].Texture;
                                std::string texName = tex0
                                    ? std::string(tex0->getName().getPath().c_str())
                                    : primaryTexture;
                                irr::s32 windMatType = classify(texName);
                                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(windMatType);
                                mat.MaterialTypeParam = meshBbox.MinEdge.Y;
                                mat.MaterialTypeParam2 = meshBbox.MaxEdge.Y;
                                mat.BackfaceCulling = false;
                                // Ensure ETC1 alpha companion is on layer 1 (may already be set by mesh builder)
                                if (constrainedTextureCache_ && tex0 && !mat.TextureLayer[1].Texture) {
                                    // Look up alpha companion by texture name
                                    irr::core::stringc texNameStr = tex0->getName().getPath();
                                    irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(
                                        std::string(texNameStr.c_str()));
                                    if (alphaTex) mat.setTexture(1, alphaTex);
                                }
                                LOG_DEBUG(MOD_SHADER, "fixup tree '{}' mat[{}]: prevType={} -> windType={} "
                                          "tex0={} alphaTex1={} bboxY=[{:.2f},{:.2f}]",
                                          objName, mi, prevMatType, windMatType,
                                          (void*)tex0, (void*)mat.TextureLayer[1].Texture,
                                          meshBbox.MinEdge.Y, meshBbox.MaxEdge.Y);
                            }
                            ++treesWindApplied;
                        } else {
                            ++treesNullMesh;
                            LOG_WARN(MOD_GRAPHICS, "SEQ Step11: tree '{}' identified but getMesh() returned null, "
                                     "wind shader NOT applied", objName);
                        }
                    }
                }

                // Animated textures
                if (animatedTextureManager_ && objInstance.geometry) {
                    irr::scene::IMesh* objMesh = objNode->getMesh();
                    if (objMesh) {
                        animatedTextureManager_->addMesh(*objInstance.geometry, currentZone_->objectTextures, objMesh);
                        animatedTextureManager_->addSceneNode(objNode);
                        ++animatedApplied;
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: fixup pass: processed={}/{} objects, trees found={}, "
                     "wind applied={}, null mesh={}, animated={}, skipped: notBuilt={}, badIndex={}, "
                     "nullNode={}, noGeomPlaceable={}",
                     fixupProcessed, deferredObjects_.size(), treesFound, treesWindApplied,
                     treesNullMesh, animatedApplied, fixupSkipNotBuilt, fixupSkipBadIndex,
                     fixupSkipNullNode, fixupSkipNoGeomPlaceable);
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: complete");
        logAssetBuildTime("seq_env", 0, stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep12_Lights(ZoneLoadContext& ctx) {
    // ── Step 12: Lights — zone lighting ────────────────────────────────────
    ctx.updateProgress(96, "Setting up lighting...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: currentZone_={}, lights count={}",
                  (bool)currentZone_,
                  currentZone_ ? currentZone_->lights.size() : 0);
        zoneLightData_.clear();
        zoneLightPositions_.clear();
        zoneLightRegions_.clear();
        if (currentZone_ && !currentZone_->lights.empty()) {
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                irr::core::vector3df pos(light->x, light->z, light->y);
                ZoneLightData zld;
                zld.radius = std::max(light->radius, 1.0f);
                zld.attConstant = 1.0f;
                zld.attLinear = 1.0f / zld.radius;
                zld.attQuadratic = 1.0f / (zld.radius * zld.radius);
                zld.baseDiffuseColor = irr::video::SColorf(light->r, light->g, light->b, 1.0f);
                zoneLightData_.push_back(zld);
                zoneLightPositions_.push_back(pos);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}]: pos=({:.1f},{:.1f},{:.1f}) radius={:.1f} "
                          "color=({:.2f},{:.2f},{:.2f}) attLin={:.4f} attQuad={:.6f}",
                          i, light->x, light->y, light->z, zld.radius,
                          light->r, light->g, light->b, zld.attLinear, zld.attQuadratic);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: no lights to process (currentZone_={}, lights={})",
                      (bool)currentZone_, currentZone_ ? currentZone_->lights.size() : 0);
        }

        // Install light BSP regions
        if (pendingZoneComputations_ && !pendingZoneComputations_->zoneLightRegions.empty()) {
            zoneLightRegions_ = std::move(pendingZoneComputations_->zoneLightRegions);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions from precomputed (count={})", zoneLightRegions_.size());
        } else if (zoneBspTree_ && !zoneBspTree_->regions.empty() && currentZone_) {
            for (size_t i = 0; i < currentZone_->lights.size(); ++i) {
                const auto& light = currentZone_->lights[i];
                size_t regionIdx = zoneBspTree_->findRegionIndexForPoint(light->x, light->y, light->z);
                zoneLightRegions_.push_back(regionIdx);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}] BSP region={} pos=({:.1f},{:.1f},{:.1f})",
                          i, regionIdx == SIZE_MAX ? -1 : (int)regionIdx,
                          light->x, light->y, light->z);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions computed via findRegionIndexForPoint (count={})", zoneLightRegions_.size());
        } else {
            zoneLightRegions_.resize(zoneLightData_.size(), SIZE_MAX);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions fallback SIZE_MAX (no BSP), count={}", zoneLightRegions_.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: zoneLightData_={}, zoneLightPositions_={}, zoneLightRegions_={}",
                  zoneLightData_.size(), zoneLightPositions_.size(), zoneLightRegions_.size());
        logAssetBuildTime("seq_lights", zoneLightData_.size(), stepStart);
    }
    FlushThreadLog();
}

void IrrlichtRenderer::loadZoneStep12b_Simulation(ZoneLoadContext& ctx) {
    // ── Step 12b: SimulationWorker first-frame computation ──────────────────
    // Create SimulationWorker, set zone data, and run one synchronous computation
    // so the first rendered frame after loading has full PVS culling, sorted draw
    // list, lighting, weather, particles, etc. — no visual pop-in.
    ctx.updateProgress(99, "Computing scene...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: simulationWorker_={}, frustumCuller_={}, camera_={}",
                  (bool)simulationWorker_, (bool)frustumCuller_, (bool)camera_);

        // S05: simulationWorker_ already created in initLoadingScreen()

        // Build and register zone data
        SimulationZoneData zoneData = buildSimulationZoneData();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: buildSimulationZoneData: regions={}, objects={}, zoneLights={}",
                  zoneData.regionBounds.size(), zoneData.objects.size(), zoneData.zoneLights.size());
        simulationWorker_->setZoneData(zoneData);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: setZoneData installed on SimulationWorker");

        // Update frustum culler with current camera so frustum planes are valid
        if (frustumCuller_ && camera_) {
            irr::core::vector3df irrFwd = (camera_->getTarget() - camera_->getPosition());
            float eqFwdX = irrFwd.X;
            float eqFwdY = irrFwd.Z;
            float eqFwdZ = irrFwd.Y;
            float fovV = camera_->getFOV();
            auto screenSize = driver_->getScreenSize();
            float aspect = (float)screenSize.Width / (float)screenSize.Height;
            float camX, camY, camZ;
            cameraController_->getPositionEQ(camX, camY, camZ);
            frustumCuller_->update(camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ,
                fovV, aspect, 1.0f, renderDistance_);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: frustumCuller updated, fov={:.2f}, aspect={:.2f}, renderDist={:.0f}",
                      fovV, aspect, renderDistance_);
        }

        // Build SimulationInput from current state
        SimulationInput input;

        // Camera (Irrlicht Y-up)
        if (camera_) {
            input.cameraPos = camera_->getPosition();
            input.cameraTarget = camera_->getTarget();
        }
        if (cameraController_) {
            cameraController_->getPositionEQ(input.camEqX, input.camEqY, input.camEqZ);
        }

        // Frustum planes (EQ Z-up)
        if (frustumCuller_ && frustumCuller_->isEnabled()) {
            for (int i = 0; i < 6; ++i) {
                const float* plane = frustumCuller_->getPlane(i);
                input.frustumPlanes[i][0] = plane[0];
                input.frustumPlanes[i][1] = plane[1];
                input.frustumPlanes[i][2] = plane[2];
                input.frustumPlanes[i][3] = plane[3];
            }
            input.frustumValid = true;

            // Occlusion camera
            auto& oc = input.occlusionCamera;
            oc.fwdX = frustumCuller_->getFwdX();
            oc.fwdY = frustumCuller_->getFwdY();
            oc.fwdZ = frustumCuller_->getFwdZ();
            oc.rightX = frustumCuller_->getRightX();
            oc.rightY = frustumCuller_->getRightY();
            oc.rightZ = 0;
            oc.upX = frustumCuller_->getUpX();
            oc.upY = frustumCuller_->getUpY();
            oc.upZ = frustumCuller_->getUpZ();
            oc.fovRadV = frustumCuller_->getFov();
            oc.aspect = frustumCuller_->getAspect();
            oc.enabled = true;
        }

        input.renderDistance = renderDistance_;
        input.loadingActive = false;  // Full culling, not loading-mode

        // Player (EQ Z-up)
        input.playerX = playerX_;
        input.playerY = playerY_;
        input.playerZ = playerZ_;
        input.playerHeading = playerHeading_;

        // Timing (first frame)
        input.deltaTime = 1.0f / 30.0f;
        input.frameNumber = frameNumber_;

        // Environment
        input.currentHour = currentHour_;
        input.currentMinute = currentMinute_;
        input.timeOfDay = currentHour_ + currentMinute_ / 60.0f;

        // Player light
        input.playerLightLevel = playerLightLevel_;

        // Vision and weather modifiers
        input.visionType = static_cast<uint8_t>(currentVision_);
        if (weatherEffects_ && weatherEffects_->isEnabled()) {
            input.weatherAmbientModifier = weatherEffects_->getAmbientLightModifier();
        }

        // Entity snapshots
        if (entityRenderer_) {
            const auto& entities = entityRenderer_->getEntities();
            input.entitySnapshots.reserve(entities.size());
            for (const auto& [spawnId, visual] : entities) {
                if (!visual.sceneNode) continue;
                SimulationInput::EntitySnapshot snap;
                snap.spawnId = spawnId;
                snap.lastX = visual.lastX;
                snap.lastY = visual.lastY;
                snap.lastZ = visual.lastZ;
                snap.serverX = visual.serverX;
                snap.serverY = visual.serverY;
                snap.serverZ = visual.serverZ;
                snap.serverHeading = visual.serverHeading;
                snap.timeSinceUpdate = visual.timeSinceUpdate;
                snap.lastUpdateInterval = visual.lastUpdateInterval;
                snap.collisionZOffset = visual.collisionZOffset;
                snap.modelYOffset = visual.modelYOffset;
                snap.serverAnimation = visual.serverAnimation;
                snap.lastNonZeroAnimation = visual.lastNonZeroAnimation;
                snap.cachedBspRegion = visual.cachedBspRegion;
                snap.bspRegionDirty = visual.bspRegionDirty;
                snap.isNPC = visual.isNPC;
                snap.isPlayer = visual.isPlayer;
                snap.isCorpse = visual.isCorpse;
                snap.isFading = visual.isFading;
                snap.inSceneGraph = visual.inSceneGraph;
                snap.hasVelocity = false;
                input.entitySnapshots.push_back(snap);
            }
            input.entityRenderDistance = config_.constrainedConfig.entityRenderDistance;
            input.maxVisibleEntities = config_.constrainedConfig.maxVisibleEntities;
            input.entityCullingEnabled = true;
            input.nameTagDistance = entityRenderer_->getNameTagDistance();
            input.nameTagsVisible = entityRenderer_->areNameTagsVisible();
        }

        // Sky state
        if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
            input.skyEnabled = true;
            input.skyInitialized = true;
            input.skyCloudScrollOffset = skyRenderer_->getCloudScrollOffset();
        }

        // Weather effects state
        if (weatherEffects_ && weatherEffects_->isEnabled()) {
            input.weatherEnabled = true;
            input.weatherTransitionProgress = weatherEffects_->getTransitionProgress();
            input.weatherTransitionDuration = weatherEffects_->getTransitionDuration();
            input.weatherCurrentDarkening = weatherEffects_->getCurrentDarkening();
            input.weatherTargetDarkening = weatherEffects_->getTargetDarkening();
            input.weatherLightningFlashTimer = weatherEffects_->getLightningFlashTimer();
            input.weatherLightningBoltTimer = weatherEffects_->getLightningBoltTimer();
            input.weatherLightningTimer = weatherEffects_->getLightningTimer();
            input.weatherLightningActive = weatherEffects_->isLightningActive();
            input.weatherLightningEnabled = weatherEffects_->isLightningEnabled();
            input.weatherType = weatherEffects_->getCurrentType();
            input.weatherIntensity = weatherEffects_->getCurrentIntensity();
        }

        // Particle system input
        if (particleManager_) {
            auto& pi = input.particleInput;
            pi.deltaTime = input.deltaTime;
            if (camera_) {
                auto cp = camera_->getAbsolutePosition();
                pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
            }
            if (zoneShader_) {
                const float* amb = zoneShader_->ambientColor();
                pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
            }
            pi.windDirection = particleManager_->getWindDirection();
            pi.windStrength = particleManager_->getWindStrength();
            pi.fireEnabled = particleManager_->isUnifiedFireEnabled();
            pi.commands = particleManager_->drainCommands();
            pi.unifiedRendererInitialized = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: particleInput assembled: fireEnabled={}, "
                      "unifiedRendererInitialized={}, commands={}, windStrength={:.2f}",
                      pi.fireEnabled, pi.unifiedRendererInitialized,
                      pi.commands.size(), pi.windStrength);
        }

        // Boids system input
        if (boidsManager_ && boidsManager_->isEnabled()) {
            auto& bi = input.boidsInput;
            bi.deltaTime = input.deltaTime;
            bi.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
            bi.playerHeading = playerHeading_;
            bi.timeOfDay = input.timeOfDay;
            bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            bi.initialized = true;
        }

        // Tumbleweed system input
        if (tumbleweedManager_ && tumbleweedManager_->isEnabled()) {
            auto& ti = input.tumbleweedInput;
            ti.deltaTime = input.deltaTime;
            ti.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
            ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            ti.initialized = true;
        }

        // Weather system input
        if (weatherSystem_) {
            auto& wi = input.weatherInput;
            wi.deltaTime = input.deltaTime;
            wi.initialized = true;
        }

        // Detail wind/disturbance input
        if (detailManager_ && detailManager_->isEnabled()) {
            auto& dti = input.detailInput;
            dti.deltaTime = input.deltaTime;
            const auto& wc = detailManager_->getWindController();
            const auto& wcParams = wc.getParams();
            dti.windStrength = detailManager_->getZoneConfig().windStrength;
            dti.windFrequency = wcParams.frequency;
            dti.gustFrequency = wcParams.gustFrequency;
            dti.gustStrength = wcParams.gustStrength;
            dti.windDirX = wcParams.direction.X;
            dti.windDirY = wcParams.direction.Y;
            dti.playerPosX = playerX_;
            dti.playerPosY = playerZ_;
            dti.playerPosZ = playerY_;
            dti.initialized = true;
        }

        // Run synchronous computation
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationInput: entitySnapshots={}, frustumValid={}, "
                  "particleInput.fireEnabled={}, skyEnabled={}, weatherEnabled={}, renderDist={:.0f}",
                  input.entitySnapshots.size(), input.frustumValid,
                  input.particleInput.fireEnabled, input.skyEnabled,
                  input.weatherEnabled, input.renderDistance);
        SimulationOutput output;
        simulationWorker_->computeOnce(input, output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationOutput: regions={}, objects={}, lights={}, entities={}",
                  output.regionVisible.size(), output.objectVisible.size(),
                  output.lightVisible.size(), output.entityResults.size());

        // Apply results to renderer state
        applySimulationOutput(output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: applySimulationOutput applied, currentPvsRegion_={}", currentPvsRegion_);

        // CRITICAL: applySimulationOutput sets particleRenderBuffer_ to point
        // into output.particleOutput.renderBuffer. But 'output' is a local
        // variable that will be destroyed when this scope ends, leaving a
        // dangling pointer. Null it now — the runtime SimulationWorker will
        // set it correctly from its persistent double-buffered output.
        particleRenderBuffer_ = nullptr;

        // Update door PVS visibility now that currentPvsRegion_ is set
        if (doorManager_) {
            doorManager_->setPvsRegion(currentPvsRegion_);
            doorManager_->update(0.0f);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: doorManager_ PVS region set to {}, initial update done", currentPvsRegion_);
        }

        LOG_INFO(MOD_GRAPHICS, "Sequential: SimulationWorker first-frame computed and applied "
                 "(regions={}, objects={}, lights={}, entities={})",
                 output.regionVisible.size(), output.objectVisible.size(),
                 output.lightVisible.size(), output.entityResults.size());
        logAssetBuildTime("seq_simulation", 0, stepStart);
        FlushThreadLog();

        // Set up BSP-based camera collision now that PVS region is known
        if (cameraController_ && zoneBspTree_) {
            cameraController_->setBspCollision(zoneBspTree_.get(), &regionBoundingBoxes_);
            cameraController_->setBspPlayerRegion(currentPvsRegion_);
            LOG_INFO(MOD_GRAPHICS, "Camera BSP collision initialized for region {}", currentPvsRegion_);
        }
    }
}

void IrrlichtRenderer::loadZoneStep13_Cleanup(ZoneLoadContext& ctx) {
    // ── Step 13: Cleanup ───────────────────────────────────────────────────
    ctx.updateProgress(100, "Finalizing...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: releaseTextureDataAfterUpload={}, currentZone_={}, "
                  "pendingZoneComputations_={}",
                  config_.constrainedConfig.releaseTextureDataAfterUpload,
                  (bool)currentZone_, (bool)pendingZoneComputations_);
        if (config_.constrainedConfig.releaseTextureDataAfterUpload && currentZone_) {
            size_t freed = currentZone_->releaseTexturePixelData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released {:.1f}MB texture pixel data", freed / (1024.0f * 1024.0f));
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: texture pixel data retained (releaseTextureDataAfterUpload={}, currentZone_={})",
                      config_.constrainedConfig.releaseTextureDataAfterUpload, (bool)currentZone_);
        }
        if (currentZone_) {
            currentZone_->clearCharacterData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released zone character data");
        }
        if (currentZone_ && currentZone_->wldLoader) {
            currentZone_->wldLoader->releasePostLoadData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released WLD post-load data");
        } else if (currentZone_ && !currentZone_->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: wldLoader is null, skipping releasePostLoadData");
        }
        pendingZoneComputations_.reset();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: pendingZoneComputations_ released");
        logAssetBuildTime("seq_cleanup", 0, stepStart);
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
        drawLoadingScreen(percent / 100.0f, wtext);
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
    stopSimulationWorker();
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
        size_t meshBytes = constrainedMeshCache_ ? constrainedMeshCache_->getCurrentUsage() : 0;
        logLoadMemAuditStep(stepName, getEQE1MappedBytesTotal(), texBytes, meshBytes, 0);
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

    // P13: dump the texture upload/eviction ledger at the zone-in-complete
    // marker (no-op unless --texture-ledger was passed — see
    // ConstrainedRendererConfig::textureUploadLedgerPath).
    if (constrainedTextureCache_ && constrainedTextureCache_->isLedgerEnabled()) {
        constrainedTextureCache_->dumpLedger(config_.constrainedConfig.textureUploadLedgerPath,
                                              currentZoneName_);
    }

    dumpScene();
    FlushThreadLog();
}

} // namespace Graphics
} // namespace EQT
