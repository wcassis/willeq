// EQT::Graphics::DiagnosticsComponent: the runtime debug toggles.
// Moved verbatim from src/client/graphics/renderer_debug.cpp by L04
// (refactor/L04_diagnostics_component.md).
#include "client/graphics/diagnostics/diagnostics_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_zone_merge.h"
#include "common/logging.h"
#include "client/data_paths.h"
#include <json/json.h>
#include <fstream>

namespace EQT {
namespace Graphics {

void DiagnosticsComponent::toggleWireframe() {
    ctx_.wireframeMode = !ctx_.wireframeMode;

    if (ctx_.zoneMeshNode) {
        for (irr::u32 i = 0; i < ctx_.zoneMeshNode->getMaterialCount(); ++i) {
            ctx_.zoneMeshNode->getMaterial(i).Wireframe = ctx_.wireframeMode;
        }
    }

    for (auto* node : ctx_.objectNodes) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).Wireframe = ctx_.wireframeMode;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Wireframe mode: {}", (ctx_.wireframeMode ? "ON" : "OFF"));
}

void DiagnosticsComponent::toggleHUD() {
    hudEnabled_ = !hudEnabled_;
    if (ctx_.Overlays().hudText()) {
        ctx_.Overlays().hudText()->setVisible(hudEnabled_);
    }
    if (ctx_.Overlays().hotkeysText()) {
        ctx_.Overlays().hotkeysText()->setVisible(hudEnabled_);
    }
    LOG_INFO(MOD_GRAPHICS, "HUD: {}", (hudEnabled_ ? "ON" : "OFF"));
}

void DiagnosticsComponent::toggleAllUI() {
    ctx_.Frame().allUIHidden_ = !ctx_.Frame().allUIHidden_;
    LOG_INFO(MOD_GRAPHICS, "All UI: {}", (ctx_.Frame().allUIHidden_ ? "HIDDEN" : "VISIBLE"));
}

void DiagnosticsComponent::toggleNameTags() {
    if (ctx_.entityRenderer) {
        bool visible = !ctx_.config.constrainedConfig.nameTagsEnabled;
        ctx_.config.constrainedConfig.nameTagsEnabled = visible;
        ctx_.entityRenderer->setNameTagsVisible(visible);
        LOG_INFO(MOD_GRAPHICS, "Name tags: {}", (visible ? "ON" : "OFF"));
    }
}

void DiagnosticsComponent::toggleFog() {
    ctx_.fogEnabled = !ctx_.fogEnabled;

    if (ctx_.zoneMeshNode) {
        for (irr::u32 i = 0; i < ctx_.zoneMeshNode->getMaterialCount(); ++i) {
            ctx_.zoneMeshNode->getMaterial(i).FogEnable = ctx_.fogEnabled;
        }
    }

    for (auto* node : ctx_.objectNodes) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).FogEnable = ctx_.fogEnabled;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: {}", (ctx_.fogEnabled ? "ON" : "OFF"));
}

