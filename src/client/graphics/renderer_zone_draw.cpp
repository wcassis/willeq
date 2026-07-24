#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_mesh_cache.h"
#include "common/logging.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer);
extern void gles2DeleteStaticHWBuffer(void* driver, const void* meshBuffer);
#endif

namespace EQT {
namespace Graphics {

namespace {

// P06: estimate a region's projected screen-space coverage, cheaply and
// conservatively -- project the 8 world-space (Irrlicht-coords) AABB corners
// with the current view-projection matrix and take the clamped screen-space
// bounding-rect area. If any corner is at/behind the near plane (w <= 0),
// the region is treated as covering the full screen -- conservative (never
// under-counts), and avoids near-plane clipping math for a decision that
// only needs to be directionally right, not exact (see
// refactor/P06_area_aware_budget.md "Projection details").
float estimateRegionScreenArea(const irr::core::aabbox3df& worldBox,
                                const irr::core::matrix4& viewProj,
                                float screenW, float screenH) {
    const float fullScreenArea = screenW * screenH;
    const irr::core::vector3df& mn = worldBox.MinEdge;
    const irr::core::vector3df& mx = worldBox.MaxEdge;
    const irr::core::vector3df corners[8] = {
        {mn.X, mn.Y, mn.Z}, {mx.X, mn.Y, mn.Z}, {mn.X, mx.Y, mn.Z}, {mx.X, mx.Y, mn.Z},
        {mn.X, mn.Y, mx.Z}, {mx.X, mn.Y, mx.Z}, {mn.X, mx.Y, mx.Z}, {mx.X, mx.Y, mx.Z}
    };

    float minSx = std::numeric_limits<float>::max();
    float maxSx = -std::numeric_limits<float>::max();
    float minSy = std::numeric_limits<float>::max();
    float maxSy = -std::numeric_limits<float>::max();
    for (int i = 0; i < 8; ++i) {
        float clip[4];
        viewProj.transformVect(clip, corners[i]);
        if (clip[3] <= 0.0001f) {
            return fullScreenArea;  // At/behind near plane -- conservative.
        }
        const float invW = 1.0f / clip[3];
        const float sx = (clip[0] * invW * 0.5f + 0.5f) * screenW;
        const float sy = (1.0f - (clip[1] * invW * 0.5f + 0.5f)) * screenH;
        minSx = std::min(minSx, sx);
        maxSx = std::max(maxSx, sx);
        minSy = std::min(minSy, sy);
        maxSy = std::max(maxSy, sy);
    }

    // Clamp to viewport.
    minSx = std::max(0.0f, minSx);
    maxSx = std::min(screenW, maxSx);
    minSy = std::max(0.0f, minSy);
    maxSy = std::min(screenH, maxSy);

    if (maxSx <= minSx || maxSy <= minSy) return 0.0f;
    return (maxSx - minSx) * (maxSy - minSy);
}

} // namespace

void IrrlichtRenderer::createZoneMesh() {
    if (!currentZone_ || !currentZone_->geometry) {
        return;
    }

    if (zoneMeshNode_) {
        zoneMeshNode_->remove();
        zoneMeshNode_ = nullptr;
    }

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());

    // Pass constrained texture cache if in constrained mode
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_);
    }
    // Pass GLSL shader material types if available
    if (zoneShader_ && zoneShader_->isAvailable()) {
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest(),
                                       zoneShader_->getActiveAlphaTestRGBA());
    }

    irr::scene::IMesh* mesh = nullptr;

    if (!currentZone_->textures.empty() && !currentZone_->geometry->textureNames().empty()) {
        mesh = builder.buildTexturedMesh(*currentZone_->geometry, currentZone_->textures);
    } else {
        mesh = builder.buildColoredMesh(*currentZone_->geometry);
    }

    if (mesh) {
        // Use octree scene node for automatic frustum culling of zone geometry
        // minimalPolysPerNode=256 controls octree subdivision granularity
        // This helps with OpenGL/llvmpipe but not with Irrlicht's software renderer
        zoneMeshNode_ = smgr_->addOctreeSceneNode(mesh, nullptr, -1, 256);
        if (zoneMeshNode_) {
            LOG_INFO(MOD_GRAPHICS, "Zone mesh created as octree node (polys per node: 256)");
            for (irr::u32 i = 0; i < zoneMeshNode_->getMaterialCount(); ++i) {
                zoneMeshNode_->getMaterial(i).Lighting = lightingEnabled_;
                zoneMeshNode_->getMaterial(i).BackfaceCulling = false;
                zoneMeshNode_->getMaterial(i).GouraudShading = true;
                zoneMeshNode_->getMaterial(i).FogEnable = fogEnabled_;
                zoneMeshNode_->getMaterial(i).Wireframe = wireframeMode_;
                zoneMeshNode_->getMaterial(i).NormalizeNormals = true;
                // Set material colors for proper lighting response
                zoneMeshNode_->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                zoneMeshNode_->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }

            // NOTE: Collision detection is set up progressively via setupMinimalZoneCollision()
            // and addRegionToCollision() during the background zone loading phases.

            // Initialize animated texture manager for zone textures
            animatedTextureManager_ = std::make_unique<AnimatedTextureManager>(driver_, device_->getFileSystem());
            if (constrainedTextureCache_) {
                animatedTextureManager_->setConstrainedTextureCache(constrainedTextureCache_);
            }
            int animCount = animatedTextureManager_->initialize(
                *currentZone_->geometry, currentZone_->textures, mesh);

            // Auto-detect water texture animations by naming pattern (water01, water02, etc.)
            int waterAnimCount = animatedTextureManager_->detectWaterAnimations(
                currentZone_->textures, mesh);
            animCount += waterAnimCount;

            if (animCount > 0) {
                LOG_DEBUG(MOD_GRAPHICS, "Initialized {} animated zone textures ({} water auto-detected)",
                          animCount, waterAnimCount);
                // Register the zone scene node for animated texture updates
                animatedTextureManager_->addSceneNode(zoneMeshNode_);
            }
        }
        mesh->drop();
    }
}

