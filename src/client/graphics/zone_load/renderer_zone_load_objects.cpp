// Zone-load steps 6 (Objects), 7 (Doors) and 9 (Collision).
//
// Step 9 is grouped with 6 and 7 rather than with the environment steps
// because it consumes exactly what they produce -- region, object and door
// geometry -- into the collision sets.
//
// Split out of renderer_zone_load.cpp by T166; see that file's header.

#include <map>
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
#include "client/core/memory_ledger.h"
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
#include "client/graphics/placed_object_meshes.h"

namespace EQT {
namespace Graphics {

void ZoneLoadComponent::loadZoneStep6_Objects(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Objects);  // T491
    // ── Step 6: Objects — zone objects ──────────────────────────────────────
    ctx.updateProgress(75, "Installing zone objects...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ before={}", ctx_.objectLights.size());
        ctx_.deferredObjects.clear();

        if (pendingZoneComputations_ && !pendingZoneComputations_->prebuiltDeferredObjects.empty()) {
            ctx_.deferredObjects = std::move(pendingZoneComputations_->prebuiltDeferredObjects);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: using prebuilt deferred objects, count={}", ctx_.deferredObjects.size());
        } else if (pendingZoneComputations_ && !pendingZoneComputations_->deferredObjectEntries.empty() && ctx_.currentZone) {
            for (auto& [objIdx, bspRegion] : pendingZoneComputations_->deferredObjectEntries) {
                if (objIdx >= ctx_.currentZone->objects.size()) continue;
                const auto& objInstance = ctx_.currentZone->objects[objIdx];
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
                ctx_.deferredObjects.push_back(deferred);
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: built deferred objects from entries, count={}", ctx_.deferredObjects.size());
        } else {
            ctx_.ZoneStream().indexObjectMeshes();
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: used indexObjectMeshes() fallback");
        }

        // Zone bounds
        if (ctx_.currentZone && ctx_.currentZone->geometry) {
            ctx_.zoneBoundsMinX = ctx_.currentZone->geometry->minX;
            ctx_.zoneBoundsMaxX = ctx_.currentZone->geometry->maxX;
            ctx_.zoneBoundsMinY = ctx_.currentZone->geometry->minY;
            ctx_.zoneBoundsMaxY = ctx_.currentZone->geometry->maxY;
            ctx_.zoneBoundsValid = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from geometry: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      ctx_.zoneBoundsMinX, ctx_.zoneBoundsMaxX, ctx_.zoneBoundsMinY, ctx_.zoneBoundsMaxY);
        } else if (!ctx_.regionBoundingBoxes.empty()) {
            float rMinX = std::numeric_limits<float>::max(), rMaxX = std::numeric_limits<float>::lowest();
            float rMinY = std::numeric_limits<float>::max(), rMaxY = std::numeric_limits<float>::lowest();
            for (const auto& [idx, bb] : ctx_.regionBoundingBoxes) {
                rMinX = std::min(rMinX, bb.MinEdge.X); rMaxX = std::max(rMaxX, bb.MaxEdge.X);
                rMinY = std::min(rMinY, bb.MinEdge.Y); rMaxY = std::max(rMaxY, bb.MaxEdge.Y);
            }
            ctx_.zoneBoundsMinX = rMinX; ctx_.zoneBoundsMaxX = rMaxX;
            ctx_.zoneBoundsMinY = rMinY; ctx_.zoneBoundsMaxY = rMaxY;
            ctx_.zoneBoundsValid = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds from regionBoundingBoxes: X=[{:.1f},{:.1f}] Y=[{:.1f},{:.1f}]",
                      ctx_.zoneBoundsMinX, ctx_.zoneBoundsMaxX, ctx_.zoneBoundsMinY, ctx_.zoneBoundsMaxY);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: zone bounds not available (no combined geometry and no region bounding boxes)");
        }

