#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/sky_renderer.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace EQT {
namespace Graphics {

// ============================================================================
// SimulationWorker Integration
// ============================================================================

SimulationZoneData IrrlichtRenderer::buildSimulationZoneData() {
    SimulationZoneData zoneData;
    zoneData.bspTree = zoneBspTree_;
    zoneData.usePvsCulling = usePvsCulling_;

    // Copy region bounding boxes (EQ Z-up coordinates)
    for (const auto& [regionIdx, bbox] : regionBoundingBoxes_) {
        SimulationZoneData::RegionBounds rb;
        rb.regionIdx = regionIdx;
        rb.minX = bbox.MinEdge.X;
        rb.minY = bbox.MinEdge.Y;
        rb.minZ = bbox.MinEdge.Z;
        rb.maxX = bbox.MaxEdge.X;
        rb.maxY = bbox.MaxEdge.Y;
        rb.maxZ = bbox.MaxEdge.Z;
        zoneData.regionBounds.push_back(rb);
    }

    // Build reverse lookup: regionIdx → index in regionBounds vector
    for (size_t i = 0; i < zoneData.regionBounds.size(); ++i) {
        zoneData.regionBoundsIndex[zoneData.regionBounds[i].regionIdx] = i;
    }

    // Build fallback list (all indices, for no-PVS zones)
    zoneData.allBoundsIndices.resize(zoneData.regionBounds.size());
    for (size_t i = 0; i < zoneData.regionBounds.size(); ++i) {
        zoneData.allBoundsIndices[i] = i;
    }

    // Pre-compute PVS visible region lists per camera region.
    // For each BspRegion with PVS data, extract its visibleRegions bitvector
    // into a compact list of regionBounds indices.
    if (zoneData.usePvsCulling && zoneData.bspTree) {
        const auto& bsp = *zoneData.bspTree;
        for (size_t camRegion = 0; camRegion < bsp.regions.size(); ++camRegion) {
            if (!bsp.regions[camRegion]) continue;
            const auto& pvs = bsp.regions[camRegion]->visibleRegions;
            if (pvs.empty()) continue;

            std::vector<size_t> indices;
            // Walk the bitvector, collect regionBounds indices for visible regions
            for (size_t regionIdx = 0; regionIdx < pvs.size(); ++regionIdx) {
                if (!pvs[regionIdx]) continue;
                auto it = zoneData.regionBoundsIndex.find(regionIdx);
                if (it != zoneData.regionBoundsIndex.end()) {
                    indices.push_back(it->second);
                }
            }
            if (!indices.empty()) {
                zoneData.pvsVisibleBoundsIndices[camRegion] = std::move(indices);
            }
        }
        LOG_INFO(MOD_GRAPHICS, "PVS lookup tables built: {} camera regions with PVS data, {} total region bounds",
                 zoneData.pvsVisibleBoundsIndices.size(), zoneData.regionBounds.size());
    }

    // Copy object data
    zoneData.objects.resize(objectNodes_.size());
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        auto& od = zoneData.objects[i];
        od.hasNode = (objectNodes_[i] != nullptr);
        if (i < objectBoundingBoxes_.size())
            od.boundingBox = objectBoundingBoxes_[i];
        if (i < objectPositions_.size())
            od.position = objectPositions_[i];
        od.bspRegion = (i < objectRegions_.size()) ? objectRegions_[i] : SIZE_MAX;
    }

    // Copy zone light data
    zoneData.zoneLights.resize(zoneLightData_.size());
    for (size_t i = 0; i < zoneLightData_.size(); ++i) {
        auto& zld = zoneData.zoneLights[i];
        if (i < zoneLightPositions_.size())
            zld.position = zoneLightPositions_[i];
        zld.bspRegion = (i < zoneLightRegions_.size()) ? zoneLightRegions_[i] : SIZE_MAX;
    }

    // Copy zone light node data for light selection (from zoneLightData_, no scene nodes)
    zoneData.zoneLightNodes.resize(zoneLightData_.size());
    for (size_t i = 0; i < zoneLightData_.size(); ++i) {
        auto& zlnd = zoneData.zoneLightNodes[i];
        if (i < zoneLightPositions_.size())
            zlnd.position = zoneLightPositions_[i];
        zlnd.diffuseColor = zoneLightData_[i].baseDiffuseColor;
        zlnd.radius = zoneLightData_[i].radius;
        zlnd.attConstant = zoneLightData_[i].attConstant;
        zlnd.attLinear = zoneLightData_[i].attLinear;
        zlnd.attQuadratic = zoneLightData_[i].attQuadratic;
    }

    // Copy zone light animation data (animated torches from WLD)
    if (currentZone_) {
        for (size_t i = 0; i < currentZone_->lights.size() && i < zoneLightData_.size(); ++i) {
            const auto& light = currentZone_->lights[i];
            if (!light->isAnimated()) continue;
            SimulationZoneData::ZoneLightAnimData anim;
            anim.lightIndex = i;
            anim.frameCount = light->frameCount;
            anim.sleepMs = light->sleepMs;
            anim.baseR = light->r;
            anim.baseG = light->g;
            anim.baseB = light->b;
            // Copy per-frame colors
            for (const auto& c : light->colors) {
                anim.frameColors.push_back(c);
            }
            // Copy light levels
            anim.lightLevels = light->lightLevels;
            zoneData.zoneLightAnims.push_back(std::move(anim));
        }
    }

