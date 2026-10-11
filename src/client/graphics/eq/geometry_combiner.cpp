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
    // B733 (issue #70): the old base-model selection picked parts by NAME --
    // exactly "{CODE}_DMSPRITEDEF" and "{CODE}HE00_DMSPRITEDEF". A part whose
    // mesh carries a name the race code does not predict (race 146 SSN's head
    // is "SNNHE00_DMSPRITEDEF") was dropped even though the character's
    // skeleton references it.
    //
    // The selection is now the UNION of the old name selection (run verbatim,
    // in the stored part order, so every character the old code drew keeps
    // drawing exactly what it drew) and the geometries the skeleton's 0x200
    // mesh list (T476) names that the name selection did not pick
    // (CharacterModel::skeletonReferenced, recorded by the loader). A
    // skeleton whose list adds nothing stores an empty tag and draws
    // byte-identically to before; a skeleton whose list names a mesh the
    // name selection cannot reach (the #70 head) draws it in addition.
    auto isDefaultName = [](const ZoneGeometry* g, const std::string& code) {
        std::string name = g->name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        std::string defaultBodyName = code + "_DMSPRITEDEF";
        std::string defaultHeadName = code + "HE00_DMSPRITEDEF";
        return name == defaultBodyName || name == defaultHeadName;
    };
    std::vector<const ZoneGeometry*> picked;
    auto pickByName = [&](const std::vector<CharacterPart>& src) {
        for (const auto& part : src) {
            if (!part.geometry) continue;
            if (isDefaultName(part.geometry.get(), upperRaceCode))
                picked.push_back(part.geometry.get());
        }
    };
    // The skinned and the raw lists carry parallel geometry names (the
    // loader stores each part in both), so one name selection covers both.
    pickByName(character.partsWithTransforms);
    for (const ZoneGeometry* g : character.skeletonReferenced) {
        if (std::find(picked.begin(), picked.end(), g) == picked.end())
            picked.push_back(g);
    }

    SelectedCharacterParts out;
    for (const auto& part : character.partsWithTransforms) {
        if (!part.geometry) continue;
        if (std::find(picked.begin(), picked.end(), part.geometry.get()) != picked.end())
            out.skinned.push_back(part);
    }
    for (const auto& part : character.rawParts) {
        if (!part.geometry) continue;
        if (isDefaultName(part.geometry.get(), upperRaceCode)) {
            out.raw.push_back(part);
        } else {
            for (const ZoneGeometry* g : character.skeletonReferenced)
                if (part.geometry->name == g->name) { out.raw.push_back(part); break; }
        }
    }
    return out;
}

} // namespace Graphics
} // namespace EQT