// ======== OnRenderPassPreRender (ILightManager hook) ========
// Fires after CAMERA pass sets up view/projection, before SOLID pass renders nodes.
// We intercept ESNRP_SOLID to draw zone geometry manually in sorted order.
void IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) {
    passStart_ = std::chrono::steady_clock::now();
    if (!firstPassSeen_) {
        firstPassStart_ = passStart_;
        firstPassSeen_ = true;
    }
    currentPass_ = renderPass;

    // Capture 3D camera transforms during the SOLID pass — this is the only
    // reliable point where ETS_VIEW/ETS_PROJECTION contain the 3D perspective
    // camera matrices. By the time drawAll() returns, they may be overwritten
    // by 2D rendering (billboards, GUI nodes, overlays).
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_) {
        renderer_->captured3DView_ = renderer_->driver_->getTransform(irr::video::ETS_VIEW);
        renderer_->captured3DProj_ = renderer_->driver_->getTransform(irr::video::ETS_PROJECTION);
        renderer_->have3DTransforms_ = true;
    }

    // Hook: draw zone geometry manually before Irrlicht's SOLID pass
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_ && renderer_->manualZoneDrawEnabled_) {
        auto drawStart = std::chrono::steady_clock::now();
        if (renderer_->portalOcclusionEnabled_ && renderer_->portalOcclusionEligible_) {
            renderer_->drawZoneGeometryWithPortals();
        } else {
            renderer_->drawZoneGeometrySorted();
        }
        renderer_->frameTimings_.manualZoneDraw = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - drawStart).count();
    }
}

// ======== uploadMeshHardwareBuffers ========
// Uploads VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void IrrlichtRenderer::uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !driver_ || driver_->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf && buf->getVertexCount() > 0)
            gles2CreateStaticHWBuffer(driver_, buf);
    }
#endif
}

// ======== deleteMeshHardwareBuffers ========
// Deletes VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void IrrlichtRenderer::deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !driver_ || driver_->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf)
            gles2DeleteStaticHWBuffer(driver_, buf);
    }
#endif
}