    // Copy object light data
    zoneData.objectLights.resize(objectLights_.size());
    for (size_t i = 0; i < objectLights_.size(); ++i) {
        auto& old = zoneData.objectLights[i];
        old.position = objectLights_[i].position;
        old.originalColor = objectLights_[i].originalColor;
        old.isFireSource = objectLights_[i].isFireSource;
        old.flickerSpeed = objectLights_[i].flickerSpeed;
        old.objectName = objectLights_[i].objectName;
        old.bspRegion = objectLights_[i].bspRegion;
        if (objectLights_[i].node) {
            const auto& ld = objectLights_[i].node->getLightData();
            old.radius = ld.Radius;
            old.attConstant = ld.Attenuation.X;
            old.attLinear = ld.Attenuation.Y;
            old.attQuadratic = ld.Attenuation.Z;
        }
    }

    // Portal system (non-owning const pointer, immutable after zone load)
    if (portalSystem_) {
        zoneData.portalSystem = portalSystem_.get();
    }

    // Portal visibility config
    zoneData.enablePortalVis = config_.constrainedConfig.portalOcclusion;
    zoneData.enableStencilBuffer = config_.constrainedConfig.enableStencilBuffer;
    zoneData.indoorRegions = indoorRegions_;

    // Collision map for entity ground snapping (immutable after zone load)
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        zoneData.hcMap = collisionMap_;
    }

    // Copy occluder data from renderer's occlusion culler (if it has occluders)
    if (occlusionCuller_ && occlusionCuller_->hasOccluders()) {
        zoneData.regionOccluders = occlusionCuller_->getRegionOccludersMap();
        zoneData.occlusionConfig = occlusionCuller_->getConfig();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: copying {} region occluder sets to worker",
                 zoneData.regionOccluders.size());
    }

    return zoneData;
}

void IrrlichtRenderer::startSimulationWorkerEarly() {
    // S05: simulationWorker_ already created in initLoadingScreen()
    if (!simulationWorker_ || simulationWorker_->isRunning()) return;

    SimulationZoneData zoneData = buildSimulationZoneData();
    simulationWorker_->setZoneData(zoneData);
    simulationWorker_->start();

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker started early for zone '{}' (BSP/regions/objects/lights)",
             currentZoneName_);
}

// registerSimulationWorkerTreeData() removed (P32): it fed
// AnimatedTreeManager's CPU-instancing tree buffers (populated only via the
// removed EDT_BURNINGSVIDEO branch) into SimulationWorker::updateTreeData().
// Confirmed uncalled anywhere in the tree before deletion — dead code, went
// with the class.

void IrrlichtRenderer::registerSimulationWorkerVertexAnimData() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;
    if (vertexAnimatedMeshes_.empty()) return;

    std::vector<SimulationZoneData::VertexAnimData> vertAnims(vertexAnimatedMeshes_.size());
    for (size_t i = 0; i < vertexAnimatedMeshes_.size(); ++i) {
        const auto& vam = vertexAnimatedMeshes_[i];
        auto& vad = vertAnims[i];
        if (vam.animData && !vam.animData->frames.empty()) {
            vad.delayMs = vam.animData->delayMs;
            vad.frameCount = vam.animData->frames.size();
            vad.framePositions.resize(vam.animData->frames.size());
            for (size_t f = 0; f < vam.animData->frames.size(); ++f) {
                vad.framePositions[f] = vam.animData->frames[f].positions;
            }
            vad.vertexMapping = vam.vertexMapping;
            vad.centerOffsetX = vam.centerOffsetX;
            vad.centerOffsetY = vam.centerOffsetY;
            vad.centerOffsetZ = vam.centerOffsetZ;
            if (vam.mesh) {
                vad.bufferVertexCounts.resize(vam.mesh->getMeshBufferCount());
                for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                    vad.bufferVertexCounts[b] = vam.mesh->getMeshBuffer(b)->getVertexCount();
                }
            }
        }
    }

    simulationWorker_->updateVertexAnimData(std::move(vertAnims));
    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: registered {} vertex animated meshes", vertexAnimatedMeshes_.size());
}

void IrrlichtRenderer::stopSimulationWorker() {
    if (simulationWorker_ && simulationWorker_->isRunning()) {
        simulationWorker_->stop();
        simulationWorker_->clearZoneData();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker stopped for zone transition");
    }
}

