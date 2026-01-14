#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <regex>

namespace EQT {
namespace Graphics {

EquipmentModelLoader::EquipmentModelLoader(irr::scene::ISceneManager* smgr,
                                           irr::video::IVideoDriver* driver,
                                           irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    meshBuilder_ = std::make_unique<ZoneMeshBuilder>(smgr, driver, fileSystem);
}

EquipmentModelLoader::~EquipmentModelLoader() {
    // Drop all cached meshes
    for (auto& [id, mesh] : meshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
}

void EquipmentModelLoader::setClientPath(const std::string& path) {
    clientPath_ = path;
    // Ensure path ends with separator
    if (!clientPath_.empty() && clientPath_.back() != '/') {
        clientPath_ += '/';
    }
}

int EquipmentModelLoader::loadItemModelMapping(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS, "EquipmentModelLoader: Failed to open item mapping file: {}", jsonPath);
        return -1;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "EquipmentModelLoader: Failed to parse item mapping JSON: {}", errors);
        return -1;
    }

    itemToModelMap_.clear();

    // Parse JSON object: { "itemId": modelId, ... }
    for (const auto& key : root.getMemberNames()) {
        uint32_t itemId = std::stoul(key);
        int modelId = root[key].asInt();
        itemToModelMap_[itemId] = modelId;
    }

    mappingLoaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Loaded {} item-to-model mappings", itemToModelMap_.size());
    return static_cast<int>(itemToModelMap_.size());
}

int EquipmentModelLoader::getModelIdForItem(uint32_t databaseItemId) const {
    auto it = itemToModelMap_.find(databaseItemId);
    if (it != itemToModelMap_.end()) {
        return it->second;
    }
    return -1;
}

bool EquipmentModelLoader::isShield(int modelId) {
    // IT200-IT299 are shield models
    return modelId >= 200 && modelId < 300;
}

int EquipmentModelLoader::parseModelIdFromActorName(const std::string& actorName) {
    // Parse "IT123" -> 123
    // Note: WLD loader strips _ACTORDEF suffix, so we just match IT followed by digits
    // Actor names are uppercase
    std::regex pattern("IT(\\d+)");
    std::smatch match;
    if (std::regex_match(actorName, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

bool EquipmentModelLoader::loadEquipmentArchives() {
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "EquipmentModelLoader: Client path not set");
        return false;
    }

    // Load all gequip archives (lower priority first, higher priority last for duplicates)
    // gequip.s3d: IT27-153, gequip2: IT11-656, gequip3: IT10000-10105
    // gequip4: IT10015-11502, gequip5: IT10501-10523, gequip6: IT661-668, gequip8: IT10524-10733
    std::vector<std::string> archiveNames = {
        "gequip8.s3d", "gequip6.s3d", "gequip5.s3d", "gequip4.s3d",
        "gequip3.s3d", "gequip2.s3d", "gequip.s3d"
    };

    for (const auto& archiveName : archiveNames) {
        std::string archivePath = clientPath_ + archiveName;
        loadEquipmentArchive(archivePath);
    }

    archivesLoaded_ = true;

    return !equipmentModels_.empty();
}

bool EquipmentModelLoader::loadEquipmentArchive(const std::string& archivePath) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Could not open archive: {}", archivePath);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Loading equipment archive: {}", archivePath);

    // Load textures from archive
    std::vector<std::string> texFiles;
    archive.getFilenames(".bmp", texFiles);

    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            textures_[lowerName] = tex;
        }
    }

    // Find WLD files
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: No WLD file found in: {}", archivePath);
        return false;
    }

    // Parse each WLD file
    for (const auto& wldName : wldFiles) {
        WldLoader wldLoader;
        if (!wldLoader.parseFromArchive(archivePath, wldName)) {
            LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to parse WLD: {}", wldName);
            continue;
        }

        // Get actors (Fragment 0x14) and geometries
        const auto& objectDefs = wldLoader.getObjectDefs();
        const auto& modelRefs = wldLoader.getModelRefs();
        const auto& geometries = wldLoader.getGeometries();

        LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: WLD {}: {} actors, {} geometries",
                wldName, objectDefs.size(), geometries.size());

        // Debug: Log first few actor names to see what pattern they use
        int debugCount = 0;
        for (const auto& [name, _] : objectDefs) {
            if (debugCount++ < 5) {
                LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Sample actor name: '{}'", name);
            }
        }

        // Process each actor that matches IT### pattern
        for (const auto& [actorName, objDef] : objectDefs) {
            int modelId = parseModelIdFromActorName(actorName);
            if (modelId < 0) {
                continue;  // Not an equipment actor
            }

            // Skip if already loaded (from earlier archive)
            if (equipmentModels_.find(modelId) != equipmentModels_.end()) {
                continue;
            }

            // Find geometries by name pattern matching IT{modelId}
            // Equipment models use naming like IT10653_DMSPRITEDEF
            std::vector<std::shared_ptr<ZoneGeometry>> actorGeometries;
            std::string prefix = "IT" + std::to_string(modelId);

            for (const auto& geom : geometries) {
                if (geom && !geom->name.empty()) {
                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });
                    // Match IT{id}_ pattern (e.g., IT10653_DMSPRITEDEF)
                    if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                        actorGeometries.push_back(geom);
                    }
                }
            }

            if (actorGeometries.empty()) {
                continue;  // No geometry found for this actor
            }

            // Debug: log what we're storing
            LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Storing model IT{} from actor {} with {} geometries",
                    modelId, actorName, actorGeometries.size());

            // Combine geometries into a single equipment model
            auto equipModel = std::make_shared<EquipmentModelData>();
            equipModel->modelName = "IT" + std::to_string(modelId);
            equipModel->modelId = modelId;

            // Extract archive name from path
            size_t lastSlash = archivePath.find_last_of("/\\");
            equipModel->sourceArchive = (lastSlash != std::string::npos)
                ? archivePath.substr(lastSlash + 1) : archivePath;
            equipModel->sourceWld = wldName;

            // Combine all geometries (in case there are multiple mesh parts)
            auto combinedGeom = std::make_shared<ZoneGeometry>();
            combinedGeom->name = equipModel->modelName;

            for (const auto& geom : actorGeometries) {
                size_t vertexOffset = combinedGeom->vertices.size();

                // Track geometry names used
                if (!geom->name.empty()) {
                    equipModel->geometryName += (equipModel->geometryName.empty() ? "" : ", ") + geom->name;
                }

                // Add vertices
                for (const auto& v : geom->vertices) {
                    combinedGeom->vertices.push_back(v);
                }

                // Add triangles with adjusted indices
                for (const auto& tri : geom->triangles) {
                    Triangle newTri = tri;
                    newTri.v1 += static_cast<uint32_t>(vertexOffset);
                    newTri.v2 += static_cast<uint32_t>(vertexOffset);
                    newTri.v3 += static_cast<uint32_t>(vertexOffset);
                    combinedGeom->triangles.push_back(newTri);
                }

                // Add texture names
                for (const auto& texName : geom->textureNames) {
                    // Check if already added
                    bool found = false;
                    for (const auto& existing : combinedGeom->textureNames) {
                        if (existing == texName) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        combinedGeom->textureNames.push_back(texName);
                        equipModel->textureNames.push_back(texName);
                    }
                }
            }

            equipModel->geometry = combinedGeom;
            equipModel->textures = textures_;  // Share texture map

            equipmentModels_[modelId] = equipModel;
        }
    }

    return true;
}

