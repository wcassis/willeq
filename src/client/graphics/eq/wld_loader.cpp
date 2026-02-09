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

// Safe unaligned read helper - uses memcpy to avoid bus errors on ARM
template<typename T>
static inline T read_val(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}

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
    totalRegionCount_ = 0;

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
    WldHeader header = read_val<WldHeader>(&buffer[idx]);
    idx += sizeof(WldHeader);

    // Store total region count for PVS array sizing
    totalRegionCount_ = header.bspRegionCount;

    // Check magic
    if (header.magic != 0x54503d02) {
        return false;
    }

    // Determine if old format
    bool oldFormat = (header.version == 0x00015500);

    // Read and decode string hash
    if (idx + header.hashLength > buffer.size()) {
        return false;
    }

    std::vector<char> hashBuffer(header.hashLength);
    std::memcpy(hashBuffer.data(), &buffer[idx], header.hashLength);
    decodeStringHash(hashBuffer.data(), header.hashLength);
    idx += header.hashLength;

    // Parse fragments
    std::vector<std::pair<size_t, WldFragmentHeader>> fragmentOffsets;
    size_t fragIdx = idx;
    for (uint32_t i = 0; i < header.fragmentCount; ++i) {
        if (fragIdx + sizeof(WldFragmentHeader) > buffer.size()) {
            break;
        }

        WldFragmentHeader fragHeader = read_val<WldFragmentHeader>(&buffer[fragIdx]);
        fragmentOffsets.push_back({fragIdx + sizeof(WldFragmentHeader), fragHeader});
        fragIdx += sizeof(WldFragmentHeader) + fragHeader.size;
    }

    // Parse all fragments
    // According to libeq/LanternExtractor, ALL fragment data starts with nameRef (4 bytes),
    // then fragment-specific data. We extract nameRef here and pass data+4 to parsers.
    for (uint32_t i = 0; i < fragmentOffsets.size(); ++i) {
        const auto& [dataOffset, fragHeader] = fragmentOffsets[i];
        const char* fragData = &buffer[dataOffset];
        uint32_t fragSize = fragHeader.size;

        // All fragments start with nameRef (4 bytes, signed int32)
        if (fragSize < 4) continue;
        int32_t nameRef = read_val<int32_t>(fragData);
        const char* fragBody = fragData + 4;  // Skip nameRef
        uint32_t fragBodySize = fragSize - 4;

        switch (fragHeader.id) {
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
                parseFragment15(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data(), oldFormat);
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
            case 0x2A:
                parseFragment2A(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            case 0x35:
                parseFragment35(fragBody, fragBodySize, i + 1);
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
    WldFragment03Header header = read_val<WldFragment03Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment03Header);

    uint32_t count = header.textureCount;
    if (count == 0) count = 1;

    WldTexture tex;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = read_val<uint16_t>(ptr);
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
    WldFragment04Header header = read_val<WldFragment04Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment04Header);

    WldTextureBrush brush;
    brush.flags = header.flags;

    // Check for animated texture (bit 3 set)
    brush.isAnimated = (header.flags & (1 << 3)) != 0;

    // Skip unknown field if bit 2 is set
    if (header.flags & (1 << 2)) {
        ptr += sizeof(uint32_t);
    }

    // Read animation delay if bit 3 is set (animated texture)
    if (brush.isAnimated) {
        brush.animationDelayMs = read_val<int32_t>(ptr);
        ptr += sizeof(int32_t);
    }

    uint32_t count = header.textureCount;
    if (count == 0) count = 1;

    for (uint32_t i = 0; i < count; ++i) {
        WldFragmentRef ref = read_val<WldFragmentRef>(ptr);
        ptr += sizeof(WldFragmentRef);

        if (ref.id > 0) {
            brush.textureRefs.push_back(ref.id);
        }
    }

    brushes_[fragIndex] = brush;
}

void WldLoader::parseFragment05(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragmentRef ref = read_val<WldFragmentRef>(fragBuffer);
    if (ref.id > 0) {
        textureRefs_[fragIndex] = ref.id;
    }
}

