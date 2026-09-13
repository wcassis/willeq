// Zone-load steps 10 (Sky), 11 (Env), 12 (Lights), 12b (Simulation) and
// 13 (Cleanup) -- everything after the world and its entities exist.
//
// Split out of renderer_zone_load.cpp by T166; see that file's header.

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
#include "client/graphics/zone_load/zone_load_component.h"
#include "client/graphics/zone_load/zone_load_context.h"

namespace EQT {
namespace Graphics {

bool ZoneLoadComponent::loadZoneStep10_Sky(ZoneLoadContext& ctx) {
    // ── Step 10: Sky — all sky, fog, weather ───────────────────────────────
    ctx.updateProgress(90, "Loading sky and weather...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        auto* computations = pendingZoneComputations_.get();
        bool skyRendering = ctx_.config.constrainedConfig.skyRendering;
        auto skyDomeMode = ctx_.config.constrainedConfig.skyDomeMode;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: computations={}, skyRendering={}, skyDomeMode={}, "
                  "storedZoneEnv.pending={}, skyRenderer_={}, weatherSystem_={}",
                  (bool)computations, skyRendering, (int)skyDomeMode,
                  ctx_.ZoneLifecycle().storedZoneEnvironment().pending, (bool)ctx_.skyRenderer, (bool)ctx_.weatherSystem);

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
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(ctx_.currentZoneName);
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
                        ctx_.constrainedTextureCache->getOrLoad(lowerName, texInfo->data, "sky", "loadZoneStep10_Sky");
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
                        std::string weatherType = skyData->skyConfig->getWeatherTypeForZone(ctx_.currentZoneName);
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
                if (loadZoneWeatherConfig(ctx_.currentZoneName, wconfig)) {
                    weatherData->config = wconfig;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: loaded zone weather config for '{}'", ctx_.currentZoneName);
                } else {
                    weatherData->config.zoneName = ctx_.currentZoneName;
                    weatherData->config.defaultWeather = WeatherType::Normal;
                    weatherData->config.enabled = true;
                    weatherData->loaded = true;
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: using default weather config for '{}'", ctx_.currentZoneName);
                }
                computations->weatherConfig = std::move(weatherData);
            }
        }