// ======== drawZoneGeometrySorted ========
// Draws zone geometry sorted by material (primary) and distance (secondary).
// Material sorting minimizes expensive setMaterial() state changes (glUseProgram,
// glBindTexture, glUniform*). Distance sorting within each material group
// preserves front-to-back order for early-Z rejection on tile-based GPUs.
void IrrlichtRenderer::drawZoneGeometrySorted() {
    if (sortedZoneDrawList_.empty() && !fallbackMeshNode_) return;

    // Collect all visible mesh buffers into a flat draw list
    sortedDrawEntries_.clear();

    // Adaptive lighting: if /plight is on and budget > 0, nearest N regions
    // get per-pixel lighting; the rest get lightweight per-vertex shaders.
    const bool adaptiveLight = zoneShader_ && zoneShader_->isPerPixelPlayerLight() &&
                                config_.constrainedConfig.adaptiveLightingZoneBudget > 0;
    const int zonePPBudget = config_.constrainedConfig.adaptiveLightingZoneBudget;
    const int objectPPBudget = config_.constrainedConfig.adaptiveLightingObjectBudget;
    const irr::s32 ppSolid = zoneShader_ ? zoneShader_->getMaterialTypeSolid() : -1;
    const irr::s32 ppAlpha = zoneShader_ ? zoneShader_->getMaterialTypeAlphaTest() : -1;
    const irr::s32 lwSolid = zoneShader_ ? zoneShader_->getMaterialTypeLWSolid() : -1;
    const irr::s32 lwAlpha = zoneShader_ ? zoneShader_->getMaterialTypeLWAlphaTest() : -1;

    // Compute lightweight material override for a buffer beyond the per-pixel budget.
    // Returns -1 if no override needed (keep original material).
    const irr::s32 ppWind = zoneShader_ ? zoneShader_->getMaterialTypeWindAlphaTest() : -1;
    const irr::s32 lwWind = zoneShader_ ? zoneShader_->getMaterialTypeLWWindAlphaTest() : -1;

    static bool adaptiveLightLogOnce = false;
    if (!adaptiveLightLogOnce) {
        LOG_INFO(MOD_GRAPHICS, "[ADAPTIVE] adaptiveLight={} ppSolid={} ppAlpha={} ppWind={} lwSolid={} lwAlpha={} lwWind={} zoneBudget={} objBudget={} zoneBudgetMode={} areaFactor={}",
                 adaptiveLight, ppSolid, ppAlpha, ppWind, lwSolid, lwAlpha, lwWind, zonePPBudget, objectPPBudget,
                 (config_.constrainedConfig.adaptiveLightingBudgetMode ==
                      ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Area) ? "area" : "count",
                 config_.constrainedConfig.adaptiveLightingAreaBudgetFactor);
        adaptiveLightLogOnce = true;
    }

    auto getLightweightOverride = [&](irr::s32 origType) -> irr::s32 {
        if (!adaptiveLight || lwSolid < 0) return -1;
        if (origType == ppSolid) return lwSolid;
        if (origType == ppAlpha) return lwAlpha;
        if (origType == ppWind && lwWind >= 0) return lwWind;
        return -1;
    };

    auto collectMeshBuffers = [&](irr::scene::IMeshSceneNode* node, float distSq,
                                    bool useLightweight = false) {
        irr::scene::IMesh* mesh = node->getMesh();
        if (!mesh) return;

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(node->getPosition());

        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();

            // Compute per-buffer material override for adaptive lighting
            irr::s32 matOverride = useLightweight ? getLightweightOverride(mat.MaterialType) : -1;

            // Material key: (materialType << 16) | textureID
            // Groups all same-shader, same-texture draws together
            uint32_t texId = 0;
            if (mat.getTexture(0)) {
                // Use low 16 bits of texture pointer as a hash
                texId = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
            }
            irr::s32 effectiveType = (matOverride >= 0) ? matOverride : mat.MaterialType;
            uint32_t matKey = (static_cast<uint32_t>(effectiveType) << 16) | texId;

            sortedDrawEntries_.push_back({matKey, distSq, buf, worldMat, matOverride});
        }
    };

    // When portal vis is enabled via BFS (no stencil), filter to portal-visible regions
    const bool useBfsPortalCulling = portalVisibleRegions_ && !portalVisibleRegions_->empty() &&
                                     config_.constrainedConfig.portalOcclusion &&
                                     !config_.constrainedConfig.enableStencilBuffer;

    // Occlusion-culled regions set (from SimWorker's software occlusion pass)
    const std::unordered_set<size_t>* occlusionCulled = nullptr;
    if (currentFrameSimResults_ && !currentFrameSimResults_->occlusionCulledRegions.empty()) {
        occlusionCulled = &currentFrameSimResults_->occlusionCulledRegions;
    }

    // P06: area-mode budget setup. sortedZoneDrawList_ is already nearest-first
    // (frustum-visible regions sorted by distanceSq in sim_visibility.cpp), so
    // the existing draw loop below can accumulate cumulative screen area in
    // order without a separate distance pre-pass.
    const bool areaBudgetMode = adaptiveLight &&
        config_.constrainedConfig.adaptiveLightingBudgetMode ==
            ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Area;
    irr::core::matrix4 zoneViewProj;
    float screenAreaW = 0.0f, screenAreaH = 0.0f, areaBudgetLimit = 0.0f;
    float cumulativeRegionArea = 0.0f;
    if (areaBudgetMode) {
        zoneViewProj = captured3DProj_ * captured3DView_;
        irr::core::dimension2du screenSize = driver_->getScreenSize();
        screenAreaW = static_cast<float>(screenSize.Width);
        screenAreaH = static_cast<float>(screenSize.Height);
        areaBudgetLimit = config_.constrainedConfig.adaptiveLightingAreaBudgetFactor *
                          screenAreaW * screenAreaH;
    }

    int regionsDrawn = 0;
    int portalCulled = 0;
    int occlusionCulledCount = 0;
    for (const auto& entry : sortedZoneDrawList_) {
        if (!entry.node) continue;

        // BFS portal culling: skip regions not reachable through portal walk
        if (useBfsPortalCulling && portalVisibleRegions_->count(entry.regionIdx) == 0) {
            portalCulled++;
            continue;
        }

        // Software occlusion culling: skip regions behind walls/terrain
        if (occlusionCulled && occlusionCulled->count(entry.regionIdx)) {
            occlusionCulledCount++;
            continue;
        }

        auto it = regionMeshNodes_.find(entry.regionIdx);
        if (it == regionMeshNodes_.end() || !it->second) continue;

        bool regionUseLW;
        if (!adaptiveLight) {
            regionUseLW = false;
        } else if (areaBudgetMode) {
            // Add this region's own estimated area to the running total FIRST,
            // then decide. This must include the current region -- the whole
            // point of area mode is that even the FIRST (nearest) region can
            // flip to lightweight if its own screen footprint alone exceeds
            // the budget (the P05 outskirts case: a single huge region covers
            // nearly the whole screen). Deciding on the pre-region total would
            // always give the nearest region a free pass, exactly reproducing
            // the count-mode bug for the single/few-huge-region case.
            irr::scene::IMesh* regionMesh = it->second->getMesh();
            if (regionMesh) {
                irr::core::aabbox3df worldBox = regionMesh->getBoundingBox();
                worldBox.MinEdge += it->second->getPosition();
                worldBox.MaxEdge += it->second->getPosition();
                cumulativeRegionArea += estimateRegionScreenArea(
                    worldBox, zoneViewProj, screenAreaW, screenAreaH);
            }
            regionUseLW = cumulativeRegionArea > areaBudgetLimit;
        } else {
            regionUseLW = regionsDrawn >= zonePPBudget;
        }
        collectMeshBuffers(it->second, entry.distanceSq, regionUseLW);
        regionsDrawn++;
    }

    // Add visible object mesh buffers (sorted alongside zone geometry)
    if (!objectNodes_.empty() && currentFrameSimResults_) {
        const auto* results = currentFrameSimResults_;

        // Get camera position for distance sorting
        irr::core::vector3df camPos(0, 0, 0);
        if (smgr_ && smgr_->getActiveCamera()) {
            camPos = smgr_->getActiveCamera()->getAbsolutePosition();
        }

        int objectsDrawn = 0;
        for (size_t i = 0; i < objectNodes_.size() && i < results->objectVisible.size(); ++i) {
            if (!results->objectVisible[i]) continue;
            auto* node = objectNodes_[i];
            if (!node || !node->getMesh()) continue;

            // Distance from stored object position (Irrlicht coords)
            float distSq = 0.0f;
            if (i < objectPositions_.size()) {
                irr::core::vector3df diff = objectPositions_[i] - camPos;
                distSq = diff.getLengthSQ();
            }

            // Adaptive lighting: beyond object budget, use lightweight
            bool objUseLW = adaptiveLight && objectPPBudget > 0 && objectsDrawn >= objectPPBudget;

            // World transform from node's relative transformation
            irr::core::matrix4 worldMat = node->getRelativeTransformation();

            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 bi = 0; bi < mesh->getMeshBufferCount(); ++bi) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bi);
                if (!buf || buf->getVertexCount() == 0) continue;

                const irr::video::SMaterial& mat = buf->getMaterial();
                irr::s32 matOverride = objUseLW ? getLightweightOverride(mat.MaterialType) : -1;

                if (node->getName() && (std::string(node->getName()) == "TREE6" || std::string(node->getName()) == "TREE1")) {
                    LOG_DEBUG(MOD_GRAPHICS, "[ADAPTIVE] {} obj[{}] buf[{}] matType={} useLW={} override={}",
                              node->getName(), i, bi, (int)mat.MaterialType, objUseLW, matOverride);
                }

                uint32_t texId = 0;
                if (mat.getTexture(0)) {
                    texId = static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(mat.getTexture(0)) & 0xFFFF);
                }
                irr::s32 effectiveType = (matOverride >= 0) ? matOverride : mat.MaterialType;
                uint32_t matKey = (static_cast<uint32_t>(effectiveType) << 16) | texId;

                sortedDrawEntries_.push_back({matKey, distSq, buf, worldMat, matOverride});
            }
            objectsDrawn++;
        }
    }

    // Add fallback mesh buffers (always drawn last — use max distance)
    if (fallbackMeshNode_) {
        collectMeshBuffers(fallbackMeshNode_, 1e18f);
    }

    // Sort by material key (primary), distance (secondary)
    std::sort(sortedDrawEntries_.begin(), sortedDrawEntries_.end(),
        [](const ZoneDrawEntry& a, const ZoneDrawEntry& b) {
            if (a.materialKey != b.materialKey)
                return a.materialKey < b.materialKey;
            return a.distanceSq < b.distanceSq;
        });

    // Draw — setMaterial() is called every time because custom shader callbacks
    // need the current world matrix. However, sorting by material ensures the
    // driver's state tracking (SOGLES2State) skips redundant GL calls for
    // blend/depth/cull/texture/program when consecutive draws share the same material.
    for (const auto& de : sortedDrawEntries_) {
        driver_->setTransform(irr::video::ETS_WORLD, de.worldMat);
        if (de.materialOverride >= 0) {
            // Adaptive lighting: override material type (per-pixel → lightweight)
            irr::video::SMaterial mat = de.buffer->getMaterial();
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
            driver_->setMaterial(mat);
        } else {
            driver_->setMaterial(de.buffer->getMaterial());
        }
        driver_->drawMeshBuffer(de.buffer);
    }

    frameTimings_.manualZoneRegionsDrawn = regionsDrawn;
    frameTimings_.manualZoneDrawCalls = static_cast<int>(sortedDrawEntries_.size());
}