void DiagnosticsComponent::toggleZoneLights() {
    // 3-state cycle:
    // State 1: Lighting ON, Zone lights OFF (default) - dark scene, only object lights
    // State 2: Lighting ON, Zone lights ON - normal ambient/sun + zone lights
    // State 3: Lighting OFF, Zone lights OFF - no lighting effects
    // Then back to State 1

    if (ctx_.lightingEnabled && !zoneLightsEnabled_) {
        // State 1 -> State 2: Turn zone lights ON, restore normal ambient/sun
        zoneLightsEnabled_ = true;
        // Restore normal ambient and sun based on time of day -- see the
        // T423 mark at the end of this function.
        if (sunLight_) {
            sunLight_->setVisible(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: ON ({} lights)", ctx_.zoneLightData.size());
    } else if (ctx_.lightingEnabled && zoneLightsEnabled_) {
        // State 2 -> State 3: Turn both OFF
        zoneLightsEnabled_ = false;
        ctx_.lightingEnabled = false;
        // Update materials to disable lighting
        if (ctx_.zoneMeshNode) {
            for (irr::u32 i = 0; i < ctx_.zoneMeshNode->getMaterialCount(); ++i) {
                ctx_.zoneMeshNode->getMaterial(i).Lighting = false;
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        for (auto* node : ctx_.objectNodes) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        // Update entity materials
        if (ctx_.entityRenderer) {
            ctx_.entityRenderer->setLightingEnabled(false);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: OFF, Zone lights: OFF");
    } else {
        // State 3 -> State 1: Turn lighting ON, keep zone lights OFF
        // Dark scene - only object lights provide illumination
        ctx_.lightingEnabled = true;
        zoneLightsEnabled_ = false;
        // Update materials to enable lighting
        if (ctx_.zoneMeshNode) {
            for (irr::u32 i = 0; i < ctx_.zoneMeshNode->getMaterialCount(); ++i) {
                ctx_.zoneMeshNode->getMaterial(i).Lighting = true;
                ctx_.zoneMeshNode->getMaterial(i).NormalizeNormals = true;
                ctx_.zoneMeshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                ctx_.zoneMeshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                    node->getMaterial(i).NormalizeNormals = true;
                    node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        for (auto* node : ctx_.objectNodes) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                    node->getMaterial(i).NormalizeNormals = true;
                    node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        // Set dark ambient and disable sun - only object lights illuminate
        ctx_.smgr->setAmbientLight(irr::video::SColorf(0.005f, 0.005f, 0.008f, 1.0f));
        if (sunLight_) {
            sunLight_->setVisible(false);
        }
        // Update entity materials
        if (ctx_.entityRenderer) {
            ctx_.entityRenderer->setLightingEnabled(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: OFF (dark mode)");
    }

    // T423 (board B362): all three arms write ctx_.lightingEnabled or
    // zoneLightsEnabled_, and updateTimeOfDay() READS both -- it early-returns
    // while zone lights are off, which is exactly why this used to recompose
    // inline. It marks like every other non-clock writer now, and the frame
    // pass runs the recompute before the same frame's draw, so the toggle is
    // still instant. Marked for all three states, not just the restore: the
    // gate updateTimeOfDay() reads has moved in every one of them.
    owner_.markSceneAmbientDirty();
}

void DiagnosticsComponent::togglePlayerLight() {
    if (owner_.isLoading()) return;
    debugPlayerLightEnabled_ = !debugPlayerLightEnabled_;
    // Toggle shader variant (GLES2 only — lightweight vs per-pixel player light)
    if (ctx_.zoneShader && ctx_.zoneShader->isLightweightAvailable()) {
        ctx_.zoneShader->setPerPixelPlayerLight(debugPlayerLightEnabled_);
        LOG_INFO(MOD_GRAPHICS, "plight toggle: pp solid={} alpha={} atlasSolid={} atlasAlpha={}",
            ctx_.zoneShader->getMaterialTypeSolid(), ctx_.zoneShader->getMaterialTypeAlphaTest(),
            ctx_.zoneShader->getMaterialTypeAtlasSolid(), ctx_.zoneShader->getMaterialTypeAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: lw solid={} alpha={} atlasSolid={} atlasAlpha={}",
            ctx_.zoneShader->getMaterialTypeLWSolid(), ctx_.zoneShader->getMaterialTypeLWAlphaTest(),
            ctx_.zoneShader->getMaterialTypeLWAtlasSolid(), ctx_.zoneShader->getMaterialTypeLWAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: active solid={} alpha={} atlasSolid={} atlasAlpha={}",
            ctx_.zoneShader->getActiveSolid(), ctx_.zoneShader->getActiveAlphaTest(),
            ctx_.zoneShader->getActiveAtlasSolid(), ctx_.zoneShader->getActiveAtlasAlpha());
        swapZoneMeshMaterials();
    }
    LOG_INFO(MOD_GRAPHICS, "Debug: Player light {}", debugPlayerLightEnabled_ ? "ENABLED" : "DISABLED");
}

void DiagnosticsComponent::swapZoneMeshMaterials() {
    if (!ctx_.zoneShader) return;

    // Build mapping from old material type IDs to new active variant IDs.
    // After setPerPixelPlayerLight(), the getActive*() and getMaterialType*() methods
    // return the new and old types respectively (or vice versa).
    // We need to map both directions: per-pixel ↔ lightweight.
    struct MaterialMap {
        irr::s32 ppSolid, ppAlpha, ppAlphaRGBA, ppAtlasSolid, ppAtlasAlpha, ppWind;
        irr::s32 lwSolid, lwAlpha, lwAlphaRGBA, lwAtlasSolid, lwAtlasAlpha, lwWind;
    };
    MaterialMap m;
    m.ppSolid = ctx_.zoneShader->getMaterialTypeSolid();
    m.ppAlpha = ctx_.zoneShader->getMaterialTypeAlphaTest();
    m.ppAlphaRGBA = ctx_.zoneShader->getMaterialTypeAlphaTestRGBA();  // P21: H02d Option-A RGBA variant
    m.ppAtlasSolid = ctx_.zoneShader->getMaterialTypeAtlasSolid();
    m.ppAtlasAlpha = ctx_.zoneShader->getMaterialTypeAtlasAlpha();
    m.ppWind = ctx_.zoneShader->getMaterialTypeWindAlphaTest();
    m.lwSolid = ctx_.zoneShader->getMaterialTypeLWSolid();
    m.lwAlpha = ctx_.zoneShader->getMaterialTypeLWAlphaTest();
    m.lwAlphaRGBA = ctx_.zoneShader->getMaterialTypeLWAlphaTestRGBA();  // P21: H02d Option-A RGBA variant
    m.lwAtlasSolid = ctx_.zoneShader->getMaterialTypeLWAtlasSolid();
    m.lwAtlasAlpha = ctx_.zoneShader->getMaterialTypeLWAtlasAlpha();
    m.lwWind = ctx_.zoneShader->getMaterialTypeLWWindAlphaTest();

    // Determine target for each known custom material type
    auto mapMaterial = [&](irr::s32 cur) -> irr::s32 {
        // Per-pixel → lightweight
        if (cur == m.ppSolid && m.lwSolid >= 0) return ctx_.zoneShader->getActiveSolid();
        if (cur == m.ppAlpha && m.lwAlpha >= 0) return ctx_.zoneShader->getActiveAlphaTest();
        if (cur == m.ppAtlasSolid && m.lwAtlasSolid >= 0) return ctx_.zoneShader->getActiveAtlasSolid();
        if (cur == m.ppAtlasAlpha && m.lwAtlasAlpha >= 0) return ctx_.zoneShader->getActiveAtlasAlpha();
        if (cur == m.ppWind && m.lwWind >= 0) return ctx_.zoneShader->getActiveWindAlphaTest();
        // Lightweight → per-pixel
        if (cur == m.lwSolid) return ctx_.zoneShader->getActiveSolid();
        if (cur == m.lwAlpha) return ctx_.zoneShader->getActiveAlphaTest();
        if (cur == m.lwAtlasSolid) return ctx_.zoneShader->getActiveAtlasSolid();
        if (cur == m.lwAtlasAlpha) return ctx_.zoneShader->getActiveAtlasAlpha();
        if (cur == m.lwWind) return ctx_.zoneShader->getActiveWindAlphaTest();
        return cur;  // Not a custom shader material — leave unchanged
    };

    int swapTotal = 0, swapChanged = 0;
    auto swapNode = [&](irr::scene::IMeshSceneNode* node) {
        if (!node) return;
        // Swap on BOTH mesh buffer materials AND node materials.
        // Irrlicht uses node materials when ReadOnlyMaterials=false (default).
        auto* mesh = node->getMesh();
        if (!mesh) return;
        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            auto& mat = mesh->getMeshBuffer(i)->getMaterial();
            irr::s32 cur = mat.MaterialType;
            irr::s32 mapped = mapMaterial(cur);
            ++swapTotal;
            if (mapped != cur) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(mapped);
                ++swapChanged;
            }
        }
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            auto& mat = node->getMaterial(i);
            irr::s32 cur = mat.MaterialType;
            irr::s32 mapped = mapMaterial(cur);
            if (mapped != cur) {
                LOG_TRACE(MOD_SHADER, "swapNode: node mat[{}] {} -> {}", i, cur, mapped);
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(mapped);
            }
        }
    };

    // Swap zone mesh node (non-PVS single mesh)
    swapNode(ctx_.zoneMeshNode);
    // T504 (zone draw rework): in POOLED mode the zone's materials are the pools'
    // table, a few dozen entries instead of one per region buffer.
    if (const auto& pools = ctx_.ZoneLifecycle().zoneMergeBuffers(); pools && pools->isPooled()) {
        for (size_t mi = 0; mi < pools->materialCount(); ++mi) {
            auto& mat = pools->material(mi);
            irr::s32 cur = mat.MaterialType;
            irr::s32 mapped = mapMaterial(cur);
            ++swapTotal;
            if (mapped != cur) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(mapped);
                ++swapChanged;
            }
        }
    }

    // Swap PVS region mesh nodes
    for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
        swapNode(node);
    }

    // Swap object nodes (trees use wind material)
    for (auto* node : ctx_.objectNodes) {
        swapNode(node);
    }

    // Swap door mesh materials and update stored types for future door builds
    int doorSwapped = 0;
    if (ctx_.doorManager) {
        ctx_.doorManager->setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                             ctx_.zoneShader->getActiveAlphaTest(),
                                             ctx_.zoneShader->getActiveAlphaTestRGBA());
        // Swap materials on existing door scene nodes
        for (auto* node : ctx_.doorManager->getDoorSceneNodes()) {
            swapNode(node);
            ++doorSwapped;
        }
        // Invalidate mesh cache so future rebuilds use the new material types
        ctx_.doorManager->invalidateMeshCache("");
    }

    LOG_INFO(MOD_GRAPHICS, "swapZoneMeshMaterials: swapped {}/{} mesh buffers (zone={} regions={} objects={} doors={})",
        swapChanged, swapTotal,
        ctx_.zoneMeshNode != nullptr, ctx_.regionMeshNodes.size(),
        ctx_.objectNodes.size(), doorSwapped);

    // Rebuild sorted draw list if manual zone draw is active (material keys changed)
    if (ctx_.manualZoneDrawEnabled) {
        ctx_.ZoneLifecycle().sortedDrawEntries().clear();
    }

    // Swap existing entity mesh materials and update future entity material types
    if (ctx_.entityRenderer && ctx_.zoneShader->isAvailable()) {
        ctx_.entityRenderer->swapShaderMaterials(
            m.ppSolid, ctx_.zoneShader->getActiveSolid(),
            m.ppAlpha, ctx_.zoneShader->getActiveAlphaTest(),
            m.ppAlphaRGBA, ctx_.zoneShader->getActiveAlphaTestRGBA());
        // Also swap lw→active in case entities had lightweight materials
        ctx_.entityRenderer->swapShaderMaterials(
            m.lwSolid, ctx_.zoneShader->getActiveSolid(),
            m.lwAlpha, ctx_.zoneShader->getActiveAlphaTest(),
            m.lwAlphaRGBA, ctx_.zoneShader->getActiveAlphaTestRGBA());
        ctx_.entityRenderer->setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                                ctx_.zoneShader->getActiveAlphaTest(),
                                                ctx_.zoneShader->getActiveAlphaTestRGBA());
    }
    // P32: AnimatedTreeManager::setShaderMaterialTypes() removed along with
    // the class — it fed shaderMaterialSolid_/AlphaTest_/AlphaTestRGBA_
    // members used only by createAnimatedTree() (CPU-instancing, dead on
    // GLES2; reachable only via the removed EDT_BURNINGSVIDEO branch). Tree
    // wind material types are set directly on mesh/node materials in the
    // Step 11 fixup pass (renderer_zone_load.cpp) and renderer_zone_stream.cpp
    // instead, which this debug shader-swap path does not touch.
}

void DiagnosticsComponent::swapToDebugShader(irr::s32 debugMat) {
    // Swap all per-pixel custom shader materials to the debug visualization program.
    // Stores the original material type in debugOriginalMaterials_ for restoration.
    debugOriginalMaterials_.clear();
    // Custom shader materials start at the first compiled material type (solid)
    irr::s32 firstCustom = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeSolid() : 256;

    auto swapNodeToDebug = [&](irr::scene::IMeshSceneNode* node) {
        if (!node) return;
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            auto& mat = node->getMaterial(i);
            irr::s32 cur = mat.MaterialType;
            if (cur >= firstCustom && cur != debugMat) {
                debugOriginalMaterials_.push_back({node, i, cur});
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(debugMat);
            }
        }
    };

    if (ctx_.zoneMeshNode) swapNodeToDebug(ctx_.zoneMeshNode);
    for (auto& [idx, node] : ctx_.regionMeshNodes) swapNodeToDebug(node);
    for (auto* node : ctx_.objectNodes) swapNodeToDebug(node);
}

void DiagnosticsComponent::swapFromDebugShader(irr::s32 debugMat) {
    // Restore original materials saved by swapToDebugShader.
    for (auto& entry : debugOriginalMaterials_) {
        if (entry.node && entry.materialIndex < entry.node->getMaterialCount()) {
            entry.node->getMaterial(entry.materialIndex).MaterialType =
                static_cast<irr::video::E_MATERIAL_TYPE>(entry.originalType);
        }
    }
    debugOriginalMaterials_.clear();
}

void DiagnosticsComponent::toggleObjectLights() {
    if (owner_.isLoading()) return;
    debugObjectLightsEnabled_ = !debugObjectLightsEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Debug: Object lights {}", debugObjectLightsEnabled_ ? "ENABLED" : "DISABLED");
}

void DiagnosticsComponent::toggleDirectionalLight() {
    if (owner_.isLoading()) return;
    debugDirectionalLightEnabled_ = !debugDirectionalLightEnabled_;
    if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
        if (!debugDirectionalLightEnabled_) {
            // Fullbright: ambient=1, sun=0, tint=1
            ctx_.zoneShader->setAmbientColor(1.0f, 1.0f, 1.0f);
            ctx_.zoneShader->setSunColor(0.0f, 0.0f, 0.0f);
            ctx_.zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
        }
    }
    // T423 (board B362): updateTimeOfDay() branches on
    // debugDirectionalLightEnabled_ -- it writes the fullbright override in
    // one state and the composed ambient in the other -- so this flag is an
    // ambient input in BOTH directions. Marked outside the zoneShader guard:
    // the mark is about the input having moved, not about a pointer that
    // happens to be null before initDeferredGraphics().
    owner_.markSceneAmbientDirty();
    LOG_INFO(MOD_GRAPHICS, "Debug: Directional/ambient light {}", debugDirectionalLightEnabled_ ? "ENABLED" : "DISABLED");
}