        // S05: skyRenderer_ already created in initLoadingScreen() — apply zone-specific data
        bool hasBgData = computations && computations->skyLoadData && computations->skyLoadData->skyLoader;
        if (ctx_.skyRenderer && hasBgData) {
            ctx_.skyRenderer->initializeFromPreloaded(
                std::move(computations->skyLoadData->skyLoader),
                std::move(computations->skyLoadData->skyConfig));
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applied preloaded sky data");
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: preloaded sky data not applied (skyRenderer_={}, hasBgData={})",
                      (bool)ctx_.skyRenderer, hasBgData);
        }

        if (ctx_.ZoneLifecycle().storedZoneEnvironment().pending) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: applying stored zone env: skyType={}, zoneType={}, "
                      "fogR={}, fogG={}, fogB={}, fogMaxClip={}",
                      ctx_.ZoneLifecycle().storedZoneEnvironment().skyType, ctx_.ZoneLifecycle().storedZoneEnvironment().zoneType,
                      ctx_.ZoneLifecycle().storedZoneEnvironment().fogR[0], ctx_.ZoneLifecycle().storedZoneEnvironment().fogG[0],
                      ctx_.ZoneLifecycle().storedZoneEnvironment().fogB[0], ctx_.ZoneLifecycle().storedZoneEnvironment().fogMaxClip[0]);
            if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized()) {
                ctx_.skyRenderer->prepareSkyType(ctx_.ZoneLifecycle().storedZoneEnvironment().skyType, ctx_.currentZoneName);
                bool isDungeon = (ctx_.ZoneLifecycle().storedZoneEnvironment().zoneType == 2);
                ctx_.isIndoorZone = isDungeon;
                bool skySettingEnabled = owner_.getDisplaySettings().skyEnabled;
                ctx_.skyRenderer->setEnabled(!isDungeon && skySettingEnabled && ctx_.config.constrainedConfig.skyRendering);
            } else {
                LOG_WARN(MOD_GRAPHICS, "SEQ Step10: stored zone env pending but skyRenderer not ready "
                         "(skyRenderer_={}, initialized={}) — sky type/dungeon detection skipped",
                         (bool)ctx_.skyRenderer, ctx_.skyRenderer ? ctx_.skyRenderer->isInitialized() : false);
            }

            ctx_.ZoneLifecycle().zoneMaxClip() = (ctx_.ZoneLifecycle().storedZoneEnvironment().fogMaxClip[0] > 0.0f)
                ? ctx_.ZoneLifecycle().storedZoneEnvironment().fogMaxClip[0] : 99999.0f;
            owner_.setRenderDistance(ctx_.ZoneLifecycle().userRenderDistance());

            if (ctx_.driver && ctx_.fogEnabled) {
                irr::video::SColor fogColor(255,
                    ctx_.ZoneLifecycle().storedZoneEnvironment().fogR[0], ctx_.ZoneLifecycle().storedZoneEnvironment().fogG[0], ctx_.ZoneLifecycle().storedZoneEnvironment().fogB[0]);
                float fogEnd = ctx_.renderDistance;
                float fogStart = std::max(0.0f, ctx_.renderDistance - ctx_.fogThickness);
                ctx_.driver->setFog(fogColor, irr::video::EFT_FOG_LINEAR, fogStart, fogEnd, 0.0f, true, false);
            }

            if (ctx_.weatherSystem) {
                if (computations && computations->weatherConfig && computations->weatherConfig->loaded) {
                    ctx_.weatherSystem->setZoneConfig(computations->weatherConfig->config);
                    // P04: consumed (setZoneConfig copies) — free the wrapper
                    // now instead of at step 13 (skyLoadData precedent).
                    computations->weatherConfig.reset();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem zone config applied");
                } else {
                    ctx_.weatherSystem->setWeatherFromZone(ctx_.currentZoneName);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: weatherSystem fallback to zone name '{}'", ctx_.currentZoneName);
                }
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: stored zone environment not pending — sky type, dungeon detection, fog, and weather skipped");
        }

        // T08: Upload sky textures via constrained cache uploadTexture()
        // Sky textures were decoded by getOrLoad() above; now promote to GPU.
        // Uses loadedSkyTextureNames (collected before skyLoader was moved).
        if (!ctx_.config.constrainedConfig.skipSkyTextureUpload && ctx_.skyRenderer &&
            ctx_.constrainedTextureCache && !loadedSkyTextureNames.empty()) {
            size_t skyUploaded = 0;
            for (const auto& texName : loadedSkyTextureNames) {
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                irr::video::ITexture* tex = ctx_.constrainedTextureCache->uploadTexture(lowerName);
                if (tex && ctx_.skyRenderer) {
                    ctx_.skyRenderer->registerUploadedTexture(texName, tex);
                    ++skyUploaded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: uploaded {} sky textures via constrained cache", skyUploaded);
        }
        if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized() && ctx_.skyRenderer->isSkyPrepared()) {
            ctx_.skyRenderer->clearSkyForRebuild();
            if (computations && computations->skyLoadData) {
                auto& skyData = *computations->skyLoadData;
                if (skyData.precomputedWldDome) {
                    ctx_.skyRenderer->createSkyDomeFromWldGeometry(
                        skyData.precomputedWldDome->vertices, skyData.precomputedWldDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from WLD geometry");
                } else if (skyData.precomputedDome) {
                    ctx_.skyRenderer->createSkyDomeFromPrecomputed(
                        skyData.precomputedDome->vertices, skyData.precomputedDome->indices);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized from precomputed mesh");
                } else {
                    ctx_.skyRenderer->applySkyType();
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no precomputed data)");
                }
            } else {
                ctx_.skyRenderer->applySkyType();
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky dome finalized via applySkyType (no skyLoadData)");
            }
        }
        if (computations && computations->skyLoadData) {
            computations->skyLoadData->precomputedDome.reset();
            computations->skyLoadData->precomputedWldDome.reset();
        }
        if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized()) {
            ctx_.skyRenderer->createCelestialBodiesOnly();
            ctx_.skyRenderer->applyInitialColors();
            ctx_.skyRenderer->consumeSkyPrepared();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: sky celestial bodies created, initial colors applied, sky consumed");
        }
        if (ctx_.ZoneLifecycle().storedZoneEnvironment().pending) ctx_.ZoneLifecycle().storedZoneEnvironment().pending = false;
        if (computations && computations->skyLoadData) computations->skyLoadData.reset();
        ctx_.Overlays().setupFog();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step10: complete, skyRenderer_={}, skyInit={}, skyEnabled={}, "
                  "isIndoorZone_={}, fogEnabled_={}, renderDistance_={:.0f}, weatherSystem_={}",
                  (bool)ctx_.skyRenderer,
                  ctx_.skyRenderer ? ctx_.skyRenderer->isInitialized() : false,
                  ctx_.skyRenderer ? ctx_.skyRenderer->isEnabled() : false,
                  ctx_.isIndoorZone, ctx_.fogEnabled, ctx_.renderDistance, (bool)ctx_.weatherSystem);
        ctx_.ZoneStream().logAssetBuildTime("seq_sky", 0, stepStart);
    }
    FlushThreadLog();
    return true;
}