// ======== drawRegionMesh ========
// Draws a single region's mesh buffers with proper world transform.
void IrrlichtRenderer::drawRegionMesh(size_t regionIdx) {
    irr::scene::IMeshSceneNode* node = nullptr;

    if (regionIdx == SIZE_MAX) {
        node = fallbackMeshNode_;
    } else {
        auto it = regionMeshNodes_.find(regionIdx);
        if (it == regionMeshNodes_.end() || !it->second) return;
        node = it->second;
    }

    if (!node) return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    // Set world transform — region nodes only have translation (position), no rotation/scale.
    // Use getPosition() (relative) not getAbsolutePosition() because Irrlicht's OnAnimate()
    // skips invisible nodes, so AbsoluteTransformation is never computed for hidden nodes.
    // Zone mesh nodes are direct children of the root scene node, so relative == absolute.
    irr::core::matrix4 worldMat;
    worldMat.setTranslation(node->getPosition());
    driver_->setTransform(irr::video::ETS_WORLD, worldMat);

    // Draw each mesh buffer
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf || buf->getVertexCount() == 0) continue;
        driver_->setMaterial(buf->getMaterial());
        driver_->drawMeshBuffer(buf);
    }
}

// ======== drawPortalQuad ========
// Draws a portal polygon as a triangle fan (for stencil write/clear).
// Portal vertices are in EQ Z-up coords, converted to Irrlicht Y-up: (x,y,z) -> (x,z,y)
void IrrlichtRenderer::drawPortalQuad(const Portal& portal) {
    size_t n = portal.vertexCount();
    if (n < 3) return;

    std::vector<irr::video::S3DVertex> verts(n);
    for (size_t i = 0; i < n; ++i) {
        verts[i].Pos.X = portal.vx(i);
        verts[i].Pos.Y = portal.vz(i);  // EQ Z -> Irrlicht Y
        verts[i].Pos.Z = portal.vy(i);  // EQ Y -> Irrlicht Z
        verts[i].Color = irr::video::SColor(255, 255, 255, 255);
        verts[i].TCoords.X = 0.0f;
        verts[i].TCoords.Y = 0.0f;
        verts[i].Normal.X = portal.normalX;
        verts[i].Normal.Y = portal.normalZ;
        verts[i].Normal.Z = portal.normalY;
    }

    // Fan triangulation: (0,1,2), (0,2,3), (0,3,4), ...
    std::vector<irr::u16> indices;
    indices.reserve((n - 2) * 3);
    for (size_t i = 1; i + 1 < n; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<irr::u16>(i));
        indices.push_back(static_cast<irr::u16>(i + 1));
    }

    // Identity world transform
    irr::core::matrix4 identity;
    driver_->setTransform(irr::video::ETS_WORLD, identity);

    // Material: no texture, no lighting, depth test but no depth write
    irr::video::SMaterial mat;
    mat.Lighting = false;
    mat.ZBuffer = irr::video::ECFN_LESSEQUAL;
    mat.ZWriteEnable = false;
    mat.BackfaceCulling = false;
    mat.MaterialType = irr::video::EMT_SOLID;
    driver_->setMaterial(mat);

    driver_->drawVertexPrimitiveList(verts.data(), static_cast<irr::u32>(n),
        indices.data(), static_cast<irr::u32>(n - 2),
        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}