void WldLoader::parseFragment30(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    // Fragment 0x30 structure matches eqsage: sage/lib/s3d/materials/material.js
    WldFragment30Header header = read_val<WldFragment30Header>(fragBuffer);

    WldTextureBrush material;

    // Extract material type from parameters (matches eqsage's materialType masking)
    uint32_t materialType = header.parameters & ~0x80000000;

    if (materialType == 0 || header.bitmapInfoRef == 0) {
        // Boundary or invisible material
        material.flags = 1;
    } else {
        // Resolve texture reference chain: 0x30 -> 0x05 -> 0x04
        int32_t frag05Ref = header.bitmapInfoRef;
        auto it05 = textureRefs_.find(frag05Ref);
        if (it05 != textureRefs_.end()) {
            material.textureRefs.push_back(it05->second);
        }
        material.flags = 0;
    }

    materials_[fragIndex] = material;
}

void WldLoader::parseFragment31(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragment31Header header = read_val<WldFragment31Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment31Header);

    WldTextureBrushSet brushSet;
    for (uint32_t i = 0; i < header.count; ++i) {
        uint32_t refId = read_val<uint32_t>(ptr);
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

    // Skip colors (RGBA packed as uint32_t)
    ptr += sizeof(uint32_t) * header.colorCount;

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

    // Resolve texture names
    if (header.frag1 > 0) {
        auto it31 = brushSets_.find(header.frag1);
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
    if (header.frag2 > 0) {
        uint32_t animVertRefFragIdx = header.frag2;
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

    // Resolve texture names - try fragment reference from flags
    // Legacy mesh may store texture brush set reference differently
    uint32_t texBrushSetRef = header.flags & 0xFFFF;  // Lower 16 bits might be the reference
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
    WldFragment2DHeader header = read_val<WldFragment2DHeader>(fragBuffer);

    WldModelRef modelRef;
    if (header.ref > 0) {
        modelRef.geometryFragRef = header.ref;
    } else {
        modelRef.geometryFragRef = 0;
    }

    modelRefs_[fragIndex] = modelRef;
}

void WldLoader::parseFragment10(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat,
                                const std::vector<std::pair<size_t, WldFragmentHeader>>& fragments,
                                const std::vector<char>& buffer) {
    WldFragment10Header header = read_val<WldFragment10Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment10Header);

    auto track = std::make_shared<SkeletonTrack>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        // The hash can be much larger than 65536 for big archives like global_chr.s3d
        if (nameOffset > 0) {
            track->name = std::string(&hash[nameOffset]);
        }
    }

    if (header.flags & 1) ptr += sizeof(int32_t) * 3;
    if (header.flags & 2) ptr += sizeof(float);

    std::vector<std::shared_ptr<SkeletonBone>> allBones;
    allBones.reserve(header.trackRefCount);

    // Collect tree relationships: (parent_bone_index, child_bone_index)
    std::vector<std::pair<int, int>> treeRelations;

    for (uint32_t i = 0; i < header.trackRefCount; ++i) {
        if (ptr + sizeof(WldFragment10BoneEntry) > fragBuffer + fragLength) break;

        WldFragment10BoneEntry entry = read_val<WldFragment10BoneEntry>(ptr);
        ptr += sizeof(WldFragment10BoneEntry);

        auto bone = std::make_shared<SkeletonBone>();

        if (entry.nameRef < 0) {
            int32_t nameOffset = -entry.nameRef;
            if (nameOffset > 0 ) {
                bone->name = std::string(&hash[nameOffset]);
            }
        }

        if (entry.orientationRef > 0) {
            auto it13 = boneOrientationRefs_.find(entry.orientationRef);
            if (it13 != boneOrientationRefs_.end()) {
                auto it12 = boneOrientations_.find(it13->second);
                if (it12 != boneOrientations_.end()) {
                    bone->orientation = it12->second;
                }
            }
        }

        if (entry.modelRef > 0) {
            bone->modelRef = entry.modelRef;
        }

        allBones.push_back(bone);

        // Read child indices - store for later processing
        for (uint32_t j = 0; j < entry.childCount; ++j) {
            if (ptr + sizeof(int32_t) > fragBuffer + fragLength) break;
            int32_t childIdx = read_val<int32_t>(ptr);
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
    WldFragment11Header header = read_val<WldFragment11Header>(fragBuffer);
    if (header.ref > 0) {
        skeletonRefs_[fragIndex] = header.ref;
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
    uint32_t flags = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);

    // Read frame count
    int32_t frameCount = read_val<int32_t>(ptr);
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

        int16_t rotDenom = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotX = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotY = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotZ = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftX = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftY = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftZ = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftDenom = read_val<int16_t>(ptr); ptr += 2;

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

    int32_t trackDefRef = read_val<int32_t>(ptr);
    ptr += sizeof(int32_t);

    int32_t flags = read_val<int32_t>(ptr);
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
        trackRef->frameMs = read_val<int32_t>(ptr);
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
    // Fragment 0x1B - Light Source Definition
    // Matches eqsage: sage/lib/s3d/lights/light.js LightSource
    // Variable-length structure with conditional fields based on flags

    if (fragLength < 8) {
        return;  // Need at least flags + frameCount
    }

    const char* ptr = fragBuffer;

    auto light = std::make_shared<ZoneLight>();

    // Get name from hash table
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            light->name = std::string(&hash[nameOffset]);
        }
    }

    // Initialize position (set by Fragment 0x28)
    light->x = 0.0f;
    light->y = 0.0f;
    light->z = 0.0f;
    light->radius = 0.0f;

    // Read flags and frameCount
    light->flags = read_val<uint32_t>(ptr);
    ptr += 4;
    light->frameCount = read_val<uint32_t>(ptr);
    ptr += 4;

    // Read optional currentFrame (if flags & 0x01)
    if (light->flags & LIGHT_FLAG_HAS_CURRENT_FRAME) {
        light->currentFrame = read_val<uint32_t>(ptr);
        ptr += 4;
    }

    // Read optional sleep (if flags & 0x02)
    if (light->flags & LIGHT_FLAG_HAS_SLEEP) {
        light->sleepMs = read_val<uint32_t>(ptr);
        ptr += 4;
    }

    // Read optional light levels (if flags & 0x04)
    if (light->flags & LIGHT_FLAG_HAS_LIGHT_LEVELS) {
        light->lightLevels.reserve(light->frameCount);
        for (uint32_t i = 0; i < light->frameCount; ++i) {
            float level = read_val<float>(ptr);
            light->lightLevels.push_back(level);
            ptr += 4;
        }
    }

    // Read optional colors (if flags & 0x10)
    // This is the CORRECT flag (0x10 = HasColor), NOT 0x08 (SkipFrames)
    if (light->flags & LIGHT_FLAG_HAS_COLOR) {
        light->colors.reserve(light->frameCount);
        for (uint32_t i = 0; i < light->frameCount; ++i) {
            float r = read_val<float>(ptr);
            ptr += 4;
            float g = read_val<float>(ptr);
            ptr += 4;
            float b = read_val<float>(ptr);
            ptr += 4;
            light->colors.push_back({r, g, b});
        }

        // Set primary color from first frame
        if (!light->colors.empty()) {
            auto& [r, g, b] = light->colors[0];
            light->r = r;
            light->g = g;
            light->b = b;
        } else {
            light->r = 1.0f;
            light->g = 1.0f;
            light->b = 1.0f;
        }
    } else {
        // No color data - default to white
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
    }

    lightDefs_[fragIndex] = light;
}

