// Zone-load steps 6 (Objects), 7 (Doors) and 9 (Collision).
//
// Step 9 is grouped with 6 and 7 rather than with the environment steps
// because it consumes exactly what they produce -- region, object and door
// geometry -- into the collision sets.
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
#include "client/graphics/zone_load_context.h"

namespace EQT {
namespace Graphics {

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

} // namespace Graphics
} // namespace EQT
