// Part 1 of 2: the non-atlased ZoneMeshBuilder path. The texture-atlas
// conditional region and buildAtlasedMesh live in zone_geometry_atlased.cpp (J32).
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

ZoneMeshBuilder::ZoneMeshBuilder(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                                 irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
}

irr::video::SColor heightToColor(float height, float minHeight, float maxHeight) {
    float range = maxHeight - minHeight;
    if (range < 0.001f) range = 1.0f;

    float normalized = (height - minHeight) / range;
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    // Color gradient from blue (low) through green to red (high)
    irr::u8 r, g, b;
    if (normalized < 0.5f) {
        float t = normalized * 2.0f;
        r = static_cast<irr::u8>(0);
        g = static_cast<irr::u8>(t * 255);
        b = static_cast<irr::u8>((1.0f - t) * 255);
    } else {
        float t = (normalized - 0.5f) * 2.0f;
        r = static_cast<irr::u8>(t * 255);
        g = static_cast<irr::u8>((1.0f - t) * 255);
        b = static_cast<irr::u8>(0);
    }

    return irr::video::SColor(255, r, g, b);
}

irr::video::SColor normalToColor(float nx, float ny, float nz) {
    // Map normal components from [-1,1] to [0,255]
    irr::u8 r = static_cast<irr::u8>((nx * 0.5f + 0.5f) * 255);
    irr::u8 g = static_cast<irr::u8>((ny * 0.5f + 0.5f) * 255);
    irr::u8 b = static_cast<irr::u8>((nz * 0.5f + 0.5f) * 255);
    return irr::video::SColor(255, r, g, b);
}

// Relocated verbatim (T67, 2026-08-11) from
// ConstrainedMeshCache::estimateMeshSize() -- see this file's header
// declaration and src/client/graphics/CLAUDE.md for the reference pointer
// to the removed cache's implementation history. Calculation unchanged.
size_t estimateMeshSizeBytes(irr::scene::IMeshSceneNode* node) {
    if (!node || !node->getMesh()) return 0;

    size_t total = 0;
    auto* mesh = node->getMesh();
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        auto* buf = mesh->getMeshBuffer(i);
        if (buf) {
            total += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
            total += buf->getIndexCount() * sizeof(uint16_t);
        }
    }
    return total;
}

