// door_manager_mesh.cpp -- DoorManager's mesh sourcing: the real door mesh
// looked up out of the zone's object archive, and the procedural placeholder
// drawn when that lookup misses.
//
// Split out of door_manager.cpp by T453 (contiguous range 145..470 of the
// 1,365-line original, moved verbatim). Both file-static helpers
// (colorFromName, createOctagonalPrism) came with it: their only callers are
// createPlaceholderMesh, in this TU, so neither gained external linkage.
#include "client/graphics/door_manager.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace EQT {
namespace Graphics {

irr::scene::IMesh* DoorManager::findDoorMesh(const std::string& doorName) const
{
    lastMissingTextures_.clear();

    if (!currentZone_ || !smgr_) {
        return nullptr;
    }

    // Convert door name to uppercase for comparison and cache key
    std::string upperDoorName = doorName;
    std::transform(upperDoorName.begin(), upperDoorName.end(), upperDoorName.begin(), ::toupper);

    // Check mesh cache first — avoids rebuilding identical meshes (e.g. 25 DOOR1 instances)
    auto cacheIt = doorMeshCache_.find(upperDoorName);
    if (cacheIt != doorMeshCache_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door mesh cache hit for '{}'", upperDoorName);
        // T470 (B621): the caller owns what this returns (door_manager.h,
        // doorMeshCache_), so a hit hands out a reference of its own and the
        // cache keeps its grab.
        cacheIt->second->grab();
        return cacheIt->second;
    }

    irr::scene::IMesh* mesh = nullptr;

    // First, search objectGeometries map (contains all object models from _obj.wld)
    // This is the primary lookup for doors since they're dynamically placed
    auto geomIt = currentZone_->objectGeometries.find(upperDoorName);
    if (geomIt != currentZone_->objectGeometries.end() && geomIt->second) {
        LOG_DEBUG(MOD_GRAPHICS, "Found door mesh '{}' in objectGeometries ({} verts, {} tris)",
            upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        ZoneMeshBuilder builder(smgr_, driver_, nullptr);
        if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
        if (shaderMaterialSolid_ >= 0)
            builder.setShaderMaterialTypes(shaderMaterialSolid_, shaderMaterialAlphaTest_, shaderMaterialAlphaTestRGBA_);
        if (!currentZone_->objectTextures.empty() && !geomIt->second->textureNames().empty()) {
            mesh = builder.buildTexturedMesh(*geomIt->second, currentZone_->objectTextures);
        } else {
            mesh = builder.buildColoredMesh(*geomIt->second);
        }
        if (!mesh) {
            LOG_WARN(MOD_GRAPHICS, "Failed to build mesh for door '{}' (verts={}, tris={})",
                upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        }
        // Only cache when all textures are present — if async textures are pending,
        // skip cache so the mesh will be rebuilt when textures arrive
        const auto& missing = builder.getMissingTextures();
        if (!missing.empty()) {
            lastMissingTextures_.insert(lastMissingTextures_.end(), missing.begin(), missing.end());
        } else if (mesh) {
            const_cast<DoorManager*>(this)->cacheDoorMesh(upperDoorName, mesh);
        }
        return mesh;
    }

    // Try partial match in objectGeometries (for names like "DOOR_QEY01")
    for (const auto& [name, geom] : currentZone_->objectGeometries) {
        if (!geom) continue;

        // Try partial match (door name contains object name or vice versa)
        if (name.find(upperDoorName) != std::string::npos ||
            upperDoorName.find(name) != std::string::npos) {
            LOG_DEBUG(MOD_GRAPHICS, "Found door mesh via partial match: '{}' -> '{}' ({} verts)",
                upperDoorName, name, geom->vertices.size());
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
            if (shaderMaterialSolid_ >= 0)
                builder.setShaderMaterialTypes(shaderMaterialSolid_, shaderMaterialAlphaTest_, shaderMaterialAlphaTestRGBA_);
            if (!currentZone_->objectTextures.empty() && !geom->textureNames().empty()) {
                mesh = builder.buildTexturedMesh(*geom, currentZone_->objectTextures);
            } else {
                mesh = builder.buildColoredMesh(*geom);
            }
            const auto& missing = builder.getMissingTextures();
            if (!missing.empty()) {
                lastMissingTextures_.insert(lastMissingTextures_.end(), missing.begin(), missing.end());
            } else if (mesh) {
                const_cast<DoorManager*>(this)->cacheDoorMesh(upperDoorName, mesh);
            }
            return mesh;
        }
    }

    // Fallback: Search pre-placed zone objects for matching geometry
    for (const auto& objInstance : currentZone_->objects) {
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        std::string objName = objInstance.placeable->getName();
        std::transform(objName.begin(), objName.end(), objName.begin(), ::toupper);

        // Try exact match first, then partial match
        bool matched = (objName == upperDoorName) ||
                       (objName.find(upperDoorName) != std::string::npos) ||
                       (upperDoorName.find(objName) != std::string::npos);

        if (matched) {
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
            if (shaderMaterialSolid_ >= 0)
                builder.setShaderMaterialTypes(shaderMaterialSolid_, shaderMaterialAlphaTest_, shaderMaterialAlphaTestRGBA_);
            if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
            } else {
                mesh = builder.buildColoredMesh(*objInstance.geometry);
            }
            const auto& missing = builder.getMissingTextures();
            if (!missing.empty()) {
                lastMissingTextures_.insert(lastMissingTextures_.end(), missing.begin(), missing.end());
            } else if (mesh) {
                const_cast<DoorManager*>(this)->cacheDoorMesh(upperDoorName, mesh);
            }
            return mesh;
        }
    }

    return nullptr;
}

void DoorManager::invalidateMeshCache(const std::string& doorName) {
    // T470 (B621): each entry is the cache's own grab(); erasing without a
    // drop() leaked it (both the "" form and the by-name form did).
    if (doorName.empty()) {
        for (auto& [name, mesh] : doorMeshCache_) {
            if (mesh) mesh->drop();
        }
        doorMeshCache_.clear();
        return;
    }
    std::string upper = doorName;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto it = doorMeshCache_.find(upper);
    if (it == doorMeshCache_.end()) return;
    if (it->second) it->second->drop();
    doorMeshCache_.erase(it);
}

void DoorManager::cacheDoorMesh(const std::string& key, irr::scene::IMesh* mesh) {
    if (!mesh) return;
    mesh->grab();  // the cache's own reference; the caller keeps the one it was handed
    auto it = doorMeshCache_.find(key);
    if (it != doorMeshCache_.end() && it->second) {
        it->second->drop();
    }
    doorMeshCache_[key] = mesh;
}

// Generate a unique color from a door name using a simple hash
static irr::video::SColor colorFromName(const std::string& name)
{
    // FNV-1a hash of the full name
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }

    // Convert hash to HSV with high saturation and medium-high value for visibility
    float hue = (hash % 360) / 360.0f;
    float sat = 0.6f + (((hash >> 12) % 30) / 100.0f);  // 0.6-0.9
    float val = 0.65f + (((hash >> 20) % 25) / 100.0f);  // 0.65-0.9

    // HSV to RGB
    int hi = static_cast<int>(hue * 6.0f) % 6;
    float f = hue * 6.0f - hi;
    float p = val * (1.0f - sat);
    float q = val * (1.0f - f * sat);
    float t = val * (1.0f - (1.0f - f) * sat);

    float r, g, b;
    switch (hi) {
        case 0: r = val; g = t;   b = p;   break;
        case 1: r = q;   g = val; b = p;   break;
        case 2: r = p;   g = val; b = t;   break;
        case 3: r = p;   g = q;   b = val; break;
        case 4: r = t;   g = p;   b = val; break;
        default:r = val; g = p;   b = q;   break;
    }

    return irr::video::SColor(255,
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        static_cast<uint8_t>(b * 255));
}

// Build an octagonal prism mesh (barrel shape) manually
static irr::scene::IMesh* createOctagonalPrism(irr::scene::ISceneManager* smgr,
                                                 float radius, float height,
                                                 const irr::video::SColor& color)
{
    const int SIDES = 8;
    // Vertices: 2 center + 2*SIDES rim = 18 verts
    // Triangles: 2*SIDES (caps) + 2*SIDES (sides) = 32 tris
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    buf->Vertices.set_used(2 + 2 * SIDES + 2 * SIDES);  // centers + rim tops + rim bottoms
    buf->Indices.set_used(SIDES * 3 * 2 + SIDES * 6);   // cap tris + side quads

    float halfH = height * 0.5f;

    // Center vertices for top and bottom caps
    auto& topCenter = buf->Vertices[0];
    topCenter.Pos = irr::core::vector3df(0, halfH, 0);
    topCenter.Normal = irr::core::vector3df(0, 1, 0);
    topCenter.Color = color;
    topCenter.TCoords = irr::core::vector2df(0.5f, 0.5f);

    auto& botCenter = buf->Vertices[1];
    botCenter.Pos = irr::core::vector3df(0, -halfH, 0);
    botCenter.Normal = irr::core::vector3df(0, -1, 0);
    botCenter.Color = color;
    botCenter.TCoords = irr::core::vector2df(0.5f, 0.5f);

    // Rim vertices
    for (int i = 0; i < SIDES; ++i) {
        float angle = static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) / SIDES;
        float cx = radius * std::cos(angle);
        float cz = radius * std::sin(angle);
        irr::core::vector3df outNormal(std::cos(angle), 0, std::sin(angle));

        // Top rim
        auto& vt = buf->Vertices[2 + i];
        vt.Pos = irr::core::vector3df(cx, halfH, cz);
        vt.Normal = irr::core::vector3df(0, 1, 0);
        vt.Color = color;
        vt.TCoords = irr::core::vector2df(0, 0);

        // Bottom rim
        auto& vb = buf->Vertices[2 + SIDES + i];
        vb.Pos = irr::core::vector3df(cx, -halfH, cz);
        vb.Normal = irr::core::vector3df(0, -1, 0);
        // Darken bottom vertices slightly for depth
        vb.Color = irr::video::SColor(255,
            static_cast<uint8_t>(color.getRed() * 0.7f),
            static_cast<uint8_t>(color.getGreen() * 0.7f),
            static_cast<uint8_t>(color.getBlue() * 0.7f));
        vb.TCoords = irr::core::vector2df(0, 0);
    }

