// IrrlichtRenderer diagnostics: scene dump, frame-timing profiling, memory report.
// Split out of irrlicht_renderer.cpp (F14) — verbatim move, no behavior change.

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/sky_renderer.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <chrono>
#include <set>

#ifdef EQT_HAS_GLES2
// Declared in irrlicht_renderer.cpp (H01: hit when building EQT_GLES2 on x86;
// pre-existing gap from the Batch F file split — these externs were only
// re-declared in the files that already needed them at the time).
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetHWBufferCount(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
#endif

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::dumpScene() const {
    LOG_INFO(MOD_GRAPHICS, "=== SCENE DUMP (dumpscene) ===");
    LOG_INFO(MOD_GRAPHICS, "  currentPvsRegion_={}, usePvsCulling_={}", currentPvsRegion_, usePvsCulling_);

    // Count Irrlicht scene graph nodes recursively
    std::function<int(irr::scene::ISceneNode*)> countNodes = [&](irr::scene::ISceneNode* node) -> int {
        if (!node) return 0;
        int count = 1;
        for (auto* child : node->getChildren()) count += countNodes(child);
        return count;
    };
    int totalSceneNodes = smgr_ ? countNodes(smgr_->getRootSceneNode()) - 1 : 0;  // -1 for root itself
    LOG_INFO(MOD_GRAPHICS, "  Total Irrlicht scene graph nodes: {}", totalSceneNodes);

    // --- Entities ---
    if (entityRenderer_) {
        const auto& entities = entityRenderer_->getEntities();
        size_t inGraph = 0, outGraph = 0;
        for (const auto& [id, ent] : entities) {
            if (ent.inSceneGraph) inGraph++; else outGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ENTITIES: {} total, {} in-graph, {} out-of-graph",
                 entities.size(), inGraph, outGraph);
        for (const auto& [id, ent] : entities) {
            const char* type = ent.isPlayer ? "PLAYER" : (ent.isNPC ? "NPC" : "PC");
            LOG_INFO(MOD_GRAPHICS, "    [{}] id={} '{}' pos=({:.1f},{:.1f},{:.1f}) bspRegion={} inGraph={} placeholder={}",
                     type, id, ent.name, ent.lastX, ent.lastY, ent.lastZ,
                     ent.cachedBspRegion == SIZE_MAX ? static_cast<int64_t>(-1) : static_cast<int64_t>(ent.cachedBspRegion),
                     ent.inSceneGraph, ent.usesPlaceholder);
        }
    } else {
        LOG_INFO(MOD_GRAPHICS, "  ENTITIES: no EntityRenderer");
    }

    // --- Doors ---
    if (doorManager_) {
        size_t inGraph = 0, outGraph = 0, total = doorManager_->getDoorCount();
        // Iterate door IDs 0-255
        for (uint16_t i = 0; i < 256; ++i) {
            const auto* door = doorManager_->getDoor(static_cast<uint8_t>(i));
            if (!door) continue;
            if (door->inSceneGraph) inGraph++; else outGraph++;
            LOG_INFO(MOD_GRAPHICS, "    [DOOR] id={} '{}' pos=({:.1f},{:.1f},{:.1f}) bspRegion={} inGraph={} meshBuilt={}",
                     door->doorId, door->modelName, door->x, door->y, door->z,
                     door->bspRegion == SIZE_MAX ? static_cast<int64_t>(-1) : static_cast<int64_t>(door->bspRegion),
                     door->inSceneGraph, door->meshBuilt);
        }
        LOG_INFO(MOD_GRAPHICS, "  DOORS: {} total, {} in-graph, {} out-of-graph", total, inGraph, outGraph);
    }

    // --- Objects ---
    {
        size_t inGraph = 0, outGraph = 0;
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (!objectNodes_[i]) continue;
            bool ig = (i < objectInSceneGraph_.size()) ? objectInSceneGraph_[i] : true;
            if (ig) inGraph++; else outGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  OBJECTS: {} total, {} in-graph, {} out-of-graph",
                 objectNodes_.size(), inGraph, outGraph);
    }

    // --- Zone Lights ---
    LOG_INFO(MOD_GRAPHICS, "  ZONE LIGHTS: {} total (no scene nodes, data-only)",
             zoneLightData_.size());

    // --- Zone Meshes ---
    {
        size_t zmInGraph = 0, zmOutGraph = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (!node) continue;
            if (node->getParent()) zmInGraph++; else zmOutGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  ZONE MESHES: {} total, {} in-graph, {} out-of-graph | manualDraw={}",
                 regionMeshNodes_.size(), zmInGraph, zmOutGraph, manualZoneDrawEnabled_);
    }

    // --- Animated Trees ---
    // P32: AnimatedTreeManager (CPU-instancing) removed. treeManager_ was
    // unconditionally constructed, so this always took the "0 total" branch
    // on GLES2 (animatedTrees_ was populated only via the removed
    // EDT_BURNINGSVIDEO path) — preserved verbatim as a fixed 0/0/0.
    size_t treeInGraph = 0, treeTotal = 0;
    LOG_INFO(MOD_GRAPHICS, "  ANIMATED TREES: {} total, {} in-graph, {} out-of-graph",
             treeTotal, treeInGraph, treeTotal - treeInGraph);

    // --- Object Lights ---
    {
        int olInGraph = 0;
        for (const auto& ol : objectLights_) {
            if (ol.node && ol.node->getParent()) olInGraph++;
        }
        LOG_INFO(MOD_GRAPHICS, "  OBJECT LIGHTS: {} total, {} in-graph", objectLights_.size(), olInGraph);
    }

    // --- Sky Renderer ---
    int skyNodeCount = 0;
    if (skyRenderer_) {
        skyNodeCount = skyRenderer_->getSceneNodeCount();
        LOG_INFO(MOD_GRAPHICS, "  SKY NODES: {}", skyNodeCount);
    }

    // --- Zone Collision Node ---
    bool collisionInGraph = zoneCollisionNode_ && zoneCollisionNode_->getParent();
    if (zoneCollisionNode_) {
        LOG_INFO(MOD_GRAPHICS, "  ZONE COLLISION NODE: in-graph={}", collisionInGraph);
    }

    // --- Weather Nodes ---
    int weatherNodeCount = 0;
    if (weatherEffects_) {
        weatherNodeCount = weatherEffects_->getSceneNodeCount();
        if (weatherNodeCount > 0) {
            LOG_INFO(MOD_GRAPHICS, "  WEATHER NODES: {}", weatherNodeCount);
        }
    }

    // --- Entity Lights ---
    int entityLightsInGraph = 0;
    if (entityRenderer_) {
        for (const auto& [id, ent] : entityRenderer_->getEntities()) {
            if (ent.lightNode && ent.lightNode->getParent()) entityLightsInGraph++;
        }
        if (entityLightsInGraph > 0) {
            LOG_INFO(MOD_GRAPHICS, "  ENTITY LIGHTS: {} in-graph", entityLightsInGraph);
        }
    }

    // --- Other nodes ---
    // Build a set of all known/tracked scene node pointers for diagnostic enumeration
    std::set<irr::scene::ISceneNode*> knownNodes;
    int otherNodes = totalSceneNodes;
    // Subtract known categories
    if (entityRenderer_) {
        for (const auto& [id, ent] : entityRenderer_->getEntities()) {
            if (ent.inSceneGraph && ent.sceneNode) {
                otherNodes -= countNodes(ent.sceneNode);  // entity + all descendants (children, grandchildren, etc.)
                knownNodes.insert(ent.sceneNode);
            }
            // Subtract entity light nodes (parented to ROOT, not to entity)
            if (ent.lightNode && ent.lightNode->getParent()) {
                otherNodes--;
                knownNodes.insert(ent.lightNode);
            }
        }
    }
    if (doorManager_) {
        for (uint16_t i = 0; i < 256; ++i) {
            const auto* door = doorManager_->getDoor(static_cast<uint8_t>(i));
            if (door && door->inSceneGraph) {
                otherNodes -= 2;  // pivot + mesh child
                if (door->pivotNode) knownNodes.insert(door->pivotNode);
            }
        }
    }
    for (size_t i = 0; i < objectNodes_.size(); ++i) {
        if (objectNodes_[i] && i < objectInSceneGraph_.size() && objectInSceneGraph_[i]) {
            otherNodes--;
            knownNodes.insert(objectNodes_[i]);
        }
    }
    // Zone lights have no scene nodes — nothing to subtract
    // Subtract zone mesh nodes that are in the scene graph
    for (const auto& [regionIdx, node] : regionMeshNodes_) {
        if (node && node->getParent()) {
            otherNodes--;
            knownNodes.insert(node);
        }
    }
    // Subtract object light nodes
    for (const auto& ol : objectLights_) {
        if (ol.node && ol.node->getParent()) {
            otherNodes--;
            knownNodes.insert(ol.node);
        }
    }
    // Subtract sky renderer nodes and collect their pointers
    otherNodes -= skyNodeCount;
    if (skyRenderer_) skyRenderer_->collectSceneNodes(knownNodes);
    // Subtract weather nodes and collect their pointers
    otherNodes -= weatherNodeCount;
    if (weatherEffects_) weatherEffects_->collectSceneNodes(knownNodes);
    // Subtract zone collision node
    if (collisionInGraph) { otherNodes--; knownNodes.insert(zoneCollisionNode_); }
    if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) { otherNodes--; knownNodes.insert(fallbackMeshNode_); }
    if (zonePlaceholderNode_) { otherNodes--; knownNodes.insert(zonePlaceholderNode_); }
    if (camera_) { otherNodes--; knownNodes.insert(camera_); }
    if (sunLight_) { otherNodes--; knownNodes.insert(sunLight_); }
    if (playerLightNode_) { otherNodes--; knownNodes.insert(playerLightNode_); }
    // P32: the animated-tree scene-node loop was removed along with
    // AnimatedTreeManager — animatedTrees_ (and this loop) was always empty
    // on GLES2, so it never inserted anything into knownNodes. treeInGraph
    // (always 0, see above) is kept for this subtraction's exact prior
    // arithmetic (a no-op).
    otherNodes -= static_cast<int>(treeInGraph);
    LOG_INFO(MOD_GRAPHICS, "  OTHER: camera={}, sunLight={}, playerLight={}, unaccounted={}",
             camera_ != nullptr, sunLight_ != nullptr, playerLightNode_ != nullptr, otherNodes);

    // Enumerate unaccounted nodes for diagnostics
    if (otherNodes != 0 && smgr_) {
        auto* root = smgr_->getRootSceneNode();
        int idx = 0;
        for (auto* child : root->getChildren()) {
            if (knownNodes.count(child) == 0) {
                // Not in any tracked category — identify by type
                const char* typeName = "unknown";
                switch (child->getType()) {
                    case irr::scene::ESNT_MESH: typeName = "mesh"; break;
                    case irr::scene::ESNT_LIGHT: typeName = "light"; break;
                    case irr::scene::ESNT_CAMERA: typeName = "camera"; break;
                    case irr::scene::ESNT_BILLBOARD: typeName = "billboard"; break;
                    case irr::scene::ESNT_ANIMATED_MESH: typeName = "animMesh"; break;
                    case irr::scene::ESNT_EMPTY: typeName = "empty"; break;
                    case irr::scene::ESNT_PARTICLE_SYSTEM: typeName = "particle"; break;
                    case irr::scene::ESNT_TEXT: typeName = "text"; break;
                    case irr::scene::ESNT_OCTREE: typeName = "octree"; break;
                    default: break;
                }
                auto pos = child->getPosition();
                const char* nodeName = child->getName() ? child->getName() : "";
                // For mesh nodes, log material/texture info to help identify
                std::string extraInfo;
                if (child->getType() == irr::scene::ESNT_MESH) {
                    auto* meshNode = static_cast<irr::scene::IMeshSceneNode*>(child);
                    auto matCount = meshNode->getMaterialCount();
                    extraInfo = fmt::format(" mats={}", matCount);
                    if (matCount > 0) {
                        auto& mat = meshNode->getMaterial(0);
                        if (mat.getTexture(0)) {
                            auto texName = std::string(mat.getTexture(0)->getName().getPath().c_str());
                            extraInfo += fmt::format(" tex='{}'", texName);
                        }
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "    UNACCOUNTED[{}]: type={} name='{}' visible={} pos=({:.1f},{:.1f},{:.1f}) children={}{}",
                         idx, typeName, nodeName, child->isVisible(), pos.X, pos.Y, pos.Z,
                         static_cast<int>(child->getChildren().size()), extraInfo);
                idx++;
            }
        }
    }

    // --- Fire Glow ---
    {
        unsigned int icoProgram = 0;
#ifdef EQT_HAS_DRM
        icoProgram = icosphereProgram_;
#endif
        LOG_INFO(MOD_GRAPHICS, "  FIRE GLOW: lights={}, lightingEnabled={}, icospheresEnabled={}, "
                 "maxLights={}, fireEffectsEnabled={}, icosphereProgram={}",
                 fireGlowLights_.size(), fireGlowLightingEnabled_, fireGlowIcospheresEnabled_,
                 maxFireGlowLights_, fireEffectsEnabled_, icoProgram);
    }

    // --- Visibility counts (from SimulationWorker output applied to scene) ---
    {
        size_t visRegions = 0, visObjects = 0;
        for (const auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && node->isVisible()) visRegions++;
        }
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i] && i < objectInSceneGraph_.size() && objectInSceneGraph_[i])
                visObjects++;
        }
        size_t visEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
        LOG_INFO(MOD_GRAPHICS, "  VISIBLE: regions={}/{}, objects={}/{}, lights={}, entities={}",
                 visRegions, regionMeshNodes_.size(),
                 visObjects, objectNodes_.size(),
                 activeLightCount_, visEntities);
    }

    LOG_INFO(MOD_GRAPHICS, "=== END SCENE DUMP ===");
}