irr::scene::IMesh* ZoneMeshBuilder::buildMesh(const ZoneGeometry& geometry) {
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Maximum vertices per buffer (16-bit index limit)
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    // Group triangles by which vertex range they fall into
    // Build multiple mesh buffers if needed
    std::vector<std::vector<size_t>> bufferTriangles;
    std::vector<size_t> vertexToBuffer(geometry.vertices.size(), SIZE_MAX);
    std::vector<size_t> bufferVertexCount;

    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        const auto& tri = geometry.triangles[i];

        // Find which buffer this triangle should go in
        // based on its vertices
        size_t maxVertIdx = std::max({tri.v1, tri.v2, tri.v3});
        size_t bufferIdx = maxVertIdx / MAX_VERTICES_PER_BUFFER;

        // Ensure we have enough buffer entries
        while (bufferTriangles.size() <= bufferIdx) {
            bufferTriangles.push_back(std::vector<size_t>());
            bufferVertexCount.push_back(0);
        }

        bufferTriangles[bufferIdx].push_back(i);
    }

    // Now build a mesh buffer for each group
    for (size_t bufIdx = 0; bufIdx < bufferTriangles.size(); bufIdx++) {
        if (bufferTriangles[bufIdx].empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Track which vertices we've added to this buffer
        std::unordered_map<size_t, irr::u16> globalToLocal;

        for (size_t triIdx : bufferTriangles[bufIdx]) {
            const auto& tri = geometry.triangles[triIdx];

            // Add vertices if not already present
            for (size_t vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    const auto& v = geometry.vertices[vidx];
                    irr::video::S3DVertex vertex;
                    vertex.Pos.X = v.x;
                    vertex.Pos.Y = v.z;  // EQ uses Z-up, Irrlicht uses Y-up
                    vertex.Pos.Z = v.y;
                    vertex.Normal.X = v.nx;
                    vertex.Normal.Y = v.nz;
                    vertex.Normal.Z = v.ny;
                    vertex.TCoords.X = v.u;
                    vertex.TCoords.Y = v.v;
                    vertex.Color = irr::video::SColor(255, 200, 200, 200);

                    globalToLocal[vidx] = static_cast<irr::u16>(buffer->Vertices.size());
                    buffer->Vertices.push_back(vertex);
                }
            }

            // Add indices
            buffer->Indices.push_back(globalToLocal[tri.v1]);
            buffer->Indices.push_back(globalToLocal[tri.v2]);
            buffer->Indices.push_back(globalToLocal[tri.v3]);
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

irr::scene::IMesh* ZoneMeshBuilder::buildColoredMesh(const ZoneGeometry& geometry) {
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Find height range for coloring
    float minZ = geometry.minZ;
    float maxZ = geometry.maxZ;

    // Maximum vertices per buffer (16-bit index limit)
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    // Group triangles by which vertex range they fall into
    std::vector<std::vector<size_t>> bufferTriangles;

    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        const auto& tri = geometry.triangles[i];

        // Find which buffer this triangle should go in
        size_t maxVertIdx = std::max({tri.v1, tri.v2, tri.v3});
        size_t bufferIdx = maxVertIdx / MAX_VERTICES_PER_BUFFER;

        // Ensure we have enough buffer entries
        while (bufferTriangles.size() <= bufferIdx) {
            bufferTriangles.push_back(std::vector<size_t>());
        }

        bufferTriangles[bufferIdx].push_back(i);
    }

    // Build a mesh buffer for each group
    for (size_t bufIdx = 0; bufIdx < bufferTriangles.size(); bufIdx++) {
        if (bufferTriangles[bufIdx].empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Track which vertices we've added to this buffer
        std::unordered_map<size_t, irr::u16> globalToLocal;

        for (size_t triIdx : bufferTriangles[bufIdx]) {
            const auto& tri = geometry.triangles[triIdx];

            // Add vertices if not already present
            for (size_t vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    const auto& v = geometry.vertices[vidx];
                    irr::video::S3DVertex vertex;
                    vertex.Pos.X = v.x;
                    vertex.Pos.Y = v.z;  // EQ uses Z-up, Irrlicht uses Y-up
                    vertex.Pos.Z = v.y;
                    vertex.Normal.X = v.nx;
                    vertex.Normal.Y = v.nz;
                    vertex.Normal.Z = v.ny;
                    vertex.TCoords.X = v.u;
                    vertex.TCoords.Y = v.v;

                    // Color based on height
                    vertex.Color = heightToColor(v.z, minZ, maxZ);

                    globalToLocal[vidx] = static_cast<irr::u16>(buffer->Vertices.size());
                    buffer->Vertices.push_back(vertex);
                }
            }

            // Add indices
            buffer->Indices.push_back(globalToLocal[tri.v1]);
            buffer->Indices.push_back(globalToLocal[tri.v2]);
            buffer->Indices.push_back(globalToLocal[tri.v3]);
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

irr::video::ITexture* ZoneMeshBuilder::loadTextureFromBMP(const std::string& name,
                                                          const std::vector<char>& data,
                                                          bool applyPaletteMask) {
    // Note: fileSystem_ is not actually used in this function - textures are loaded
    // from raw byte data, not from the filesystem. Only driver_ is required.
    if (data.empty() || !driver_) {
        return nullptr;
    }

    // Route through constrained texture cache: decode + upload
    if (constrainedCache_) {
        // getOrLoad decodes to RGBA (no GL). Returns ITexture* if already uploaded.
        // P13: tag with this builder's upload category (set via setUploadCategory,
        // "zone_region" or "object" — see renderer_zone_load.cpp step4/step6).
        // P50: applyPaletteMask threaded through to getOrLoad()/uploadTexture()
        // (P46's pattern, previously only wired to T05 and
        // equipment_model_loader.cpp's own direct constrainedCache_ calls —
        // this call site is ZoneMeshBuilder's general-purpose one, used by
        // mesh_building.cpp's composite-deferred on-demand fallback and
        // animated_mesh_creation.cpp's equipment texture-variant swap).
        irr::video::ITexture* texture = constrainedCache_->getOrLoad(name, data, uploadCategory_, "loadTextureFromBMP", applyPaletteMask);
        if (!texture) {
            // Not yet uploaded — upload now (requires GL context on this thread)
            texture = constrainedCache_->uploadTexture(name, applyPaletteMask);
        }
        if (texture) {
            textureCache_[name] = texture;
            // P50: pass applyPaletteMask through — a masked-variant entry is
            // stored under the mangled cacheKey() and a plain-name hasAlpha()
            // query would always miss it. In practice this OR's into a signal
            // (texturesWithAlpha_) that is itself always dominated by the
            // WLD-derived textureMasked() flag at mesh-build time (see the
            // isMaskedMaterial OR below), so this fix is correctness/hygiene,
            // not a behavior change — see this unit's Review.
            if (constrainedCache_->hasAlpha(name, applyPaletteMask)) {
                texturesWithAlpha_.insert(name);
            }
            return texture;
        }
        LOG_WARN(MOD_GRAPHICS, "Constrained cache failed for '{}' ({} bytes), decode or upload error", name, data.size());
        return nullptr;
    }

    // T14: No constrained cache — cannot load texture.
    LOG_FATAL(MOD_GRAPHICS, "loadTextureFromBMP: no constrained cache for '{}' ({} bytes)", name, data.size());
    return nullptr;
}

irr::scene::IMesh* ZoneMeshBuilder::buildTexturedMesh(
    const ZoneGeometry& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    bool flipV) {
    missingTextures_.clear();

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    // If no textures available, fall back to colored mesh
    if (textures.empty() || geometry.textureNames().empty()) {
        return buildColoredMesh(geometry);
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Maximum vertices per buffer (16-bit index limit)
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    // Find height range for coloring fallback
    float minZ = geometry.minZ;
    float maxZ = geometry.maxZ;

    // Group triangles by texture index
    std::map<uint32_t, std::vector<size_t>> trianglesByTexture;
    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        trianglesByTexture[geometry.triangles[i].textureIndex].push_back(i);
    }

    // Build a mesh buffer for each texture
    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        // Check if this texture is marked as invisible (collision-only / sky)
        bool isInvisible = (texIdx < geometry.textureInvisible().size()) && geometry.textureInvisible()[texIdx];
        std::string texNameForLog = (texIdx < geometry.textureNames().size()) ? geometry.textureNames()[texIdx] : "";
        if (isInvisible) {
            // Only skip if it's truly a collision-only material (empty texture name)
            if (texNameForLog.empty()) {
                continue;
            }
        }

        // Get texture name for this index
        std::string texName;
        irr::video::ITexture* texture = nullptr;

        if (texIdx < geometry.textureNames().size()) {
            texName = geometry.textureNames()[texIdx];
            if (!texName.empty()) {
                // Convert to lowercase for lookup (textures stored with lowercase keys)
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Try to load texture
                auto texIt = textures.find(lowerTexName);
                if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                    texture = loadTextureFromBMP(texName, texIt->second->data);
                    if (!texture) {
                        // T92: was LOG_DEBUG — the sole reason zone-geometry
                        // texture failures (the magenta/checkerboard wall and
                        // floor defect's actual failure signal, if any) have
                        // never been named: model-loading failures already
                        // log at LOG_WARN, but this, the analogous failure
                        // for zone geometry, was whispered at DEBUG and
                        // suppressed at any production log level. Raised to
                        // WARN with the texture name so a normal qeynos2 run
                        // reports it.
                        LOG_WARN(MOD_GRAPHICS, "buildTexturedMesh: loadTextureFromBMP returned null for '{}' ({} bytes)",
                            texName, texIt->second->data.size());
                    }
                } else if (texIt == textures.end()) {
                    LOG_DEBUG(MOD_GRAPHICS, "buildTexturedMesh: texture '{}' not found in textures map ({} entries)",
                        lowerTexName, textures.size());
                } else if (texIt->second && texIt->second->data.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "buildTexturedMesh: texture '{}' has empty data", lowerTexName);
                }

                if (!texture && constrainedCache_) {
                    // Pixel data may have been released after initial load;
                    // check the constrained cache which retains GPU textures
                    texture = constrainedCache_->getTexture(lowerTexName);
                    if (!texture) {
                        texture = constrainedCache_->getTexture(texName);
                    }
                    // Track alpha from cache (loadTextureFromBMP was skipped due to empty data)
                    if (texture) {
                        if (constrainedCache_->hasAlpha(lowerTexName) || constrainedCache_->hasAlpha(texName)) {
                            texturesWithAlpha_.insert(texName);
                        }
                    }
                }

                // Record textures that resolved to null (async GPU upload still pending)
                if (!texture && !lowerTexName.empty()) {
                    missingTextures_.push_back(lowerTexName);
                }
            }
        }

        // Split into sub-buffers if needed for 16-bit index limit
        std::vector<std::vector<size_t>> subBuffers;
        std::unordered_map<size_t, size_t> vertexToSubBuffer;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];
            size_t maxVertIdx = std::max({tri.v1, tri.v2, tri.v3});
            size_t bufferIdx = maxVertIdx / MAX_VERTICES_PER_BUFFER;

            while (subBuffers.size() <= bufferIdx) {
                subBuffers.push_back(std::vector<size_t>());
            }
            subBuffers[bufferIdx].push_back(triIdx);
        }

        // Build mesh buffer for each sub-buffer
        for (const auto& subTriIndices : subBuffers) {
            if (subTriIndices.empty()) continue;

            irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

            // Set material
            buffer->Material.MaterialType = irr::video::EMT_SOLID;
            buffer->Material.BackfaceCulling = false;  // Disabled to show both sides of polygons
            buffer->Material.Lighting = false;  // Use vertex/texture colors

            if (texture) {
                buffer->Material.setTexture(0, texture);

                // Check if this texture has alpha transparency. Post-decode pixel
                // inspection (texturesWithAlpha_) alone misses masked materials
                // whose alpha is baked into an opaque-compressed source (e.g. DXT1
                // with no punch-through blocks actually used) — the WLD Fragment
                // 0x30 materialType (TransparentMasked = 0x13) is the ground-truth
                // signal for that, so OR it in regardless of what decode found.
                bool isMaskedMaterial = (texIdx < geometry.textureMasked().size()) && geometry.textureMasked()[texIdx];
                bool hasAlpha = isMaskedMaterial ||
                    (texturesWithAlpha_.find(texName) != texturesWithAlpha_.end());
                if (hasAlpha) {
                    // H02d (Option A): alpha-source-aware shader selection.
                    // ETC1-sourced textures carry a companion alpha texture on
                    // unit 1 (uAlphaTexture) — use the existing companion
                    // variant, unchanged. The direct RGBA/DDS decode path has
                    // real alpha in the main texture's own alpha channel and
                    // never builds a companion; using the companion variant
                    // there reads whatever (or nothing) happens to be bound on
                    // unit 1 (the driver's opaque-white fallback), which never
                    // discards — the RGBA variant reads uTexture.a directly
                    // instead, matching desktop-GL's known-good
                    // EMT_TRANSPARENT_ALPHA_CHANNEL_REF semantics.
                    bool isETC1 = constrainedCache_ && constrainedCache_->isETC1Texture(texName);
                    if (isETC1) {
                        // Use GLSL shader alpha-test material if available, else fixed-function
                        if (shaderMaterialAlphaTest_ >= 0) {
                            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                        } else {
                            buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                        }
                        // Bind ETC1 alpha companion texture on layer 1 (sampled by uAlphaTexture in shader)
                        if (constrainedCache_) {
                            irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(texName);
                            if (alphaTex) {
                                buffer->Material.setTexture(1, alphaTex);
                            }
                        }
                    } else {
                        // Direct RGBA/DDS path: no companion exists or is needed.
                        if (shaderMaterialAlphaTestRGBA_ >= 0) {
                            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
                        } else {
                            buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                        }
                    }
                    // Alpha-to-coverage produces smoother edges on vegetation when MSAA is active
                    if (constrainedCache_ && constrainedCache_->getConfig().enableAlphaToCoverage) {
                        buffer->Material.AntiAliasing |= irr::video::EAAM_ALPHA_TO_COVERAGE;
                    }
                } else {
                    // Use GLSL shader solid material if available, else fixed-function
                    if (shaderMaterialSolid_ >= 0) {
                        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    } else {
                        buffer->Material.MaterialType = irr::video::EMT_SOLID;
                    }
                }

                // Texture filtering: bilinear
                buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                buffer->Material.setFlag(irr::video::EMF_TRILINEAR_FILTER, false);
                buffer->Material.setFlag(irr::video::EMF_ANISOTROPIC_FILTER, false);
                // Enable texture wrapping/tiling for UV coords > 1
                buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
            }

            // Track which vertices we've added to this buffer
            // Key includes UV cell base so vertices at cell boundaries get separate entries
            std::unordered_map<uint64_t, irr::u16> globalToLocal;

            for (size_t triIdx : subTriIndices) {
                const auto& tri = geometry.triangles[triIdx];

                // Per-triangle UV cell basing for FP16 varying precision (Mali 400 GLES2).
                // Subtract integer part so the UV varying stays in 0-1 range.
                // GL_REPEAT wrapping makes this visually identical.
                const auto& tv0 = geometry.vertices[tri.v1];
                const auto& tv1 = geometry.vertices[tri.v2];
                const auto& tv2 = geometry.vertices[tri.v3];
                float fv0 = flipV ? (1.0f - tv0.v) : tv0.v;
                float fv1 = flipV ? (1.0f - tv1.v) : tv1.v;
                float fv2 = flipV ? (1.0f - tv2.v) : tv2.v;
                int cellU = static_cast<int>(std::floor(std::min({tv0.u, tv1.u, tv2.u})));
                int cellV = static_cast<int>(std::floor(std::min({fv0, fv1, fv2})));

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
                        vertex.Pos.Y = v.z;  // EQ uses Z-up, Irrlicht uses Y-up
                        vertex.Pos.Z = v.y;
                        vertex.Normal.X = v.nx;
                        vertex.Normal.Y = v.nz;
                        vertex.Normal.Z = v.ny;
                        vertex.TCoords.X = v.u - static_cast<float>(cellU);
                        vertex.TCoords.Y = (flipV ? (1.0f - v.v) : v.v) - static_cast<float>(cellV);

                        // White color if textured, height-based if not
                        if (texture) {
                            vertex.Color = irr::video::SColor(255, 255, 255, 255);
                        } else {
                            vertex.Color = heightToColor(v.z, minZ, maxZ);
                        }

                        irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                        globalToLocal[key] = localIdx;
                        buffer->Vertices.push_back(vertex);
                        buffer->Indices.push_back(localIdx);
                    }
                }
            }

            buffer->recalculateBoundingBox();
            mesh->addMeshBuffer(buffer);
            buffer->drop();
        }
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

