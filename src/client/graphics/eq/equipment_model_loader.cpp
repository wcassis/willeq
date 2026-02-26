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

    // Index all gequip archives (lower priority first, higher priority last for duplicates)
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

    return !equipmentModelIndex_.empty();
}

bool EquipmentModelLoader::loadEquipmentArchive(const std::string& archivePath) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Could not open archive: {}", archivePath);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Indexing equipment archive: {}", archivePath);

    // Index texture filenames (no data loaded)
    size_t texCount = 0;
    std::vector<std::string> texFiles;
    archive.getFilenames(".bmp", texFiles);

    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        // Don't overwrite if already indexed (earlier archives have priority)
        if (textureIndex_.find(lowerName) == textureIndex_.end()) {
            textureIndex_[lowerName] = {archivePath, texName};
            texCount++;
        }
    }

    // Find WLD files
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: No WLD file found in: {}", archivePath);
        return false;
    }

    // Parse each WLD to discover actor -> model ID mapping (needed for index)
    size_t modelCount = 0;
    for (const auto& wldName : wldFiles) {
        WldLoader wldLoader;
        if (!wldLoader.parseFromArchive(archivePath, wldName)) {
            LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to parse WLD: {}", wldName);
            continue;
        }

        const auto& objectDefs = wldLoader.getObjectDefs();
        const auto& geometries = wldLoader.getGeometries();

        LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: WLD {}: {} actors, {} geometries",
                wldName, objectDefs.size(), geometries.size());

        // Process each actor that matches IT### pattern
        for (const auto& [actorName, objDef] : objectDefs) {
            int modelId = parseModelIdFromActorName(actorName);
            if (modelId < 0) {
                continue;
            }

            // Skip if already indexed (from earlier archive)
            if (equipmentModelIndex_.find(modelId) != equipmentModelIndex_.end()) {
                continue;
            }

            // Discover texture names used by this model's geometries
            std::string prefix = "IT" + std::to_string(modelId);
            std::vector<std::string> modelTexNames;
            bool hasGeometry = false;

            for (const auto& geom : geometries) {
                if (geom && !geom->name.empty()) {
                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });
                    if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                        hasGeometry = true;
                        for (const auto& texName : geom->textureNames) {
                            bool found = false;
                            for (const auto& existing : modelTexNames) {
                                if (existing == texName) { found = true; break; }
                            }
                            if (!found) {
                                modelTexNames.push_back(texName);
                            }
                        }
                    }
                }
            }

            if (!hasGeometry) {
                continue;
            }

            LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Indexing model IT{} from actor {}",
                    modelId, actorName);

            equipmentModelIndex_[modelId] = {archivePath, wldName, actorName, modelTexNames};
            modelCount++;
        }
    }

    // Extract archive name for summary log
    size_t lastSlash = archivePath.find_last_of("/\\");
    std::string archiveName = (lastSlash != std::string::npos)
        ? archivePath.substr(lastSlash + 1) : archivePath;
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Indexed {} models, {} textures from {}",
             modelCount, texCount, archiveName);

    return true;
}

