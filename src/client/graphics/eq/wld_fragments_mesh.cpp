#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>
#include <vector>
#include <iostream>
#include <iomanip>
#include "client/graphics/eq/wld_internal.h"

namespace EQT {
namespace Graphics {

void WldLoader::parseFragment36(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    WldFragment36Header header = read_val<WldFragment36Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment36Header);

    auto geom = std::make_shared<ZoneGeometry>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0 ) {
            geom->name = std::string(&hash[nameOffset]);
        }
    }

    // Debug: Print mesh info for character meshes
    LOG_TRACE(MOD_GRAPHICS, "WLD Frag36 [{}]: fragLen={}", geom->name, fragLength);
    LOG_TRACE(MOD_GRAPHICS, "  Header: flags=0x{:x} frag1={} center=({},{},{})",
        header.flags, header.frag1, header.centerX, header.centerY, header.centerZ);
    LOG_TRACE(MOD_GRAPHICS, "  verts={} polys={} texcoords={} normals={} colors={}",
        header.vertexCount, header.polygonCount, header.texCoordCount, header.normalCount, header.colorCount);
    LOG_TRACE(MOD_GRAPHICS, "  size6={} polyTexCnt={} vertTexCnt={} size9={} scale={} oldFmt={}",
        header.size6, header.polygonTexCount, header.vertexTexCount, header.size9, header.scale, oldFormat);

    geom->minX = header.minX;
    geom->minY = header.minY;
    geom->minZ = header.minZ;
    geom->maxX = header.maxX;
    geom->maxY = header.maxY;
    geom->maxZ = header.maxZ;

    // Store mesh center for skinning reference
    geom->centerX = header.centerX;
    geom->centerY = header.centerY;
    geom->centerZ = header.centerZ;

    float scale = 1.0f / static_cast<float>(1 << header.scale);
    float recip256 = 1.0f / 256.0f;
    float recip128 = 1.0f / 128.0f;  // For signed int8 normals: [-128,127] -> [-1,1]

    // Read vertices
    // For character meshes, vertices are stored relative to center
    // We keep them that way for proper bone transform application
    geom->vertices.resize(header.vertexCount);
    const char* vertexStart = ptr;
    for (uint16_t i = 0; i < header.vertexCount; ++i) {
        WldVertex v = read_val<WldVertex>(ptr);
        ptr += sizeof(WldVertex);

        // Store vertices relative to center (don't add center yet)
        // Bone transforms will position them correctly
        geom->vertices[i].x = v.x * scale;
        geom->vertices[i].y = v.y * scale;
        geom->vertices[i].z = v.z * scale;

        // Debug: Print vertices for character meshes - first few
        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Vert[{}]: raw({},{},{}) -> scaled({},{},{})",
                i, v.x, v.y, v.z, geom->vertices[i].x, geom->vertices[i].y, geom->vertices[i].z);
        }
    }
    LOG_TRACE(MOD_GRAPHICS, "  Total vertex bytes read: {} (expected {})", (ptr - vertexStart), header.vertexCount * 6);
    LOG_TRACE(MOD_GRAPHICS, "  Mesh center: ({},{},{})", geom->centerX, geom->centerY, geom->centerZ);

    // Read texture coordinates
    if (oldFormat) {
        for (uint16_t i = 0; i < header.texCoordCount && i < header.vertexCount; ++i) {
            WldTexCoordOld tc = read_val<WldTexCoordOld>(ptr);
            ptr += sizeof(WldTexCoordOld);

            geom->vertices[i].u = tc.u * recip256;
            geom->vertices[i].v = tc.v * recip256;
        }
        ptr += sizeof(WldTexCoordOld) * (header.texCoordCount > header.vertexCount ?
                                         header.texCoordCount - header.vertexCount : 0);
    } else {
        for (uint16_t i = 0; i < header.texCoordCount && i < header.vertexCount; ++i) {
            WldTexCoordNew tc = read_val<WldTexCoordNew>(ptr);
            ptr += sizeof(WldTexCoordNew);

            geom->vertices[i].u = tc.u;
            geom->vertices[i].v = tc.v;
        }
        ptr += sizeof(WldTexCoordNew) * (header.texCoordCount > header.vertexCount ?
                                         header.texCoordCount - header.vertexCount : 0);
    }

    // Read normals - int8 values divided by 128 give range [-1, 1], then normalize
    for (uint16_t i = 0; i < header.normalCount && i < header.vertexCount; ++i) {
        WldNormal n = read_val<WldNormal>(ptr);
        ptr += sizeof(WldNormal);

        float nx = static_cast<float>(n.x) * recip128;
        float ny = static_cast<float>(n.y) * recip128;
        float nz = static_cast<float>(n.z) * recip128;

        // Normalize the vector (eqsage does this)
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) {
            geom->vertices[i].nx = nx / len;
            geom->vertices[i].ny = ny / len;
            geom->vertices[i].nz = nz / len;
        } else {
            // Default to up vector if normal is degenerate
            geom->vertices[i].nx = 0.0f;
            geom->vertices[i].ny = 0.0f;
            geom->vertices[i].nz = 1.0f;
        }
    }
    ptr += sizeof(WldNormal) * (header.normalCount > header.vertexCount ?
                                header.normalCount - header.vertexCount : 0);

    // Baked per-vertex lighting (T408, B110). Until T408 this array was
    // `ptr += sizeof(uint32_t) * header.colorCount;` under a `// Skip colors`
    // comment, so every zone vertex reached the GPU as opaque white and the
    // official client's static lamp pools did not exist in fable at all.
    // The array is one packed 32-bit colour per vertex; it is stored raw here
    // and unpacked by the mesh builders, which alone know Irrlicht's channel
    // order.
    if (header.colorCount > 0) {
        geom->vertexColors.resize(header.colorCount);
        for (uint32_t i = 0; i < header.colorCount; ++i) {
            geom->vertexColors[i] = read_val<uint32_t>(ptr);
            ptr += sizeof(uint32_t);
        }
    }

    // Read polygons
    geom->triangles.resize(header.polygonCount);
    for (uint16_t i = 0; i < header.polygonCount; ++i) {
        WldPolygon p = read_val<WldPolygon>(ptr);
        ptr += sizeof(WldPolygon);

        // Reference implementation reverses winding order: index[2], index[1], index[0]
        geom->triangles[i].v1 = p.index[2];
        geom->triangles[i].v2 = p.index[1];
        geom->triangles[i].v3 = p.index[0];
        geom->triangles[i].flags = p.flags;
        geom->triangles[i].textureIndex = 0;

        // Debug: Print first few polygon indices for character meshes
        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Poly[{}]: flags={} v1={} v2={} v3={} (max_v={})",
                i, p.flags, p.index[0], p.index[1], p.index[2], header.vertexCount);
        }
    }

    // Validate polygon indices
    bool hasInvalidIdx = false;
    for (const auto& tri : geom->triangles) {
        if (tri.v1 >= header.vertexCount || tri.v2 >= header.vertexCount || tri.v3 >= header.vertexCount) {
            hasInvalidIdx = true;
            break;
        }
    }
    if (hasInvalidIdx) {
        LOG_WARN(MOD_GRAPHICS, "  WARNING: Invalid polygon indices found in {}", geom->name);
    }

    // Parse vertex pieces (bone assignments) - size6 entries of 4 bytes each
    // Each entry is: int16_t count, int16_t boneIndex
    geom->vertexPieces.resize(header.size6);
    for (uint16_t i = 0; i < header.size6; ++i) {
        int16_t pieceCount = read_val<int16_t>(ptr);
        int16_t pieceBone = read_val<int16_t>(ptr + 2);
        geom->vertexPieces[i].count = pieceCount;
        geom->vertexPieces[i].boneIndex = pieceBone;
        ptr += 4;

        if (i < 10) {
            LOG_TRACE(MOD_GRAPHICS, "  VertexPiece[{}]: count={} bone={}",
                i, geom->vertexPieces[i].count, geom->vertexPieces[i].boneIndex);
        }
    }
    if (header.size6 > 0) {
        LOG_TRACE(MOD_GRAPHICS, "  Total vertex pieces: {}", header.size6);
    }

    // Read texture mappings (polygonsByTex)
    uint32_t polyIdx = 0;
    for (uint16_t i = 0; i < header.polygonTexCount; ++i) {
        WldTexMap tm = read_val<WldTexMap>(ptr);
        ptr += sizeof(WldTexMap);

        for (uint16_t j = 0; j < tm.polyCount && polyIdx < geom->triangles.size(); ++j) {
            geom->triangles[polyIdx++].textureIndex = tm.tex;
        }
    }

    // Skip verticesByTex entries (vertexTexCount * 4 bytes)
    // This data maps vertices to textures but we don't need it for rendering
    ptr += 4 * header.vertexTexCount;

    // Resolve texture names — use cached material list when multiple meshes
    // reference the same 0x31 fragment (e.g. all 1915 meshes in qeynos2)
    if (header.frag1 > 0) {
        LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': resolving materialList frag1={}", fragIndex, geom->name, header.frag1);
        auto cacheIt = resolvedMaterialListCache_.find(header.frag1);
        if (cacheIt != resolvedMaterialListCache_.end()) {
            // Cache hit — share the resolved material list
            geom->materialData = cacheIt->second;
            LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': materialList cache HIT for frag1={}, textureNames={}",
                      fragIndex, geom->name, header.frag1, geom->materialData->textureNames.size());
        } else {
            auto it31 = brushSets_.find(header.frag1);
            if (it31 != brushSets_.end()) {
                LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': found brushSet for frag1={} with {} brushRefs",
                          fragIndex, geom->name, header.frag1, it31->second.brushRefs.size());
                auto matList = std::make_shared<ResolvedMaterialList>();
                const WldTextureBrushSet& brushSet = it31->second;

                for (size_t bri = 0; bri < brushSet.brushRefs.size(); ++bri) {
                    uint32_t brushRef = brushSet.brushRefs[bri];
                    std::string texName;
                    bool isInvisible = false;
                    bool isMasked = false;
                    TextureAnimationInfo animInfo;

                    auto it30 = materials_.find(brushRef);
                    if (it30 != materials_.end()) {
                        const WldTextureBrush& material = it30->second;

                        if (material.flags & 1) {
                            isInvisible = true;
                            LOG_TRACE(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: invisible (flags & 1)", fragIndex, bri, brushRef);
                        }
                        if (material.materialType == WLD_MATERIAL_TYPE_TRANSPARENT_MASKED) {
                            isMasked = true;
                        }

                        if (!material.textureRefs.empty()) {
                            uint32_t brushIdx = material.textureRefs[0];
                            auto it04 = brushes_.find(brushIdx);
                            if (it04 != brushes_.end()) {
                                const WldTextureBrush& brush = it04->second;

                                animInfo.isAnimated = brush.isAnimated;
                                animInfo.animationDelayMs = brush.animationDelayMs;

                                for (uint32_t texIdx : brush.textureRefs) {
                                    auto it03 = textures_.find(texIdx);
                                    if (it03 != textures_.end()) {
                                        const WldTexture& tex = it03->second;
                                        for (const auto& frame : tex.frames) {
                                            animInfo.frames.push_back(frame);
                                        }
                                    } else {
                                        LOG_WARN(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: texIdx={} NOT FOUND in textures_ (size={})",
                                                 fragIndex, bri, brushRef, texIdx, textures_.size());
                                    }
                                }

                                if (!animInfo.frames.empty()) {
                                    texName = animInfo.frames[0];
                                }
                                LOG_TRACE(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: resolved texName='{}' via brushIdx={} ({} frames)",
                                          fragIndex, bri, brushRef, texName, brushIdx, animInfo.frames.size());
                            } else {
                                LOG_WARN(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: brushIdx={} NOT FOUND in brushes_ (size={})",
                                         fragIndex, bri, brushRef, brushIdx, brushes_.size());
                            }
                        } else {
                            isInvisible = true;
                            LOG_TRACE(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: no textureRefs, marking invisible", fragIndex, bri, brushRef);
                        }
                    } else {
                        isInvisible = true;
                        LOG_WARN(MOD_GRAPHICS, "  Frag36[{}] brushRef[{}]={}: NOT FOUND in materials_ (size={}), marking invisible",
                                 fragIndex, bri, brushRef, materials_.size());
                    }

                    matList->textureNames.push_back(texName);
                    matList->textureInvisible.push_back(isInvisible);
                    matList->textureMasked.push_back(isMasked);
                    matList->textureAnimations.push_back(animInfo);
                }

                resolvedMaterialListCache_[header.frag1] = matList;
                geom->materialData = matList;
                LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': resolved materialList with {} textureNames",
                          fragIndex, geom->name, matList->textureNames.size());
                // Log first few resolved names
                for (size_t ti = 0; ti < matList->textureNames.size() && ti < 5; ++ti) {
                    LOG_TRACE(MOD_GRAPHICS, "  Frag36[{}] textureName[{}]='{}' invisible={}",
                              fragIndex, ti, matList->textureNames[ti], matList->textureInvisible[ti]);
                }
                if (matList->textureNames.size() > 5) {
                    LOG_TRACE(MOD_GRAPHICS, "  Frag36[{}] ... and {} more textureNames", fragIndex, matList->textureNames.size() - 5);
                }
            } else {
                LOG_WARN(MOD_GRAPHICS, "Frag36[{}] '{}': frag1={} NOT FOUND in brushSets_ (size={})",
                         fragIndex, geom->name, header.frag1, brushSets_.size());
            }
        }
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': frag1={} (<=0), no materialList to resolve", fragIndex, geom->name, header.frag1);
    }

    if (geom->textureNames().empty()) {
        std::set<uint32_t> uniqueTexIndices;
        for (const auto& tri : geom->triangles) {
            uniqueTexIndices.insert(tri.textureIndex);
        }
        uint32_t maxIdx = uniqueTexIndices.empty() ? 0 : *uniqueTexIndices.rbegin();
        auto& mat = geom->mutableMaterialData();
        mat.textureNames.resize(maxIdx + 1);
        mat.textureInvisible.resize(maxIdx + 1, false);
        mat.textureMasked.resize(maxIdx + 1, false);
        mat.textureAnimations.resize(maxIdx + 1);
        LOG_DEBUG(MOD_GRAPHICS, "Frag36[{}] '{}': textureNames was empty after resolution, created {} placeholder entries (maxTexIdx={})",
                  fragIndex, geom->name, maxIdx + 1, maxIdx);
    }

    // Link vertex animation data if this mesh has animated vertices
    // frag2 contains the reference to the 0x2F fragment (mesh animated vertices reference)
    if (header.frag2 > 0) {
        uint32_t animVertRefFragIdx = header.frag2;
        LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': looking up vertex animation frag2={}", fragIndex, geom->name, header.frag2);
        // Look up the 0x2F -> 0x37 mapping
        auto refIt = meshAnimatedVerticesRefs_.find(animVertRefFragIdx);
        if (refIt != meshAnimatedVerticesRefs_.end()) {
            uint32_t animVertFragIdx = refIt->second;
            // Look up the actual animated vertices data
            auto animIt = meshAnimatedVertices_.find(animVertFragIdx);
            if (animIt != meshAnimatedVertices_.end()) {
                geom->animatedVertices = animIt->second;
                LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': linked to vertex animation '{}' ({} frames)",
                    fragIndex, geom->name, animIt->second->name, animIt->second->frames.size());
            } else {
                LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': animVertFragIdx={} NOT FOUND in meshAnimatedVertices_",
                          fragIndex, geom->name, animVertFragIdx);
            }
        } else {
            LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': animVertRefFragIdx={} NOT FOUND in meshAnimatedVerticesRefs_",
                      fragIndex, geom->name, animVertRefFragIdx);
        }
    }

    if (!geom->vertices.empty() && !geom->triangles.empty()) {
        geometries_.push_back(geom);
        // Also store by fragment index for precise bone model lookups
        geometryByFragIndex_[fragIndex] = geom;
        LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': stored geometry (verts={} tris={} textureNames={} materialData={})",
                  fragIndex, geom->name, geom->vertices.size(), geom->triangles.size(),
                  geom->textureNames().size(), (bool)geom->materialData);
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag36[{}] '{}': NOT stored (verts={} tris={})", fragIndex, geom->name, geom->vertices.size(), geom->triangles.size());
    }
}