// ============================================================================
// Pre-uploaded Texture Mesh Building (for multi-frame entity pipeline)
// ============================================================================

irr::scene::IMesh* ZoneMeshBuilder::buildTexturedMeshFromUploaded(
    const ZoneGeometry& geometry,
    const std::vector<irr::video::ITexture*>& textures,
    const std::vector<bool>& textureAlpha,
    bool flipV) {

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    float minZ = geometry.minZ;
    float maxZ = geometry.maxZ;

    // Group triangles by texture index
    std::map<uint32_t, std::vector<size_t>> trianglesByTexture;
    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        trianglesByTexture[geometry.triangles[i].textureIndex].push_back(i);
    }

    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        bool isInvisible = (texIdx < geometry.textureInvisible().size()) && geometry.textureInvisible()[texIdx];
        if (isInvisible) {
            std::string texName = (texIdx < geometry.textureNames().size()) ? geometry.textureNames()[texIdx] : "";
            if (texName.empty()) continue;
        }

        // Look up pre-uploaded texture
        irr::video::ITexture* texture = (texIdx < textures.size()) ? textures[texIdx] : nullptr;
        bool hasAlpha = (texIdx < textureAlpha.size()) ? textureAlpha[texIdx] : false;
        // H02d: texture name needed to query the cache's alpha-source (ETC1
        // companion vs direct RGBA) for shader variant selection below.
        std::string texName = (texIdx < geometry.textureNames().size()) ? geometry.textureNames()[texIdx] : "";

        // Split into sub-buffers for 16-bit index limit
        std::vector<std::vector<size_t>> subBuffers;
        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];
            size_t maxVertIdx = std::max({tri.v1, tri.v2, tri.v3});
            size_t bufferIdx = maxVertIdx / MAX_VERTICES_PER_BUFFER;
            while (subBuffers.size() <= bufferIdx) {
                subBuffers.push_back(std::vector<size_t>());
            }
            subBuffers[bufferIdx].push_back(triIdx);
        }

        for (const auto& subTriIndices : subBuffers) {
            if (subTriIndices.empty()) continue;

            irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

            buffer->Material.MaterialType = irr::video::EMT_SOLID;
            buffer->Material.BackfaceCulling = false;
            buffer->Material.Lighting = false;

            if (texture) {
                buffer->Material.setTexture(0, texture);
                if (hasAlpha) {
                    // H02d (Option A): see buildTexturedMesh for rationale.
                    bool isETC1 = constrainedCache_ && !texName.empty() && constrainedCache_->isETC1Texture(texName);
                    if (isETC1) {
                        if (shaderMaterialAlphaTest_ >= 0) {
                            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                        } else {
                            buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                        }
                        if (constrainedCache_) {
                            irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(texName);
                            if (alphaTex) {
                                buffer->Material.setTexture(1, alphaTex);
                            }
                        }
                    } else {
                        if (shaderMaterialAlphaTestRGBA_ >= 0) {
                            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
                        } else {
                            buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                        }
                    }
                } else {
                    if (shaderMaterialSolid_ >= 0) {
                        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    }
                }
                buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                buffer->Material.setFlag(irr::video::EMF_TRILINEAR_FILTER, false);
                buffer->Material.setFlag(irr::video::EMF_ANISOTROPIC_FILTER, false);
                buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
            }

            std::unordered_map<uint64_t, irr::u16> globalToLocal;

            for (size_t triIdx : subTriIndices) {
                const auto& tri = geometry.triangles[triIdx];

                const auto& tv0 = geometry.vertices[tri.v1];
                const auto& tv1 = geometry.vertices[tri.v2];
                const auto& tv2 = geometry.vertices[tri.v3];
                float fv0 = flipV ? (1.0f - tv0.v) : tv0.v;
                float fv1 = flipV ? (1.0f - tv1.v) : tv1.v;
                float fv2 = flipV ? (1.0f - tv2.v) : tv2.v;
                int cellU = static_cast<int>(std::floor(std::min({tv0.u, tv1.u, tv2.u})));
                int cellV = static_cast<int>(std::floor(std::min({fv0, fv1, fv2})));

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
                        vertex.TCoords.Y = (flipV ? (1.0f - v.v) : v.v) - static_cast<float>(cellV);
                        vertex.Color = texture
                            ? irr::video::SColor(255, 255, 255, 255)
                            : heightToColor(v.z, minZ, maxZ);

                        irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                        globalToLocal[key] = localIdx;
                        buffer->Vertices.push_back(vertex);
                        buffer->Indices.push_back(localIdx);
                    }
                }
            }

            buffer->recalculateBoundingBox();
            mesh->addMeshBuffer(buffer);
            buffer->drop();
        }
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