void IrrlichtRenderer::postSimulationInput(float deltaTime) {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;

    SimulationInput input;

    // Camera (Irrlicht Y-up)
    if (camera_) {
        input.cameraPos = camera_->getPosition();
        input.cameraTarget = camera_->getTarget();
    }

    // Camera position in EQ Z-up
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
    } else {
        input.frustumValid = false;
    }

    input.renderDistance = renderDistance_;
    input.loadingActive = loadingScreenVisible_ || loading_.load(std::memory_order_relaxed);

    // Player (EQ Z-up)
    input.playerX = playerX_;
    input.playerY = playerY_;
    input.playerZ = playerZ_;
    input.playerHeading = playerHeading_;

    // Timing
    input.deltaTime = deltaTime;
    input.frameNumber = frameNumber_;

    // Environment
    input.currentHour = currentHour_;
    input.currentMinute = currentMinute_;
    input.timeOfDay = currentHour_ + currentMinute_ / 60.0f;

    // Player light
    input.playerLightLevel = playerLightLevel_;
    if (playerLightNode_ && playerLightLevel_ > 0) {
        const auto& ld = playerLightNode_->getLightData();
        input.playerLightColor = ld.DiffuseColor;
        input.playerLightRadius = ld.Radius;
        input.playerLightAttConstant = ld.Attenuation.X;
        input.playerLightAttLinear = ld.Attenuation.Y;
        input.playerLightAttQuadratic = ld.Attenuation.Z;
    }

    // Vision and weather modifiers (for zone light animation colors)
    input.visionType = static_cast<uint8_t>(currentVision_);
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        input.weatherAmbientModifier = weatherEffects_->getAmbientLightModifier();
    }

    // Tree wind state snapshot
    if (treeWindController_.isEnabled()) {
        const auto& wc = treeWindController_;
        const auto& cfg = wc.getConfig();
        input.treeWind.enabled = cfg.enabled;
        input.treeWind.time = wc.getTime();
        input.treeWind.baseFrequency = cfg.baseFrequency;
        input.treeWind.baseStrength = cfg.baseStrength;
        input.treeWind.gustFrequency = cfg.gustFrequency;
        input.treeWind.gustStrength = cfg.gustStrength;
        input.treeWind.turbulence = cfg.turbulence;
        input.treeWind.influenceStartHeight = cfg.influenceStartHeight;
        input.treeWind.influenceExponent = cfg.influenceExponent;
        input.treeWind.weatherMultiplier = wc.getWeatherMultiplier();
        auto dir = wc.getWindDirection();
        input.treeWind.windDirX = dir.X;
        input.treeWind.windDirY = dir.Y;
    }

    // Sky state snapshot
    if (skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
        input.skyEnabled = true;
        input.skyInitialized = true;
        input.skyCloudScrollOffset = skyRenderer_->getCloudScrollOffset();
    }

    // Weather effects state snapshot
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

    // Occlusion camera state
    if (frustumCuller_ && frustumCuller_->isEnabled()) {
        auto& oc = input.occlusionCamera;
        oc.fwdX = frustumCuller_->getFwdX();
        oc.fwdY = frustumCuller_->getFwdY();
        oc.fwdZ = frustumCuller_->getFwdZ();
        oc.rightX = frustumCuller_->getRightX();
        oc.rightY = frustumCuller_->getRightY();
        oc.rightZ = 0;  // Right vector is horizontal in EQ Z-up
        oc.upX = frustumCuller_->getUpX();
        oc.upY = frustumCuller_->getUpY();
        oc.upZ = frustumCuller_->getUpZ();
        oc.fovRadV = frustumCuller_->getFov();
        oc.aspect = frustumCuller_->getAspect();
        oc.enabled = true;
    }

    // Entity snapshots for worker interpolation
    if (entityRenderer_) {
        // Drain pending updates for worker
        auto drained = entityRenderer_->drainPendingUpdates();
        input.entityPendingUpdates.reserve(drained.size());
        for (const auto& d : drained) {
            SimulationInput::EntityPendingUpdate epu;
            epu.spawnId = d.spawnId;
            epu.x = d.x; epu.y = d.y; epu.z = d.z;
            epu.heading = d.heading;
            epu.dx = d.dx; epu.dy = d.dy; epu.dz = d.dz;
            epu.animation = d.animation;
            input.entityPendingUpdates.push_back(epu);
        }

        // Build entity snapshots
        const auto& entities = entityRenderer_->getEntities();
        input.entitySnapshots.reserve(entities.size());
        for (const auto& [spawnId, visual] : entities) {
            if (!visual.sceneNode) continue;
            SimulationInput::EntitySnapshot snap;
            snap.spawnId = spawnId;
            snap.lastX = visual.lastX;
            snap.lastY = visual.lastY;
            snap.lastZ = visual.lastZ;
            snap.velocityX = visual.velocityX;
            snap.velocityY = visual.velocityY;
            snap.velocityZ = visual.velocityZ;
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
            snap.hasVelocity = (std::abs(visual.velocityX) > 0.01f ||
                                std::abs(visual.velocityY) > 0.01f ||
                                std::abs(visual.velocityZ) > 0.01f);
            input.entitySnapshots.push_back(snap);
        }

        // Entity culling config (always active)
        input.entityRenderDistance = config_.constrainedConfig.entityRenderDistance;
        input.maxVisibleEntities = config_.constrainedConfig.maxVisibleEntities;
        input.entityCullingEnabled = true;

        // Name tag visibility config
        input.nameTagDistance = entityRenderer_->getNameTagDistance();
        input.nameTagsVisible = entityRenderer_->areNameTagsVisible();
    }

    // Vertex animation timing
    input.vertAnimDeltaMs = deltaTime * 1000.0f;

    // Particle system input
    if (particleManager_ && zoneReady_) {
        auto& pi = input.particleInput;
        pi.deltaTime = deltaTime;
        if (camera_) {
            auto cp = camera_->getAbsolutePosition();
            pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
        }

        // Ambient color from zone shader
        if (zoneShader_) {
            const float* amb = zoneShader_->ambientColor();
            pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
        }

        // Wind from particle manager's env state
        pi.windDirection = particleManager_->getWindDirection();
        pi.windStrength = particleManager_->getWindStrength();

        // Entity positions/directions resolved last frame
        pi.entityPositions = std::move(pendingParticleEntityPositions_);
        pi.entityDirections = std::move(pendingParticleEntityDirections_);

        // Commands from particle manager
        pi.commands = particleManager_->drainCommands();

        // State flags
        pi.fireEnabled = particleManager_->isUnifiedFireEnabled();
        pi.unifiedRendererInitialized = true;  // If we got here, it's initialized
        pi.poolSize = 0;  // Worker determines pool size from config

        // Weather lights: collect nearby lights for per-particle illumination
        if (particleManager_->isWeatherParticlesActive()) {
            float maxLightDist = 150.0f;
            float maxLightDistSq = maxLightDist * maxLightDist;
            float colorBoost = 2.5f;
            glm::vec3 camIrr = pi.cameraPos;

            for (size_t i = 0; i < zoneLightData_.size() && i < zoneLightPositions_.size(); ++i) {
                const auto& pos = zoneLightPositions_[i];
                float dx = pos.X - camIrr.x, dy = pos.Y - camIrr.y, dz = pos.Z - camIrr.z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq < maxLightDistSq) {
                    const auto& zld = zoneLightData_[i];
                    float radius = zld.radius > 0 ? zld.radius : 30.0f;
                    pi.weatherLights.push_back({
                        glm::vec3(pos.X, pos.Y, pos.Z),
                        radius,
                        glm::vec3(
                            std::min(1.0f, zld.baseDiffuseColor.r * colorBoost),
                            std::min(1.0f, zld.baseDiffuseColor.g * colorBoost),
                            std::min(1.0f, zld.baseDiffuseColor.b * colorBoost)
                        )
                    });
                }
            }

            // Player light
            if (playerLightNode_ && playerLightLevel_ > 0) {
                auto pos = playerLightNode_->getPosition();
                auto& ld = playerLightNode_->getLightData();
                pi.weatherLights.push_back({
                    glm::vec3(pos.X, pos.Y, pos.Z),
                    ld.Radius,
                    glm::vec3(
                        std::min(1.0f, ld.DiffuseColor.r * colorBoost),
                        std::min(1.0f, ld.DiffuseColor.g * colorBoost),
                        std::min(1.0f, ld.DiffuseColor.b * colorBoost)
                    )
                });
            }
        }
    }

    // Boids system input
    if (boidsManager_ && boidsManager_->isEnabled() && zoneReady_) {
        auto& bi = input.boidsInput;
        bi.deltaTime = deltaTime;
        bi.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
        bi.playerHeading = playerHeading_;
        bi.timeOfDay = currentHour_ + currentMinute_ / 60.0f;
        bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        bi.commands = boidsManager_->drainCommands();
        bi.initialized = true;
    }

    // Tumbleweed system input
    if (tumbleweedManager_ && tumbleweedManager_->isEnabled() && zoneReady_) {
        auto& ti = input.tumbleweedInput;
        ti.deltaTime = deltaTime;
        ti.playerPosition = glm::vec3(playerX_, playerY_, playerZ_);
        ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        ti.commands = tumbleweedManager_->drainCommands();
        ti.initialized = true;
    }

    // Weather system input
    if (weatherSystem_) {
        auto& wi = input.weatherInput;
        wi.deltaTime = deltaTime;
        wi.commands = weatherSystem_->drainCommands();
        wi.initialized = true;
    }

    // Spell VFX input (desktop GL worker-driven path)
    if (spellVisualFX_ && spellVisualFX_->isWorkerDriven()) {
        auto& svi = input.spellVfxInput;
        svi.deltaTime = deltaTime;
        svi.commands = spellVisualFX_->drainCommands();
        svi.initialized = true;
    }

    // Detail wind/disturbance input
    if (detailManager_ && detailManager_->isEnabled() && zoneReady_) {
        auto& dti = input.detailInput;
        dti.deltaTime = deltaTime;
        // Wind params from detail manager's wind controller and zone config
        const auto& wc = detailManager_->getWindController();
        const auto& wcParams = wc.getParams();
        dti.windStrength = detailManager_->getZoneConfig().windStrength;
        dti.windFrequency = wcParams.frequency;
        dti.gustFrequency = wcParams.gustFrequency;
        dti.gustStrength = wcParams.gustStrength;
        dti.windDirX = wcParams.direction.X;
        dti.windDirY = wcParams.direction.Y;
        // Disturbance params
        const auto& dc = detailManager_->getFoliageDisturbanceConfig();
        dti.disturbanceEnabled = dc.enabled;
        dti.playerRadius = dc.playerRadius;
        dti.playerStrength = dc.playerStrength;
        dti.maxDisplacement = dc.maxDisplacement;
        dti.verticalDipFactor = dc.verticalDipFactor;
        dti.velocityInfluence = dc.velocityInfluence;
        dti.heightExponent = dc.heightExponent;
        dti.recoveryRate = dc.recoveryRate;
        // Player state (Irrlicht Y-up coords)
        dti.playerPosX = playerX_;
        dti.playerPosY = playerZ_;  // EQ Z → Irrlicht Y
        dti.playerPosZ = playerY_;  // EQ Y → Irrlicht Z
        // Compute velocity for disturbance
        static float lastDetailPX = playerX_, lastDetailPY = playerY_;
        if (deltaTime > 0.001f) {
            dti.playerVelX = (playerX_ - lastDetailPX) / deltaTime;
            dti.playerVelZ = (playerY_ - lastDetailPY) / deltaTime;
        }
        lastDetailPX = playerX_;
        lastDetailPY = playerY_;
        dti.playerMoving = (dti.playerVelX * dti.playerVelX + dti.playerVelZ * dti.playerVelZ) > 0.1f;
        dti.commands = detailManager_->drainCommands();
        dti.initialized = true;
    }

    simulationWorker_->postInput(input);
}

