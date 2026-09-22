#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <cstring>
#include <cctype>
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
        }
        // T490: the RLE arena (every region's PVS, back to back) and the
        // bounded decode cache -- not regions^2/8.
        total += bspTree_->pvsArenaBytes() + bspTree_->pvsCacheBytes();
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
        }
        pvsBytes = bspTree_->pvsArenaBytes() + bspTree_->pvsCacheBytes();
        bspBytes += pvsBytes;
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

size_t WldLoader::releaseRegionGeometry() {
    size_t freed = 0;
    for (const auto& g : geometries_) {
        // a geometry another owner still holds is not freed by dropping ours
        if (g && g.use_count() <= 2) freed += g->getFullMemoryUsage();
    }
    std::vector<std::shared_ptr<ZoneGeometry>>().swap(geometries_);
    std::map<uint32_t, std::shared_ptr<ZoneGeometry>>().swap(geometryByFragIndex_);
    return freed;
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

// T504c Leg 2: the same rule parseFragment13 applies to a parsed name
// ({ANIM}{MODEL}{BONE}_TRACK, or {MODEL}{BONE}_TRACK for a pose track),
// applied to the hash string BEFORE the body is read so an unwanted track
// costs nothing. 0x12 names end in _TRACKDEF, 0x13 in _TRACK; find("_TRACK")
// strips both.
std::string WldLoader::trackModelCode(const std::string& name) {
    std::string cleaned = name;
    const size_t trackPos = cleaned.find("_TRACK");
    if (trackPos != std::string::npos) cleaned = cleaned.substr(0, trackPos);
    std::string code;
    if (cleaned.length() >= 6 && std::isalpha(static_cast<unsigned char>(cleaned[0]))
        && std::isdigit(static_cast<unsigned char>(cleaned[1]))
        && std::isdigit(static_cast<unsigned char>(cleaned[2]))) {
        code = cleaned.substr(3, 3);
    } else if (cleaned.length() >= 4) {
        code = cleaned.substr(0, 3);
    }
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char ch) { return std::toupper(ch); });
    return code;
}

bool WldLoader::trackNameWanted(int32_t nameRef, const char* hash) const {
    if (codeFilter_.empty()) return true;
    if (nameRef >= 0) return true;                 // no name: keep (STOP 2 rule: never guess)
    const std::string code = trackModelCode(std::string(&hash[-nameRef]));
    if (code.empty()) return true;                 // no code: keep
    for (const auto& want : codeFilter_)
        if (want == code) return true;
    return false;
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
    // T504c Leg 2g: the PVS arena's reserve -- the exact RLE byte total of
    // the 0x22 regions, so the arena is allocated once at its final size.
    pvsArenaReserveHint_ = 0;
    for (const auto& [off, fh] : fragmentOffsets)
        if (fh.id == 0x22 && fh.size >= 4 && off + fh.size <= buffer.size())
            pvsArenaReserveHint_ += fragment22PvsBytes(&buffer[off] + 4, fh.size - 4);
    std::map<uint32_t, uint32_t> fragTypeSkipped;   // T504c Leg 2: bodies the code filter skipped

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
                if (!trackNameWanted(nameRef, hashBuffer.data())) { fragTypeSkipped[0x12]++; break; }
                parseFragment12(fragBody, fragBodySize, i + 1, nameRef, hashBuffer.data());
                break;
            case 0x13:
                if (!trackNameWanted(nameRef, hashBuffer.data())) { fragTypeSkipped[0x13]++; break; }
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
    {
        // T504c Leg 1: the character-relevant fragment counts of every parse
        // and the keyframes the 0x12 track definitions hold -- the number a
        // fragment-level filter is held to.
        size_t frames = 0;
        for (const auto& [idx, td] : trackDefs_) if (td) frames += td->frames.size();
        auto c = [&](uint32_t t) { auto it = fragTypeCounts.find(t); return it == fragTypeCounts.end() ? 0u : it->second; };
        auto sk = [&](uint32_t t) { auto it = fragTypeSkipped.find(t); return it == fragTypeSkipped.end() ? 0u : it->second; };
        // T504c Leg 2 (STOP 2 check): a KEPT track reference whose track
        // definition was skipped would be a filter error -- the 0x13 and its
        // 0x12 carry the same model code, so this must stay 0 with or without
        // a filter. Counted here so a run can be held to it mechanically.
        size_t dangling13 = 0;
        for (const auto& [idx, tr] : trackRefs_)
            if (tr && tr->trackDefRef != 0 && trackDefs_.find(tr->trackDefRef) == trackDefs_.end()) ++dangling13;
        LOG_INFO(MOD_GRAPHICS, "[wldparse] bytes={} fragments={} skel10={} track12={} trackref13={} actor14={} mesh36={} keyframes={} trackdef_bytes={} filter={} skipped12={} skipped13={} dangling13={}",
                 buffer.size(), fragmentOffsets.size(), c(0x10), c(0x12), c(0x13), c(0x14), c(0x36), frames,
                 frames * sizeof(BoneTransform), codeFilter_.size(), sk(0x12), sk(0x13), dangling13);
    }
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: results — geometries={} geometryByFragIndex={} textures_={} brushes_={} textureRefs_={} materials_={} brushSets_={} resolvedMaterialListCache_={} skeletonTracks={} modelRefs={} placeables={} objectDefs={} lights={}",
              geometries_.size(), geometryByFragIndex_.size(), textures_.size(), brushes_.size(), textureRefs_.size(),
              materials_.size(), brushSets_.size(), resolvedMaterialListCache_.size(),
              skeletonTracks_.size(), modelRefs_.size(), placeables_.size(), objectDefs_.size(), lights_.size());

    if (bspTree_) bspTree_->shrinkPvsArena();  // T490: trim the RLE arena's growth slack

    bool result = !geometries_.empty() || !placeables_.empty() || !objectDefs_.empty() || !skeletonTracks_.empty() || !lights_.empty();
    LOG_DEBUG(MOD_GRAPHICS, "WldLoader::parseWldBuffer: returning {}", result);
    return result;
}


// BspTree implementation

// D20f1: BspTree method implementations moved to src/client/zone_bsp.cpp

} // namespace Graphics
} // namespace EQT

// T326: the disabled block that stood here (outside the namespace close) was
// deleted -- D20f1 had already moved these implementations to
// src/client/zone_bsp.cpp. Recover with:
//   git show bcbf2ddc:src/client/graphics/eq/wld_loader.cpp