// ======== computePortalVisibleRegions ========
// Walk the portal graph from the camera's BSP region via BFS.
// Only regions reachable through portal openings that face the camera
// and intersect the view frustum are added.  Depth limit 3.
void IrrlichtRenderer::buildRegionNeighborMap() {
    regionNeighbors_.clear();
    if (!portalSystem_ || !portalSystem_->hasPortals()) return;

    const auto& portalData = portalSystem_->getData();
    for (const auto& [regionIdx, portalIndices] : portalData.regionPortals) {
        auto& neighbors = regionNeighbors_[regionIdx];
        for (size_t pi : portalIndices) {
            size_t other = portalSystem_->getOtherRegion(pi, regionIdx);
            if (other != SIZE_MAX) neighbors.push_back(other);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "Built region neighbor map: {} regions with portal neighbors",
              regionNeighbors_.size());
}

// ======== drawZoneGeometryWithPortals ========
// Uses stencil-based portal occlusion to render only visible rooms.
void IrrlichtRenderer::drawZoneGeometryWithPortals() {
#ifdef EQT_HAS_GLES2
    if (!portalSystem_ || currentPvsRegion_ == SIZE_MAX) {
        // Fallback: just do front-to-back sorted draw
        drawZoneGeometrySorted();
        return;
    }

    // Draw current room with no stencil masking
    std::unordered_set<size_t> drawnRegions;
    drawRegionMesh(currentPvsRegion_);
    drawnRegions.insert(currentPvsRegion_);

    // Recursively draw visible rooms through portals using stencil
    drawPortalRecursive(currentPvsRegion_, 0, 3, drawnRegions);

    // Disable stencil, restore color mask and depth write to defaults
    // (matches SOGLES2State::reset() defaults so driver cache stays in sync)
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    // Draw fallback mesh (geometry not in any BSP region)
    if (fallbackMeshNode_) {
        drawRegionMesh(SIZE_MAX);
    }
#else
    drawZoneGeometrySorted();
#endif
}

// ======== drawPortalRecursive ========
// Recursively draws rooms visible through portals using nested stencil masking.
void IrrlichtRenderer::drawPortalRecursive(size_t fromRegion, int stencilLevel,
                                            int maxDepth,
                                            std::unordered_set<size_t>& drawn) {
#ifdef EQT_HAS_GLES2
    if (!portalSystem_ || maxDepth <= 0) return;

    const auto& portalIndices = portalSystem_->getPortalsForRegion(fromRegion);

    for (size_t portalIdx : portalIndices) {
        size_t otherRegion = portalSystem_->getOtherRegion(portalIdx, fromRegion);
        if (otherRegion == SIZE_MAX) continue;
        if (drawn.count(otherRegion)) continue;

        // Check PVS: is otherRegion visible from current region?
        if (currentPvsRegion_ != SIZE_MAX && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            const auto& currentRegionData = zoneBspTree_->regions[currentPvsRegion_];
            if (currentRegionData && otherRegion < currentRegionData->visibleRegions.size()) {
                if (!currentRegionData->visibleRegions[otherRegion]) continue;
            }
        }

        // Check if the other region has a mesh
        auto nodeIt = regionMeshNodes_.find(otherRegion);
        if (nodeIt == regionMeshNodes_.end() || !nodeIt->second) continue;

        // Frustum cull the portal center (quick reject)
        if (frustumCuller_ && frustumCuller_->isEnabled()) {
            const Portal& portal = portalSystem_->getData().portals[portalIdx];
            auto bboxIt = regionBoundingBoxes_.find(otherRegion);
            if (bboxIt != regionBoundingBoxes_.end()) {
                const auto& bbox = bboxIt->second;
                if (!frustumCuller_->testAABB(
                        bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                        bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z)) {
                    continue;
                }
            }
        }

        const Portal& portal = portalSystem_->getData().portals[portalIdx];

        // Step 1: Stencil write — draw portal quad, increment stencil where depth passes
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, stencilLevel, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);

        // Step 2: Draw room mesh where stencil == stencilLevel+1
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        drawRegionMesh(otherRegion);
        drawn.insert(otherRegion);

        // Step 3: Recurse into deeper rooms
        drawPortalRecursive(otherRegion, stencilLevel + 1, maxDepth - 1, drawn);

        // Step 4: Stencil pop — decrement stencil back to previous level
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);
    }

    // Restore color/depth after portal loop
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
#endif
}