irr::scene::IMesh* EquipmentModelLoader::getEquipmentMeshByModelId(int modelId) {
    if (modelId < 0) {
        return nullptr;
    }

    // Check cache first
    auto cacheIt = meshCache_.find(modelId);
    if (cacheIt != meshCache_.end()) {
        return cacheIt->second;
    }

    // Find equipment model data
    auto modelIt = equipmentModels_.find(modelId);
    if (modelIt == equipmentModels_.end()) {
        return nullptr;  // Model not found
    }

    const auto& equipModel = modelIt->second;
    if (!equipModel || !equipModel->geometry) {
        return nullptr;
    }

    // Build mesh from geometry
    irr::scene::IMesh* mesh = buildMeshFromGeometry(equipModel->geometry, equipModel->textures);
    if (mesh) {
        meshCache_[modelId] = mesh;
    }

    return mesh;
}

irr::scene::IMesh* EquipmentModelLoader::getEquipmentMesh(uint32_t equipmentId) {
    // First try to look up as a database item ID (for player inventory items)
    int modelId = getModelIdForItem(equipmentId);
    if (modelId >= 0) {
        return getEquipmentMeshByModelId(modelId);
    }

    // If not found in mapping, try using the value directly as an IT model ID
    // NPC spawn packets contain direct model IDs, not database item IDs
    // Common NPC weapon model IDs are in the IT10000+ range
    return getEquipmentMeshByModelId(static_cast<int>(equipmentId));
}

