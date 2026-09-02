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

void WldLoader::parseFragment14(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    WldFragment14Header header = read_val<WldFragment14Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment14Header);

    WldObjectDef objDef;

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0 ) {
            objDef.name = std::string(&hash[nameOffset]);
            std::transform(objDef.name.begin(), objDef.name.end(), objDef.name.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            // Remove _ACTORDEF suffix to match placeable naming convention
            size_t actorDefPos = objDef.name.find("_ACTORDEF");
            if (actorDefPos != std::string::npos) {
                objDef.name = objDef.name.substr(0, actorDefPos);
            }
        }
    }

    if (header.flags & 1) ptr += sizeof(int32_t);
    if (header.flags & 2) ptr += sizeof(int32_t);

    for (uint32_t i = 0; i < header.entries; ++i) {
        if (ptr + sizeof(uint32_t) > fragBuffer + fragLength) break;
        uint32_t sz = read_val<uint32_t>(ptr);
        ptr += sizeof(uint32_t);
        ptr += sz * (sizeof(int32_t) + sizeof(float));
    }

    for (uint32_t i = 0; i < header.entries2; ++i) {
        if (ptr + sizeof(uint32_t) > fragBuffer + fragLength) break;
        uint32_t ref = read_val<uint32_t>(ptr);
        ptr += sizeof(uint32_t);

        if (ref > 0) {
            objDef.meshRefs.push_back(ref);
        }
    }

    if (!objDef.name.empty()) {
        objectDefs_[objDef.name] = objDef;
        LOG_TRACE(MOD_GRAPHICS, "Frag14[{}]: Actor '{}' flags=0x{:x} entries={} entries2={} meshRefs={}",
                  fragIndex, objDef.name, header.flags, header.entries, header.entries2, objDef.meshRefs.size());
        for (size_t mi = 0; mi < objDef.meshRefs.size(); ++mi) {
            LOG_TRACE(MOD_GRAPHICS, "  Frag14[{}] '{}': meshRef[{}]={}", fragIndex, objDef.name, mi, objDef.meshRefs[mi]);
        }
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag14[{}]: Actor with empty name, not stored", fragIndex);
    }
}