void WldLoader::parseFragment28(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x28 - Light Instance
    // Matches eqsage: sage/lib/s3d/lights/light.js LightInstance
    // Places a light source at a position with a radius

    WldFragmentRef ref = read_val<WldFragmentRef>(fragBuffer);
    WldFragment28Header header = read_val<WldFragment28Header>(
        fragBuffer + sizeof(WldFragmentRef));

    uint32_t lightDefIndex = ref.id;
    if (lightDefIndex == 0) {
        return;
    }

    auto it = lightDefs_.find(lightDefIndex);
    if (it == lightDefs_.end()) {
        // No light definition found - create basic white light
        auto light = std::make_shared<ZoneLight>();
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
        light->x = header.x;
        light->y = header.y;
        light->z = header.z;
        light->radius = header.radius;
        lights_.push_back(light);
        return;
    }

    // Copy light definition and add position/radius
    auto light = std::make_shared<ZoneLight>();
    light->name = it->second->name;
    light->r = it->second->r;
    light->g = it->second->g;
    light->b = it->second->b;
    light->x = header.x;
    light->y = header.y;
    light->z = header.z;
    light->radius = header.radius;

    // Copy animation data
    light->flags = it->second->flags;
    light->frameCount = it->second->frameCount;
    light->currentFrame = it->second->currentFrame;
    light->sleepMs = it->second->sleepMs;
    light->lightLevels = it->second->lightLevels;
    light->colors = it->second->colors;

    lights_.push_back(light);
}