bool EquipmentModelLoader::loadEquipmentModelOnDemand(int modelId) {
    auto indexIt = equipmentModelIndex_.find(modelId);
    if (indexIt == equipmentModelIndex_.end()) {
        return false;
    }

    const auto& ref = indexIt->second;

    // Open the archive
    PfsArchive archive;
    if (!archive.open(ref.archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to open archive for model IT{}: {}",
                 modelId, ref.archivePath);
        return false;
    }

    // Parse WLD
    WldLoader wldLoader;
    if (!wldLoader.parseFromArchive(ref.archivePath, ref.wldName)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to parse WLD for model IT{}: {}",
                 modelId, ref.wldName);
        return false;
    }

    const auto& geometries = wldLoader.getGeometries();

    // Find geometries for this model
    std::string prefix = "IT" + std::to_string(modelId);
    std::vector<std::shared_ptr<ZoneGeometry>> actorGeometries;

    for (const auto& geom : geometries) {
        if (geom && !geom->name.empty()) {
            std::string geomName = geom->name;
            std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                actorGeometries.push_back(geom);
            }
        }
    }

    if (actorGeometries.empty()) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: No geometry found for model IT{}", modelId);
        return false;
    }

    // Build equipment model data (same as old loadEquipmentArchive logic, for one model)
    auto equipModel = std::make_shared<EquipmentModelData>();
    equipModel->modelName = "IT" + std::to_string(modelId);
    equipModel->modelId = modelId;

    size_t lastSlash = ref.archivePath.find_last_of("/\\");
    equipModel->sourceArchive = (lastSlash != std::string::npos)
        ? ref.archivePath.substr(lastSlash + 1) : ref.archivePath;
    equipModel->sourceWld = ref.wldName;

    auto combinedGeom = std::make_shared<ZoneGeometry>();
    combinedGeom->name = equipModel->modelName;

    for (const auto& geom : actorGeometries) {
        size_t vertexOffset = combinedGeom->vertices.size();

        if (!geom->name.empty()) {
            equipModel->geometryName += (equipModel->geometryName.empty() ? "" : ", ") + geom->name;
        }

        for (const auto& v : geom->vertices) {
            combinedGeom->vertices.push_back(v);
        }

        for (const auto& tri : geom->triangles) {
            Triangle newTri = tri;
            newTri.v1 += static_cast<uint32_t>(vertexOffset);
            newTri.v2 += static_cast<uint32_t>(vertexOffset);
            newTri.v3 += static_cast<uint32_t>(vertexOffset);
            combinedGeom->triangles.push_back(newTri);
        }

        for (const auto& texName : geom->textureNames) {
            bool found = false;
            for (const auto& existing : combinedGeom->textureNames) {
                if (existing == texName) { found = true; break; }
            }
            if (!found) {
                combinedGeom->textureNames.push_back(texName);
                equipModel->textureNames.push_back(texName);
            }
        }
    }

    equipModel->geometry = combinedGeom;

    // Load textures on demand
    for (const auto& texName : equipModel->textureNames) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        auto tex = getEquipmentTexture(lowerName);
        if (tex) {
            equipModel->textures[lowerName] = tex;
        }
    }

    equipmentModels_[modelId] = equipModel;

    LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Loaded model IT{} on demand ({} verts, {} tris, {} textures)",
             modelId, combinedGeom->vertices.size(), combinedGeom->triangles.size(),
             equipModel->textures.size());

    return true;
}

std::shared_ptr<TextureInfo> EquipmentModelLoader::getEquipmentTexture(const std::string& lowerName) {
    // Check cache first
    auto cacheIt = textures_.find(lowerName);
    if (cacheIt != textures_.end()) {
        return cacheIt->second;
    }

    // Look up in index
    auto indexIt = textureIndex_.find(lowerName);
    if (indexIt == textureIndex_.end()) {
        return nullptr;
    }

    // Extract from archive on demand
    const auto& ref = indexIt->second;
    PfsArchive archive;
    if (!archive.open(ref.archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to open archive for texture: {}", ref.archivePath);
        return nullptr;
    }

    std::vector<char> data;
    if (!archive.get(ref.entryName, data)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to extract texture: {} from {}", ref.entryName, ref.archivePath);
        return nullptr;
    }

    auto tex = std::make_shared<TextureInfo>();
    tex->name = ref.entryName;
    tex->data = std::move(data);

    textures_[lowerName] = tex;

    LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Loaded texture on demand: {} ({} bytes)",
              lowerName, tex->data.size());

    return tex;
}

const EquipmentModelData* EquipmentModelLoader::getEquipmentModelData(int modelId) {
    // Trigger on-demand loading if indexed but not yet loaded
    if (equipmentModels_.find(modelId) == equipmentModels_.end()) {
        if (equipmentModelIndex_.find(modelId) != equipmentModelIndex_.end()) {
            loadEquipmentModelOnDemand(modelId);
        }
    }
    auto it = equipmentModels_.find(modelId);
    return (it != equipmentModels_.end()) ? it->second.get() : nullptr;
}