// ============================================================================
// Composite Texture Mesh Building (single-buffer, remapped UVs)
// ============================================================================

irr::scene::IMesh* ZoneMeshBuilder::buildCompositeMesh(
    const ZoneGeometry& geometry,
    irr::video::ITexture* compositeTexture,
    const CompositeTextureData& compositeData,
    bool hasAlpha,
    bool flipV) {

    if (geometry.vertices.empty() || geometry.triangles.empty() || !compositeTexture) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    // Material setup: single composite texture, CLAMP_TO_EDGE (tiling is pre-baked)
    buffer->Material.setTexture(0, compositeTexture);
    buffer->Material.BackfaceCulling = false;
    buffer->Material.Lighting = false;
    if (hasAlpha) {
        // H02d (Option A): see buildTexturedMesh for rationale. No single
        // originating texture name exists here (compositeTexture merges many
        // source textures), so use the pointer-based reverse lookup.
        bool isETC1 = constrainedCache_ && constrainedCache_->isETC1Texture(compositeTexture);
        if (isETC1) {
            if (shaderMaterialAlphaTest_ >= 0) {
                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
            } else {
                buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
            }
            if (constrainedCache_) {
                irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(compositeTexture);
                if (alphaTex) {
                    buffer->Material.setTexture(1, alphaTex);
                }
            }
        } else {
            if (shaderMaterialAlphaTestRGBA_ >= 0) {
                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
            } else {
                buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
            }
        }
    } else {
        if (shaderMaterialSolid_ >= 0) {
            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
        } else {
            buffer->Material.MaterialType = irr::video::EMT_SOLID;
        }
    }
    buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
    buffer->Material.setFlag(irr::video::EMF_TRILINEAR_FILTER, false);
    buffer->Material.setFlag(irr::video::EMF_ANISOTROPIC_FILTER, false);
    buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
    buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;

    float minZ = geometry.minZ;
    float maxZ = geometry.maxZ;

    // Track unmapped textures for fallback info
    size_t unmappedTriangles = 0;

    // Build a single vertex/index buffer with all triangles, remapping UVs
    std::unordered_map<uint64_t, irr::u16> globalToLocal;

    for (size_t i = 0; i < geometry.triangles.size(); ++i) {
        const auto& tri = geometry.triangles[i];

        // Skip invisible textures
        uint32_t texIdx = tri.textureIndex;
        bool isInvisible = (texIdx < geometry.textureInvisible().size()) && geometry.textureInvisible()[texIdx];
        if (isInvisible) {
            std::string texName = (texIdx < geometry.textureNames().size()) ? geometry.textureNames()[texIdx] : "";
            if (texName.empty()) continue;
        }

        // Look up composite entry for this triangle's texture
        std::string texName;
        if (texIdx < geometry.textureNames().size()) {
            texName = geometry.textureNames()[texIdx];
        }
        const CompositeTextureEntry* entry = nullptr;
        if (!texName.empty()) {
            entry = compositeData.lookup(texName);
        }

        if (!entry) {
            unmappedTriangles++;
            continue;  // Skip triangles whose textures aren't in the composite
        }

        // Cell basing: same as buildTexturedMeshFromUploaded
        const auto& tv0 = geometry.vertices[tri.v1];
        const auto& tv1 = geometry.vertices[tri.v2];
        const auto& tv2 = geometry.vertices[tri.v3];
        float fv0 = flipV ? (1.0f - tv0.v) : tv0.v;
        float fv1 = flipV ? (1.0f - tv1.v) : tv1.v;
        float fv2 = flipV ? (1.0f - tv2.v) : tv2.v;
        int cellU = static_cast<int>(std::floor(std::min({tv0.u, tv1.u, tv2.u})));
        int cellV = static_cast<int>(std::floor(std::min({fv0, fv1, fv2})));

        for (uint32_t vidx : {tri.v1, tri.v2, tri.v3}) {
            // Key includes texture index + cell to handle shared vertices across textures
            uint64_t key = (static_cast<uint64_t>(vidx) << 32)
                         | (static_cast<uint64_t>(texIdx) << 16)
                         | (static_cast<uint64_t>(static_cast<uint8_t>(cellU)) << 8)
                         | static_cast<uint64_t>(static_cast<uint8_t>(cellV));
            auto it = globalToLocal.find(key);
            if (it != globalToLocal.end()) {
                buffer->Indices.push_back(it->second);
            } else {
                const auto& v = geometry.vertices[vidx];
                irr::video::S3DVertex vertex;
                vertex.Pos.X = v.x;
                vertex.Pos.Y = v.z;  // Irrlicht Y↔Z swap
                vertex.Pos.Z = v.y;
                vertex.Normal.X = v.nx;
                vertex.Normal.Y = v.nz;
                vertex.Normal.Z = v.ny;
                vertex.Color = irr::video::SColor(255, 255, 255, 255);

                // UV remap: cell-based UV → normalized [0,1) → composite sheet position
                float baseU = v.u - static_cast<float>(cellU);
                float baseV = (flipV ? (1.0f - v.v) : v.v) - static_cast<float>(cellV);
                float fractU = baseU / static_cast<float>(entry->tilesU);
                float fractV = baseV / static_cast<float>(entry->tilesV);
                vertex.TCoords.X = entry->uvOffsetU + fractU * entry->uvScaleU;
                vertex.TCoords.Y = entry->uvOffsetV + fractV * entry->uvScaleV;

                if (buffer->Vertices.size() >= 65535) {
                    // Hit 16-bit index limit — finalize this buffer and start a new one
                    buffer->recalculateBoundingBox();
                    mesh->addMeshBuffer(buffer);
                    buffer->drop();

                    buffer = new irr::scene::SMeshBuffer();
                    buffer->Material.setTexture(0, compositeTexture);
                    buffer->Material.BackfaceCulling = false;
                    buffer->Material.Lighting = false;
                    if (hasAlpha) {
                        // H02d (Option A): see buildTexturedMesh for rationale.
                        bool isETC1 = constrainedCache_ && constrainedCache_->isETC1Texture(compositeTexture);
                        if (isETC1) {
                            if (shaderMaterialAlphaTest_ >= 0) {
                                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                            } else {
                                buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                            }
                            if (constrainedCache_) {
                                irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(compositeTexture);
                                if (alphaTex) {
                                    buffer->Material.setTexture(1, alphaTex);
                                }
                            }
                        } else {
                            if (shaderMaterialAlphaTestRGBA_ >= 0) {
                                buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
                            } else {
                                buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                            }
                        }
                    } else {
                        if (shaderMaterialSolid_ >= 0) {
                            buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                        } else {
                            buffer->Material.MaterialType = irr::video::EMT_SOLID;
                        }
                    }
                    buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                    buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
                    buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;
                    globalToLocal.clear();
                }

                irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                globalToLocal[key] = localIdx;
                buffer->Vertices.push_back(vertex);
                buffer->Indices.push_back(localIdx);
            }
        }
    }

    if (buffer->Vertices.size() > 0) {
        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
    }
    buffer->drop();

    if (unmappedTriangles > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "ZoneMeshBuilder::buildCompositeMesh: {} triangles not in composite (skipped)",
                  unmappedTriangles);
    }

    mesh->recalculateBoundingBox();
    LOG_DEBUG(MOD_GRAPHICS, "ZoneMeshBuilder::buildCompositeMesh: built mesh with {} buffers, {} total vertices",
              mesh->getMeshBufferCount(),
              mesh->getMeshBufferCount() > 0 ? mesh->getMeshBuffer(0)->getVertexCount() : 0);
    return mesh;
}

