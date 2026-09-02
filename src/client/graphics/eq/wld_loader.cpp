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

// XOR key for decoding WLD string hash
static const uint8_t HASH_KEY[] = { 0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A };

void decodeStringHash(char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        str[i] ^= HASH_KEY[i % 8];
    }
}

size_t WldLoader::getMemoryUsage() const {
    size_t total = sizeof(WldLoader);

    // Per-region geometries — deduplicate shared materialData
    std::set<const void*> seenMaterials;
    for (const auto& geom : geometries_) {
        if (!geom) continue;
        total += geom->getMemoryUsage();
        if (geom->materialData) {
            if (seenMaterials.insert(geom->materialData.get()).second) {
                total += geom->materialData->getMemoryUsage();
            }
        }
    }

    // Geometry-by-fragment index (just map overhead)
    total += geometryByFragIndex_.size() * (sizeof(uint32_t) + sizeof(std::shared_ptr<ZoneGeometry>) + 48);

    // Textures (WldTexture = vector of strings)
    for (const auto& [id, tex] : textures_) {
        for (const auto& f : tex.frames) total += f.capacity();
        total += tex.frames.capacity() * sizeof(std::string);
    }

    // Texture names
    for (const auto& n : textureNames_) total += n.capacity();
    total += textureNames_.capacity() * sizeof(std::string);

    // Placeables
    total += placeables_.capacity() * sizeof(std::shared_ptr<Placeable>);
    for (const auto& p : placeables_)
        if (p) total += sizeof(Placeable);

    // Object defs
    for (const auto& [name, def] : objectDefs_) {
        total += name.capacity() + sizeof(WldObjectDef) + def.name.capacity();
        total += def.meshRefs.capacity() * sizeof(uint32_t);
    }

    // Skeleton tracks
    for (const auto& [id, st] : skeletonTracks_) {
        if (!st) continue;
        total += sizeof(SkeletonTrack) + st->name.capacity();
        total += st->allBones.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
        total += st->parentIndices.capacity() * sizeof(int);
        for (const auto& bone : st->allBones) {
            if (!bone) continue;
            total += sizeof(SkeletonBone) + bone->name.capacity();
            total += bone->children.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
        }
    }

    // Bone orientations
    total += boneOrientations_.size() * (sizeof(uint32_t) + sizeof(BoneOrientation) + 48);

    // Track defs (animation keyframes - can be large)
    for (const auto& [id, td] : trackDefs_) {
        if (!td) continue;
        total += sizeof(TrackDef) + td->name.capacity();
        total += td->frames.capacity() * sizeof(BoneTransform);
    }

    // Track refs
    for (const auto& [id, tr] : trackRefs_) {
        if (!tr) continue;
        total += sizeof(TrackRef) + tr->name.capacity() + tr->animCode.capacity()
               + tr->modelCode.capacity() + tr->boneName.capacity();
    }

    // BSP tree
    if (bspTree_) {
        total += sizeof(BspTree);
        total += bspTree_->nodes.capacity() * sizeof(BspNode);
        for (const auto& region : bspTree_->regions) {
            if (!region) continue;
            total += sizeof(BspRegion);
            total += region->regionTypes.capacity() * sizeof(RegionType);
            total += region->visibleRegions.capacity() / 8;  // vector<bool>
        }
    }

    // Lights
    for (const auto& light : lights_) {
        if (!light) continue;
        total += sizeof(ZoneLight) + light->name.capacity();
        total += light->lightLevels.capacity() * sizeof(float);
        total += light->colors.capacity() * sizeof(std::tuple<float,float,float>);
    }

    // Mesh animated vertices
    for (const auto& [id, mav] : meshAnimatedVertices_) {
        if (!mav) continue;
        total += sizeof(MeshAnimatedVertices) + mav->name.capacity();
        for (const auto& f : mav->frames)
            total += f.positions.capacity() * sizeof(float);
        total += mav->frames.capacity() * sizeof(VertexAnimFrame);
    }

    // Map overhead for all the std::map containers
    total += (textures_.size() + brushes_.size() + textureRefs_.size() + materials_.size()
            + brushSets_.size() + objectDefs_.size() + modelRefs_.size() + skeletonTracks_.size()
            + skeletonRefs_.size() + boneOrientations_.size() + boneOrientationRefs_.size()
            + lightDefs_.size() + lightDefRefs_.size() + trackDefs_.size() + trackRefs_.size()
            + meshAnimatedVertices_.size() + meshAnimatedVerticesRefs_.size()) * 48;

    return total;
}