irr::scene::IMesh* EquipmentModelLoader::getEquipmentMeshByModelId(int modelId) {
    if (modelId < 0) {
        return nullptr;
    }

    // Check mesh cache first
    auto cacheIt = meshCache_.find(modelId);
    if (cacheIt != meshCache_.end()) {
        return cacheIt->second;
    }

    // On-demand: load model from index if not yet loaded
    if (equipmentModels_.find(modelId) == equipmentModels_.end()) {
        if (equipmentModelIndex_.find(modelId) != equipmentModelIndex_.end()) {
            loadEquipmentModelOnDemand(modelId);
        }
    }

    // Find equipment model data
    auto modelIt = equipmentModels_.find(modelId);
    if (modelIt == equipmentModels_.end()) {
        return nullptr;
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

void EquipmentModelLoader::addMeshRef(int modelId) {
    if (modelId < 0) return;
    meshRefCounts_[modelId]++;
}

void EquipmentModelLoader::removeMeshRef(int modelId) {
    if (modelId < 0) return;
    auto it = meshRefCounts_.find(modelId);
    if (it == meshRefCounts_.end()) return;

    it->second--;
    if (it->second <= 0) {
        meshRefCounts_.erase(it);

        // Evict mesh from cache
        auto meshIt = meshCache_.find(modelId);
        if (meshIt != meshCache_.end()) {
            if (meshIt->second) {
                // Remove associated textures from driver
                auto modelIt = equipmentModels_.find(modelId);
                if (modelIt != equipmentModels_.end() && modelIt->second) {
                    for (const auto& texName : modelIt->second->textureNames) {
                        std::string lowerName = texName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                      [](unsigned char c) { return std::tolower(c); });
                        auto* tex = driver_->getTexture(lowerName.c_str());
                        if (tex) {
                            driver_->removeTexture(tex);
                        }
                    }
                }
                meshIt->second->drop();
            }
            meshCache_.erase(meshIt);
            LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Evicted mesh for model IT{}", modelId);
        }
    }
}

size_t EquipmentModelLoader::releaseRawTextureData() {
    size_t freed = 0;

    // Release from the master texture map
    for (auto& [name, tex] : textures_) {
        if (!tex) continue;
        freed += tex->rawDataBytes();
        tex->data.clear();
        tex->data.shrink_to_fit();
        for (auto& frame : tex->frames) {
            frame.data.clear();
            frame.data.shrink_to_fit();
        }
    }

    // Release from per-model texture maps (shared_ptrs may alias master map,
    // but data vectors are already cleared above; handle any unique copies)
    for (auto& [id, model] : equipmentModels_) {
        if (!model) continue;
        for (auto& [name, tex] : model->textures) {
            if (!tex) continue;
            size_t bytes = tex->rawDataBytes();
            if (bytes > 0) {
                freed += bytes;
                tex->data.clear();
                tex->data.shrink_to_fit();
                for (auto& frame : tex->frames) {
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
        }
    }

    return freed;
}

EquipmentModelLoader::MemoryStats EquipmentModelLoader::getMemoryStats() const {
    MemoryStats stats;
    stats.indexedModelCount = equipmentModelIndex_.size();
    stats.loadedGeometryCount = equipmentModels_.size();
    stats.meshCacheCount = meshCache_.size();
    stats.mappingCount = itemToModelMap_.size();

    // Count raw texture bytes in on-demand loaded textures
    for (const auto& [name, tex] : textures_) {
        if (tex) stats.rawTextureBytes += tex->rawDataBytes();
    }

    // Geometry data in loaded equipment models
    for (const auto& [id, model] : equipmentModels_) {
        if (model && model->geometry)
            stats.geometryBytes += model->geometry->getMemoryUsage();
    }

    // Irrlicht mesh vertex/index buffers
    for (const auto& [id, mesh] : meshCache_) {
        if (!mesh) continue;
        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            auto* buf = mesh->getMeshBuffer(i);
            if (buf) {
                stats.irrlichtMeshBytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                stats.irrlichtMeshBytes += buf->getIndexCount() * sizeof(irr::u16);
            }
        }
    }

    // Index data overhead
    stats.indexBytes = equipmentModelIndex_.size() * 128  // ~128 bytes per index entry (strings)
                     + textureIndex_.size() * 96           // ~96 bytes per texture ref
                     + itemToModelMap_.size() * 16;        // 16 bytes per mapping entry

    return stats;
}

void EquipmentModelLoader::adoptIndex(
    std::map<int, EquipmentModelRef>&& modelIndex,
    std::map<std::string, EquipmentTextureRef>&& textureIndex,
    std::map<uint32_t, int>&& itemToModelMap)
{
    equipmentModelIndex_ = std::move(modelIndex);
    textureIndex_ = std::move(textureIndex);
    itemToModelMap_ = std::move(itemToModelMap);
    archivesLoaded_ = true;
    mappingLoaded_ = !itemToModelMap_.empty();
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: adopted pre-built index ({} models, {} textures, {} mappings)",
             equipmentModelIndex_.size(), textureIndex_.size(), itemToModelMap_.size());
}

bool EquipmentModelLoader::indexEquipmentArchive(
    const std::string& archivePath,
    std::map<int, EquipmentModelRef>& outModelIndex,
    std::map<std::string, EquipmentTextureRef>& outTextureIndex)
{
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader::indexEquipmentArchive: Could not open: {}", archivePath);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Indexing equipment archive (bg): {}", archivePath);

    // Index texture filenames (no data loaded)
    size_t texCount = 0;
    std::vector<std::string> texFiles;
    archive.getFilenames(".bmp", texFiles);

    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (outTextureIndex.find(lowerName) == outTextureIndex.end()) {
            outTextureIndex[lowerName] = {archivePath, texName};
            texCount++;
        }
    }

    // Find WLD files
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader::indexEquipmentArchive: No WLD in: {}", archivePath);
        return false;
    }

    size_t modelCount = 0;
    for (const auto& wldName : wldFiles) {
        WldLoader wldLoader;
        if (!wldLoader.parseFromArchive(archivePath, wldName)) {
            LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader::indexEquipmentArchive: Failed to parse WLD: {}", wldName);
            continue;
        }

        const auto& objectDefs = wldLoader.getObjectDefs();
        const auto& geometries = wldLoader.getGeometries();

        for (const auto& [actorName, objDef] : objectDefs) {
            int modelId = parseModelIdFromActorName(actorName);
            if (modelId < 0) continue;
            if (outModelIndex.find(modelId) != outModelIndex.end()) continue;

            std::string prefix = "IT" + std::to_string(modelId);
            std::vector<std::string> modelTexNames;
            bool hasGeometry = false;

            for (const auto& geom : geometries) {
                if (geom && !geom->name.empty()) {
                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });
                    if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                        hasGeometry = true;
                        for (const auto& texName : geom->textureNames) {
                            bool found = false;
                            for (const auto& existing : modelTexNames) {
                                if (existing == texName) { found = true; break; }
                            }
                            if (!found) {
                                modelTexNames.push_back(texName);
                            }
                        }
                    }
                }
            }

            if (!hasGeometry) continue;

            outModelIndex[modelId] = {archivePath, wldName, actorName, modelTexNames};
            modelCount++;
        }
    }

    size_t lastSlash = archivePath.find_last_of("/\\");
    std::string archiveName = (lastSlash != std::string::npos)
        ? archivePath.substr(lastSlash + 1) : archivePath;
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: Indexed (bg) {} models, {} textures from {}",
             modelCount, texCount, archiveName);

    return true;
}