    irr::u32 idx = 0;
    // Top cap triangles (fan from center)
    for (int i = 0; i < SIDES; ++i) {
        buf->Indices[idx++] = 0;  // top center
        buf->Indices[idx++] = static_cast<irr::u16>(2 + i);
        buf->Indices[idx++] = static_cast<irr::u16>(2 + (i + 1) % SIDES);
    }
    // Bottom cap triangles (reverse winding)
    for (int i = 0; i < SIDES; ++i) {
        buf->Indices[idx++] = 1;  // bottom center
        buf->Indices[idx++] = static_cast<irr::u16>(2 + SIDES + (i + 1) % SIDES);
        buf->Indices[idx++] = static_cast<irr::u16>(2 + SIDES + i);
    }
    // Side quads (two triangles each)
    for (int i = 0; i < SIDES; ++i) {
        int next = (i + 1) % SIDES;
        irr::u16 tl = static_cast<irr::u16>(2 + i);
        irr::u16 tr = static_cast<irr::u16>(2 + next);
        irr::u16 bl = static_cast<irr::u16>(2 + SIDES + i);
        irr::u16 br = static_cast<irr::u16>(2 + SIDES + next);
        buf->Indices[idx++] = tl; buf->Indices[idx++] = bl; buf->Indices[idx++] = tr;
        buf->Indices[idx++] = tr; buf->Indices[idx++] = bl; buf->Indices[idx++] = br;
    }