void WldLoader::logMemoryBreakdown() const {
    auto fmtBytes = [](size_t bytes) -> std::string {
        if (bytes >= 1024 * 1024)
            return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
        if (bytes >= 1024)
            return fmt::format("{:.1f} KB", bytes / 1024.0);
        return fmt::format("{} B", bytes);
    };

    // Geometry vertex/triangle data
    size_t geomVertexBytes = 0, geomTriBytes = 0, geomOtherBytes = 0;
    std::set<const void*> seenMaterials;
    size_t materialBytes = 0;
    size_t uniqueMaterialCount = 0;

    for (const auto& geom : geometries_) {
        if (!geom) continue;
        geomVertexBytes += geom->vertices.capacity() * sizeof(Vertex3D);
        geomTriBytes += geom->triangles.capacity() * sizeof(Triangle);
        geomOtherBytes += sizeof(ZoneGeometry) + geom->vertexPieces.capacity() * sizeof(VertexPiece);
        if (geom->animatedVertices) {
            geomOtherBytes += sizeof(MeshAnimatedVertices);
            for (const auto& f : geom->animatedVertices->frames)
                geomOtherBytes += f.positions.capacity() * sizeof(float);
        }
        if (geom->materialData && seenMaterials.insert(geom->materialData.get()).second) {
            materialBytes += geom->materialData->getMemoryUsage();
            uniqueMaterialCount++;
        }
    }

    // BSP tree
    size_t bspBytes = 0, pvsBytes = 0;
    size_t bspNodeCount = 0, bspRegionCount = 0;
    if (bspTree_) {
        bspNodeCount = bspTree_->nodes.size();
        bspRegionCount = bspTree_->regions.size();
        bspBytes += sizeof(BspTree) + bspTree_->nodes.capacity() * sizeof(BspNode);
        for (const auto& region : bspTree_->regions) {
            if (!region) continue;
            bspBytes += sizeof(BspRegion) + region->regionTypes.capacity() * sizeof(RegionType);
            size_t pvs = region->visibleRegions.capacity() / 8;
            pvsBytes += pvs;
            bspBytes += pvs;
        }
    }

    // Skeleton/Animation
    size_t skelBytes = 0;
    for (const auto& [id, st] : skeletonTracks_) {
        if (!st) continue;
        skelBytes += sizeof(SkeletonTrack) + st->name.capacity();
        skelBytes += st->allBones.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
        for (const auto& bone : st->allBones)
            if (bone) skelBytes += sizeof(SkeletonBone) + bone->name.capacity();
    }
    for (const auto& [id, td] : trackDefs_)
        if (td) skelBytes += sizeof(TrackDef) + td->name.capacity() + td->frames.capacity() * sizeof(BoneTransform);

    // Textures map
    size_t texMapBytes = 0;
    for (const auto& [id, tex] : textures_) {
        for (const auto& f : tex.frames) texMapBytes += f.capacity();
        texMapBytes += tex.frames.capacity() * sizeof(std::string);
    }
    for (const auto& n : textureNames_) texMapBytes += n.capacity();
    texMapBytes += textureNames_.capacity() * sizeof(std::string);

    // Lights
    size_t lightBytes = 0;
    for (const auto& light : lights_)
        if (light) lightBytes += sizeof(ZoneLight) + light->name.capacity()
            + light->lightLevels.capacity() * sizeof(float)
            + light->colors.capacity() * sizeof(std::tuple<float,float,float>);

    LOG_INFO(MOD_GRAPHICS, "[WLD Memory Breakdown]");
    LOG_INFO(MOD_GRAPHICS, "  Geometries: {} (vertices {}, triangles {}, other {}, total {})",
        geometries_.size(), fmtBytes(geomVertexBytes), fmtBytes(geomTriBytes),
        fmtBytes(geomOtherBytes), fmtBytes(geomVertexBytes + geomTriBytes + geomOtherBytes));
    LOG_INFO(MOD_GRAPHICS, "  Material data: {} ({} unique lists shared across {} geometries)",
        fmtBytes(materialBytes), uniqueMaterialCount, geometries_.size());
    LOG_INFO(MOD_GRAPHICS, "  BSP tree: {} ({} nodes, {} regions, PVS {})",
        fmtBytes(bspBytes), bspNodeCount, bspRegionCount, fmtBytes(pvsBytes));
    LOG_INFO(MOD_GRAPHICS, "  Textures map: {}", fmtBytes(texMapBytes));
    LOG_INFO(MOD_GRAPHICS, "  Skeleton/Animation: {}", fmtBytes(skelBytes));
    LOG_INFO(MOD_GRAPHICS, "  Lights: {}", fmtBytes(lightBytes));
    LOG_INFO(MOD_GRAPHICS, "  Total: {}", fmtBytes(getMemoryUsage()));
}

