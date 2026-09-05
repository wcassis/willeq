// tools/model_viewer/model_viewer_mesh.cpp -- texture upload, mesh
// building from ZoneGeometry, body/head variant combining and the
// animation cycling that rebuilds them.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"

// Cycle through available animations
void cycleAnimation(int direction) {
    if (!animatedNode || !useAnimatedMesh || availableAnimations.empty()) {
        std::cout << "No animations available" << std::endl;
        return;
    }

    // Cycle index
    currentAnimIndex += direction;
    if (currentAnimIndex < -1) {
        currentAnimIndex = static_cast<int>(availableAnimations.size()) - 1;
    } else if (currentAnimIndex >= static_cast<int>(availableAnimations.size())) {
        currentAnimIndex = -1;
    }

    // Apply animation
    if (currentAnimIndex < 0) {
        animatedNode->stopAnimation();
        std::cout << "Animation: NONE (pose)" << std::endl;
    } else {
        const std::string& animCode = availableAnimations[currentAnimIndex];
        if (animatedNode->playAnimation(animCode, true)) {
            std::cout << "Animation: " << animCode << " (" << (currentAnimIndex + 1) << "/"
                      << availableAnimations.size() << ")" << std::endl;
        } else {
            std::cout << "Failed to play animation: " << animCode << std::endl;
        }
    }

    // Reset pause state
    animationPaused = false;
    // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
    animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
}

// Toggle between static and animated mesh mode
void toggleAnimationMode() {
    useAnimatedMesh = !useAnimatedMesh;
    std::cout << "Mode: " << (useAnimatedMesh ? "ANIMATED" : "STATIC") << std::endl;

    // Rebuild with current settings preserved
    if (useAnimatedMesh) {
        reloadEntityWithTextures();
    } else {
        rebuildCurrentVariant();
    }
}

