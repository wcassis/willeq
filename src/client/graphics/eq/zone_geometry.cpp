// The non-atlased ZoneMeshBuilder path. The texture-atlas conditional region
// and buildAtlasedMesh lived in zone_geometry_atlased.cpp (J32), deleted with
// the unreached texture-atlas runtime (B494, #169).
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/constrained_texture_cache.h"
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

// B1019 (GitLab #340): the layout-equality net's CORE side, defined in this
// TU because this is the TU willeq_core compiles -- the sizeof here carries
// the core's own macro state. tests/test_zone_geometry_atlas_layout.cpp
// links willeq_core and compares its consumer-side sizeof against this.
size_t ZoneMeshBuilder::layoutSize() { return sizeof(ZoneMeshBuilder); }

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

size_t finishMeshBuffer(irr::scene::SMeshBuffer* buffer) {
    if (!buffer) return 0;
    const size_t slack =
        (buffer->Vertices.allocated_size() - buffer->Vertices.size()) * sizeof(irr::video::S3DVertex) +
        (buffer->Indices.allocated_size() - buffer->Indices.size()) * sizeof(irr::u16);
    buffer->Vertices.reallocate(buffer->Vertices.size());
    buffer->Indices.reallocate(buffer->Indices.size());
    buffer->recalculateBoundingBox();
    return slack;
}

size_t finishMesh(irr::scene::SMesh* mesh) {
    if (!mesh) return 0;
    const size_t slack =
        (mesh->MeshBuffers.allocated_size() - mesh->MeshBuffers.size()) * sizeof(irr::scene::IMeshBuffer*);
    mesh->MeshBuffers.reallocate(mesh->MeshBuffers.size());
    mesh->recalculateBoundingBox();
    return slack;
}


namespace {
// T408 (B110): unpack the WLD's baked per-vertex light into an Irrlicht vertex
// colour. The 0x36 colour array is packed BGRA; the fourth byte is NOT part of
// the baked RGB (its qeynos2 mean is 107/255 against 26.8/53.5/53.1 for
// B/G/R -- tests/test_baked_vertex_lighting.cpp pins both) and is carried
// through in the vertex alpha as the sun/sky exposure channel the shader's
// uBakedLight.y masks the sun term with. Callers set
// Material.MaterialTypeParam = 1 on the buffers that get these, which is the
// per-material flag the zone shader keys uBakedFlag off; without it the
// shader's equation is exactly the pre-T408 one.
inline bool geometryHasBakedLight(const EQT::Graphics::ZoneGeometry& g) {
    return g.vertexColors.size() == g.vertices.size() && !g.vertexColors.empty();
}
inline irr::video::SColor bakedVertexColor(uint32_t packedBgra) {
    return irr::video::SColor(
        static_cast<irr::u32>((packedBgra >> 24) & 0xFF),   // A: sky exposure
        static_cast<irr::u32>((packedBgra >> 16) & 0xFF),   // R
        static_cast<irr::u32>((packedBgra >> 8) & 0xFF),    // G
        static_cast<irr::u32>(packedBgra & 0xFF));          // B
}
}  // namespace

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

        // B976 (#294): zone region geometry gets the depth tie-break flag
        // (see include/client/graphics/zone_depth_tie_break.h).
        if (zoneGeometryDepthTie_)
            buffer->Material.MaterialTypeParam2 = 1.0f;

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

        finishMeshBuffer(buffer);  // B904: exact-size arrays
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    finishMesh(mesh);  // B904
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

        // B976 (#294): zone region geometry gets the depth tie-break flag
        // (see include/client/graphics/zone_depth_tie_break.h).
        if (zoneGeometryDepthTie_)
            buffer->Material.MaterialTypeParam2 = 1.0f;

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

        finishMeshBuffer(buffer);  // B904: exact-size arrays
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    finishMesh(mesh);  // B904
    return mesh;
}

irr::video::ITexture* ZoneMeshBuilder::loadTextureFromBMP(const std::string& name,
                                                          const std::vector<char>& data,
                                                          bool applyPaletteMask,
                                                          const TextureOrigin* origin) {
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
        irr::video::ITexture* texture = constrainedCache_->getOrLoad(name, data, uploadCategory_, "loadTextureFromBMP",
                                                                     applyPaletteMask, origin);
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

    // T408 (B110): this builder serves BOTH zone/object geometry (flipV=false)
    // and character models (flipV=true -- variant_loading.cpp,
    // race_model_loader.cpp). Only the zone's WLD bakes static light into its
    // vertex colours; a character's are the model's own and are not light, so
    // the baked path is gated on flipV as well as on the array being present.
    const bool useBaked = !flipV && geometryHasBakedLight(geometry);

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
                    const TextureOrigin origin = texIt->second->origin();  // B947
                    texture = loadTextureFromBMP(texName, texIt->second->data, false, &origin);
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
            // B976 (#294): zone region geometry gets the depth tie-break flag
            // (see include/client/graphics/zone_depth_tie_break.h).
            if (zoneGeometryDepthTie_)
                buffer->Material.MaterialTypeParam2 = 1.0f;
            // T408 (B110): the zone shader's per-material baked-light flag.
            // ShaderCallback::OnSetMaterial reads it; 0 keeps the pre-T408
            // equation exact for every mesh that is not carrying baked light.
            buffer->Material.MaterialTypeParam = useBaked ? 1.0f : 0.0f;

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

                        // T408 (B110): EQ's baked per-vertex light where the
                        // WLD carries it, white otherwise (the pre-T408
                        // value), height-based when untextured.
                        if (texture) {
                            vertex.Color = useBaked
                                ? bakedVertexColor(geometry.vertexColors[vidx])
                                : irr::video::SColor(255, 255, 255, 255);
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

            finishMeshBuffer(buffer);  // B904: exact-size arrays
            mesh->addMeshBuffer(buffer);
            buffer->drop();
        }
    }

    finishMesh(mesh);  // B904
    return mesh;
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
        const TextureOrigin origin = pendingIt->second->origin();  // B947
        irr::video::ITexture* texture = loadTextureFromBMP(name, pendingIt->second->data, applyPaletteMask,
                                                           &origin);

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

void ZoneMeshBuilder::forgetTexture(const irr::video::ITexture* texture) {
    if (!texture) return;
    for (auto it = textureCache_.begin(); it != textureCache_.end(); ) {
        if (it->second == texture) it = textureCache_.erase(it);
        else ++it;
    }
}

void ZoneMeshBuilder::clearTextureCache() {
    textureCache_.clear();
    pendingTextures_.clear();
    texturesWithAlpha_.clear();
}

} // namespace Graphics
} // namespace EQT