    buf->recalculateBoundingBox();
    buf->setHardwareMappingHint(irr::scene::EHM_STATIC);
    buf->Material.Lighting = false;
    buf->Material.BackfaceCulling = false;
    buf->Material.MaterialType = irr::video::EMT_SOLID;

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();
    return mesh;
}

irr::scene::IMesh* DoorManager::createPlaceholderMesh(const std::string& doorName) const
{
    if (!smgr_) {
        return nullptr;
    }

    // Uppercase the name for category matching
    std::string upper = doorName;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Determine shape category from name prefix
    irr::core::vector3df dims;
    bool useOctagon = false;

    if (upper.find("BARREL") != std::string::npos || upper.find("KEG") != std::string::npos) {
        // Barrel/keg: octagonal prism
        useOctagon = true;
        dims = irr::core::vector3df(1.0f, 2.5f, 0);  // radius, height (unused z)
    } else if (upper.find("CRATE") != std::string::npos || upper.find("BOX") != std::string::npos) {
        // Crates: cube-ish, small height variation per variant
        float h = 2.0f + (doorName.empty() ? 0.0f : (doorName.back() % 5) * 0.3f);
        dims = irr::core::vector3df(2.0f, h, 2.0f);
    } else if (upper.find("DOOR") != std::string::npos || upper.find("GATE") != std::string::npos) {
        // Doors/gates: tall thin slab
        dims = irr::core::vector3df(2.0f, 6.0f, 0.5f);
    } else if (upper.find("CHEST") != std::string::npos) {
        // Chest: low wide box
        dims = irr::core::vector3df(2.0f, 1.2f, 1.5f);
    } else if (upper.find("SACK") != std::string::npos || upper.find("BAG") != std::string::npos) {
        // Sack/bag: small octagonal
        useOctagon = true;
        dims = irr::core::vector3df(0.8f, 1.5f, 0);
    } else if (upper.find("BENCH") != std::string::npos || upper.find("TABLE") != std::string::npos) {
        // Furniture: wide low slab
        dims = irr::core::vector3df(3.0f, 1.5f, 1.5f);
    } else {
        // Unknown: default cube, slightly varied by name
        dims = irr::core::vector3df(2.0f, 3.0f, 2.0f);
    }

    // Get unique color for this specific name
    irr::video::SColor color = doorName.empty()
        ? irr::video::SColor(255, 100, 150, 200)  // fallback blue-ish
        : colorFromName(doorName);

    irr::scene::IMesh* mesh = nullptr;

    if (useOctagon) {
        mesh = createOctagonalPrism(smgr_, dims.X, dims.Y, color);
    } else {
        mesh = smgr_->getGeometryCreator()->createCubeMesh(dims);
        if (mesh) {
            // Bake color into vertex colors — GLES2 shaders read aColor
            // from vertices, not DiffuseColor/AmbientColor from the material
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
                irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
                for (irr::u32 v = 0; v < mb->getVertexCount(); ++v) {
                    // Darken bottom vertices for depth
                    float yFactor = (verts[v].Pos.Y < 0) ? 0.7f : 1.0f;
                    verts[v].Color = irr::video::SColor(255,
                        static_cast<uint8_t>(color.getRed() * yFactor),
                        static_cast<uint8_t>(color.getGreen() * yFactor),
                        static_cast<uint8_t>(color.getBlue() * yFactor));
                }
            }
        }
    }

    return mesh;
}

} // namespace Graphics
} // namespace EQT
