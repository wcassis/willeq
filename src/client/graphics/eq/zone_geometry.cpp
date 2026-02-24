#include "client/graphics/eq/zone_geometry.h"
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
                                                          const std::vector<char>& data) {
    // Note: fileSystem_ is not actually used in this function - textures are loaded
    // from raw byte data, not from the filesystem. Only driver_ is required.
    if (data.empty() || !driver_) {
        return nullptr;
    }

    // If constrained texture cache is set, route through it
    // This handles downsampling, 16-bit conversion, and memory budget enforcement
    if (constrainedCache_) {
        irr::video::ITexture* texture = constrainedCache_->getOrLoad(name, data);
        if (texture) {
            // Also cache locally for quick lookups (cache owns the texture)
            textureCache_[name] = texture;
            // Track alpha so materials get transparency type instead of EMT_SOLID
            if (constrainedCache_->hasAlpha(name)) {
                texturesWithAlpha_.insert(name);
            }
            LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' loaded via constrained cache ({} bytes)", name, data.size());
            return texture;
        }
        LOG_WARN(MOD_GRAPHICS, "Constrained cache failed for '{}' ({} bytes), falling back to direct load", name, data.size());
        // Fall through to unconstrained loading path below
    }

    // Check cache first (unconstrained mode)
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return it->second;
    }

    // Check if file is DDS format (EQ .bmp files are often DDS compressed)
    if (DDSDecoder::isDDS(data)) {
        // Decode DDS to RGBA pixels
        DecodedImage decoded = DDSDecoder::decode(data);
        if (!decoded.isValid()) {
            // Failed to decode - cache as nullptr
            textureCache_[name] = nullptr;
            return nullptr;
        }

        uint32_t width = decoded.width;
        uint32_t height = decoded.height;

        // Check if texture has any transparency (alpha < 255)
        bool hasTransparency = false;
        for (size_t i = 3; i < decoded.pixels.size(); i += 4) {
            if (decoded.pixels[i] < 255) {
                hasTransparency = true;
                break;
            }
        }

        // Convert RGBA to ARGB format (Irrlicht's native format)
        std::vector<uint32_t> argbPixels(width * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t srcIdx = (y * width + x) * 4;
                uint8_t r = decoded.pixels[srcIdx + 0];
                uint8_t g = decoded.pixels[srcIdx + 1];
                uint8_t b = decoded.pixels[srcIdx + 2];
                uint8_t a = decoded.pixels[srcIdx + 3];

                // ARGB format: A in high byte
                argbPixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }

        // Create Irrlicht image directly from ARGB data to preserve alpha
        irr::video::IImage* image = driver_->createImageFromData(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2d<irr::u32>(width, height),
            argbPixels.data(),
            false,  // Don't own the data (we have our own copy)
            false   // Don't delete on drop
        );

        if (!image) {
            textureCache_[name] = nullptr;
            return nullptr;
        }

        // Create texture from image
        std::string texName = "dds_" + name;
        irr::video::ITexture* texture = driver_->addTexture(texName.c_str(), image);
        image->drop();

        if (texture && hasTransparency) {
            texturesWithAlpha_.insert(name);
        }

        textureCache_[name] = texture;
        return texture;
    }

    // Check for valid BMP header
    if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
        // Standard BMP file - write to temp and load
        std::string tempPath = "/tmp/eqt_tex_" + name;

        std::ofstream outFile(tempPath, std::ios::binary);
        if (!outFile) {
            textureCache_[name] = nullptr;
            return nullptr;
        }
        outFile.write(data.data(), data.size());
        outFile.close();

        irr::video::ITexture* texture = driver_->getTexture(tempPath.c_str());
        textureCache_[name] = texture;
        return texture;
    }

    // Unknown format
    textureCache_[name] = nullptr;
    return nullptr;
}

