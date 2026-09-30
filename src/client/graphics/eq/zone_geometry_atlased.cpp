// zone_geometry_atlased.cpp -- the texture-atlas half of ZoneMeshBuilder.
//
// J32 (Batch J, file-size reclamation) lifted this out of zone_geometry.cpp
// verbatim. It holds the whole feature-conditional region that the atlas macro
// fences off in the original file, and that region MUST NOT be split further:
// six file-local entities (two tessellation structs and four static helpers)
// live inside it together with their only consumer, so any cut between them
// leaves undeclared statics -- and, because the conditional is on in the
// default configuration, a mismatched fence would still compile here and break
// only where the feature is off.
//
// buildAtlasedMesh is 499 lines. It is a recorded Batch-G decomposition
// candidate that J32 deliberately did not touch: this is a move-only commit.

#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/composite_texture_data.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/constrained_texture_cache.h"
#ifdef EQT_HAS_TEXTURE_ATLAS
#include "client/graphics/texture_atlas.h"
#endif
#include "common/logging.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <fstream>

// ===========================================================================
// UV and Coordinate System Notes
// ===========================================================================
// EQ uses a left-handed coordinate system (Z-up), same as DirectX/Irrlicht.
// UV coordinates in EQ follow DirectX convention (origin at top-left, V increases downward).
//
// When comparing to eqsage (which exports to glTF):
// - eqsage negates V coordinates because glTF uses OpenGL convention (origin at bottom-left)
// - eqsage negates X positions because glTF uses right-handed coordinates
// - These transformations are for FORMAT CONVERSION, not bug fixes
//
// For Irrlicht rendering, we do NOT need these transformations because:
// - Irrlicht uses the same UV convention as EQ (DirectX-style, origin top-left)
// - Irrlicht uses the same coordinate handedness as EQ (left-handed)
//
// Character models use flipV because the character model UV data in EQ files
// is stored with a different convention than zone/object geometry.
// ===========================================================================