// ============================================================================
// Performance: Lazy Texture Loading (Phase 3)
// ============================================================================

void ZoneMeshBuilder::registerLazyTexture(const std::string& name, std::shared_ptr<TextureInfo> texInfo) {
    if (name.empty() || !texInfo || texInfo->data.empty()) {
        return;
    }

    // Don't register if already loaded
    if (textureCache_.find(name) != textureCache_.end()) {
        return;
    }

    // Register for lazy loading
    pendingTextures_[name] = texInfo;
}

irr::video::ITexture* ZoneMeshBuilder::getOrLoadTexture(const std::string& name, bool applyPaletteMask) {
    if (name.empty()) {
        return nullptr;
    }

    // Check if already loaded
    auto cacheIt = textureCache_.find(name);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }

    // Check if pending (lazy load now)
    auto pendingIt = pendingTextures_.find(name);
    if (pendingIt != pendingTextures_.end() && pendingIt->second && !pendingIt->second->data.empty()) {
        // Load the texture now
        irr::video::ITexture* texture = loadTextureFromBMP(name, pendingIt->second->data, applyPaletteMask);

        // Remove from pending (data no longer needed)
        pendingTextures_.erase(pendingIt);

        return texture;
    }

    // Not found
    return nullptr;
}

bool ZoneMeshBuilder::hasTexture(const std::string& name) const {
    return textureCache_.find(name) != textureCache_.end() ||
           pendingTextures_.find(name) != pendingTextures_.end();
}

void ZoneMeshBuilder::registerUploadedTexture(const std::string& name,
                                               irr::video::ITexture* texture, bool hasAlpha) {
    if (!name.empty() && texture) {
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        textureCache_[lowerName] = texture;
        if (hasAlpha) {
            texturesWithAlpha_.insert(name);
            texturesWithAlpha_.insert(lowerName);
        }
    }
}

void ZoneMeshBuilder::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    constrainedCache_ = cache;
}

void ZoneMeshBuilder::clearTextureCache() {
    textureCache_.clear();
    pendingTextures_.clear();
    texturesWithAlpha_.clear();
}

} // namespace Graphics
} // namespace EQT
