// detail_manager_surface.cpp -- DetailManager's surface queries: the ground
// raycast, the texture behind a hit triangle, the surface type at a world
// position, and the texture-name classifier that turns a texture into a
// SurfaceType.
//
// Split out of detail_manager.cpp by T453 (contiguous range 649..1176 of the
// 1,219-line original, moved verbatim).
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_chunk.h"
#include "client/graphics/detail/detail_texture_atlas.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <set>
#include <map>

namespace EQT {
namespace Graphics {
namespace Detail {

bool DetailManager::getGroundInfo(float x, float z, float& outY,
                                  irr::core::vector3df& outNormal,
                                  SurfaceType& outSurfaceType) const {
    if (!groundRaycast_) {
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager::getGroundInfo: No ground raycast callback");
        return false;
    }

    // Use BSP-filtered ground raycast from renderer
    // startY=60.0 — below sky domes/ceilings but above terrain
    irr::core::vector3df hitNormal;
    irr::core::triangle3df hitTriangle;

    if (groundRaycast_(x, z, 60.0f, outY, hitNormal, hitTriangle)) {
        // Ensure normal points up (away from ground)
        outNormal = hitNormal;
        if (outNormal.Y < 0) {
            outNormal = -outNormal;
        }

        // Get surface type - prefer pre-computed surface map (O(1) lookup)
        outSurfaceType = SurfaceType::Grass;  // Default

        // Query position in Irrlicht coords: x=EQ_X, z=EQ_Y (the input params)
        if (surfaceMap_.isLoaded()) {
            // Fast path: use pre-computed surface map
            // Surface map uses EQ coordinates (X, Y horizontal)
            // Input x=EQ_X, z=EQ_Y
            outSurfaceType = surfaceMap_.getSurfaceType(x, z);

            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Surface map lookup at ({:.1f}, {:.1f}) -> {}",
                     x, z, static_cast<int>(outSurfaceType));
        } else {
            // Slow path: on-the-fly texture detection (fallback if no surface map)
            static bool loggedZoneMeshStatus = false;
            if (!loggedZoneMeshStatus) {
                loggedZoneMeshStatus = true;
                LOG_DEBUG(MOD_GRAPHICS, "DetailManager::getGroundInfo: No surface map, using on-the-fly detection. "
                         "zoneMeshNode_={}, additionalMeshNodes_.size={}",
                         (void*)zoneMeshNode_, additionalMeshNodes_.size());
            }
            // Check for mesh nodes (zoneMeshNode_ in non-PVS mode, additionalMeshNodes_ in PVS mode)
            if (zoneMeshNode_ || !additionalMeshNodes_.empty()) {
                outSurfaceType = getTextureFromMeshNode(hitTriangle);
            }

            // Fall back to zone geometry lookup if mesh lookup didn't find a specific texture
            if (outSurfaceType == SurfaceType::Grass) {
                SurfaceType geomSurface = getSurfaceTypeAtPosition(x, z);
                // Only use geometry lookup if it found a specific non-default surface
                if (geomSurface != SurfaceType::Grass) {
                    outSurfaceType = geomSurface;
                }
            }
        }

        return true;
    }