irr::scene::IMesh* EquipmentModelLoader::buildMeshFromGeometry(
    const std::shared_ptr<ZoneGeometry>& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {

    if (!geometry || geometry->vertices.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Group triangles by texture index -> texture name
    std::map<std::string, std::vector<Triangle>> trianglesByTexture;
    for (const auto& tri : geometry->triangles) {
        std::string texName = "";
        if (tri.textureIndex < geometry->textureNames.size()) {
            texName = geometry->textureNames[tri.textureIndex];
        }
        trianglesByTexture[texName].push_back(tri);
    }

    // Create a mesh buffer for each texture group
    for (const auto& [texName, tris] : trianglesByTexture) {
        if (tris.empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Collect unique vertices used by this texture group
        std::map<uint32_t, uint32_t> vertexRemap;
        std::vector<irr::video::S3DVertex> bufferVerts;

        for (const auto& tri : tris) {
            for (uint32_t idx : {tri.v1, tri.v2, tri.v3}) {
                if (vertexRemap.find(idx) == vertexRemap.end()) {
                    if (idx < geometry->vertices.size()) {
                        const auto& v = geometry->vertices[idx];

                        irr::video::S3DVertex irrVert;
                        // EQ uses Z-up, Irrlicht uses Y-up: EQ(x,y,z) -> Irr(x,z,y)
                        irrVert.Pos.X = v.x;
                        irrVert.Pos.Y = v.z;  // Swap Y and Z
                        irrVert.Pos.Z = v.y;
                        irrVert.Normal.X = v.nx;
                        irrVert.Normal.Y = v.nz;
                        irrVert.Normal.Z = v.ny;
                        irrVert.TCoords.X = v.u;
                        irrVert.TCoords.Y = v.v;
                        // Vertex3D doesn't have color, use white
                        irrVert.Color = irr::video::SColor(255, 255, 255, 255);

                        vertexRemap[idx] = static_cast<uint32_t>(bufferVerts.size());
                        bufferVerts.push_back(irrVert);
                    }
                }
            }
        }

        // Add vertices to buffer
        for (const auto& vert : bufferVerts) {
            buffer->Vertices.push_back(vert);
        }

        // Add indices
        for (const auto& tri : tris) {
            auto it1 = vertexRemap.find(tri.v1);
            auto it2 = vertexRemap.find(tri.v2);
            auto it3 = vertexRemap.find(tri.v3);

            if (it1 != vertexRemap.end() && it2 != vertexRemap.end() && it3 != vertexRemap.end()) {
                buffer->Indices.push_back(static_cast<irr::u16>(it1->second));
                buffer->Indices.push_back(static_cast<irr::u16>(it2->second));
                buffer->Indices.push_back(static_cast<irr::u16>(it3->second));
            }
        }

        // Set material
        buffer->Material.MaterialType = irr::video::EMT_SOLID;
        buffer->Material.Lighting = true;
        buffer->Material.BackfaceCulling = false;

        // Try to load texture
        if (!texName.empty()) {
            std::string lowerTexName = texName;
            std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            auto texIt = textures.find(lowerTexName);
            if (texIt != textures.end() && texIt->second) {
                const auto& texInfo = texIt->second;
                if (!texInfo->data.empty()) {
                    irr::video::ITexture* tex = nullptr;

                    // Check if this is a DDS file and decode it
                    if (DDSDecoder::isDDS(texInfo->data)) {
                        DecodedImage decoded = DDSDecoder::decode(texInfo->data);
                        if (decoded.isValid()) {
                            // Convert RGBA to ARGB (Irrlicht's ECF_A8R8G8B8 format)
                            std::vector<uint8_t> argbPixels(decoded.pixels.size());
                            for (size_t i = 0; i < decoded.pixels.size(); i += 4) {
                                argbPixels[i + 0] = decoded.pixels[i + 2];  // B
                                argbPixels[i + 1] = decoded.pixels[i + 1];  // G
                                argbPixels[i + 2] = decoded.pixels[i + 0];  // R
                                argbPixels[i + 3] = decoded.pixels[i + 3];  // A
                            }

                            // Create Irrlicht image from converted ARGB data
                            irr::video::IImage* img = driver_->createImageFromData(
                                irr::video::ECF_A8R8G8B8,
                                irr::core::dimension2d<irr::u32>(decoded.width, decoded.height),
                                argbPixels.data(), false, false);
                            if (img) {
                                tex = driver_->addTexture(texInfo->name.c_str(), img);
                                img->drop();
                            }
                        }
                    } else {
                        // Non-DDS file, try loading directly
                        irr::io::IReadFile* memFile = fileSystem_->createMemoryReadFile(
                            texInfo->data.data(), static_cast<irr::s32>(texInfo->data.size()),
                            texInfo->name.c_str(), false);
                        if (memFile) {
                            tex = driver_->getTexture(memFile);
                            memFile->drop();
                        }
                    }

                    if (tex) {
                        buffer->Material.setTexture(0, tex);
                        buffer->Material.MaterialType = irr::video::EMT_SOLID;
                    }
                }
            }
        }

        buffer->recalculateBoundingBox();
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

} // namespace Graphics
} // namespace EQT
