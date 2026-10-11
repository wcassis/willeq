#include "client/graphics/eq/geometry_combiner.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace EQT {
namespace Graphics {

std::shared_ptr<ZoneGeometry> combineCharacterParts(
    const std::vector<std::shared_ptr<ZoneGeometry>>& parts) {

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterParts: {} parts", parts.size());

    if (parts.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "combineCharacterParts: empty parts, returning nullptr");
        return nullptr;
    }

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    uint32_t vertexOffset = 0;
    uint32_t textureOffset = 0;

    for (const auto& part : parts) {
        if (!part || part->vertices.empty()) {
            continue;
        }

        // Add vertices
        for (const auto& v : part->vertices) {
            combined->vertices.push_back(v);
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        // Add triangles with adjusted indices
        for (const auto& tri : part->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            // Adjust texture index
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        // Merge texture names
        for (const auto& texName : part->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }

        // Merge invisible flags
        for (bool inv : part->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        // P37: merge masked flags too — previously dropped entirely, so every
        // combined character geometry's textureMasked() came back empty
        // regardless of source data, silently defeating any masked-material
        // classification done against the combined geometry (mesh_building.cpp).
        for (bool m : part->textureMasked()) {
            combined->mutableMaterialData().textureMasked.push_back(m);
        }

        // Merge vertex pieces
        for (const auto& piece : part->vertexPieces) {
            combined->vertexPieces.push_back(piece);
        }

        vertexOffset += static_cast<uint32_t>(part->vertices.size());
        textureOffset += static_cast<uint32_t>(part->textureNames().size());
    }

    if (combined->vertices.empty() || combined->triangles.empty()) {
        return nullptr;
    }

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterParts: RESULT verts={} tris={} textureNames={}",
              combined->vertices.size(), combined->triangles.size(), combined->textureNames().size());
    combined->name = "combined";
    return combined;
}

std::shared_ptr<ZoneGeometry> combineCharacterPartsWithTransforms(
    const std::vector<CharacterPart>& parts) {

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsWithTransforms: {} parts", parts.size());

    if (parts.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsWithTransforms: empty parts, returning nullptr");
        return nullptr;
    }

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    uint32_t vertexOffset = 0;
    uint32_t textureOffset = 0;

    size_t partIdx = 0;
    for (const auto& part : parts) {
        if (!part.geometry || part.geometry->vertices.empty()) {
            LOG_TRACE(MOD_GRAPHICS, "  combiner part[{}]: SKIPPED (geometry={} verts={})",
                      partIdx, (bool)part.geometry, part.geometry ? part.geometry->vertices.size() : 0);
            partIdx++;
            continue;
        }

        LOG_TRACE(MOD_GRAPHICS, "  combiner part[{}]: '{}' verts={} tris={} materialData={} textureNames={} vertexPieces={}",
                  partIdx, part.geometry->name, part.geometry->vertices.size(),
                  part.geometry->triangles.size(), (bool)part.geometry->materialData,
                  part.geometry->textureNames().size(), part.geometry->vertexPieces.size());
        // Log actual texture names for this part
        for (size_t ti = 0; ti < part.geometry->textureNames().size(); ++ti) {
            LOG_TRACE(MOD_GRAPHICS, "    combiner part[{}] textureName[{}]='{}' invisible={}",
                      partIdx, ti, part.geometry->textureNames()[ti],
                      ti < part.geometry->textureInvisible().size() ? part.geometry->textureInvisible()[ti] : false);
        }

        // Debug output for bone transforms
        if (std::abs(part.shiftX) > 0.001f || std::abs(part.shiftY) > 0.001f || std::abs(part.shiftZ) > 0.001f) {
            LOG_DEBUG(MOD_GRAPHICS, "  Part bone transform: shift=({},{},{}) rot=({},{},{})",
                      part.shiftX, part.shiftY, part.shiftZ, part.rotateX, part.rotateY, part.rotateZ);
        }

        // Pre-compute rotation matrix from Euler angles (ZYX order)
        // EQ uses radians
        float cosX = std::cos(part.rotateX);
        float sinX = std::sin(part.rotateX);
        float cosY = std::cos(part.rotateY);
        float sinY = std::sin(part.rotateY);
        float cosZ = std::cos(part.rotateZ);
        float sinZ = std::sin(part.rotateZ);

        // Combined rotation matrix (ZYX = Rz * Ry * Rx)
        // Row 0
        float r00 = cosY * cosZ;
        float r01 = sinX * sinY * cosZ - cosX * sinZ;
        float r02 = cosX * sinY * cosZ + sinX * sinZ;
        // Row 1
        float r10 = cosY * sinZ;
        float r11 = sinX * sinY * sinZ + cosX * cosZ;
        float r12 = cosX * sinY * sinZ - sinX * cosZ;
        // Row 2
        float r20 = -sinY;
        float r21 = sinX * cosY;
        float r22 = cosX * cosY;

        // Add vertices with bone transform applied
        for (const auto& v : part.geometry->vertices) {
            Vertex3D transformed = v;

            // Apply rotation first
            float rx = r00 * v.x + r01 * v.y + r02 * v.z;
            float ry = r10 * v.x + r11 * v.y + r12 * v.z;
            float rz = r20 * v.x + r21 * v.y + r22 * v.z;

            // Then apply translation
            transformed.x = rx + part.shiftX;
            transformed.y = ry + part.shiftY;
            transformed.z = rz + part.shiftZ;

            // Also transform normals (rotation only, no translation)
            float rnx = r00 * v.nx + r01 * v.ny + r02 * v.nz;
            float rny = r10 * v.nx + r11 * v.ny + r12 * v.nz;
            float rnz = r20 * v.nx + r21 * v.ny + r22 * v.nz;
            transformed.nx = rnx;
            transformed.ny = rny;
            transformed.nz = rnz;

            combined->vertices.push_back(transformed);

            // Update bounds
            combined->minX = std::min(combined->minX, transformed.x);
            combined->minY = std::min(combined->minY, transformed.y);
            combined->minZ = std::min(combined->minZ, transformed.z);
            combined->maxX = std::max(combined->maxX, transformed.x);
            combined->maxY = std::max(combined->maxY, transformed.y);
            combined->maxZ = std::max(combined->maxZ, transformed.z);
        }

        // Add triangles with adjusted indices
        for (const auto& tri : part.geometry->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        // Merge texture names
        for (const auto& texName : part.geometry->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }

        // Merge invisible flags
        for (bool inv : part.geometry->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        // P37: merge masked flags too (see combineCharacterParts's comment above).
        for (bool m : part.geometry->textureMasked()) {
            combined->mutableMaterialData().textureMasked.push_back(m);
        }

        // Merge vertex pieces with adjusted vertex offsets
        for (const auto& piece : part.geometry->vertexPieces) {
            VertexPiece adjustedPiece = piece;
            // Note: vertex indices are relative to part, need to adjust for combined mesh
            // But bone indices should stay the same
            combined->vertexPieces.push_back(adjustedPiece);
        }

        vertexOffset += static_cast<uint32_t>(part.geometry->vertices.size());
        textureOffset += static_cast<uint32_t>(part.geometry->textureNames().size());
        LOG_TRACE(MOD_GRAPHICS, "  combiner part[{}]: after merge — combined textureNames={} verts={} tris={}",
                  partIdx, combined->textureNames().size(), combined->vertices.size(), combined->triangles.size());
        partIdx++;
    }

    if (combined->vertices.empty() || combined->triangles.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsWithTransforms: result empty (verts={} tris={}), returning nullptr",
                  combined->vertices.size(), combined->triangles.size());
        return nullptr;
    }

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsWithTransforms: RESULT verts={} tris={} textureNames={} materialData={}",
              combined->vertices.size(), combined->triangles.size(),
              combined->textureNames().size(), (bool)combined->materialData);
    combined->name = "combined_with_transforms";
    return combined;
}

std::shared_ptr<ZoneGeometry> combineCharacterPartsRaw(
    const std::vector<CharacterPart>& parts) {

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsRaw: {} parts", parts.size());

    if (parts.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsRaw: empty parts, returning nullptr");
        return nullptr;
    }

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    uint32_t vertexOffset = 0;
    uint32_t textureOffset = 0;

    for (const auto& part : parts) {
        if (!part.geometry || part.geometry->vertices.empty()) {
            continue;
        }

        // Add vertices WITHOUT applying bone transforms
        for (const auto& v : part.geometry->vertices) {
            combined->vertices.push_back(v);

            // Update bounds
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        // Add triangles with adjusted indices
        for (const auto& tri : part.geometry->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        // Merge texture names
        for (const auto& texName : part.geometry->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }

        // Merge invisible flags
        for (bool inv : part.geometry->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        // P37: merge masked flags too (see combineCharacterParts's comment above).
        for (bool m : part.geometry->textureMasked()) {
            combined->mutableMaterialData().textureMasked.push_back(m);
        }

        // Merge vertex pieces (critical for animation!)
        for (const auto& piece : part.geometry->vertexPieces) {
            combined->vertexPieces.push_back(piece);
        }

        vertexOffset += static_cast<uint32_t>(part.geometry->vertices.size());
        textureOffset += static_cast<uint32_t>(part.geometry->textureNames().size());
    }

    if (combined->vertices.empty() || combined->triangles.empty()) {
        return nullptr;
    }

    LOG_DEBUG(MOD_GRAPHICS, "combineCharacterPartsRaw: RESULT verts={} tris={} textureNames={} materialData={}",
              combined->vertices.size(), combined->triangles.size(),
              combined->textureNames().size(), (bool)combined->materialData);
    combined->name = "combined_raw";
    return combined;
}

SelectedCharacterParts selectCharacterParts(const CharacterModel& character,
                                            const std::string& upperRaceCode) {
    // B733 (issue #70): the skeleton's 0x200 mesh list (character.skeleton,
    // T476) names the meshes this character DEFORMS. A head can carry a name
    // the race code does not predict (race 146 SSN's head is
    // "SNNHE00_DMSPRITEDEF" -- the "SNN" spelling differs from the race code
    // "SSN"), so the old name selection ({CODE}_DMSPRITEDEF /
    // {CODE}HE00_DMSPRITEDEF) dropped it even though the skeleton referenced
    // it.
    //
    // The loader stores EXACTLY the geometries the skeleton references: the
    // bone-attached meshes (flattenSkeleton) plus the 0x200-listed meshes the
    // bones did not name (B733, s3d_loader_characters.cpp). So when the list
    // is non-empty, every part the character carries is a referenced part and
    // the selection is the whole stored set -- the skeleton's part list, no
    // name matching. When the list is empty there is no part list to select
    // from, and the old name selection runs verbatim: the race-code names as
    // the fallback for skeletons that list none. (An empty-list character's
    // stored set is already exactly its referenced set, so this also matches
    // the "select what the skeleton references" rule -- but keeping the name
    // match verbatim is what makes every such character draw byte-identically
    // to before, which the gate's golden frames demand.)
    const bool hasSkeletonMeshList = character.skeleton
        && !character.skeleton->meshRefs.empty();
    if (hasSkeletonMeshList) {
        SelectedCharacterParts out;
        out.skinned = character.partsWithTransforms;
        out.raw = character.rawParts;
        return out;
    }

    // Fallback: the old name selection, verbatim.
    SelectedCharacterParts out;
    std::string defaultBodyName = upperRaceCode + "_DMSPRITEDEF";
    std::string defaultHeadName = upperRaceCode + "HE00_DMSPRITEDEF";
    for (const auto& part : character.partsWithTransforms) {
        if (!part.geometry) continue;
        std::string partName = part.geometry->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (partName == defaultBodyName || partName == defaultHeadName)
            out.skinned.push_back(part);
    }
    for (const auto& part : character.rawParts) {
        if (!part.geometry) continue;
        std::string partName = part.geometry->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (partName == defaultBodyName || partName == defaultHeadName)
            out.raw.push_back(part);
    }
    return out;
}

} // namespace Graphics
} // namespace EQT
