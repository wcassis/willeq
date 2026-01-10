// RaceModelLoader mesh building methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class

#include "client/graphics/eq/race_model_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>

namespace EQT {
namespace Graphics {

std::map<std::string, std::shared_ptr<TextureInfo>> RaceModelLoader::getMergedTextures() {
    // Performance optimization: return cached result if valid
    if (mergedTexturesCacheValid_) {
        return cachedMergedTextures_;
    }

    // Build a merged texture map from all sources, matching model viewer behavior:
    // Order: global_chr.s3d -> global2-7_chr.s3d -> armor textures -> zone_chr.s3d (overrides)

    // Ensure numbered globals are loaded (they contain some armor textures)
    if (!numberedGlobalsLoaded_) {
        loadNumberedGlobalModels();
    }

    // Ensure armor textures are loaded (global17-23_amr.s3d contain high-tier armor textures)
    if (!armorTexturesLoaded_) {
        loadArmorTextures();
    }

    std::map<std::string, std::shared_ptr<TextureInfo>> merged;

    // 1. Start with global textures
    merged = globalTextures_;

    // 2. Add textures from numbered globals (only add new, don't override)
    for (const auto& [num, texMap] : numberedGlobalTextures_) {
        for (const auto& [name, tex] : texMap) {
            if (merged.find(name) == merged.end()) {
                merged[name] = tex;
            }
        }
    }

    // 3. Add armor textures (global17-23_amr.s3d - only add new, don't override)
    for (const auto& [name, tex] : armorTextures_) {
        if (merged.find(name) == merged.end()) {
            merged[name] = tex;
        }
    }

    // 4. Add/override with zone textures (zone takes precedence)
    for (const auto& [name, tex] : zoneTextures_) {
        merged[name] = tex;
    }

    // Cache the result
    cachedMergedTextures_ = merged;
    mergedTexturesCacheValid_ = true;

    return cachedMergedTextures_;
}

irr::scene::IMesh* RaceModelLoader::buildMeshFromGeometry(
    const std::shared_ptr<ZoneGeometry>& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    uint8_t bodyTextureVariant,
    const std::string& raceCode) {

    if (!geometry) {
        return nullptr;
    }

    // Build multi-buffer textured mesh while maintaining vertex mapping for animation
    // Group triangles by texture, but track where each original vertex ends up

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Prepare lowercase race code for texture matching
    // The raceCode might be a full model name like "QCF_DMSPRITEDEF", so extract just the 3-letter prefix
    std::string lowerRaceCode = raceCode;
    // Remove any suffix (find underscore or use first 3 chars)
    size_t underscorePos = lowerRaceCode.find('_');
    if (underscorePos != std::string::npos) {
        lowerRaceCode = lowerRaceCode.substr(0, underscorePos);
    }
    // Race codes are typically 3 characters (HUM, ELF, QCF, etc.)
    if (lowerRaceCode.length() > 3) {
        lowerRaceCode = lowerRaceCode.substr(0, 3);
    }
    std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Group triangles by texture index
    std::map<irr::u32, std::vector<size_t>> trianglesByTexture;
    for (size_t i = 0; i < geometry->triangles.size(); ++i) {
        trianglesByTexture[geometry->triangles[i].textureIndex].push_back(i);
    }

    // Build original vertices array (in order, for bone transforms)
    originalVerticesForAnimation_.clear();
    originalVerticesForAnimation_.reserve(geometry->vertices.size());
    for (const auto& v : geometry->vertices) {
        irr::video::S3DVertex vert;
        // Store in EQ coordinates - conversion happens during animation
        vert.Pos.X = v.x;
        vert.Pos.Y = v.y;
        vert.Pos.Z = v.z;
        vert.Normal.X = v.nx;
        vert.Normal.Y = v.ny;
        vert.Normal.Z = v.nz;
        vert.TCoords.X = v.u;
        vert.TCoords.Y = 1.0f - v.v;  // Flip V for character models
        vert.Color = irr::video::SColor(255, 255, 255, 255);
        originalVerticesForAnimation_.push_back(vert);
    }

    // Initialize vertex mapping (original index -> buffer location)
    vertexMappingForAnimation_.clear();
    vertexMappingForAnimation_.resize(geometry->vertices.size());
    // Initialize all mappings to invalid
    for (auto& m : vertexMappingForAnimation_) {
        m.bufferIndex = 0xFFFFFFFF;
        m.localIndex = 0xFFFFFFFF;
    }

    irr::u32 bufferIndex = 0;
    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Load texture for this buffer
        irr::video::ITexture* texture = nullptr;
        if (texIdx < geometry->textureNames.size() && meshBuilder_) {
            const std::string& texName = geometry->textureNames[texIdx];
            if (!texName.empty()) {
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Apply equipment texture override if variant > 0
                // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
                // e.g., qcfch0001.bmp -> qcfch0101.bmp (texture=1, leather)
                std::string finalTexName = lowerTexName;
                bool overrideApplied = false;

                if (bodyTextureVariant > 0) {
                    // Check for CLK (robe) textures first
                    // Pattern: clkXXYY.bmp where XX is robe clk number (04-10), YY is variant (01-06)
                    // Robe textures 10-16 map to clk04-clk10
                    if (lowerTexName.length() >= 10 && lowerTexName.substr(0, 3) == "clk" &&
                        bodyTextureVariant >= 10 && bodyTextureVariant <= 16) {
                        // Extract the page number (last 2 digits before .bmp)
                        // clk0401.bmp -> page = "01"
                        std::string pageNum = lowerTexName.substr(5, 2);

                        // Calculate target CLK number from robe texture
                        // Texture 10 -> clk04, 11 -> clk05, ..., 16 -> clk10
                        int targetClk = bodyTextureVariant - 6;

                        char clkTexName[32];
                        snprintf(clkTexName, sizeof(clkTexName), "clk%02d%s.bmp", targetClk, pageNum.c_str());
                        std::string clkTexNameStr = clkTexName;

                        // Check if target CLK texture exists
                        auto clkIt = textures.find(clkTexNameStr);
                        if (clkIt != textures.end() && clkIt->second && !clkIt->second->data.empty()) {
                            finalTexName = clkTexNameStr;
                            overrideApplied = true;
                        }
                    }
                    // Check if texture name starts with race code and has "00" pattern
                    // Body parts: ch (chest), lg (legs), ft (feet), ua (upper arms), fa (forearms), hn (hands)
                    // Pattern: {race}{part}00{page}.bmp where part is 2 chars
                    else if (!lowerRaceCode.empty() &&
                             lowerTexName.length() >= lowerRaceCode.length() + 6 &&
                             lowerTexName.substr(0, lowerRaceCode.length()) == lowerRaceCode) {
                        // Find "00" after race code + 2-char body part code
                        size_t partEnd = lowerRaceCode.length() + 2;
                        if (partEnd + 2 <= lowerTexName.length() &&
                            lowerTexName.substr(partEnd, 2) == "00") {
                            // Build equipment variant texture name
                            char variantStr[8];
                            snprintf(variantStr, sizeof(variantStr), "%02d", bodyTextureVariant);
                            std::string equipTexName = lowerTexName.substr(0, partEnd) + variantStr +
                                                       lowerTexName.substr(partEnd + 2);

                            // Check if equipment variant texture exists
                            auto equipIt = textures.find(equipTexName);
                            if (equipIt != textures.end() && equipIt->second && !equipIt->second->data.empty()) {
                                finalTexName = equipTexName;
                                overrideApplied = true;
                            }
                        }
                    }
                }

                // Performance: Try cache first via getOrLoadTexture, then lazy load if needed
                texture = meshBuilder_->getOrLoadTexture(finalTexName);
                if (!texture) {
                    // Not in cache, register for lazy loading and load now
                    auto texIt = textures.find(finalTexName);
                    if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                        texture = meshBuilder_->loadTextureFromBMP(finalTexName, texIt->second->data);
                        LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: Loaded texture \"{}\"{}",
                                  bufferIndex, finalTexName, (overrideApplied ? " (equipment override)" : ""));
                    } else {
                        // If equipment variant not found, try the original texture
                        if (overrideApplied) {
                            texture = meshBuilder_->getOrLoadTexture(lowerTexName);
                            if (!texture) {
                                texIt = textures.find(lowerTexName);
                                if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                                    texture = meshBuilder_->loadTextureFromBMP(lowerTexName, texIt->second->data);
                                    LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: Loaded fallback texture \"{}\"", bufferIndex, lowerTexName);
                                } else {
                                    LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: FAILED to find texture \"{}\" or fallback \"{}\"", bufferIndex, finalTexName, lowerTexName);
                                }
                            } else {
                                LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: Using cached fallback texture \"{}\"", bufferIndex, lowerTexName);
                            }
                        } else {
                            LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: FAILED to find texture \"{}\"", bufferIndex, lowerTexName);
                        }
                    }
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: Using cached texture \"{}\"{}",
                              bufferIndex, finalTexName, (overrideApplied ? " (equipment override)" : ""));
                }
            }
        }

        buffer->Material.Lighting = false;
        buffer->Material.BackfaceCulling = false;
        if (texture) {
            buffer->Material.setTexture(0, texture);
            buffer->Material.MaterialType = irr::video::EMT_SOLID;
        } else {
            buffer->Material.DiffuseColor = irr::video::SColor(255, 200, 180, 160);
        }

        // Map from global vertex index to local buffer index
        std::map<irr::u32, irr::u16> globalToLocal;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry->triangles[triIdx];

            for (irr::u32 vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    // Add vertex to buffer
                    const auto& v = geometry->vertices[vidx];
                    irr::video::S3DVertex vert;
                    // Store in EQ coordinates - conversion happens during animation
                    vert.Pos.X = v.x;
                    vert.Pos.Y = v.y;
                    vert.Pos.Z = v.z;
                    vert.Normal.X = v.nx;
                    vert.Normal.Y = v.ny;
                    vert.Normal.Z = v.nz;
                    vert.TCoords.X = v.u;
                    vert.TCoords.Y = 1.0f - v.v;
                    vert.Color = irr::video::SColor(255, 255, 255, 255);

                    irr::u16 localIdx = static_cast<irr::u16>(buffer->Vertices.size());
                    globalToLocal[vidx] = localIdx;
                    buffer->Vertices.push_back(vert);

                    // Record vertex mapping for animation
                    if (vidx < vertexMappingForAnimation_.size()) {
                        vertexMappingForAnimation_[vidx].bufferIndex = bufferIndex;
                        vertexMappingForAnimation_[vidx].localIndex = localIdx;
                    }
                }
            }

            // Add triangle indices
            buffer->Indices.push_back(globalToLocal[tri.v1]);
            buffer->Indices.push_back(globalToLocal[tri.v2]);
            buffer->Indices.push_back(globalToLocal[tri.v3]);
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
        bufferIndex++;
    }

    mesh->recalculateBoundingBox();

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Built multi-texture animated mesh with {} buffers, {} original vertices",
              mesh->getMeshBufferCount(), geometry->vertices.size());

    return mesh;
}

irr::scene::IMesh* RaceModelLoader::buildMeshWithEquipment(
    const std::shared_ptr<ZoneGeometry>& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    const std::string& raceCode,
    const uint32_t* equipment) {

    // For now, delegate to standard mesh building
    // Equipment texture swapping is handled at the scene node level
    return buildMeshFromGeometry(geometry, textures);
}

} // namespace Graphics
} // namespace EQT