void WldLoader::parseFragment15(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    // Fragment 0x15 (ActorInstance) uses flag-based variable-length parsing
    // Do NOT cast to a fixed struct - parse field by field based on flags
    // Note: nameRef is already extracted by the caller (like other fragment parsers)
    // Note: Fragment 0x15 typically has nameRef == 0; the name comes from objectRef

    const char* ptr = fragBuffer;
    const char* endPtr = fragBuffer + fragLength;

    // Read object definition reference (int32) - this is a STRING reference (negative = string hash offset)
    // per eqsage: objectName = wld.getString(objectRef).replace('_ACTORDEF', '').toLowerCase()
    if (ptr + sizeof(int32_t) > endPtr) return;
    int32_t objectRef = read_val<int32_t>(ptr);
    ptr += sizeof(int32_t);

    // objectRef < 0 means it's a string hash reference (like _ACTORDEF name)
    if (objectRef >= 0) {
        // No valid string reference for object name - skip this placeable
        return;
    }

    auto placeable = std::make_shared<Placeable>();

    // Set the object name from the string hash using objectRef (not nameRef)
    int32_t nameOffset = -objectRef;
    if (nameOffset > 0) {
        std::string name(&hash[nameOffset]);
        std::transform(name.begin(), name.end(), name.begin(),
                      [](unsigned char c) { return std::toupper(c); });
        // Remove _ACTORDEF suffix if present (like eqsage does)
        size_t actorDefPos = name.find("_ACTORDEF");
        if (actorDefPos != std::string::npos) {
            name = name.substr(0, actorDefPos);
        }
        placeable->setName(name);
    }

    // Read flags (uint32)
    if (ptr + sizeof(uint32_t) > endPtr) return;
    uint32_t flags = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);

    // Read sphere reference (uint32) - always present
    if (ptr + sizeof(uint32_t) > endPtr) return;
    uint32_t sphereRef = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);
    (void)sphereRef;  // Collision sphere reference, not currently used

    // If HasCurrentAction flag set, read action (uint32)
    if (flags & Fragment15Flags::HasCurrentAction) {
        if (ptr + sizeof(uint32_t) > endPtr) return;
        ptr += sizeof(uint32_t);  // Skip action ID
    }

    // If HasLocation flag set, read position and rotation
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float rawRotX = 0.0f, rawRotY = 0.0f, rawRotZ = 0.0f;

    if (flags & Fragment15Flags::HasLocation) {
        if (ptr + 6 * sizeof(float) + sizeof(uint32_t) > endPtr) return;

        x = read_val<float>(ptr); ptr += sizeof(float);
        y = read_val<float>(ptr); ptr += sizeof(float);
        z = read_val<float>(ptr); ptr += sizeof(float);
        rawRotX = read_val<float>(ptr); ptr += sizeof(float);
        rawRotY = read_val<float>(ptr); ptr += sizeof(float);
        rawRotZ = read_val<float>(ptr); ptr += sizeof(float);
        ptr += sizeof(uint32_t);  // Unknown extra field
    }

    placeable->setLocation(x, y, z);

    // Convert rotation from EQ format to internal representation
    // This EXACTLY matches eqsage's Location class (sage/lib/s3d/common/location.js):
    //   this.rotateX = 0;
    //   this.rotateZ = rotateY * modifier;
    //   this.rotateY = rotateX * modifier * -1;
    //
    // EQ rotation values are in 512ths of a circle (0-511 = 0-360 degrees)
    const float rotModifier = 360.0f / 512.0f;
    float finalRotX = 0.0f;                        // Always 0 (matches eqsage)
    float finalRotY = rawRotX * rotModifier * -1.0f;  // Primary yaw, negated (matches eqsage)
    float finalRotZ = rawRotY * rotModifier;       // Secondary rotation (matches eqsage)
    placeable->setRotation(finalRotX, finalRotY, finalRotZ);

    // If HasBoundingRadius flag set, read bounding radius (float)
    if (flags & Fragment15Flags::HasBoundingRadius) {
        if (ptr + sizeof(float) > endPtr) return;
        ptr += sizeof(float);  // Skip bounding radius
    }

    // If HasScaleFactor flag set, read uniform scale (single float!)
    float scaleFactor = 1.0f;  // Default to 1.0 if not present
    if (flags & Fragment15Flags::HasScaleFactor) {
        if (ptr + sizeof(float) > endPtr) return;
        scaleFactor = read_val<float>(ptr);
        ptr += sizeof(float);
    }

    // Set uniform scale for all axes (EQ uses single scale factor)
    placeable->setScale(scaleFactor, scaleFactor, scaleFactor);

    // Debug: log all placeables
    LOG_DEBUG(MOD_GRAPHICS, "[PLACEABLE] {} pos=({:.2f},{:.2f},{:.2f}) rot=({:.2f},{:.2f},{:.2f}) scale={:.4f} flags=0x{:X}",
        placeable->getName(), x, y, z, finalRotX, finalRotY, finalRotZ, scaleFactor, flags);

    // If HasSound flag set, read sound name reference (int32)
    if (flags & Fragment15Flags::HasSound) {
        if (ptr + sizeof(int32_t) > endPtr) return;
        ptr += sizeof(int32_t);  // Skip sound reference
    }

    // If HasVertexColorReference flag set, read vertex color reference (uint32)
    if (flags & Fragment15Flags::HasVertexColorReference) {
        if (ptr + sizeof(uint32_t) > endPtr) return;
        ptr += sizeof(uint32_t);  // Skip vertex color reference
    }

    // Read userData size and skip userData
    if (ptr + sizeof(uint32_t) > endPtr) return;
    uint32_t userDataSize = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);

    if (userDataSize > 0 && ptr + userDataSize <= endPtr) {
        // userData can contain zone line info, etc. - skip for now
        ptr += userDataSize;
    }

    placeables_.push_back(placeable);
}

