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

namespace EQT {
namespace Graphics {

// XOR key for decoding WLD string hash
static const uint8_t HASH_KEY[] = { 0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A };

void decodeStringHash(char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        str[i] ^= HASH_KEY[i % 8];
    }
}

bool WldLoader::parseFromArchive(const std::string& archivePath, const std::string& wldName) {
    geometries_.clear();
    textures_.clear();
    brushes_.clear();
    textureRefs_.clear();
    materials_.clear();
    brushSets_.clear();
    textureNames_.clear();
    placeables_.clear();
    objectDefs_.clear();
    modelRefs_.clear();
    skeletonTracks_.clear();
    skeletonRefs_.clear();
    boneOrientations_.clear();
    boneOrientationRefs_.clear();
    lightDefs_.clear();
    lights_.clear();
    geometryByFragIndex_.clear();
    trackDefs_.clear();
    trackRefs_.clear();
    meshAnimatedVertices_.clear();
    meshAnimatedVerticesRefs_.clear();
    bspTree_.reset();

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        return false;
    }

    std::vector<char> buffer;
    if (!archive.get(wldName, buffer)) {
        return false;
    }

    return parseWldBuffer(buffer);
}

bool WldLoader::parseWldBuffer(const std::vector<char>& buffer) {
    if (buffer.size() < sizeof(WldHeader)) {
        return false;
    }

    size_t idx = 0;
    const WldHeader* header = reinterpret_cast<const WldHeader*>(&buffer[idx]);
    idx += sizeof(WldHeader);

    // Check magic
    if (header->magic != 0x54503d02) {
        return false;
    }

    // Determine if old format
    bool oldFormat = (header->version == 0x00015500);

    // Read and decode string hash
    if (idx + header->hashLength > buffer.size()) {
        return false;
    }

    std::vector<char> hashBuffer(header->hashLength);
    std::memcpy(hashBuffer.data(), &buffer[idx], header->hashLength);
    decodeStringHash(hashBuffer.data(), header->hashLength);
    idx += header->hashLength;

    // Parse fragments
    std::vector<std::pair<size_t, const WldFragmentHeader*>> fragmentOffsets;
    size_t fragIdx = idx;
    for (uint32_t i = 0; i < header->fragmentCount; ++i) {
        if (fragIdx + sizeof(WldFragmentHeader) > buffer.size()) {
            break;
        }

        const WldFragmentHeader* fragHeader = reinterpret_cast<const WldFragmentHeader*>(&buffer[fragIdx]);
        fragmentOffsets.push_back({fragIdx + sizeof(WldFragmentHeader), fragHeader});
        fragIdx += sizeof(WldFragmentHeader) + fragHeader->size;
    }

    // Parse all fragments
    // According to libeq/LanternExtractor, ALL fragment data starts with nameRef (4 bytes),
    // then fragment-specific data. We extract nameRef here and pass data+4 to parsers.
    for (uint32_t i = 0; i < fragmentOffsets.size(); ++i) {
        const auto& [dataOffset, fragHeader] = fragmentOffsets[i];
        const char* fragData = &buffer[dataOffset];
        uint32_t fragSize = fragHeader->size;

        // All fragments start with nameRef (4 bytes, signed int32)
        if (fragSize < 4) continue;
        int32_t nameRef = *reinterpret_cast<const int32_t*>(fragData);
        const char* fragBody = fragData + 4;  // Skip nameRef
        uint32_t fragBodySize = fragSize - 4;

        switch (fragHeader->id) {
            case 0x03:
                parseFragment03(fragBody, fragBodySize, i + 1, hashBuffer.data(), oldFormat);
                break;
            case 0x04:
                parseFragment04(fragBody, fragBodySize, i + 1, hashBuffer.data(), oldFormat);
                break;
            case 0x05:
                parseFragment05(fragBody, fragBodySize, i + 1);
                break;
            case 0x30:
                parseFragment30(fragBody, fragBodySize, i + 1, hashBuffer.data(), oldFormat);
                break;
            case 0x31:
                parseFragment31(fragBody, fragBodySize, i + 1);
                break;
            case 0x36:
                parseFragment36(fragBody, fragBodySize, i + 1, nameRef,
                               hashBuffer.data(), oldFormat);
                break;
            case 0x14:
                parseFragment14(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data(), oldFormat);
                break;
            case 0x15:
                parseFragment15(fragBody, fragBodySize, i + 1, hashBuffer.data(), oldFormat);
                break;
            case 0x2C:
                parseFragment2C(fragBody, fragBodySize, i + 1, nameRef,
                               hashBuffer.data(), oldFormat);
                break;
            case 0x2D:
                parseFragment2D(fragBody, fragBodySize, i + 1);
                break;
            case 0x10:
                parseFragment10(fragBody, fragBodySize, i + 1, nameRef,
                               hashBuffer.data(), oldFormat, fragmentOffsets, buffer);
                break;
            case 0x11:
                parseFragment11(fragBody, fragBodySize, i + 1);
                break;
            case 0x12:
                parseFragment12(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            case 0x13:
                parseFragment13(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            case 0x1B:
                parseFragment1B(fragBody, fragBodySize, i + 1, nameRef,
                               hashBuffer.data(), oldFormat);
                break;
            case 0x28:
                parseFragment28(fragBody, fragBodySize, i + 1);
                break;
            case 0x2F:
                parseFragment2F(fragBody, fragBodySize, i + 1);
                break;
            case 0x37:
                parseFragment37(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            case 0x21:
                parseFragment21(fragBody, fragBodySize, i + 1);
                break;
            case 0x22:
                parseFragment22(fragBody, fragBodySize, i + 1);
                break;
            case 0x29:
                parseFragment29(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            default:
                break;
        }
    }

    return !geometries_.empty() || !placeables_.empty() || !objectDefs_.empty() || !skeletonTracks_.empty() || !lights_.empty();
}

void WldLoader::parseFragment03(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    const WldFragment03Header* header = reinterpret_cast<const WldFragment03Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment03Header);

    uint32_t count = header->textureCount;
    if (count == 0) count = 1;

    WldTexture tex;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = *reinterpret_cast<const uint16_t*>(ptr);
        ptr += sizeof(uint16_t);

        std::vector<char> nameBuf(nameLen);
        std::memcpy(nameBuf.data(), ptr, nameLen);
        decodeStringHash(nameBuf.data(), nameLen);

        std::string texName(nameBuf.data());
        std::transform(texName.begin(), texName.end(), texName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        tex.frames.push_back(texName);

        ptr += nameLen;
    }

    textures_[fragIndex] = tex;
}

void WldLoader::parseFragment04(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    const WldFragment04Header* header = reinterpret_cast<const WldFragment04Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment04Header);

    WldTextureBrush brush;
    brush.flags = header->flags;

    // Check for animated texture (bit 3 set)
    brush.isAnimated = (header->flags & (1 << 3)) != 0;

    // Skip unknown field if bit 2 is set
    if (header->flags & (1 << 2)) {
        ptr += sizeof(uint32_t);
    }

    // Read animation delay if bit 3 is set (animated texture)
    if (brush.isAnimated) {
        brush.animationDelayMs = *reinterpret_cast<const int32_t*>(ptr);
        ptr += sizeof(int32_t);
    }

    uint32_t count = header->textureCount;
    if (count == 0) count = 1;

    for (uint32_t i = 0; i < count; ++i) {
        const WldFragmentRef* ref = reinterpret_cast<const WldFragmentRef*>(ptr);
        ptr += sizeof(WldFragmentRef);

        if (ref->id > 0) {
            brush.textureRefs.push_back(ref->id);
        }
    }

    brushes_[fragIndex] = brush;
}

void WldLoader::parseFragment05(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    const WldFragmentRef* ref = reinterpret_cast<const WldFragmentRef*>(fragBuffer);
    if (ref->id > 0) {
        textureRefs_[fragIndex] = ref->id;
    }
}

void WldLoader::parseFragment30(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    const WldFragment30Header* header = reinterpret_cast<const WldFragment30Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment30Header);

    if (header->flags == 0) {
        ptr += sizeof(uint32_t) * 2;
    }

    const WldFragmentRef* ref = reinterpret_cast<const WldFragmentRef*>(ptr);

    WldTextureBrush material;

    if (header->params1 == 0 || ref->id == 0) {
        material.flags = 1;
    } else {
        uint32_t frag05Ref = ref->id;
        auto it05 = textureRefs_.find(frag05Ref);
        if (it05 != textureRefs_.end()) {
            material.textureRefs.push_back(it05->second);
        }
        material.flags = 0;
    }

    materials_[fragIndex] = material;
}

void WldLoader::parseFragment31(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    const WldFragment31Header* header = reinterpret_cast<const WldFragment31Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment31Header);

    WldTextureBrushSet brushSet;
    for (uint32_t i = 0; i < header->count; ++i) {
        uint32_t refId = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += sizeof(uint32_t);

        if (refId > 0) {
            brushSet.brushRefs.push_back(refId);
        }
    }

    brushSets_[fragIndex] = brushSet;
}