void DiagnosticsComponent::cycleObjectLights() {
    // Cycle: 0 -> 1 -> 2 -> 3 -> ... -> 8 -> 0 -> ...
    if (maxObjectLights_ >= 8) {
        maxObjectLights_ = 0;
    } else {
        maxObjectLights_++;
    }

    LOG_INFO(MOD_GRAPHICS, "Object lights: {} max", maxObjectLights_);
}

void DiagnosticsComponent::toggleOldModels() {
    if (!ctx_.entityRenderer) {
        return;
    }

    auto* loader = ctx_.entityRenderer->getRaceModelLoader();
    if (!loader) {
        return;
    }

    bool newState = !loader->isUsingOldModels();
    loader->setUseOldModels(newState);
    loader->clearCache();

    LOG_INFO(MOD_GRAPHICS, "Model mode: {}", (newState ? "Old (Classic)" : "New (Luclin+)"));
}

bool DiagnosticsComponent::isUsingOldModels() const {
    if (!ctx_.entityRenderer) {
        return true;  // Default to old models
    }

    auto* loader = ctx_.entityRenderer->getRaceModelLoader();
    if (!loader) {
        return true;
    }

    return loader->isUsingOldModels();
}

void DiagnosticsComponent::toggleManualZoneDraw() {
    if (owner_.isLoading()) return;
    if (!ctx_.usePvsCulling || ctx_.regionMeshNodes.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Manual zone draw not available (no PVS culling)");
        return;
    }
    ctx_.manualZoneDrawEnabled = !ctx_.manualZoneDrawEnabled;
    if (ctx_.manualZoneDrawEnabled) {
        // S05: renderPassTimer_ already created in initLoadingScreen() — just install
        if (ctx_.smgr && ctx_.renderPassTimer) {
            ctx_.smgr->setLightManager(ctx_.renderPassTimer);
        }
        // Remove zone mesh nodes from scene graph — manual draw accesses them directly
        for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
            if (node && node->getParent()) { node->grab(); node->remove(); }
        }
        // Remove object nodes from scene graph — drawn via sorted draw list
        for (size_t i = 0; i < ctx_.objectNodes.size(); ++i) {
            auto* node = ctx_.objectNodes[i];
            if (node && node->getParent()) {
                node->grab(); node->remove();
                if (i < ctx_.objectInSceneGraph.size()) ctx_.objectInSceneGraph[i] = false;
            }
        }
    } else {
        // Re-add zone mesh nodes to scene graph for Irrlicht-managed rendering
        for (auto& [regionIdx, node] : ctx_.regionMeshNodes) {
            if (node && !node->getParent()) {
                ctx_.smgr->getRootSceneNode()->addChild(node);
                node->drop();
            }
        }
        // Re-add visible object nodes to scene graph
        for (size_t i = 0; i < ctx_.objectNodes.size(); ++i) {
            auto* node = ctx_.objectNodes[i];
            if (node && !node->getParent()) {
                ctx_.smgr->getRootSceneNode()->addChild(node);
                node->drop();
                if (i < ctx_.objectInSceneGraph.size()) ctx_.objectInSceneGraph[i] = true;
            }
        }
    }
    LOG_INFO(MOD_GRAPHICS, "Manual zone draw (front-to-back sorting): {}",
             ctx_.manualZoneDrawEnabled ? "ENABLED" : "DISABLED");
}

