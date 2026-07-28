#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_mesh_cache.h"
#include "client/graphics/entity_prep_worker.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::processFrameLazyLoad() {
    if (!constrainedMeshCache_ || !currentZone_ || !currentZone_->wldLoader) {
        static int lazyLoadSkipLog = 0;
        if (++lazyLoadSkipLog % 300 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: SKIP — cache={} zone={} wld={}",
                      constrainedMeshCache_ != nullptr, currentZone_ != nullptr,
                      (currentZone_ ? (currentZone_->wldLoader != nullptr) : false));
        }
        return;
    }

    // GREEN-only: max 1 region build per frame to stay within budget.
    if (governor_ && governor_->getState() != BudgetState::Green) {
        static int lazyLoadGovLog = 0;
        if (++lazyLoadGovLog % 300 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: SKIP — governor state={} (not GREEN) | "
                      "meshLoadQueue_.size={}", governor_->getStateName(),
                      meshLoadQueue_.size());
        }
        return;
    }

    // Log queue state before scanning
    size_t queueSize = meshLoadQueue_.size();
    size_t alreadyLoaded = 0;
    size_t needBuild = 0;
    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx))
            alreadyLoaded++;
        else
            needBuild++;
    }

    static int lazyLoadQueueLog = 0;
    if (needBuild > 0 || ++lazyLoadQueueLog % 300 == 1) {
        LOG_DEBUG(MOD_GRAPHICS, "processFrameLazyLoad: queue={} (loaded={} needBuild={}) | "
                  "cache usage {}/{} bytes, {} loaded total",
                  queueSize, alreadyLoaded, needBuild,
                  constrainedMeshCache_->getCurrentUsage(),
                  constrainedMeshCache_->getMemoryLimit(),
                  constrainedMeshCache_->getLoadedCount());
    }

    bool builtRegion = false;
    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
        LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: attempting region {} (first unloaded in queue)",
                 entry.regionIdx);
        if (rebuildRegionMesh(entry.regionIdx)) {
            LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: SUCCESS region {} | cache usage {}/{} bytes",
                entry.regionIdx,
                constrainedMeshCache_->getCurrentUsage(),
                constrainedMeshCache_->getMemoryLimit());
            sendLoadProgress(fmt::format("[LazyLoad] Region {} [{}/{}]",
                entry.regionIdx, alreadyLoaded + 1, queueSize));
            builtRegion = true;
        } else {
            LOG_ERROR(MOD_GRAPHICS, "processFrameLazyLoad: FAILED region {} — rebuildRegionMesh returned false",
                      entry.regionIdx);
        }
        break;  // One region max per frame
    }

    // Rebuild one region whose async fallback textures have arrived (if no region was built this frame)
    if (!builtRegion && !textureRebuildQueue_.empty()) {
        auto texEntry = textureRebuildQueue_.front();
        textureRebuildQueue_.erase(textureRebuildQueue_.begin());

        auto existingIt = regionMeshNodes_.find(texEntry.regionIdx);
        if (existingIt != regionMeshNodes_.end() && existingIt->second) {
            deleteMeshHardwareBuffers(existingIt->second);
            if (animatedTextureManager_)
                animatedTextureManager_->removeSceneNode(existingIt->second);
            if (existingIt->second->getParent()) existingIt->second->remove();
            else existingIt->second->drop();
            existingIt->second = nullptr;
        }

        constrainedMeshCache_->markForRebuild(texEntry.regionIdx);
        if (rebuildRegionMesh(texEntry.regionIdx)) {
            LOG_INFO(MOD_GRAPHICS, "processFrameLazyLoad: rebuilt region {} with textures (queue remaining: {})",
                     texEntry.regionIdx, textureRebuildQueue_.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "processFrameLazyLoad: FAILED to rebuild region {} with textures",
                     texEntry.regionIdx);
        }
    }

    // Evict with per-frame cap
    if (constrainedMeshCache_->getCurrentUsage() > constrainedMeshCache_->getMemoryLimit()) {
        constexpr int MAX_EVICTIONS_PER_FRAME = 10;
        int evictionsThisFrame = 0;
        auto evicted = constrainedMeshCache_->evictUntilAvailable(0, protectedRegions_);
        for (size_t idx : evicted) {
            if (evictionsThisFrame >= MAX_EVICTIONS_PER_FRAME) break;
            auto it = regionMeshNodes_.find(idx);
            if (it != regionMeshNodes_.end() && it->second) {
                deleteMeshHardwareBuffers(it->second);
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(it->second);
                if (it->second->getParent()) it->second->remove(); else it->second->drop();
                it->second = nullptr;
            }
            // Remove evicted region from texture rebuild tracking
            for (auto& [texName, regions] : pendingTextureRegions_) {
                regions.erase(idx);
            }
            textureRebuildQueue_.erase(
                std::remove_if(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                    [idx](const TextureRebuildEntry& e) { return e.regionIdx == idx; }),
                textureRebuildQueue_.end());
            evictionsThisFrame++;
        }
    }
}

// ---------------------------------------------------------------------------
// Background mesh build: drain, finalize, material swap, submit
// ---------------------------------------------------------------------------

void IrrlichtRenderer::drainMeshResults() {
    std::lock_guard<std::mutex> lock(meshResultMutex_);
    if (meshResultQueue_.empty()) return;
    for (auto& r : meshResultQueue_)
        localMeshResults_.push_back(std::move(r));
    meshResultQueue_.clear();
}