std::string WldLoader::resolveTextureName(uint32_t texIndex) const {
    return "";
}

void WldLoader::parseFragment36(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    const WldFragment36Header* header = reinterpret_cast<const WldFragment36Header*>(fragBuffer);
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
        header->flags, header->frag1, header->centerX, header->centerY, header->centerZ);
    LOG_TRACE(MOD_GRAPHICS, "  verts={} polys={} texcoords={} normals={} colors={}",
        header->vertexCount, header->polygonCount, header->texCoordCount, header->normalCount, header->colorCount);
    LOG_TRACE(MOD_GRAPHICS, "  size6={} polyTexCnt={} vertTexCnt={} size9={} scale={} oldFmt={}",
        header->size6, header->polygonTexCount, header->vertexTexCount, header->size9, header->scale, oldFormat);

    geom->minX = header->minX;
    geom->minY = header->minY;
    geom->minZ = header->minZ;
    geom->maxX = header->maxX;
    geom->maxY = header->maxY;
    geom->maxZ = header->maxZ;

    // Store mesh center for skinning reference
    geom->centerX = header->centerX;
    geom->centerY = header->centerY;
    geom->centerZ = header->centerZ;

    float scale = 1.0f / static_cast<float>(1 << header->scale);
    float recip256 = 1.0f / 256.0f;
    float recip127 = 1.0f / 127.0f;

    // Read vertices
    // For character meshes, vertices are stored relative to center
    // We keep them that way for proper bone transform application
    geom->vertices.resize(header->vertexCount);
    const char* vertexStart = ptr;
    for (uint16_t i = 0; i < header->vertexCount; ++i) {
        const WldVertex* v = reinterpret_cast<const WldVertex*>(ptr);
        ptr += sizeof(WldVertex);

        // Store vertices relative to center (don't add center yet)
        // Bone transforms will position them correctly
        geom->vertices[i].x = v->x * scale;
        geom->vertices[i].y = v->y * scale;
        geom->vertices[i].z = v->z * scale;

        // Debug: Print vertices for character meshes - first few
        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Vert[{}]: raw({},{},{}) -> scaled({},{},{})",
                i, v->x, v->y, v->z, geom->vertices[i].x, geom->vertices[i].y, geom->vertices[i].z);
        }
    }
    LOG_TRACE(MOD_GRAPHICS, "  Total vertex bytes read: {} (expected {})", (ptr - vertexStart), header->vertexCount * 6);
    LOG_TRACE(MOD_GRAPHICS, "  Mesh center: ({},{},{})", geom->centerX, geom->centerY, geom->centerZ);

    // Read texture coordinates
    if (oldFormat) {
        for (uint16_t i = 0; i < header->texCoordCount && i < header->vertexCount; ++i) {
            const WldTexCoordOld* tc = reinterpret_cast<const WldTexCoordOld*>(ptr);
            ptr += sizeof(WldTexCoordOld);

            geom->vertices[i].u = tc->u * recip256;
            geom->vertices[i].v = tc->v * recip256;
        }
        ptr += sizeof(WldTexCoordOld) * (header->texCoordCount > header->vertexCount ?
                                         header->texCoordCount - header->vertexCount : 0);
    } else {
        for (uint16_t i = 0; i < header->texCoordCount && i < header->vertexCount; ++i) {
            const WldTexCoordNew* tc = reinterpret_cast<const WldTexCoordNew*>(ptr);
            ptr += sizeof(WldTexCoordNew);

            geom->vertices[i].u = tc->u;
            geom->vertices[i].v = tc->v;
        }
        ptr += sizeof(WldTexCoordNew) * (header->texCoordCount > header->vertexCount ?
                                         header->texCoordCount - header->vertexCount : 0);
    }

    // Read normals - reference uses in->x * recip_127 without subtracting 128
    for (uint16_t i = 0; i < header->normalCount && i < header->vertexCount; ++i) {
        const WldNormal* n = reinterpret_cast<const WldNormal*>(ptr);
        ptr += sizeof(WldNormal);

        geom->vertices[i].nx = static_cast<float>(n->x) * recip127;
        geom->vertices[i].ny = static_cast<float>(n->y) * recip127;
        geom->vertices[i].nz = static_cast<float>(n->z) * recip127;
    }
    ptr += sizeof(WldNormal) * (header->normalCount > header->vertexCount ?
                                header->normalCount - header->vertexCount : 0);

    // Skip colors (RGBA packed as uint32_t)
    ptr += sizeof(uint32_t) * header->colorCount;

    // Read polygons
    geom->triangles.resize(header->polygonCount);
    for (uint16_t i = 0; i < header->polygonCount; ++i) {
        const WldPolygon* p = reinterpret_cast<const WldPolygon*>(ptr);
        ptr += sizeof(WldPolygon);

        // Reference implementation reverses winding order: index[2], index[1], index[0]
        geom->triangles[i].v1 = p->index[2];
        geom->triangles[i].v2 = p->index[1];
        geom->triangles[i].v3 = p->index[0];
        geom->triangles[i].flags = p->flags;
        geom->triangles[i].textureIndex = 0;

        // Debug: Print first few polygon indices for character meshes
        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Poly[{}]: flags={} v1={} v2={} v3={} (max_v={})",
                i, p->flags, p->index[0], p->index[1], p->index[2], header->vertexCount);
        }
    }

    // Validate polygon indices
    bool hasInvalidIdx = false;
    for (const auto& tri : geom->triangles) {
        if (tri.v1 >= header->vertexCount || tri.v2 >= header->vertexCount || tri.v3 >= header->vertexCount) {
            hasInvalidIdx = true;
            break;
        }
    }
    if (hasInvalidIdx) {
        LOG_WARN(MOD_GRAPHICS, "  WARNING: Invalid polygon indices found in {}", geom->name);
    }

    // Parse vertex pieces (bone assignments) - size6 entries of 4 bytes each
    // Each entry is: int16_t count, int16_t boneIndex
    geom->vertexPieces.resize(header->size6);
    for (uint16_t i = 0; i < header->size6; ++i) {
        const int16_t* pieceData = reinterpret_cast<const int16_t*>(ptr);
        geom->vertexPieces[i].count = pieceData[0];
        geom->vertexPieces[i].boneIndex = pieceData[1];
        ptr += 4;

        if (i < 10) {
            LOG_TRACE(MOD_GRAPHICS, "  VertexPiece[{}]: count={} bone={}",
                i, geom->vertexPieces[i].count, geom->vertexPieces[i].boneIndex);
        }
    }
    if (header->size6 > 0) {
        LOG_TRACE(MOD_GRAPHICS, "  Total vertex pieces: {}", header->size6);
    }

    // Read texture mappings (polygonsByTex)
    uint32_t polyIdx = 0;
    for (uint16_t i = 0; i < header->polygonTexCount; ++i) {
        const WldTexMap* tm = reinterpret_cast<const WldTexMap*>(ptr);
        ptr += sizeof(WldTexMap);

        for (uint16_t j = 0; j < tm->polyCount && polyIdx < geom->triangles.size(); ++j) {
            geom->triangles[polyIdx++].textureIndex = tm->tex;
        }
    }

    // Skip verticesByTex entries (vertexTexCount * 4 bytes)
    // This data maps vertices to textures but we don't need it for rendering
    ptr += 4 * header->vertexTexCount;

    // Resolve texture names
    if (header->frag1 > 0) {
        auto it31 = brushSets_.find(header->frag1);
        if (it31 != brushSets_.end()) {
            const WldTextureBrushSet& brushSet = it31->second;

            for (uint32_t brushRef : brushSet.brushRefs) {
                std::string texName;
                bool isInvisible = false;
                TextureAnimationInfo animInfo;

                auto it30 = materials_.find(brushRef);
                if (it30 != materials_.end()) {
                    const WldTextureBrush& material = it30->second;

                    if (material.flags & 1) {
                        isInvisible = true;
                    }

                    if (!material.textureRefs.empty()) {
                        uint32_t brushIdx = material.textureRefs[0];
                        auto it04 = brushes_.find(brushIdx);
                        if (it04 != brushes_.end()) {
                            const WldTextureBrush& brush = it04->second;

                            // Get animation info from the brush (Fragment 0x04)
                            animInfo.isAnimated = brush.isAnimated;
                            animInfo.animationDelayMs = brush.animationDelayMs;

                            // Collect all frame texture names from all referenced 0x03 fragments
                            for (uint32_t texIdx : brush.textureRefs) {
                                auto it03 = textures_.find(texIdx);
                                if (it03 != textures_.end()) {
                                    const WldTexture& tex = it03->second;
                                    for (const auto& frame : tex.frames) {
                                        animInfo.frames.push_back(frame);
                                    }
                                }
                            }

                            // Primary texture name is the first frame
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

                geom->textureNames.push_back(texName);
                geom->textureInvisible.push_back(isInvisible);
                geom->textureAnimations.push_back(animInfo);
            }
        }
    }

    if (geom->textureNames.empty()) {
        std::set<uint32_t> uniqueTexIndices;
        for (const auto& tri : geom->triangles) {
            uniqueTexIndices.insert(tri.textureIndex);
        }
        uint32_t maxIdx = uniqueTexIndices.empty() ? 0 : *uniqueTexIndices.rbegin();
        geom->textureNames.resize(maxIdx + 1);
        geom->textureInvisible.resize(maxIdx + 1, false);
        geom->textureAnimations.resize(maxIdx + 1);
    }

    // Link vertex animation data if this mesh has animated vertices
    // frag2 contains the reference to the 0x2F fragment (mesh animated vertices reference)
    if (header->frag2 > 0) {
        uint32_t animVertRefFragIdx = header->frag2;
        // Look up the 0x2F -> 0x37 mapping
        auto refIt = meshAnimatedVerticesRefs_.find(animVertRefFragIdx);
        if (refIt != meshAnimatedVerticesRefs_.end()) {
            uint32_t animVertFragIdx = refIt->second;
            // Look up the actual animated vertices data
            auto animIt = meshAnimatedVertices_.find(animVertFragIdx);
            if (animIt != meshAnimatedVertices_.end()) {
                geom->animatedVertices = animIt->second;
                LOG_DEBUG(MOD_GRAPHICS, "WldLoader: Linked mesh '{}' to vertex animation '{}' ({} frames)",
                    geom->name, animIt->second->name, animIt->second->frames.size());
            }
        }
    }

    if (!geom->vertices.empty() && !geom->triangles.empty()) {
        geometries_.push_back(geom);
        // Also store by fragment index for precise bone model lookups
        geometryByFragIndex_[fragIndex] = geom;
    }
}

std::shared_ptr<ZoneGeometry> WldLoader::getCombinedGeometry() const {
    if (geometries_.empty()) {
        return nullptr;
    }

    auto combined = std::make_shared<ZoneGeometry>();
    combined->name = "combined";

    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    std::map<std::string, uint32_t> globalTexMap;
    uint32_t vertexOffset = 0;

    for (const auto& geom : geometries_) {
        std::vector<uint32_t> texRemap;
        for (size_t i = 0; i < geom->textureNames.size(); ++i) {
            const auto& texName = geom->textureNames[i];
            bool isInvisible = (i < geom->textureInvisible.size()) ? geom->textureInvisible[i] : false;
            TextureAnimationInfo animInfo;
            if (i < geom->textureAnimations.size()) {
                animInfo = geom->textureAnimations[i];
            }

            auto it = globalTexMap.find(texName);
            if (it != globalTexMap.end()) {
                texRemap.push_back(it->second);
                if (isInvisible && it->second < combined->textureInvisible.size()) {
                    combined->textureInvisible[it->second] = true;
                }
                // Update animation info if this one has animation and existing doesn't
                if (animInfo.isAnimated && it->second < combined->textureAnimations.size() &&
                    !combined->textureAnimations[it->second].isAnimated) {
                    combined->textureAnimations[it->second] = animInfo;
                }
            } else {
                uint32_t newIdx = static_cast<uint32_t>(combined->textureNames.size());
                globalTexMap[texName] = newIdx;
                combined->textureNames.push_back(texName);
                combined->textureInvisible.push_back(isInvisible);
                combined->textureAnimations.push_back(animInfo);
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

    return combined;
}

void WldLoader::parseFragment14(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    const WldFragment14Header* header = reinterpret_cast<const WldFragment14Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment14Header);

    WldObjectDef objDef;

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0 ) {
            objDef.name = std::string(&hash[nameOffset]);
            std::transform(objDef.name.begin(), objDef.name.end(), objDef.name.begin(),
                          [](unsigned char c) { return std::toupper(c); });
        }
    }

    if (header->flags & 1) ptr += sizeof(int32_t);
    if (header->flags & 2) ptr += sizeof(int32_t);

    for (uint32_t i = 0; i < header->entries; ++i) {
        if (ptr + sizeof(uint32_t) > fragBuffer + fragLength) break;
        uint32_t sz = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += sizeof(uint32_t);
        ptr += sz * (sizeof(int32_t) + sizeof(float));
    }

    for (uint32_t i = 0; i < header->entries2; ++i) {
        if (ptr + sizeof(uint32_t) > fragBuffer + fragLength) break;
        uint32_t ref = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += sizeof(uint32_t);

        if (ref > 0) {
            objDef.meshRefs.push_back(ref);
        }
    }

    if (!objDef.name.empty()) {
        objectDefs_[objDef.name] = objDef;
    }
}

void WldLoader::parseFragment15(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    const WldFragmentRef* ref = reinterpret_cast<const WldFragmentRef*>(fragBuffer);
    const WldFragment15Header* header = reinterpret_cast<const WldFragment15Header*>(
        fragBuffer + sizeof(WldFragmentRef));

    if (ref->id >= 0) {
        return;
    }

    auto placeable = std::make_shared<Placeable>();

    int32_t nameOffset = -ref->id;
    if (nameOffset > 0 ) {
        std::string name(&hash[nameOffset]);
        std::transform(name.begin(), name.end(), name.begin(),
                      [](unsigned char c) { return std::toupper(c); });
        placeable->setName(name);
    }

    placeable->setLocation(header->x, header->y, header->z);

    float rotX = header->rotateX / 512.0f * 360.0f;
    float rotY = header->rotateY / 512.0f * 360.0f;
    float rotZ = header->rotateZ / 512.0f * 360.0f;
    placeable->setRotation(rotX, rotY, rotZ);

    float scaleX = header->scaleX;
    float scaleY = header->scaleY;
    placeable->setScale(scaleX, scaleY, scaleY);

    placeables_.push_back(placeable);
}

void WldLoader::parseFragment2C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    // Legacy Mesh format (0x2C) - found in older character models like global_chr.s3d
    // Uses uncompressed 32-bit floats for vertices, normals, and UVs
    const WldFragment2CHeader* header = reinterpret_cast<const WldFragment2CHeader*>(fragBuffer);
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
        header->flags, header->centerX, header->centerY, header->centerZ, header->scale);
    LOG_TRACE(MOD_GRAPHICS, "  verts={} polys={} texcoords={} normals={} colors={}",
        header->vertexCount, header->polygonCount, header->texCoordCount, header->normalCount, header->colorCount);
    LOG_TRACE(MOD_GRAPHICS, "  vertexPieceCount={} polyTexCnt={} vertTexCnt={}",
        header->vertexPieceCount, header->polygonTexCount, header->vertexTexCount);

    geom->minX = header->minX;
    geom->minY = header->minY;
    geom->minZ = header->minZ;
    geom->maxX = header->maxX;
    geom->maxY = header->maxY;
    geom->maxZ = header->maxZ;

    geom->centerX = header->centerX;
    geom->centerY = header->centerY;
    geom->centerZ = header->centerZ;

    // Read vertices - raw 32-bit floats (not compressed)
    geom->vertices.resize(header->vertexCount);
    for (uint32_t i = 0; i < header->vertexCount; ++i) {
        const float* v = reinterpret_cast<const float*>(ptr);
        ptr += sizeof(float) * 3;

        geom->vertices[i].x = v[0];
        geom->vertices[i].y = v[1];
        geom->vertices[i].z = v[2];

        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Vert[{}]: ({},{},{})", i, v[0], v[1], v[2]);
        }
    }

    // Read texture coordinates - raw 32-bit floats
    for (uint32_t i = 0; i < header->texCoordCount && i < header->vertexCount; ++i) {
        const float* tc = reinterpret_cast<const float*>(ptr);
        ptr += sizeof(float) * 2;

        geom->vertices[i].u = tc[0];
        geom->vertices[i].v = tc[1];
    }
    // Skip extra tex coords if any
    if (header->texCoordCount > header->vertexCount) {
        ptr += sizeof(float) * 2 * (header->texCoordCount - header->vertexCount);
    }

    // Read normals - raw 32-bit floats
    for (uint32_t i = 0; i < header->normalCount && i < header->vertexCount; ++i) {
        const float* n = reinterpret_cast<const float*>(ptr);
        ptr += sizeof(float) * 3;

        geom->vertices[i].nx = n[0];
        geom->vertices[i].ny = n[1];
        geom->vertices[i].nz = n[2];
    }
    // Skip extra normals if any
    if (header->normalCount > header->vertexCount) {
        ptr += sizeof(float) * 3 * (header->normalCount - header->vertexCount);
    }

    // Skip colors (RGBA as uint32_t)
    ptr += sizeof(uint32_t) * header->colorCount;

    // Read polygons - same format as modern mesh
    geom->triangles.resize(header->polygonCount);
    for (uint32_t i = 0; i < header->polygonCount; ++i) {
        const WldPolygon* p = reinterpret_cast<const WldPolygon*>(ptr);
        ptr += sizeof(WldPolygon);

        // Reverse winding order like modern mesh
        geom->triangles[i].v1 = p->index[2];
        geom->triangles[i].v2 = p->index[1];
        geom->triangles[i].v3 = p->index[0];
        geom->triangles[i].flags = p->flags;
        geom->triangles[i].textureIndex = 0;

        if (i < 5) {
            LOG_TRACE(MOD_GRAPHICS, "  Poly[{}]: flags={} v={},{},{}",
                i, p->flags, p->index[0], p->index[1], p->index[2]);
        }
    }

    // Parse vertex pieces (bone assignments) - Legacy format reads Start directly from file
    // Format: int16_t count, int16_t startIndex
    geom->vertexPieces.resize(header->vertexPieceCount);
    for (uint16_t i = 0; i < header->vertexPieceCount; ++i) {
        const int16_t* pieceData = reinterpret_cast<const int16_t*>(ptr);
        geom->vertexPieces[i].count = pieceData[0];
        geom->vertexPieces[i].boneIndex = pieceData[1];  // In legacy, this is the start index used as key
        ptr += 4;

        if (i < 10) {
            LOG_TRACE(MOD_GRAPHICS, "  VertexPiece[{}]: count={} bone/start={}",
                i, geom->vertexPieces[i].count, geom->vertexPieces[i].boneIndex);
        }
    }

    // Read texture mappings
    uint32_t polyIdx = 0;
    for (uint16_t i = 0; i < header->polygonTexCount; ++i) {
        const WldTexMap* tm = reinterpret_cast<const WldTexMap*>(ptr);
        ptr += sizeof(WldTexMap);

        for (uint16_t j = 0; j < tm->polyCount && polyIdx < geom->triangles.size(); ++j) {
            geom->triangles[polyIdx++].textureIndex = tm->tex;
        }
    }

    // Skip verticesByTex entries
    ptr += 4 * header->vertexTexCount;

    // Resolve texture names - try fragment reference from flags
    // Legacy mesh may store texture brush set reference differently
    uint32_t texBrushSetRef = header->flags & 0xFFFF;  // Lower 16 bits might be the reference
    if (texBrushSetRef > 0) {
        auto it31 = brushSets_.find(texBrushSetRef);
        if (it31 != brushSets_.end()) {
            const WldTextureBrushSet& brushSet = it31->second;

            for (uint32_t brushRef : brushSet.brushRefs) {
                std::string texName;
                bool isInvisible = false;
                TextureAnimationInfo animInfo;

                auto it30 = materials_.find(brushRef);
                if (it30 != materials_.end()) {
                    const WldTextureBrush& material = it30->second;

                    if (material.flags & 1) {
                        isInvisible = true;
                    }

                    if (!material.textureRefs.empty()) {
                        uint32_t brushIdx = material.textureRefs[0];
                        auto it04 = brushes_.find(brushIdx);
                        if (it04 != brushes_.end()) {
                            const WldTextureBrush& brush = it04->second;

                            // Get animation info from the brush (Fragment 0x04)
                            animInfo.isAnimated = brush.isAnimated;
                            animInfo.animationDelayMs = brush.animationDelayMs;

                            // Collect all frame texture names from all referenced 0x03 fragments
                            for (uint32_t texIdx : brush.textureRefs) {
                                auto it03 = textures_.find(texIdx);
                                if (it03 != textures_.end()) {
                                    const WldTexture& tex = it03->second;
                                    for (const auto& frame : tex.frames) {
                                        animInfo.frames.push_back(frame);
                                    }
                                }
                            }

                            // Primary texture name is the first frame
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

                geom->textureNames.push_back(texName);
                geom->textureInvisible.push_back(isInvisible);
                geom->textureAnimations.push_back(animInfo);
            }
        }
    }

    if (geom->textureNames.empty()) {
        std::set<uint32_t> uniqueTexIndices;
        for (const auto& tri : geom->triangles) {
            uniqueTexIndices.insert(tri.textureIndex);
        }
        uint32_t maxIdx = uniqueTexIndices.empty() ? 0 : *uniqueTexIndices.rbegin();
        geom->textureNames.resize(maxIdx + 1);
        geom->textureInvisible.resize(maxIdx + 1, false);
        geom->textureAnimations.resize(maxIdx + 1);
    }

    if (!geom->vertices.empty() && !geom->triangles.empty()) {
        geometries_.push_back(geom);
        geometryByFragIndex_[fragIndex] = geom;
        LOG_TRACE(MOD_GRAPHICS, "  Added Legacy Mesh with {} verts, {} tris",
            geom->vertices.size(), geom->triangles.size());
    }
}

void WldLoader::parseFragment2D(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    const WldFragment2DHeader* header = reinterpret_cast<const WldFragment2DHeader*>(fragBuffer);

    WldModelRef modelRef;
    if (header->ref > 0) {
        modelRef.geometryFragRef = header->ref;
    } else {
        modelRef.geometryFragRef = 0;
    }

    modelRefs_[fragIndex] = modelRef;
}

void WldLoader::parseFragment10(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat,
                                const std::vector<std::pair<size_t, const WldFragmentHeader*>>& fragments,
                                const std::vector<char>& buffer) {
    const WldFragment10Header* header = reinterpret_cast<const WldFragment10Header*>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment10Header);

    auto track = std::make_shared<SkeletonTrack>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        // The hash can be much larger than 65536 for big archives like global_chr.s3d
        if (nameOffset > 0) {
            track->name = std::string(&hash[nameOffset]);
        }
    }

    if (header->flags & 1) ptr += sizeof(int32_t) * 3;
    if (header->flags & 2) ptr += sizeof(float);

    std::vector<std::shared_ptr<SkeletonBone>> allBones;
    allBones.reserve(header->trackRefCount);

    // Collect tree relationships: (parent_bone_index, child_bone_index)
    std::vector<std::pair<int, int>> treeRelations;

    for (uint32_t i = 0; i < header->trackRefCount; ++i) {
        if (ptr + sizeof(WldFragment10BoneEntry) > fragBuffer + fragLength) break;

        const WldFragment10BoneEntry* entry = reinterpret_cast<const WldFragment10BoneEntry*>(ptr);
        ptr += sizeof(WldFragment10BoneEntry);

        auto bone = std::make_shared<SkeletonBone>();

        if (entry->nameRef < 0) {
            int32_t nameOffset = -entry->nameRef;
            if (nameOffset > 0 ) {
                bone->name = std::string(&hash[nameOffset]);
            }
        }

        if (entry->orientationRef > 0) {
            auto it13 = boneOrientationRefs_.find(entry->orientationRef);
            if (it13 != boneOrientationRefs_.end()) {
                auto it12 = boneOrientations_.find(it13->second);
                if (it12 != boneOrientations_.end()) {
                    bone->orientation = it12->second;
                }
            }
        }

        if (entry->modelRef > 0) {
            bone->modelRef = entry->modelRef;
        }

        allBones.push_back(bone);

        // Read child indices - store for later processing
        for (uint32_t j = 0; j < entry->childCount; ++j) {
            if (ptr + sizeof(int32_t) > fragBuffer + fragLength) break;
            int32_t childIdx = *reinterpret_cast<const int32_t*>(ptr);
            ptr += sizeof(int32_t);
            // Store relationship: current bone (i) has child at childIdx
            treeRelations.push_back(std::make_pair(static_cast<int>(i), childIdx));
        }
    }

    // Build parent index array (initialized to -1 for roots)
    track->parentIndices.resize(allBones.size(), -1);

    // Now establish parent-child relationships after all bones are created
    for (const auto& relation : treeRelations) {
        int parentIdx = relation.first;
        int childIdx = relation.second;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < allBones.size() &&
            childIdx >= 0 && static_cast<size_t>(childIdx) < allBones.size()) {
            allBones[parentIdx]->children.push_back(allBones[childIdx]);
            track->parentIndices[childIdx] = parentIdx;
        }
    }

    // Store all bones in original file order
    track->allBones = allBones;

    // Find root bones (bones that are not children of any other bone)
    std::set<std::shared_ptr<SkeletonBone>> childBones;
    for (const auto& bone : allBones) {
        for (const auto& child : bone->children) {
            childBones.insert(child);
        }
    }

    for (const auto& bone : allBones) {
        if (childBones.find(bone) == childBones.end()) {
            track->bones.push_back(bone);
        }
    }

    skeletonTracks_[fragIndex] = track;
}