bool EquipmentModelLoader::buildEquipmentIndex(
    const std::string& clientPath,
    std::map<int, EquipmentModelRef>& outModelIndex,
    std::map<std::string, EquipmentTextureRef>& outTextureIndex)
{
    std::vector<std::string> archiveNames = {
        "gequip8.s3d", "gequip6.s3d", "gequip5.s3d", "gequip4.s3d",
        "gequip3.s3d", "gequip2.s3d", "gequip.s3d"
    };

    for (const auto& archiveName : archiveNames) {
        std::string archivePath = clientPath + archiveName;
        indexEquipmentArchive(archivePath, outModelIndex, outTextureIndex);
    }

    return !outModelIndex.empty();
}

int EquipmentModelLoader::loadItemModelMappingStatic(const std::string& jsonPath,
                                                      std::map<uint32_t, int>& outMap)
{
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        return -1;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "EquipmentModelLoader::loadItemModelMappingStatic: JSON parse error: {}", errors);
        return -1;
    }

    outMap.clear();
    for (const auto& key : root.getMemberNames()) {
        uint32_t itemId = std::stoul(key);
        int modelId = root[key].asInt();
        outMap[itemId] = modelId;
    }

    return static_cast<int>(outMap.size());
}

const EquipmentModelLoader::EquipmentModelRef* EquipmentModelLoader::getModelRef(int modelId) const {
    auto it = equipmentModelIndex_.find(modelId);
    return (it != equipmentModelIndex_.end()) ? &it->second : nullptr;
}

const EquipmentModelLoader::EquipmentTextureRef* EquipmentModelLoader::getTextureRef(const std::string& name) const {
    auto it = textureIndex_.find(name);
    return (it != textureIndex_.end()) ? &it->second : nullptr;
}

void EquipmentModelLoader::cacheEquipmentModelData(int modelId, std::shared_ptr<EquipmentModelData> data) {
    if (data) {
        equipmentModels_[modelId] = std::move(data);
    }
}