void WldLoader::parseFragment2C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    // Legacy Mesh format (0x2C) - found in older character models like global_chr.s3d
    // Uses uncompressed 32-bit floats for vertices, normals, and UVs
    WldFragment2CHeader header = read_val<WldFragment2CHeader>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment2CHeader);

    auto geom = std::make_shared<ZoneGeometry>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0 ) {
            geom->name = std::string(&hash[nameOffset]);
        }
    }

    // Debug output for legacy meshes
    LOG_TRACE(MOD_GRAPHICS, "WLD Frag2C (Legacy) [{}]: fragLen={}", geom->name, fragLength);
    LOG_TRACE(MOD_GRAPHICS, "  Header: flags=0x{:x} center=({},{},{}) scale={}",
        header.flags, header.centerX, header.centerY, header.centerZ, header.scale);
    LOG_TRACE(MOD_GRAPHICS, "  verts={} polys={} texcoords={} normals={} colors={}",
        header.vertexCount, header.polygonCount, header.texCoordCount, header.normalCount, header.colorCount);
    LOG_TRACE(MOD_GRAPHICS, "  vertexPieceCount={} polyTexCnt={} vertTexCnt={}",
        header.vertexPieceCount, header.polygonTexCount, header.vertexTexCount);

    geom->minX = header.minX;
    geom->minY = header.minY;
    geom->minZ = header.minZ;
    geom->maxX = header.maxX;
    geom->maxY = header.maxY;
    geom->maxZ = header.maxZ;

    geom->centerX = header.centerX;
    geom->centerY = header.centerY;
    geom->centerZ = header.centerZ;

    // Read vertices - raw 32-bit floats (not compressed)
    geom->vertices.resize(header.vertexCount);
    for (uint32_t i = 0; i < header.vertexCount; ++i) {
        float vx = read_val<float>(ptr);
        float vy = read_val<float>(ptr + 4);
        float vz = read_val<float>(ptr + 8);
        ptr += sizeof(float) * 3;

        geom->vertices[i].x = vx;
        geom->vertices[i].y = vy;
        geom->vertices[i].z = vz;

        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Vert[{}]: ({},{},{})", i, vx, vy, vz);
        }
    }

    // Read texture coordinates - raw 32-bit floats
    for (uint32_t i = 0; i < header.texCoordCount && i < header.vertexCount; ++i) {
        float tu = read_val<float>(ptr);
        float tv = read_val<float>(ptr + 4);
        ptr += sizeof(float) * 2;

        geom->vertices[i].u = tu;
        geom->vertices[i].v = tv;
    }
    // Skip extra tex coords if any
    if (header.texCoordCount > header.vertexCount) {
        ptr += sizeof(float) * 2 * (header.texCoordCount - header.vertexCount);
    }

    // Read normals - raw 32-bit floats, normalize for consistency
    for (uint32_t i = 0; i < header.normalCount && i < header.vertexCount; ++i) {
        float nx = read_val<float>(ptr);
        float ny = read_val<float>(ptr + 4);
        float nz = read_val<float>(ptr + 8);
        ptr += sizeof(float) * 3;

        // Normalize the vector
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
    // Skip extra normals if any
    if (header.normalCount > header.vertexCount) {
        ptr += sizeof(float) * 3 * (header.normalCount - header.vertexCount);
    }

    // Skip colors (RGBA as uint32_t)
    ptr += sizeof(uint32_t) * header.colorCount;

    // Read polygons - same format as modern mesh
    geom->triangles.resize(header.polygonCount);
    for (uint32_t i = 0; i < header.polygonCount; ++i) {
        WldPolygon p = read_val<WldPolygon>(ptr);
        ptr += sizeof(WldPolygon);

        // Reverse winding order like modern mesh
        geom->triangles[i].v1 = p.index[2];
        geom->triangles[i].v2 = p.index[1];
        geom->triangles[i].v3 = p.index[0];
        geom->triangles[i].flags = p.flags;
        geom->triangles[i].textureIndex = 0;

        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Poly[{}]: flags={} v={},{},{}",
                i, p.flags, p.index[0], p.index[1], p.index[2]);
        }
    }

    // Parse vertex pieces (bone assignments) - Legacy format reads Start directly from file
    // Format: int16_t count, int16_t startIndex
    geom->vertexPieces.resize(header.vertexPieceCount);
    for (uint16_t i = 0; i < header.vertexPieceCount; ++i) {
        int16_t pieceCount = read_val<int16_t>(ptr);
        int16_t pieceBone = read_val<int16_t>(ptr + 2);
        geom->vertexPieces[i].count = pieceCount;
        geom->vertexPieces[i].boneIndex = pieceBone;  // In legacy, this is the start index used as key
        ptr += 4;

        if (i < 10) {
            LOG_TRACE(MOD_GRAPHICS, "  VertexPiece[{}]: count={} bone/start={}",
                i, geom->vertexPieces[i].count, geom->vertexPieces[i].boneIndex);
        }
    }

    // Read texture mappings
    uint32_t polyIdx = 0;
    for (uint16_t i = 0; i < header.polygonTexCount; ++i) {
        WldTexMap tm = read_val<WldTexMap>(ptr);
        ptr += sizeof(WldTexMap);

        for (uint16_t j = 0; j < tm.polyCount && polyIdx < geom->triangles.size(); ++j) {
            geom->triangles[polyIdx++].textureIndex = tm.tex;
        }
    }

    // Skip verticesByTex entries
    ptr += 4 * header.vertexTexCount;

    // Resolve texture names — use cached material list (same as parseFragment36)
    uint32_t texBrushSetRef = header.flags & 0xFFFF;
    LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': flags=0x{:x} texBrushSetRef={} (flags & 0xFFFF)",
              fragIndex, geom->name, header.flags, texBrushSetRef);
    if (texBrushSetRef > 0) {
        auto cacheIt = resolvedMaterialListCache_.find(texBrushSetRef);
        if (cacheIt != resolvedMaterialListCache_.end()) {
            geom->materialData = cacheIt->second;
            LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': materialList cache HIT for texBrushSetRef={}", fragIndex, geom->name, texBrushSetRef);
        } else {
            bool usedFallbackMaterialList = false;
            auto it31 = brushSets_.find(texBrushSetRef);
            if (it31 == brushSets_.end() && lastBrushSetFragIndex_ != 0) {
                // T211: fall back to the nearest preceding 0x31 material list.
                auto fallback = brushSets_.find(lastBrushSetFragIndex_);
                if (fallback != brushSets_.end()) {
                    LOG_DEBUG(MOD_GRAPHICS, "Frag2C[{}] '{}': texBrushSetRef={} unresolvable "
                              "(flags=0x{:x}); using nearest preceding materialList frag {}",
                              fragIndex, geom->name, texBrushSetRef, header.flags,
                              lastBrushSetFragIndex_);
                    it31 = fallback;
                    usedFallbackMaterialList = true;
                }
            }
            if (it31 != brushSets_.end()) {
                LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': found brushSet for texBrushSetRef={} with {} brushRefs",
                          fragIndex, geom->name, texBrushSetRef, it31->second.brushRefs.size());
                auto matList = std::make_shared<ResolvedMaterialList>();
                const WldTextureBrushSet& brushSet = it31->second;

                for (uint32_t brushRef : brushSet.brushRefs) {
                    std::string texName;
                    bool isInvisible = false;
                    bool isMasked = false;
                    TextureAnimationInfo animInfo;

                    auto it30 = materials_.find(brushRef);
                    if (it30 != materials_.end()) {
                        const WldTextureBrush& material = it30->second;

                        if (material.flags & 1) {
                            isInvisible = true;
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
                                    }
                                }

                                if (!animInfo.frames.empty()) {
                                    texName = animInfo.frames[0];
                                }
                            }
                        } else {
                            isInvisible = true;
                        }
                    } else {
                        isInvisible = true;
                    }

                    matList->textureNames.push_back(texName);
                    matList->textureInvisible.push_back(isInvisible);
                    matList->textureMasked.push_back(isMasked);
                    matList->textureAnimations.push_back(animInfo);
                }

                resolvedMaterialListCache_[texBrushSetRef] = matList;
                geom->materialData = matList;
                LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': resolved materialList with {} textureNames",
                          fragIndex, geom->name, matList->textureNames.size());
                if (usedFallbackMaterialList) {
                    // T211: report what the fallback actually produced, not just
                    // that the lookup stopped failing. Empty names here would
                    // mean the mesh still renders as the magenta placeholder.
                    size_t named = 0;
                    for (const auto& n : matList->textureNames) if (!n.empty()) ++named;
                    LOG_INFO(MOD_GRAPHICS, "Frag2C[{}] '{}': fallback materialList frag {} -> "
                             "{} textureNames ({} non-empty, first='{}')",
                             fragIndex, geom->name, lastBrushSetFragIndex_,
                             matList->textureNames.size(), named,
                             matList->textureNames.empty() ? "" : matList->textureNames[0]);
                }
            } else {
                // T211: is this a near-miss (ref inside the key range, one entry
                // absent) or the wrong field entirely (ref nowhere near any key)?
                // The 0x2C header has no material-list reference field, so
                // texBrushSetRef is `flags & 0xFFFF` -- a heuristic, not a spec
                // field. Print the key range so the answer is in the log.
                uint32_t kmin = UINT32_MAX, kmax = 0;
                for (const auto& [k, v] : brushSets_) { if (k < kmin) kmin = k; if (k > kmax) kmax = k; }
                LOG_WARN(MOD_GRAPHICS, "Frag2C[{}] '{}': texBrushSetRef={} NOT FOUND in brushSets_ "
                         "(size={}, keys {}..{}, flags=0x{:x})",
                         fragIndex, geom->name, texBrushSetRef, brushSets_.size(),
                         brushSets_.empty() ? 0 : kmin, kmax, header.flags);
            }
        }
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': texBrushSetRef=0, no materialList to resolve", fragIndex, geom->name);
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
        LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': textureNames empty after resolution, created {} placeholder entries",
                  fragIndex, geom->name, maxIdx + 1);
    }

    if (!geom->vertices.empty() && !geom->triangles.empty()) {
        geometries_.push_back(geom);
        geometryByFragIndex_[fragIndex] = geom;
        LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': stored legacy mesh (verts={} tris={} textureNames={} materialData={})",
                  fragIndex, geom->name, geom->vertices.size(), geom->triangles.size(),
                  geom->textureNames().size(), (bool)geom->materialData);
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag2C[{}] '{}': NOT stored (verts={} tris={})", fragIndex, geom->name, geom->vertices.size(), geom->triangles.size());
    }
}

void WldLoader::parseFragment2D(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragment2DHeader header = read_val<WldFragment2DHeader>(fragBuffer);

    WldModelRef modelRef;
    if (header.ref > 0) {
        modelRef.geometryFragRef = header.ref;
        LOG_TRACE(MOD_GRAPHICS, "Frag2D[{}]: MeshReference ref={}", fragIndex, header.ref);
    } else {
        modelRef.geometryFragRef = 0;
        LOG_TRACE(MOD_GRAPHICS, "Frag2D[{}]: MeshReference ref={} (<=0, stored as 0)", fragIndex, header.ref);
    }

    modelRefs_[fragIndex] = modelRef;
}

} // namespace Graphics
} // namespace EQT