void WldLoader::parseFragment11(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    const WldFragment11Header* header = reinterpret_cast<const WldFragment11Header*>(fragBuffer);
    if (header->ref > 0) {
        skeletonRefs_[fragIndex] = header->ref;
    }
}

void WldLoader::parseFragment12(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x12 - TrackDef - Contains animation keyframe data
    // Structure:
    //   int32_t nameRef (already in header)
    //   int32_t flags
    //   int32_t frameCount
    //   For each frame:
    //     int16_t rotDenom (quaternion W)
    //     int16_t rotX, rotY, rotZ (quaternion X, Y, Z)
    //     int16_t shiftX, shiftY, shiftZ (translation)
    //     int16_t shiftDenom (scale)

    const char* ptr = fragBuffer;

    // Read flags
    uint32_t flags = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += sizeof(uint32_t);

    // Read frame count
    int32_t frameCount = *reinterpret_cast<const int32_t*>(ptr);
    ptr += sizeof(int32_t);

    auto trackDef = std::make_shared<TrackDef>();
    trackDef->fragIndex = fragIndex;

    // Get name from hash
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            trackDef->name = std::string(&hash[nameOffset]);
        }
    }

    // Parse all frames
    trackDef->frames.reserve(frameCount);

    for (int32_t i = 0; i < frameCount; ++i) {
        BoneTransform frame;

        int16_t rotDenom = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t rotX = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t rotY = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t rotZ = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t shiftX = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t shiftY = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t shiftZ = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        int16_t shiftDenom = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;

        // EQ stores rotation as quaternion components (w, x, y, z) as int16 values
        float qw = static_cast<float>(rotDenom);
        float qx = static_cast<float>(rotX);
        float qy = static_cast<float>(rotY);
        float qz = static_cast<float>(rotZ);

        // Normalize the quaternion
        float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
        if (len > 0.0001f) {
            frame.quatX = qx / len;
            frame.quatY = qy / len;
            frame.quatZ = qz / len;
            frame.quatW = qw / len;
        } else {
            // Identity quaternion if zero length
            frame.quatX = 0.0f;
            frame.quatY = 0.0f;
            frame.quatZ = 0.0f;
            frame.quatW = 1.0f;
        }

        // Translation is divided by 256 (fixed point)
        frame.shiftX = static_cast<float>(shiftX) / 256.0f;
        frame.shiftY = static_cast<float>(shiftY) / 256.0f;
        frame.shiftZ = static_cast<float>(shiftZ) / 256.0f;

        // Scale is shiftDenom / 256 (defaults to 1.0 if zero)
        frame.scale = (shiftDenom != 0) ? static_cast<float>(shiftDenom) / 256.0f : 1.0f;

        trackDef->frames.push_back(frame);
    }

    trackDefs_[fragIndex] = trackDef;

    // Also store first frame as BoneOrientation for backwards compatibility
    if (!trackDef->frames.empty()) {
        auto orientation = std::make_shared<BoneOrientation>(trackDef->frames[0]);
        boneOrientations_[fragIndex] = orientation;
    }
}