        // Build all object meshes eagerly inline (no deferred/PVS-gated build at runtime)
        {
            ZoneMeshBuilder objBuilder(ctx_.smgr, ctx_.driver, ctx_.device->getFileSystem());
            objBuilder.setUploadCategory("object");  // P13
            if (ctx_.constrainedTextureCache)
                objBuilder.setConstrainedTextureCache(ctx_.constrainedTextureCache);
            if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
                objBuilder.setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                                   ctx_.zoneShader->getActiveAlphaTest(),
                                                   ctx_.zoneShader->getActiveAlphaTestRGBA());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder shader materials set (solid={}, alphaTest={})",
                          ctx_.zoneShader->getActiveSolid(), ctx_.zoneShader->getActiveAlphaTest());
            }
            if (ctx_.zoneShader && ctx_.zoneShader->isAtlasAvailable()) {
                objBuilder.setAtlasShaderMaterialTypes(ctx_.zoneShader->getActiveAtlasSolid(),
                                                         ctx_.zoneShader->getActiveAtlasAlpha());
                LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj builder atlas shader materials set (solid={}, alpha={})",
                          ctx_.zoneShader->getActiveAtlasSolid(), ctx_.zoneShader->getActiveAtlasAlpha());
            }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: totalObjects={}", ctx_.deferredObjects.size());

            size_t totalObjects = ctx_.deferredObjects.size();
            size_t objectsBuilt = 0;
            // T504 (load-step audit): ONE mesh per distinct geometry. Instances share
            // their ZoneGeometry (shared_ptr) but each built its own IMesh, CPU arrays
            // and VBO -- MEASURED dreadlands: 814 instances of 62 geometries,
            // 3.56 MiB of vertex+index arrays where 0.49 MiB would do (and the same
            // again in VBOs). The cache holds one reference per mesh and releases it
            // when the step ends; the nodes keep the mesh alive.
            // B936 (#254): vertex-ANIMATED geometries are shared too. T504 kept them
            // private "because their vertices are rewritten every frame", but every
            // placement of a geometry shows the same keyframe at every instant
            // (placed_object_meshes.h), so the private copies always held the same
            // bytes: growthplane had 4,314 of them for 4 geometries (47.1 MB of
            // `objects` on x86). One mesh per geometry is posed once per keyframe
            // change and registered once (PlacedObjectMeshes).
            // WILLEQ_SHARE_OBJECT_MESHES=0 builds one mesh per instance again (animated
            // ones included, each registered and posed on its own: fable's pre-T504
            // layout), with the same binary, for frame-for-frame comparison.
            const char* shareEnv = std::getenv("WILLEQ_SHARE_OBJECT_MESHES");
            PlacedObjectMeshes placedMeshes(!(shareEnv && shareEnv[0] == '0'));

            for (size_t oi = 0; oi < totalObjects; ++oi) {
            
                DeferredObject& deferred = ctx_.deferredObjects[oi];
                if (deferred.meshBuilt) { ++objectsBuilt; continue; }
                if (deferred.objectIndex >= ctx_.currentZone->objects.size()) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const auto& objInstance = ctx_.currentZone->objects[deferred.objectIndex];
                if (!objInstance.geometry || !objInstance.placeable) {
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                const std::string& objName = objInstance.placeable->getName();

                // Build mesh (or reuse the one already built for this geometry)
                irr::scene::IMesh* objMesh = nullptr;
                const bool vertexAnimatedObject = isVertexAnimatedGeometry(*objInstance.geometry);
                // B936: owner rows for WILLEQ_LEDGER_OWNERS=objects (no effect
                // otherwise). obj.meshes = the shared mesh arrays (built once per
                // geometry); obj.instances = the per-placement node, its
                // transform and material copies; obj.anim = the keyframe binding
                // (once per animated mesh).
                eqt::memory::StartupScope meshOwner("obj.meshes");
                // One reference for this iteration, balanced by the
                // objMesh->drop() at its end.
                objMesh = placedMeshes.acquire(*objInstance.geometry, [&]() -> irr::scene::IMesh* {
                    if (!ctx_.currentZone->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built textured mesh, texNames={}",
                                  oi, objName, objInstance.geometry->textureNames().size());
                        return objBuilder.buildTexturedMesh(*objInstance.geometry, ctx_.currentZone->objectTextures);
                    }
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built colored mesh (no textures)", oi, objName);
                    return objBuilder.buildColoredMesh(*objInstance.geometry);
                });

                // Missing textures are not re-textured later (T04 made the cache
                // synchronous; T475 deleted the dead pendingTextureObjects queue).
                // This line is B671's evidence.
                const auto& missingObjTex = objBuilder.getMissingTextures();
                if (!missingObjTex.empty()) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' has {} missing textures (not re-textured later, board B671)",
                             oi, objName, missingObjTex.size());
                }

                if (!objMesh) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' mesh build returned null — skipping", oi, objName);
                    deferred.meshBuilt = true; ++objectsBuilt; continue;
                }

                eqt::memory::StartupScope instanceOwner("obj.instances");
                // B938 (#256): the per-placement state is a compact record, not a
                // full IMeshSceneNode. In a PVS zone (manualZoneDrawEnabled) the
                // object is drawn by drawZoneGeometrySorted() from the shared
                // mesh's buffers + the record's world transform, so no node is
                // allocated at all. In a non-PVS zone the scene-graph pass
                // (Irrlicht's SOLID drawAll) still needs a real node, so the
                // record carries it there.
                float scaleX = objInstance.placeable->getScaleX();
                float scaleY = objInstance.placeable->getScaleY();
                float scaleZ = objInstance.placeable->getScaleZ();
                float ox = objInstance.placeable->getX();
                float oy = objInstance.placeable->getY();
                float oz = objInstance.placeable->getZ();

                PlacedObjectRecord record;
                record.mesh = objMesh;
                record.name = objName;
                record.position = irr::core::vector3df(ox, oz, oy);
                if (ctx_.zoneBspTree)
                    record.region = ctx_.zoneBspTree->findRegionIndexForPoint(ox, oy, oz);
                // B938 (#256): the record is the placement's ONLY state -- before
                // it the culling data lived in four renderer-parallel vectors
                // (objectPositions_ / objectRegions_ / objectBoundingBoxes_ /
                // objectInSceneGraph_), ~45 B per placement, all indexed by the
                // same placement index. They are folded in here; consumers read
                // the record now.
                // Reference accounting. The acquire() above handed this
                // iteration one reference (balanced by the objMesh->drop() at
                // the end of the loop). In a PVS zone the record is the SOLE
                // holder of the mesh, so it grabs its own (dropped in zone
                // teardown); in a non-PVS zone the node created below holds a
                // reference, so the record must not double-grab.
                const bool needsOwnRef = ctx_.manualZoneDrawEnabled;
                if (needsOwnRef) objMesh->grab();

                // The record's world transform is the old node's
                // getRelativeTransformation(), computed with the exact same
                // three-setter idiom the node used to get (setRotationDegrees +
                // setTranslation, then `*= scale` only when the scale is not
                // (1,1,1)). The manual draw path reads it byte-identically.
                record.world = irr::core::matrix4();
                record.world.setRotationDegrees(irr::core::vector3df(
                    objInstance.placeable->getRotateX(),
                    objInstance.placeable->getRotateY(),
                    objInstance.placeable->getRotateZ()));
                record.world.setTranslation(irr::core::vector3df(ox, oz, oy));
                {
                    const irr::core::vector3df scale(scaleX, scaleZ, scaleY);
                    if (scale != irr::core::vector3df(1.f, 1.f, 1.f)) {
                        irr::core::matrix4 smat;
                        smat.setScale(scale);
                        record.world *= smat;
                    }
                }
                // World-space transformed bounding box, exactly the old
                // node->getTransformedBoundingBox(): the mesh's bounding box
                // transformed by the relative (record.world) matrix.
                record.box = objMesh->getBoundingBox();
                record.world.transformBoxEx(record.box);

                // Non-PVS zone: the scene-graph pass needs a real node with the
                // old per-buffer SMaterial overrides. PVS zones skip this -- the
                // node was the 2.2 KB per placement.
                irr::scene::IMeshSceneNode* objNode = nullptr;
                if (!ctx_.manualZoneDrawEnabled) {
                    objNode = ctx_.smgr->addMeshSceneNode(objMesh);
                    if (!objNode) {
                        LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' addMeshSceneNode failed — mesh dropped", oi, objName);
                        objMesh->drop();
                        record.mesh = nullptr;
                        deferred.meshBuilt = true; ++objectsBuilt; continue;
                    }
                    objNode->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));
                    objNode->setPosition(irr::core::vector3df(ox, oz, oy));
                    objNode->setRotation(irr::core::vector3df(
                        objInstance.placeable->getRotateX(),
                        objInstance.placeable->getRotateY(),
                        objInstance.placeable->getRotateZ()));
                    for (irr::u32 mi = 0; mi < objNode->getMaterialCount(); ++mi) {
                        objNode->getMaterial(mi).Lighting = ctx_.lightingEnabled;
                        objNode->getMaterial(mi).BackfaceCulling = false;
                        objNode->getMaterial(mi).GouraudShading = true;
                        objNode->getMaterial(mi).FogEnable = ctx_.fogEnabled;
                        objNode->getMaterial(mi).Wireframe = ctx_.wireframeMode;
                        objNode->getMaterial(mi).NormalizeNormals = true;
                        objNode->getMaterial(mi).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        objNode->getMaterial(mi).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                    objNode->setName(objName.c_str());
                    objNode->grab();
                    record.node = objNode;
                }

                // Register vertex-animated objects (flags, banners, grass, plants):
                // ONE entry per mesh (B936), however many placements draw it.
                if (vertexAnimatedObject && objMesh) {
                    eqt::memory::StartupScope animOwner("obj.anim");
                    size_t mappedVerts = 0;
                    if (placedMeshes.registerAnimated(*objInstance.geometry, objMesh, objName,
                                                      ctx_.vertexAnimatedMeshes, &mappedVerts)) {
                        const auto& animData = objInstance.geometry->animatedVertices;
                        if (objInstance.geometry->vertices.empty() || animData->frames[0].positions.size() < 3) {
                            LOG_WARN(MOD_GRAPHICS, "SEQ Step6: obj '{}' vertex anim: cannot compute center offset "
                                     "(verts={}, animVerts={})", objName,
                                     objInstance.geometry->vertices.size(), animData->frames[0].positions.size() / 3);
                        }
                        LOG_INFO(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' registered as vertex-animated "
                                 "({} frames, {}ms delay, {} mapped/{} total verts)",
                                 oi, objName, animData->frames.size(), animData->delayMs,
                                 mappedVerts, objInstance.geometry->vertices.size());
                    }
                }

                size_t newNodeIndex = ctx_.objectRecords.size();
                ctx_.objectRecords.push_back(std::move(record));
                deferred.nodeIndex = newNodeIndex;

                // PVS visibility (no SimWorker yet, so all visible).
                // When manualZoneDrawEnabled_, objects are always out of scene graph
                // and rendered via drawZoneGeometrySorted() instead.
                bool pvsVisible = owner_.isRegionPvsVisible(ctx_.objectRecords[newNodeIndex].region);
                if (!pvsVisible || ctx_.manualZoneDrawEnabled) {
                    // Only a real scene-graph node (non-PVS zone) is removed; PVS
                    // records have none.
                    if (ctx_.objectRecords[newNodeIndex].node)
                        ctx_.objectRecords[newNodeIndex].node->remove();
                    if (!pvsVisible) {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' removed from scene graph (PVS hidden, region={})",
                                  oi, objName, ctx_.objectRecords[newNodeIndex].region);
                    }
                }
                ctx_.objectRecords[newNodeIndex].inSceneGraph = pvsVisible && !ctx_.manualZoneDrawEnabled;

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
                    if (ctx_.currentZone && !ctx_.currentZone->lights.empty()) {
                        float bestDist = 50.0f;
                        for (const auto& zoneLight : ctx_.currentZone->lights) {
                            float dx = zoneLight->x - ox;
                            float dy = zoneLight->y - oy;
                            float hDist = std::sqrt(dx * dx + dy * dy);
                            if (hDist < bestDist) {
                                bestDist = hDist;
                                lightPos = irr::core::vector3df(zoneLight->x, zoneLight->z, zoneLight->y);
                            }
                        }
                    }

                    irr::scene::ILightSceneNode* lightNode = ctx_.smgr->addLightSceneNode(
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
                        objLight.bspRegion = ctx_.objectRecords[newNodeIndex].region;

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

                        bool lightPvsVis = owner_.isRegionPvsVisible(objLight.bspRegion);
                        if (lightPvsVis)
                            ctx_.smgr->getRootSceneNode()->addChild(lightNode);
                        ctx_.objectLightInSceneGraph.push_back(lightPvsVis);

                        ctx_.objectLights.push_back(objLight);
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
            LOG_INFO(MOD_GRAPHICS, "[objmesh] shared meshes: {} built, {} instances reused one (sharing {})",
                     placedMeshes.meshesBuilt(), placedMeshes.placementsReusingAMesh(),
                     placedMeshes.sharing() ? "on" : "OFF (WILLEQ_SHARE_OBJECT_MESHES=0)");
            {
                // B936: the shared block is reported ONCE with its claimants
                // (CLAUDE.md's shared-block rule). The mesh arrays are charged to
                // `objects` (owner row obj.meshes when WILLEQ_LEDGER_OWNERS=objects).
                uint64_t animArrayBytes = 0;
                for (const auto& vam : ctx_.vertexAnimatedMeshes) {
                    for (irr::u32 b = 0; vam.mesh && b < vam.mesh->getMeshBufferCount(); ++b) {
                        const auto* buf = vam.mesh->getMeshBuffer(b);
                        animArrayBytes += static_cast<uint64_t>(buf->getVertexCount()) * sizeof(irr::video::S3DVertex) +
                                          static_cast<uint64_t>(buf->getIndexCount()) * sizeof(irr::u16);
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "[objmesh] vertex-animated: {} placements draw {} meshes ({} B of vertex+index "
                         "arrays, held once; claimants: object draw, keyframe pose, object collision snapshot)",
                         placedMeshes.animatedPlacements(), ctx_.vertexAnimatedMeshes.size(), animArrayBytes);
            }
            placedMeshes.release();
            LOG_INFO(MOD_GRAPHICS, "Sequential: built all {} object meshes eagerly", objectsBuilt);
            // T504 (load-step audit): every placeable INSTANCE builds its own mesh
            // from a geometry that instances share. How much is duplicated?
            {
                std::map<const ZoneGeometry*, size_t> instancesPerGeometry;
                uint64_t instanceVertexBytes = 0, distinctVertexBytes = 0;
                uint64_t instanceIndexBytes = 0, distinctIndexBytes = 0;
                for (const auto& inst : ctx_.currentZone->objects) {
                    if (!inst.geometry || !inst.placeable) continue;
                    const uint64_t vb = static_cast<uint64_t>(inst.geometry->vertices.size()) * sizeof(irr::video::S3DVertex);
                    const uint64_t ib = static_cast<uint64_t>(inst.geometry->triangles.size()) * 3 * sizeof(irr::u16);
                    instanceVertexBytes += vb;
                    instanceIndexBytes += ib;
                    if (instancesPerGeometry[inst.geometry.get()]++ == 0) {
                        distinctVertexBytes += vb;
                        distinctIndexBytes += ib;
                    }
                }
                size_t instances = 0;
                for (const auto& [g, n] : instancesPerGeometry) instances += n;
                LOG_INFO(MOD_GRAPHICS, "[objmesh] instances={} distinct_geometries={} | per-instance meshes: {} B vertices + {} B indices; "
                         "one mesh per distinct geometry would be {} B + {} B",
                         instances, instancesPerGeometry.size(), instanceVertexBytes, instanceIndexBytes,
                         distinctVertexBytes, distinctIndexBytes);
            }
        }
        {
            int fireSrcCount = 0;
            for (const auto& ol : ctx_.objectLights) { if (ol.isFireSource) ++fireSrcCount; }
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: objectLights_ after={}, fire sources from objects={}",
                      ctx_.objectLights.size(), fireSrcCount);
        }
        ctx_.ZoneStream().logAssetBuildTime("seq_objects", ctx_.deferredObjects.size(), stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep7_Doors(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Doors);  // T491
    // ── Step 7: Doors — door meshes ────────────────────────────────────────
    ctx.updateProgress(78, "Building door meshes...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step7: doorManager_={}", (bool)ctx_.doorManager);
        // T468 (board B590): the one construction body, which also registers
        // the door texture guard. A no-op construction when setupInstantScene()
        // already built the manager, which is every session.
        ctx_.ZoneLifecycle().ensureDoorManager();

        // Rebuild all doors at once
        if (ctx_.doorManager) {
            std::vector<uint8_t> doorList;
            ctx_.doorManager->getUnbuiltDoors(doorList);
            size_t unbuiltCount = doorList.size();
            size_t placeholderCount = 0;
            for (uint8_t id = 0; id < 255; ++id) {
                const auto* door = ctx_.doorManager->getDoor(id);
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
                bool rebuilt = ctx_.doorManager->rebuildSingleDoor(doorId);
                if (!rebuilt) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} rebuildSingleDoor failed", doorId);
                    ++doorsFailed;
                }
                // Not re-textured later (T475 deleted the dead pendingTextureDoors
                // queue; B640 closed with it). This line is B671's evidence.
                const auto& missing = ctx_.doorManager->getLastMissingTextures();
                if (!missing.empty()) {
                    LOG_WARN(MOD_GRAPHICS, "SEQ Step7: door {} has {} missing textures (not re-textured later, board B671)",
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
        // The cache was wired by ensureDoorManager() and again by Step 1 -- no separate runtime set needed.
        ctx_.ZoneStream().logAssetBuildTime("seq_doors", 0, stepStart);
    }
    FlushThreadLog();
}

void ZoneLoadComponent::loadZoneStep9_Collision(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Spatial);  // T491
    // ── Step 9: Collision ──────────────────────────────────────────────────
    ctx.updateProgress(88, "Setting up collision...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: smgr_={}, regionWorldTriangles_={}, objectRecords={}, zoneBspTree_={}",
                  (bool)ctx_.smgr, ctx_.regionWorldTriangles.size(), ctx_.objectRecords.size(), (bool)ctx_.zoneBspTree);
        {
            // B909: owner labels for WILLEQ_LEDGER_OWNERS=spatial (no effect
            // otherwise -- the rows are printed only for the owner category).
            eqt::memory::StartupScope regionTriOwner("sp.region_tris");
            ctx_.PlayerCamera().setupMinimalZoneCollision();
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: setupMinimalZoneCollision complete, regionWorldTriangles_={}",
                  ctx_.regionWorldTriangles.size());
        // B909b: the owner row of the placed-object collision set (instances,
        // region index, snapshots). It used to hold a world-space copy of
        // every instance's triangles; it now holds each distinct mesh once
        // (read in place from the mesh the draw path holds, charged to
        // `objects`) plus a matrix and a box per instance.
        eqt::memory::StartupScope objectTriOwner("sp.object_instances");

        size_t objectsAdded = 0;
        if (ctx_.smgr) {
            // Vertex-animated meshes are rewritten on every keyframe change; their
            // collision is snapshotted here, the load-time pose the old copy held.
            // B936: one mesh per animated geometry, so one entry per mesh here.
            std::vector<const irr::scene::IMesh*> animatedMeshes;
            for (const auto& vam : ctx_.vertexAnimatedMeshes) animatedMeshes.push_back(vam.mesh);
            ctx_.objectCollision.clear();
            // Sharing census for the log (B909b measured the representation
            // with it): instances and triangles per distinct mesh.
            struct MeshUse { size_t instances = 0; size_t triangles = 0; const char* name = ""; };
            std::map<const irr::scene::IMesh*, MeshUse> meshUse;
            for (size_t oi = 0; oi < ctx_.objectRecords.size(); ++oi) {
                const auto& rec = ctx_.objectRecords[oi];
                if (rec.mesh) {
                    size_t region = rec.region;
                    irr::scene::IMesh* mesh = rec.mesh;
                    const bool animated = std::find(animatedMeshes.begin(), animatedMeshes.end(), mesh) !=
                                          animatedMeshes.end();
                    if (ctx_.objectCollision.addInstance(region, mesh,
                                                         SelectorWorldMatrix(rec.world),
                                                         animated)) {
                        objectsAdded++;
                        MeshUse& use = meshUse[mesh];
                        if (use.instances++ == 0) {
                            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
                                use.triangles += mesh->getMeshBuffer(b)->getIndexCount() / 3;
                            use.name = rec.name.c_str();
                        }
                    }
                }
            }
            ctx_.objectCollision.finish();
            {
                std::vector<MeshUse> uses;
                size_t maxInstances = 0;
                for (const auto& [m, use] : meshUse) {
                    uses.push_back(use);
                    maxInstances = std::max(maxInstances, use.instances);
                }
                std::sort(uses.begin(), uses.end(), [](const MeshUse& a, const MeshUse& b) {
                    return a.instances * a.triangles > b.instances * b.triangles;
                });
                std::string top;
                for (size_t k = 0; k < uses.size() && k < 5; ++k) {
                    top += fmt::format("{}{} x{} ({} tris)", k ? ", " : "", uses[k].name, uses[k].instances,
                                       uses[k].triangles);
                }
                LOG_INFO(MOD_GRAPHICS, "Object collision sharing: {} instances over {} distinct meshes "
                         "(max {} instances of one mesh); largest per-instance-copy contributors: {}",
                         objectsAdded, meshUse.size(), maxInstances, top);
            }
            const auto& oc = ctx_.objectCollision;
            LOG_INFO(MOD_GRAPHICS, "Sequential: {} objects -> {} world-space-equivalent triangles across {} regions; "
                     "held once: {} distinct meshes, {} mesh triangles, {} bytes owned (instances + index), "
                     "{} bytes of mesh arrays read in place (charged to objects; claimants: object draw + collision); "
                     "a per-instance copy would be {} bytes",
                     oc.instanceCount(), oc.worldTriangleCount(), oc.regionCount(), oc.meshCount(),
                     oc.meshTriangleCount(), oc.ownedBytes(), oc.sharedMeshBytes(),
                     oc.worldTriangleCount() * sizeof(irr::core::triangle3df));
        }
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: collision setup complete, "
                  "regionWorldTriangles_={}, objectCollision_ regions={}, doorCollisionData_={}, bspFiltered={}",
                  ctx_.regionWorldTriangles.size(), ctx_.objectCollision.regionCount(),
                  ctx_.doorCollisionData.size(), (bool)(ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()));
        ctx_.ZoneStream().logAssetBuildTime("seq_collision", 0, stepStart);
    }
    FlushThreadLog();
}

} // namespace Graphics
} // namespace EQT