void WldLoader::releasePostLoadData() {
    // Release data already extracted to zone source (characters, objects, lights)
    // or only needed during initial mesh building. Preserve what the mesh cache
    // needs for progressive region rebuilds: geometries_, geometryByFragIndex_,
    // bspTree_, textures_, textureNames_.

    skeletonTracks_.clear();
    skeletonRefs_.clear();
    boneOrientations_.clear();
    boneOrientationRefs_.clear();
    trackDefs_.clear();
    trackRefs_.clear();
    placeables_.clear();
    objectDefs_.clear();
    modelRefs_.clear();
    lights_.clear();
    lightDefs_.clear();
    lightDefRefs_.clear();
    ambientLightRegions_.clear();
    globalAmbientLight_.reset();
    meshAnimatedVertices_.clear();
    meshAnimatedVerticesRefs_.clear();

    // Material resolution data — only needed during parseFragment36/2C
    brushes_.clear();
    materials_.clear();
    brushSets_.clear();
    textureRefs_.clear();
    resolvedMaterialListCache_.clear();
}

bool WldLoader::parseFromArchive(const std::string& archivePath, const std::string& wldName) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "WldLoader::parseFromArchive: failed to open archive '{}'", archivePath);
        return false;
    }
    return parseFromOpenArchive(archive, wldName);
}

bool WldLoader::parseFromOpenArchive(PfsArchive& archive, const std::string& wldName) {
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseFromOpenArchive: wld='{}'", wldName);
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
    lightDefRefs_.clear();
    lights_.clear();
    geometryByFragIndex_.clear();
    trackDefs_.clear();
    trackRefs_.clear();
    meshAnimatedVertices_.clear();
    meshAnimatedVerticesRefs_.clear();
    resolvedMaterialListCache_.clear();
    bspTree_.reset();
    totalRegionCount_ = 0;

    std::vector<char> buffer;
    if (!archive.get(wldName, buffer)) {
        LOG_WARN(MOD_GRAPHICS, "WldLoader::parseFromOpenArchive: WLD '{}' not found in archive", wldName);
        return false;
    }

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseFromArchive: WLD buffer size={} bytes", buffer.size());
    return parseWldBuffer(buffer);
}