void WldLoader::parseFragment13(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x13 - TrackRef - References a TrackDef and contains metadata
    // Structure:
    //   int32_t nameRef (already in header)
    //   int32_t trackDefRef
    //   int32_t flags
    //   [optional] int32_t frameMs (if flags bit 0 is set)

    const char* ptr = fragBuffer;

    int32_t trackDefRef = *reinterpret_cast<const int32_t*>(ptr);
    ptr += sizeof(int32_t);

    int32_t flags = *reinterpret_cast<const int32_t*>(ptr);
    ptr += sizeof(int32_t);

    auto trackRef = std::make_shared<TrackRef>();

    // Get name from hash
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            trackRef->name = std::string(&hash[nameOffset]);
        }
    }

    trackRef->trackDefRef = (trackDefRef > 0) ? static_cast<uint32_t>(trackDefRef) : 0;
    trackRef->isPoseAnimation = false;
    trackRef->isNameParsed = false;

    // Read frameMs if flags bit 0 is set
    if (flags & 1) {
        trackRef->frameMs = *reinterpret_cast<const int32_t*>(ptr);
        ptr += sizeof(int32_t);
    } else {
        trackRef->frameMs = 0;
    }

    // Parse the track name to extract animation code, model code, and bone name
    // Track names follow the pattern: {ANIM_CODE}{MODEL_CODE}{BONE_NAME}_TRACK
    // e.g., "C01HUF_ROOT_TRACK" -> animCode="c01", modelCode="huf", boneName="root"
    // or "HUF_ROOT_TRACK" for pose animation -> animCode="pos", modelCode="huf", boneName="root"
    if (!trackRef->name.empty()) {
        std::string cleanedName = trackRef->name;

        // Remove _TRACK suffix if present
        size_t trackPos = cleanedName.find("_TRACK");
        if (trackPos != std::string::npos) {
            cleanedName = cleanedName.substr(0, trackPos);
        }

        // Convert to lowercase for consistent comparison
        std::transform(cleanedName.begin(), cleanedName.end(), cleanedName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if first 3 chars look like an animation code (letter + 2 digits)
        bool hasAnimCode = false;
        if (cleanedName.length() >= 6 &&
            std::isalpha(cleanedName[0]) &&
            std::isdigit(cleanedName[1]) &&
            std::isdigit(cleanedName[2])) {
            hasAnimCode = true;
        }

        if (hasAnimCode) {
            // Pattern: {ANIM_CODE}{MODEL_CODE}{BONE_NAME}
            // e.g., "l01humpe" -> animCode="l01", modelCode="hum", boneName="pe"
            trackRef->animCode = cleanedName.substr(0, 3);
            trackRef->modelCode = cleanedName.substr(3, 3);
            trackRef->boneName = cleanedName.substr(6);
            trackRef->isNameParsed = true;
        } else if (cleanedName.length() >= 4) {
            // Pattern: {MODEL_CODE}{BONE_NAME} (pose animation)
            // e.g., "humpe" -> animCode="pos", modelCode="hum", boneName="pe"
            trackRef->animCode = "pos";
            trackRef->modelCode = cleanedName.substr(0, 3);
            trackRef->boneName = cleanedName.substr(3);
            trackRef->isPoseAnimation = true;
            trackRef->isNameParsed = true;
        } else if (cleanedName.length() == 3) {
            // Just model code (root bone pose)
            trackRef->animCode = "pos";
            trackRef->modelCode = cleanedName;
            trackRef->boneName = "root";
            trackRef->isPoseAnimation = true;
            trackRef->isNameParsed = true;
        }

        // Clean up bone name - remove leading underscore if present
        if (!trackRef->boneName.empty() && trackRef->boneName[0] == '_') {
            trackRef->boneName = trackRef->boneName.substr(1);
        }
    }

    trackRefs_[fragIndex] = trackRef;

    // Also store reference for backwards compatibility
    if (trackRef->trackDefRef > 0) {
        boneOrientationRefs_[fragIndex] = trackRef->trackDefRef;
    }
}