// P10: cross-region zone draw merge runtime toggle (/mergedraws). Mirrors
// /sort's pattern — no scene-graph manipulation needed here (the merge
// decision/build lives entirely inside drawZoneGeometrySorted(), gated on
// this bool); toggling off just makes the next frame take the unmerged
// branch, toggling on lets the next eligible frame attempt a build.
void DiagnosticsComponent::toggleMergeZoneDraws() {
    if (owner_.isLoading()) return;
    // T504: pooled zone geometry has no unmerged form to fall back to.
    if (const auto& pools = ctx_.ZoneLifecycle().zoneMergeBuffers(); pools && pools->isPooled()) {
        LOG_INFO(MOD_GRAPHICS, "[mergedraws] the zone is drawn from pools (no per-region nodes) -- "
                 "cannot be switched off for this zone visit; restart with WILLEQ_ZONE_POOLS=0 for the legacy path");
        return;
    }
    ctx_.mergeZoneDrawsEnabled = !ctx_.mergeZoneDrawsEnabled;
    if (!ctx_.mergeZoneDrawsEnabled && ctx_.ZoneLifecycle().zoneMergeBuffers()) {
        // Free the merge buffers immediately when turned off — no reason to
        // hold the duplicated vertex/index memory while unused.
        ctx_.ZoneLifecycle().zoneMergeBuffers()->destroy();
    }
    // Re-arm the "disabled for this zone" latch when explicitly re-enabled,
    // so a user flipping /mergedraws off/on after an eviction-triggered
    // auto-disable gets a fresh attempt (at the cost of possibly hitting the
    // same eviction-driven fallback again — that's expected, not a bug).
    if (ctx_.mergeZoneDrawsEnabled) {
        ctx_.ZoneLifecycle().mergeZoneDrawsDisabledForZone() = false;
        // T67: was `constrainedMeshCache_ ? ...->getEvictionCount() : 0`,
        // always 0 (the cache was never constructed). Pre-existing note:
        // unlike drawZoneGeometrySorted()'s own gate (renderer_zone_draw.cpp),
        // this reset never included constrainedTextureCache_'s eviction count
        // either, so re-enabling immediately after a texture-eviction-triggered
        // auto-disable could under-count and attempt a merge build one
        // eviction "early" -- not introduced here, not fixed here.
        ctx_.ZoneLifecycle().mergeZoneDrawsBuildEvictionCount() = 0;
    }
    LOG_INFO(MOD_GRAPHICS, "Zone draw merge (/mergedraws): {}",
             ctx_.mergeZoneDrawsEnabled ? "ENABLED" : "DISABLED");
}

