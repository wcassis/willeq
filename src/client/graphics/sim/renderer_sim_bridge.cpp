#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/sim/sim_bridge_component.h"
#include "client/graphics/frame/frame_component.h"  // B995: particleOptionKnobs()
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/placed_object_meshes.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace EQT {
namespace Graphics {

// L13: out of line so this TU's complete EQ::SpellVisualFX (included above)
// instantiates the unique_ptr deleter -- corrections 35 and 45.
SimBridgeComponent::SimBridgeComponent(RendererContext& ctx, IrrlichtRenderer& owner)
	: ctx_(ctx), owner_(owner) {}

SimBridgeComponent::~SimBridgeComponent() = default;

// ============================================================================
// SimulationWorker Integration
// ============================================================================

SimulationZoneData SimBridgeComponent::buildSimulationZoneData() {
    SimulationZoneData zoneData;
    zoneData.bspTree = ctx_.zoneBspTree;
    zoneData.usePvsCulling = ctx_.usePvsCulling;

    // Copy region bounding boxes (EQ Z-up coordinates)
    for (const auto& [regionIdx, bbox] : ctx_.regionBoundingBoxes) {
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

    // T490: the per-camera-region PVS candidate list is built on demand by
    // the worker (SimulationZoneData::pvsCandidatesFor) from the region's
    // RLE PVS, one camera region at a time. The whole-zone table this used to
    // build here was regions x visible-per-region entries.
    if (zoneData.usePvsCulling && zoneData.bspTree) {
        LOG_INFO(MOD_GRAPHICS, "PVS candidate lists: on demand per camera region, {} total region bounds",
                 zoneData.regionBounds.size());
    }

    // Copy object data
    // B938 (#256): per-placement state is a record, not a node. The record is
    // the placement itself -- in a PVS zone there is no node, but the placement
    // still exists and must be visibility-tested and drawn by the manual pass.
    // So hasNode means "a renderable placement exists" (record holds a mesh),
    // not "a scene node exists". The culling data (box, position, region) is
    // folded into the record now, so there are no parallel vectors to bound-check.
    zoneData.objects.resize(ctx_.objectRecords.size());
    for (size_t i = 0; i < ctx_.objectRecords.size(); ++i) {
        const auto& rec = ctx_.objectRecords[i];
        auto& od = zoneData.objects[i];
        od.hasNode = (rec.mesh != nullptr);
        od.boundingBox = rec.box;
        od.position = rec.position;
        od.bspRegion = rec.region;
    }

    // Copy zone light data
    zoneData.zoneLights.resize(ctx_.zoneLightData.size());
    for (size_t i = 0; i < ctx_.zoneLightData.size(); ++i) {
        auto& zld = zoneData.zoneLights[i];
        if (i < ctx_.zoneLightPositions.size())
            zld.position = ctx_.zoneLightPositions[i];
        zld.bspRegion = (i < ctx_.ZoneLoad().zoneLightRegions().size()) ? ctx_.ZoneLoad().zoneLightRegions()[i] : SIZE_MAX;
    }

    // Copy zone light node data for light selection (from zoneLightData_, no scene nodes)
    zoneData.zoneLightNodes.resize(ctx_.zoneLightData.size());
    for (size_t i = 0; i < ctx_.zoneLightData.size(); ++i) {
        auto& zlnd = zoneData.zoneLightNodes[i];
        if (i < ctx_.zoneLightPositions.size())
            zlnd.position = ctx_.zoneLightPositions[i];
        zlnd.diffuseColor = ctx_.zoneLightData[i].baseDiffuseColor;
        zlnd.radius = ctx_.zoneLightData[i].radius;
        zlnd.attConstant = ctx_.zoneLightData[i].attConstant;
        zlnd.attLinear = ctx_.zoneLightData[i].attLinear;
        zlnd.attQuadratic = ctx_.zoneLightData[i].attQuadratic;
    }

    // Copy zone light animation data (animated torches from WLD)
    if (ctx_.currentZone) {
        for (size_t i = 0; i < ctx_.currentZone->lights.size() && i < ctx_.zoneLightData.size(); ++i) {
            const auto& light = ctx_.currentZone->lights[i];
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
    zoneData.objectLights.resize(ctx_.objectLights.size());
    for (size_t i = 0; i < ctx_.objectLights.size(); ++i) {
        auto& old = zoneData.objectLights[i];
        old.position = ctx_.objectLights[i].position;
        old.originalColor = ctx_.objectLights[i].originalColor;
        old.isFireSource = ctx_.objectLights[i].isFireSource;
        old.flickerSpeed = ctx_.objectLights[i].flickerSpeed;
        old.objectName = ctx_.objectLights[i].objectName;
        old.bspRegion = ctx_.objectLights[i].bspRegion;
        if (ctx_.objectLights[i].node) {
            const auto& ld = ctx_.objectLights[i].node->getLightData();
            old.radius = ld.Radius;
            old.attConstant = ld.Attenuation.X;
            old.attLinear = ld.Attenuation.Y;
            old.attQuadratic = ld.Attenuation.Z;
        }
    }

    // Portal system (non-owning const pointer, immutable after zone load)
    if (ctx_.portalSystem) {
        zoneData.portalSystem = ctx_.portalSystem.get();
    }

    // Portal visibility config
    zoneData.enablePortalVis = ctx_.config.constrainedConfig.portalOcclusion;
    zoneData.enableStencilBuffer = ctx_.config.constrainedConfig.enableStencilBuffer;
    zoneData.indoorRegions = ctx_.indoorRegions;

    // Collision map for entity ground snapping (immutable after zone load)
    if (ctx_.collisionMap && ctx_.collisionMap->IsLoaded()) {
        zoneData.hcMap = ctx_.collisionMap;
    }

    // Hand the occluder sets to the worker (T504: moved, not copied -- the
    // renderer-side culler has no per-frame reader once the worker owns them).
    if (ctx_.occlusionCuller && ctx_.occlusionCuller->hasOccluders()) {
        zoneData.regionOccluders = ctx_.occlusionCuller->takeRegionOccluders();
        zoneData.occlusionConfig = ctx_.occlusionCuller->getConfig();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: handing {} region occluder sets to worker",
                 zoneData.regionOccluders.regionCount());
    }

    return zoneData;
}

void SimBridgeComponent::startSimulationWorkerEarly() {
    // S05: simulationWorker_ already created in initLoadingScreen()
    if (!owner_.simulationWorker_ || owner_.simulationWorker_->isRunning()) return;

    SimulationZoneData zoneData = buildSimulationZoneData();
    owner_.simulationWorker_->setZoneData(std::move(zoneData));
    owner_.simulationWorker_->start();

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker started early for zone '{}' (BSP/regions/objects/lights)",
             ctx_.currentZoneName);
}

// registerSimulationWorkerTreeData() removed (P32): it fed
// AnimatedTreeManager's CPU-instancing tree buffers (populated only via the
// removed EDT_BURNINGSVIDEO branch) into SimulationWorker::updateTreeData().
// Confirmed uncalled anywhere in the tree before deletion — dead code, went
// with the class.

void SimBridgeComponent::registerSimulationWorkerVertexAnimData() {
    if (!owner_.simulationWorker_ || !owner_.simulationWorker_->isRunning()) return;
    if (ctx_.vertexAnimatedMeshes.empty()) return;

    std::vector<SimulationZoneData::VertexAnimData> vertAnims(ctx_.vertexAnimatedMeshes.size());
    for (size_t i = 0; i < ctx_.vertexAnimatedMeshes.size(); ++i) {
        const auto& vam = ctx_.vertexAnimatedMeshes[i];
        auto& vad = vertAnims[i];
        // T504: only the timing goes to the worker (it advances a frame
        // counter); the frame data stays in the renderer's own entry.
        if (vam.animData && !vam.animData->frames.empty()) {
            vad.delayMs = vam.animData->delayMs;
            vad.frameCount = vam.animData->frames.size();
        }
    }

    owner_.simulationWorker_->updateVertexAnimData(std::move(vertAnims));
    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: registered {} vertex animated meshes", ctx_.vertexAnimatedMeshes.size());
}

void SimBridgeComponent::stopSimulationWorker() {
    if (owner_.simulationWorker_ && owner_.simulationWorker_->isRunning()) {
        owner_.simulationWorker_->stop();
        owner_.simulationWorker_->clearZoneData();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker stopped for zone transition");
    }
}

void SimBridgeComponent::postSimulationInput(float deltaTime) {
    if (!owner_.simulationWorker_ || !owner_.simulationWorker_->isRunning()) return;

    SimulationInput input;

    // Camera (Irrlicht Y-up)
    if (ctx_.camera) {
        input.cameraPos = ctx_.camera->getPosition();
        input.cameraTarget = ctx_.camera->getTarget();
    }

    // Camera position in EQ Z-up
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
    } else {
        input.frustumValid = false;
    }

    input.renderDistance = ctx_.renderDistance;
    input.loadingActive = ctx_.loadingScreenVisible || owner_.loading_.load(std::memory_order_relaxed);

    // Player (EQ Z-up)
    input.playerX = ctx_.playerX;
    input.playerY = ctx_.playerY;
    input.playerZ = ctx_.playerZ;
    input.playerHeading = ctx_.playerHeading;

    // Timing
    input.deltaTime = deltaTime;
    input.frameNumber = ctx_.frameNumber;

    // Environment
    input.currentHour = ctx_.currentHour;
    input.currentMinute = ctx_.currentMinute;
    input.timeOfDay = ctx_.currentHour + ctx_.currentMinute / 60.0f;

    // Player light
    input.playerLightLevel = playerLightLevel_;
    if (ctx_.Diagnostics().playerLightNode() && playerLightLevel_ > 0) {
        const auto& ld = ctx_.Diagnostics().playerLightNode()->getLightData();
        input.playerLightColor = ld.DiffuseColor;
        input.playerLightRadius = ld.Radius;
        input.playerLightAttConstant = ld.Attenuation.X;
        input.playerLightAttLinear = ld.Attenuation.Y;
        input.playerLightAttQuadratic = ld.Attenuation.Z;
    }

    // Vision and weather modifiers (for zone light animation colors)
    input.visionType = static_cast<uint8_t>(ctx_.currentVision);
    if (ctx_.weatherEffects && ctx_.weatherEffects->isEnabled()) {
        input.weatherAmbientModifier = ctx_.weatherEffects->getAmbientLightModifier();
    }

    // Tree wind state snapshot
    if (ctx_.treeWindController.isEnabled()) {
        const auto& wc = ctx_.treeWindController;
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
    if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized() && ctx_.skyRenderer->isEnabled()) {
        input.skyEnabled = true;
        input.skyInitialized = true;
        input.skyCloudScrollOffset = ctx_.skyRenderer->getCloudScrollOffset();
    }

    // Weather effects state snapshot
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

    // Occlusion camera state
    if (ctx_.frustumCuller && ctx_.frustumCuller->isEnabled()) {
        auto& oc = input.occlusionCamera;
        oc.fwdX = ctx_.frustumCuller->getFwdX();
        oc.fwdY = ctx_.frustumCuller->getFwdY();
        oc.fwdZ = ctx_.frustumCuller->getFwdZ();
        oc.rightX = ctx_.frustumCuller->getRightX();
        oc.rightY = ctx_.frustumCuller->getRightY();
        oc.rightZ = 0;  // Right vector is horizontal in EQ Z-up
        oc.upX = ctx_.frustumCuller->getUpX();
        oc.upY = ctx_.frustumCuller->getUpY();
        oc.upZ = ctx_.frustumCuller->getUpZ();
        oc.fovRadV = ctx_.frustumCuller->getFov();
        oc.aspect = ctx_.frustumCuller->getAspect();
        oc.enabled = true;
    }

    // Entity snapshots for worker interpolation
    if (ctx_.entityRenderer) {
        // Drain pending updates for worker
        auto drained = ctx_.entityRenderer->drainPendingUpdates();
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
        const auto& entities = ctx_.entityRenderer->getEntities();
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
        input.entityRenderDistance = ctx_.config.constrainedConfig.entityRenderDistance;
        input.maxVisibleEntities = ctx_.config.constrainedConfig.maxVisibleEntities;
        // B964: impostored entities do not count against the full-model cap;
        // the tier policy keeps the full models within maxVisibleEntities.
        if (ctx_.entityRenderer)
            input.maxVisibleEntities += ctx_.entityRenderer->impostorVisibilityAllowance();
        input.entityCullingEnabled = true;
        // B1071 (GitLab #391): the worker must never cull the current target
        // (PVS/frustum/distance/cap). It is read from the same source the
        // impostor pass pins on (Overlays().currentTargetId(), refreshed by
        // the event drain before this frame's input is posted), so the two
        // decisions can never disagree. Without it, a culled target is
        // removed from the scene graph and invisible on screen while the
        // target outline keeps painting its hull -- B1064's solid-silhouette
        // still (refactor/B1071/README.md).
        input.targetId = ctx_.Overlays().currentTargetId();

        // Name tag visibility config
        input.nameTagDistance = ctx_.entityRenderer->getNameTagDistance();
        input.nameTagsVisible = ctx_.entityRenderer->areNameTagsVisible();
    }

    // Vertex animation timing
    input.vertAnimDeltaMs = deltaTime * 1000.0f;

    // Particle system input
    if (ctx_.particleManager && ctx_.zoneReady) {
        auto& pi = input.particleInput;
        pi.deltaTime = deltaTime;
        if (ctx_.camera) {
            auto cp = ctx_.camera->getAbsolutePosition();
            pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
        }

        // Ambient color from zone shader
        if (ctx_.zoneShader) {
            const float* amb = ctx_.zoneShader->ambientColor();
            pi.ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
        }

        // Wind from particle manager's env state
        pi.windDirection = ctx_.particleManager->getWindDirection();
        pi.windStrength = ctx_.particleManager->getWindStrength();

        // Entity positions/directions resolved last frame
        pi.entityPositions = std::move(pendingParticleEntityPositions_);
        pi.entityDirections = std::move(pendingParticleEntityDirections_);

        // Commands from particle manager
        pi.commands = ctx_.particleManager->drainCommands();

        // State flags
        pi.fireEnabled = ctx_.particleManager->isUnifiedFireEnabled();
        // B995: the Options window's particle pages, read from the ONE record
        // (OptionsPanelState) every frame: the manager's two renderers apply
        // opacity / near clip at draw, the worker applies density at spawn.
        if (ctx_.frameComponent) {  // absent only in a component-level test
            const auto knobs = ctx_.Frame().particleOptionKnobs();
            ctx_.particleManager->setParticleKnobs(knobs);
            for (size_t f = 0; f < knobs.size(); ++f) pi.density[f] = knobs[f].density;
            // B1165 (#484): the Spell page's stage gate + creation density.
            if (spellVisualFX_) spellVisualFX_->setSpellFxOptions(ctx_.Frame().spellFxOptions());
        }
        pi.unifiedRendererInitialized = true;  // If we got here, it's initialized
        // T422 (board B375): `global.maxParticles` in config/spell_effects.json
        // had NO consumer.  This line used to send 0 with the comment "Worker
        // determines pool size from config", and the worker's own arm is
        // `if (poolSize <= 0) poolSize = 1024;` -- a hardcoded default that
        // happened to equal the shipped config value, so the knob looked
        // honoured and was inert.  T422 raised the impact/spellComplete burst
        // counts against the oracle (B226/B356) and the two of them together
        // exceed 1024, so a silently-clamped pool would have made every
        // measured count wrong.  0 is still accepted by the worker and is what
        // the headless tests pass.
        pi.poolSize = EQT::Graphics::Environment::SpellEffectsConfig::instance()
                          .getGlobal().maxParticles;

        // Weather lights: collect nearby lights for per-particle illumination
        if (ctx_.particleManager->isWeatherParticlesActive()) {
            float maxLightDist = 150.0f;
            float maxLightDistSq = maxLightDist * maxLightDist;
            float colorBoost = 2.5f;
            glm::vec3 camIrr = pi.cameraPos;

            for (size_t i = 0; i < ctx_.zoneLightData.size() && i < ctx_.zoneLightPositions.size(); ++i) {
                const auto& pos = ctx_.zoneLightPositions[i];
                float dx = pos.X - camIrr.x, dy = pos.Y - camIrr.y, dz = pos.Z - camIrr.z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq < maxLightDistSq) {
                    const auto& zld = ctx_.zoneLightData[i];
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
            if (ctx_.Diagnostics().playerLightNode() && playerLightLevel_ > 0) {
                auto pos = ctx_.Diagnostics().playerLightNode()->getPosition();
                auto& ld = ctx_.Diagnostics().playerLightNode()->getLightData();
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
    if (ctx_.boidsManager && ctx_.boidsManager->isEnabled() && ctx_.zoneReady) {
        auto& bi = input.boidsInput;
        bi.deltaTime = deltaTime;
        bi.playerPosition = glm::vec3(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
        bi.playerHeading = ctx_.playerHeading;
        bi.timeOfDay = ctx_.currentHour + ctx_.currentMinute / 60.0f;
        bi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        bi.commands = ctx_.boidsManager->drainCommands();
        bi.initialized = true;
    }

    // Tumbleweed system input
    if (ctx_.tumbleweedManager && ctx_.tumbleweedManager->isEnabled() && ctx_.zoneReady) {
        auto& ti = input.tumbleweedInput;
        ti.deltaTime = deltaTime;
        ti.playerPosition = glm::vec3(ctx_.playerX, ctx_.playerY, ctx_.playerZ);
        ti.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        ti.commands = ctx_.tumbleweedManager->drainCommands();
        ti.initialized = true;
    }

    // Weather system input
    if (ctx_.weatherSystem) {
        auto& wi = input.weatherInput;
        wi.deltaTime = deltaTime;
        wi.commands = ctx_.weatherSystem->drainCommands();
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
    if (ctx_.detailManager && ctx_.detailManager->isEnabled() && ctx_.zoneReady) {
        auto& dti = input.detailInput;
        dti.deltaTime = deltaTime;
        // Wind params from detail manager's wind controller and zone config
        const auto& wc = ctx_.detailManager->getWindController();
        const auto& wcParams = wc.getParams();
        dti.windStrength = ctx_.detailManager->getZoneConfig().windStrength;
        dti.windFrequency = wcParams.frequency;
        dti.gustFrequency = wcParams.gustFrequency;
        dti.gustStrength = wcParams.gustStrength;
        dti.windDirX = wcParams.direction.X;
        dti.windDirY = wcParams.direction.Y;
        // Disturbance params
        const auto& dc = ctx_.detailManager->getFoliageDisturbanceConfig();
        dti.disturbanceEnabled = dc.enabled;
        dti.playerRadius = dc.playerRadius;
        dti.playerStrength = dc.playerStrength;
        dti.maxDisplacement = dc.maxDisplacement;
        dti.verticalDipFactor = dc.verticalDipFactor;
        dti.velocityInfluence = dc.velocityInfluence;
        dti.heightExponent = dc.heightExponent;
        dti.recoveryRate = dc.recoveryRate;
        // Player state (Irrlicht Y-up coords)
        dti.playerPosX = ctx_.playerX;
        dti.playerPosY = ctx_.playerZ;  // EQ Z → Irrlicht Y
        dti.playerPosZ = ctx_.playerY;  // EQ Y → Irrlicht Z
        // Compute velocity for disturbance
        static float lastDetailPX = ctx_.playerX, lastDetailPY = ctx_.playerY;
        if (deltaTime > 0.001f) {
            dti.playerVelX = (ctx_.playerX - lastDetailPX) / deltaTime;
            dti.playerVelZ = (ctx_.playerY - lastDetailPY) / deltaTime;
        }
        lastDetailPX = ctx_.playerX;
        lastDetailPY = ctx_.playerY;
        dti.playerMoving = (dti.playerVelX * dti.playerVelX + dti.playerVelZ * dti.playerVelZ) > 0.1f;
        dti.commands = ctx_.detailManager->drainCommands();
        dti.initialized = true;
    }

    owner_.simulationWorker_->postInput(input);
}

void SimBridgeComponent::applySimulationResults() {
    if (!owner_.simulationWorker_ || !owner_.simulationWorker_->isRunning()) return;

    const SimulationOutput* results = owner_.simulationWorker_->swapAndGetResults();
    if (!results) return;

    // Bind the frame-scoped pointers FIRST, then apply values -- this
    // restores the pre-split ordering exactly (currentFrameSimResults_ was
    // set at old :581, before the apply call; regionPvsDepthMap_/
    // portalVisibleRegions_/particleRenderBuffer_ were set inline at their
    // old assignment points, all of which ran before applySimulationOutput()
    // returned). It matters because applySimulationOutput() reads
    // regionPvsDepthMap_ TRANSITIVELY: inside the `currentPvsRegion_ !=
    // prevPvsRegion` block it calls onPvsRegionChanged()
    // (renderer_zone_stream.cpp), which calls getRegionPvsDepth() (via
    // entityPrepWorker_->updateDepths() and gpuUploadThread_->reprioritize()),
    // which reads regionPvsDepthMap_ -- and this is exactly the PVS-boundary
    // moment the depth map changes, so binding after the call would have
    // fed that re-prioritization stale, previous-frame depths right when it
    // matters most. Checked (not merely by reading this function's body,
    // which is what the first version of this comment claimed and got
    // wrong): traced every function applySimulationOutput() calls
    // (rebuildPvsNeighborhood(), onPvsRegionChanged(), and its
    // entityRenderer_/particleManager_/spellVisualFX_/detailManager_/
    // weatherSystem_/skyRenderer_ callees) for reads of all four pointer
    // members; only the getRegionPvsDepth() path above reads any of them.
    // Binding first is safe because bindFrameSimulationBuffers()'s own gate
    // condition (usePvsCulling_ && !regionVisible.empty()) reads the same
    // `results`/`usePvsCulling_` state either order -- nothing between the
    // two calls mutates either.
    bindFrameSimulationBuffers(results);
    applySimulationOutput(*results);
}

// T116: value-only half of the old applySimulationOutput(). Stores no
// pointers anywhere -- safe to call with a SimulationOutput of ANY lifetime,
// including loadZoneSequential()'s Step12b stack local. If you are adding a
// member that stores a pointer/reference into `results`, it does NOT belong
// in this function -- it belongs in bindFrameSimulationBuffers() below,
// which only the runtime path (applySimulationResults()) may call.
void SimBridgeComponent::applySimulationOutput(const SimulationOutput& resultsRef) {
    const SimulationOutput* results = &resultsRef;

    // Apply PVS region visibility
    if (ctx_.usePvsCulling && !results->regionVisible.empty()) {
        // Build a lookup: index in regionBounds → regionIdx
        // We need to map worker output indices back to actual regionMeshNodes_
        size_t idx = 0;
        for (const auto& [regionIdx, node] : ctx_.regionMeshNodes) {
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

        size_t prevPvsRegion = ctx_.currentPvsRegion;
        ctx_.currentPvsRegion = results->currentPvsRegion;

        // T116: regionPvsDepthMap_ used to be pointed at results->regionPvsDepth
        // here. That store moved to bindFrameSimulationBuffers() -- see this
        // function's header comment. This function only applies values now.

        // Re-prioritize pending queues only when leaving the PVS neighborhood.
        // Small region crossings within the neighborhood are suppressed to avoid
        // expensive re-prioritization cascades on every boundary crossing.
        if (ctx_.currentPvsRegion != prevPvsRegion && prevPvsRegion != SIZE_MAX) {
            // T464 (B507/B508): the camera controller is no longer fed this
            // region -- it is the CAMERA's region, and the camera's collision
            // now looks up the player's eye region itself.
            // Full re-prioritization only when leaving neighborhood
            if (!ctx_.ZoneStream().isInPvsNeighborhood(ctx_.currentPvsRegion)) {
                ctx_.ZoneStream().rebuildPvsNeighborhood(ctx_.currentPvsRegion);
                ctx_.ZoneStream().onPvsRegionChanged();
            }
        }

        // Always log PVS state from worker (throttled to every 30 frames)
        static int simWorkerPvsLogCounter = 0;
        if (++simWorkerPvsLogCounter % 30 == 1 || ctx_.currentPvsRegion != prevPvsRegion) {
            LOG_DEBUG(MOD_GRAPHICS, "SimWorker apply: PVS={}{} meshLoadQ={} regionVis={}",
                      ctx_.currentPvsRegion,
                      (ctx_.currentPvsRegion != prevPvsRegion ? fmt::format(" (was {})", prevPvsRegion) : ""),
                      results->meshLoadQueue.size(),
                      results->regionVisible.size());
        }

        // T116: portalVisibleRegions_ used to be pointed at
        // results->portalVisibleRegions here. That store moved to
        // bindFrameSimulationBuffers() -- see this function's header comment.

        // Per-region portal occlusion: enable/disable based on camera's indoor status
        // When indoor region map is loaded, dynamically toggle portal occlusion as
        // the player moves between indoor and outdoor regions within the same zone.
        if (ctx_.portalOcclusionEligible && ctx_.config.constrainedConfig.portalOcclusion &&
            ctx_.config.constrainedConfig.enableStencilBuffer && !ctx_.indoorRegions.empty()) {
            bool wasEnabled = ctx_.portalOcclusionEnabled;
            ctx_.portalOcclusionEnabled = results->cameraInIndoorRegion;
            if (ctx_.portalOcclusionEnabled != wasEnabled) {
                LOG_DEBUG(MOD_GRAPHICS, "Portal occlusion {} (camera region {} is {})",
                          ctx_.portalOcclusionEnabled ? "ON" : "OFF",
                          ctx_.currentPvsRegion,
                          results->cameraInIndoorRegion ? "indoor" : "outdoor");
            }
        }

        // Mesh load queue / protected-regions population for the constrained
        // mesh cache removed T67 — was gated on constrainedMeshCache_, never
        // true (the cache was never constructed), so protectedRegions_ was
        // already permanently all-false in every shipped build (its only
        // writer lived here). meshLoadQueue_'s only readers were the same
        // dead-gated code and were removed alongside it. T292 then removed
        // protectedRegions_ itself, its dead progressive-load readers, and
        // SimulationOutput::protectedRegions (this function's log line was
        // that field's last consumer). See src/client/graphics/CLAUDE.md for
        // the reference pointer.

        // Apply sorted region draw list for front-to-back zone rendering
        if (ctx_.manualZoneDrawEnabled && !results->sortedRegions.empty()) {
            ctx_.sortedZoneDrawList.clear();
            ctx_.sortedZoneDrawList.reserve(results->sortedRegions.size());
            for (const auto& sr : results->sortedRegions) {
                auto it = ctx_.regionMeshNodes.find(sr.regionIdx);
                irr::scene::IMeshSceneNode* node = (it != ctx_.regionMeshNodes.end()) ? it->second : nullptr;
                ctx_.sortedZoneDrawList.push_back({sr.regionIdx, sr.distanceSq, node});
            }
        }
    }

    // Apply object visibility
    // When manualZoneDrawEnabled_, objects are drawn via drawZoneGeometrySorted()
    // alongside zone geometry for material sorting. Keep them out of the scene graph
    // to avoid double-rendering by drawAll().
    // B938 (#256): in a PVS zone the records carry no node -- the branch below is
    // exactly what kept them out of the graph, so it simply does nothing there.
    if (!results->objectVisible.empty() && !ctx_.manualZoneDrawEnabled) {
        for (size_t i = 0; i < results->objectVisible.size() && i < ctx_.objectRecords.size(); ++i) {
            auto& rec = ctx_.objectRecords[i];
            auto* node = rec.node;
            if (!node) continue;

            bool shouldBeVisible = results->objectVisible[i] != 0;
            bool isInScene = rec.inSceneGraph;

            if (shouldBeVisible && !isInScene) {
                ctx_.smgr->getRootSceneNode()->addChild(node);
                node->setVisible(true);
                rec.inSceneGraph = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                rec.inSceneGraph = false;
            }
        }
    }

    // Zone light visibility: no scene nodes to add/remove.
    // Worker uses lightVisible output internally for light selection filtering.

    // Apply light selection to shader
    if (results->activeLightCount > 0 && ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
        bool playerLightSet = false;
        for (int i = 0; i < results->activeLightCount; ++i) {
            const auto& sl = results->selectedLights[i];
            if (!sl.valid) continue;

            float boost = (i == 0 && sl.isPlayerLight) ? 3.0f : 1.0f;
            ctx_.zoneShader->setPointLight(i,
                sl.position.X, sl.position.Y, sl.position.Z,
                sl.diffuseColor.r * boost,
                sl.diffuseColor.g * boost,
                sl.diffuseColor.b * boost,
                sl.attConstant, sl.attLinear, sl.attQuadratic);

            // Set per-pixel player light FS uniforms (GLES2: computed in fragment shader)
            if (i == 0 && sl.isPlayerLight) {
                ctx_.zoneShader->setPlayerLightPos(sl.position.X, sl.position.Y, sl.position.Z);
                ctx_.zoneShader->setPlayerLightColor(
                    sl.diffuseColor.r * boost,
                    sl.diffuseColor.g * boost,
                    sl.diffuseColor.b * boost);
                ctx_.zoneShader->setPlayerLightAtten(sl.attConstant, sl.attLinear, sl.attQuadratic);
                playerLightSet = true;
            }
        }
        // Clear player light FS uniforms if no player light active
        if (!playerLightSet) {
            ctx_.zoneShader->setPlayerLightColor(0.0f, 0.0f, 0.0f);
        }

        // Apply debug toggles (/plight, /olight, L key)
        int effectiveLightCount = results->activeLightCount;

        // /plight off: zero player light in both VS (light[0]) and FS
        if (!ctx_.Diagnostics().debugPlayerLightEnabled()) {
            ctx_.zoneShader->setPointLight(0, 0,0,0, 0,0,0, 1,0,0);
            ctx_.zoneShader->setPlayerLightColor(0.0f, 0.0f, 0.0f);
            if (playerLightSet) effectiveLightCount--;
            playerLightSet = false;
        }

        // /olight off: zero all zone lights (indices 1-7) in VS
        if (!ctx_.Diagnostics().debugObjectLightsEnabled()) {
            for (int i = 1; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                ctx_.zoneShader->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = playerLightSet ? 1 : 0;
        } else if (ctx_.Diagnostics().maxObjectLights() < effectiveLightCount - (playerLightSet ? 1 : 0)) {
            // L key: limit zone light count, zero excess slots
            int maxZone = ctx_.Diagnostics().maxObjectLights();
            int startClear = (playerLightSet ? 1 : 0) + maxZone;
            for (int i = startClear; i < ZoneShaderManager::MAX_POINT_LIGHTS; ++i)
                ctx_.zoneShader->setPointLight(i, 0,0,0, 0,0,0, 1,0,0);
            effectiveLightCount = (playerLightSet ? 1 : 0) + maxZone;
        }

        ctx_.zoneShader->setNumPointLights(effectiveLightCount);
        activeLightCount_ = effectiveLightCount;
    }

    // Apply object light PVS visibility (scene graph add/remove)
    if (!results->objectLightVisible.empty()) {
        for (size_t i = 0; i < results->objectLightVisible.size() && i < ctx_.objectLights.size(); ++i) {
            auto* node = ctx_.objectLights[i].node;
            if (!node) continue;
            bool shouldBeVisible = results->objectLightVisible[i] != 0;
            bool isInScene = (i < ctx_.objectLightInSceneGraph.size()) ? ctx_.objectLightInSceneGraph[i] : false;
            if (shouldBeVisible && !isInScene) {
                ctx_.smgr->getRootSceneNode()->addChild(node);
                if (i < ctx_.objectLightInSceneGraph.size())
                    ctx_.objectLightInSceneGraph[i] = true;
            } else if (!shouldBeVisible && isInScene) {
                node->remove();
                if (i < ctx_.objectLightInSceneGraph.size())
                    ctx_.objectLightInSceneGraph[i] = false;
            }
        }
    }

    // Apply fire flicker colors to object lights (for other systems that read them)
    if (!results->objectLightColors.empty()) {
        for (size_t i = 0; i < results->objectLightColors.size() && i < ctx_.objectLights.size(); ++i) {
            auto* node = ctx_.objectLights[i].node;
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
    if (results->skyState.valid && ctx_.skyRenderer && ctx_.skyRenderer->isInitialized() && ctx_.skyRenderer->isEnabled()) {
        SkyRenderer::SkyState skyState;
        skyState.cloudScrollOffset = results->skyState.cloudScrollOffset;
        ctx_.skyRenderer->applyState(skyState);
    }

    // Apply weather system state from worker (must fire listeners before weatherEffects apply)
    if (results->weatherOutput.valid && ctx_.weatherSystem) {
        const auto& wo = results->weatherOutput;
        ctx_.weatherSystem->applyWorkerResults(wo.currentWeather, wo.targetWeather,
                                           wo.transitionProgress, wo.windIntensity,
                                           wo.weatherChanged, wo.newWeatherType);
    }

    // Apply weather effects state from worker
    if (results->weatherEffectsState.valid && ctx_.weatherEffects && ctx_.weatherEffects->isEnabled()) {
        WeatherEffectsController::WeatherEffectsState wes;
        wes.transitionProgress = results->weatherEffectsState.transitionProgress;
        wes.currentDarkening = results->weatherEffectsState.currentDarkening;
        wes.lightningFlashTimer = results->weatherEffectsState.lightningFlashTimer;
        wes.lightningBoltTimer = results->weatherEffectsState.lightningBoltTimer;
        wes.lightningActive = results->weatherEffectsState.lightningActive;
        wes.triggerLightningFlash = results->weatherEffectsState.triggerLightningFlash;
        // T423 (board B362): this is where the worker's advanced weather state
        // lands back on the render thread, so it is the weather WRITER as far
        // as the scene ambient is concerned. The modifier is not a pure
        // function of the OP_Weather packet -- a lightning flash lifts it for
        // flashDuration and drops it again, with no packet either way -- so the
        // mark is taken on the modifier actually MOVING rather than on every
        // apply, which would dirty the ambient every frame weather is enabled.
        const float modBefore = ctx_.weatherEffects->getAmbientLightModifier();
        ctx_.weatherEffects->applyState(wes);
        if (std::abs(ctx_.weatherEffects->getAmbientLightModifier() - modBefore) > 0.0005f) {
            owner_.markSceneAmbientDirty();
        }
    }

    // Update player light position (every frame, not gated)
    if (ctx_.Diagnostics().playerLightNode() && playerLightLevel_ > 0) {
        ctx_.Diagnostics().playerLightNode()->setPosition(irr::core::vector3df(ctx_.playerX, ctx_.playerZ + 3.0f, ctx_.playerY));
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
    if (ctx_.detailManager && ctx_.detailManager->isEnabled() && results->detailOutput.valid) {
        ctx_.detailManager->applyWorkerResults(results->detailOutput);
    }

    // Apply vertex animation results (frame index computed by worker). B936:
    // one entry per animated MESH, drawn by every placement of its geometry,
    // so this poses each geometry once (growthplane: 4 meshes, where fable
    // rewrote 4,314 private copies). The old per-placement `node->isVisible()`
    // skip is gone with the per-placement entry; it never skipped anything in
    // practice (objects leave the scene graph by remove(), which keeps
    // isVisible() true; only the diagnostics hide-all toggles cleared it).
    if (!results->vertexAnims.empty() && !ctx_.vertexAnimatedMeshes.empty()) {
        for (size_t i = 0; i < results->vertexAnims.size() && i < ctx_.vertexAnimatedMeshes.size(); ++i) {
            const auto& vr = results->vertexAnims[i];
            if (!vr.frameChanged) continue;
            applyVertexAnimFrame(ctx_.vertexAnimatedMeshes[i], vr.currentFrame);
        }
    }

    // Apply particle output from worker. T116: particleRenderBuffer_ used to
    // be pointed/nulled here; that store moved to bindFrameSimulationBuffers()
    // -- see this function's header comment. Everything else in this block
    // (active count, position resolution) is value application and stays.
    if (results->particleOutput.valid && ctx_.particleManager) {
        // Update particle manager's active count from worker
        ctx_.particleManager->setWorkerActiveCount(results->particleOutput.activeCount);

        // Resolve entity positions for the next frame's particle input
        pendingParticleEntityPositions_.clear();
        pendingParticleEntityDirections_.clear();

        for (uint16_t entityID : results->particleOutput.positionRequestEntities) {
            glm::vec3 pos;
            if (ctx_.particleManager->resolveEntityPosition(entityID, pos)) {
                pendingParticleEntityPositions_[entityID] = pos;
            }
        }

        // Resolve entity DIRECTIONS for the next frame's particle input.
        // T395's one-frame-behind rule applies to both: an emitter sees what
        // THIS output named, resolved now. B293 (#124):
        // directionRequestEntities was emitted by the worker (the spray half
        // of useDynamicDirection, SpellVisualFX::createCastGlow passing
        // isSpray through) and read back from pi.entityDirections, but no
        // loop ever resolved it -- pendingParticleEntityDirections_ was
        // cleared each frame and refilled with nothing, so
        // ParticleManager::entityDirCallback_ (installed by two callers)
        // was invoked by none and entity-direction-aware emission was
        // unreachable. The position arm above was the template; this one
        // mirrors it.
        for (uint16_t entityID : results->particleOutput.directionRequestEntities) {
            glm::vec3 dir;
            if (ctx_.particleManager->resolveEntityDirection(entityID, dir)) {
                pendingParticleEntityDirections_[entityID] = dir;
            }
        }
    }

    // Apply boids output from worker
    if (results->boidsOutput.valid && ctx_.boidsManager) {
        ctx_.boidsManager->applyWorkerResults(results->boidsOutput);
    }

    // Apply tumbleweed output from worker
    if (results->tumbleweedOutput.valid && ctx_.tumbleweedManager) {
        ctx_.tumbleweedManager->applyWorkerResults(results->tumbleweedOutput);
    }

    // Apply occlusion-culled regions from worker
    ctx_.occlusionCulledRegions = results->occlusionCulledRegions;

    // Apply entity interpolation and visibility results
    if (ctx_.entityRenderer && !results->entityResults.empty()) {
        auto& entities = const_cast<std::map<uint16_t, EntityVisual>&>(ctx_.entityRenderer->getEntities());

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
                if (ctx_.entityRenderer->hasBoatsInZone()) {
                    float boatZ = ctx_.entityRenderer->findBoatDeckZ(er.posX, er.posY, er.posZ);
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
                    ctx_.smgr->getRootSceneNode()->addChild(visual.sceneNode);
                    visual.sceneNode->setVisible(true);
                    visual.inSceneGraph = true;
                    if (visual.lightNode) {
                        ctx_.smgr->getRootSceneNode()->addChild(visual.lightNode);
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
                    if (!visual.meshBuilt && !ctx_.progressiveLoadingActive && ctx_.entityPrepReady) {
                        ctx_.progressiveLoadingActive = true;
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
                // B431: the PC/NPC name options narrow the sim's verdict.
                visual.nameNode->setVisible(er.nameTagVisible &&
                                            ctx_.entityRenderer->nameTagKindVisible(visual));
            }
        }
        ctx_.entityRenderer->setVisibleEntityCount(results->entityVisibleCount);
    }
}

// T116 (refactor/T116_simulationoutput_lifetime.md): the ONLY function
// allowed to store a pointer/reference into a SimulationOutput on a member.
// `persistent` MUST outlive every frame the bound members are read on --
// call this ONLY from applySimulationResults(), with the SimulationWorker's
// stable, persistent, double-buffered output_[2] pointer. If you are adding
// a new member that points into SimulationOutput, it goes here, guarded by
// the same condition its value-side counterpart in applySimulationOutput()
// uses (so a member never binds under conditions applySimulationOutput()
// itself wouldn't have populated it under) -- NEVER in applySimulationOutput()
// itself, which loadZoneSequential()'s Step12b calls with a stack local that
// does not outlive the frame. This is the fix for three separate dangling-
// pointer bugs (T111 x2, T115 x1) made in this exact spot, each patched
// after the fact by nulling the member again on the loading thread -- see
// the plan for why a fourth nulling line was rejected in favor of this split.
void SimBridgeComponent::bindFrameSimulationBuffers(const SimulationOutput* persistent) {
    owner_.currentFrameSimResults_ = persistent;

    // Mirrors applySimulationOutput()'s own regionPvsDepthMap_/
    // portalVisibleRegions_ value-application gate exactly (usePvsCulling_ &&
    // non-empty regionVisible) -- if that block didn't run, these two
    // pointers keep whatever they were bound to on the last frame that DID
    // run it (matches the pre-split behavior, which never nulled them here
    // either).
    if (ctx_.usePvsCulling && !persistent->regionVisible.empty()) {
        ctx_.regionPvsDepthMap = &persistent->regionPvsDepth;
        owner_.portalVisibleRegions_ = &persistent->portalVisibleRegions;
    }

    // Mirrors applySimulationOutput()'s particleOutput.valid && particleManager_
    // gate exactly, including the null-on-else (pre-split behavior: goes null
    // the instant particle output stops being valid, so stale-frame data is
    // never read).
    if (persistent->particleOutput.valid && ctx_.particleManager) {
        ctx_.particleRenderBuffer = &persistent->particleOutput.renderBuffer;
    } else {
        ctx_.particleRenderBuffer = nullptr;
    }
}

std::vector<std::string> SimBridgeComponent::getSimWorkerDebugInfo() const {
    std::vector<std::string> lines;

    if (!owner_.simulationWorker_) {
        lines.push_back("SimulationWorker: not created");
        return lines;
    }

    auto info = owner_.simulationWorker_->getDebugInfo();

    lines.push_back(fmt::format("SimulationWorker: {}",
        owner_.simulationWorker_->isRunning() ? "RUNNING" : "STOPPED"));
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
        0, ctx_.vertexAnimatedMeshes.size()));
    lines.push_back("  Offloaded: visibility, lighting, trees, vertex anims, fire flicker, boids, tumbleweeds");
    lines.push_back("  Ungated: detail wind, particles, sky, weather (30Hz)");

    return lines;
}

} // namespace Graphics
} // namespace EQT