std::shared_ptr<ZoneGeometry> WldLoader::getCombinedGeometry() const {
    if (geometries_.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "WldLoader::getCombinedGeometry: no geometries, returning nullptr");
        return nullptr;
    }

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::getCombinedGeometry: combining {} geometries", geometries_.size());
    // Log materialData status of input geometries
    size_t withMaterial = 0, withoutMaterial = 0, withEmptyNames = 0;
    for (const auto& geom : geometries_) {
        if (geom->materialData) {
            withMaterial++;
            if (geom->materialData->textureNames.empty()) withEmptyNames++;
        } else {
            withoutMaterial++;
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::getCombinedGeometry: input geometries — withMaterialData={} withoutMaterialData={} withEmptyTextureNames={}",
              withMaterial, withoutMaterial, withEmptyNames);

    auto combined = std::make_shared<ZoneGeometry>();
    combined->name = "combined";
    auto& combinedMat = combined->mutableMaterialData();

    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    std::map<std::string, uint32_t> globalTexMap;
    uint32_t vertexOffset = 0;

    for (const auto& geom : geometries_) {
        std::vector<uint32_t> texRemap;
        for (size_t i = 0; i < geom->textureNames().size(); ++i) {
            const auto& texName = geom->textureNames()[i];
            bool isInvisible = (i < geom->textureInvisible().size()) ? geom->textureInvisible()[i] : false;
            TextureAnimationInfo animInfo;
            if (i < geom->textureAnimations().size()) {
                animInfo = geom->textureAnimations()[i];
            }

            auto it = globalTexMap.find(texName);
            if (it != globalTexMap.end()) {
                texRemap.push_back(it->second);
                if (isInvisible && it->second < combinedMat.textureInvisible.size()) {
                    combinedMat.textureInvisible[it->second] = true;
                }
                // Update animation info if this one has animation and existing doesn't
                if (animInfo.isAnimated && it->second < combinedMat.textureAnimations.size() &&
                    !combinedMat.textureAnimations[it->second].isAnimated) {
                    combinedMat.textureAnimations[it->second] = animInfo;
                }
            } else {
                uint32_t newIdx = static_cast<uint32_t>(combinedMat.textureNames.size());
                globalTexMap[texName] = newIdx;
                combinedMat.textureNames.push_back(texName);
                combinedMat.textureInvisible.push_back(isInvisible);
                combinedMat.textureAnimations.push_back(animInfo);
                texRemap.push_back(newIdx);
            }
        }

        for (const auto& v : geom->vertices) {
            // Add mesh center to get world-space positions
            // (vertices are stored relative to center)
            Vertex3D worldV = v;
            worldV.x += geom->centerX;
            worldV.y += geom->centerY;
            worldV.z += geom->centerZ;
            combined->vertices.push_back(worldV);
            combined->minX = std::min(combined->minX, worldV.x);
            combined->minY = std::min(combined->minY, worldV.y);
            combined->minZ = std::min(combined->minZ, worldV.z);
            combined->maxX = std::max(combined->maxX, worldV.x);
            combined->maxY = std::max(combined->maxY, worldV.y);
            combined->maxZ = std::max(combined->maxZ, worldV.z);
        }

        for (const auto& tri : geom->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            if (tri.textureIndex < texRemap.size()) {
                t.textureIndex = texRemap[tri.textureIndex];
            } else {
                t.textureIndex = tri.textureIndex;
            }
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        vertexOffset += static_cast<uint32_t>(geom->vertices.size());
    }

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::getCombinedGeometry: result verts={} tris={} textureNames={}",
              combined->vertices.size(), combined->triangles.size(), combinedMat.textureNames.size());
    return combined;
}

std::shared_ptr<ZoneGeometry> WldLoader::getGeometryForRegion(size_t regionIndex) const {
    if (!bspTree_ || regionIndex >= bspTree_->regions.size()) {
        return nullptr;
    }

    const auto& region = bspTree_->regions[regionIndex];
    if (!region->containsPolygons || region->meshReference < 0) {
        return nullptr;
    }

    // meshReference is a 1-indexed fragment reference
    // Convert to 0-indexed and look up in geometryByFragIndex_
    auto it = geometryByFragIndex_.find(static_cast<uint32_t>(region->meshReference));
    return (it != geometryByFragIndex_.end()) ? it->second : nullptr;
}

bool WldLoader::hasPvsData() const {
    if (!bspTree_ || bspTree_->regions.empty()) {
        return false;
    }

    // Check if at least one region has PVS visibility data
    for (const auto& region : bspTree_->regions) {
        if (!region->visibleRegions.empty()) {
            return true;
        }
    }

    return false;
}

} // namespace Graphics
} // namespace EQT