int64_t IrrlichtRenderer::measureSection() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
    sectionStart_ = now;
    return us;
}

void IrrlichtRenderer::logFrameTimings() {
    if (frameTimingsSampleCount_ == 0) return;

    float avgTotal = static_cast<float>(frameTimingsAccum_.totalFrame) / frameTimingsSampleCount_;
    float fpsEstimate = avgTotal > 0 ? 1000000.0f / avgTotal : 0;

    // Calculate percentages
    auto pct = [&](int64_t val) -> float {
        return frameTimingsAccum_.totalFrame > 0 ?
            100.0f * static_cast<float>(val) / frameTimingsAccum_.totalFrame : 0;
    };

    // Calculate averages in microseconds
    auto avg = [&](int64_t val) -> float {
        return static_cast<float>(val) / frameTimingsSampleCount_;
    };

    LOG_INFO(MOD_GRAPHICS, "=== FRAME TIMING BREAKDOWN ({} frames, {:.1f} fps estimate) ===",
             frameTimingsSampleCount_, fpsEstimate);
    LOG_INFO(MOD_GRAPHICS, "  Total Frame:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.totalFrame), 100.0f);
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Input Handling:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.inputHandling), pct(frameTimingsAccum_.inputHandling));
    LOG_INFO(MOD_GRAPHICS, "    Player Movement:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.playerMovement), pct(frameTimingsAccum_.playerMovement));
    LOG_INFO(MOD_GRAPHICS, "  Camera Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.cameraUpdate), pct(frameTimingsAccum_.cameraUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Entity Update:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.entityUpdate), pct(frameTimingsAccum_.entityUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Door Update:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.doorUpdate), pct(frameTimingsAccum_.doorUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Spell VFX Update:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.spellVfxUpdate), pct(frameTimingsAccum_.spellVfxUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Animated Textures:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.animatedTextures), pct(frameTimingsAccum_.animatedTextures));
    LOG_INFO(MOD_GRAPHICS, "  Vertex Animations:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.vertexAnimations), pct(frameTimingsAccum_.vertexAnimations));
    LOG_INFO(MOD_GRAPHICS, "  Fire Flicker:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.fireFlicker), pct(frameTimingsAccum_.fireFlicker));
    LOG_INFO(MOD_GRAPHICS, "  Object Visibility:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectVisibility), pct(frameTimingsAccum_.objectVisibility));
    LOG_INFO(MOD_GRAPHICS, "  Zone Light Vis:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.zoneLightVisibility), pct(frameTimingsAccum_.zoneLightVisibility));
    LOG_INFO(MOD_GRAPHICS, "  PVS Visibility:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.pvsVisibility), pct(frameTimingsAccum_.pvsVisibility));
    LOG_INFO(MOD_GRAPHICS, "    Occlusion Cull:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.occlusionCulling), pct(frameTimingsAccum_.occlusionCulling));
    // "Mesh Loading" line removed T67 — was gated on constrainedMeshCache_,
    // never true (the cache was never constructed), so it never printed.
    LOG_INFO(MOD_GRAPHICS, "  Object Lights:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.objectLights), pct(frameTimingsAccum_.objectLights));
    LOG_INFO(MOD_GRAPHICS, "  Tier2 (Detail/Tree):{:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.tier2Update), pct(frameTimingsAccum_.tier2Update));
    LOG_INFO(MOD_GRAPHICS, "  Weather System:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.weatherSystemUpdate), pct(frameTimingsAccum_.weatherSystemUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Tier3 (Env/Sky):    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.tier3Update), pct(frameTimingsAccum_.tier3Update));
    LOG_INFO(MOD_GRAPHICS, "  HUD Update:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.hudUpdate), pct(frameTimingsAccum_.hudUpdate));
    LOG_INFO(MOD_GRAPHICS, "  Scene Draw All:     {:>8.0f} us ({:>5.1f}%)  [{} nodes, {} polys]",
             avg(frameTimingsAccum_.sceneDrawAll), pct(frameTimingsAccum_.sceneDrawAll),
             frameTimingsSampleCount_ > 0 ? frameTimingsAccum_.sceneNodeCount / frameTimingsSampleCount_ : 0,
             lastPolygonCount_);
    LOG_INFO(MOD_GRAPHICS, "    Animate+Register: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneAnimate), pct(frameTimingsAccum_.sceneAnimate));
    LOG_INFO(MOD_GRAPHICS, "    Solid Pass:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneSolid), pct(frameTimingsAccum_.sceneSolid));
    if (manualZoneDrawEnabled_) {
        LOG_INFO(MOD_GRAPHICS, "    Manual Zone Draw: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.manualZoneDraw), pct(frameTimingsAccum_.manualZoneDraw));
    }
    LOG_INFO(MOD_GRAPHICS, "    Transparent Pass: {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneTransparent), pct(frameTimingsAccum_.sceneTransparent));
    LOG_INFO(MOD_GRAPHICS, "    Skybox Pass:      {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneSkybox), pct(frameTimingsAccum_.sceneSkybox));
    LOG_INFO(MOD_GRAPHICS, "    Other Passes:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.sceneOther), pct(frameTimingsAccum_.sceneOther));
    LOG_INFO(MOD_GRAPHICS, "  Target Box:         {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.targetBox), pct(frameTimingsAccum_.targetBox));
    LOG_INFO(MOD_GRAPHICS, "  Particles:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.particles), pct(frameTimingsAccum_.particles));
    LOG_INFO(MOD_GRAPHICS, "  Boids:              {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.boids), pct(frameTimingsAccum_.boids));
    LOG_INFO(MOD_GRAPHICS, "  Weather Render:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.weatherRender), pct(frameTimingsAccum_.weatherRender));
    LOG_INFO(MOD_GRAPHICS, "  Debug Overlays:     {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.debugOverlays), pct(frameTimingsAccum_.debugOverlays));
    LOG_INFO(MOD_GRAPHICS, "  Casting Bars:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.castingBars), pct(frameTimingsAccum_.castingBars));
    LOG_INFO(MOD_GRAPHICS, "  GUI Draw All:       {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.guiDrawAll), pct(frameTimingsAccum_.guiDrawAll));
    LOG_INFO(MOD_GRAPHICS, "  Static UI:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.staticUI), pct(frameTimingsAccum_.staticUI));
    LOG_INFO(MOD_GRAPHICS, "  Zone Line Overlay:  {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.zoneLineOverlay), pct(frameTimingsAccum_.zoneLineOverlay));
    LOG_INFO(MOD_GRAPHICS, "  Footprint Render:   {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.footprintRender), pct(frameTimingsAccum_.footprintRender));
    LOG_INFO(MOD_GRAPHICS, "  Post Render:        {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.postRender), pct(frameTimingsAccum_.postRender));
    LOG_INFO(MOD_GRAPHICS, "  SimWorker Apply:    {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.simWorkerApply), pct(frameTimingsAccum_.simWorkerApply));
    LOG_INFO(MOD_GRAPHICS, "  glFinish:           {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.glFinish), pct(frameTimingsAccum_.glFinish));
    LOG_INFO(MOD_GRAPHICS, "  End Scene:          {:>8.0f} us ({:>5.1f}%)", avg(frameTimingsAccum_.endScene), pct(frameTimingsAccum_.endScene));
    LOG_INFO(MOD_GRAPHICS, "  Manual Zone Draw:   {} regions avg, {} drawCalls avg",
             frameTimingsAccum_.manualZoneRegionsDrawn / std::max(1, frameTimingsSampleCount_),
             frameTimingsAccum_.manualZoneDrawCalls / std::max(1, frameTimingsSampleCount_));
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Render EMA:         {:>8.0f} us | EssSim EMA: {:>6.0f} us",
             renderCostAvgUs_, essentialSimCostAvgUs_);
    if (governor_) {
        LOG_INFO(MOD_GRAPHICS, "  Governor: {} | avg {:.1f}ms | target {:.1f}ms | ratio {:.2f}{}",
                 governor_->getStateName(),
                 governor_->getAverageFrameTimeMs(),
                 governor_->getTargetFrameTimeMs(),
                 governor_->getBudgetRatio(),
                 governor_->isForced() ? " (FORCED)" : "");
    }
}

void IrrlichtRenderer::runSceneProfile() {
    sceneProfileEnabled_ = true;
    // Wait 60 frames before profiling to allow constrained visibility to run
    sceneProfileFrameCount_ = -60;
    LOG_INFO(MOD_GRAPHICS, "Scene profile scheduled - will run after 60 frames");
}

void IrrlichtRenderer::profileSceneBreakdown() {
    if (!driver_ || !smgr_) return;

    SceneBreakdown breakdown;
    const int numSamples = 10;  // More samples for stability

    // Helper to draw scene and return (time_us, poly_count)
    auto timeAndPolyDrawAll = [this]() -> std::pair<int64_t, uint32_t> {
        if (zoneShader_) zoneShader_->beginFrame();
        auto start = std::chrono::steady_clock::now();
        smgr_->drawAll();
        auto end = std::chrono::steady_clock::now();
        int64_t time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        uint32_t polys = driver_->getPrimitiveCountDrawn();
        return {time_us, polys};
    };

    // Helper to hide all scene content including lights
    auto hideAll = [this]() {
        if (zoneMeshNode_) zoneMeshNode_->setVisible(false);
        if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(false);
        // Hide objects (they may already be out of scene graph)
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i]) objectNodes_[i]->setVisible(false);
        }
        if (doorManager_) doorManager_->setAllDoorsVisible(false);
        // Zone lights have no scene nodes — nothing to hide
        if (sunLight_) sunLight_->setVisible(false);
        if (playerLightNode_) playerLightNode_->setVisible(false);
    };

    auto showAll = [this]() {
        if (zoneMeshNode_) zoneMeshNode_->setVisible(true);
        if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(true);
        // Add objects back to scene graph and show
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            if (objectNodes_[i]) {
                if (i < objectInSceneGraph_.size() && !objectInSceneGraph_[i]) {
                    smgr_->getRootSceneNode()->addChild(objectNodes_[i]);
                    objectInSceneGraph_[i] = true;
                }
                objectNodes_[i]->setVisible(true);
            }
        }
        if (doorManager_) doorManager_->setAllDoorsVisible(true);
        // Zone lights have no scene nodes — nothing to show
        if (sunLight_) sunLight_->setVisible(true);
        if (playerLightNode_) playerLightNode_->setVisible(true);
    };

    // Count nodes
    breakdown.entityCount = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
    breakdown.objectCount = static_cast<int>(objectNodes_.size());
    breakdown.doorCount = doorManager_ ? static_cast<int>(doorManager_->getDoorCount()) : 0;
    int lightCount = static_cast<int>(zoneLightData_.size()) + (sunLight_ ? 1 : 0) + (playerLightNode_ ? 1 : 0);

    // Count total scene nodes recursively
    std::function<int(irr::scene::ISceneNode*)> countNodes = [&](irr::scene::ISceneNode* node) -> int {
        if (!node) return 0;
        int count = 1;
        const auto& children = node->getChildren();
        for (auto* child : children) {
            count += countNodes(child);
        }
        return count;
    };
    int totalSceneNodes = countNodes(smgr_->getRootSceneNode());

    LOG_INFO(MOD_GRAPHICS, "=== SCENE BREAKDOWN PROFILE ===");
    LOG_INFO(MOD_GRAPHICS, "Zone mesh node: {}", zoneMeshNode_ ? "valid" : "NULL");
    LOG_INFO(MOD_GRAPHICS, "Total scene nodes: {} (lights: {})", totalSceneNodes, lightCount);
    LOG_INFO(MOD_GRAPHICS, "Measuring each category in isolation ({} samples each)...", numSamples);

    // Hide everything first
    hideAll();

    // 1. Measure baseline (nothing visible - just scene graph overhead)
    int64_t baselineSum = 0;
    uint32_t baselinePolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        baselineSum += time;
        baselinePolys = polys;  // Should be 0 or near 0
        driver_->endScene();
    }
    int64_t baseline = baselineSum / numSamples;

    // 2. Measure zone mesh only
    if (zoneMeshNode_) zoneMeshNode_->setVisible(true);
    int64_t zoneSum = 0;
    uint32_t zonePolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        zoneSum += time;
        zonePolys = polys;  // Keep last sample's poly count
        driver_->endScene();
    }
    breakdown.zoneTime = (zoneSum / numSamples) - baseline;
    breakdown.zonePolys = static_cast<int>(zonePolys - baselinePolys);
    if (zoneMeshNode_) zoneMeshNode_->setVisible(false);

    // 3. Measure entities only
    if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(true);
    int64_t entitySum = 0;
    uint32_t entityPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        entitySum += time;
        entityPolys = polys;
        driver_->endScene();
    }
    breakdown.entityTime = (entitySum / numSamples) - baseline;
    int entityPolyCount = static_cast<int>(entityPolys - baselinePolys);
    if (entityRenderer_) entityRenderer_->setAllEntitiesVisible(false);

    // 4. Measure objects only
    for (auto* node : objectNodes_) if (node) node->setVisible(true);
    int64_t objectSum = 0;
    uint32_t objectPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        objectSum += time;
        objectPolys = polys;
        driver_->endScene();
    }
    breakdown.objectTime = (objectSum / numSamples) - baseline;
    int objectPolyCount = static_cast<int>(objectPolys - baselinePolys);
    for (auto* node : objectNodes_) if (node) node->setVisible(false);

    // 5. Measure doors only
    if (doorManager_) doorManager_->setAllDoorsVisible(true);
    int64_t doorSum = 0;
    uint32_t doorPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        doorSum += time;
        doorPolys = polys;
        driver_->endScene();
    }
    breakdown.doorTime = (doorSum / numSamples) - baseline;
    int doorPolyCount = static_cast<int>(doorPolys - baselinePolys);
    if (doorManager_) doorManager_->setAllDoorsVisible(false);

    // 6. Measure lights only (zone lights have no scene nodes, just sun + player light)
    if (sunLight_) sunLight_->setVisible(true);
    if (playerLightNode_) playerLightNode_->setVisible(true);
    int64_t lightSum = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        lightSum += time;
        driver_->endScene();
    }
    int64_t lightTime = (lightSum / numSamples) - baseline;
    if (sunLight_) sunLight_->setVisible(false);
    if (playerLightNode_) playerLightNode_->setVisible(false);

    // 7. Measure full scene
    showAll();
    int64_t totalSum = 0;
    uint32_t totalPolys = 0;
    for (int i = 0; i < numSamples; i++) {
        driver_->beginScene(true, true, irr::video::SColor(255, 50, 50, 80));
        auto [time, polys] = timeAndPolyDrawAll();
        totalSum += time;
        totalPolys = polys;
        driver_->endScene();
    }
    breakdown.totalDrawAll = totalSum / numSamples;

    // Calculate "other" time (scene overhead not captured by individual categories)
    int64_t measuredTotal = breakdown.zoneTime + breakdown.entityTime +
                            breakdown.objectTime + breakdown.doorTime + baseline;
    breakdown.otherTime = breakdown.totalDrawAll - measuredTotal;
    if (breakdown.otherTime < 0) breakdown.otherTime = 0;

    // Calculate percentages
    auto pct = [&](int64_t val) -> float {
        return breakdown.totalDrawAll > 0 ? 100.0f * val / breakdown.totalDrawAll : 0;
    };

    LOG_INFO(MOD_GRAPHICS, "");
    LOG_INFO(MOD_GRAPHICS, "Scene contents (from driver polygon count):");
    LOG_INFO(MOD_GRAPHICS, "  Zone mesh:    {:>6} polys", breakdown.zonePolys);
    LOG_INFO(MOD_GRAPHICS, "  Entities:     {:>6} polys ({} nodes)", entityPolyCount, breakdown.entityCount);
    LOG_INFO(MOD_GRAPHICS, "  Objects:      {:>6} polys ({} nodes)", objectPolyCount, breakdown.objectCount);
    LOG_INFO(MOD_GRAPHICS, "  Doors:        {:>6} polys ({} nodes)", doorPolyCount, breakdown.doorCount);
    LOG_INFO(MOD_GRAPHICS, "  Lights:       {:>6} nodes", lightCount);
    LOG_INFO(MOD_GRAPHICS, "  Total:        {:>6} polys ({} scene nodes)", totalPolys, totalSceneNodes);
    LOG_INFO(MOD_GRAPHICS, "");
    LOG_INFO(MOD_GRAPHICS, "Render time breakdown (avg of {} samples):", numSamples);
    LOG_INFO(MOD_GRAPHICS, "  Total drawAll:  {:>8} us (100.0%)", breakdown.totalDrawAll);
    LOG_INFO(MOD_GRAPHICS, "  ----------------------------------------");
    LOG_INFO(MOD_GRAPHICS, "  Zone mesh:      {:>8} us ({:>5.1f}%)", breakdown.zoneTime, pct(breakdown.zoneTime));
    LOG_INFO(MOD_GRAPHICS, "  Entities:       {:>8} us ({:>5.1f}%)", breakdown.entityTime, pct(breakdown.entityTime));
    LOG_INFO(MOD_GRAPHICS, "  Objects:        {:>8} us ({:>5.1f}%)", breakdown.objectTime, pct(breakdown.objectTime));
    LOG_INFO(MOD_GRAPHICS, "  Doors:          {:>8} us ({:>5.1f}%)", breakdown.doorTime, pct(breakdown.doorTime));
    LOG_INFO(MOD_GRAPHICS, "  Lights:         {:>8} us ({:>5.1f}%)", lightTime, pct(lightTime));
    LOG_INFO(MOD_GRAPHICS, "  Baseline:       {:>8} us ({:>5.1f}%)", baseline, pct(baseline));
    LOG_INFO(MOD_GRAPHICS, "  Interaction:    {:>8} us ({:>5.1f}%)", breakdown.otherTime, pct(breakdown.otherTime));
    LOG_INFO(MOD_GRAPHICS, "=== END SCENE BREAKDOWN ===");

    sceneProfileEnabled_ = false;
}

// ============================================================================
// Memory Report
// ============================================================================

static std::string formatBytes(size_t bytes) {
    if (bytes >= 1024 * 1024)
        return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return fmt::format("{:.1f} KB", bytes / 1024.0);
    return fmt::format("{} B", bytes);
}

MemoryReportBreakdown IrrlichtRenderer::getMemoryReportBreakdown(const MemoryReportInput& ext) const {
    MemoryReportBreakdown b;

    // --- Texture Cache ---
    // Note: texture cache textures are created via driver->addTexture() and already
    // counted in GPU Textures below — do NOT add to totalEstimate here.
    if (constrainedTextureCache_) {
        b.textureCacheActive = true;
        b.textureCacheBytesUsed = constrainedTextureCache_->getCurrentUsage();
        b.textureCacheBytesLimit = constrainedTextureCache_->getMemoryLimit();
        b.textureCacheCount = constrainedTextureCache_->getTextureCount();
    }

    // --- EQE1 Flat Packs (P04) ---
    // Named suspect for the "untracked RSS" gap: before P04 this pool wasn't
    // reported anywhere, and the v1 loader heap-loaded every opened archive
    // (including all never-referenced global archives) as pinned anonymous
    // memory. Now mmap'd (page-cache backed, reclaimable) and only opened
    // on demand, but still worth naming explicitly since it can still be a
    // multi-MB resident pool for zones that touch many global archives.
    //
    // T108: eqe1MappedBytes is MAPPED (address-space) size, not RESIDENT
    // (RSS) size -- mmap'd/page-cache-backed archives are only as resident
    // as the OS has actually faulted in. T105 measured EQE1 archives at
    // 1.6-10.6% resident (e.g. lgequip.eqe1: 23.6MB mapped, 64KB resident).
    // Deliberately NOT added to totalEstimate: doing so previously credited
    // ~110-120MB of never-faulted-in address space as "accounted for",
    // which silently deflated untrackedBytes (RSS - totalEstimate) by the
    // same amount -- see T106/T107's Reviews for the corrected untracked
    // percentages (75-90% on x86, 32.8% on the Pi, vs. the ~34-36% the
    // inflated total produced). Reported on its own line below (mapped, not
    // resident -- callers reading this field must not treat it as RSS).
    {
        b.eqe1MappedBytes = getEQE1MappedBytesTotal();
        {
            std::lock_guard<std::mutex> lock(eqe1Mutex_);
            for (const auto& [name, loader] : eqe1Loaders_) {
                if (loader) ++b.eqe1ArchiveCount;
            }
        }
        // Intentionally NOT: b.totalEstimate += b.eqe1MappedBytes; (T108)
    }

    // "Mesh Cache" section removed T67 — was gated on constrainedMeshCache_,
    // never true (the cache was never constructed), so it never printed.
    // See src/client/graphics/CLAUDE.md for the reference pointer.

    // --- Zone Geometry + Placeable Objects ---
    {
        // Estimate zone mesh memory from Irrlicht mesh buffers
        size_t zoneMeshBytes = 0;
        if (zoneMeshNode_ && zoneMeshNode_->getMesh()) {
            auto* mesh = zoneMeshNode_->getMesh();
            for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                auto* buf = mesh->getMeshBuffer(i);
                zoneMeshBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                zoneMeshBytes += buf->getIndexCount() * sizeof(uint16_t);
            }
        }
        // PVS region meshes
        size_t pvsBytes = 0;
        for (const auto& [id, node] : regionMeshNodes_) {
            if (node && node->getMesh()) {
                auto* mesh = node->getMesh();
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    auto* buf = mesh->getMeshBuffer(i);
                    pvsBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                    pvsBytes += buf->getIndexCount() * sizeof(uint16_t);
                }
            }
        }

        b.zoneGeometryBytes = usePvsCulling_ ? pvsBytes : zoneMeshBytes;
        b.totalEstimate += zoneMeshBytes + pvsBytes;

        // Placeable objects
        for (auto* node : objectNodes_) {
            if (node && node->getMesh()) {
                auto* mesh = node->getMesh();
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    auto* buf = mesh->getMeshBuffer(i);
                    b.placeableObjectsBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                    b.placeableObjectsBytes += buf->getIndexCount() * sizeof(uint16_t);
                }
            }
        }
        b.totalEstimate += b.placeableObjectsBytes;
    }

    // --- Entity Renderer (per-instance data) ---
    if (entityRenderer_) {
        b.entityInstanceBytes = entityRenderer_->getPerInstanceMemoryBytes();
        b.totalEstimate += b.entityInstanceBytes;
    }

    // --- Character Models ---
    if (entityRenderer_) {
        if (auto* rml = entityRenderer_->getRaceModelLoader()) {
            auto stats = rml->getMemoryStats();
            size_t rmlTexTotal = stats.globalTextureBytes + stats.numberedTextureBytes
                            + stats.armorTextureBytes + stats.zoneTextureBytes
                            + stats.otherChrTextureBytes;
            b.characterModelsBytes = rmlTexTotal + stats.modelGeometryBytes + stats.modelSkeletonBytes
                            + stats.characterModelBytes + stats.animatedMeshBytes
                            + stats.irrlichtMeshBytes;
            b.totalEstimate += b.characterModelsBytes;
        }
    }

    // --- Equipment Models ---
    if (entityRenderer_) {
        if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
            auto stats = eml->getMemoryStats();
            b.equipmentModelsBytes = stats.rawTextureBytes + stats.geometryBytes
                            + stats.irrlichtMeshBytes + stats.indexBytes;
            b.totalEstimate += b.equipmentModelsBytes;
        }
    }

    // --- S3D Zone Source Data (CPU-side geometry, BSP, animations kept for mesh rebuilds) ---
    if (currentZone_) {
        b.s3dZoneSourceBytes = currentZone_->getMemoryUsage();
        b.totalEstimate += b.s3dZoneSourceBytes;
    }

    // --- GPU Texture Memory (actual GPU-side allocations in shared RAM) ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        b.gpuTextureBytes = gles2GetGpuTextureMemoryUsage(driver_);
        b.totalEstimate += b.gpuTextureBytes;
    }
#endif

    // --- HCMap / Collision ---
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        auto mapStats = collisionMap_->GetMemoryStats();
        b.hcmapBytes = mapStats.totalBytes;
        b.totalEstimate += b.hcmapBytes;
    }

    // --- Framebuffer ---
    b.framebufferBudgetBytes = config_.constrainedConfig.framebufferMemoryBytes;
    if (driver_) {
        auto screenSize = driver_->getScreenSize();
        b.framebufferBytes = config_.constrainedConfig.calculateFramebufferUsage(
            static_cast<int>(screenSize.Width), static_cast<int>(screenSize.Height));
        b.totalEstimate += b.framebufferBytes;
    }

    // --- VBO/EBO GPU Buffers ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        size_t hwbCount = gles2GetHWBufferCount(driver_);
        if (hwbCount > 0) {
            b.vboEboBytes = gles2GetHWBufferMemoryUsage(driver_);
            b.totalEstimate += b.vboEboBytes;
        }
    }
#endif

    // ============================================================
    // Audio (from external input)
    // ============================================================
    if (ext.audioAvailable) {
        b.sfxCacheBytes = ext.sfxCacheBytes;
        b.totalEstimate += b.sfxCacheBytes;

        b.soundBufferCacheBytes = ext.soundBufferCacheBytes;
        b.totalEstimate += b.soundBufferCacheBytes;

        if (ext.soundFontEstimateBytes > 0) {
            b.soundFontEstimateBytes = ext.soundFontEstimateBytes;
            b.totalEstimate += b.soundFontEstimateBytes;
        }
        if (ext.musicDecodedBytes > 0) {
            b.musicDecodedBytes = ext.musicDecodedBytes;
            b.totalEstimate += b.musicDecodedBytes;
        }
        if (ext.audioPfsArchiveBytes > 0) {
            b.audioPfsArchiveBytes = ext.audioPfsArchiveBytes;
            b.totalEstimate += b.audioPfsArchiveBytes;
        }
    }

    // ============================================================
    // Game Data (from external input)
    // ============================================================
    if (ext.entityCount > 0) {
        b.entityEstimateBytes = ext.entityEstimateBytes;
        b.totalEstimate += b.entityEstimateBytes;
    }
    if (ext.doorCount > 0) {
        b.doorEstimateBytes = ext.doorEstimateBytes;
        b.totalEstimate += b.doorEstimateBytes;
    }
    if (ext.spellDbCount > 0) {
        b.spellDbEstimateBytes = ext.spellDbEstimateBytes;
        b.totalEstimate += b.spellDbEstimateBytes;
    }

    // --- Total / Untracked RSS ---
    b.processRssBytes = ext.processRssBytes;
    if (ext.processRssBytes > 0 && ext.processRssBytes > b.totalEstimate) {
        b.untrackedBytes = ext.processRssBytes - b.totalEstimate;
    }

    return b;
}

std::vector<std::string> IrrlichtRenderer::getMemoryReport(const MemoryReportInput& ext) const {
    std::vector<std::string> lines;
    const MemoryReportBreakdown b = getMemoryReportBreakdown(ext);

    lines.push_back("=== Memory Usage Report ===");
    lines.push_back("");
    lines.push_back("--- Graphics ---");

    // --- Texture Cache ---
    if (b.textureCacheActive) {
        lines.push_back(fmt::format("[Texture Cache] {} / {} ({} textures, GPU cost in GPU Textures)",
            formatBytes(b.textureCacheBytesUsed), formatBytes(b.textureCacheBytesLimit), b.textureCacheCount));
        // Hit/miss/eviction counters are live cache diagnostics, not part of
        // the byte tally — read directly from the cache rather than
        // threading them through the struct.
        if (constrainedTextureCache_) {
            lines.push_back(fmt::format("  Hits: {}  Misses: {}  Hit rate: {:.1f}%  Evictions: {}",
                constrainedTextureCache_->getCacheHits(),
                constrainedTextureCache_->getCacheMisses(),
                constrainedTextureCache_->getHitRate(),
                constrainedTextureCache_->getEvictionCount()));
        }
    } else if (driver_) {
        // Non-constrained mode: count Irrlicht driver textures
        int texCount = driver_->getTextureCount();
        lines.push_back(fmt::format("[Textures] {} loaded (driver-managed, no budget)", texCount));
    }

    // --- EQE1 Flat Packs ---
    // T108: mapped size, NOT resident -- excluded from totalEstimate below
    // (see getMemoryReportBreakdown()'s comment). Typically 1.6-10.6%
    // resident per T105's smaps measurement; printed here for visibility
    // into the mmap pool's address-space footprint, not as a memory cost.
    lines.push_back(fmt::format("[EQE1 Flat Packs] {} mmap'd, not counted in Tracked total below "
        "({} archives open, mmap/page-cache backed, typically ~2-11% resident)",
        formatBytes(b.eqe1MappedBytes), b.eqe1ArchiveCount));

    // --- Zone Geometry ---
    {
        size_t regionCount = regionMeshNodes_.size();
        size_t objectCount = objectNodes_.size();
        size_t lightCount = zoneLightData_.size();
        size_t objectLightCount = objectLights_.size();

        if (usePvsCulling_) {
            uint32_t pvsVerts = 0, pvsIndices = 0;
            for (const auto& [id, node] : regionMeshNodes_) {
                if (node && node->getMesh()) {
                    auto* mesh = node->getMesh();
                    for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                        pvsVerts += mesh->getMeshBuffer(i)->getVertexCount();
                        pvsIndices += mesh->getMeshBuffer(i)->getIndexCount();
                    }
                }
            }
            lines.push_back(fmt::format("[Zone Geometry] {} ({} regions, {}V/{}I)",
                formatBytes(b.zoneGeometryBytes), regionCount, pvsVerts, pvsIndices));
        } else if (zoneMeshNode_) {
            auto* mesh = zoneMeshNode_->getMesh();
            uint32_t verts = 0, indices = 0;
            if (mesh) {
                for (uint32_t i = 0; i < mesh->getMeshBufferCount(); ++i) {
                    verts += mesh->getMeshBuffer(i)->getVertexCount();
                    indices += mesh->getMeshBuffer(i)->getIndexCount();
                }
            }
            lines.push_back(fmt::format("[Zone Geometry] {} (single mesh, {}V/{}I)",
                formatBytes(b.zoneGeometryBytes), verts, indices));
        } else {
            lines.push_back("[Zone Geometry] Not loaded");
        }

        lines.push_back(fmt::format("[Placeable Objects] {} ({} objects)",
            formatBytes(b.placeableObjectsBytes), objectCount));

        lines.push_back(fmt::format("[Zone Lights] {} zone + {} object",
            lightCount, objectLightCount));
    }

    // --- Entity Renderer ---
    if (entityRenderer_) {
        size_t entityCount = entityRenderer_->getEntityCount();
        size_t modeledCount = entityRenderer_->getModeledEntityCount();
        size_t visibleCount = entityRenderer_->getVisibleEntityCount();
        auto* rml = entityRenderer_->getRaceModelLoader();
        size_t loadedModels = rml ? rml->getLoadedModelCount() : 0;
        lines.push_back(fmt::format("[Entities] {} total, {} modeled, {} visible, {} race models, {} instance data",
            entityCount, modeledCount, visibleCount, loadedModels, formatBytes(b.entityInstanceBytes)));
    }

    // --- Character Models ---
    if (entityRenderer_) {
        if (auto* rml = entityRenderer_->getRaceModelLoader()) {
            auto stats = rml->getMemoryStats();
            size_t rmlTexTotal = stats.globalTextureBytes + stats.numberedTextureBytes
                            + stats.armorTextureBytes + stats.zoneTextureBytes
                            + stats.otherChrTextureBytes;
            lines.push_back(fmt::format("[Character Models] {} (tex {}, geom {}, skel {}, char {}, anim mesh {}, irr mesh {})",
                formatBytes(b.characterModelsBytes),
                formatBytes(rmlTexTotal),
                formatBytes(stats.modelGeometryBytes),
                formatBytes(stats.modelSkeletonBytes),
                formatBytes(stats.characterModelBytes),
                formatBytes(stats.animatedMeshBytes),
                formatBytes(stats.irrlichtMeshBytes)));
        }
    }

    // --- Equipment Models ---
    if (entityRenderer_) {
        if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
            auto stats = eml->getMemoryStats();
            lines.push_back(fmt::format("[Equipment Models] {} (tex {}, geom {}, mesh {}, idx {}; {}/{} loaded, {} mappings)",
                formatBytes(b.equipmentModelsBytes),
                formatBytes(stats.rawTextureBytes),
                formatBytes(stats.geometryBytes),
                formatBytes(stats.irrlichtMeshBytes),
                formatBytes(stats.indexBytes),
                stats.loadedGeometryCount, stats.indexedModelCount, stats.mappingCount));
        }
    }

    // --- S3D Zone Source Data (CPU-side geometry, BSP, animations kept for mesh rebuilds) ---
    if (currentZone_) {
        size_t combinedGeomBytes = currentZone_->geometry ? currentZone_->geometry->getFullMemoryUsage() : 0;
        size_t wldBytes = currentZone_->wldLoader ? currentZone_->wldLoader->getMemoryUsage() : 0;
        size_t objGeomBytes = 0;
        for (const auto& [name, geom] : currentZone_->objectGeometries)
            if (geom) objGeomBytes += geom->getFullMemoryUsage();

        lines.push_back(fmt::format("[S3D Zone Source Data] {} (combined geom {}, WLD {}, obj geom {})",
            formatBytes(b.s3dZoneSourceBytes), formatBytes(combinedGeomBytes),
            formatBytes(wldBytes), formatBytes(objGeomBytes)));

        if (currentZone_->wldLoader) {
            currentZone_->wldLoader->logMemoryBreakdown();
        }
    }

    // --- GPU Texture Memory (actual GPU-side allocations in shared RAM) ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        int texCount = driver_->getTextureCount();
        lines.push_back(fmt::format("[GPU Textures] {} ({} textures in shared RAM)",
            formatBytes(b.gpuTextureBytes), texCount));
    }
#else
    if (driver_) {
        int texCount = driver_->getTextureCount();
        lines.push_back(fmt::format("[Irrlicht Driver] {} textures (GPU-side, not tracked)", texCount));
    }
#endif

    // --- HCMap / Collision ---
    if (collisionMap_ && collisionMap_->IsLoaded()) {
        auto mapStats = collisionMap_->GetMemoryStats();
        lines.push_back(fmt::format("[HCMap/Collision] {} ({} verts, {} faces)",
            formatBytes(b.hcmapBytes), mapStats.vertexCount, mapStats.faceCount));
    }

    // --- Framebuffer ---
    if (driver_) {
        auto screenSize = driver_->getScreenSize();
        int colorBpp = (config_.constrainedConfig.colorDepthBits == 16) ? 16 : 32;
        lines.push_back(fmt::format("[Framebuffer] {} ({}x{}, {}bpp color, D24S8)",
            formatBytes(b.framebufferBytes), screenSize.Width, screenSize.Height, colorBpp));
    }

    // --- VBO/EBO GPU Buffers ---
#ifdef EQT_HAS_GLES2
    if (driver_) {
        size_t hwbCount = gles2GetHWBufferCount(driver_);
        if (hwbCount > 0) {
            lines.push_back(fmt::format("[VBO/EBO GPU Buffers] {} ({} buffers)",
                formatBytes(b.vboEboBytes), hwbCount));
        }
    }

    // --- GPU Upload Thread ---
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        size_t pending = gpuUploadThread_->getPendingCount();
        uint64_t completed = gpuUploadThread_->getTotalUploadsCompleted();
        uint64_t totalUs = gpuUploadThread_->getTotalUploadTimeUs();
        float avgUs = completed > 0 ? static_cast<float>(totalUs) / completed : 0.0f;
        lines.push_back(fmt::format("[GPU Upload Thread] {} pending, {} completed, avg {:.0f}us/upload",
            pending, completed, avgUs));
    }
#endif

    // ============================================================
    // Audio section (from external input)
    // ============================================================
    if (ext.audioAvailable) {
        lines.push_back("");
        lines.push_back("--- Audio ---");

        lines.push_back(fmt::format("[SFX Cache] {}", formatBytes(b.sfxCacheBytes)));

        if (ext.soundBufferCacheMaxBytes > 0) {
            lines.push_back(fmt::format("[Sound Buffer Cache] {} / {}",
                formatBytes(b.soundBufferCacheBytes), formatBytes(ext.soundBufferCacheMaxBytes)));
        } else {
            lines.push_back(fmt::format("[Sound Buffer Cache] {} (no limit)",
                formatBytes(b.soundBufferCacheBytes)));
        }

        if (b.soundFontEstimateBytes > 0) {
            lines.push_back(fmt::format("[SoundFont] ~{}", formatBytes(b.soundFontEstimateBytes)));
        }
        if (b.musicDecodedBytes > 0) {
            lines.push_back(fmt::format("[Music Decoded Data] {}", formatBytes(b.musicDecodedBytes)));
        }
        if (b.audioPfsArchiveBytes > 0) {
            lines.push_back(fmt::format("[Audio PFS Archives] {}", formatBytes(b.audioPfsArchiveBytes)));
        }

        lines.push_back(fmt::format("[Zone Emitters] {} total, {} active",
            ext.zoneEmitterCount, ext.activeEmitterCount));

        // T130: the direct answer to T127's open question -- does the
        // M_ARENA_MAX=1 single malloc arena ever visibly stall the
        // real-time audio callback (e.g. contending with the per-crossing
        // LoadingThread)? "Late" = a callback whose gap from the previous
        // one exceeded 1.5x the configured period. 0 callbacks means audio
        // hasn't produced any frames yet (e.g. no track loaded), not that
        // the probe is inactive.
        if (ext.audioCallbackCount > 0) {
            lines.push_back(fmt::format(
                "[Audio Callback] {} calls | {} late (>1.5x period) | worst gap {}us",
                ext.audioCallbackCount, ext.audioLateCallbackCount, ext.audioWorstCallbackDeltaUs));
        }

        // T131: peak concurrent TinySoundFont voices seen, and how many
        // note-ons were silently dropped because every pre-allocated voice
        // (MidiPlayer::kMaxVoices) was busy. audioNoteOnFailures > 0 means
        // the cap is too low for what was actually played -- see this
        // unit's Review (refactor/T131_tsf_preallocate.md) for the headroom
        // it was chosen with. 0 peak voices means nothing has played yet,
        // not that the probe is inactive (same convention as [Audio Callback]).
        if (ext.audioPeakVoices > 0 || ext.audioNoteOnFailures > 0) {
            lines.push_back(fmt::format(
                "[Audio Voices] peak {} concurrent | {} note-on(s) dropped (cap exceeded)",
                ext.audioPeakVoices, ext.audioNoteOnFailures));
        }
    }

    // ============================================================
    // Network section (from external input)
    // ============================================================
    if (!ext.connections.empty()) {
        lines.push_back("");
        lines.push_back("--- Network ---");
        for (const auto& conn : ext.connections) {
            lines.push_back(fmt::format("[{}] RX {} TX {} | Ping {}ms",
                conn.name, formatBytes(conn.recvBytes), formatBytes(conn.sentBytes), conn.avgPing));
        }
    }

    // ============================================================
    // Game Data section (from external input)
    // ============================================================
    if (ext.entityCount > 0 || ext.doorCount > 0 || ext.spellDbCount > 0) {
        lines.push_back("");
        lines.push_back("--- Game Data ---");

        if (ext.entityCount > 0) {
            lines.push_back(fmt::format("[Entities] {} (~{})",
                ext.entityCount, formatBytes(b.entityEstimateBytes)));
        }
        if (ext.doorCount > 0) {
            lines.push_back(fmt::format("[Doors] {} (~{})",
                ext.doorCount, formatBytes(b.doorEstimateBytes)));
        }
        if (ext.spellDbCount > 0) {
            lines.push_back(fmt::format("[Spell Database] {} spells (~{})",
                ext.spellDbCount, formatBytes(b.spellDbEstimateBytes)));
        }
    }

    // ============================================================
    // Process section (from external input)
    // ============================================================
    if (ext.processRssBytes > 0) {
        lines.push_back("");
        lines.push_back("--- Process ---");
        lines.push_back(fmt::format("[RSS] {}", formatBytes(ext.processRssBytes)));
        lines.push_back(fmt::format("[Virtual] {}", formatBytes(ext.processVmBytes)));
        if (ext.sharedLibBytes > 0 || ext.anonBytes > 0 || ext.stackBytes > 0) {
            lines.push_back(fmt::format("[Shared Libs] {}", formatBytes(ext.sharedLibBytes)));
            lines.push_back(fmt::format("[Anonymous (heap+mmap)] {}", formatBytes(ext.anonBytes)));
            if (ext.stackBytes > 0)
                lines.push_back(fmt::format("[Thread Stacks] {}", formatBytes(ext.stackBytes)));
        }
        // T127: glibc allocator arena vs. in-use -- the gap between these
        // two is free memory glibc is holding onto rather than returning to
        // the OS (arena fragmentation). This was previously invisible here
        // (T106 flagged it as "no dedicated counter") and was the dominant
        // driver of zone-crossing RSS growth on this codebase (measured:
        // repeated per-crossing LoadingThread creation fragmented the
        // default multi-arena glibc policy; mallopt(M_ARENA_MAX, 1) in
        // main.cpp cut a 6-crossing tour's RSS growth ~8x). If this gap
        // grows again in the future, it means the arena fix regressed or a
        // new source of thread churn was added -- check here first, before
        // re-running a profiler from scratch.
        if (ext.mallocArenaBytes > 0) {
            size_t freeNotReleased = (ext.mallocArenaBytes > ext.mallocInUseBytes)
                ? (ext.mallocArenaBytes - ext.mallocInUseBytes) : 0;
            lines.push_back(fmt::format(
                "[Allocator] arena {} | in-use {} | free-not-released {}",
                formatBytes(ext.mallocArenaBytes), formatBytes(ext.mallocInUseBytes),
                formatBytes(freeNotReleased)));
        }
    }

    // --- Total ---
    // P04: named-suspects breakdown. Every line above this point is a named
    // pool (texture/mesh cache, zone geometry/objects/entities/models, GPU
    // textures, HCMap, framebuffer, VBO/EBO, EQE1 flat packs, audio, network).
    // "untracked" is RSS minus all of those — glibc allocator arena/heap
    // fragmentation overhead and any decode/staging buffers not yet named as
    // their own line are the leading remaining suspects (see review's audit
    // notes; malloc_trim was measured, not applied — see Deviations).
    lines.push_back("");
    if (ext.processRssBytes > 0) {
        float pct = ext.processRssBytes > 0 ? (b.totalEstimate * 100.0f / ext.processRssBytes) : 0.0f;
        lines.push_back(fmt::format("--- Tracked: {} / RSS: {} ({:.0f}%, untracked: {}) ---",
            formatBytes(b.totalEstimate), formatBytes(ext.processRssBytes), pct, formatBytes(b.untrackedBytes)));
    } else {
        lines.push_back(fmt::format("--- Tracked total: {} ---", formatBytes(b.totalEstimate)));
    }

    return lines;
}

} // namespace Graphics
} // namespace EQT