void ZoneLoadComponent::loadZoneStep11_Env(ZoneLoadContext& ctx) {
    // ── Step 11: Env — environment subsystems ──────────────────────────────
    ctx.updateProgress(93, "Initializing environment...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_={}, particleManager_={}, "
                  "boidsManager_={}, tumbleweedManager_={}, weatherEffects_={}",
                  (bool)ctx_.detailManager, (bool)ctx_.particleManager,
                  (bool)ctx_.boidsManager, (bool)ctx_.tumbleweedManager, (bool)ctx_.weatherEffects);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE fire glow flags: lightingEnabled={}, icospheresEnabled={}, "
                  "maxLights={}, fireEffectsEnabled_={}",
                  ctx_.fireGlowLightingEnabled, ctx_.fireGlowIcospheresEnabled,
                  ctx_.maxFireGlowLights, ctx_.fireEffectsEnabled);

        // Trees — always the GPU path now (P32: the EDT_BURNINGSVIDEO
        // CPU-instancing branch and AnimatedTreeManager were removed along
        // with RenderingBackend::Software). Trees are normal objects with
        // wind shaders applied in the fixup pass below; only the wind +
        // classification config needs (re)loading per zone.
        if (ctx_.currentZone && !ctx_.currentZone->objects.empty()) {
            ctx_.treeWindController.loadConfig("", ctx_.currentZoneName);
            treeIdentifier_.loadZoneOverrides(ctx_.currentZoneName);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree wind config loaded for zone '{}'", ctx_.currentZoneName);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: GPU renderer path, trees handled as normal objects with wind shaders");
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tree init SKIPPED: currentZone_={}, objects={}",
                      (bool)ctx_.currentZone,
                      ctx_.currentZone ? ctx_.currentZone->objects.size() : 0);
        }

        // S05: detailManager_ already created in initLoadingScreen()
        if (ctx_.detailManager && ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()) {
            // Create BSP-filtered ground raycast callback for detail/footprint systems
            // Uses pre-transformed world-space triangles with direct Möller–Trumbore intersection
            auto groundRaycast = [this](float x, float z, float startY,
                                        float& outY, irr::core::vector3df& outNormal,
                                        irr::core::triangle3df& outTriangle) -> bool {
                // Find BSP region at query point (Irrlicht x,z → EQ x,y)
                // startY is in Irrlicht Y-up coords = EQ Z
                size_t queryRegion = ctx_.zoneBspTree->findRegionIndexForPoint(x, z, startY);

                irr::core::vector3df rayStart(x, startY, z);
                irr::core::vector3df rayDir(0.0f, -1.0f, 0.0f);  // Cast down
                float rayLen = 560.0f;

                irr::core::vector3df hitPoint;
                irr::core::triangle3df hitTri;
                bool hit = false;
                float closestDistSq = std::numeric_limits<float>::max();

                // Test query region world triangles (direct, no matrix math)
                if (queryRegion != SIZE_MAX) {
                    auto it = ctx_.regionWorldTriangles.find(queryRegion);
                    if (it != ctx_.regionWorldTriangles.end()) {
                        float distSq;
                        if (PlayerCameraComponent::rayIntersectTriangles(rayStart, rayDir, rayLen, it->second, hitPoint, hitTri, distSq)) {
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
                    if (ctx_.PlayerCamera().rayTestDoorsAndObjects(rayStart, rayDir, rayLen, queryRegion, hp, ht, distSq)) {
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

            auto wldLoader = ctx_.currentZone ? ctx_.currentZone->wldLoader : nullptr;
            auto zoneGeom = ctx_.currentZone ? ctx_.currentZone->geometry : nullptr;
            ctx_.detailManager->onZoneEnter(ctx_.currentZoneName, groundRaycast,
                                        ctx_.zoneMeshNode, wldLoader, zoneGeom);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter called for zone '{}'", ctx_.currentZoneName);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_ onZoneEnter skipped (detailManager_={}, zoneBspTree_={}, regionWorldTriangles_={})",
                      (bool)ctx_.detailManager, (bool)ctx_.zoneBspTree, ctx_.regionWorldTriangles.size());
        }
        if (ctx_.detailManager && !ctx_.regionMeshNodes.empty()) {
            size_t nodesAdded = 0;
            for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
                if (node && node->getMesh()) {
                    ctx_.detailManager->addMeshNodeForTextureLookup(node);
                    ++nodesAdded;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: added {} mesh nodes for detail texture lookup (of {} total)",
                      nodesAdded, ctx_.regionMeshNodes.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: detailManager_={}, regionWorldTriangles_={}, regionMeshNodes_={}",
                  (bool)ctx_.detailManager, ctx_.regionWorldTriangles.size(), ctx_.regionMeshNodes.size());

        // Particles
        if (ctx_.particleManager) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(ctx_.currentZoneName);
            ctx_.particleManager->onZoneEnter(ctx_.currentZoneName, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ onZoneEnter done, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));

            // Fire sources
            std::vector<glm::vec3> fireSources;
            std::vector<float> fireRadii;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: objectLights_ count={}", ctx_.objectLights.size());
            for (const auto& objLight : ctx_.objectLights) {
                if (objLight.isFireSource) {
                    fireSources.emplace_back(objLight.position.X, objLight.position.Z, objLight.position.Y);
                    fireRadii.push_back(objLight.node ? objLight.node->getRadius() : 120.0f);
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fire sources from objectLights_: {} sources, {} radii",
                      fireSources.size(), fireRadii.size());
            if (ctx_.currentZone) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: currentZone_->lights count={}", ctx_.currentZone->lights.size());
                for (const auto& zl : ctx_.currentZone->lights) {
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
                ctx_.particleManager->setFireSources(fireSources);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setFireSources count={}", fireSources.size());
            }
            if (ctx_.detailManager && ctx_.detailManager->hasSurfaceMap()) {
                ctx_.particleManager->setSurfaceMap(ctx_.detailManager->getSurfaceMap());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ setSurfaceMap from detailManager_");
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ no surface map (detailManager_={}, hasSurfaceMap={})",
                          (bool)ctx_.detailManager, ctx_.detailManager ? ctx_.detailManager->hasSurfaceMap() : false);
            }

            ctx_.particleManager->initUnifiedRenderer();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ initUnifiedRenderer complete");
            if (!fireSources.empty()) {
                ctx_.particleManager->createFireEmitters(fireSources, fireRadii);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ createFireEmitters called, sources={}, radii={}",
                          fireSources.size(), fireRadii.size());
            }

            // Collect fire glow lights (same sources as fire particles)
            // Positions in fireSources are EQ coords (Z-up); convert to Irrlicht (Y-up)
            ctx_.fireGlowLights.clear();
            uint32_t rngState = 0x12345678u;
            for (size_t i = 0; i < fireSources.size(); ++i) {
                IrrlichtRenderer::FireGlowLight e;
                // EQ(x,y,z) -> Irrlicht(x,z,y)
                e.position = irr::core::vector3df(fireSources[i].x, fireSources[i].z, fireSources[i].y);
                e.r = 0.15f; e.g = 0.09f; e.b = 0.02f;  // warm fire color (subtle)
                e.radius = fireRadii[i] * 0.3f;  // compact glow around fire source
                // Random flicker phase/speed per emitter
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerPhase = static_cast<float>(rngState & 0xFFFF) / 65535.0f * 6.28f;
                rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                e.flickerSpeed = 0.8f + static_cast<float>(rngState & 0xFFFF) / 65535.0f * 0.4f;
                ctx_.fireGlowLights.push_back(e);
            }
            LOG_INFO(MOD_GRAPHICS, "Collected {} fire glow lights for zone", ctx_.fireGlowLights.size());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fireGlowLights_ collected={}", ctx_.fireGlowLights.size());
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: particleManager_ is NULL — skipping fire source collection entirely");
        }

        // Boids
        if (ctx_.boidsManager) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(ctx_.currentZoneName);
            if (ctx_.zoneBoundsValid) {
                ctx_.boidsManager->onZoneEnter(ctx_.currentZoneName, biome,
                    glm::vec3(ctx_.zoneBoundsMinX, ctx_.zoneBoundsMinY, -1000.0f),
                    glm::vec3(ctx_.zoneBoundsMaxX, ctx_.zoneBoundsMaxY, 1000.0f));
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter with bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            } else {
                ctx_.boidsManager->onZoneEnter(ctx_.currentZoneName, biome);
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: boidsManager_ onZoneEnter without bounds, biome={}",
                          Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
            }
        }

        // Tumbleweed
        if (ctx_.tumbleweedManager) {
            auto biome = Environment::ZoneBiomeDetector::instance().getBiome(ctx_.currentZoneName);
            ctx_.tumbleweedManager->onZoneEnter(ctx_.currentZoneName, biome);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: tumbleweedManager_ onZoneEnter, biome={}",
                      Environment::ZoneBiomeDetector::instance().getBiomeName(biome));
        }

        // Weather. T396 (board B211): WeatherEffectsController::onZoneEnter()
        // and ::onZoneLeave() had ZERO callers tree-wide (grepped src/
        // include/ tests/ tools/ benchmarks/ docker/), so the weather state
        // was never reset on a zone change and the previous zone's rain kept
        // the global ambient darkened -- 1 - maxDarkening*intensity/10, i.e.
        // 0.460 at the intensity 9 qeynos2 served T377. Every other
        // environment manager here gets its onZoneEnter; this one was the
        // odd one out. It is called BEFORE the zone's own OP_Weather can
        // arrive, so a zone with no weather packet is lit normally.
        if (ctx_.weatherEffects) {
            ctx_.weatherEffects->onZoneEnter(ctx_.currentZoneName);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: weatherEffects_ onZoneEnter, ambientModifier={:.3f}",
                      ctx_.weatherEffects->getAmbientLightModifier());
            // T423 (board B362): onZoneEnter() resets the weather state, which
            // is a write to an input of the scene ambient -- mark it, exactly
            // as the OP_Weather site does.
            owner_.markSceneAmbientDirty();
        }

        // Weather surface + display settings
        if (ctx_.weatherEffects && ctx_.detailManager && ctx_.detailManager->hasSurfaceMap()) {
            ctx_.weatherEffects->setSurfaceMap(ctx_.detailManager->getSurfaceMap());
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: weatherEffects_ setSurfaceMap from detailManager_");
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvironmentalDisplaySettings");
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: PRE applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  ctx_.fireGlowLightingEnabled, ctx_.fireGlowIcospheresEnabled,
                  ctx_.maxFireGlowLights, ctx_.fireEffectsEnabled);
        ctx_.Overlays().applyEnvironmentalDisplaySettings();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: POST applyEnvSettings: fireGlowLightingEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, maxFireGlowLights_={}, fireEffectsEnabled_={}",
                  ctx_.fireGlowLightingEnabled, ctx_.fireGlowIcospheresEnabled,
                  ctx_.maxFireGlowLights, ctx_.fireEffectsEnabled);

        // Build icosphere mesh + shader for fire glow volumes
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere check: fireEffectsEnabled_={}, "
                  "fireGlowIcospheresEnabled_={}, fireGlowLights_.size()={}, icosphereVertices_.empty()={}",
                  ctx_.fireEffectsEnabled, ctx_.fireGlowIcospheresEnabled,
                  ctx_.fireGlowLights.size(), ctx_.Eqe1().icosphereVertices().empty());