std::shared_ptr<EquipmentModelData> EquipmentModelLoader::extractEquipmentModelOffThread(
    const EquipmentModelRef& ref, int modelId) {

    // Open the archive (own file handle — thread-safe)
    PfsArchive archive;
    if (!archive.open(ref.archivePath)) {
        LOG_WARN(MOD_GRAPHICS, "extractEquipmentModelOffThread: Failed to open archive for IT{}: {}",
                 modelId, ref.archivePath);
        return nullptr;
    }

    // Parse WLD (creates own WldLoader — thread-safe)
    WldLoader wldLoader;
    if (!wldLoader.parseFromArchive(ref.archivePath, ref.wldName)) {
        LOG_WARN(MOD_GRAPHICS, "extractEquipmentModelOffThread: Failed to parse WLD for IT{}: {}",
                 modelId, ref.wldName);
        return nullptr;
    }

    const auto& geometries = wldLoader.getGeometries();

    // Find geometries for this model
    std::string prefix = "IT" + std::to_string(modelId);
    std::vector<std::shared_ptr<ZoneGeometry>> actorGeometries;

    for (const auto& geom : geometries) {
        if (geom && !geom->name.empty()) {
            std::string geomName = geom->name;
            std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                actorGeometries.push_back(geom);
            }
        }
    }

    if (actorGeometries.empty()) {
        LOG_WARN(MOD_GRAPHICS, "extractEquipmentModelOffThread: No geometry for IT{}", modelId);
        return nullptr;
    }

    // Build equipment model data
    auto equipModel = std::make_shared<EquipmentModelData>();
    equipModel->modelName = "IT" + std::to_string(modelId);
    equipModel->modelId = modelId;

    size_t lastSlash = ref.archivePath.find_last_of("/\\");
    equipModel->sourceArchive = (lastSlash != std::string::npos)
        ? ref.archivePath.substr(lastSlash + 1) : ref.archivePath;
    equipModel->sourceWld = ref.wldName;

    auto combinedGeom = std::make_shared<ZoneGeometry>();
    combinedGeom->name = equipModel->modelName;

    for (const auto& geom : actorGeometries) {
        size_t vertexOffset = combinedGeom->vertices.size();

        if (!geom->name.empty()) {
            equipModel->geometryName += (equipModel->geometryName.empty() ? "" : ", ") + geom->name;
        }

        for (const auto& v : geom->vertices) {
            combinedGeom->vertices.push_back(v);
        }

        for (const auto& tri : geom->triangles) {
            Triangle newTri = tri;
            newTri.v1 += static_cast<uint32_t>(vertexOffset);
            newTri.v2 += static_cast<uint32_t>(vertexOffset);
            newTri.v3 += static_cast<uint32_t>(vertexOffset);
            combinedGeom->triangles.push_back(newTri);
        }

        for (const auto& texName : geom->textureNames) {
            bool found = false;
            for (const auto& existing : combinedGeom->textureNames) {
                if (existing == texName) { found = true; break; }
            }
            if (!found) {
                combinedGeom->textureNames.push_back(texName);
                equipModel->textureNames.push_back(texName);
            }
        }
    }

    equipModel->geometry = combinedGeom;

    // Extract raw textures from archive (thread-safe — own archive handle)
    for (const auto& texName : equipModel->textureNames) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Try to find the texture in the same archive first
        std::vector<char> data;
        bool found = false;

        // Try exact name
        if (archive.get(texName, data)) {
            found = true;
        } else if (archive.get(lowerName, data)) {
            found = true;
        } else {
            // Try with .bmp extension
            std::string bmpName = lowerName;
            auto dotPos = bmpName.find_last_of('.');
            if (dotPos != std::string::npos) {
                bmpName = bmpName.substr(0, dotPos) + ".bmp";
                if (archive.get(bmpName, data)) {
                    found = true;
                }
            }
            // Try with .dds extension
            if (!found) {
                std::string ddsName = lowerName;
                dotPos = ddsName.find_last_of('.');
                if (dotPos != std::string::npos) {
                    ddsName = ddsName.substr(0, dotPos) + ".dds";
                    if (archive.get(ddsName, data)) {
                        found = true;
                    }
                }
            }
        }

        if (found && !data.empty()) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            equipModel->textures[lowerName] = tex;
        }
    }

    // Also try to find textures in other gequip archives using the texture ref list
    // (textures may be in a different archive than the geometry)
    for (const auto& texName : equipModel->textureNames) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (equipModel->textures.count(lowerName) > 0) continue;  // Already found

        // Look for the texture in the model ref's texture names
        // If it references textures from another archive, try to find them
        for (const auto& refTexName : ref.textureNames) {
            std::string refLower = refTexName;
            std::transform(refLower.begin(), refLower.end(), refLower.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            if (refLower == lowerName) {
                // This texture is referenced but not in main archive — it might be embedded
                // in a different gequip archive. We'll skip these for now; the main-thread
                // getEquipmentTexture() path will handle them.
                break;
            }
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "extractEquipmentModelOffThread: IT{} ({} verts, {} tris, {} textures)",
             modelId, combinedGeom->vertices.size(), combinedGeom->triangles.size(),
             equipModel->textures.size());

    return equipModel;
}

} // namespace Graphics
} // namespace EQT