bool IrrlichtRenderer::finalizeOneMeshResult() {
    if (localMeshResults_.empty()) return false;

    auto result = std::move(localMeshResults_.front());
    localMeshResults_.erase(localMeshResults_.begin());
    pendingMeshBuilds_.erase(result.regionIdx);

    // Discard stale results from a different zone
    if (result.zoneName != currentZoneName_) {
        if (result.mesh) result.mesh->drop();
        return true;  // consumed work (discarded)
    }

    // Clean up existing node if present
    auto existingIt = regionMeshNodes_.find(result.regionIdx);
    if (existingIt != regionMeshNodes_.end() && existingIt->second) {
        auto* oldNode = existingIt->second;
        deleteMeshHardwareBuffers(oldNode);
        if (animatedTextureManager_)
            animatedTextureManager_->removeSceneNode(oldNode);
        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
        existingIt->second = nullptr;
    }

    if (!result.mesh) return true;

    auto* node = smgr_->addMeshSceneNode(result.mesh);
    if (!node) {
        result.mesh->drop();
        return true;
    }

    // Apply standard zone region materials
    node->setPosition(irr::core::vector3df(result.centerX, result.centerZ, result.centerY));
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
    result.mesh->drop();

    node->updateAbsolutePosition();
    node->setVisible(false);

    if (manualZoneDrawEnabled_) {
        node->grab();
        node->remove();
    }

    regionMeshNodes_[result.regionIdx] = node;

    // Assign textures to fallback buffers (placeholder or real if already in cache)
    auto* placeholder = constrainedTextureCache_ ? constrainedTextureCache_->getPlaceholderTexture() : nullptr;
    for (const auto& fb : result.fallbackBuffers) {
        if (fb.bufferIndex >= node->getMesh()->getMeshBufferCount()) continue;
        auto* buf = node->getMesh()->getMeshBuffer(fb.bufferIndex);
        if (!buf) continue;

        auto* tex = constrainedTextureCache_ ? constrainedTextureCache_->getTexture(fb.textureName) : nullptr;
        if (tex) {
            buf->getMaterial().setTexture(0, tex);
            bool hasAlpha = constrainedTextureCache_->hasAlpha(fb.textureName);
            if (hasAlpha && zoneShader_ && zoneShader_->getActiveAlphaTest() >= 0) {
                buf->getMaterial().MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                    zoneShader_->getActiveAlphaTest());
            }
        } else {
            if (placeholder) {
                buf->getMaterial().setTexture(0, placeholder);
            }
            pendingTextureBuffers_[result.regionIdx][fb.textureName].push_back(fb.bufferIndex);
        }
    }

    // Track missing textures for arrival notification
    for (const auto& texName : result.missingTextures) {
        pendingTextureRegions_[texName].insert(result.regionIdx);
    }

    // Upload static VBOs (GLES2 only)
    if (!config_.constrainedConfig.skipVBOUpload) {
#ifdef EQT_HAS_GLES2
        if (gpuUploadThread_ && gpuUploadEnabled_ && gpuUploadThread_->isAvailable() &&
            node->getMesh() && node->getMesh()->getMeshBufferCount() > 0) {
            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0) continue;

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
                std::memcpy(req.indexData.data(), buf->getIndices(), req.indexCount * sizeof(uint16_t));

                req.callbackKey = (static_cast<uint64_t>(i) << 48) |
                                  (static_cast<uint64_t>(result.regionIdx) & 0xFFFFFFFFFFFFULL);
                req.priority = WorkPriorityKey::make(getRegionPvsDepth(result.regionIdx), AssetType::ZoneMesh).value;
                gpuUploadThread_->submit(std::move(req));
            }
            pendingVBOUploads_.insert(result.regionIdx);
        } else {
            uploadMeshHardwareBuffers(node);
        }
#else
        uploadMeshHardwareBuffers(node);
#endif
    }

    // Register with animated texture manager (both mesh buffers and scene node)
    // Register with animated texture manager (buffer materials only — manual draw path)
    if (animatedTextureManager_ && currentZone_ && currentZone_->wldLoader) {
        auto geom = currentZone_->wldLoader->getGeometryForRegion(result.regionIdx);
        if (geom) {
            animatedTextureManager_->addMesh(*geom, currentZone_->textures, node->getMesh());
            LOG_TRACE(MOD_GRAPHICS, "asyncRebuild: region {} addMesh registered", result.regionIdx);
        }
    }

    // Mesh cache bookkeeping (evict + track)
    if (constrainedMeshCache_) {
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
            pendingTextureBuffers_.erase(idx);
            pendingMeshBuilds_.erase(idx);
            for (auto& [texName, regions] : pendingTextureRegions_)
                regions.erase(idx);
        }
        constrainedMeshCache_->onLoaded(result.regionIdx, node, meshSize);
    }

    LOG_INFO(MOD_GRAPHICS, "finalizeOneMeshResult: region {} finalized ({:.1f}ms bg build)",
             result.regionIdx, result.buildTimeMs);
    return true;
}

void IrrlichtRenderer::updateRegionMaterials(size_t regionIdx, const std::string& textureName,
                                              irr::video::ITexture* texture, bool hasAlpha) {
    auto ptbIt = pendingTextureBuffers_.find(regionIdx);
    if (ptbIt == pendingTextureBuffers_.end()) return;

    auto bufIt = ptbIt->second.find(textureName);
    if (bufIt == ptbIt->second.end()) return;

    auto nodeIt = regionMeshNodes_.find(regionIdx);
    if (nodeIt == regionMeshNodes_.end() || !nodeIt->second) {
        ptbIt->second.erase(bufIt);
        if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
        return;
    }

    auto* mesh = nodeIt->second->getMesh();
    if (!mesh) {
        ptbIt->second.erase(bufIt);
        if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
        return;
    }

    for (irr::u32 bufferIdx : bufIt->second) {
        if (bufferIdx >= mesh->getMeshBufferCount()) continue;
        auto* buf = mesh->getMeshBuffer(bufferIdx);
        if (!buf) continue;

        buf->getMaterial().setTexture(0, texture);
        if (hasAlpha && zoneShader_ && zoneShader_->getActiveAlphaTest() >= 0) {
            buf->getMaterial().MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                zoneShader_->getActiveAlphaTest());
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "updateRegionMaterials: region {} texture '{}' → {} buffers",
              regionIdx, textureName, bufIt->second.size());

    ptbIt->second.erase(bufIt);
    if (ptbIt->second.empty()) pendingTextureBuffers_.erase(ptbIt);
}

bool IrrlichtRenderer::processOneMaterialSwap() {
    if (materialSwapQueue_.empty()) return false;

    auto entry = std::move(materialSwapQueue_.front());
    materialSwapQueue_.erase(materialSwapQueue_.begin());
    updateRegionMaterials(entry.regionIdx, entry.textureName, entry.texture, entry.hasAlpha);
    return true;
}

bool IrrlichtRenderer::submitOneBgMeshBuild() {
    if (!constrainedMeshCache_ || !backgroundThreadPool_ || !currentZone_) return false;

    for (const auto& entry : meshLoadQueue_) {
        if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
        if (pendingMeshBuilds_.count(entry.regionIdx)) continue;

        auto zone = currentZone_;
        auto zoneName = currentZoneName_;
        size_t regionIdx = entry.regionIdx;
        int shaderSolid = zoneShader_ ? zoneShader_->getActiveSolid() : -1;
        int shaderAlpha = zoneShader_ ? zoneShader_->getActiveAlphaTest() : -1;
        int shaderAlphaRGBA = zoneShader_ ? zoneShader_->getActiveAlphaTestRGBA() : -1;

        pendingMeshBuilds_.insert(regionIdx);
        uint32_t priority = WorkPriorityKey::make(
            getRegionPvsDepth(regionIdx), AssetType::ZoneMesh).value;

        backgroundThreadPool_->submit(priority, [=, this]() {
            auto geom = zone->wldLoader->getGeometryForRegion(regionIdx);
            if (!geom || geom->vertices.empty()) {
                std::lock_guard<std::mutex> lock(meshResultMutex_);
                meshResultQueue_.push_back({regionIdx, zoneName, nullptr, {}, {}, 0, 0, 0, 0});
                return;
            }

            ZoneMeshBuilder builder(nullptr, nullptr, nullptr);
            builder.setShaderMaterialTypes(shaderSolid, shaderAlpha, shaderAlphaRGBA);

            auto start = std::chrono::steady_clock::now();
            auto* mesh = builder.buildTexturedMesh(
                *geom, zone->textures);
            float elapsed = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            BackgroundMeshResult r;
            r.regionIdx = regionIdx;
            r.zoneName = zoneName;
            r.mesh = static_cast<irr::scene::SMesh*>(mesh);
            r.fallbackBuffers = builder.getFallbackBufferMap();
            r.missingTextures = {builder.getMissingTextures().begin(), builder.getMissingTextures().end()};
            r.centerX = geom->centerX;
            r.centerY = geom->centerY;
            r.centerZ = geom->centerZ;
            r.buildTimeMs = elapsed;

            std::lock_guard<std::mutex> lock(meshResultMutex_);
            meshResultQueue_.push_back(std::move(r));
        });

        return true;  // consumed 1 unit of work
    }
    return false;
}

