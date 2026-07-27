#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/constrained_mesh_cache.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::toggleWireframe() {
    wireframeMode_ = !wireframeMode_;

    if (zoneMeshNode_) {
        for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
            zoneMeshNode_->getMaterial(i).Wireframe = wireframeMode_;
        }
    }

    for (auto* node : objectNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).Wireframe = wireframeMode_;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Wireframe mode: {}", (wireframeMode_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleHUD() {
    hudEnabled_ = !hudEnabled_;
    if (hudText_) {
        hudText_->setVisible(hudEnabled_);
    }
    if (hotkeysText_) {
        hotkeysText_->setVisible(hudEnabled_);
    }
    LOG_INFO(MOD_GRAPHICS, "HUD: {}", (hudEnabled_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleAllUI() {
    allUIHidden_ = !allUIHidden_;
    LOG_INFO(MOD_GRAPHICS, "All UI: {}", (allUIHidden_ ? "HIDDEN" : "VISIBLE"));
}

void IrrlichtRenderer::toggleNameTags() {
    if (entityRenderer_) {
        bool visible = !config_.constrainedConfig.nameTagsEnabled;
        config_.constrainedConfig.nameTagsEnabled = visible;
        entityRenderer_->setNameTagsVisible(visible);
        LOG_INFO(MOD_GRAPHICS, "Name tags: {}", (visible ? "ON" : "OFF"));
    }
}

void IrrlichtRenderer::toggleFog() {
    fogEnabled_ = !fogEnabled_;

    if (zoneMeshNode_) {
        for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
            zoneMeshNode_->getMaterial(i).FogEnable = fogEnabled_;
        }
    }

    for (auto* node : objectNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                node->getMaterial(i).FogEnable = fogEnabled_;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: {}", (fogEnabled_ ? "ON" : "OFF"));
}

void IrrlichtRenderer::toggleZoneLights() {
    // 3-state cycle:
    // State 1: Lighting ON, Zone lights OFF (default) - dark scene, only object lights
    // State 2: Lighting ON, Zone lights ON - normal ambient/sun + zone lights
    // State 3: Lighting OFF, Zone lights OFF - no lighting effects
    // Then back to State 1

    if (lightingEnabled_ && !zoneLightsEnabled_) {
        // State 1 -> State 2: Turn zone lights ON, restore normal ambient/sun
        zoneLightsEnabled_ = true;
        // Restore normal ambient and sun based on time of day
        updateTimeOfDay(currentHour_, currentMinute_);
        if (sunLight_) {
            sunLight_->setVisible(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: ON ({} lights)", zoneLightData_.size());
    } else if (lightingEnabled_ && zoneLightsEnabled_) {
        // State 2 -> State 3: Turn both OFF
        zoneLightsEnabled_ = false;
        lightingEnabled_ = false;
        // Update materials to disable lighting
        if (zoneMeshNode_) {
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = false;
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        for (auto* node : objectNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = false;
                }
            }
        }
        // Update entity materials
        if (entityRenderer_) {
            entityRenderer_->setLightingEnabled(false);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: OFF, Zone lights: OFF");
    } else {
        // State 3 -> State 1: Turn lighting ON, keep zone lights OFF
        // Dark scene - only object lights provide illumination
        lightingEnabled_ = true;
        zoneLightsEnabled_ = false;
        // Update materials to enable lighting
        if (zoneMeshNode_) {
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = true;
                zoneMeshNode_->getMaterial(i).NormalizeNormals = true;
                zoneMeshNode_->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                zoneMeshNode_->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
        // Update PVS region mesh nodes
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).Lighting = true;
                    node->getMaterial(i).NormalizeNormals = true;
                    node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        for (auto* node : objectNodes_) {
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
        smgr_->setAmbientLight(irr::video::SColorf(0.005f, 0.005f, 0.008f, 1.0f));
        if (sunLight_) {
            sunLight_->setVisible(false);
        }
        // Update entity materials
        if (entityRenderer_) {
            entityRenderer_->setLightingEnabled(true);
        }
        LOG_INFO(MOD_GRAPHICS, "Lighting: ON, Zone lights: OFF (dark mode)");
    }
}

void IrrlichtRenderer::togglePlayerLight() {
    if (isLoading()) return;
    debugPlayerLightEnabled_ = !debugPlayerLightEnabled_;
    // Toggle shader variant (GLES2 only — lightweight vs per-pixel player light)
    if (zoneShader_ && zoneShader_->isLightweightAvailable()) {
        zoneShader_->setPerPixelPlayerLight(debugPlayerLightEnabled_);
        LOG_INFO(MOD_GRAPHICS, "plight toggle: pp solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getMaterialTypeSolid(), zoneShader_->getMaterialTypeAlphaTest(),
            zoneShader_->getMaterialTypeAtlasSolid(), zoneShader_->getMaterialTypeAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: lw solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getMaterialTypeLWSolid(), zoneShader_->getMaterialTypeLWAlphaTest(),
            zoneShader_->getMaterialTypeLWAtlasSolid(), zoneShader_->getMaterialTypeLWAtlasAlpha());
        LOG_INFO(MOD_GRAPHICS, "plight toggle: active solid={} alpha={} atlasSolid={} atlasAlpha={}",
            zoneShader_->getActiveSolid(), zoneShader_->getActiveAlphaTest(),
            zoneShader_->getActiveAtlasSolid(), zoneShader_->getActiveAtlasAlpha());
        swapZoneMeshMaterials();
    }
    LOG_INFO(MOD_GRAPHICS, "Debug: Player light {}", debugPlayerLightEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::swapZoneMeshMaterials() {
    if (!zoneShader_) return;

    // Build mapping from old material type IDs to new active variant IDs.
    // After setPerPixelPlayerLight(), the getActive*() and getMaterialType*() methods
    // return the new and old types respectively (or vice versa).
    // We need to map both directions: per-pixel ↔ lightweight.
    struct MaterialMap {
        irr::s32 ppSolid, ppAlpha, ppAtlasSolid, ppAtlasAlpha, ppWind;
        irr::s32 lwSolid, lwAlpha, lwAtlasSolid, lwAtlasAlpha, lwWind;
    };
    MaterialMap m;
    m.ppSolid = zoneShader_->getMaterialTypeSolid();
    m.ppAlpha = zoneShader_->getMaterialTypeAlphaTest();
    m.ppAtlasSolid = zoneShader_->getMaterialTypeAtlasSolid();
    m.ppAtlasAlpha = zoneShader_->getMaterialTypeAtlasAlpha();
    m.ppWind = zoneShader_->getMaterialTypeWindAlphaTest();
    m.lwSolid = zoneShader_->getMaterialTypeLWSolid();
    m.lwAlpha = zoneShader_->getMaterialTypeLWAlphaTest();
    m.lwAtlasSolid = zoneShader_->getMaterialTypeLWAtlasSolid();
    m.lwAtlasAlpha = zoneShader_->getMaterialTypeLWAtlasAlpha();
    m.lwWind = zoneShader_->getMaterialTypeLWWindAlphaTest();

    // Determine target for each known custom material type
    auto mapMaterial = [&](irr::s32 cur) -> irr::s32 {
        // Per-pixel → lightweight
        if (cur == m.ppSolid && m.lwSolid >= 0) return zoneShader_->getActiveSolid();
        if (cur == m.ppAlpha && m.lwAlpha >= 0) return zoneShader_->getActiveAlphaTest();
        if (cur == m.ppAtlasSolid && m.lwAtlasSolid >= 0) return zoneShader_->getActiveAtlasSolid();
        if (cur == m.ppAtlasAlpha && m.lwAtlasAlpha >= 0) return zoneShader_->getActiveAtlasAlpha();
        if (cur == m.ppWind && m.lwWind >= 0) return zoneShader_->getActiveWindAlphaTest();
        // Lightweight → per-pixel
        if (cur == m.lwSolid) return zoneShader_->getActiveSolid();
        if (cur == m.lwAlpha) return zoneShader_->getActiveAlphaTest();
        if (cur == m.lwAtlasSolid) return zoneShader_->getActiveAtlasSolid();
        if (cur == m.lwAtlasAlpha) return zoneShader_->getActiveAtlasAlpha();
        if (cur == m.lwWind) return zoneShader_->getActiveWindAlphaTest();
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
    swapNode(zoneMeshNode_);

    // Swap PVS region mesh nodes
    for (auto& [regionIdx, node] : regionMeshNodes_) {
        swapNode(node);
    }

    // Swap fallback mesh node
    swapNode(fallbackMeshNode_);

    // Swap object nodes (trees use wind material)
    for (auto* node : objectNodes_) {
        swapNode(node);
    }

    // Swap door mesh materials and update stored types for future door builds
    int doorSwapped = 0;
    if (doorManager_) {
        doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                             zoneShader_->getActiveAlphaTest());
        // Swap materials on existing door scene nodes
        for (auto* node : doorManager_->getDoorSceneNodes()) {
            swapNode(node);
            ++doorSwapped;
        }
        // Invalidate mesh cache so future rebuilds use the new material types
        doorManager_->invalidateMeshCache("");
    }

    LOG_INFO(MOD_GRAPHICS, "swapZoneMeshMaterials: swapped {}/{} mesh buffers (zone={} regions={} fallback={} objects={} doors={})",
        swapChanged, swapTotal,
        zoneMeshNode_ != nullptr, regionMeshNodes_.size(),
        fallbackMeshNode_ != nullptr, objectNodes_.size(), doorSwapped);

    // Rebuild sorted draw list if manual zone draw is active (material keys changed)
    if (manualZoneDrawEnabled_) {
        sortedDrawEntries_.clear();
    }

    // Swap existing entity mesh materials and update future entity material types
    if (entityRenderer_ && zoneShader_->isAvailable()) {
        entityRenderer_->swapShaderMaterials(
            m.ppSolid, zoneShader_->getActiveSolid(),
            m.ppAlpha, zoneShader_->getActiveAlphaTest());
        // Also swap lw→active in case entities had lightweight materials
        entityRenderer_->swapShaderMaterials(
            m.lwSolid, zoneShader_->getActiveSolid(),
            m.lwAlpha, zoneShader_->getActiveAlphaTest());
        entityRenderer_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                zoneShader_->getActiveAlphaTest());
    }
    if (treeManager_ && zoneShader_->isAvailable()) {
        treeManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                             zoneShader_->getActiveAlphaTest(),
                                             zoneShader_->getActiveAlphaTestRGBA());
    }
}

void IrrlichtRenderer::swapToDebugShader(irr::s32 debugMat) {
    // Swap all per-pixel custom shader materials to the debug visualization program.
    // Stores the original material type in debugOriginalMaterials_ for restoration.
    debugOriginalMaterials_.clear();
    // Custom shader materials start at the first compiled material type (solid)
    irr::s32 firstCustom = zoneShader_ ? zoneShader_->getMaterialTypeSolid() : 256;

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

    if (zoneMeshNode_) swapNodeToDebug(zoneMeshNode_);
    for (auto& [idx, node] : regionMeshNodes_) swapNodeToDebug(node);
    if (fallbackMeshNode_) swapNodeToDebug(fallbackMeshNode_);
    for (auto* node : objectNodes_) swapNodeToDebug(node);
}

void IrrlichtRenderer::swapFromDebugShader(irr::s32 debugMat) {
    // Restore original materials saved by swapToDebugShader.
    for (auto& entry : debugOriginalMaterials_) {
        if (entry.node && entry.materialIndex < entry.node->getMaterialCount()) {
            entry.node->getMaterial(entry.materialIndex).MaterialType =
                static_cast<irr::video::E_MATERIAL_TYPE>(entry.originalType);
        }
    }
    debugOriginalMaterials_.clear();
}

void IrrlichtRenderer::toggleObjectLights() {
    if (isLoading()) return;
    debugObjectLightsEnabled_ = !debugObjectLightsEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Debug: Object lights {}", debugObjectLightsEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleDirectionalLight() {
    if (isLoading()) return;
    debugDirectionalLightEnabled_ = !debugDirectionalLightEnabled_;
    if (zoneShader_ && zoneShader_->isAvailable()) {
        if (!debugDirectionalLightEnabled_) {
            // Fullbright: ambient=1, sun=0, tint=1
            zoneShader_->setAmbientColor(1.0f, 1.0f, 1.0f);
            zoneShader_->setSunColor(0.0f, 0.0f, 0.0f);
            zoneShader_->setTintColor(1.0f, 1.0f, 1.0f);
        } else {
            // Restore time-of-day lighting
            updateTimeOfDay(currentHour_, currentMinute_);
        }
    }
    LOG_INFO(MOD_GRAPHICS, "Debug: Directional/ambient light {}", debugDirectionalLightEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::cycleObjectLights() {
    // Cycle: 0 -> 1 -> 2 -> 3 -> ... -> 8 -> 0 -> ...
    if (maxObjectLights_ >= 8) {
        maxObjectLights_ = 0;
    } else {
        maxObjectLights_++;
    }

    LOG_INFO(MOD_GRAPHICS, "Object lights: {} max", maxObjectLights_);
}

void IrrlichtRenderer::toggleOldModels() {
    if (!entityRenderer_) {
        return;
    }

    auto* loader = entityRenderer_->getRaceModelLoader();
    if (!loader) {
        return;
    }

    bool newState = !loader->isUsingOldModels();
    loader->setUseOldModels(newState);
    loader->clearCache();

    LOG_INFO(MOD_GRAPHICS, "Model mode: {}", (newState ? "Old (Classic)" : "New (Luclin+)"));
}

bool IrrlichtRenderer::isUsingOldModels() const {
    if (!entityRenderer_) {
        return true;  // Default to old models
    }

    auto* loader = entityRenderer_->getRaceModelLoader();
    if (!loader) {
        return true;
    }

    return loader->isUsingOldModels();
}

void IrrlichtRenderer::toggleManualZoneDraw() {
    if (isLoading()) return;
    if (!usePvsCulling_ || regionMeshNodes_.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Manual zone draw not available (no PVS culling)");
        return;
    }
    manualZoneDrawEnabled_ = !manualZoneDrawEnabled_;
    if (manualZoneDrawEnabled_) {
        // S05: renderPassTimer_ already created in initLoadingScreen() — just install
        if (smgr_ && renderPassTimer_) {
            smgr_->setLightManager(renderPassTimer_);
        }
        // Remove zone mesh nodes from scene graph — manual draw accesses them directly
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && node->getParent()) { node->grab(); node->remove(); }
        }
        if (fallbackMeshNode_ && fallbackMeshNode_->getParent()) {
            fallbackMeshNode_->grab(); fallbackMeshNode_->remove();
        }
        // Remove object nodes from scene graph — drawn via sorted draw list
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            auto* node = objectNodes_[i];
            if (node && node->getParent()) {
                node->grab(); node->remove();
                if (i < objectInSceneGraph_.size()) objectInSceneGraph_[i] = false;
            }
        }
    } else {
        // Re-add zone mesh nodes to scene graph for Irrlicht-managed rendering
        for (auto& [regionIdx, node] : regionMeshNodes_) {
            if (node && !node->getParent()) {
                smgr_->getRootSceneNode()->addChild(node);
                node->drop();
            }
        }
        if (fallbackMeshNode_ && !fallbackMeshNode_->getParent()) {
            smgr_->getRootSceneNode()->addChild(fallbackMeshNode_);
            fallbackMeshNode_->drop();
        }
        // Re-add visible object nodes to scene graph
        for (size_t i = 0; i < objectNodes_.size(); ++i) {
            auto* node = objectNodes_[i];
            if (node && !node->getParent()) {
                smgr_->getRootSceneNode()->addChild(node);
                node->drop();
                if (i < objectInSceneGraph_.size()) objectInSceneGraph_[i] = true;
            }
        }
    }
    LOG_INFO(MOD_GRAPHICS, "Manual zone draw (front-to-back sorting): {}",
             manualZoneDrawEnabled_ ? "ENABLED" : "DISABLED");
}

// P10: cross-region zone draw merge runtime toggle (/mergedraws). Mirrors
// /sort's pattern — no scene-graph manipulation needed here (the merge
// decision/build lives entirely inside drawZoneGeometrySorted(), gated on
// this bool); toggling off just makes the next frame take the unmerged
// branch, toggling on lets the next eligible frame attempt a build.
void IrrlichtRenderer::toggleMergeZoneDraws() {
    if (isLoading()) return;
    mergeZoneDrawsEnabled_ = !mergeZoneDrawsEnabled_;
    if (!mergeZoneDrawsEnabled_ && zoneMergeBuffers_) {
        // Free the merge buffers immediately when turned off — no reason to
        // hold the duplicated vertex/index memory while unused.
        zoneMergeBuffers_->destroy();
    }
    // Re-arm the "disabled for this zone" latch when explicitly re-enabled,
    // so a user flipping /mergedraws off/on after an eviction-triggered
    // auto-disable gets a fresh attempt (at the cost of possibly hitting the
    // same eviction-driven fallback again — that's expected, not a bug).
    if (mergeZoneDrawsEnabled_) {
        mergeZoneDrawsDisabledForZone_ = false;
        mergeZoneDrawsBuildEvictionCount_ = constrainedMeshCache_ ? constrainedMeshCache_->getEvictionCount() : 0;
    }
    LOG_INFO(MOD_GRAPHICS, "Zone draw merge (/mergedraws): {}",
             mergeZoneDrawsEnabled_ ? "ENABLED" : "DISABLED");
}

bool IrrlichtRenderer::loadIndoorRegionMap(const std::string& zoneName) {
    indoorRegions_.clear();

    std::string regionMapsPath = config_.regionMapsPath;
    if (regionMapsPath.empty()) regionMapsPath = "data/region_maps";

    std::string mapFile = regionMapsPath + "/" + zoneName + ".json";
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
            indoorRegions_.insert(static_cast<size_t>(r["region"].asUInt()));
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Loaded region map: {} ({} indoor regions out of {} total)",
             mapFile, indoorRegions_.size(), regions.size());
    return true;
}

void IrrlichtRenderer::togglePortalOcclusion() {
    if (isLoading()) return;
    if (!portalOcclusionEligible_) {
        LOG_INFO(MOD_GRAPHICS, "Portal occlusion not available (no portals or too few)");
        return;
    }
    portalOcclusionEnabled_ = !portalOcclusionEnabled_;
    LOG_INFO(MOD_GRAPHICS, "Portal occlusion: {}",
             portalOcclusionEnabled_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::togglePortalDebugDraw() {
    if (isLoading()) return;
    portalDebugDraw_ = !portalDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Portal debug draw: {}",
             portalDebugDraw_ ? "ENABLED" : "DISABLED");
}

void IrrlichtRenderer::toggleStencilDebugDraw() {
    if (isLoading()) return;
    stencilDebugDraw_ = !stencilDebugDraw_;
    LOG_INFO(MOD_GRAPHICS, "Stencil debug draw: {}",
             stencilDebugDraw_ ? "ENABLED" : "DISABLED");
}

} // namespace Graphics
} // namespace EQT