bool IrrlichtRenderer::rebuildRegionMesh(size_t regionIdx) {
    auto rebuildStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: BEGIN region {} | currentZone_={} wldLoader={} "
             "constrainedMeshCache_={} meshCacheLoaded={} regionMeshNodes_.count={}",
             regionIdx,
             currentZone_ != nullptr,
             (currentZone_ && currentZone_->wldLoader) ? true : false,
             constrainedMeshCache_ != nullptr,
             (constrainedMeshCache_ ? constrainedMeshCache_->isLoaded(regionIdx) : false),
             regionMeshNodes_.count(regionIdx));

    if (!currentZone_ || !currentZone_->wldLoader) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — currentZone_={} wldLoader={}",
                 regionIdx, currentZone_ != nullptr,
                 (currentZone_ ? (currentZone_->wldLoader != nullptr) : false));
        return false;
    }

    // Clean up existing node if rebuilding (prevents scene node leak)
    auto existingIt = regionMeshNodes_.find(regionIdx);
    if (existingIt != regionMeshNodes_.end() && existingIt->second) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: region {} already has node (parent={}), cleaning up",
                 regionIdx, existingIt->second->getParent() != nullptr);
        auto* oldNode = existingIt->second;
        deleteMeshHardwareBuffers(oldNode);
        if (animatedTextureManager_)
            animatedTextureManager_->removeSceneNode(oldNode);
        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
        existingIt->second = nullptr;
    }

    auto geom = currentZone_->wldLoader->getGeometryForRegion(regionIdx);
    if (!geom || geom->vertices.empty()) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — getGeometryForRegion returned {} "
                 "(vertices={})",
                 regionIdx, geom ? "geom" : "nullptr",
                 geom ? geom->vertices.size() : 0);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} geom OK — verts={} tris={} texNames={} "
             "center=({:.1f},{:.1f},{:.1f})",
             regionIdx, geom->vertices.size(), geom->triangles.size(),
             geom->textureNames().size(),
             geom->centerX, geom->centerY, geom->centerZ);

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
    // Only use constrained cache at runtime — during sequential loading, the cache's
    // async background decode submits to bgThreadPool_ which may not be initialized,
    // causing pthread_mutex_lock(NULL) crashes.
    if (constrainedTextureCache_ && !isLoading())
        builder.setConstrainedTextureCache(constrainedTextureCache_);

    bool shaderAvail = zoneShader_ && zoneShader_->isAvailable();
    bool atlasAvail = zoneShader_ && zoneShader_->isAtlasAvailable();
    if (shaderAvail)
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest(),
                                       zoneShader_->getActiveAlphaTestRGBA());
    if (atlasAvail)
        builder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                             zoneShader_->getActiveAtlasAlpha());

    bool hasTextures = !currentZone_->textures.empty();
    bool hasTexNames = !geom->textureNames().empty();

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} build config — shader={} "
             "hasTextures={} (count={}) hasTexNames={} activeSolid={} activeAlpha={}",
             regionIdx, shaderAvail,
             hasTextures, currentZone_->textures.size(),
             hasTexNames,
             shaderAvail ? zoneShader_->getActiveSolid() : -1,
             shaderAvail ? zoneShader_->getActiveAlphaTest() : -1);

    irr::scene::IMesh* mesh = nullptr;
    const char* buildPath = "none";
    auto meshBuildStart = std::chrono::steady_clock::now();

    if (hasTextures && hasTexNames) {
        buildPath = "buildTexturedMesh";
        mesh = builder.buildTexturedMesh(*geom, currentZone_->textures);
    } else {
        buildPath = "buildColoredMesh";
        mesh = builder.buildColoredMesh(*geom);
    }

    auto meshBuildEnd = std::chrono::steady_clock::now();
    auto meshBuildMs = std::chrono::duration_cast<std::chrono::microseconds>(
        meshBuildEnd - meshBuildStart).count() / 1000.0f;

    if (!mesh) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — {} returned null "
                  "(took {:.1f}ms) | textures.size={} texNames.size={}",
                  regionIdx, buildPath, meshBuildMs,
                  currentZone_->textures.size(), geom->textureNames().size());
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} mesh built via {} in {:.1f}ms — "
             "buffers={}", regionIdx, buildPath, meshBuildMs, mesh->getMeshBufferCount());

    // Track textures that were null during build (async GPU upload still pending).
    // When these textures complete upload, this region will be queued for rebuild.
    const auto& missing = builder.getMissingTextures();
    if (!missing.empty()) {
        for (const auto& texName : missing) {
            pendingTextureRegions_[texName].insert(regionIdx);
        }
        LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} has {} missing textures (async pending), "
                 "will rebuild when uploads complete", regionIdx, missing.size());
    }

    auto* node = smgr_->addMeshSceneNode(mesh);
    if (!node) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — addMeshSceneNode returned null",
                  regionIdx);
        mesh->drop();
        return false;
    }

    // Apply standard zone region materials
    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = lightingEnabled_;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).GouraudShading = true;
        node->getMaterial(i).FogEnable = fogEnabled_;
        node->getMaterial(i).Wireframe = wireframeMode_;
        node->getMaterial(i).NormalizeNormals = true;
        node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
        node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
    }
    mesh->drop();

    // Force AbsoluteTransformation update after setPosition — Irrlicht only updates
    // this in OnAnimate() for visible nodes. Since we immediately set the node invisible
    // (below), OnAnimate() will skip it and the AbsoluteTransformation would remain at
    // identity (from the constructor). Triangle selectors created by addRegionToCollision()
    // use getAbsoluteTransformation() to transform collision triangles, so a stale identity
    // transform would place them at center-relative positions instead of world-space.
    node->updateAbsolutePosition();

    // Start invisible — PVS will make it visible on the next Tier2 frame if appropriate.
    // Without this, newly-built nodes are visible by default in Irrlicht, and if PVS
    // doesn't run before the next render, they add to the polygon count unchecked.
    node->setVisible(false);

    // When manual draw is active, remove from scene graph entirely — the manual draw
    // path accesses nodes directly via regionMeshNodes_ map using getPosition()/getMesh().
    if (manualZoneDrawEnabled_) {
        node->grab();
        node->remove();
    }

    // Update renderer state
    regionMeshNodes_[regionIdx] = node;

    // Upload static VBOs for zone geometry (GLES2 only)
    if (!config_.constrainedConfig.skipVBOUpload) {
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_ && gpuUploadEnabled_ && gpuUploadThread_->isAvailable() &&
            node->getMesh() && node->getMesh()->getMeshBufferCount() > 0) {
            // Submit VBO uploads asynchronously — mesh renders via software vertex
            // path (client-side arrays) until each buffer's upload completes.
            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0)
                    continue;

                UploadRequest req;
                req.type = UploadRequestType::VertexBuffer;
                req.vertexCount = buf->getVertexCount();
                req.indexCount = buf->getIndexCount();

                switch (buf->getVertexType()) {
                    case irr::video::EVT_STANDARD:  req.vertexStride = 36; break;
                    case irr::video::EVT_2TCOORDS:  req.vertexStride = 44; break;
                    case irr::video::EVT_TANGENTS:  req.vertexStride = 60; break;
                    default: req.vertexStride = 36; break;
                }

                size_t vboSize = req.vertexCount * req.vertexStride;
                req.vertexData.resize(vboSize);
                std::memcpy(req.vertexData.data(), buf->getVertices(), vboSize);

                req.indexData.resize(req.indexCount);
                std::memcpy(req.indexData.data(), buf->getIndices(),
                            req.indexCount * sizeof(uint16_t));

                // Encode regionIdx (low 48 bits) + buffer index (high 16 bits)
                req.callbackKey = (static_cast<uint64_t>(i) << 48) |
                                  (static_cast<uint64_t>(regionIdx) & 0xFFFFFFFFFFFFULL);
                req.priority = WorkPriorityKey::make(getRegionPvsDepth(regionIdx), AssetType::ZoneMesh).value;

                gpuUploadThread_->submit(std::move(req));
            }
            pendingVBOUploads_.insert(regionIdx);
        } else {
            uploadMeshHardwareBuffers(node);
        }