void IrrlichtRenderer::indexObjectMeshes() {
    if (!currentZone_) {
        return;
    }

    deferredObjects_.clear();

    for (size_t i = 0; i < currentZone_->objects.size(); ++i) {
        const auto& objInstance = currentZone_->objects[i];
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        // P32: the software-path tree skip (isTreeObject + EDT_BURNINGSVIDEO
        // check) was removed — GLES2 is the only backend now, so
        // hasGpuShaders was always true here and this never actually
        // skipped anything on the surviving path. Trees are always indexed
        // normally for PVS/distance culling.

        DeferredObject deferred;
        deferred.objectIndex = i;
        deferred.meshBuilt = false;

        float x = objInstance.placeable->getX();
        float y = objInstance.placeable->getY();
        float z = objInstance.placeable->getZ();

        // Compute BSP region
        if (zoneBspTree_) {
            deferred.bspRegion = zoneBspTree_->findRegionIndexForPoint(x, y, z);
        }

        // Estimate world bounds from geometry extents
        const auto& geom = objInstance.geometry;
        float scaleX = objInstance.placeable->getScaleX();
        float scaleY = objInstance.placeable->getScaleY();
        float scaleZ = objInstance.placeable->getScaleZ();
        // Approximate bounding box in Irrlicht coords (x, z, y)
        float halfW = std::max(std::abs(geom->maxX - geom->minX), std::abs(geom->maxY - geom->minY)) * 0.5f * std::max(scaleX, scaleY);
        float halfH = std::abs(geom->maxZ - geom->minZ) * 0.5f * scaleZ;
        deferred.worldBounds = irr::core::aabbox3df(
            x - halfW, z - halfH, y - halfW,
            x + halfW, z + halfH, y + halfW);

        deferredObjects_.push_back(deferred);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Indexed {} deferred objects", deferredObjects_.size());
}

void IrrlichtRenderer::buildDeferredObject(size_t idx) {
    if (idx >= deferredObjects_.size() || !currentZone_ || !smgr_) {
        return;
    }

    DeferredObject& deferred = deferredObjects_[idx];
    if (deferred.meshBuilt) {
        return;
    }

    if (deferred.objectIndex >= currentZone_->objects.size()) {
        return;
    }

    const auto& objInstance = currentZone_->objects[deferred.objectIndex];
    if (!objInstance.geometry || !objInstance.placeable) {
        deferred.meshBuilt = true;
        return;
    }

    const std::string& objName = objInstance.placeable->getName();

    ZoneMeshBuilder builder(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_) {
        builder.setConstrainedTextureCache(constrainedTextureCache_);
    }
    if (zoneShader_ && zoneShader_->isAvailable()) {
        builder.setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                       zoneShader_->getActiveAlphaTest(),
                                       zoneShader_->getActiveAlphaTestRGBA());
    }
    if (zoneShader_ && zoneShader_->isAtlasAvailable()) {
        builder.setAtlasShaderMaterialTypes(zoneShader_->getActiveAtlasSolid(),
                                             zoneShader_->getActiveAtlasAlpha());
    }

    irr::scene::IMesh* mesh = nullptr;
    if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
        mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
    } else {
        mesh = builder.buildColoredMesh(*objInstance.geometry);
    }

    // Track textures that were missing (async pending) for rebuild when they arrive
    const auto& missingObjTex = builder.getMissingTextures();
    if (!missingObjTex.empty()) {
        for (const auto& texName : missingObjTex) {
            pendingTextureObjects_[texName].insert(idx);
        }
    }

    if (!mesh) {
        deferred.meshBuilt = true;
        return;
    }

    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    if (!node) {
        mesh->drop();
        deferred.meshBuilt = true;
        return;
    }

    float scaleX = objInstance.placeable->getScaleX();
    float scaleY = objInstance.placeable->getScaleY();
    float scaleZ = objInstance.placeable->getScaleZ();
    node->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

    float x = objInstance.placeable->getX();
    float y = objInstance.placeable->getY();
    float z = objInstance.placeable->getZ();
    node->setPosition(irr::core::vector3df(x, z, y));

    float rotX = objInstance.placeable->getRotateX();
    float rotY = objInstance.placeable->getRotateY();
    float rotZ = objInstance.placeable->getRotateZ();
    node->setRotation(irr::core::vector3df(rotX, rotY, rotZ));

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

    // Apply wind shader to tree meshes on GPU path (deferred build).
    // H02d (Option A): do NOT blanket-assign the ETC1-companion wind variant
    // to every tree material — masked RGBA/DDS-decoded foliage (e.g.
    // gfaydark's nekpine.bmp) has no companion alpha texture, so an
    // unconditional override lands it on a shader that samples an unbound
    // unit 1, which the GLES2 driver's opaque-white 1x1 fallback never
    // discards, producing solid opaque quads (P20b root cause). Classify
    // per-material exactly like ZoneMeshBuilder::buildTexturedMesh does:
    // masked+ETC1 -> companion-alpha wind variant (bind companion on unit
    // 1); masked+RGBA -> wind variant reading uTexture.a; non-masked ->
    // plain wind/solid variant (still gets wind vertex displacement, no
    // alpha-test discard).
    if (zoneShader_ && zoneShader_->isWindAvailable()) {
        std::string primaryTexture;
        if (!objInstance.geometry->textureNames().empty()) {
            primaryTexture = objInstance.geometry->textureNames()[0];
        }
        if (treeIdentifier_.isTreeMesh(objName, primaryTexture)) {
            irr::core::aabbox3df meshBbox = mesh->getBoundingBox();
            float meshMinY = meshBbox.MinEdge.Y;
            float meshMaxY = meshBbox.MaxEdge.Y;
            const auto& textureNames = objInstance.geometry->textureNames();
            const auto& textureMasked = objInstance.geometry->textureMasked();
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                auto& mat = node->getMaterial(i);
                irr::video::ITexture* tex0 = mat.TextureLayer[0].Texture;
                std::string texName = tex0 ? std::string(tex0->getName().getPath().c_str())
                                            : primaryTexture;
                bool isMaskedMaterial = false;
                for (size_t ti = 0; ti < textureNames.size(); ++ti) {
                    if (textureNames[ti] == texName) {
                        isMaskedMaterial = (ti < textureMasked.size()) && textureMasked[ti];
                        break;
                    }
                }
                if (isMaskedMaterial) {
                    bool isETC1 = constrainedTextureCache_ && !texName.empty() &&
                        constrainedTextureCache_->isETC1Texture(texName);
                    if (isETC1) {
                        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            zoneShader_->getActiveWindAlphaTest());
                        if (constrainedTextureCache_ && !mat.TextureLayer[1].Texture) {
                            irr::video::ITexture* alphaTex =
                                constrainedTextureCache_->getAlphaTexture(texName);
                            if (alphaTex) mat.setTexture(1, alphaTex);
                        }
                    } else {
                        irr::s32 windRGBAType = zoneShader_->getActiveWindAlphaTestRGBA();
                        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            windRGBAType >= 0 ? windRGBAType : zoneShader_->getActiveWindAlphaTest());
                    }
                } else {
                    irr::s32 windSolidType = zoneShader_->getActiveWindSolid();
                    mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                        windSolidType >= 0 ? windSolidType : zoneShader_->getActiveWindAlphaTest());
                }
                mat.MaterialTypeParam = meshMinY;
                mat.MaterialTypeParam2 = meshMaxY;
                mat.BackfaceCulling = false;
            }
        }
    }

    if (animatedTextureManager_ && objInstance.geometry) {
        animatedTextureManager_->addMesh(*objInstance.geometry, currentZone_->objectTextures, mesh);
        animatedTextureManager_->addSceneNode(node);
    }

    node->setName(objName.c_str());
    node->grab();
    size_t newNodeIndex = objectNodes_.size();
    objectNodes_.push_back(node);
    objectPositions_.push_back(irr::core::vector3df(x, z, y));
    if (zoneBspTree_) {
        objectRegions_.push_back(zoneBspTree_->findRegionIndexForPoint(x, y, z));
    } else {
        objectRegions_.push_back(SIZE_MAX);
    }
    node->updateAbsolutePosition();
    objectBoundingBoxes_.push_back(node->getTransformedBoundingBox());

    // Track deferred→node mapping for texture rebuild
    deferred.nodeIndex = newNodeIndex;

    // PVS check at insertion — start hidden if not in visible region.
    // When manualZoneDrawEnabled_, objects are always out of scene graph
    // and rendered via drawZoneGeometrySorted() instead.
    bool pvsVisible = isRegionPvsVisible(objectRegions_.back());
    if (!pvsVisible || manualZoneDrawEnabled_) {
        node->remove();  // Remove from scene graph (grab() keeps it alive)
    }
    objectInSceneGraph_.push_back(pvsVisible && !manualZoneDrawEnabled_);

    // Create object light if this is a light source (torch, lantern, etc.)
    // Create object light if this is a light source (torch, lantern, etc.)
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
                upperTex.find("TORCH") != std::string::npos) {
                hasFireTexture = true;
            }
            if (upperTex.find("LANTERN") != std::string::npos ||
                upperTex.find("LANT") != std::string::npos) {
                hasLanternTexture = true;
            }
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
        irr::core::vector3df lightPos(x, z, y);

        // Try to find a nearby zone light with the correct elevated position
        if (currentZone_ && !currentZone_->lights.empty()) {
            float bestDist = 50.0f;
            for (const auto& zoneLight : currentZone_->lights) {
                float dx = zoneLight->x - x;
                float dy = zoneLight->y - y;
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

            // grab() + remove() pattern: keep node alive but out of scene graph
            lightNode->grab();
            lightNode->remove();

            ObjectLight objLight;
            objLight.node = lightNode;
            objLight.position = lightPos;
            objLight.objectName = objName;
            objLight.originalColor = lightColor;
            objLight.bspRegion = objectRegions_.back();  // Same BSP region as parent object

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

            // Initial PVS check — only add to scene if region is visible
            bool pvsVis = isRegionPvsVisible(objLight.bspRegion);
            if (pvsVis) {
                smgr_->getRootSceneNode()->addChild(lightNode);
            }
            objectLightInSceneGraph_.push_back(pvsVis);

            objectLights_.push_back(objLight);
            LOG_DEBUG(MOD_GRAPHICS, "Deferred object light created: {} at ({:.1f},{:.1f},{:.1f}) region={}",
                objName, lightPos.X, lightPos.Y, lightPos.Z, objLight.bspRegion);
        }
    }

    mesh->drop();
    deferred.meshBuilt = true;
}