    return false;
}

SurfaceType DetailManager::getTextureFromMeshNode(const irr::core::triangle3df& hitTriangle) const {
    // Build list of all mesh nodes to search
    std::vector<irr::scene::IMeshSceneNode*> meshNodes;
    if (zoneMeshNode_) {
        meshNodes.push_back(zoneMeshNode_);
    }
    for (auto* node : additionalMeshNodes_) {
        if (node) {
            meshNodes.push_back(node);
        }
    }

    if (meshNodes.empty()) {
        return SurfaceType::Grass;
    }

    // Get hit triangle vertices for comparison
    const auto& p1 = hitTriangle.pointA;
    const auto& p2 = hitTriangle.pointB;
    const auto& p3 = hitTriangle.pointC;
    irr::core::vector3df hitCenter = (p1 + p2 + p3) / 3.0f;

    // Debug: log mesh info once
    static bool loggedMeshInfo = false;
    if (!loggedMeshInfo) {
        loggedMeshInfo = true;
        size_t totalBuffers = 0;
        for (auto* node : meshNodes) {
            if (node->getMesh()) {
                totalBuffers += node->getMesh()->getMeshBufferCount();
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: {} mesh nodes with {} total buffers for texture lookup",
                 meshNodes.size(), totalBuffers);
    }

    float closestDist = 999999.0f;
    std::string closestTexName;

    // Search through all mesh nodes and their buffers
    for (auto* meshNode : meshNodes) {
        irr::scene::IMesh* mesh = meshNode->getMesh();
        if (!mesh) continue;

        for (irr::u32 bufIdx = 0; bufIdx < mesh->getMeshBufferCount(); ++bufIdx) {
            irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(bufIdx);
            if (!buffer) continue;

            // Check vertex type
            irr::video::E_VERTEX_TYPE vtype = buffer->getVertexType();

            // Check if buffer has this triangle by comparing vertex positions
            const irr::u16* indices = buffer->getIndices();
            irr::u32 indexCount = buffer->getIndexCount();

            // Handle different vertex types
            for (irr::u32 i = 0; i + 2 < indexCount; i += 3) {
                irr::core::vector3df v1, v2, v3;

                if (vtype == irr::video::EVT_STANDARD) {
                    irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else if (vtype == irr::video::EVT_2TCOORDS) {
                    irr::video::S3DVertex2TCoords* vertices = static_cast<irr::video::S3DVertex2TCoords*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else if (vtype == irr::video::EVT_TANGENTS) {
                    irr::video::S3DVertexTangents* vertices = static_cast<irr::video::S3DVertexTangents*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else {
                    continue;
                }

                // Calculate distance to triangle center
                irr::core::vector3df triCenter = (v1 + v2 + v3) / 3.0f;
                float dist = triCenter.getDistanceFrom(hitCenter);

                if (dist < closestDist) {
                    closestDist = dist;
                    const irr::video::SMaterial& mat = buffer->getMaterial();
                    irr::video::ITexture* tex = mat.getTexture(0);
                    if (tex) {
                        irr::io::path texPath = tex->getName();
                        closestTexName = texPath.c_str();
                    }
                }

                // Compare with small tolerance - try multiple orderings
                float tolerance = 1.0f;
                bool match = (v1.equals(p1, tolerance) && v2.equals(p2, tolerance) && v3.equals(p3, tolerance)) ||
                            (v1.equals(p2, tolerance) && v2.equals(p3, tolerance) && v3.equals(p1, tolerance)) ||
                            (v1.equals(p3, tolerance) && v2.equals(p1, tolerance) && v3.equals(p2, tolerance)) ||
                            (v1.equals(p1, tolerance) && v2.equals(p3, tolerance) && v3.equals(p2, tolerance)) ||
                            (v1.equals(p2, tolerance) && v2.equals(p1, tolerance) && v3.equals(p3, tolerance)) ||
                            (v1.equals(p3, tolerance) && v2.equals(p2, tolerance) && v3.equals(p1, tolerance));

                if (match) {
                    // Found the matching triangle - get texture from this buffer's material
                    const irr::video::SMaterial& mat = buffer->getMaterial();
                    irr::video::ITexture* tex = mat.getTexture(0);
                    if (tex) {
                        irr::io::path texPath = tex->getName();
                        std::string texName = texPath.c_str();

                        // Extract just the filename if it's a full path
                        size_t lastSlash = texName.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            texName = texName.substr(lastSlash + 1);
                        }

                        // Debug logging (limited)
                        static int meshTexLogCount = 0;
                        if (meshTexLogCount++ < 10) {
                            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Mesh texture lookup found '{}' (exact match)", texName);
                        }

                        return classifyTexture(texName);
                    }
                    return SurfaceType::Grass;
                }
            }
        }
    }

    // No exact match - use closest triangle's texture if close enough
    static int noMatchLogCount = 0;
    if (noMatchLogCount++ < 5) {
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: No exact triangle match, closest dist={:.1f}, tex='{}'",
                 closestDist, closestTexName);
    }

    // If closest is within reasonable distance, use its texture
    // Use a larger threshold (50 units) since detail chunks query over larger areas
    // and mesh batching can result in triangles being grouped with nearby surfaces
    if (closestDist < 50.0f && !closestTexName.empty()) {
        size_t lastSlash = closestTexName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            closestTexName = closestTexName.substr(lastSlash + 1);
        }
        return classifyTexture(closestTexName);
    }

    // Triangle not found in mesh buffers
    return SurfaceType::Grass;
}

SurfaceType DetailManager::getSurfaceTypeAtPosition(float x, float z) const {
    if (!zoneGeometry_ || zoneGeometry_->triangles.empty() || zoneTextureNames_.empty()) {
        return SurfaceType::Grass;  // Default fallback
    }

    const auto& vertices = zoneGeometry_->vertices;
    const auto& triangles = zoneGeometry_->triangles;

    // Debug: log coordinate bounds to understand the geometry range
    static bool loggedCoords = false;
    if (!loggedCoords && !vertices.empty()) {
        loggedCoords = true;
        float minX = vertices[0].x, maxX = vertices[0].x;
        float minY = vertices[0].y, maxY = vertices[0].y;
        for (const auto& v : vertices) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.y > maxY) maxY = v.y;
        }
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Query pos Irrlicht ({:.1f}, {:.1f})",  x, z);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Zone geometry EQ X range: [{:.1f}, {:.1f}]", minX, maxX);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Zone geometry EQ Y range: [{:.1f}, {:.1f}]", minY, maxY);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Note - Irrlicht X=EQ X, Irrlicht Z=EQ Y");
    }

    // Find triangle containing this XZ point (ignoring Y for ground lookup)
    // Note: Zone geometry uses EQ coordinates where Y is horizontal, Z is up
    // Irrlicht uses X horizontal, Y up, Z horizontal
    // So we search in EQ XY space (which maps to Irrlicht XZ)

    // Debug: track closest triangle
    static int debugCount = 0;
    float closestDist = 999999.0f;
    size_t closestTriIdx = 0;

    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx) {
        const auto& tri = triangles[triIdx];
        if (tri.v1 >= vertices.size() || tri.v2 >= vertices.size() || tri.v3 >= vertices.size()) {
            continue;
        }

        const auto& v1 = vertices[tri.v1];
        const auto& v2 = vertices[tri.v2];
        const auto& v3 = vertices[tri.v3];

        // Check if point (x, z) is inside the triangle's XY projection (EQ coords)
        // Note: Irrlicht X = EQ X, Irrlicht Z = EQ Y
        float x1 = v1.x, y1 = v1.y;
        float x2 = v2.x, y2 = v2.y;
        float x3 = v3.x, y3 = v3.y;

        // Track distance to triangle center for debugging
        float cx = (x1 + x2 + x3) / 3.0f;
        float cy = (y1 + y2 + y3) / 3.0f;
        float dist = std::sqrt((x - cx) * (x - cx) + (z - cy) * (z - cy));
        if (dist < closestDist) {
            closestDist = dist;
            closestTriIdx = triIdx;
        }

        // Barycentric coordinate test
        float denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
        if (std::abs(denom) < 0.0001f) continue;  // Degenerate triangle

        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (z - y3)) / denom;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (z - y3)) / denom;
        float c = 1.0f - a - b;

        // Check if point is inside triangle (with small tolerance)
        if (a >= -0.01f && b >= -0.01f && c >= -0.01f && a <= 1.01f && b <= 1.01f && c <= 1.01f) {
            // Found the triangle - get its texture
            static int logCount = 0;
            if (tri.textureIndex < zoneTextureNames_.size()) {
                const std::string& texName = zoneTextureNames_[tri.textureIndex];
                SurfaceType surfType = classifyTexture(texName);

                // Debug logging (limited)
                if (logCount++ < 20) {
                    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Position ({:.1f}, {:.1f}) -> texIdx {} -> texture '{}' -> surface {}",
                             x, z, tri.textureIndex, texName, static_cast<int>(surfType));
                }

                return surfType;
            } else {
                if (logCount++ < 20) {
                    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Position ({:.1f}, {:.1f}) -> texIdx {} OUT OF RANGE (max {})",
                             x, z, tri.textureIndex, zoneTextureNames_.size());
                }
            }
            break;
        }
    }

    // Debug: log closest triangle info if no match found
    if (debugCount++ < 5 && closestDist < 999999.0f) {
        const auto& tri = triangles[closestTriIdx];
        const auto& v1 = vertices[tri.v1];
        const auto& v2 = vertices[tri.v2];
        const auto& v3 = vertices[tri.v3];
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: No triangle match for ({:.1f}, {:.1f}), closest dist={:.1f}",
                 x, z, closestDist);
        LOG_DEBUG(MOD_GRAPHICS, "  Closest tri verts: ({:.1f},{:.1f}), ({:.1f},{:.1f}), ({:.1f},{:.1f})",
                 v1.x, v1.y, v2.x, v2.y, v3.x, v3.y);
    }

    return SurfaceType::Grass;  // Default if no triangle found
}