#ifdef EQT_HAS_DRM
        if (ctx_.fireEffectsEnabled && ctx_.fireGlowIcospheresEnabled && !ctx_.fireGlowLights.empty() && ctx_.Eqe1().icosphereVertices().empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: building icosphere mesh + shader");
            ctx_.Eqe1().buildIcosphereMesh();
            ctx_.Eqe1().compileIcosphereShader();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build done: vertices={}, indices={}, "
                      "VBO={}, IBO={}, program={}",
                      ctx_.Eqe1().icosphereVertices().size(), ctx_.Eqe1().icosphereIndices().size(),
                      ctx_.Eqe1().icosphereVBO(), ctx_.Eqe1().icosphereIBO(), ctx_.icosphereProgram);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: icosphere build SKIPPED");
        }
#else
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: EQT_HAS_DRM not defined, icosphere build skipped");
#endif

        // Release combined zone geometry (if detail system disabled)
        if (ctx_.currentZone && ctx_.currentZone->geometry && !ctx_.detailManager) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: releasing combined zone geometry (detailManager_ inactive), "
                      "vertices={}, triangles={}",
                      ctx_.currentZone->geometry->vertices.size(), ctx_.currentZone->geometry->triangles.size());
            ctx_.currentZone->geometry->vertices.clear();
            ctx_.currentZone->geometry->vertices.shrink_to_fit();
            ctx_.currentZone->geometry->triangles.clear();
            ctx_.currentZone->geometry->triangles.shrink_to_fit();
        }

        // Governor reset
        if (ctx_.governor) {
            ctx_.governor->requestReset();
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
                      ctx_.deferredObjects.size(), (bool)ctx_.animatedTextureManager,
                      (bool)ctx_.zoneShader, ctx_.zoneShader ? ctx_.zoneShader->isWindAvailable() : false);
            size_t fixupProcessed = 0, fixupSkipNotBuilt = 0, fixupSkipBadIndex = 0;
            size_t fixupSkipNullNode = 0, fixupSkipNoGeomPlaceable = 0;
            size_t treesFound = 0, treesWindApplied = 0, treesNullMesh = 0;
            size_t animatedApplied = 0;
            bool windCheckEnabled = ctx_.zoneShader && ctx_.zoneShader->isWindAvailable();
            if (!windCheckEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: fixup wind shader check DISABLED: "
                          "zoneShader_={}, windAvailable={}",
                          (bool)ctx_.zoneShader,
                          ctx_.zoneShader ? ctx_.zoneShader->isWindAvailable() : false);
            }
            for (size_t oi = 0; oi < ctx_.deferredObjects.size(); ++oi) {
                const auto& deferred = ctx_.deferredObjects[oi];
                if (!deferred.meshBuilt || deferred.nodeIndex == SIZE_MAX) { ++fixupSkipNotBuilt; continue; }
                if (deferred.nodeIndex >= ctx_.objectNodes.size()) { ++fixupSkipBadIndex; continue; }
                if (deferred.objectIndex >= ctx_.currentZone->objects.size()) { ++fixupSkipBadIndex; continue; }

                auto* objNode = ctx_.objectNodes[deferred.nodeIndex];
                if (!objNode) { ++fixupSkipNullNode; continue; }
                const auto& objInstance = ctx_.currentZone->objects[deferred.objectIndex];
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
                                    irr::s32 windSolidType = ctx_.zoneShader->getActiveWindSolid();
                                    return windSolidType >= 0 ? windSolidType : ctx_.zoneShader->getActiveWindAlphaTest();
                                }
                                bool isETC1 = ctx_.constrainedTextureCache && !texName.empty() &&
                                    ctx_.constrainedTextureCache->isETC1Texture(texName);
                                if (isETC1) {
                                    return ctx_.zoneShader->getActiveWindAlphaTest();
                                }
                                irr::s32 windRGBAType = ctx_.zoneShader->getActiveWindAlphaTestRGBA();
                                return windRGBAType >= 0 ? windRGBAType : ctx_.zoneShader->getActiveWindAlphaTest();
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
                                if (ctx_.constrainedTextureCache && bufMat.TextureLayer[0].Texture && !bufMat.TextureLayer[1].Texture) {
                                    irr::core::stringc texNameStr = bufMat.TextureLayer[0].Texture->getName().getPath();
                                    irr::video::ITexture* alphaTex = ctx_.constrainedTextureCache->getAlphaTexture(
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
                                if (ctx_.constrainedTextureCache && tex0 && !mat.TextureLayer[1].Texture) {
                                    // Look up alpha companion by texture name
                                    irr::core::stringc texNameStr = tex0->getName().getPath();
                                    irr::video::ITexture* alphaTex = ctx_.constrainedTextureCache->getAlphaTexture(
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
                if (ctx_.animatedTextureManager && objInstance.geometry) {
                    irr::scene::IMesh* objMesh = objNode->getMesh();
                    if (objMesh) {
                        ctx_.animatedTextureManager->addMesh(*objInstance.geometry, ctx_.currentZone->objectTextures, objMesh);
                        ctx_.animatedTextureManager->addSceneNode(objNode);
                        ++animatedApplied;
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "Sequential: fixup pass: processed={}/{} objects, trees found={}, "
                     "wind applied={}, null mesh={}, animated={}, skipped: notBuilt={}, badIndex={}, "
                     "nullNode={}, noGeomPlaceable={}",
                     fixupProcessed, ctx_.deferredObjects.size(), treesFound, treesWindApplied,
                     treesNullMesh, animatedApplied, fixupSkipNotBuilt, fixupSkipBadIndex,
                     fixupSkipNullNode, fixupSkipNoGeomPlaceable);
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step11: complete");
        ctx_.ZoneStream().logAssetBuildTime("seq_env", 0, stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep12_Lights(ZoneLoadContext& ctx) {
    // ── Step 12: Lights — zone lighting ────────────────────────────────────
    ctx.updateProgress(96, "Setting up lighting...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: currentZone_={}, lights count={}",
                  (bool)ctx_.currentZone,
                  ctx_.currentZone ? ctx_.currentZone->lights.size() : 0);
        ctx_.zoneLightData.clear();
        ctx_.zoneLightPositions.clear();
        zoneLightRegions_.clear();
        if (ctx_.currentZone && !ctx_.currentZone->lights.empty()) {
            for (size_t i = 0; i < ctx_.currentZone->lights.size(); ++i) {
                const auto& light = ctx_.currentZone->lights[i];
                irr::core::vector3df pos(light->x, light->z, light->y);
                IrrlichtRenderer::ZoneLightData zld;
                zld.radius = std::max(light->radius, 1.0f);
                zld.attConstant = 1.0f;
                zld.attLinear = 1.0f / zld.radius;
                zld.attQuadratic = 1.0f / (zld.radius * zld.radius);
                zld.baseDiffuseColor = irr::video::SColorf(light->r, light->g, light->b, 1.0f);
                ctx_.zoneLightData.push_back(zld);
                ctx_.zoneLightPositions.push_back(pos);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}]: pos=({:.1f},{:.1f},{:.1f}) radius={:.1f} "
                          "color=({:.2f},{:.2f},{:.2f}) attLin={:.4f} attQuad={:.6f}",
                          i, light->x, light->y, light->z, zld.radius,
                          light->r, light->g, light->b, zld.attLinear, zld.attQuadratic);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: no lights to process (currentZone_={}, lights={})",
                      (bool)ctx_.currentZone, ctx_.currentZone ? ctx_.currentZone->lights.size() : 0);
        }

        // Install light BSP regions
        if (pendingZoneComputations_ && !pendingZoneComputations_->zoneLightRegions.empty()) {
            zoneLightRegions_ = std::move(pendingZoneComputations_->zoneLightRegions);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions from precomputed (count={})", zoneLightRegions_.size());
        } else if (ctx_.zoneBspTree && !ctx_.zoneBspTree->regions.empty() && ctx_.currentZone) {
            for (size_t i = 0; i < ctx_.currentZone->lights.size(); ++i) {
                const auto& light = ctx_.currentZone->lights[i];
                size_t regionIdx = ctx_.zoneBspTree->findRegionIndexForPoint(light->x, light->y, light->z);
                zoneLightRegions_.push_back(regionIdx);
                LOG_TRACE(MOD_GRAPHICS, "SEQ Step12: light[{}] BSP region={} pos=({:.1f},{:.1f},{:.1f})",
                          i, regionIdx == SIZE_MAX ? -1 : (int)regionIdx,
                          light->x, light->y, light->z);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions computed via findRegionIndexForPoint (count={})", zoneLightRegions_.size());
        } else {
            zoneLightRegions_.resize(ctx_.zoneLightData.size(), SIZE_MAX);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: light BSP regions fallback SIZE_MAX (no BSP), count={}", zoneLightRegions_.size());
        }

        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12: zoneLightData_={}, zoneLightPositions_={}, zoneLightRegions_={}",
                  ctx_.zoneLightData.size(), ctx_.zoneLightPositions.size(), zoneLightRegions_.size());
        ctx_.ZoneStream().logAssetBuildTime("seq_lights", ctx_.zoneLightData.size(), stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep12b_Simulation(ZoneLoadContext& ctx) {
    // ── Step 12b: SimulationWorker first-frame computation ──────────────────
    // Create SimulationWorker, set zone data, and run one synchronous computation
    // so the first rendered frame after loading has full PVS culling, sorted draw
    // list, lighting, weather, particles, etc. — no visual pop-in.
    ctx.updateProgress(99, "Computing scene...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: simulationWorker_={}, frustumCuller_={}, camera_={}",
                  (bool)owner_.simulationWorker_, (bool)ctx_.frustumCuller, (bool)ctx_.camera);

        // S05: simulationWorker_ already created in initLoadingScreen()

        // Build and register zone data
        SimulationZoneData zoneData = ctx_.SimBridge().buildSimulationZoneData();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: buildSimulationZoneData: regions={}, objects={}, zoneLights={}",
                  zoneData.regionBounds.size(), zoneData.objects.size(), zoneData.zoneLights.size());
        owner_.simulationWorker_->setZoneData(zoneData);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: setZoneData installed on SimulationWorker");

        // Update frustum culler with current camera so frustum planes are valid
        if (ctx_.frustumCuller && ctx_.camera) {
            irr::core::vector3df irrFwd = (ctx_.camera->getTarget() - ctx_.camera->getPosition());
            float eqFwdX = irrFwd.X;
            float eqFwdY = irrFwd.Z;
            float eqFwdZ = irrFwd.Y;
            float fovV = ctx_.camera->getFOV();
            auto screenSize = ctx_.driver->getScreenSize();
            float aspect = (float)screenSize.Width / (float)screenSize.Height;
            float camX, camY, camZ;
            ctx_.cameraController->getPositionEQ(camX, camY, camZ);
            ctx_.frustumCuller->update(camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ,
                fovV, aspect, 1.0f, ctx_.renderDistance);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: frustumCuller updated, fov={:.2f}, aspect={:.2f}, renderDist={:.0f}",
                      fovV, aspect, ctx_.renderDistance);
        }

        // Build SimulationInput from current state
        SimulationInput input;

        // Camera (Irrlicht Y-up)
        if (ctx_.camera) {
            input.cameraPos = ctx_.camera->getPosition();
            input.cameraTarget = ctx_.camera->getTarget();
        }
        if (ctx_.cameraController) {
            ctx_.cameraController->getPositionEQ(input.camEqX, input.camEqY, input.camEqZ);
        }

        // Frustum planes (EQ Z-up)
        if (ctx_.frustumCuller && ctx_.frustumCuller->isEnabled()) {
            for (int i = 0; i < 6; ++i) {
                const float* plane = ctx_.frustumCuller->getPlane(i);
                input.frustumPlanes[i][0] = plane[0];
                input.frustumPlanes[i][1] = plane[1];
                input.frustumPlanes[i][2] = plane[2];
                input.frustumPlanes[i][3] = plane[3];
            }
            input.frustumValid = true;

            // Occlusion camera
            auto& oc = input.occlusionCamera;
            oc.fwdX = ctx_.frustumCuller->getFwdX();
            oc.fwdY = ctx_.frustumCuller->getFwdY();
            oc.fwdZ = ctx_.frustumCuller->getFwdZ();
            oc.rightX = ctx_.frustumCuller->getRightX();
            oc.rightY = ctx_.frustumCuller->getRightY();
            oc.rightZ = 0;
            oc.upX = ctx_.frustumCuller->getUpX();
            oc.upY = ctx_.frustumCuller->getUpY();
            oc.upZ = ctx_.frustumCuller->getUpZ();
            oc.fovRadV = ctx_.frustumCuller->getFov();
            oc.aspect = ctx_.frustumCuller->getAspect();
            oc.enabled = true;
        }

        input.renderDistance = ctx_.renderDistance;
        input.loadingActive = false;  // Full culling, not loading-mode

        // Player (EQ Z-up)
        input.playerX = ctx_.playerX;
        input.playerY = ctx_.playerY;
        input.playerZ = ctx_.playerZ;
        input.playerHeading = ctx_.playerHeading;

        // Timing (first frame)
        input.deltaTime = 1.0f / 30.0f;
        input.frameNumber = ctx_.frameNumber;

        // Environment
        input.currentHour = ctx_.currentHour;
        input.currentMinute = ctx_.currentMinute;
        input.timeOfDay = ctx_.currentHour + ctx_.currentMinute / 60.0f;

        // Player light
        input.playerLightLevel = ctx_.SimBridge().playerLightLevel();

        // Vision and weather modifiers
        input.visionType = static_cast<uint8_t>(ctx_.currentVision);
        if (ctx_.weatherEffects && ctx_.weatherEffects->isEnabled()) {
            input.weatherAmbientModifier = ctx_.weatherEffects->getAmbientLightModifier();
        }

        // Entity snapshots
        if (ctx_.entityRenderer) {
            const auto& entities = ctx_.entityRenderer->getEntities();
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
            input.entityRenderDistance = ctx_.config.constrainedConfig.entityRenderDistance;
            input.maxVisibleEntities = ctx_.config.constrainedConfig.maxVisibleEntities;
            input.entityCullingEnabled = true;
            input.nameTagDistance = ctx_.entityRenderer->getNameTagDistance();
            input.nameTagsVisible = ctx_.entityRenderer->areNameTagsVisible();
        }

        // Sky state
        if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized() && ctx_.skyRenderer->isEnabled()) {
            input.skyEnabled = true;
            input.skyInitialized = true;
            input.skyCloudScrollOffset = ctx_.skyRenderer->getCloudScrollOffset();
        }

        // Weather effects state
        if (ctx_.weatherEffects && ctx_.weatherEffects->isEnabled()) {
            input.weatherEnabled = true;
            input.weatherTransitionProgress = ctx_.weatherEffects->getTransitionProgress();
            input.weatherTransitionDuration = ctx_.weatherEffects->getTransitionDuration();
            input.weatherCurrentDarkening = ctx_.weatherEffects->getCurrentDarkening();
            input.weatherTargetDarkening = ctx_.weatherEffects->getTargetDarkening();
            input.weatherDarkeningRate = ctx_.weatherEffects->getDarkeningRate();
            input.weatherLightningFlashTimer = ctx_.weatherEffects->getLightningFlashTimer();
            input.weatherLightningBoltTimer = ctx_.weatherEffects->getLightningBoltTimer();
            input.weatherLightningTimer = ctx_.weatherEffects->getLightningTimer();
            input.weatherLightningActive = ctx_.weatherEffects->isLightningActive();
            input.weatherLightningEnabled = ctx_.weatherEffects->isLightningEnabled();
            input.weatherType = ctx_.weatherEffects->getCurrentType();
            input.weatherIntensity = ctx_.weatherEffects->getCurrentIntensity();
        }

        // Particle system input
        if (ctx_.particleManager) {
            auto& pi = input.particleInput;
            pi.deltaTime = input.deltaTime;
            if (ctx_.camera) {
                auto cp = ctx_.camera->getAbsolutePosition();
                pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
            }
            if (ctx_.zoneShader) {
                const float* amb = ctx_.zoneShader->ambientColor();
                pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
            }
            pi.windDirection = ctx_.particleManager->getWindDirection();
            pi.windStrength = ctx_.particleManager->getWindStrength();
            pi.fireEnabled = ctx_.particleManager->isUnifiedFireEnabled();
            pi.commands = ctx_.particleManager->drainCommands();
            pi.unifiedRendererInitialized = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: particleInput assembled: fireEnabled={}, "
                      "unifiedRendererInitialized={}, commands={}, windStrength={:.2f}",
                      pi.fireEnabled, pi.unifiedRendererInitialized,
                      pi.commands.size(), pi.windStrength);
        }

        // Boids system input
        if (ctx_.boidsManager && ctx_.boidsManager->isEnabled()) {
            auto& bi = input.boidsInput;
            bi.deltaTime = input.deltaTime;
            bi.playerPosition = glm::vec3(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
            bi.playerHeading = ctx_.playerHeading;
            bi.timeOfDay = input.timeOfDay;
            bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            bi.initialized = true;
        }

        // Tumbleweed system input
        if (ctx_.tumbleweedManager && ctx_.tumbleweedManager->isEnabled()) {
            auto& ti = input.tumbleweedInput;
            ti.deltaTime = input.deltaTime;
            ti.playerPosition = glm::vec3(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
            ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            ti.initialized = true;
        }

        // Weather system input
        if (ctx_.weatherSystem) {
            auto& wi = input.weatherInput;
            wi.deltaTime = input.deltaTime;
            wi.initialized = true;
        }

        // Detail wind/disturbance input
        if (ctx_.detailManager && ctx_.detailManager->isEnabled()) {
            auto& dti = input.detailInput;
            dti.deltaTime = input.deltaTime;
            const auto& wc = ctx_.detailManager->getWindController();
            const auto& wcParams = wc.getParams();
            dti.windStrength = ctx_.detailManager->getZoneConfig().windStrength;
            dti.windFrequency = wcParams.frequency;
            dti.gustFrequency = wcParams.gustFrequency;
            dti.gustStrength = wcParams.gustStrength;
            dti.windDirX = wcParams.direction.X;
            dti.windDirY = wcParams.direction.Y;
            dti.playerPosX = ctx_.playerX;
            dti.playerPosY = ctx_.playerZ;
            dti.playerPosZ = ctx_.playerY;
            dti.initialized = true;
        }

        // Run synchronous computation
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationInput: entitySnapshots={}, frustumValid={}, "
                  "particleInput.fireEnabled={}, skyEnabled={}, weatherEnabled={}, renderDist={:.0f}",
                  input.entitySnapshots.size(), input.frustumValid,
                  input.particleInput.fireEnabled, input.skyEnabled,
                  input.weatherEnabled, input.renderDistance);
        SimulationOutput output;
        owner_.simulationWorker_->computeOnce(input, output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: SimulationOutput: regions={}, objects={}, lights={}, entities={}",
                  output.regionVisible.size(), output.objectVisible.size(),
                  output.lightVisible.size(), output.entityResults.size());

        // T116: applySimulationOutput() is value-only now -- stores no
        // pointers into `output`, a local about to go out of scope. The
        // T111/T115 nulling lines formerly here are gone: bindFrameSimulationBuffers()
        // (the pointer-storing half) is reachable only from the runtime path.
        ctx_.SimBridge().applySimulationOutput(output);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: applySimulationOutput applied, currentPvsRegion_={}", ctx_.currentPvsRegion);

        // Update door PVS visibility now that currentPvsRegion_ is set
        if (ctx_.doorManager) {
            ctx_.doorManager->setPvsRegion(ctx_.currentPvsRegion);
            ctx_.doorManager->update(0.0f);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step12b: doorManager_ PVS region set to {}, initial update done", ctx_.currentPvsRegion);
        }

        LOG_INFO(MOD_GRAPHICS, "Sequential: SimulationWorker first-frame computed and applied "
                 "(regions={}, objects={}, lights={}, entities={})",
                 output.regionVisible.size(), output.objectVisible.size(),
                 output.lightVisible.size(), output.entityResults.size());
        ctx_.ZoneStream().logAssetBuildTime("seq_simulation", 0, stepStart);
        FlushThreadLog();

        // Set up BSP-based camera collision now that PVS region is known
        if (ctx_.cameraController && ctx_.zoneBspTree) {
            ctx_.cameraController->setBspCollision(ctx_.zoneBspTree.get(), &ctx_.regionBoundingBoxes);
            // T464: no region hand-off -- the camera looks up the player's
            // eye region itself on every placement.
            LOG_INFO(MOD_GRAPHICS, "Camera BSP collision initialized (camera region {})", ctx_.currentPvsRegion);
        }
    }
}

void ZoneLoadComponent::loadZoneStep13_Cleanup(ZoneLoadContext& ctx) {
    // ── Step 13: Cleanup ───────────────────────────────────────────────────
    ctx.updateProgress(100, "Finalizing...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: releaseTextureDataAfterUpload={}, currentZone_={}, "
                  "pendingZoneComputations_={}",
                  ctx_.config.constrainedConfig.releaseTextureDataAfterUpload,
                  (bool)ctx_.currentZone, (bool)pendingZoneComputations_);
        if (ctx_.config.constrainedConfig.releaseTextureDataAfterUpload && ctx_.currentZone) {
            size_t freed = ctx_.currentZone->releaseTexturePixelData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released {:.1f}MB texture pixel data", freed / (1024.0f * 1024.0f));
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: texture pixel data retained (releaseTextureDataAfterUpload={}, currentZone_={})",
                      ctx_.config.constrainedConfig.releaseTextureDataAfterUpload, (bool)ctx_.currentZone);
        }
        if (ctx_.currentZone) {
            ctx_.currentZone->clearCharacterData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released zone character data");
        }
        if (ctx_.currentZone && ctx_.currentZone->wldLoader) {
            ctx_.currentZone->wldLoader->releasePostLoadData();
            LOG_INFO(MOD_GRAPHICS, "Sequential: released WLD post-load data");
        } else if (ctx_.currentZone && !ctx_.currentZone->wldLoader) {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: wldLoader is null, skipping releasePostLoadData");
        }
        pendingZoneComputations_.reset();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step13: pendingZoneComputations_ released");
        ctx_.ZoneStream().logAssetBuildTime("seq_cleanup", 0, stepStart);
    }
    FlushThreadLog();
}

} // namespace Graphics
} // namespace EQT