void WldLoader::parseFragment1B(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    const WldFragment1BHeader* header = reinterpret_cast<const WldFragment1BHeader*>(fragBuffer);

    auto light = std::make_shared<ZoneLight>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0 ) {
            light->name = std::string(&hash[nameOffset]);
        }
    }

    light->x = 0.0f;
    light->y = 0.0f;
    light->z = 0.0f;
    light->radius = 0.0f;

    if (header->flags & (1 << 3)) {
        light->r = header->color[0];
        light->g = header->color[1];
        light->b = header->color[2];
    } else {
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
    }

    lightDefs_[fragIndex] = light;
}

void WldLoader::parseFragment28(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    const WldFragmentRef* ref = reinterpret_cast<const WldFragmentRef*>(fragBuffer);
    const WldFragment28Header* header = reinterpret_cast<const WldFragment28Header*>(
        fragBuffer + sizeof(WldFragmentRef));

    uint32_t lightDefIndex = ref->id;
    if (lightDefIndex == 0) {
        return;
    }

    auto it = lightDefs_.find(lightDefIndex);
    if (it == lightDefs_.end()) {
        auto light = std::make_shared<ZoneLight>();
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
        light->x = header->x;
        light->y = header->y;
        light->z = header->z;
        light->radius = header->radius;
        lights_.push_back(light);
        return;
    }

    auto light = std::make_shared<ZoneLight>();
    light->name = it->second->name;
    light->r = it->second->r;
    light->g = it->second->g;
    light->b = it->second->b;
    light->x = header->x;
    light->y = header->y;
    light->z = header->z;
    light->radius = header->radius;

    lights_.push_back(light);
}