void IrrlichtRenderer::processFrameProgressiveLoad() {
    if (!progressiveLoadingActive_) return;

    // Promote background-preloaded model data to main cache.
    // This moves RaceModelData from the staging map to loadedModels_ so that
    // getMeshForRace() can find it. Must happen before any buildEntityMesh() calls.
    if (entityPrepReady_ && entityRenderer_ && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->promotePreparedModels();
    }

    // --- Critical tasks: always proceed regardless of governor state ---

    // P1: Player's BSP region (must have ground to stand on)
    if (constrainedMeshCache_ && currentPvsRegion_ != SIZE_MAX &&
        !constrainedMeshCache_->isLoaded(currentPvsRegion_)) {
        LOG_INFO(MOD_GRAPHICS, "Progressive P1: building CRITICAL player region {} (not loaded in cache)",
                 currentPvsRegion_);
        bool ok = rebuildRegionMesh(currentPvsRegion_);
        if (ok) {
            addRegionToCollision(currentPvsRegion_);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1: SUCCESS built player region {} + collision",
                     currentPvsRegion_);
        } else {
            LOG_ERROR(MOD_GRAPHICS, "Progressive P1: FAILED to build player region {}!",
                      currentPvsRegion_);
        }
    }

    // Player entity goes through processOneEntityBuildStep() like other entities
    // (no synchronous build here — avoids 7.6s render thread stall from disk I/O)

    // Update loading screen progress during asset loading.
    // --- GREEN gate: exactly ONE non-critical step per frame when GREEN ---
    // Bypass when loading screen is visible (no scene to compete with).
    if (governor_ && !loadingScreenVisible_ && governor_->getState() != BudgetState::Green) {
        static int progGovLog = 0;
        if (++progGovLog % 300 == 1) {
            size_t qNeedBuild = 0;
            for (const auto& e : meshLoadQueue_)
                if (constrainedMeshCache_ && !constrainedMeshCache_->isLoaded(e.regionIdx))
                    qNeedBuild++;
            LOG_DEBUG(MOD_GRAPHICS, "Progressive: GREEN gate blocked — governor={} | "
                      "meshLoadQueue_.size={} needBuild={}",
                      governor_->getStateName(), meshLoadQueue_.size(), qNeedBuild);
        }
        // Populate pending queue even when not GREEN (zero-cost, main-thread-only).
        // Do NOT dispatch — worker stays idle during non-GREEN frames.
        if (++entityPrepScanCounter_ % 10 == 0) {
            queueEntityPrepRequests();
        }
        checkProgressiveLoadingComplete();
        return;
    }

    bool didWork = false;
    // During loading screen, never mark didWork so all priorities run each frame
    // (no scene rendering to compete with).
    const bool canMarkWork = !loadingScreenVisible_;
    auto stepStart = std::chrono::steady_clock::now();

    // Lightweight entity ops (always run under GREEN, no didWork cost):
    if (entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        if (++entityPrepScanCounter_ % 10 == 0) {
            queueEntityPrepRequests();
        }
        if (entityPrepWorker_ && entityPrepWorker_->isIdle()) {
            entityPrepWorker_->dispatchOne();
        }
    }

    // Priority 1: Heavy entity ops (cost didWork budget)
    if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        // Poll completed prep results — distributing a result consumes the budget
        if (entityRenderer_->pollAndDistributePrepResults()) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_poll", 0, stepStart);
        }
    }
    if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild) {
        if (entityRenderer_->processOneEntityBuildStep(currentPvsRegion_)) {
            didWork = canMarkWork;
            logAssetBuildTime("entity_step", 0, stepStart);
            // Report entity progress to chat
            size_t totalEntities = 0, builtEntities = 0;
            for (const auto& [id, vis] : entityRenderer_->getEntities()) {
                if (vis.inSceneGraph) {
                    totalEntities++;
                    if (vis.meshBuilt) builtEntities++;
                }
            }
            sendLoadProgress(fmt::format("[Load] Entity {}/{}", builtEntities, totalEntities));
        }
    }

    // Priority 1.5: Rebuild one region whose async fallback textures have arrived
    if (!didWork && constrainedMeshCache_ && !textureRebuildQueue_.empty()) {
        auto entry = textureRebuildQueue_.front();
        textureRebuildQueue_.erase(textureRebuildQueue_.begin());

        // Clean up existing node before rebuild
        auto existingIt = regionMeshNodes_.find(entry.regionIdx);
        if (existingIt != regionMeshNodes_.end() && existingIt->second) {
            deleteMeshHardwareBuffers(existingIt->second);
            if (animatedTextureManager_)
                animatedTextureManager_->removeSceneNode(existingIt->second);
            if (existingIt->second->getParent()) existingIt->second->remove();
            else existingIt->second->drop();
            existingIt->second = nullptr;
        }

        constrainedMeshCache_->markForRebuild(entry.regionIdx);
        if (rebuildRegionMesh(entry.regionIdx)) {
            addRegionToCollision(entry.regionIdx);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1.5: rebuilt region {} with textures (queue remaining: {})",
                     entry.regionIdx, textureRebuildQueue_.size());
            didWork = canMarkWork;
            logAssetBuildTime("tex_rebuild", entry.regionIdx, stepStart);
        } else {
            LOG_WARN(MOD_GRAPHICS, "Progressive P1.5: FAILED to rebuild region {} with textures",
                     entry.regionIdx);
        }
    }

    // Priority 1.6: Rebuild one door whose async textures have arrived
    if (!didWork && !doorTextureRebuildQueue_.empty() && doorManager_) {
        uint8_t doorId = doorTextureRebuildQueue_.front();
        doorTextureRebuildQueue_.erase(doorTextureRebuildQueue_.begin());
        doorManager_->rebuildSingleDoor(doorId);
        didWork = canMarkWork;
        logAssetBuildTime("door_tex_rebuild", doorId, stepStart);
        LOG_INFO(MOD_GRAPHICS, "Progressive P1.6: rebuilt door {} with textures (queue: {})",
                 doorId, doorTextureRebuildQueue_.size());
    }

    // Priority 1.7: Rebuild one PVS object whose async textures have arrived
    if (!didWork && !objectTextureRebuildQueue_.empty()) {
        size_t objIdx = objectTextureRebuildQueue_.front();
        objectTextureRebuildQueue_.erase(objectTextureRebuildQueue_.begin());
        if (objIdx < deferredObjects_.size()) {
            // Find and remove the existing scene node for this deferred object
            size_t nodeIdx = deferredObjects_[objIdx].nodeIndex;
            if (nodeIdx != SIZE_MAX && nodeIdx < objectNodes_.size() && objectNodes_[nodeIdx]) {
                auto* oldNode = objectNodes_[nodeIdx];
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(oldNode);
                if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
                objectNodes_[nodeIdx] = nullptr;
                if (nodeIdx < objectInSceneGraph_.size())
                    objectInSceneGraph_[nodeIdx] = false;
            }
            deferredObjects_[objIdx].meshBuilt = false;
            buildDeferredObject(objIdx);
            didWork = canMarkWork;
            logAssetBuildTime("obj_tex_rebuild", objIdx, stepStart);
            LOG_INFO(MOD_GRAPHICS, "Progressive P1.7: rebuilt object {} with textures (queue: {})",
                     objIdx, objectTextureRebuildQueue_.size());
        }
    }

    // Priority 2: Build one PVS neighbor region
    if (!didWork && constrainedMeshCache_) {
        size_t qNeedBuild = 0;
        size_t firstUnloaded = SIZE_MAX;
        for (const auto& entry : meshLoadQueue_) {
            if (!constrainedMeshCache_->isLoaded(entry.regionIdx)) {
                if (getRegionPvsDepth(entry.regionIdx) > static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth)) continue;
                qNeedBuild++;
                if (firstUnloaded == SIZE_MAX) firstUnloaded = entry.regionIdx;
            }
        }
        if (qNeedBuild > 0) {
            LOG_INFO(MOD_GRAPHICS, "Progressive P2: attempting region {} ({} need building in queue of {})",
                     firstUnloaded, qNeedBuild, meshLoadQueue_.size());
        }

        for (const auto& entry : meshLoadQueue_) {
            if (constrainedMeshCache_->isLoaded(entry.regionIdx)) continue;
            // PVS depth gate: skip regions beyond configured depth
            if (getRegionPvsDepth(entry.regionIdx) > static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth)) continue;
            if (rebuildRegionMesh(entry.regionIdx)) {
                addRegionToCollision(entry.regionIdx);
                LOG_INFO(MOD_GRAPHICS, "Progressive P2: SUCCESS region {} + collision (PVS={}, queue={}, "
                          "needBuild={})",
                          entry.regionIdx, currentPvsRegion_, meshLoadQueue_.size(), qNeedBuild - 1);
                didWork = canMarkWork;
                logAssetBuildTime("region", entry.regionIdx, stepStart);
                sendLoadProgress(fmt::format("[Load] Region {} [{}/{}]",
                    entry.regionIdx, meshLoadQueue_.size() - (qNeedBuild - 1), meshLoadQueue_.size()));
            } else {
                LOG_ERROR(MOD_GRAPHICS, "Progressive P2: FAILED region {} — rebuildRegionMesh returned false "
                          "(PVS={}, queue={})",
                          entry.regionIdx, currentPvsRegion_, meshLoadQueue_.size());
            }
            break;  // One attempt max
        }
    }

    // Priority 3: Build one door in current PVS set (nearest first)
    if (!didWork && doorManager_) {
        std::vector<uint8_t> pvsDoors;
        doorManager_->getDoorsInRegions(protectedRegions_, pvsDoors);
        // PVS depth gate: remove doors beyond configured depth
        pvsDoors.erase(std::remove_if(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t doorId) {
            const auto* dv = doorManager_->getDoor(doorId);
            if (!dv) return true;
            return getRegionPvsDepth(dv->bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth);
        }), pvsDoors.end());
        std::sort(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t a, uint8_t b) {
            const auto* da = doorManager_->getDoor(a);
            const auto* db = doorManager_->getDoor(b);
            if (!da || !db) return false;
            float distA = (da->x - playerX_) * (da->x - playerX_) + (da->y - playerY_) * (da->y - playerY_);
            float distB = (db->x - playerX_) * (db->x - playerX_) + (db->y - playerY_) * (db->y - playerY_);
            return distA < distB;
        });
        size_t totalPvsDoors = pvsDoors.size();
        size_t doorIndex = 0;
        for (auto doorId : pvsDoors) {
            if (doorManager_->isDoorMeshBuilt(doorId)) continue;
            // Use rebuildSingleDoor to properly clean up placeholder nodes
            // before building real mesh (direct buildDoorMesh leaks placeholders)
            doorManager_->rebuildSingleDoor(doorId);
            // Track any textures that were pending async upload
            const auto& missingDoorTex = doorManager_->getLastMissingTextures();
            if (!missingDoorTex.empty()) {
                for (const auto& texName : missingDoorTex) {
                    pendingTextureDoors_[texName].insert(doorId);
                }
            }
            addDoorToCollision(doorId);
            didWork = canMarkWork;
            logAssetBuildTime("door", doorId, stepStart);
            const auto* dv = doorManager_->getDoor(doorId);
            sendLoadProgress(fmt::format("[Load] Door {} '{}' [{}/{}]",
                doorId, dv ? dv->modelName : "?", doorIndex + 1, totalPvsDoors));
            break;  // One door max
        }
    }

    // Priority 4: Build one PVS object
    if (!didWork) {
        size_t totalPvsObjects = 0, builtPvsObjects = 0;
        for (const auto& obj : deferredObjects_) {
            if (obj.bspRegion < protectedRegions_.size() && protectedRegions_[obj.bspRegion] &&
                getRegionPvsDepth(obj.bspRegion) <= static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth)) {
                totalPvsObjects++;
                if (obj.meshBuilt) builtPvsObjects++;
            }
        }
        // Collect unbuilt PVS objects with distance, sort nearest-first
        std::vector<std::pair<size_t, float>> objCandidates;
        for (size_t i = 0; i < deferredObjects_.size(); ++i) {
            if (deferredObjects_[i].meshBuilt) continue;
            if (deferredObjects_[i].bspRegion >= protectedRegions_.size() || !protectedRegions_[deferredObjects_[i].bspRegion]) continue;
            // PVS depth gate
            if (getRegionPvsDepth(deferredObjects_[i].bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth)) continue;
            auto center = deferredObjects_[i].worldBounds.getCenter();
            // worldBounds is Irrlicht Y-up: (x, z_eq, y_eq)
            float dx = center.X - playerX_;
            float dy = center.Z - playerY_;  // Irr Z = EQ Y
            objCandidates.push_back({i, dx*dx + dy*dy});
        }
        std::sort(objCandidates.begin(), objCandidates.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        if (!objCandidates.empty()) {
            size_t i = objCandidates[0].first;
            buildDeferredObject(i);
            addObjectToCollision(objectNodes_.size() - 1);
            didWork = canMarkWork;
            logAssetBuildTime("pvs_object", i, stepStart);
            std::string objName = "?";
            if (currentZone_ && deferredObjects_[i].objectIndex < currentZone_->objects.size()) {
                const auto& objInstance = currentZone_->objects[deferredObjects_[i].objectIndex];
                if (objInstance.placeable) objName = objInstance.placeable->getName();
            }
            sendLoadProgress(fmt::format("[Load] Object '{}' [{}/{}]",
                objName, builtPvsObjects + 1, totalPvsObjects));
        }
    }

    // Priority 5: Lazy icon extraction (disabled — WindowManager removed)

    // Always queue background prep requests (free work on background thread)
    queueEntityPrepRequests();

    // Mesh cache eviction (lightweight, cap per frame)
    if (constrainedMeshCache_ &&
        constrainedMeshCache_->getCurrentUsage() > constrainedMeshCache_->getMemoryLimit()) {
        constexpr int MAX_EVICTIONS_PER_FRAME = 10;
        int evictionsThisFrame = 0;
        auto evicted = constrainedMeshCache_->evictUntilAvailable(0, protectedRegions_);
        for (size_t idx : evicted) {
            if (evictionsThisFrame >= MAX_EVICTIONS_PER_FRAME) break;
            auto it = regionMeshNodes_.find(idx);
            if (it != regionMeshNodes_.end() && it->second) {
                deleteMeshHardwareBuffers(it->second);
                if (animatedTextureManager_)
                    animatedTextureManager_->removeSceneNode(it->second);
                if (it->second->getParent()) it->second->remove(); else it->second->drop();
                it->second = nullptr;
            }
            // Remove evicted region from texture rebuild tracking
            for (auto& [texName, regions] : pendingTextureRegions_) {
                regions.erase(idx);
            }
            textureRebuildQueue_.erase(
                std::remove_if(textureRebuildQueue_.begin(), textureRebuildQueue_.end(),
                    [idx](const TextureRebuildEntry& e) { return e.regionIdx == idx; }),
                textureRebuildQueue_.end());
            evictionsThisFrame++;
        }
    }

    if (didWork) {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: 1 step this frame ({:.1f}ms remaining, gov=GREEN)",
            governor_ ? governor_->getRemainingBudgetMs() : 0.0f);
    }

    checkProgressiveLoadingComplete();
}