irr::scene::IMesh* ZoneMeshBuilder::buildTexturedMesh(
    const ZoneGeometry& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    bool flipV) {

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    // If no textures available, fall back to colored mesh
    if (textures.empty() || geometry.textureNames.empty()) {
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
        bool isInvisible = (texIdx < geometry.textureInvisible.size()) && geometry.textureInvisible[texIdx];
        std::string texNameForLog = (texIdx < geometry.textureNames.size()) ? geometry.textureNames[texIdx] : "";
        if (isInvisible) {
            // Only skip if it's truly a collision-only material (empty texture name)
            if (texNameForLog.empty()) {
                continue;
            }
        }

        // Get texture name for this index
        std::string texName;
        irr::video::ITexture* texture = nullptr;

        if (texIdx < geometry.textureNames.size()) {
            texName = geometry.textureNames[texIdx];
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
                        LOG_DEBUG(MOD_GRAPHICS, "buildTexturedMesh: loadTextureFromBMP returned null for '{}' ({} bytes)",
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

                // Check if this texture has alpha transparency
                bool hasAlpha = (texturesWithAlpha_.find(texName) != texturesWithAlpha_.end());
                if (hasAlpha) {
                    // Use GLSL shader alpha-test material if available, else fixed-function
                    if (shaderMaterialAlphaTest_ >= 0) {
                        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                    } else {
                        buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
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

        bool isInvisible = (texIdx < geometry.textureInvisible.size()) && geometry.textureInvisible[texIdx];
        if (isInvisible) {
            std::string texName = (texIdx < geometry.textureNames.size()) ? geometry.textureNames[texIdx] : "";
            if (texName.empty()) continue;
        }

        // Look up pre-uploaded texture
        irr::video::ITexture* texture = (texIdx < textures.size()) ? textures[texIdx] : nullptr;
        bool hasAlpha = (texIdx < textureAlpha.size()) ? textureAlpha[texIdx] : false;

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
                    if (shaderMaterialAlphaTest_ >= 0) {
                        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                    } else {
                        buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
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
// Atlas Batched Mesh Building
// ============================================================================

#ifdef EQT_HAS_TEXTURE_ATLAS
irr::scene::IMesh* ZoneMeshBuilder::buildAtlasedMesh(
    const ZoneGeometry& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    const TextureAtlas& atlas,
    int pageIndexOffset) {

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    const size_t MAX_VERTICES_PER_BUFFER = 65535;

    // Classify each texture index: atlas page or fallback
    // Key: atlas page index (or -1 for non-atlased), Value: triangle indices
    struct PageBucket {
        int pageIndex;
        bool hasAlpha;
        uint16_t alphaPageIndex;
        std::vector<size_t> triangleIndices;
    };

    // Map: atlas page index -> PageBucket
    std::map<int, PageBucket> pageBuckets;
    // Non-atlased triangles (animated, missing): keyed by texture index
    std::map<uint32_t, std::vector<size_t>> fallbackTriangles;

    for (size_t i = 0; i < geometry.triangles.size(); ++i) {
        const auto& tri = geometry.triangles[i];
        uint32_t texIdx = tri.textureIndex;

        // Skip invisible/collision-only textures
        if (texIdx < geometry.textureInvisible.size() && geometry.textureInvisible[texIdx]) {
            std::string texName = (texIdx < geometry.textureNames.size()) ? geometry.textureNames[texIdx] : "";
            if (texName.empty()) continue;
        }

        if (texIdx >= geometry.textureNames.size()) {
            fallbackTriangles[texIdx].push_back(i);
            continue;
        }

        const std::string& texName = geometry.textureNames[texIdx];

        // Skip animated textures — they must use per-texture fallback rendering
        if (texIdx < geometry.textureAnimations.size() &&
            geometry.textureAnimations[texIdx].isAnimated) {
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
        // If so, the atlas UV (tile->uvOffset + relUV * tileScale) would overflow
        // into adjacent tiles. Send these to per-texture fallback rendering.
        {
            const auto& v0 = geometry.vertices[tri.v1];
            const auto& v1 = geometry.vertices[tri.v2];
            const auto& v2 = geometry.vertices[tri.v3];
            float uMin = std::min({v0.u, v1.u, v2.u});
            float uMax = std::max({v0.u, v1.u, v2.u});
            float vMin = std::min({v0.v, v1.v, v2.v});
            float vMax = std::max({v0.v, v1.v, v2.v});
            // relU = u - floor(uMin), so max relU = uMax - floor(uMin).
            // If that exceeds 1.0, the atlas UV will sample outside the tile.
            float maxRelU = uMax - std::floor(uMin);
            float maxRelV = vMax - std::floor(vMin);
            if (maxRelU > 1.0f || maxRelV > 1.0f) {
                fallbackTriangles[texIdx].push_back(i);
                continue;
            }
        }

        int pageIdx = tile->pageIndex;
        auto& bucket = pageBuckets[pageIdx];
        bucket.pageIndex = pageIdx;
        bucket.hasAlpha = tile->hasAlpha;
        bucket.alphaPageIndex = tile->alphaPageIndex;
        bucket.triangleIndices.push_back(i);
    }

    LOG_INFO(MOD_GRAPHICS, "buildAtlasedMesh: {} atlas page buckets, {} fallback texture groups",
             pageBuckets.size(), fallbackTriangles.size());

    // Log which textures are atlas-batched vs fallback
    {
        std::set<std::string> atlasedNames, fallbackNames;
        for (const auto& [pageIdx, bucket] : pageBuckets) {
            for (size_t triIdx : bucket.triangleIndices) {
                uint32_t texIdx = geometry.triangles[triIdx].textureIndex;
                if (texIdx < geometry.textureNames.size())
                    atlasedNames.insert(geometry.textureNames[texIdx]);
            }
        }
        for (const auto& [texIdx, triIndices] : fallbackTriangles) {
            if (texIdx < geometry.textureNames.size())
                fallbackNames.insert(geometry.textureNames[texIdx]);
        }
        for (const auto& n : atlasedNames)
            LOG_DEBUG(MOD_GRAPHICS, "  [ATLAS] texture '{}' -> atlas page (pageOffset={})", n, pageIndexOffset);
        for (const auto& n : fallbackNames)
            LOG_DEBUG(MOD_GRAPHICS, "  [FALLBACK] texture '{}' -> per-texture (constrained cache or direct)", n);
    }

    // Build mesh buffers for atlas page buckets using S3DVertex2TCoords
    for (const auto& [pageIdx, bucket] : pageBuckets) {
        if (bucket.triangleIndices.empty()) continue;

        // Get the atlas page GL texture handle
        uint32_t glTexHandle = atlas.getPageTexture(static_cast<uint16_t>(pageIdx));
        if (glTexHandle == 0) {
            LOG_WARN(MOD_GRAPHICS, "buildAtlasedMesh: No GL texture for atlas page {}", pageIdx);
            continue;
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
            std::string texName = geometry.textureNames[texIdx];
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
        irr::video::ITexture* texture = nullptr;

        if (texIdx < geometry.textureNames.size()) {
            texName = geometry.textureNames[texIdx];
            if (!texName.empty()) {
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                auto texIt = textures.find(lowerTexName);
                if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                    texture = loadTextureFromBMP(texName, texIt->second->data);
                }
                if (!texture && constrainedCache_) {
                    texture = constrainedCache_->getTexture(lowerTexName);
                    if (!texture) texture = constrainedCache_->getTexture(texName);
                }
            }
        }

        // Build sub-buffers (same splitting logic as buildTexturedMesh)
        // Key includes UV cell base for FP16 precision (Mali 400 GLES2)
        std::unordered_map<uint64_t, irr::u16> globalToLocal;
        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        buffer->Material.BackfaceCulling = false;
        buffer->Material.Lighting = false;
        if (texture) {
            buffer->Material.setTexture(0, texture);
            bool hasAlpha = (texturesWithAlpha_.find(texName) != texturesWithAlpha_.end());
            if (hasAlpha) {
                if (shaderMaterialAlphaTest_ >= 0) {
                    buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                } else {
                    buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                }
            } else {
                if (shaderMaterialSolid_ >= 0) {
                    buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                } else {
                    buffer->Material.MaterialType = irr::video::EMT_SOLID;
                }
            }
            buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
            buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
            buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
        }

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];

            // Check if we need a new buffer
            if (buffer->Vertices.size() + 3 > MAX_VERTICES_PER_BUFFER) {
                buffer->recalculateBoundingBox();
                mesh->addMeshBuffer(buffer);
                buffer->drop();
                buffer = new irr::scene::SMeshBuffer();
                buffer->Material.BackfaceCulling = false;
                buffer->Material.Lighting = false;
                if (texture) {
                    buffer->Material.setTexture(0, texture);
                    if (shaderMaterialSolid_ >= 0) {
                        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    }
                    buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                    buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                    buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
                }
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
                    vertex.Color = texture ? irr::video::SColor(255, 255, 255, 255)
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

irr::video::ITexture* ZoneMeshBuilder::getOrLoadTexture(const std::string& name) {
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
        irr::video::ITexture* texture = loadTextureFromBMP(name, pendingIt->second->data);

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