void WldLoader::parseFragment2A(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x2A - Ambient Light Region
    // Matches eqsage: sage/lib/s3d/lights/light.js AmbientLight
    // Defines regions where ambient light applies

    if (fragLength < 8) {
        return;  // Need at least flags + regionCount
    }

    auto ambient = std::make_shared<AmbientLightRegion>();

    // Get name from hash table
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            ambient->name = std::string(&hash[nameOffset]);
        }
    }

    const char* ptr = fragBuffer;

    // Read flags and regionCount
    ambient->flags = read_val<uint32_t>(ptr);
    ptr += 4;
    uint32_t regionCount = read_val<uint32_t>(ptr);
    ptr += 4;

    // Read region references
    ambient->regionRefs.reserve(regionCount);
    for (uint32_t i = 0; i < regionCount; ++i) {
        int32_t regionRef = read_val<int32_t>(ptr);
        ambient->regionRefs.push_back(regionRef);
        ptr += 4;
    }

    ambientLightRegions_.push_back(ambient);
}

void WldLoader::parseFragment35(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x35 - Global Ambient Light
    // Matches eqsage: sage/lib/s3d/lights/light.js GlobalAmbientLight
    // Defines zone-wide ambient color

    if (fragLength < 4) {
        return;  // Need 4 bytes (BGRA)
    }

    auto ambient = std::make_shared<GlobalAmbientLight>();

    // eqsage reads in BGRA order
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(fragBuffer);
    uint8_t blue = bytes[0];
    uint8_t green = bytes[1];
    uint8_t red = bytes[2];
    uint8_t alpha = bytes[3];

    // Normalize to 0.0-1.0 range
    ambient->r = static_cast<float>(red) / 255.0f;
    ambient->g = static_cast<float>(green) / 255.0f;
    ambient->b = static_cast<float>(blue) / 255.0f;
    ambient->a = static_cast<float>(alpha) / 255.0f;

    globalAmbientLight_ = ambient;
}

void WldLoader::parseFragment2F(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x2F - Mesh Animated Vertices Reference
    // References a 0x37 fragment containing animated vertex frames
    if (fragLength < 8) {  // Minimum: ref + flags
        return;
    }

    // Read the fragment reference (to 0x37)
    const char* ptr = fragBuffer;
    int32_t meshAnimVertRef = read_val<int32_t>(ptr);

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
    uint32_t flags = read_val<uint32_t>(ptr); ptr += 4;
    uint16_t vertexCount = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t frameCount = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t delayMs = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t param2 = read_val<uint16_t>(ptr); ptr += 2;
    int16_t scaleVal = read_val<int16_t>(ptr); ptr += 2;

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
            int16_t vd0 = read_val<int16_t>(ptr);
            int16_t vd1 = read_val<int16_t>(ptr + 2);
            int16_t vd2 = read_val<int16_t>(ptr + 4);
            ptr += 6;  // 3 x int16_t

            // Store raw EQ coordinates (transform applied at render time if needed)
            animVerts->frames[f].positions[v * 3 + 0] = vd0 * scale;
            animVerts->frames[f].positions[v * 3 + 1] = vd1 * scale;
            animVerts->frames[f].positions[v * 3 + 2] = vd2 * scale;
        }
    }

    meshAnimatedVertices_[fragIndex] = animVerts;

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader: Parsed fragment 0x37 '{}' with {} frames, {} vertices, {}ms delay",
        animVerts->name, frameCount, vertexCount, animVerts->delayMs);
}

