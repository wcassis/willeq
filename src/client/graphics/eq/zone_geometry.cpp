#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/constrained_texture_cache.h"
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
        }
        return texture;
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
                    // Use alpha test for transparency (cheaper than blending)
                    buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
                } else {
                    buffer->Material.MaterialType = irr::video::EMT_SOLID;
                }

                // Enable bilinear filtering for smoother textures
                buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                buffer->Material.setFlag(irr::video::EMF_TRILINEAR_FILTER, false);
                buffer->Material.setFlag(irr::video::EMF_ANISOTROPIC_FILTER, false);
                // Enable texture wrapping/tiling for UV coords > 1
                buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
            }

            // Track which vertices we've added to this buffer
            std::unordered_map<size_t, irr::u16> globalToLocal;

            for (size_t triIdx : subTriIndices) {
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
                        vertex.TCoords.Y = flipV ? (1.0f - v.v) : v.v;

                        // White color if textured, height-based if not
                        if (texture) {
                            vertex.Color = irr::video::SColor(255, 255, 255, 255);
                        } else {
                            vertex.Color = heightToColor(v.z, minZ, maxZ);
                        }

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
    }

    mesh->recalculateBoundingBox();
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

void ZoneMeshBuilder::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    constrainedCache_ = cache;
}

} // namespace Graphics
} // namespace EQT