uint8_t IrrlichtRenderer::getRegionPvsDepth(size_t regionIdx) const {
    if (!regionPvsDepthMap_) return 255;
    auto it = regionPvsDepthMap_->find(regionIdx);
    return (it != regionPvsDepthMap_->end()) ? it->second : 255;
}

void IrrlichtRenderer::rebuildPvsNeighborhood(size_t anchorRegion) {
    pvsNeighborhood_.clear();
    pvsNeighborhoodAnchor_ = anchorRegion;
    if (anchorRegion == SIZE_MAX || regionNeighbors_.empty()) return;

    const int K = config_.constrainedConfig.pvsNeighborhoodHops;
    if (K <= 0) return;  // Disabled: fire onPvsRegionChanged on every crossing

    struct Entry { size_t region; int depth; };
    std::vector<Entry> queue;
    queue.push_back({anchorRegion, 0});
    pvsNeighborhood_.insert(anchorRegion);

    size_t head = 0;
    while (head < queue.size()) {
        auto [fromRegion, depth] = queue[head++];
        if (depth >= K) continue;
        auto it = regionNeighbors_.find(fromRegion);
        if (it == regionNeighbors_.end()) continue;
        for (size_t neighbor : it->second) {
            if (pvsNeighborhood_.insert(neighbor).second) {
                queue.push_back({neighbor, depth + 1});
            }
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "PVS neighborhood rebuilt: anchor={}, {} regions (K={})",
              anchorRegion, pvsNeighborhood_.size(), K);
}

bool IrrlichtRenderer::isInPvsNeighborhood(size_t regionIdx) const {
    return pvsNeighborhoodAnchor_ != SIZE_MAX &&
           pvsNeighborhood_.count(regionIdx) > 0;
}

void IrrlichtRenderer::onPvsRegionChanged() {
    // Re-sort entity prep pending queue with updated PVS depths
    if (entityPrepWorker_) {
        entityPrepWorker_->updateDepths([this](size_t bspRegion) {
            return getRegionPvsDepth(bspRegion);
        });
    }

#ifdef EQT_HAS_GLES2
    // Re-prioritize GPU upload queue
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        gpuUploadThread_->reprioritize([this](const UploadRequest& req) -> uint32_t {
            uint8_t sourceType = static_cast<uint8_t>(req.callbackKey >> 56);
            if (sourceType == 0 || sourceType == 1) {
                // VBO upload: low 48 bits = region index
                size_t regionIdx = req.callbackKey & 0xFFFFFFFFFFFFULL;
                uint8_t depth = getRegionPvsDepth(regionIdx);
                return WorkPriorityKey::make(depth, AssetType::ZoneMesh).value;
            } else if (sourceType == 3) {
                // Constrained cache texture (unified: zone + entity textures)
                return WorkPriorityKey::make(0, AssetType::ZoneTexture).value;
            }
            // Icons and unknown: lowest priority
            return WorkPriorityKey::makeNonSpatial(AssetType::Icon).value;
        });
    }
#endif
    // meshLoadQueue_ and textureRebuildQueue_ are repopulated each frame — no action needed
    // Note: camera BSP collision (setBspPlayerRegion) is updated inline in applySimulationOutput
    // on every region change, not gated by the neighborhood.
}