#else
        uploadMeshHardwareBuffers(node);
#endif
    }

    // Register with animated texture manager (buffer materials only — manual draw path)
    if (animatedTextureManager_) {
        auto geom = currentZone_->wldLoader->getGeometryForRegion(regionIdx);
        if (geom) {
            animatedTextureManager_->addMesh(*geom, currentZone_->textures, node->getMesh());
            LOG_TRACE(MOD_GRAPHICS, "rebuildRegionMesh: region {} addMesh registered", regionIdx);
        }
    }

    // Pre-evict to make room, then update cache
    size_t meshSize = ConstrainedMeshCache::estimateMeshSize(node);
    auto evicted = constrainedMeshCache_->evictUntilAvailable(meshSize, protectedRegions_);
    for (size_t idx : evicted) {
        auto it = regionMeshNodes_.find(idx);
        if (it != regionMeshNodes_.end() && it->second) {
            deleteMeshHardwareBuffers(it->second);
            if (animatedTextureManager_)
                animatedTextureManager_->removeSceneNode(it->second);
            if (it->second->getParent()) it->second->remove(); else it->second->drop();
            it->second = nullptr;
        }
        for (auto& [texName, regions] : pendingTextureRegions_) {
            regions.erase(idx);
        }
    }
    constrainedMeshCache_->onLoaded(regionIdx, node, meshSize);

    auto rebuildEnd = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::microseconds>(
        rebuildEnd - rebuildStart).count() / 1000.0f;

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: SUCCESS region {} — {} bytes, {:.1f}ms total "
             "(mesh build {:.1f}ms) | cache usage {}/{} bytes, {} loaded",
             regionIdx, meshSize, totalMs, meshBuildMs,
             constrainedMeshCache_->getCurrentUsage(),
             constrainedMeshCache_->getMemoryLimit(),
             constrainedMeshCache_->getLoadedCount());
    return true;
}

} // namespace Graphics
} // namespace EQT