// Decode RLE-encoded PVS (Potentially Visible Set) data from Fragment 0x22
// RLE format from libeq documentation:
// - 0x00..0x3E: skip forward by this many region IDs (not visible)
// - 0x3F, WORD: skip forward by the amount in the following 16-bit WORD
// - 0x40..0x7F: skip forward based on bits 3..5, then include IDs based on bits 0..2
// - 0x80..0xBF: include IDs based on bits 3..5, then skip forward based on bits 0..2
// - 0xC0..0xFE: (byte - 0xC0) region IDs are nearby (visible)
// - 0xFF, WORD: the number of region IDs given by the following WORD are nearby
static std::vector<bool> decodeRlePvs(const char* data, int16_t dataSize, uint32_t numRegions) {
    std::vector<bool> visible(numRegions, false);
    uint32_t region = 0;
    int i = 0;

    while (i < dataSize && region < numRegions) {
        uint8_t byte = static_cast<uint8_t>(data[i++]);

        if (byte <= 0x3E) {
            // 0x00..0x3E - skip forward by this many region IDs (not visible)
            region += byte;
        } else if (byte == 0x3F) {
            // 0x3F, WORD - skip forward by the amount given in the following 16-bit WORD
            if (i + 2 <= dataSize) {
                uint16_t skipCount = read_val<uint16_t>(&data[i]);
                i += 2;
                region += skipCount;
            } else {
                break; // Malformed data
            }
        } else if (byte <= 0x7F) {
            // 0x40..0x7F - skip forward based on bits 3..5, then include IDs based on bits 0..2
            uint8_t val = byte - 0x40;
            uint8_t skipCount = (val >> 3) & 0x07;   // bits 3..5
            uint8_t includeCount = val & 0x07;        // bits 0..2
            region += skipCount;
            for (uint8_t j = 0; j < includeCount && region < numRegions; j++) {
                visible[region++] = true;
            }
        } else if (byte <= 0xBF) {
            // 0x80..0xBF - include IDs based on bits 3..5, then skip forward based on bits 0..2
            uint8_t val = byte - 0x80;
            uint8_t includeCount = (val >> 3) & 0x07;  // bits 3..5
            uint8_t skipCount = val & 0x07;            // bits 0..2
            for (uint8_t j = 0; j < includeCount && region < numRegions; j++) {
                visible[region++] = true;
            }
            region += skipCount;
        } else if (byte <= 0xFE) {
            // 0xC0..0xFE - (byte - 0xC0) region IDs are nearby (visible)
            uint8_t includeCount = byte - 0xC0;
            for (uint8_t j = 0; j < includeCount && region < numRegions; j++) {
                visible[region++] = true;
            }
        } else { // byte == 0xFF
            // 0xFF, WORD - the number of region IDs given by the following WORD are nearby
            if (i + 2 <= dataSize) {
                uint16_t includeCount = read_val<uint16_t>(&data[i]);
                i += 2;
                for (uint16_t j = 0; j < includeCount && region < numRegions; j++) {
                    visible[region++] = true;
                }
            } else {
                break; // Malformed data
            }
        }
    }

    return visible;
}