// Combine current body and head variant into characterGeometry
void combineCurrentVariants() {
    std::vector<std::shared_ptr<ZoneGeometry>> parts;

    if (!bodyVariants.empty() && currentBodyVariant < bodyVariants.size()) {
        parts.push_back(bodyVariants[currentBodyVariant]);
    }
    if (!headVariants.empty() && currentHeadVariant < headVariants.size()) {
        parts.push_back(headVariants[currentHeadVariant]);
    }

    if (parts.empty()) return;

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    u32 vertexOffset = 0;
    u32 textureOffset = 0;

    for (const auto& part : parts) {
        if (!part || part->vertices.empty()) continue;

        for (const auto& v : part->vertices) {
            combined->vertices.push_back(v);
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        for (const auto& tri : part->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        for (const auto& texName : part->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }
        for (bool inv : part->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        vertexOffset += static_cast<u32>(part->vertices.size());
        textureOffset += static_cast<u32>(part->textureNames().size());
    }

    combined->name = "combined_variant";
    characterGeometry = combined;
}

// Load texture from BMP/DDS data
video::ITexture* loadTexture(const std::string& name, const std::vector<char>& data) {
    if (data.empty() || !driver) return nullptr;

    // Check if DDS
    if (data.size() >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
        // DDS file - decode it
        DecodedImage decoded = DDSDecoder::decode(data);
        if (!decoded.isValid()) return nullptr;

        std::vector<u32> argbPixels(decoded.width * decoded.height);
        for (u32 y = 0; y < decoded.height; ++y) {
            for (u32 x = 0; x < decoded.width; ++x) {
                size_t srcIdx = (y * decoded.width + x) * 4;
                u8 r = decoded.pixels[srcIdx + 0];
                u8 g = decoded.pixels[srcIdx + 1];
                u8 b = decoded.pixels[srcIdx + 2];
                u8 a = decoded.pixels[srcIdx + 3];
                argbPixels[y * decoded.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }

        video::IImage* image = driver->createImageFromData(
            video::ECF_A8R8G8B8,
            core::dimension2d<u32>(decoded.width, decoded.height),
            argbPixels.data(), false, false);

        if (!image) return nullptr;

        video::ITexture* texture = driver->addTexture(name.c_str(), image);
        image->drop();
        return texture;
    }

    // BMP file - write to temp and load
    if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
        std::string tempPath = "/tmp/model_viewer_" + name;
        std::ofstream outFile(tempPath, std::ios::binary);
        if (!outFile) return nullptr;
        outFile.write(data.data(), data.size());
        outFile.close();
        return driver->getTexture(tempPath.c_str());
    }

    return nullptr;
}

// Check if a vertex is an outlier
bool isOutlierVertex(const Vertex3D& v, float threshold) {
    return std::abs(v.x) > threshold || std::abs(v.y) > threshold || std::abs(v.z) > threshold;
}

// Build mesh from geometry with optional X/Y swap
scene::IMesh* buildMesh(const ZoneGeometry& geometry, bool doSwapXY) {
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    scene::SMesh* mesh = new scene::SMesh();

    // Count outliers if filtering
    if (filterOutliers) {
        int outlierCount = 0;
        for (const auto& v : geometry.vertices) {
            if (isOutlierVertex(v, outlierThreshold)) {
                outlierCount++;
            }
        }
        std::cout << "Found " << outlierCount << " outlier vertices (threshold=" << outlierThreshold << ")" << std::endl;
    }

    // Group triangles by texture, optionally skipping triangles with outlier vertices
    std::map<u32, std::vector<size_t>> trianglesByTexture;
    int skippedTriangles = 0;
    for (size_t i = 0; i < geometry.triangles.size(); i++) {
        const auto& tri = geometry.triangles[i];

        // Check if any vertex in this triangle is an outlier
        if (filterOutliers) {
            bool hasOutlier = isOutlierVertex(geometry.vertices[tri.v1], outlierThreshold) ||
                              isOutlierVertex(geometry.vertices[tri.v2], outlierThreshold) ||
                              isOutlierVertex(geometry.vertices[tri.v3], outlierThreshold);
            if (hasOutlier) {
                skippedTriangles++;
                continue;
            }
        }

        trianglesByTexture[geometry.triangles[i].textureIndex].push_back(i);
    }

    if (filterOutliers && skippedTriangles > 0) {
        std::cout << "Skipped " << skippedTriangles << " triangles with outlier vertices" << std::endl;
    }

    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        scene::SMeshBuffer* buffer = new scene::SMeshBuffer();

        // Get texture
        std::string texName;
        video::ITexture* texture = nullptr;
        if (texIdx < geometry.textureNames().size()) {
            texName = geometry.textureNames()[texIdx];
            if (!texName.empty()) {
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // If equipment textures enabled, try to find the equipment variant texture
                std::string finalTexName = lowerTexName;
                if (useEquipmentTextures) {
                    // Try to map base texture to equipment variant
                    // e.g., humch0001.bmp -> humch0101.bmp (leather), humch0201.bmp (chain), etc.
                    // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
                    size_t pos = lowerTexName.find("00");
                    if (pos != std::string::npos && pos >= 3 && lowerTexName.length() > pos + 4) {
                        // Check if this looks like a body texture (e.g., humch0001.bmp)
                        char variantStr[8];
                        snprintf(variantStr, sizeof(variantStr), "%02d", currentBodyTexture);
                        std::string equipTexName = lowerTexName.substr(0, pos) + variantStr + lowerTexName.substr(pos + 2);

                        // Check if equipment texture exists
                        auto equipIt = characterTextures.find(equipTexName);
                        if (equipIt != characterTextures.end() && equipIt->second && !equipIt->second->data.empty()) {
                            if (IsDebugEnabled()) {
                                std::cout << "    Texture override: " << lowerTexName << " -> " << equipTexName << std::endl;
                            }
                            finalTexName = equipTexName;
                        } else if (IsDebugEnabled()) {
                            std::cout << "    Texture override failed: " << equipTexName << " not found" << std::endl;
                        }
                    }
                }

                auto texIt = characterTextures.find(finalTexName);
                if (texIt != characterTextures.end() && texIt->second && !texIt->second->data.empty()) {
                    texture = loadTexture(finalTexName, texIt->second->data);
                    if (IsDebugEnabled()) {
                        std::cout << "    Loaded texture: " << finalTexName << std::endl;
                    }
                } else if (IsDebugEnabled()) {
                    std::cout << "    Texture not found: " << finalTexName << std::endl;
                }
            }
        }

        buffer->Material.BackfaceCulling = false;
        buffer->Material.Lighting = false;
        if (texture) {
            buffer->Material.setTexture(0, texture);
            buffer->Material.MaterialType = video::EMT_SOLID;
        }

        std::unordered_map<size_t, u16> globalToLocal;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];

            for (size_t vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    const auto& v = geometry.vertices[vidx];
                    video::S3DVertex vertex;

                    if (doSwapXY) {
                        // Swap X and Y before coordinate transform
                        vertex.Pos.X = v.y;  // Swapped
                        vertex.Pos.Y = v.z;  // EQ Z -> Irrlicht Y
                        vertex.Pos.Z = v.x;  // Swapped
                        vertex.Normal.X = v.ny;
                        vertex.Normal.Y = v.nz;
                        vertex.Normal.Z = v.nx;
                    } else {
                        // Standard transform
                        vertex.Pos.X = v.x;
                        vertex.Pos.Y = v.z;  // EQ Z -> Irrlicht Y
                        vertex.Pos.Z = v.y;
                        vertex.Normal.X = v.nx;
                        vertex.Normal.Y = v.nz;
                        vertex.Normal.Z = v.ny;
                    }

                    vertex.TCoords.X = flipU ? (1.0f - v.u) : v.u;  // Optionally flip U coordinate
                    vertex.TCoords.Y = 1.0f - v.v;  // Flip V coordinate for correct texture orientation
                    vertex.Color = video::SColor(255, 255, 255, 255);

                    globalToLocal[vidx] = static_cast<u16>(buffer->Vertices.size());
                    buffer->Vertices.push_back(vertex);
                }
            }

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

void rebuildMesh() {
    if (!characterGeometry || !smgr) return;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove old nodes (both types)
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }

    // Build new mesh
    scene::IMesh* mesh = buildMesh(*characterGeometry, swapXY);
    if (!mesh) {
        LOG_ERROR(MOD_MAIN, "Failed to build mesh");
        return;
    }

    // Create new node
    modelNode = smgr->addMeshSceneNode(mesh);
    if (modelNode) {
        modelNode->setScale(core::vector3df(10, 10, 10));  // Scale up for visibility
        modelNode->setPosition(core::vector3df(0, 0, 0));
        modelNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

        for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
            modelNode->getMaterial(i).Wireframe = wireframe;
            modelNode->getMaterial(i).BackfaceCulling = false;
            modelNode->getMaterial(i).Lighting = false;
        }
    }
    mesh->drop();
}

void rebuildCurrentVariant() {
    combineCurrentVariants();
    rebuildMesh();
}

// Combine character parts into single geometry
std::shared_ptr<ZoneGeometry> combineCharacterParts(
    const std::vector<std::shared_ptr<ZoneGeometry>>& parts) {

    if (parts.empty()) return nullptr;

    auto combined = std::make_shared<ZoneGeometry>();
    combined->minX = combined->minY = combined->minZ = std::numeric_limits<float>::max();
    combined->maxX = combined->maxY = combined->maxZ = std::numeric_limits<float>::lowest();

    u32 vertexOffset = 0;
    u32 textureOffset = 0;

    for (const auto& part : parts) {
        if (!part || part->vertices.empty()) continue;

        for (const auto& v : part->vertices) {
            combined->vertices.push_back(v);
            combined->minX = std::min(combined->minX, v.x);
            combined->minY = std::min(combined->minY, v.y);
            combined->minZ = std::min(combined->minZ, v.z);
            combined->maxX = std::max(combined->maxX, v.x);
            combined->maxY = std::max(combined->maxY, v.y);
            combined->maxZ = std::max(combined->maxZ, v.z);
        }

        for (const auto& tri : part->triangles) {
            Triangle t;
            t.v1 = tri.v1 + vertexOffset;
            t.v2 = tri.v2 + vertexOffset;
            t.v3 = tri.v3 + vertexOffset;
            t.textureIndex = tri.textureIndex + textureOffset;
            t.flags = tri.flags;
            combined->triangles.push_back(t);
        }

        for (const auto& texName : part->textureNames()) {
            combined->mutableMaterialData().textureNames.push_back(texName);
        }
        for (bool inv : part->textureInvisible()) {
            combined->mutableMaterialData().textureInvisible.push_back(inv);
        }

        vertexOffset += static_cast<u32>(part->vertices.size());
        textureOffset += static_cast<u32>(part->textureNames().size());
    }

    if (combined->vertices.empty() || combined->triangles.empty()) return nullptr;

    combined->name = "combined";
    return combined;
}

// Load body/head variants for a specific race code from an S3D file
// Returns true if variants were found and loaded
bool loadVariantsFromS3D(const std::string& s3dPath, const std::string& raceCode) {
    S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        return false;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        return false;
    }

    // Convert race code to uppercase for matching
    std::string upperRaceCode = raceCode;
    std::transform(upperRaceCode.begin(), upperRaceCode.end(), upperRaceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Find the character model matching this race code
    std::shared_ptr<CharacterModel> character = nullptr;
    for (const auto& ch : zone->characters) {
        std::string charName = ch->name;
        std::transform(charName.begin(), charName.end(), charName.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        if (charName.find(upperRaceCode) != std::string::npos) {
            character = ch;
            break;
        }
    }

    if (!character || character->parts.empty()) {
        return false;
    }

    // Clear existing variants
    bodyVariants.clear();
    headVariants.clear();

    // Categorize parts into body variants and head variants
    std::string headPattern = upperRaceCode + "HE";
    for (const auto& part : character->parts) {
        std::string partName = part->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        if (partName.find(headPattern) == 0) {
            headVariants.push_back(part);
            if (IsDebugEnabled()) {
                std::cout << "  Head variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
            }
        } else if (partName.find(upperRaceCode) == 0) {
            bodyVariants.push_back(part);
            if (IsDebugEnabled()) {
                std::cout << "  Body variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
            }
        }
    }

    // Also merge textures from this S3D file
    for (const auto& [name, tex] : zone->characterTextures) {
        characterTextures[name] = tex;  // Override existing
    }

    if (IsDebugEnabled()) {
        std::cout << "Loaded " << bodyVariants.size() << " body variants, "
                  << headVariants.size() << " head variants from " << s3dPath << std::endl;
    }

    // Reset variant indices
    currentBodyVariant = 0;
    currentHeadVariant = 0;

    return !bodyVariants.empty();
}