bool WldLoader::parseWldBuffer(const std::vector<char>& buffer) {
    if (buffer.size() < sizeof(WldHeader)) {
        LOG_WARN(MOD_GRAPHICS, "WldLoader::parseWldBuffer: buffer too small ({} < {})", buffer.size(), sizeof(WldHeader));
        return false;
    }

    size_t idx = 0;
    WldHeader header = read_val<WldHeader>(&buffer[idx]);
    idx += sizeof(WldHeader);

    // Store total region count for PVS array sizing
    totalRegionCount_ = header.bspRegionCount;

    // Check magic
    if (header.magic != 0x54503d02) {
        LOG_WARN(MOD_GRAPHICS, "WldLoader::parseWldBuffer: bad magic 0x{:08x}", header.magic);
        return false;
    }

    // Determine if old format
    bool oldFormat = (header.version == 0x00015500);
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: version=0x{:x} oldFormat={} fragmentCount={} bspRegionCount={} hashLength={}",
              header.version, oldFormat, header.fragmentCount, header.bspRegionCount, header.hashLength);

    // Read and decode string hash
    if (idx + header.hashLength > buffer.size()) {
        LOG_WARN(MOD_GRAPHICS, "WldLoader::parseWldBuffer: hash overflows buffer");
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
            LOG_WARN(MOD_GRAPHICS, "WldLoader::parseWldBuffer: fragment {} header overflows buffer at offset {}", i, fragIdx);
            break;
        }

        WldFragmentHeader fragHeader = read_val<WldFragmentHeader>(&buffer[fragIdx]);
        fragmentOffsets.push_back({fragIdx + sizeof(WldFragmentHeader), fragHeader});
        fragIdx += sizeof(WldFragmentHeader) + fragHeader.size;
    }
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: collected {} fragment offsets", fragmentOffsets.size());

    // Count fragment types for summary
    std::map<uint32_t, uint32_t> fragTypeCounts;

    // Parse all fragments
    // According to libeq/LanternExtractor, ALL fragment data starts with nameRef (4 bytes),
    // then fragment-specific data. We extract nameRef here and pass data+4 to parsers.
    for (uint32_t i = 0; i < fragmentOffsets.size(); ++i) {
        const auto& [dataOffset, fragHeader] = fragmentOffsets[i];
        const char* fragData = &buffer[dataOffset];
        uint32_t fragSize = fragHeader.size;
        fragTypeCounts[fragHeader.id]++;

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
            case 0x1C:
                parseFragment1C(fragBody, fragBodySize, i + 1);
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

    // Log fragment type summary
    for (const auto& [fragType, count] : fragTypeCounts) {
        LOG_TRACE(MOD_GRAPHICS, "WldLoader::parseWldBuffer: fragment type 0x{:02x} count={}", fragType, count);
    }
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: results — geometries={} geometryByFragIndex={} textures_={} brushes_={} textureRefs_={} materials_={} brushSets_={} resolvedMaterialListCache_={} skeletonTracks={} modelRefs={} placeables={} objectDefs={} lights={}",
              geometries_.size(), geometryByFragIndex_.size(), textures_.size(), brushes_.size(), textureRefs_.size(),
              materials_.size(), brushSets_.size(), resolvedMaterialListCache_.size(),
              skeletonTracks_.size(), modelRefs_.size(), placeables_.size(), objectDefs_.size(), lights_.size());

    bool result = !geometries_.empty() || !placeables_.empty() || !objectDefs_.empty() || !skeletonTracks_.empty() || !lights_.empty();
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: returning {}", result);
    return result;
}


// BspTree implementation

// D20f1: BspTree method implementations moved to src/client/zone_bsp.cpp

} // namespace Graphics
} // namespace EQT

#if 0  // D20f1: removed — implementations now in src/client/zone_bsp.cpp
namespace removed {
BspBounds BspTree::clipBoundsByPlane_REMOVED(const BspBounds& bounds,
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

size_t BspTree::findRegionIndexForPoint(float x, float y, float z) const {
    if (nodes.empty()) return SIZE_MAX;

    int nodeIdx = 0;

    while (nodeIdx >= 0 && static_cast<size_t>(nodeIdx) < nodes.size()) {
        const BspNode& node = nodes[nodeIdx];

        // Check if this node has a region
        if (node.regionId > 0 && static_cast<size_t>(node.regionId - 1) < regions.size()) {
            return static_cast<size_t>(node.regionId - 1);
        }

        // Determine which side of the split plane the point is on
        float dot = x * node.normalX + y * node.normalY + z * node.normalZ + node.splitDistance;

        if (dot >= 0) {
            nodeIdx = node.left;   // front_tree
        } else {
            nodeIdx = node.right;  // back_tree
        }
    }

    return SIZE_MAX;
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
} // removed
#endif // D20f1 removed code