void WldLoader::parseFragment21(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x21 - BSP Tree
    // Contains the BSP tree nodes for zone region detection
    if (fragLength < 4) {
        return;
    }

    const char* ptr = fragBuffer;
    uint32_t nodeCount = read_val<uint32_t>(ptr);
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
        node.normalX = read_val<float>(ptr); ptr += 4;
        node.normalY = read_val<float>(ptr); ptr += 4;
        node.normalZ = read_val<float>(ptr); ptr += 4;
        node.splitDistance = read_val<float>(ptr); ptr += 4;
        node.regionId = read_val<int32_t>(ptr); ptr += 4;
        // Left/right are 1-indexed in file, convert to 0-indexed (-1 for no child)
        int32_t leftRaw = read_val<int32_t>(ptr); ptr += 4;
        int32_t rightRaw = read_val<int32_t>(ptr); ptr += 4;
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

    int32_t flags = read_val<int32_t>(ptr); ptr += 4;
    region->containsPolygons = (flags == 0x181);

    // Skip unknown1 (always 0)
    ptr += 4;

    int32_t data1Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data2Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip unknown2 (always 0)
    ptr += 4;

    int32_t data3Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data4Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip unknown3 (always 0)
    ptr += 4;

    int32_t data5Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data6Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip data1 and data2 (12 bytes each entry)
    ptr += 12 * data1Size + 12 * data2Size;

    // Skip data3 (variable size per entry)
    for (int32_t i = 0; i < data3Size; ++i) {
        if (ptr + 8 > fragBuffer + fragLength) break;
        int32_t data3Flags = read_val<int32_t>(ptr); ptr += 4;
        int32_t data3Size2 = read_val<int32_t>(ptr); ptr += 4;
        ptr += data3Size2 * 4;
    }

    // Skip data4 (no data per entry in known files)

    // Skip data5 (28 bytes each entry)
    ptr += 7 * 4 * data5Size;

    // Decode PVS (Potentially Visible Set) data
    // This RLE-encoded bitfield indicates which other regions are visible from this region
    if (ptr + 2 <= fragBuffer + fragLength) {
        int16_t pvsSize = read_val<int16_t>(ptr); ptr += 2;
        if (pvsSize > 0 && totalRegionCount_ > 0 && ptr + pvsSize <= fragBuffer + fragLength) {
            region->visibleRegions = decodeRlePvs(ptr, pvsSize, totalRegionCount_);
            // Count visible regions for logging
            size_t visibleCount = 0;
            for (bool v : region->visibleRegions) {
                if (v) visibleCount++;
            }
            LOG_TRACE(MOD_MAP, "[BSP] Region {} PVS: {} bytes decoded, {}/{} regions visible",
                regionIndex, pvsSize, visibleCount, totalRegionCount_);
        }
        ptr += pvsSize;
    }

    // Skip final unknown data
    if (ptr + 4 <= fragBuffer + fragLength) {
        ptr += 4;  // bytes
        ptr += 16; // unknown padding
    }

    // Read mesh reference if region contains polygons
    if (region->containsPolygons && ptr + 4 <= fragBuffer + fragLength) {
        region->meshReference = read_val<int32_t>(ptr);
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

    int32_t flags = read_val<int32_t>(ptr); ptr += 4;
    int32_t regionCount = read_val<int32_t>(ptr); ptr += 4;

    // Read region indices and link this type to them
    std::vector<int32_t> regionIndices;
    for (int32_t i = 0; i < regionCount; ++i) {
        if (ptr + 4 > fragBuffer + fragLength) break;
        int32_t regionIdx = read_val<int32_t>(ptr); ptr += 4;
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

BspBounds BspTree::clipBoundsByPlane(const BspBounds& bounds,
                                      float nx, float ny, float nz, float dist,
                                      bool frontSide) {
    if (!bounds.valid) return bounds;

    // For each corner of the AABB, compute which side of the plane it's on
    // Then find the extent of corners on the desired side
    float corners[8][3] = {
        {bounds.minX, bounds.minY, bounds.minZ},
        {bounds.maxX, bounds.minY, bounds.minZ},
        {bounds.minX, bounds.maxY, bounds.minZ},
        {bounds.maxX, bounds.maxY, bounds.minZ},
        {bounds.minX, bounds.minY, bounds.maxZ},
        {bounds.maxX, bounds.minY, bounds.maxZ},
        {bounds.minX, bounds.maxY, bounds.maxZ},
        {bounds.maxX, bounds.maxY, bounds.maxZ}
    };

    // Check which corners are on the desired side
    bool anyOnSide = false;
    bool allOnSide = true;
    for (int i = 0; i < 8; ++i) {
        float dot = corners[i][0] * nx + corners[i][1] * ny + corners[i][2] * nz + dist;
        bool onFront = (dot >= 0);
        bool onDesiredSide = (frontSide == onFront);
        if (onDesiredSide) anyOnSide = true;
        else allOnSide = false;
    }

    // If no corners on desired side, the bounds are empty
    if (!anyOnSide) {
        return BspBounds();
    }

    // If all corners on desired side, return original bounds
    if (allOnSide) {
        return bounds;
    }

    // Plane intersects the AABB - compute clipped bounds
    // For each axis, find where the plane constrains the bounds
    BspBounds result = bounds;

    // For axis-aligned clipping approximation:
    // If normal has significant component in an axis, adjust that axis's bounds
    const float EPSILON = 0.001f;

    if (std::abs(nx) > EPSILON) {
        // Plane has X component: x = -(ny*y + nz*z + dist) / nx
        // Find the X value at the center of YZ bounds
        float centerY = (bounds.minY + bounds.maxY) / 2.0f;
        float centerZ = (bounds.minZ + bounds.maxZ) / 2.0f;
        float xAtPlane = -(ny * centerY + nz * centerZ + dist) / nx;

        if (xAtPlane > bounds.minX && xAtPlane < bounds.maxX) {
            if ((nx > 0) == frontSide) {
                // Front side means X >= xAtPlane (approximately)
                result.minX = std::max(result.minX, xAtPlane);
            } else {
                // Back side means X < xAtPlane (approximately)
                result.maxX = std::min(result.maxX, xAtPlane);
            }
        }
    }

    if (std::abs(ny) > EPSILON) {
        float centerX = (bounds.minX + bounds.maxX) / 2.0f;
        float centerZ = (bounds.minZ + bounds.maxZ) / 2.0f;
        float yAtPlane = -(nx * centerX + nz * centerZ + dist) / ny;

        if (yAtPlane > bounds.minY && yAtPlane < bounds.maxY) {
            if ((ny > 0) == frontSide) {
                result.minY = std::max(result.minY, yAtPlane);
            } else {
                result.maxY = std::min(result.maxY, yAtPlane);
            }
        }
    }

    if (std::abs(nz) > EPSILON) {
        float centerX = (bounds.minX + bounds.maxX) / 2.0f;
        float centerY = (bounds.minY + bounds.maxY) / 2.0f;
        float zAtPlane = -(nx * centerX + ny * centerY + dist) / nz;

        if (zAtPlane > bounds.minZ && zAtPlane < bounds.maxZ) {
            if ((nz > 0) == frontSide) {
                result.minZ = std::max(result.minZ, zAtPlane);
            } else {
                result.maxZ = std::min(result.maxZ, zAtPlane);
            }
        }
    }

    // Validate result
    if (result.minX >= result.maxX || result.minY >= result.maxY || result.minZ >= result.maxZ) {
        return BspBounds();
    }

    return result;
}

BspBounds BspTree::computeRegionBoundsRecursive(int nodeIdx, size_t targetRegionIndex,
                                                 const BspBounds& currentBounds) const {
    // Base case: invalid node or empty bounds
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size() || !currentBounds.valid) {
        return BspBounds();
    }

    const BspNode& node = nodes[nodeIdx];

    // Check if this node has our target region
    if (node.regionId > 0) {
        size_t thisRegionIndex = static_cast<size_t>(node.regionId - 1);
        if (thisRegionIndex == targetRegionIndex) {
            // Found it! Return current bounds
            return currentBounds;
        }
        // This is a different region, not what we're looking for
        return BspBounds();
    }

    // Interior node - need to traverse children
    BspBounds result;

    // Clip bounds for left child (front side: dot >= 0)
    if (node.left >= 0) {
        BspBounds leftBounds = clipBoundsByPlane(currentBounds,
            node.normalX, node.normalY, node.normalZ, node.splitDistance, true);
        if (leftBounds.valid) {
            BspBounds leftResult = computeRegionBoundsRecursive(node.left, targetRegionIndex, leftBounds);
            result.merge(leftResult);
        }
    }

    // Clip bounds for right child (back side: dot < 0)
    if (node.right >= 0) {
        BspBounds rightBounds = clipBoundsByPlane(currentBounds,
            node.normalX, node.normalY, node.normalZ, node.splitDistance, false);
        if (rightBounds.valid) {
            BspBounds rightResult = computeRegionBoundsRecursive(node.right, targetRegionIndex, rightBounds);
            result.merge(rightResult);
        }
    }

    return result;
}

BspBounds BspTree::computeRegionBounds(size_t regionIndex, const BspBounds& initialBounds) const {
    if (nodes.empty() || regionIndex >= regions.size()) {
        return BspBounds();
    }

    return computeRegionBoundsRecursive(0, regionIndex, initialBounds);
}

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