void WldLoader::parseFragment2F(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x2F - Mesh Animated Vertices Reference
    // References a 0x37 fragment containing animated vertex frames
    if (fragLength < 8) {  // Minimum: ref + flags
        return;
    }

    // Read the fragment reference (to 0x37)
    const char* ptr = fragBuffer;
    int32_t meshAnimVertRef = *reinterpret_cast<const int32_t*>(ptr);

    LOG_DEBUG(MOD_GRAPHICS, "Fragment 0x2F: fragIndex={} meshAnimVertRef={}", fragIndex, meshAnimVertRef);

    // Store the mapping from this fragment to the 0x37 fragment it references
    if (meshAnimVertRef > 0) {
        meshAnimatedVerticesRefs_[fragIndex] = static_cast<uint32_t>(meshAnimVertRef);
    }
}

void WldLoader::parseFragment37(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                 int32_t nameRef, const char* hash) {
    // Fragment 0x37 - Mesh Animated Vertices (DMTRACKDEF)
    // Contains frames of vertex positions for vertex animation (flags, banners, etc.)
    if (fragLength < 18) {  // Minimum header size
        return;
    }

    // Read fields - fragment 0x37 format appears to be:
    // bytes 0-3: flags/unknown (4 bytes)
    // bytes 4-5: vertexCount (2 bytes)
    // bytes 6-7: frameCount (2 bytes)
    // bytes 8-9: delay (2 bytes)
    // bytes 10-11: param2 (2 bytes)
    // bytes 12-13: scale (2 bytes)
    // ... possibly more header bytes
    // Then vertex data: frameCount * vertexCount * 6 bytes

    const char* ptr = fragBuffer;
    uint32_t flags = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;
    uint16_t vertexCount = *reinterpret_cast<const uint16_t*>(ptr); ptr += 2;
    uint16_t frameCount = *reinterpret_cast<const uint16_t*>(ptr); ptr += 2;
    uint16_t delayMs = *reinterpret_cast<const uint16_t*>(ptr); ptr += 2;
    uint16_t param2 = *reinterpret_cast<const uint16_t*>(ptr); ptr += 2;
    int16_t scaleVal = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;

    // Header is 14 bytes: flags(4) + vertexCount(2) + frameCount(2) + delay(2) + param2(2) + scale(2)
    // ptr is already at byte 14 after reading header fields
    // (Don't use calculated headerSize as it may include padding)

    LOG_DEBUG(MOD_GRAPHICS, "Fragment 0x37: flags=0x{:x} vertexCount={} frameCount={} delayMs={} scale={} fragLen={}",
        flags, vertexCount, frameCount, delayMs, scaleVal, fragLength);

    auto animVerts = std::make_shared<MeshAnimatedVertices>();
    animVerts->fragIndex = fragIndex;

    // Name comes from fragment header's nameRef
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            animVerts->name = &hash[nameOffset];
        }
    }

    // Animation delay in milliseconds
    animVerts->delayMs = delayMs;
    if (animVerts->delayMs <= 0) {
        animVerts->delayMs = 100;  // Default to 100ms if not specified
    }

    // Calculate scale factor
    float scale = 1.0f / (1 << scaleVal);

    // ptr is already positioned after the header fields (18 bytes)

    // Read all frames
    animVerts->frames.resize(frameCount);
    for (uint16_t f = 0; f < frameCount; ++f) {
        animVerts->frames[f].positions.resize(vertexCount * 3);

        for (uint16_t v = 0; v < vertexCount; ++v) {
            const int16_t* vertData = reinterpret_cast<const int16_t*>(ptr);
            ptr += 6;  // 3 x int16_t

            // Store raw EQ coordinates (transform applied at render time if needed)
            animVerts->frames[f].positions[v * 3 + 0] = vertData[0] * scale;
            animVerts->frames[f].positions[v * 3 + 1] = vertData[1] * scale;
            animVerts->frames[f].positions[v * 3 + 2] = vertData[2] * scale;
        }
    }

    meshAnimatedVertices_[fragIndex] = animVerts;

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader: Parsed fragment 0x37 '{}' with {} frames, {} vertices, {}ms delay",
        animVerts->name, frameCount, vertexCount, animVerts->delayMs);
}

