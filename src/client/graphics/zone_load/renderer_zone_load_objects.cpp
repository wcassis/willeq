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
            // again in VBOs). Vertex-ANIMATED objects (flags, banners) keep a private
            // mesh because their vertices are rewritten every frame. The cache holds
            // one reference per mesh and releases it when the step ends; the nodes
            // keep the mesh alive.
            std::map<const ZoneGeometry*, irr::scene::IMesh*> sharedObjectMeshes;
            size_t sharedMeshHits = 0;
            // WILLEQ_SHARE_OBJECT_MESHES=0 builds one mesh per instance again, with
            // the same binary, for frame-for-frame comparison.
            const char* shareEnv = std::getenv("WILLEQ_SHARE_OBJECT_MESHES");
            const bool shareObjectMeshes = !(shareEnv && shareEnv[0] == '0');

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
                const bool vertexAnimatedObject = objInstance.geometry->animatedVertices &&
                                                  objInstance.geometry->animatedVertices->frames.size() >= 2;
                if (shareObjectMeshes && !vertexAnimatedObject) {
                    auto sharedIt = sharedObjectMeshes.find(objInstance.geometry.get());
                    if (sharedIt != sharedObjectMeshes.end()) {
                        objMesh = sharedIt->second;
                        objMesh->grab();   // balanced by the objMesh->drop() at the end of this iteration
                        ++sharedMeshHits;
                    }
                }
                if (objMesh) {
                    // shared mesh reused
                } else if (!ctx_.currentZone->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                    objMesh = objBuilder.buildTexturedMesh(*objInstance.geometry, ctx_.currentZone->objectTextures);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built textured mesh, texNames={}",
                              oi, objName, objInstance.geometry->textureNames().size());
                } else {
                    objMesh = objBuilder.buildColoredMesh(*objInstance.geometry);
                    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' built colored mesh (no textures)", oi, objName);
                }

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

                if (shareObjectMeshes && !vertexAnimatedObject &&
                    sharedObjectMeshes.find(objInstance.geometry.get()) == sharedObjectMeshes.end()) {
                    objMesh->grab();   // the cache's reference, dropped after the loop
                    sharedObjectMeshes[objInstance.geometry.get()] = objMesh;
                }
                irr::scene::IMeshSceneNode* objNode = ctx_.smgr->addMeshSceneNode(objMesh);
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
                    objNode->getMaterial(mi).Lighting = ctx_.lightingEnabled;
                    objNode->getMaterial(mi).BackfaceCulling = false;
                    objNode->getMaterial(mi).GouraudShading = true;
                    objNode->getMaterial(mi).FogEnable = ctx_.fogEnabled;
                    objNode->getMaterial(mi).Wireframe = ctx_.wireframeMode;
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

                        ctx_.vertexAnimatedMeshes.push_back(std::move(vam));
                        LOG_INFO(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' registered as vertex-animated "
                                 "({} frames, {}ms delay, {} mapped/{} total verts)",
                                 oi, objName, animData->frames.size(), animData->delayMs,
                                 mappedVerts, objInstance.geometry->vertices.size());
                    }
                }

                objNode->setName(objName.c_str());
                objNode->grab();
                size_t newNodeIndex = ctx_.objectNodes.size();
                ctx_.objectNodes.push_back(objNode);
                ctx_.objectPositions.push_back(irr::core::vector3df(ox, oz, oy));
                if (ctx_.zoneBspTree)
                    ctx_.objectRegions.push_back(ctx_.zoneBspTree->findRegionIndexForPoint(ox, oy, oz));
                else
                    ctx_.objectRegions.push_back(SIZE_MAX);
                objNode->updateAbsolutePosition();
                ctx_.objectBoundingBoxes.push_back(objNode->getTransformedBoundingBox());
                deferred.nodeIndex = newNodeIndex;

                // PVS visibility (no SimWorker yet, so all visible).
                // When manualZoneDrawEnabled_, objects are always out of scene graph
                // and rendered via drawZoneGeometrySorted() instead.
                bool pvsVisible = owner_.isRegionPvsVisible(ctx_.objectRegions.back());
                if (!pvsVisible || ctx_.manualZoneDrawEnabled) {
                    objNode->remove();
                    if (!pvsVisible) {
                        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step6: obj[{}] '{}' removed from scene graph (PVS hidden, region={})",
                                  oi, objName, ctx_.objectRegions.back());
                    }
                }
                ctx_.objectInSceneGraph.push_back(pvsVisible && !ctx_.manualZoneDrawEnabled);

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
                        objLight.bspRegion = ctx_.objectRegions.back();

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
            LOG_INFO(MOD_GRAPHICS, "[objmesh] shared meshes: {} built, {} instances reused one", sharedObjectMeshes.size(), sharedMeshHits);
            for (auto& [geom, mesh] : sharedObjectMeshes) mesh->drop();
            sharedObjectMeshes.clear();
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
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step9: smgr_={}, regionWorldTriangles_={}, objectNodes_={}, zoneBspTree_={}",
                  (bool)ctx_.smgr, ctx_.regionWorldTriangles.size(), ctx_.objectNodes.size(), (bool)ctx_.zoneBspTree);
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
            // Vertex-animated meshes are rewritten every frame; their collision
            // is snapshotted here, the load-time pose the old copy held.
            std::vector<const irr::scene::IMesh*> animatedMeshes;
            for (const auto& vam : ctx_.vertexAnimatedMeshes) animatedMeshes.push_back(vam.mesh);
            ctx_.objectCollision.clear();
            for (size_t oi = 0; oi < ctx_.objectNodes.size(); ++oi) {
                auto* node = ctx_.objectNodes[oi];
                if (node && node->getMesh()) {
                    size_t region = (oi < ctx_.objectRegions.size()) ? ctx_.objectRegions[oi] : SIZE_MAX;
                    irr::scene::IMesh* mesh = node->getMesh();
                    const bool animated = std::find(animatedMeshes.begin(), animatedMeshes.end(), mesh) !=
                                          animatedMeshes.end();
                    if (ctx_.objectCollision.addInstance(region, mesh,
                                                         SelectorWorldMatrix(node->getAbsoluteTransformation()),
                                                         animated)) {
                        objectsAdded++;
                    }
                }
            }
            ctx_.objectCollision.finish();
            const auto& oc = ctx_.objectCollision;
            LOG_INFO(MOD_GRAPHICS, "Sequential: {} objects -> {} world-space-equivalent triangles across {} regions; "
                     "held once: {} distinct meshes, {} mesh triangles, {} bytes owned (instances + index), "
                     "{} bytes of mesh arrays read in place (charged to objects; claimants: object draw + collision); "
                     "a per-instance copy would be {} bytes",
                     objectsAdded, oc.worldTriangleCount(), oc.regionCount(), oc.meshCount(),
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