void IrrlichtRenderer::queueEntityPrepRequests() {
    if (!entityPrepWorker_ || !entityRenderer_ || !entityPrepReady_) return;

    std::vector<uint16_t> unbuilt;
    entityRenderer_->getUnbuiltEntities(unbuilt);
    if (unbuilt.empty()) return;

    const bool pvsValid = (currentPvsRegion_ != SIZE_MAX);

    const auto& entities = entityRenderer_->getEntities();
    for (auto spawnId : unbuilt) {
        if (entityRenderer_->isEntityMeshBuilt(spawnId)) continue;
        auto it = entities.find(spawnId);
        if (it == entities.end()) continue;
        const auto& vis = it->second;
        // Don't prep invisible entities — matches build step visibility filter
        if (!vis.inSceneGraph) continue;
        // Don't re-queue entities whose background prep already completed
        if (vis.entityPrepComplete) continue;

        // When player is outside all BSP regions (SIZE_MAX), only build the player entity
        if (!pvsValid && spawnId != playerSpawnId_) continue;

        // PVS depth gate: queue entities within configured portal hop distance
        uint8_t depth = 255;
        if (spawnId == playerSpawnId_) {
            depth = 0;
        } else {
            depth = getRegionPvsDepth(vis.cachedBspRegion);
        }
        if (depth > static_cast<uint8_t>(config_.constrainedConfig.entityPrepMaxPvsDepth)) continue;

        // Per-entity dedup: each entity gets its own prep job (equipment/variant work differs)
        if (!entityPrepWorker_->isPendingForEntity(spawnId)) {
            EntityPrepWorker::PrepRequest req;
            req.spawnId = spawnId;
            req.raceId = vis.raceId;
            req.gender = vis.gender;
            req.appearance = vis.appearance;
            req.pvsDepth = depth;
            req.bspRegion = vis.cachedBspRegion;
            entityPrepWorker_->requestPrep(req);
        }
    }
}