bool DiagnosticsComponent::loadIndoorRegionMap(const std::string& zoneName) {
    ctx_.indoorRegions.clear();

    std::string regionMapsPath = ctx_.config.regionMapsPath;
    if (regionMapsPath.empty()) regionMapsPath = "data/region_maps";

    // T399 (board B235): resolved on the COMPLETE path, so the config knob
    // (config.regionMapsPath, absolute or relative) keeps working.
    std::string mapFile = eqt::ResolveDataPath(regionMapsPath + "/" + zoneName + ".json");
    std::ifstream ifs(mapFile);
    if (!ifs.is_open()) {
        LOG_DEBUG(MOD_GRAPHICS, "No region map file: {} (portal vis will use zone-level heuristic)", mapFile);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, ifs, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse region map {}: {}", mapFile, errors);
        return false;
    }

    const auto& regions = root["regions"];
    if (!regions.isArray()) {
        LOG_WARN(MOD_GRAPHICS, "Region map {} has no 'regions' array", mapFile);
        return false;
    }

    for (const auto& r : regions) {
        if (r.isMember("indoor") && r["indoor"].asBool()) {
            ctx_.indoorRegions.insert(static_cast<size_t>(r["region"].asUInt()));
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Loaded region map: {} ({} indoor regions out of {} total)",
             mapFile, ctx_.indoorRegions.size(), regions.size());
    return true;
}

void DiagnosticsComponent::togglePortalOcclusion() {
    if (owner_.isLoading()) return;
    // B971: stencil is the only mechanism /portal renders with, and the
    // orangepi preset has no stencil buffer on its surface (no stencil user,
    // Will 2026-10-01). The auto-enable paths (zone load / progressive load)
    // are already gated on enableStencilBuffer, so the only way to reach here
    // is the manual toggle: say plainly that there is no stencil to use
    // instead of silently building a portal system that can never render.
    if (!ctx_.config.constrainedConfig.enableStencilBuffer) {
        LOG_WARN(MOD_GRAPHICS, "Portal occlusion unavailable: this preset has no stencil buffer (enableStencilBuffer off)");
        return;
    }
    // T504: presets with portalOcclusion off no longer build the portal system at
    // zone load (it cost +92.9 MiB in iceclad). Ask the deferred builder
    // (ZoneStreamComponent) for it; it lands within a frame or two.
    if (!ctx_.portalSystem && ctx_.zoneBspTree) {
        ctx_.portalBuildPending = true;
        LOG_INFO(MOD_GRAPHICS, "Portal occlusion: building the portal system on demand -- run /portal again");
        return;
    }
    if (!ctx_.portalOcclusionEligible) {
        LOG_INFO(MOD_GRAPHICS, "Portal occlusion not available (no portals or too few)");
        return;
    }
    ctx_.portalOcclusionEnabled = !ctx_.portalOcclusionEnabled;
    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: {}",
             ctx_.portalOcclusionEnabled ? "ENABLED" : "DISABLED");
}

void DiagnosticsComponent::togglePortalDebugDraw() {
    if (owner_.isLoading()) return;
    portalDebugDraw_ = !portalDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Portal debug draw: {}",
             portalDebugDraw_ ? "ENABLED" : "DISABLED");
}

void DiagnosticsComponent::toggleStencilDebugDraw() {
    if (owner_.isLoading()) return;
    stencilDebugDraw_ = !stencilDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Stencil debug draw: {}",
             stencilDebugDraw_ ? "ENABLED" : "DISABLED");
}

} // namespace Graphics
} // namespace EQT