void WldLoader::parseFragment21(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x21 - BSP Tree
    // Contains the BSP tree nodes for zone region detection
    if (fragLength < 4) {
        return;
    }

    const char* ptr = fragBuffer;
    uint32_t nodeCount = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += sizeof(uint32_t);

    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x21: fragLength={} Loading {} BSP tree nodes", fragLength, nodeCount);
    LOG_TRACE(MOD_MAP, "[BSP] Expected size: {} bytes", 4 + nodeCount * 28);
    if (fragLength != 4 + nodeCount * 28) {
        LOG_WARN(MOD_MAP, "[BSP] WARNING: Size mismatch! Off by {} bytes", (int)(fragLength - (4 + nodeCount * 28)));
    }

    // Debug: count nodes with regions
    int nodesWithRegions = 0;

    // Create BSP tree if not exists
    if (!bspTree_) {
        bspTree_ = std::make_shared<BspTree>();
    }

    bspTree_->nodes.reserve(nodeCount);

    // Each node is 28 bytes: normalX(4) + normalY(4) + normalZ(4) + splitDist(4) + regionId(4) + left(4) + right(4)
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (ptr + 28 > fragBuffer + fragLength) break;

        BspNode node;
        node.normalX = *reinterpret_cast<const float*>(ptr); ptr += 4;
        node.normalY = *reinterpret_cast<const float*>(ptr); ptr += 4;
        node.normalZ = *reinterpret_cast<const float*>(ptr); ptr += 4;
        node.splitDistance = *reinterpret_cast<const float*>(ptr); ptr += 4;
        node.regionId = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        // Left/right are 1-indexed in file, convert to 0-indexed (-1 for no child)
        int32_t leftRaw = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        int32_t rightRaw = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        node.left = leftRaw - 1;
        node.right = rightRaw - 1;

        if (node.regionId > 0) {
            nodesWithRegions++;
            // Debug: print nodes with zone 4 regions (regionId 1902-1966 for regions 1901-1965)
            // Zone 4 regions are 1901, 1903, 1905, etc. up to ~1965
            if (node.regionId >= 1902 && node.regionId <= 1966) {
                LOG_TRACE(MOD_MAP, "[BSP] ZONE4 Node {} has regionId={} (region {}) normal=({},{},{}) dist={}",
                    i, node.regionId, node.regionId-1, node.normalX, node.normalY, node.normalZ, node.splitDistance);
            }
        }

        bspTree_->nodes.push_back(node);
    }

    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x21: {} nodes have regionId > 0", nodesWithRegions);
}

void WldLoader::parseFragment22(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x22 - BSP Region
    // Defines a region in the BSP tree (water, lava, zone line, etc.)
    if (fragLength < 40) {
        return;
    }

    // Create BSP tree if not exists
    if (!bspTree_) {
        bspTree_ = std::make_shared<BspTree>();
    }

    auto region = std::make_shared<BspRegion>();
    size_t regionIndex = bspTree_->regions.size();

    const char* ptr = fragBuffer;

    int32_t flags = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
    region->containsPolygons = (flags == 0x181);

    // Skip unknown1 (always 0)
    ptr += 4;

    int32_t data1Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
    int32_t data2Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;

    // Skip unknown2 (always 0)
    ptr += 4;

    int32_t data3Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
    int32_t data4Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;

    // Skip unknown3 (always 0)
    ptr += 4;

    int32_t data5Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
    int32_t data6Size = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;

    // Skip data1 and data2 (12 bytes each entry)
    ptr += 12 * data1Size + 12 * data2Size;

    // Skip data3 (variable size per entry)
    for (int32_t i = 0; i < data3Size; ++i) {
        if (ptr + 8 > fragBuffer + fragLength) break;
        int32_t data3Flags = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        int32_t data3Size2 = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        ptr += data3Size2 * 4;
    }

    // Skip data4 (no data per entry in known files)

    // Skip data5 (28 bytes each entry)
    ptr += 7 * 4 * data5Size;

    // Skip PVS data
    if (ptr + 2 <= fragBuffer + fragLength) {
        int16_t pvsSize = *reinterpret_cast<const int16_t*>(ptr); ptr += 2;
        ptr += pvsSize;
    }

    // Skip final unknown data
    if (ptr + 4 <= fragBuffer + fragLength) {
        ptr += 4;  // bytes
        ptr += 16; // unknown padding
    }

    // Read mesh reference if region contains polygons
    if (region->containsPolygons && ptr + 4 <= fragBuffer + fragLength) {
        region->meshReference = *reinterpret_cast<const int32_t*>(ptr);
    }

    bspTree_->regions.push_back(region);
}

void WldLoader::parseFragment29(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                 int32_t nameRef, const char* hash) {
    // Fragment 0x29 - Region Type
    // Links region type info (water, lava, zone line, etc.) to BSP regions
    if (fragLength < 12 || !bspTree_) {
        return;
    }

    const char* ptr = fragBuffer;

    int32_t flags = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
    int32_t regionCount = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;

    // Read region indices and link this type to them
    std::vector<int32_t> regionIndices;
    for (int32_t i = 0; i < regionCount; ++i) {
        if (ptr + 4 > fragBuffer + fragLength) break;
        int32_t regionIdx = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
        regionIndices.push_back(regionIdx);
    }

    // Get the region type string from the fragment NAME (not body)
    // nameRef is a negative offset into the string hash table
    std::string regionTypeString;
    if (nameRef < 0 && hash) {
        regionTypeString = std::string(hash + (-nameRef));
    }

    // Convert to lowercase for pattern matching
    std::transform(regionTypeString.begin(), regionTypeString.end(), regionTypeString.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Determine region types from the string
    std::vector<RegionType> types;
    std::optional<ZoneLineInfo> zoneLineInfo;

    if (regionTypeString.substr(0, 4) == "wtn_" || regionTypeString.substr(0, 3) == "wt_") {
        types.push_back(RegionType::Water);
    } else if (regionTypeString.substr(0, 5) == "wtntp") {
        types.push_back(RegionType::Water);
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.substr(0, 4) == "lan_" || regionTypeString.substr(0, 3) == "la_") {
        types.push_back(RegionType::Lava);
    } else if (regionTypeString.substr(0, 5) == "lantp") {
        types.push_back(RegionType::Lava);
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.substr(0, 5) == "drntp") {
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.length() >= 6 && regionTypeString[0] == 'z' &&
               regionTypeString.find("_zone") != std::string::npos) {
        // Handle z####_zone format (e.g., z0000_zone, z0001_zone)
        // The number after 'z' is the zone point index
        types.push_back(RegionType::Zoneline);
        ZoneLineInfo info;
        info.type = ZoneLineType::Reference;
        info.zonePointIndex = 0;
        // Extract zone point index from string (e.g., "z0009_zone" -> 9)
        size_t numStart = 1;  // Skip 'z'
        size_t numEnd = regionTypeString.find('_');
        if (numEnd != std::string::npos && numEnd > numStart) {
            try {
                info.zonePointIndex = std::stoul(regionTypeString.substr(numStart, numEnd - numStart));
            } catch (...) {
                // Keep default 0
            }
        }
        zoneLineInfo = info;
    } else if (regionTypeString.substr(0, 4) == "drp_") {
        types.push_back(RegionType::Pvp);
    } else if (regionTypeString.substr(0, 4) == "drn_") {
        if (regionTypeString.find("_s_") != std::string::npos) {
            types.push_back(RegionType::Slippery);
        } else {
            types.push_back(RegionType::Unknown);
        }
    } else if (regionTypeString.substr(0, 4) == "sln_") {
        types.push_back(RegionType::WaterBlockLOS);
    } else if (regionTypeString.substr(0, 4) == "vwn_") {
        types.push_back(RegionType::FreezingWater);
    } else {
        types.push_back(RegionType::Normal);
    }

    // Apply types to all referenced regions
    for (int32_t regionIdx : regionIndices) {
        if (regionIdx >= 0 && static_cast<size_t>(regionIdx) < bspTree_->regions.size()) {
            auto& region = bspTree_->regions[regionIdx];
            region->regionTypes = types;
            region->zoneLineInfo = zoneLineInfo;

            // Debug output for zone lines
            for (auto type : types) {
                if (type == RegionType::Zoneline) {
                    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x29: Region {} marked as zone line (regionTypeString='{}')",
                        regionIdx, regionTypeString);
                    if (zoneLineInfo) {
                        LOG_TRACE(MOD_MAP, "[BSP]   -> type={} zoneId={} zonePointIdx={} coords=({}, {}, {}) heading={}",
                            (zoneLineInfo->type == ZoneLineType::Absolute ? "Absolute" : "Reference"),
                            zoneLineInfo->zoneId, zoneLineInfo->zonePointIndex,
                            zoneLineInfo->x, zoneLineInfo->y, zoneLineInfo->z, zoneLineInfo->heading);
                    }
                    break;
                }
            }
        }
    }
}