void IrrlichtRenderer::applySimulationResults() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;

    const SimulationOutput* results = simulationWorker_->swapAndGetResults();
    if (!results) return;
    currentFrameSimResults_ = results;

    applySimulationOutput(*results);
}

void IrrlichtRenderer::applySimulationOutput(const SimulationOutput& resultsRef) {
    const SimulationOutput* results = &resultsRef;

    // Apply PVS region visibility
    if (usePvsCulling_ && !results->regionVisible.empty()) {
        // Build a lookup: index in regionBounds → regionIdx
        // We need to map worker output indices back to actual regionMeshNodes_
        size_t idx = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) { idx++; continue; }
            // Find matching index in worker output
            // Worker output is indexed by position in zoneData_.regionBounds vector
            // which was built from regionBoundingBoxes_ in the same order
            bool visible = false;
            if (idx < results->regionVisible.size()) {
                visible = results->regionVisible[idx] != 0;
            }
            node->setVisible(visible);
            idx++;
        }

        size_t prevPvsRegion = currentPvsRegion_;
        currentPvsRegion_ = results->currentPvsRegion;

        // Point to PVS depth map in front buffer (no copy — stable until next swap)
        regionPvsDepthMap_ = &results->regionPvsDepth;

        // Re-prioritize pending queues only when leaving the PVS neighborhood.
        // Small region crossings within the neighborhood are suppressed to avoid
        // expensive re-prioritization cascades on every boundary crossing.
        if (currentPvsRegion_ != prevPvsRegion && prevPvsRegion != SIZE_MAX) {
            // Camera collision update is cheap and always needed
            if (cameraController_) {
                cameraController_->setBspPlayerRegion(currentPvsRegion_);
            }
            // Full re-prioritization only when leaving neighborhood
            if (!isInPvsNeighborhood(currentPvsRegion_)) {
                rebuildPvsNeighborhood(currentPvsRegion_);
                onPvsRegionChanged();
            }
        }

        // Always log PVS state from worker (throttled to every 30 frames)
        static int simWorkerPvsLogCounter = 0;
        if (++simWorkerPvsLogCounter % 30 == 1 || currentPvsRegion_ != prevPvsRegion) {
            LOG_DEBUG(MOD_GRAPHICS, "SimWorker apply: PVS={}{} meshLoadQ={} protected={} regionVis={}",
                      currentPvsRegion_,
                      (currentPvsRegion_ != prevPvsRegion ? fmt::format(" (was {})", prevPvsRegion) : ""),
                      results->meshLoadQueue.size(), results->protectedRegions.size(),
                      results->regionVisible.size());
        }

        // Point to portal visible regions in front buffer (no copy — stable until next swap)
        // Used for geometry stencil masking only (not entity/light culling)
        portalVisibleRegions_ = &results->portalVisibleRegions;

        // Per-region portal occlusion: enable/disable based on camera's indoor status
        // When indoor region map is loaded, dynamically toggle portal occlusion as
        // the player moves between indoor and outdoor regions within the same zone.
        if (portalOcclusionEligible_ && config_.constrainedConfig.portalOcclusion &&
            config_.constrainedConfig.enableStencilBuffer && !indoorRegions_.empty()) {
            bool wasEnabled = portalOcclusionEnabled_;
            portalOcclusionEnabled_ = results->cameraInIndoorRegion;
            if (portalOcclusionEnabled_ != wasEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "Portal occlusion {} (camera region {} is {})",
                          portalOcclusionEnabled_ ? "ON" : "OFF",
                          currentPvsRegion_,
                          results->cameraInIndoorRegion ? "indoor" : "outdoor");
            }
        }

        // Mesh load queue / protected-regions population for the constrained
        // mesh cache removed T67 — was gated on constrainedMeshCache_, never
        // true (the cache was never constructed), so protectedRegions_ was
        // already permanently all-false in every shipped build (its only
        // writer lived here). meshLoadQueue_'s only readers were the same
        // dead-gated code and were removed alongside it. See
        // src/client/graphics/CLAUDE.md for the reference pointer.

        // Apply sorted region draw list for front-to-back zone rendering
        if (manualZoneDrawEnabled_ && !results->sortedRegions.empty()) {
            sortedZoneDrawList_.clear();
            sortedZoneDrawList_.reserve(results->sortedRegions.size());
            for (const auto& sr : results->sortedRegions) {
                auto it = regionMeshNodes_.find(sr.regionIdx);
                irr::scene::IMeshSceneNode* node = (it != regionMeshNodes_.end()) ? it->second : nullptr;
                sortedZoneDrawList_.push_back({sr.regionIdx, sr.distanceSq, node});
            }
        }
    }

    // Apply object visibility
    // When manualZoneDrawEnabled_, objects are drawn via drawZoneGeometrySorted()
    // alongside zone geometry for material sorting. Keep them out of the scene graph
    // to avoid double-rendering by drawAll().
    if (!results->objectVisible.empty() && !manualZoneDrawEnabled_) {
        for (size_t i = 0; i < results->objectVisible.size() && i < objectNodes_.size(); ++i) {
            auto* node = objectNodes_[i];
            if (!node) continue;

            bool shouldBeVisible = results->objectVisible[i] != 0;
            bool isInScene = (i < objectInSceneGraph_.size()) ? objectInSceneGraph_[i] : false;

            if (shouldBeVisible && !isInScene) {
                smgr_->getRootSceneNode()->addChild(node);
                node->setVisible(true);
                if (i < objectInSceneGraph_.size())
                    objectInSceneGraph_[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < objectInSceneGraph_.size())
                    objectInSceneGraph_[i] = false;
            }
        }
    }

    // Zone light visibility: no scene nodes to add/remove.
    // Worker uses lightVisible output internally for light selection filtering.

    // Apply light selection to shader
    if (results->activeLightCount > 0 && zoneShader_ && zoneShader_->isAvailable()) {
        bool playerLightSet = false;
        for (int i = 0; i < results->activeLightCount; ++i) {
            const auto& sl = results->selectedLights[i];
            if (!sl.valid) continue;

            float boost = (i == 0 && sl.isPlayerLight) ? 3.0f : 1.0f;
            zoneShader_->setPointLight(i,
                sl.position.X, sl.position.Y, sl.position.Z,
                sl.diffuseColor.r * boost,
                sl.diffuseColor.g * boost,
                sl.diffuseColor.b * boost,
                sl.attConstant, sl.attLinear, sl.attQuadratic);

            // Set per-pixel player light FS uniforms (GLES2: computed in fragment shader)
            if (i == 0 && sl.isPlayerLight) {
                zoneShader_->setPlayerLightPos(sl.position.X, sl.position.Y, sl.position.Z);
                zoneShader_->setPlayerLightColor(
                    sl.diffuseColor.r * boost,
                    sl.diffuseColor.g * boost,
                    sl.diffuseColor.b * boost);
                zoneShader_->setPlayerLightAtten(sl.attConstant, sl.attLinear, sl.attQuadratic);
                playerLightSet = true;
            }
        }
        // Clear player light FS uniforms if no player light active
        if (!playerLightSet) {
            zoneShader_->setPlayerLightColor(0.0f, 0.0f, 0.0f);
        }

        // Apply debug toggles (/plight, /olight, L key)
        int effectiveLightCount = results->activeLightCount;

        // /plight off: zero player light in both VS (light[0]) and FS
        if (!debugPlayerLightEnabled_) {
            zoneShader_->setPointLight(0, 0,0,0, 0,0,0, 1,0,0);
            zoneShader_->setPlayerLightColor(0.0f, 0.0f, 0.0f);
            if (playerLightSet) effectiveLightCount--;
            playerLightSet = false;
        }

        // /olight off: zero all zone lights (indices 1-7) in VS
        if (!debugObjectLightsEnabled_) {
            for (int i = 1; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                zoneShader_->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = playerLightSet ? 1 : 0;
        } else if (maxObjectLights_ < effectiveLightCount - (playerLightSet ? 1 : 0)) {
            // L key: limit zone light count, zero excess slots
            int maxZone = maxObjectLights_;
            int startClear = (playerLightSet ? 1 : 0) + maxZone;
            for (int i = startClear; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                zoneShader_->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = (playerLightSet ? 1 : 0) + maxZone;
        }

        zoneShader_->setNumPointLights(effectiveLightCount);
        activeLightCount_ = effectiveLightCount;
    }

    // Apply object light PVS visibility (scene graph add/remove)
    if (!results->objectLightVisible.empty()) {
        for (size_t i = 0; i < results->objectLightVisible.size() && i < objectLights_.size(); ++i) {
            auto* node = objectLights_[i].node;
            if (!node) continue;
            bool shouldBeVisible = results->objectLightVisible[i] != 0;
            bool isInScene = (i < objectLightInSceneGraph_.size()) ? objectLightInSceneGraph_[i] : false;
            if (shouldBeVisible && !isInScene) {
                smgr_->getRootSceneNode()->addChild(node);
                if (i < objectLightInSceneGraph_.size())
                    objectLightInSceneGraph_[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < objectLightInSceneGraph_.size())
                    objectLightInSceneGraph_[i] = false;
            }
        }
    }

    // Apply fire flicker colors to object lights (for other systems that read them)
    if (!results->objectLightColors.empty()) {
        for (size_t i = 0; i < results->objectLightColors.size() && i < objectLights_.size(); ++i) {
            auto* node = objectLights_[i].node;
            if (!node) continue;
            auto& ld = node->getLightData();
            ld.DiffuseColor.r = results->objectLightColors[i].r;
            ld.DiffuseColor.g = results->objectLightColors[i].g;
            ld.DiffuseColor.b = results->objectLightColors[i].b;
        }
    }

    // Zone light animation colors: worker applies them to its own zoneData_ internally.
    // No scene nodes to update on main thread.

    // Apply sky state from worker
    if (results->skyState.valid && skyRenderer_ && skyRenderer_->isInitialized() && skyRenderer_->isEnabled()) {
        SkyRenderer::SkyState skyState;
        skyState.cloudScrollOffset = results->skyState.cloudScrollOffset;
        skyRenderer_->applyState(skyState);
    }

    // Apply weather system state from worker (must fire listeners before weatherEffects apply)
    if (results->weatherOutput.valid && weatherSystem_) {
        const auto& wo = results->weatherOutput;
        weatherSystem_->applyWorkerResults(wo.currentWeather, wo.targetWeather,
                                           wo.transitionProgress, wo.windIntensity,
                                           wo.weatherChanged, wo.newWeatherType);
    }

    // Apply weather effects state from worker
    if (results->weatherEffectsState.valid && weatherEffects_ && weatherEffects_->isEnabled()) {
        WeatherEffectsController::WeatherEffectsState wes;
        wes.transitionProgress = results->weatherEffectsState.transitionProgress;
        wes.currentDarkening = results->weatherEffectsState.currentDarkening;
        wes.lightningFlashTimer = results->weatherEffectsState.lightningFlashTimer;
        wes.lightningBoltTimer = results->weatherEffectsState.lightningBoltTimer;
        wes.lightningActive = results->weatherEffectsState.lightningActive;
        wes.triggerLightningFlash = results->weatherEffectsState.triggerLightningFlash;
        weatherEffects_->applyState(wes);
    }

    // Update player light position (every frame, not gated)
    if (playerLightNode_ && playerLightLevel_ > 0) {
        playerLightNode_->setPosition(irr::core::vector3df(playerX_, playerZ_ + 3.0f, playerY_));
    }

    // P32: the tree wind shadow-vertex apply block was removed along with
    // AnimatedTreeManager. results->treeShadows was only ever populated by
    // SimulationWorker::updateTreeData(), which registerSimulationWorkerTreeData()
    // called — and that function was confirmed uncalled anywhere, so
    // results->treeShadows was always empty and this block was already
    // fully dead (its `!results->treeShadows.empty()` guard never passed).

    // Apply spell VFX results from worker (desktop GL path)
    if (results->spellVfxOutput.valid && spellVisualFX_ && spellVisualFX_->isWorkerDriven()) {
        spellVisualFX_->applyWorkerResults(*results);
    }

    // Apply detail wind/disturbance shadow vertices
    if (detailManager_ && detailManager_->isEnabled() && results->detailOutput.valid) {
        detailManager_->applyWorkerResults(results->detailOutput);
    }

    // Apply vertex animation results (frame index computed by worker)
    if (!results->vertexAnims.empty() && !vertexAnimatedMeshes_.empty()) {
        for (size_t i = 0; i < results->vertexAnims.size() && i < vertexAnimatedMeshes_.size(); ++i) {
            const auto& vr = results->vertexAnims[i];
            auto& vam = vertexAnimatedMeshes_[i];
            if (!vr.frameChanged) continue;
            if (!vam.mesh || !vam.animData || vam.animData->frames.empty()) continue;

            vam.currentFrame = vr.currentFrame;

            // Skip expensive per-vertex work for non-visible meshes
            if (vam.node && !vam.node->isVisible()) continue;

            const VertexAnimFrame& frame = vam.animData->frames[vam.currentFrame];
            size_t expectedVerts = frame.positions.size() / 3;

            for (irr::u32 b = 0; b < vam.mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = vam.mesh->getMeshBuffer(b);
                irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                irr::u32 vertexCount = buffer->getVertexCount();
                if (b >= vam.vertexMapping.size() || vam.vertexMapping[b].size() != vertexCount) continue;
                for (irr::u32 v = 0; v < vertexCount; ++v) {
                    size_t animIdx = vam.vertexMapping[b][v];
                    if (animIdx == SIZE_MAX || animIdx >= expectedVerts) continue;
                    float eqX = frame.positions[animIdx * 3 + 0] + vam.centerOffsetX;
                    float eqY = frame.positions[animIdx * 3 + 1] + vam.centerOffsetY;
                    float eqZ = frame.positions[animIdx * 3 + 2] + vam.centerOffsetZ;
                    vertices[v].Pos.X = eqX;
                    vertices[v].Pos.Y = eqZ;
                    vertices[v].Pos.Z = eqY;
                }
                buffer->setDirty(irr::scene::EBT_VERTEX);
            }
        }
    }

    // Apply particle output from worker
    if (results->particleOutput.valid && particleManager_) {
        // Store render buffer pointer for use during rendering
        particleRenderBuffer_ = &results->particleOutput.renderBuffer;

        // Update particle manager's active count from worker
        particleManager_->setWorkerActiveCount(results->particleOutput.activeCount);

        // Resolve entity positions for the next frame's particle input
        pendingParticleEntityPositions_.clear();
        pendingParticleEntityDirections_.clear();

        for (uint16_t entityID : results->particleOutput.positionRequestEntities) {
            glm::vec3 pos;
            if (particleManager_->resolveEntityPosition(entityID, pos)) {
                pendingParticleEntityPositions_[entityID] = pos;
            }
        }
    } else {
        particleRenderBuffer_ = nullptr;
    }

    // Apply boids output from worker
    if (results->boidsOutput.valid && boidsManager_) {
        boidsManager_->applyWorkerResults(results->boidsOutput);
    }

    // Apply tumbleweed output from worker
    if (results->tumbleweedOutput.valid && tumbleweedManager_) {
        tumbleweedManager_->applyWorkerResults(results->tumbleweedOutput);
    }

    // Apply occlusion-culled regions from worker
    occlusionCulledRegions_ = results->occlusionCulledRegions;

    // Apply entity interpolation and visibility results
    if (entityRenderer_ && !results->entityResults.empty()) {
        auto& entities = const_cast<std::map<uint16_t, EntityVisual>&>(entityRenderer_->getEntities());

        for (const auto& er : results->entityResults) {
            auto it = entities.find(er.spawnId);
            if (it == entities.end()) continue;
            EntityVisual& visual = it->second;

            // Apply interpolated position (skip player — camera controls it)
            if (er.wasInterpolated && !visual.isPlayer) {
                visual.lastX = er.posX;
                visual.lastY = er.posY;
                visual.lastZ = er.posZ;
                if (visual.sceneNode) {
                    visual.sceneNode->setPosition(irr::core::vector3df(
                        er.posX, er.posZ + visual.modelYOffset, er.posY));
                }
                if (visual.lightNode) {
                    visual.lightNode->setPosition(irr::core::vector3df(
                        er.posX, er.posZ + visual.modelYOffset + 3.0f, er.posY));
                }
                // Boat collision fixup — only checked when boats exist in zone
                if (entityRenderer_->hasBoatsInZone()) {
                    float boatZ = entityRenderer_->findBoatDeckZ(er.posX, er.posY, er.posZ);
                    if (boatZ > -999000.0f) {
                        float targetZ = boatZ;
                        if (std::abs(targetZ - er.posZ) < 20.0f) {
                            visual.lastZ = targetZ;
                            if (visual.sceneNode) {
                                visual.sceneNode->setPosition(irr::core::vector3df(
                                    er.posX, targetZ + visual.modelYOffset, er.posY));
                            }
                        }
                    }
                }
            }

            // Apply BSP region cache
            visual.cachedBspRegion = er.cachedBspRegion;
            visual.bspRegionDirty = er.bspRegionDirty;

            // Apply entity visibility (culling results)
            if (visual.sceneNode) {
                if (er.shouldBeVisible && !visual.inSceneGraph) {
                    smgr_->getRootSceneNode()->addChild(visual.sceneNode);
                    visual.sceneNode->setVisible(true);
                    visual.inSceneGraph = true;
                    if (visual.lightNode) {
                        smgr_->getRootSceneNode()->addChild(visual.lightNode);
                    }
                    // T111: checkProgressiveLoadingComplete()/queueEntityPrepRequests()
                    // only ever consider entities with inSceneGraph==true (this has
                    // always been true, not new to this unit) — which, per this
                    // function, is NOT "has a scene node" but "is currently within
                    // the visible-entity set" (the 40-entity render cap / render
                    // distance culling), a set that changes every frame as the
                    // player moves. An entity T109's load-distance gate deferred
                    // (meshBuilt=false) sits with inSceneGraph=false until it enters
                    // that set — at which point, if progressive loading already
                    // declared itself complete and went inactive, nothing would
                    // ever notice this entity became a real candidate. Re-arm here,
                    // exactly mirroring registerEntity()'s brand-new-spawn re-arm
                    // (renderer_entities.cpp) and gated the same way
                    // (entityPrepReady_ — the worker must exist first). Live-caught
                    // in this unit's own proof session: without this, a player
                    // walking toward deferred entities never picked them up because
                    // progressiveLoadingActive_ had already gone false long before
                    // they entered view.
                    if (!visual.meshBuilt && !progressiveLoadingActive_ && entityPrepReady_) {
                        progressiveLoadingActive_ = true;
                        LOG_DEBUG(MOD_GRAPHICS, "Progressive loading re-enabled: entity {} ({}) "
                                  "became visible while still unbuilt", visual.spawnId, visual.name);
                    }
                } else if (!er.shouldBeVisible && visual.inSceneGraph) {
                    visual.sceneNode->remove();
                    visual.inSceneGraph = false;
                    if (visual.lightNode) {
                        visual.lightNode->remove();
                    }
                }
            }
            if (visual.nameNode) {
                visual.nameNode->setVisible(er.nameTagVisible);
            }
        }
        entityRenderer_->setVisibleEntityCount(results->entityVisibleCount);
    }
}

std::vector<std::string> IrrlichtRenderer::getSimWorkerDebugInfo() const {
    std::vector<std::string> lines;

    if (!simulationWorker_) {
        lines.push_back("SimulationWorker: not created");
        return lines;
    }

    auto info = simulationWorker_->getDebugInfo();

    lines.push_back(fmt::format("SimulationWorker: {}",
        simulationWorker_->isRunning() ? "RUNNING" : "STOPPED"));
    lines.push_back(fmt::format("  Frames computed: {}", info.framesComputed));
    lines.push_back(fmt::format("  Frames skipped: {} ({:.1f}%)",
        info.framesSkipped,
        (info.framesComputed + info.framesSkipped) > 0
            ? 100.0f * info.framesSkipped / (info.framesComputed + info.framesSkipped)
            : 0.0f));
    lines.push_back(fmt::format("  Last compute: {:.2f}ms", info.lastComputeTimeMs));
    lines.push_back(fmt::format("  Avg compute: {:.2f}ms", info.avgComputeTimeMs));
    lines.push_back(fmt::format("  Worker busy: {}", info.workerBusy ? "YES" : "no"));
    // P32: tree count is always 0 now (AnimatedTreeManager's CPU-instancing
    // count removed — it was always 0 on GLES2 already, since that path was
    // only reachable via the removed EDT_BURNINGSVIDEO branch).
    lines.push_back(fmt::format("  Trees: {}, VertAnims: {}",
        0, vertexAnimatedMeshes_.size()));
    lines.push_back("  Offloaded: visibility, lighting, trees, vertex anims, fire flicker, boids, tumbleweeds");
    lines.push_back("  Ungated: detail wind, particles, sky, weather (30Hz)");

    return lines;
}

} // namespace Graphics
} // namespace EQT