SurfaceType DetailManager::classifyTexture(const std::string& textureName) const {
    // Empty texture name - default to grass for outdoor zones
    // Hard surfaces in EQ zones (cobble, brick, floor) typically HAVE texture names
    // Missing texture data usually comes from terrain/ground meshes
    if (textureName.empty()) {
        return SurfaceType::Grass;
    }

    // Convert to lowercase for case-insensitive matching
    std::string name = textureName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // Helper to check if name starts with a prefix
    auto startsWith = [&name](const char* prefix) {
        return name.rfind(prefix, 0) == 0;
    };

    // Debug: log texture classification (limited)
    static int classifyLogCount = 0;

    // Helper to log classification
    auto logAndReturn = [&](SurfaceType surface, const char* reason) {
        if (classifyLogCount++ < 20) {
            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: classifyTexture '{}' -> {} ({})",
                     name, static_cast<int>(surface), reason);
        }
        return surface;
    };

    // === EXCLUSIONS FIRST - textures that are NOT walkable ground ===

    // Check for water/lava first (these should be excluded entirely)
    if (name.find("water") != std::string::npos ||
        name.find("lava") != std::string::npos ||
        name.find("lavaf") != std::string::npos ||
        name.find("falls") != std::string::npos ||
        name.find("fount") != std::string::npos ||
        name.find("agua") != std::string::npos) {
        return logAndReturn(SurfaceType::Water, "water/lava");
    }

    // Wall surfaces - treat as brick/stone (indoor) - exclude from detail
    if (name.find("wall") != std::string::npos ||
        name.find("waal") != std::string::npos ||     // EQ typo
        name.find("wail") != std::string::npos ||     // EQ typo
        name.find("wafl") != std::string::npos) {     // EQ abbreviation
        return logAndReturn(SurfaceType::Brick, "wall");
    }

    // Roof textures - no vegetation
    if (name.find("roof") != std::string::npos ||
        name.find("ceil") != std::string::npos) {
        return logAndReturn(SurfaceType::Stone, "roof/ceiling");
    }

    // === BIOME-SPECIFIC GROUND TEXTURES ===
    // Check these BEFORE generic grass to properly categorize expansion content

    // Swamp/marsh textures (Innothule, Feerrott, Kunark swamps)
    if (name.find("swamp") != std::string::npos ||
        name.find("marsh") != std::string::npos ||
        name.find("bog") != std::string::npos ||
        name.find("muck") != std::string::npos ||
        name.find("slime") != std::string::npos ||
        name.find("sludge") != std::string::npos) {
        return logAndReturn(SurfaceType::Swamp, "swamp/marsh");
    }

    // Jungle textures (Kunark tropical zones)
    if (name.find("jungle") != std::string::npos ||
        name.find("fern") != std::string::npos ||
        name.find("palm") != std::string::npos ||
        name.find("tropical") != std::string::npos ||
        startsWith("ej") ||           // Emerald Jungle prefix
        startsWith("sbjung")) {       // Stonebrunt jungle
        return logAndReturn(SurfaceType::Jungle, "jungle/tropical");
    }

    // Firiona Vie grass is tropical (Kunark) - check for "fir" prefix but not "fire"
    if ((startsWith("fir") || name.find("firgrass") != std::string::npos) &&
        name.find("fire") == std::string::npos) {
        return logAndReturn(SurfaceType::Jungle, "firiona/tropical");
    }

    // Snow/ice textures (Velious zones) - expanded patterns
    if (name.find("snow") != std::string::npos ||
        name.find("ice") != std::string::npos ||
        name.find("frost") != std::string::npos ||
        name.find("frozen") != std::string::npos ||
        name.find("icsnow") != std::string::npos ||
        startsWith("gdr") ||          // Great Divide prefix
        startsWith("vel") ||          // Velketor prefix
        startsWith("wice") ||         // Velious water ice
        startsWith("thu")) {          // Thurgadin prefix
        return logAndReturn(SurfaceType::Snow, "snow/ice");
    }

    // === GENERIC GROUND TEXTURES ===

    // Grass surfaces
    if (name.find("grass") != std::string::npos ||
        name.find("gras") != std::string::npos ||
        name.find("lawn") != std::string::npos ||
        name.find("turf") != std::string::npos) {
        return logAndReturn(SurfaceType::Grass, "grass");
    }

    // Natural rock/cliff terrain (NOT man-made stone floors)
    if ((name.find("rock") != std::string::npos ||
         name.find("cliff") != std::string::npos ||
         name.find("boulder") != std::string::npos ||
         name.find("mountain") != std::string::npos ||
         name.find("crag") != std::string::npos) &&
        name.find("floor") == std::string::npos &&
        name.find("flor") == std::string::npos &&
        name.find("tile") == std::string::npos) {
        return logAndReturn(SurfaceType::Rock, "natural rock");
    }

    // Man-made stone surfaces - cobblestone, floors, tiles
    if (name.find("stone") != std::string::npos ||
        name.find("cobble") != std::string::npos ||
        name.find("coble") != std::string::npos ||    // EQ uses "coble" not "cobble"
        name.find("marble") != std::string::npos ||
        name.find("granite") != std::string::npos ||
        name.find("slate") != std::string::npos ||
        name.find("pave") != std::string::npos ||
        name.find("floor") != std::string::npos ||
        name.find("flor") != std::string::npos ||     // EQ abbreviation
        name.find("flr") != std::string::npos ||      // EQ abbreviation
        name.find("tile") != std::string::npos ||
        name.find("til") != std::string::npos ||      // EQ abbreviation
        name.find("carpet") != std::string::npos) {   // Carpet = indoor
        return logAndReturn(SurfaceType::Stone, "stone/floor/tile");
    }

    // Brick surfaces
    if (name.find("brick") != std::string::npos ||
        name.find("city") != std::string::npos ||     // citywall = stone/brick
        name.find("cyw") != std::string::npos ||      // cyw = city wall prefix
        name.find("leav") != std::string::npos ||     // eaves/leaves on buildings
        name.find("eave") != std::string::npos) {     // roof eaves
        return logAndReturn(SurfaceType::Brick, "brick/city");
    }

    // Wood surfaces
    if (name.find("wood") != std::string::npos ||
        name.find("plank") != std::string::npos ||
        name.find("board") != std::string::npos ||
        name.find("timber") != std::string::npos ||
        name.find("deck") != std::string::npos ||
        name.find("wdfloor") != std::string::npos ||
        name.find("jam") != std::string::npos) {       // Door jamb/frame
        return logAndReturn(SurfaceType::Wood, "wood");
    }

    // Gold/decorative building textures - treat as stone
    if (name.find("gold") != std::string::npos ||
        name.find("ornate") != std::string::npos ||
        name.find("decor") != std::string::npos ||
        name.find("trim") != std::string::npos) {
        return logAndReturn(SurfaceType::Stone, "decorative");
    }

    // Sand surfaces
    if (name.find("sand") != std::string::npos ||
        name.find("beach") != std::string::npos ||
        name.find("desert") != std::string::npos ||
        name.find("dune") != std::string::npos) {
        return logAndReturn(SurfaceType::Sand, "sand");
    }

    // Dirt surfaces
    if (name.find("dirt") != std::string::npos ||
        name.find("drt") != std::string::npos ||       // EQ abbreviation for dirt
        name.find("xdrt") != std::string::npos ||      // exterior dirt pattern
        name.find("mud") != std::string::npos ||
        name.find("ground") != std::string::npos ||
        name.find("path") != std::string::npos ||
        name.find("road") != std::string::npos) {
        return logAndReturn(SurfaceType::Dirt, "dirt");
    }

    // === KUNARK ZONE PREFIXES ===

    // Burning Woods - volcanic/ash ground
    if (startsWith("bw") && (name.find("ground") != std::string::npos ||
                             name.find("grass") != std::string::npos)) {
        return logAndReturn(SurfaceType::Dirt, "burning woods ash");
    }

    // Dreadlands - barren rock
    if (startsWith("dread") || startsWith("drd")) {
        return logAndReturn(SurfaceType::Rock, "dreadlands rock");
    }

    // Field of Bone - bone/dirt ground
    if (startsWith("fob") || name.find("bone") != std::string::npos) {
        return logAndReturn(SurfaceType::Dirt, "field of bone");
    }

    // Default to grass for unknown textures
    // Hard surfaces (cobble, brick, etc.) are usually explicitly named in EQ zones
    return logAndReturn(SurfaceType::Grass, "default/unknown");
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