std::optional<ZoneLineInfo> WldLoader::decodeZoneLineString(const std::string& regionTypeString) {
    // Decode zone line destination from region type string
    // Formats:
    //   drntp_zone -> Reference type, index 0
    //   drntp00255{index}... -> Reference type, index = slice(10,16)
    //   drntp{zoneId}{x}{y}{z}{rot} -> Absolute type
    //     zoneId: 5 digits (slice 5-10)
    //     x: 6 digits (slice 10-16)
    //     y: 6 digits (slice 16-22)
    //     z: 6 digits (slice 22-28)
    //     rot: 3 digits (slice 28-31)
    // Same patterns for wtntp and lantp

    ZoneLineInfo info;

    // Find the prefix (drntp, wtntp, or lantp)
    std::string prefix;
    if (regionTypeString.substr(0, 5) == "drntp" ||
        regionTypeString.substr(0, 5) == "wtntp" ||
        regionTypeString.substr(0, 5) == "lantp") {
        prefix = regionTypeString.substr(0, 5);
    } else {
        return std::nullopt;
    }

    // Special case: drntp_zone
    if (regionTypeString == "drntp_zone") {
        info.type = ZoneLineType::Reference;
        info.zonePointIndex = 0;
        return info;
    }

    // Need at least 10 characters for zone ID
    if (regionTypeString.length() < 10) {
        return std::nullopt;
    }

    // Parse zone ID (5 digits after prefix)
    int zoneId = 0;
    try {
        zoneId = std::stoi(regionTypeString.substr(5, 5));
    } catch (...) {
        return std::nullopt;
    }

    // Zone ID 255 means reference type
    if (zoneId == 255) {
        if (regionTypeString.length() >= 16) {
            info.type = ZoneLineType::Reference;
            try {
                info.zonePointIndex = std::stoi(regionTypeString.substr(10, 6));
            } catch (...) {
                info.zonePointIndex = 0;
            }
        }
        return info;
    }

    // Absolute type - parse coordinates
    info.type = ZoneLineType::Absolute;
    info.zoneId = static_cast<uint16_t>(zoneId);

    if (regionTypeString.length() >= 28) {
        try {
            info.x = static_cast<float>(std::stoi(regionTypeString.substr(10, 6)));
            info.y = static_cast<float>(std::stoi(regionTypeString.substr(16, 6)));
            info.z = static_cast<float>(std::stoi(regionTypeString.substr(22, 6)));
        } catch (...) {
            // Keep default 0 values
        }
    }

    if (regionTypeString.length() >= 31) {
        try {
            info.heading = static_cast<float>(std::stoi(regionTypeString.substr(28, 3)));
        } catch (...) {
            info.heading = 0.0f;
        }
    }

    return info;
}

// BspTree implementation

std::shared_ptr<BspRegion> BspTree::findRegionForPoint(float x, float y, float z) const {
    if (nodes.empty()) {
        return nullptr;
    }

    // Traverse BSP tree to find the leaf node containing this point
    int nodeIdx = 0;
    int depth = 0;
    static int debugCounter = 0;
    debugCounter++;
    bool shouldLog = (debugCounter % 50 == 0);  // Log every 50th call
    bool verboseLog = (debugCounter <= 3);  // Very verbose for first 3 calls

    while (nodeIdx >= 0 && static_cast<size_t>(nodeIdx) < nodes.size()) {
        const BspNode& node = nodes[nodeIdx];

        if (verboseLog && depth < 20) {
            LOG_TRACE(MOD_MAP, "[BSP] depth={} node={} normal=({},{},{}) dist={} regionId={} left={} right={}",
                depth, nodeIdx, node.normalX, node.normalY, node.normalZ, node.splitDistance, node.regionId, node.left, node.right);
        }

        // Check if this node has a region
        if (node.regionId > 0 && static_cast<size_t>(node.regionId - 1) < regions.size()) {
            if (GetDebugLevel() >= 2) {
                LOG_DEBUG(MOD_MAP, "[BSP TRAVERSE] Found region {} at depth {} for point ({}, {}, {})",
                    node.regionId - 1, depth, x, y, z);
            }
            return regions[node.regionId - 1];
        }

        // Determine which side of the split plane the point is on
        float dot = x * node.normalX + y * node.normalY + z * node.normalZ + node.splitDistance;

        if (verboseLog && depth < 20) {
            LOG_TRACE(MOD_MAP, "[BSP]   dot={} -> going {}", dot, (dot >= 0 ? "FRONT" : "BACK"));
        }

        // BSP convention: front_tree (left) for positive side (dot >= 0), back_tree (right) for negative side (dot < 0)
        if (dot >= 0) {
            nodeIdx = node.left;   // front_tree
        } else {
            nodeIdx = node.right;  // back_tree
        }
        depth++;
    }

    if (shouldLog && GetDebugLevel() >= 2) {
        LOG_DEBUG(MOD_MAP, "[BSP TRAVERSE] No region found for point ({}, {}, {}) after depth {} (ended at nodeIdx={})",
            x, y, z, depth, nodeIdx);
    }

    return nullptr;
}

std::optional<ZoneLineInfo> BspTree::checkZoneLine(float x, float y, float z) const {
    static int checkCounter = 0;
    checkCounter++;
    bool shouldLog = (checkCounter % 20 == 0);  // Log every 20th call

    auto region = findRegionForPoint(x, y, z);
    if (region) {
        // Debug: log region types found
        if (shouldLog && GetDebugLevel() >= 2) {
            std::string typeStr;
            for (RegionType type : region->regionTypes) {
                switch (type) {
                    case RegionType::Normal: typeStr += "Normal "; break;
                    case RegionType::Water: typeStr += "Water "; break;
                    case RegionType::Lava: typeStr += "Lava "; break;
                    case RegionType::Pvp: typeStr += "Pvp "; break;
                    case RegionType::Zoneline: typeStr += "ZONELINE "; break;
                    case RegionType::WaterBlockLOS: typeStr += "WaterBlockLOS "; break;
                    case RegionType::FreezingWater: typeStr += "FreezingWater "; break;
                    case RegionType::Slippery: typeStr += "Slippery "; break;
                    default: typeStr += "Unknown "; break;
                }
            }
            LOG_DEBUG(MOD_MAP, "[BSP CHECK] Point ({}, {}, {}) -> region with {} types: {}",
                x, y, z, region->regionTypes.size(), typeStr);
        }

        // Check if this region is a zone line
        for (RegionType type : region->regionTypes) {
            if (type == RegionType::Zoneline && region->zoneLineInfo) {
                LOG_INFO(MOD_MAP, "[BSP CHECK] ZONE LINE FOUND at ({}, {}, {})!", x, y, z);
                return region->zoneLineInfo;
            }
        }
    } else if (shouldLog && GetDebugLevel() >= 2) {
        LOG_DEBUG(MOD_MAP, "[BSP CHECK] Point ({}, {}, {}) -> NO REGION FOUND", x, y, z);
    }
    return std::nullopt;
}

} // namespace Graphics
} // namespace EQT