namespace EQT {
namespace Graphics {

// ============================================================================
// Pre-Tessellation: Split triangles at integer UV boundaries
// ============================================================================

#ifdef EQT_HAS_TEXTURE_ATLAS

// Interpolated vertex for pre-tessellation (position, normal, UV)
struct TessVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

// A triangle of interpolated vertices (result of tessellation)
struct TessTriangle {
    TessVertex v[3];
};

static TessVertex lerpVertex(const TessVertex& a, const TessVertex& b, float t) {
    return {
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z),
        a.nx + t * (b.nx - a.nx),
        a.ny + t * (b.ny - a.ny),
        a.nz + t * (b.nz - a.nz),
        a.u + t * (b.u - a.u),
        a.v + t * (b.v - a.v),
    };
}

// Clip a convex polygon against one half-plane in UV space.
// axis: 0 = clip by U, 1 = clip by V
// clipVal: the integer boundary value
// keepAbove: true = keep vertices where UV >= clipVal, false = keep UV <= clipVal
static std::vector<TessVertex> clipPolygon(const std::vector<TessVertex>& poly,
                                            int axis, float clipVal, bool keepAbove) {
    if (poly.empty()) return {};
    std::vector<TessVertex> out;
    size_t n = poly.size();

    for (size_t i = 0; i < n; ++i) {
        const auto& cur = poly[i];
        const auto& next = poly[(i + 1) % n];

        float curUV = (axis == 0) ? cur.u : cur.v;
        float nextUV = (axis == 0) ? next.u : next.v;

        bool curInside = keepAbove ? (curUV >= clipVal - 1e-6f) : (curUV <= clipVal + 1e-6f);
        bool nextInside = keepAbove ? (nextUV >= clipVal - 1e-6f) : (nextUV <= clipVal + 1e-6f);

        if (curInside) {
            out.push_back(cur);
        }

        if (curInside != nextInside) {
            // Edge crosses the clip line — compute intersection
            float denom = nextUV - curUV;
            if (std::abs(denom) > 1e-8f) {
                float t = (clipVal - curUV) / denom;
                t = std::max(0.0f, std::min(1.0f, t));
                out.push_back(lerpVertex(cur, next, t));
            }
        }
    }
    return out;
}

// Fan-triangulate a convex polygon (3+ vertices) into triangles
static void fanTriangulate(const std::vector<TessVertex>& poly, std::vector<TessTriangle>& out) {
    if (poly.size() < 3) return;
    for (size_t i = 1; i + 1 < poly.size(); ++i) {
        TessTriangle t;
        t.v[0] = poly[0];
        t.v[1] = poly[i];
        t.v[2] = poly[i + 1];
        out.push_back(t);
    }
}

// Split a single triangle at integer UV boundaries so each sub-triangle
// fits within one UV cell [floor, floor+1).
// Returns the list of sub-triangles. For triangles already fitting in one cell,
// returns a single triangle (no allocation overhead from the caller's perspective
// since they check span first).
static std::vector<TessTriangle> splitTriangleToUVCells(const TessVertex& v0,
                                                         const TessVertex& v1,
                                                         const TessVertex& v2) {
    // Find the integer grid lines crossing the triangle's UV bounding box
    float uMin = std::min({v0.u, v1.u, v2.u});
    float uMax = std::max({v0.u, v1.u, v2.u});
    float vMin = std::min({v0.v, v1.v, v2.v});
    float vMax = std::max({v0.v, v1.v, v2.v});

    // Integer boundaries that lie strictly inside the UV range
    int uStart = static_cast<int>(std::floor(uMin)) + 1;
    int uEnd   = static_cast<int>(std::ceil(uMax));
    int vStart = static_cast<int>(std::floor(vMin)) + 1;
    int vEnd   = static_cast<int>(std::ceil(vMax));

    // Start with the original triangle as a polygon
    std::vector<std::vector<TessVertex>> strips;
    strips.push_back({v0, v1, v2});

    // Clip by each integer U line, splitting polygons into vertical strips
    for (int u = uStart; u < uEnd; ++u) {
        float uVal = static_cast<float>(u);
        std::vector<std::vector<TessVertex>> newStrips;
        for (auto& poly : strips) {
            auto left = clipPolygon(poly, 0, uVal, false);
            auto right = clipPolygon(poly, 0, uVal, true);
            if (left.size() >= 3) newStrips.push_back(std::move(left));
            if (right.size() >= 3) newStrips.push_back(std::move(right));
        }
        strips = std::move(newStrips);
    }

    // Clip each strip by each integer V line
    for (int v = vStart; v < vEnd; ++v) {
        float vVal = static_cast<float>(v);
        std::vector<std::vector<TessVertex>> newStrips;
        for (auto& poly : strips) {
            auto below = clipPolygon(poly, 1, vVal, false);
            auto above = clipPolygon(poly, 1, vVal, true);
            if (below.size() >= 3) newStrips.push_back(std::move(below));
            if (above.size() >= 3) newStrips.push_back(std::move(above));
        }
        strips = std::move(newStrips);
    }

    // Fan-triangulate all resulting convex polygons
    std::vector<TessTriangle> result;
    for (auto& poly : strips) {
        fanTriangulate(poly, result);
    }
    return result;
}

// ============================================================================
// Atlas Batched Mesh Building
// ============================================================================

irr::scene::IMesh* ZoneMeshBuilder::buildAtlasedMesh(
    const ZoneGeometry& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    const TextureAtlas& atlas,
    int pageIndexOffset,
    bool deferTextures) {
    missingTextures_.clear();
    fallbackBufferMap_.clear();

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    // Classify each texture index: atlas page or fallback
    // Key: atlas page index (or -1 for non-atlased), Value: triangle indices + tessellated tris
    struct TessTriData {
        TessTriangle tri;
        uint32_t textureIndex;
    };
    struct PageBucket {
        int pageIndex;
        bool hasAlpha;
        uint16_t alphaPageIndex;
        std::vector<size_t> triangleIndices;            // Original tris that fit in one UV cell
        std::vector<TessTriData> tessellatedTriangles;  // Sub-tris from UV cell splitting
    };

    // Map: atlas page index -> PageBucket
    std::map<int, PageBucket> pageBuckets;
    // Non-atlased triangles (animated, missing): keyed by texture index
    std::map<uint32_t, std::vector<size_t>> fallbackTriangles;

    for (size_t i = 0; i < geometry.triangles.size(); ++i) {
        const auto& tri = geometry.triangles[i];
        uint32_t texIdx = tri.textureIndex;

        // Skip invisible/collision-only textures
        if (texIdx < geometry.textureInvisible().size() && geometry.textureInvisible()[texIdx]) {
            std::string texName = (texIdx < geometry.textureNames().size()) ? geometry.textureNames()[texIdx] : "";
            if (texName.empty()) continue;
        }

        if (texIdx >= geometry.textureNames().size()) {
            fallbackTriangles[texIdx].push_back(i);
            continue;
        }

        const std::string& texName = geometry.textureNames()[texIdx];

        // Skip animated textures — they must use per-texture fallback rendering
        if (texIdx < geometry.textureAnimations().size() &&
            geometry.textureAnimations()[texIdx].isAnimated) {
            fallbackTriangles[texIdx].push_back(i);
            continue;
        }

        // Look up in atlas
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        const auto* tile = atlas.lookup(lowerName);
        if (!tile) {
            // Not in atlas — fallback
            fallbackTriangles[texIdx].push_back(i);
            continue;
        }

        // Check if triangle UVs span more than 1 cell in either dimension.
        // If so, pre-tessellate at integer UV boundaries so each sub-triangle
        // fits within one UV cell for correct atlas UV precomputation.
        {
            const auto& v0 = geometry.vertices[tri.v1];
            const auto& v1 = geometry.vertices[tri.v2];
            const auto& v2 = geometry.vertices[tri.v3];
            float uMin = std::min({v0.u, v1.u, v2.u});
            float uMax = std::max({v0.u, v1.u, v2.u});
            float vMin = std::min({v0.v, v1.v, v2.v});
            float vMax = std::max({v0.v, v1.v, v2.v});
            float maxRelU = uMax - std::floor(uMin);
            float maxRelV = vMax - std::floor(vMin);

            int pageIdx = tile->pageIndex;
            auto& bucket = pageBuckets[pageIdx];
            bucket.pageIndex = pageIdx;
            bucket.hasAlpha = tile->hasAlpha;
            bucket.alphaPageIndex = tile->alphaPageIndex;

            if (maxRelU >= 1.0f || maxRelV >= 1.0f) {
                // Triangle spans or touches UV cell boundary — tessellate.
                // Must use >= not > because a vertex at exactly UV=N.0 maps
                // to relU=1.0, which lands in the adjacent atlas tile's padding.
                TessVertex tv0 = {v0.x, v0.y, v0.z, v0.nx, v0.ny, v0.nz, v0.u, v0.v};
                TessVertex tv1 = {v1.x, v1.y, v1.z, v1.nx, v1.ny, v1.nz, v1.u, v1.v};
                TessVertex tv2 = {v2.x, v2.y, v2.z, v2.nx, v2.ny, v2.nz, v2.u, v2.v};
                auto subTris = splitTriangleToUVCells(tv0, tv1, tv2);
                for (auto& st : subTris) {
                    bucket.tessellatedTriangles.push_back({st, texIdx});
                }
            } else {
                // Fits in one cell — use original triangle index
                bucket.triangleIndices.push_back(i);
            }
        }
    }

    {
        size_t totalOriginal = 0, totalTessellated = 0;
        for (const auto& [pageIdx, bucket] : pageBuckets) {
            totalOriginal += bucket.triangleIndices.size();
            totalTessellated += bucket.tessellatedTriangles.size();
        }
        LOG_INFO(MOD_GRAPHICS, "buildAtlasedMesh: {} atlas page buckets ({} original + {} tessellated tris), {} fallback texture groups",
                 pageBuckets.size(), totalOriginal, totalTessellated, fallbackTriangles.size());
    }

    // Log which textures are atlas-batched vs fallback
    {
        std::set<std::string> atlasedNames, fallbackNames;
        for (const auto& [pageIdx, bucket] : pageBuckets) {
            for (size_t triIdx : bucket.triangleIndices) {
                uint32_t texIdx = geometry.triangles[triIdx].textureIndex;
                if (texIdx < geometry.textureNames().size())
                    atlasedNames.insert(geometry.textureNames()[texIdx]);
            }
            for (const auto& td : bucket.tessellatedTriangles) {
                if (td.textureIndex < geometry.textureNames().size())
                    atlasedNames.insert(geometry.textureNames()[td.textureIndex]);
            }
        }
        for (const auto& [texIdx, triIndices] : fallbackTriangles) {
            if (texIdx < geometry.textureNames().size())
                fallbackNames.insert(geometry.textureNames()[texIdx]);
        }
        for (const auto& n : atlasedNames)
            LOG_DEBUG(MOD_GRAPHICS, "  [ATLAS] texture '{}' -> atlas page (pageOffset={})", n, pageIndexOffset);
        for (const auto& n : fallbackNames)
            LOG_DEBUG(MOD_GRAPHICS, "  [FALLBACK] texture '{}' -> per-texture (constrained cache or direct)", n);
    }

    // Build mesh buffers for atlas page buckets using S3DVertex2TCoords
    for (const auto& [pageIdx, bucket] : pageBuckets) {
        if (bucket.triangleIndices.empty() && bucket.tessellatedTriangles.empty()) continue;

        // Get the atlas page GL texture handle
        // When deferTextures is true (background thread), skip this check —
        // the shader callback handles missing pages, and GL textures aren't accessible off-thread.
        if (!deferTextures) {
            uint32_t glTexHandle = atlas.getPageTexture(static_cast<uint16_t>(pageIdx));
            if (glTexHandle == 0) {
                LOG_WARN(MOD_GRAPHICS, "buildAtlasedMesh: No GL texture for atlas page {}", pageIdx);
                continue;
            }
        }

        // Split into sub-buffers for 16-bit index limit
        // Use a simple approach: accumulate vertices, split when exceeding limit
        struct SubBuffer {
            std::vector<irr::video::S3DVertex2TCoords> vertices;
            std::vector<irr::u16> indices;
            std::unordered_map<uint64_t, irr::u16> vertexMap;  // (globalVertIdx << 32 | texIdx) -> local index
        };

        std::vector<SubBuffer> subBuffers;
        subBuffers.emplace_back();

        for (size_t triIdx : bucket.triangleIndices) {
            const auto& tri = geometry.triangles[triIdx];
            uint32_t texIdx = tri.textureIndex;

            // Look up tile info for this triangle's texture
            std::string texName = geometry.textureNames()[texIdx];
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            const auto* tile = atlas.lookup(lowerName);
            if (!tile) continue;  // shouldn't happen, but be safe

            // Check if current sub-buffer has space for 3 more vertices
            auto& sb = subBuffers.back();
            if (sb.vertices.size() + 3 > MAX_VERTICES_PER_BUFFER) {
                subBuffers.emplace_back();
            }
            auto& currentSB = subBuffers.back();

            // Precompute per-triangle UV cell base for atlas UV computation.
            // Using a consistent floor(min) base across all 3 vertices keeps
            // the atlas UVs continuous within the triangle, avoiding the fract()
            // discontinuity that causes warping at polygon edges where UVs cross
            // an integer boundary. Vertices at cell boundaries get duplicated
            // (different atlas UVs for different cells) via the key below.
            const auto& v0 = geometry.vertices[tri.v1];
            const auto& v1 = geometry.vertices[tri.v2];
            const auto& v2 = geometry.vertices[tri.v3];
            int cellU = static_cast<int>(std::floor(std::min({v0.u, v1.u, v2.u})));
            int cellV = static_cast<int>(std::floor(std::min({v0.v, v1.v, v2.v})));
            float tileScale = tile->uvScale;

            // Add vertices
            for (uint32_t vidx : {tri.v1, tri.v2, tri.v3}) {
                // Key: vertex index + texture index + UV cell, so the same vertex
                // at a UV cell boundary gets separate entries per cell
                uint64_t key = (static_cast<uint64_t>(vidx) << 32)
                             | (static_cast<uint64_t>(texIdx) << 16)
                             | (static_cast<uint64_t>(static_cast<uint8_t>(cellU)) << 8)
                             | static_cast<uint64_t>(static_cast<uint8_t>(cellV));

                auto it = currentSB.vertexMap.find(key);
                if (it != currentSB.vertexMap.end()) {
                    currentSB.indices.push_back(it->second);
                } else {
                    const auto& v = geometry.vertices[vidx];
                    irr::video::S3DVertex2TCoords vertex;
                    // Position: EQ Z-up -> Irrlicht Y-up
                    vertex.Pos.X = v.x;
                    vertex.Pos.Y = v.z;
                    vertex.Pos.Z = v.y;
                    vertex.Normal.X = v.nx;
                    vertex.Normal.Y = v.nz;
                    vertex.Normal.Z = v.ny;
                    // TCoords = original UVs (kept for reference, not used by shader)
                    vertex.TCoords.X = v.u;
                    vertex.TCoords.Y = v.v;
                    // TCoords2 = precomputed atlas UV (full precision on CPU).
                    // Uses per-triangle cell base for continuity within the triangle.
                    float relU = v.u - static_cast<float>(cellU);
                    float relV = v.v - static_cast<float>(cellV);
                    vertex.TCoords2.X = tile->uvOffsetU + relU * tileScale;
                    vertex.TCoords2.Y = tile->uvOffsetV + relV * tileScale;
                    vertex.Color = irr::video::SColor(255, 255, 255, 255);

                    irr::u16 localIdx = static_cast<irr::u16>(currentSB.vertices.size());
                    currentSB.vertexMap[key] = localIdx;
                    currentSB.vertices.push_back(vertex);
                    currentSB.indices.push_back(localIdx);
                }
            }
        }

        // Emit tessellated sub-triangles (interpolated vertices, no dedup)
        for (const auto& td : bucket.tessellatedTriangles) {
            uint32_t texIdx = td.textureIndex;
            std::string texName = geometry.textureNames()[texIdx];
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            const auto* tile = atlas.lookup(lowerName);
            if (!tile) continue;

            // Check sub-buffer space for 3 vertices
            auto& sb = subBuffers.back();
            if (sb.vertices.size() + 3 > MAX_VERTICES_PER_BUFFER) {
                subBuffers.emplace_back();
            }
            auto& currentSB = subBuffers.back();

            // Each tessellated sub-triangle fits within one UV cell
            int cellU = static_cast<int>(std::floor(std::min({td.tri.v[0].u, td.tri.v[1].u, td.tri.v[2].u})));
            int cellV = static_cast<int>(std::floor(std::min({td.tri.v[0].v, td.tri.v[1].v, td.tri.v[2].v})));
            float tileScale = tile->uvScale;

            for (int vi = 0; vi < 3; ++vi) {
                const auto& tv = td.tri.v[vi];
                irr::video::S3DVertex2TCoords vertex;
                // Position: EQ Z-up -> Irrlicht Y-up
                vertex.Pos.X = tv.x;
                vertex.Pos.Y = tv.z;
                vertex.Pos.Z = tv.y;
                vertex.Normal.X = tv.nx;
                vertex.Normal.Y = tv.nz;
                vertex.Normal.Z = tv.ny;
                vertex.TCoords.X = tv.u;
                vertex.TCoords.Y = tv.v;
                // Precomputed atlas UV
                float relU = tv.u - static_cast<float>(cellU);
                float relV = tv.v - static_cast<float>(cellV);
                vertex.TCoords2.X = tile->uvOffsetU + relU * tileScale;
                vertex.TCoords2.Y = tile->uvOffsetV + relV * tileScale;
                vertex.Color = irr::video::SColor(255, 255, 255, 255);

                irr::u16 localIdx = static_cast<irr::u16>(currentSB.vertices.size());
                currentSB.vertices.push_back(vertex);
                currentSB.indices.push_back(localIdx);
            }
        }

        // Create Irrlicht mesh buffers from sub-buffers
        for (auto& sb : subBuffers) {
            if (sb.indices.empty()) continue;

            auto* buffer = new irr::scene::SMeshBufferLightMap();

            // Set material — atlas shader
            if (bucket.hasAlpha && shaderMaterialAtlasAlpha_ >= 0) {
                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAtlasAlpha_);
            } else if (shaderMaterialAtlasSolid_ >= 0) {
                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAtlasSolid_);
            } else {
                buffer->Material.MaterialType = irr::video::EMT_SOLID;
            }

            buffer->Material.BackfaceCulling = false;
            buffer->Material.Lighting = false;
            buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
            // Atlas textures use CLAMP_TO_EDGE (tiling is done in shader via fract())
            buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
            buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;

            // We can't set a raw GL texture handle as an Irrlicht ITexture*.
            // The atlas page textures are set via the shader callback using glBindTexture directly.
            // Store the page index in the material's user data for the shader callback.
            // Use MaterialTypeParam to pass atlas page index to shader callback.
            // pageIndexOffset shifts indices when multiple atlases share the same page texture array.
            buffer->Material.MaterialTypeParam = static_cast<float>(pageIdx + pageIndexOffset);

            // If we also have an alpha page, store it in MaterialTypeParam2
            if (bucket.hasAlpha) {
                buffer->Material.MaterialTypeParam2 = static_cast<float>(bucket.alphaPageIndex + pageIndexOffset);
            }

            // Copy vertices and indices
            buffer->Vertices.set_used(static_cast<irr::u32>(sb.vertices.size()));
            std::memcpy(buffer->Vertices.pointer(), sb.vertices.data(),
                        sb.vertices.size() * sizeof(irr::video::S3DVertex2TCoords));

            buffer->Indices.set_used(static_cast<irr::u32>(sb.indices.size()));
            std::memcpy(buffer->Indices.pointer(), sb.indices.data(),
                        sb.indices.size() * sizeof(irr::u16));

            buffer->recalculateBoundingBox();
            mesh->addMeshBuffer(buffer);
            buffer->drop();
        }
    }

    // Build fallback buffers for non-atlased triangles (same as buildTexturedMesh)
    for (const auto& [texIdx, triIndices] : fallbackTriangles) {
        if (triIndices.empty()) continue;

        std::string texName;
        std::string lowerTexName;
        irr::video::ITexture* texture = nullptr;

        if (texIdx < geometry.textureNames().size()) {
            texName = geometry.textureNames()[texIdx];
            if (!texName.empty()) {
                lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (!deferTextures) {
                    // Synchronous path: load texture now
                    auto texIt = textures.find(lowerTexName);
                    if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                        const TextureOrigin origin = texIt->second->origin();  // B947
                        texture = loadTextureFromBMP(texName, texIt->second->data, false, &origin);
                    }
                    if (!texture && constrainedCache_) {
                        texture = constrainedCache_->getTexture(lowerTexName);
                        if (!texture) texture = constrainedCache_->getTexture(texName);
                    }
                }

                // Record textures that resolved to null (async GPU upload still pending, or deferred)
                if (!texture && !lowerTexName.empty()) {
                    missingTextures_.push_back(lowerTexName);
                }
            }
        }

        // Build sub-buffers (same splitting logic as buildTexturedMesh)
        // Key includes UV cell base for FP16 precision (Mali 400 GLES2)
        std::unordered_map<uint64_t, irr::u16> globalToLocal;
        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Helper lambda to set up fallback buffer material
        auto setupFallbackMaterial = [&](irr::scene::SMeshBuffer* buf) {
            buf->Material.BackfaceCulling = false;
            buf->Material.Lighting = false;
            if (texture) {
                buf->Material.setTexture(0, texture);
                bool isMaskedMaterial = (texIdx < geometry.textureMasked().size()) && geometry.textureMasked()[texIdx];
                bool hasAlpha = isMaskedMaterial ||
                    (texturesWithAlpha_.find(texName) != texturesWithAlpha_.end());
                if (hasAlpha) {
                    if (shaderMaterialAlphaTest_ >= 0) {
                        buf->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                    } else {
                        buf->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                    }
                    // Bind ETC1 alpha companion on layer 1
                    if (constrainedCache_) {
                        irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(texName);
                        if (alphaTex) buf->Material.setTexture(1, alphaTex);
                    }
                } else {
                    if (shaderMaterialSolid_ >= 0) {
                        buf->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    } else {
                        buf->Material.MaterialType = irr::video::EMT_SOLID;
                    }
                }
                buf->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                buf->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                buf->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
            } else if (deferTextures) {
                // Deferred mode: use solid shader, no texture assigned yet
                if (shaderMaterialSolid_ >= 0) {
                    buf->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                } else {
                    buf->Material.MaterialType = irr::video::EMT_SOLID;
                }
                buf->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                buf->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                buf->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
            }
        };

        setupFallbackMaterial(buffer);
        // Track initial buffer index for fallback recording
        irr::u32 fallbackBufferStartIdx = mesh->getMeshBufferCount();

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];

            // Check if we need a new buffer
            if (buffer->Vertices.size() + 3 > MAX_VERTICES_PER_BUFFER) {
                buffer->recalculateBoundingBox();
                mesh->addMeshBuffer(buffer);
                // Record fallback buffer info for deferred texture assignment
                if (deferTextures && !texture && !lowerTexName.empty()) {
                    fallbackBufferMap_.push_back({mesh->getMeshBufferCount() - 1, lowerTexName});
                }
                buffer->drop();
                buffer = new irr::scene::SMeshBuffer();
                setupFallbackMaterial(buffer);
                globalToLocal.clear();
            }

            // Per-triangle UV cell basing for FP16 varying precision (Mali 400 GLES2).
            // Subtract integer part so the UV varying stays in 0-1 range.
            // GL_REPEAT wrapping makes this visually identical.
            const auto& tv0 = geometry.vertices[tri.v1];
            const auto& tv1 = geometry.vertices[tri.v2];
            const auto& tv2 = geometry.vertices[tri.v3];
            int cellU = static_cast<int>(std::floor(std::min({tv0.u, tv1.u, tv2.u})));
            int cellV = static_cast<int>(std::floor(std::min({tv0.v, tv1.v, tv2.v})));

            for (uint32_t vidx : {tri.v1, tri.v2, tri.v3}) {
                uint64_t key = (static_cast<uint64_t>(vidx) << 16)
                             | (static_cast<uint64_t>(static_cast<uint8_t>(cellU)) << 8)
                             | static_cast<uint64_t>(static_cast<uint8_t>(cellV));
                auto it = globalToLocal.find(key);
                if (it != globalToLocal.end()) {
                    buffer->Indices.push_back(it->second);
                } else {
                    const auto& v = geometry.vertices[vidx];
                    irr::video::S3DVertex vertex;
                    vertex.Pos.X = v.x;
                    vertex.Pos.Y = v.z;
                    vertex.Pos.Z = v.y;
                    vertex.Normal.X = v.nx;
                    vertex.Normal.Y = v.nz;
                    vertex.Normal.Z = v.ny;
                    vertex.TCoords.X = v.u - static_cast<float>(cellU);
                    vertex.TCoords.Y = v.v - static_cast<float>(cellV);
                    // In deferred mode with no texture: white vertices (placeholder)
                    vertex.Color = (texture || deferTextures) ? irr::video::SColor(255, 255, 255, 255)
                                           : heightToColor(v.z, geometry.minZ, geometry.maxZ);

                    irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                    globalToLocal[key] = localIdx;
                    buffer->Vertices.push_back(vertex);
                    buffer->Indices.push_back(localIdx);
                }
            }
        }

        if (!buffer->Indices.empty()) {
            buffer->recalculateBoundingBox();
            mesh->addMeshBuffer(buffer);
            // Record final fallback buffer info
            if (deferTextures && !texture && !lowerTexName.empty()) {
                fallbackBufferMap_.push_back({mesh->getMeshBufferCount() - 1, lowerTexName});
            }
        }
        buffer->drop();
    }

    mesh->recalculateBoundingBox();

    int totalBuffers = mesh->getMeshBufferCount();
    LOG_INFO(MOD_GRAPHICS, "buildAtlasedMesh: {} total mesh buffers (atlas pages: {}, fallback: {})",
             totalBuffers, pageBuckets.size(), fallbackTriangles.size());

    return mesh;
}
#endif // EQT_HAS_TEXTURE_ATLAS

} // namespace Graphics
} // namespace EQT