void IrrlichtRenderer::logAssetBuildTime(const char* type, size_t id,
    std::chrono::steady_clock::time_point start) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() / 1000.0f;
    if (elapsed > 10.0f) {
        LOG_WARN(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms (budget warning)", type, id, elapsed);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "Progressive: {} #{} took {:.1f}ms", type, id, elapsed);
    }
}

void IrrlichtRenderer::sendLoadProgress(const std::string& msg) {
    // WindowManager removed — load progress is logged only
    LOG_DEBUG(MOD_GRAPHICS, "LoadProgress: {}", msg);
}

void IrrlichtRenderer::checkProgressiveLoadingComplete() {
    if (!progressiveLoadingActive_) return;

    // Phase pipeline must reach Complete before we can declare progressive loading done.
    // Otherwise zero counts just mean "nothing queued yet" (pre-zone-load).
    if (!sequentialLoadComplete_) return;

    // Count unbuilt assets
    size_t unbuiltEntities = 0;
    size_t unbuiltDoors = 0;
    size_t unbuiltObjects = 0;

    if (entityRenderer_) {
        for (const auto& [id, vis] : entityRenderer_->getEntities()) {
            if (!vis.meshBuilt && vis.inSceneGraph) {
                // Only count entities in the player's PVS region as blocking.
                // Out-of-region entities will be built lazily when the player moves.
                // When player is outside all BSP regions (SIZE_MAX), only the player
                // entity counts as blocking — other entities can't be built anyway.
                if (currentPvsRegion_ == SIZE_MAX && id != playerSpawnId_)
                    continue;
                if (currentPvsRegion_ != SIZE_MAX &&
                    id != playerSpawnId_ && vis.cachedBspRegion != currentPvsRegion_)
                    continue;
                unbuiltEntities++;
            }
        }
    }

    if (doorManager_) {
        std::vector<uint8_t> pvsDoors;
        doorManager_->getDoorsInRegions(protectedRegions_, pvsDoors);
        // PVS depth gate: only count doors within configured depth
        pvsDoors.erase(std::remove_if(pvsDoors.begin(), pvsDoors.end(), [&](uint8_t doorId) {
            const auto* dv = doorManager_->getDoor(doorId);
            return dv && getRegionPvsDepth(dv->bspRegion) > static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth);
        }), pvsDoors.end());
        unbuiltDoors = pvsDoors.size();
    }

    for (const auto& obj : deferredObjects_) {
        if (!obj.meshBuilt && obj.bspRegion < protectedRegions_.size() && protectedRegions_[obj.bspRegion] &&
            getRegionPvsDepth(obj.bspRegion) <= static_cast<uint8_t>(config_.constrainedConfig.objectPrepMaxPvsDepth))
            unbuiltObjects++;
    }

    // Periodic status log for progressive loading (every 5 seconds)
    static int progCheckLog = 0;
    if (++progCheckLog % 150 == 1) {
        size_t unbuiltRegions = 0;
        if (constrainedMeshCache_) {
            for (const auto& entry : meshLoadQueue_) {
                if (!constrainedMeshCache_->isLoaded(entry.regionIdx) &&
                    getRegionPvsDepth(entry.regionIdx) <= static_cast<uint8_t>(config_.constrainedConfig.terrainPrepMaxPvsDepth))
                    unbuiltRegions++;
            }
        }
        bool pendingTextures = constrainedTextureCache_ && constrainedTextureCache_->hasPendingWork();
        size_t pendingVBOs = 0;
#ifdef EQT_HAS_GLES2
        pendingVBOs = pendingVBOUploads_.size();
#endif
        LOG_INFO(MOD_GRAPHICS, "Progressive status: entities={} doors={} objects={} "
                 "queuedRegions={} pendingVBOs={} pendingTextures={} texRebuild={} objRebuild={} "
                 "| governor={} | progressive={}",
                 unbuiltEntities, unbuiltDoors, unbuiltObjects, unbuiltRegions,
                 pendingVBOs, pendingTextures,
                 textureRebuildQueue_.size(), objectTextureRebuildQueue_.size(),
                 governor_ ? governor_->getStateName() : "N/A",
                 progressiveLoadingActive_);
    }

    if (unbuiltEntities == 0 && unbuiltDoors == 0 && unbuiltObjects == 0) {
        // Also wait for async GPU work (texture decodes/uploads, VBOs, rebuild queues)
        // before declaring progressive loading complete. Without this, the loading
        // screen hides while textures are still in flight → garbled first frame.
        bool gpuWorkPending = false;
#ifdef EQT_HAS_GLES2
        if (!pendingVBOUploads_.empty())
            gpuWorkPending = true;
#endif
        if (constrainedTextureCache_ && constrainedTextureCache_->hasPendingWork())
            gpuWorkPending = true;
        if (!objectTextureRebuildQueue_.empty() || !textureRebuildQueue_.empty())
            gpuWorkPending = true;
        if (gpuWorkPending) return;

        progressiveLoadingActive_ = false;

        // EntityPrepWorker stays alive for the entire session — entities move
        // in and out of the scene graph during gameplay and need lazy prep.

        // Release object texture pixel data — all deferred objects and doors are built.
        // Safe even with constrainedMeshCache_ active (only zone textures needed for region rebuilds).
        if (currentZone_ && !currentZone_->objectTextures.empty()) {
            size_t freed = 0;
            for (auto& [name, texInfo] : currentZone_->objectTextures) {
                if (!texInfo) continue;
                freed += texInfo->data.capacity();
                texInfo->data.clear();
                texInfo->data.shrink_to_fit();
                for (auto& frame : texInfo->frames) {
                    freed += frame.data.capacity();
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
            if (freed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f}KB object texture pixel data (progressive complete)",
                         freed / 1024.0f);
            }
        }

        // Remove zone placeholder mesh — real geometry is fully loaded
        destroyZonePlaceholder();

        // Build portal system now that all geometry is loaded. Portal occlusion
        // is an optimization (~472ms build cost) — deferred from loadZone to avoid
        // poisoning the governor during progressive loading.
        if (portalBuildPending_ && zoneBspTree_ && !regionBoundingBoxes_.empty()) {
            // Compute zone bounds from region bounding boxes (EQ coords)
            float zbMinX = std::numeric_limits<float>::max(), zbMinY = zbMinX, zbMinZ = zbMinX;
            float zbMaxX = std::numeric_limits<float>::lowest(), zbMaxY = zbMaxX, zbMaxZ = zbMaxX;
            for (const auto& [idx, bbox] : regionBoundingBoxes_) {
                zbMinX = std::min(zbMinX, bbox.MinEdge.X); zbMinY = std::min(zbMinY, bbox.MinEdge.Y); zbMinZ = std::min(zbMinZ, bbox.MinEdge.Z);
                zbMaxX = std::max(zbMaxX, bbox.MaxEdge.X); zbMaxY = std::max(zbMaxY, bbox.MaxEdge.Y); zbMaxZ = std::max(zbMaxZ, bbox.MaxEdge.Z);
            }
            portalSystem_ = std::make_unique<PortalSystem>();
            portalSystem_->buildFromBsp(*zoneBspTree_, zbMinX, zbMinY, zbMinZ, zbMaxX, zbMaxY, zbMaxZ);
            portalOcclusionEligible_ = portalSystem_->hasPortals() &&
                                       (portalSystem_->getData().portals.size() > 10);
            if (portalOcclusionEligible_) {
                LOG_INFO(MOD_GRAPHICS, "Portal occlusion eligible: {} portals (post-load build)",
                         portalSystem_->getData().portals.size());
                // Auto-enable portal occlusion (post-load path)
                if (config_.constrainedConfig.portalOcclusion &&
                    config_.constrainedConfig.enableStencilBuffer) {
                    if (!indoorRegions_.empty()) {
                        LOG_INFO(MOD_GRAPHICS, "Portal occlusion: per-region mode (post-load, {} indoor regions)",
                                 indoorRegions_.size());
                    } else {
                        size_t portalCount = portalSystem_->getData().portals.size();
                        size_t regionCount = regionBoundingBoxes_.size();
                        float portalRatio = regionCount > 0 ? static_cast<float>(portalCount) / regionCount : 999.0f;
                        if (portalRatio <= 3.0f) {
                            portalOcclusionEnabled_ = true;
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion auto-enabled (post-load): {:.1f} ratio (indoor)", portalRatio);
                        } else {
                            LOG_INFO(MOD_GRAPHICS, "Portal occlusion NOT auto-enabled (post-load): {:.1f} ratio (outdoor)", portalRatio);
                        }
                    }
                }
            }
            portalBuildPending_ = false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - progressiveLoadStartTime_).count();
        LOG_INFO(MOD_GRAPHICS, "Progressive loading complete: {}ms total streaming time", elapsed);
        sendLoadProgress(fmt::format("[Load] Complete ({:.1f}s)", elapsed / 1000.0f));

        // Regenerate detail chunks now that all terrain is in the selector
        // (chunks generated during progressive loading had incomplete terrain raycasts)
        if (detailManager_ && detailManager_->isEnabled()) {
            detailManager_->setEnabled(false);
            detailManager_->setEnabled(true);
            LOG_INFO(MOD_GRAPHICS, "Regenerating detail chunks with complete terrain");
        }

        // Release raw texture data now that all models are built
        if (config_.constrainedConfig.releaseTextureDataAfterUpload && entityRenderer_) {
            size_t totalFreed = 0;
            if (auto* eml = entityRenderer_->getEquipmentModelLoader()) {
                totalFreed += eml->releaseRawTextureData();
            }
            if (auto* rml = entityRenderer_->getRaceModelLoader()) {
                totalFreed += rml->releaseRawTextureData();
            }
            if (totalFreed > 0) {
                LOG_INFO(MOD_GRAPHICS, "Released {:.1f} KB of raw texture data after progressive load",
                    totalFreed / 1024.0f);
            }
        }
    }
}

} // namespace Graphics
} // namespace EQT
