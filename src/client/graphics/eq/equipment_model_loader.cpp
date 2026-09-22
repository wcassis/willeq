#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/equipment_actor_geometry.h"
#include "client/willeq_database.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/constrained_texture_cache.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <algorithm>
#include <iostream>
#include <regex>

namespace EQT {
namespace Graphics {

namespace {
// T126: a deliberately narrow, LOCAL duplicate of
// renderer_zone_merge.cpp's swapTextureInMesh(). Reusing that primitive
// directly would pull renderer_zone_merge.cpp's own #includes
// (irrlicht_renderer.h, entity_renderer.h, door_manager.h) into every target
// that links equipment_model_loader.cpp -- including tools/model_viewer.cpp,
// whose CMakeLists.txt link set is deliberately minimal and does not build
// IrrlichtRenderer/DoorManager at all. Flagged explicitly rather than
// silently duplicated: if a THIRD copy of this loop is ever needed, extract
// swapTextureInMesh()/swapTextureInMaterial() into a small header with no
// dependency beyond <irrlicht.h> instead of adding a fourth divergent copy.
int swapTextureInMeshLocal(irr::scene::IMesh* mesh, irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!mesh) return 0;
    int swapped = 0;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf) continue;
        irr::video::SMaterial& mat = buf->getMaterial();
        for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
            if (mat.getTexture(t) == from) {
                mat.setTexture(t, to);
                ++swapped;
            }
        }
    }
    return swapped;
}
}  // namespace

EquipmentModelLoader::EquipmentModelLoader(irr::scene::ISceneManager* smgr,
                                           irr::video::IVideoDriver* driver,
                                           irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    meshBuilder_ = std::make_unique<ZoneMeshBuilder>(smgr, driver, fileSystem);
}

EquipmentModelLoader::~EquipmentModelLoader() {
    // T126: unregister before the cache (or this object) goes away -- same
    // discipline every other TextureEvictionListener in this codebase
    // follows (see T124's Bonus Findings: a missing unregister here was
    // exactly the class of dangling-listener hazard this guard family
    // exists to prevent, one level up).
    if (constrainedTextureCache_) {
        constrainedTextureCache_->removeEvictionListener(this);
    }
    // Drop all cached meshes
    for (auto& [id, mesh] : meshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
}

void EquipmentModelLoader::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    if (constrainedTextureCache_) {
        constrainedTextureCache_->removeEvictionListener(this);
    }
    constrainedTextureCache_ = cache;
    if (constrainedTextureCache_) {
        // Resolved ONCE, here, before any eviction can occur -- see class
        // doc comment in equipment_model_loader.h and onTextureEvicted()'s
        // non-reentrancy constraint.
        placeholder_ = constrainedTextureCache_->getPlaceholderTexture();
        constrainedTextureCache_->addEvictionListener(this);
    }
}

void EquipmentModelLoader::onTextureEvicted(const std::string& /*name*/,
                                             irr::video::ITexture* texture) {
    if (!texture) return;

    // T126: fix the CACHED MESH's own buffer material, not any scene node --
    // scene nodes (EntityVisual::primaryEquipNode/secondaryEquipNode) are
    // EntityMeshTextureGuard's job (renderer_zone_merge.cpp). This is the
    // root-cause half: meshCache_ entries persist across zone unload/reload,
    // so leaving their baked material stale means every FUTURE
    // addMeshSceneNode(mesh, ...) call (a re-equip, another entity wearing
    // the same item, a later zone) constructs a brand new node that inherits
    // the dangling pointer immediately, regardless of what
    // EntityMeshTextureGuard does for nodes that already existed at eviction
    // time. See the class doc comment for the full mechanism.
    int swapped = 0;
    for (auto& [modelId, mesh] : meshCache_) {
        if (!mesh) continue;
        swapped += swapTextureInMeshLocal(mesh, texture, placeholder_);
    }

    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[equiptex] EquipmentModelLoader: swapped {} cached equipment-mesh "
                  "material reference(s) to evicted texture -> placeholder (T126 -- prevents future "
                  "addMeshSceneNode() calls from this cache from reintroducing a dangling texture "
                  "pointer)", swapped);
    }
}

void EquipmentModelLoader::setClientPath(const std::string& path) {
    clientPath_ = path;
    // Ensure path ends with separator
    if (!clientPath_.empty() && clientPath_.back() != '/') {
        clientPath_ += '/';
    }
}

int EquipmentModelLoader::getModelIdForItem(uint32_t databaseItemId) const {
    auto* db = EQT::WilleqDatabase::globalInstance();
    if (db) return db->getItemModel(databaseItemId);
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

void EquipmentModelLoader::ensureIndexLoaded() {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
    if (archivesLoaded_) return;

    std::lock_guard<std::mutex> lock(initMutex_);
    if (archivesLoaded_) return;  // Double-check after acquiring lock
    if (clientPath_.empty()) return;

    // Build equipment index from gequip archives
    if (buildEquipmentIndex(clientPath_, equipmentModelIndex_, textureIndex_)) {
        LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: lazy-init built index ({} models, {} textures)",
                 equipmentModelIndex_.size(), textureIndex_.size());
    }

    // Item-to-model mapping handled on-demand via SQLite
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: item model mapping via on-demand SQLite");

    archivesLoaded_ = true;
}

bool EquipmentModelLoader::loadEquipmentArchives() {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "EquipmentModelLoader: Client path not set");
        return false;
    }

    // Index all equipment archives (lower priority first, higher priority last for duplicates)
    // gequip.s3d: IT27-153, gequip2: IT11-656, gequip3: IT10000-10105
    // gequip4: IT10015-11502, gequip5: IT10501-10523, gequip6: IT661-668, gequip8: IT10524-10733
    // gatesequip: IT10715-10732 (Gates of Discord), ldonequip: IT10700-10713 (Lost Dungeons)
    // lgequip/lgequip2: Luclin equipment, loyequip: Loyalty, vequip: Velious
    // frogequip: Froglok, obequip_lexit: OoW, lgequip_amr/lgequip_amr2: Luclin armor
    std::vector<std::string> archiveNames = {
        "obequip_lexit.s3d", "frogequip.s3d",
        "lgequip_amr2.s3d", "lgequip_amr.s3d",
        "loyequip.s3d", "vequip.s3d",
        "lgequip2.s3d", "lgequip.s3d",
        "ldonequip.s3d", "gatesequip.s3d",
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

            // T472 (B588): what the actor DRAWS, through its reference chain --
            // a hierarchical item such as IT48 has no mesh named IT48_*.
            auto resolved = resolveEquipmentActorGeometry(wldLoader, actorName);
            if (!resolved) {
                continue;
            }

            LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Indexing model IT{} from actor {}",
                    modelId, actorName);

            equipmentModelIndex_[modelId] = {archivePath, wldName, actorName, resolved->textureNames()};
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
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
    auto indexIt = equipmentModelIndex_.find(modelId);
    if (indexIt == equipmentModelIndex_.end()) {
        return false;
    }

    const auto& ref = indexIt->second;

    // T504: one open handle + one parsed WLD are reused across the items of
    // the same archive (see parsedWldLocked); parseMutex_ is held for the
    // whole build so the cache and equipmentModels_ see one writer at a time.
    std::lock_guard<std::mutex> parseLock(parseMutex_);
    WldLoader* wld = parsedWldLocked(ref.archivePath, ref.wldName);
    if (!wld) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to open/parse {} / {} for model IT{}",
                 ref.archivePath, ref.wldName, modelId);
        return false;
    }
    WldLoader& wldLoader = *wld;

    // T472 (B588): the actor's own reference chain, with each mesh's centre
    // and any rest DAG transform baked in (equipment_actor_geometry.cpp).
    std::shared_ptr<ZoneGeometry> combinedGeom = resolveEquipmentActorGeometry(wldLoader, ref.actorName);
    if (!combinedGeom) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: No geometry found for model IT{}", modelId);
        return false;
    }

    auto equipModel = std::make_shared<EquipmentModelData>();
    equipModel->modelName = "IT" + std::to_string(modelId);
    equipModel->modelId = modelId;

    size_t lastSlash = ref.archivePath.find_last_of("/\\");
    equipModel->sourceArchive = (lastSlash != std::string::npos)
        ? ref.archivePath.substr(lastSlash + 1) : ref.archivePath;
    equipModel->sourceWld = ref.wldName;

    combinedGeom->name = equipModel->modelName;
    equipModel->geometryName = ref.actorName;
    equipModel->textureNames = combinedGeom->textureNames();
    equipModel->geometry = combinedGeom;

    // Load textures on demand
    for (const auto& texName : equipModel->textureNames) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        auto tex = getEquipmentTextureLocked(lowerName);  // parseMutex_ held above
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
    std::lock_guard<std::mutex> parseLock(parseMutex_);
    return getEquipmentTextureLocked(lowerName);
}

std::shared_ptr<TextureInfo> EquipmentModelLoader::getEquipmentTextureLocked(const std::string& lowerName) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
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

    // Extract from archive on demand (T504: through the cached open handle;
    // parseMutex_ is held by the caller)
    const auto& ref = indexIt->second;
    PfsArchive* archivePtr = openArchiveLocked(ref.archivePath);
    if (!archivePtr) {
        LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader: Failed to open archive for texture: {}", ref.archivePath);
        return nullptr;
    }
    PfsArchive& archive = *archivePtr;

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
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
    if (modelId < 0) {
        return nullptr;
    }
    ensureIndexLoaded();

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
    ensureIndexLoaded();
    // T498 (board B755/B823): `equipmentId` is an appearance MATERIAL, i.e. an
    // IT model number, for every character -- spawn packets and wear changes
    // always carried one, and since T498 the local player's own weapon slots do
    // too (the wire's items.idfile, the server's own
    // Mob::GetEquipmentMaterial() rule). This used to try a DATABASE item-id
    // lookup through data/item_models.json first, which was right for nobody
    // but the local player and silently hijacked any peer whose material
    // happened to land on one of that file's keys (8 materials, 9 peq items --
    // review/T498/collision_census.tsv). getModelIdForItem() survives for the
    // offline tools, which genuinely do hold item ids.
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
        if (tri.textureIndex < geometry->textureNames().size()) {
            texName = geometry->textureNames()[tri.textureIndex];
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

        // P37: WLD Fragment 0x30 materialType (TransparentMasked) ground-truth
        // check, mirroring mesh_building.cpp's RaceModelLoader::buildMeshFromGeometry
        // and ZoneMeshBuilder's H02d check — equipped-item masked textures (e.g.
        // helm eyeholes) get the same alpha-test classification handoff to
        // EntityRenderer::applyAlphaTestMaterialType() (P21). All triangles in
        // this group share the same texName, hence the same textureIndex/masked
        // flag; use the first triangle's index.
        bool isMaskedMaterial = false;
        if (!tris.empty()) {
            uint32_t texIdx = tris.front().textureIndex;
            const auto& masked = geometry->textureMasked();
            isMaskedMaterial = (texIdx < masked.size()) && masked[texIdx];
        }

        // Set material
        buffer->Material.MaterialType = isMaskedMaterial
            ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF
            : irr::video::EMT_SOLID;
        buffer->Material.Lighting = true;
        buffer->Material.BackfaceCulling = false;

        if (!texName.empty() && constrainedTextureCache_) {
            std::string lowerTexName = texName;
            std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            auto texIt = textures.find(lowerTexName);
            if (texIt != textures.end() && texIt->second && !texIt->second->data.empty()) {
                // P46: applyPaletteMask=isMaskedMaterial — this texture's
                // WLD-masked status was already established above (the same
                // signal that picked EMT_TRANSPARENT_ALPHA_CHANNEL_REF), so
                // the runtime BMP decode now actually punches palette index
                // 0 transparent for it instead of hardcoding alpha=255.
                constrainedTextureCache_->getOrLoad(lowerTexName, texIt->second->data,
                                                     "uncategorized", "",
                                                     isMaskedMaterial);
                // P46d: uploadTexture must be told the same applyPaletteMask
                // getOrLoad() above was, or it looks up the wrong internal
                // cache-key variant (see ConstrainedTextureCache::cacheKey()).
                irr::video::ITexture* tex = constrainedTextureCache_->uploadTexture(
                    lowerTexName, isMaskedMaterial);
                if (tex) {
                    buffer->Material.setTexture(0, tex);
                    // Re-assign after setTexture (mirrors the pre-existing
                    // pattern) — MaterialType itself is texture-independent,
                    // no need to recompute isMaskedMaterial here.
                    buffer->Material.MaterialType = isMaskedMaterial
                        ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF
                        : irr::video::EMT_SOLID;
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
                    // DISABLED: unconstrained Path B cleanup — no unconstrained textures to remove
                    // for (const auto& texName : modelIt->second->textureNames) {
                    //     std::string lowerName = texName;
                    //     std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    //                   [](unsigned char c) { return std::tolower(c); });
                    //     if (constrainedTextureCache_ && constrainedTextureCache_->hasTexture(lowerName)) {
                    //         continue;  // Cache owns this texture — leave it for LRU eviction
                    //     }
                    //     auto* tex = driver_->getTexture(lowerName.c_str());
                    //     if (tex) {
                    //         driver_->removeTexture(tex);
                    //     }
                    // }
                }
                meshIt->second->drop();
            }
            meshCache_.erase(meshIt);
            LOG_DEBUG(MOD_GRAPHICS, "EquipmentModelLoader: Evicted mesh for model IT{}", modelId);
        }
    }
}

PfsArchive* EquipmentModelLoader::openArchiveLocked(const std::string& archivePath) {
    ++parseCacheRequests_;
    if (cachedArchive_ && cachedArchivePath_ == archivePath) return cachedArchive_.get();
    ++parseCacheOpens_;
    // Drop the previous archive AND its parse BEFORE opening the next one:
    // measured with the other order, the equipment peak rose 10.3 -> 13.7
    // MiB (old parse + new block store resident together). At most one
    // archive's worth is ever held here.
    cachedWld_.reset();          // a parse belongs to the archive it came from
    cachedWldName_.clear();
    cachedArchive_.reset();
    cachedArchivePath_.clear();
    auto archive = std::make_unique<PfsArchive>();
    if (!archive->open(archivePath)) return nullptr;
    cachedArchive_ = std::move(archive);
    cachedArchivePath_ = archivePath;
    return cachedArchive_.get();
}

WldLoader* EquipmentModelLoader::parsedWldLocked(const std::string& archivePath, const std::string& wldName) {
    PfsArchive* archive = openArchiveLocked(archivePath);
    if (!archive) return nullptr;
    if (cachedWld_ && cachedWldName_ == wldName) return cachedWld_.get();
    ++parseCacheParses_;
    auto wld = std::make_unique<WldLoader>();
    if (!wld->parseFromOpenArchive(*archive, wldName)) return nullptr;
    cachedWld_ = std::move(wld);
    cachedWldName_ = wldName;
    return cachedWld_.get();
}

void EquipmentModelLoader::releaseParseCache() {
    std::lock_guard<std::mutex> lock(parseMutex_);
    if (parseCacheRequests_ > 0) {
        LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: parse cache released: {} requests, {} archive opens, {} WLD parses",
                 parseCacheRequests_, parseCacheOpens_, parseCacheParses_);
        parseCacheRequests_ = parseCacheOpens_ = parseCacheParses_ = 0;
    }
    cachedWld_.reset();
    cachedWldName_.clear();
    cachedArchive_.reset();
    cachedArchivePath_.clear();
}

size_t EquipmentModelLoader::releaseRawTextureData() {
    releaseParseCache();  // T504: the progressive load is done; drop the parse cache with the raw bytes
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
                     + textureIndex_.size() * 96;          // ~96 bytes per texture ref

    return stats;
}

void EquipmentModelLoader::adoptIndex(
    std::map<int, EquipmentModelRef>&& modelIndex,
    std::map<std::string, EquipmentTextureRef>&& textureIndex)
{
    equipmentModelIndex_ = std::move(modelIndex);
    textureIndex_ = std::move(textureIndex);
    archivesLoaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "EquipmentModelLoader: adopted pre-built index ({} models, {} textures)",
             equipmentModelIndex_.size(), textureIndex_.size());
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
        if (!wldLoader.parseFromOpenArchive(archive, wldName)) {   // T504c Leg 2f: the archive is open
            LOG_WARN(MOD_GRAPHICS, "EquipmentModelLoader::indexEquipmentArchive: Failed to parse WLD: {}", wldName);
            continue;
        }

        const auto& objectDefs = wldLoader.getObjectDefs();

        for (const auto& [actorName, objDef] : objectDefs) {
            (void)objDef;
            int modelId = parseModelIdFromActorName(actorName);
            if (modelId < 0) continue;
            if (outModelIndex.find(modelId) != outModelIndex.end()) continue;

            // T472 (B588): resolved through the actor's reference chain.
            auto resolved = resolveEquipmentActorGeometry(wldLoader, actorName);
            if (!resolved) continue;

            outModelIndex[modelId] = {archivePath, wldName, actorName, resolved->textureNames()};
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

const std::vector<std::string>& EquipmentModelLoader::indexedArchiveNames() {
    // Relative priority of the kept archives is what it was in the 17-entry
    // list this replaces (first holder wins). Before Leg 2f the list also
    // read obequip_lexit, frogequip, lgequip_amr2, lgequip_amr, loyequip,
    // vequip, lgequip2, lgequip, ldonequip, gatesequip and gequip6 -- 622 of
    // the 1,081 indexed models came from archives the original client never
    // loads, and every zone load parsed all 17 WLDs whole.
    static const std::vector<std::string> names = {
        "gequip8.s3d", "gequip5.s3d", "gequip4.s3d",
        "gequip3.s3d", "gequip2.s3d", "gequip.s3d"
    };
    return names;
}

bool EquipmentModelLoader::buildEquipmentIndex(
    const std::string& clientPath,
    std::map<int, EquipmentModelRef>& outModelIndex,
    std::map<std::string, EquipmentTextureRef>& outTextureIndex)
{
    // gequip.s3d: IT27-153, gequip2: IT11-656, gequip3: IT10000-10105
    // gequip4: IT10015-11502, gequip5: IT10501-10523, gequip8: IT10524-10733
    for (const auto& archiveName : indexedArchiveNames()) {
        std::string archivePath = clientPath + archiveName;
        indexEquipmentArchive(archivePath, outModelIndex, outTextureIndex);
    }

    return !outModelIndex.empty();
}

const EquipmentModelLoader::EquipmentModelRef* EquipmentModelLoader::getModelRef(int modelId) {
    ensureIndexLoaded();
    auto it = equipmentModelIndex_.find(modelId);
    return (it != equipmentModelIndex_.end()) ? &it->second : nullptr;
}

const EquipmentModelLoader::EquipmentTextureRef* EquipmentModelLoader::getTextureRef(const std::string& name) {
    ensureIndexLoaded();
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
        LOG_FATAL(MOD_GRAPHICS, "Failed to load equipment archive: {} (model IT{})", ref.archivePath, modelId);
        return nullptr;
    }

    // Parse WLD (creates own WldLoader — thread-safe; T504: from the open handle)
    WldLoader wldLoader;
    if (!wldLoader.parseFromOpenArchive(archive, ref.wldName)) {
        LOG_FATAL(MOD_GRAPHICS, "Failed to parse equipment WLD: {} in {} (model IT{})", ref.wldName, ref.archivePath, modelId);
        return nullptr;
    }
    return extractFromParsed(archive, wldLoader, ref, modelId);
}

std::shared_ptr<EquipmentModelData> EquipmentModelLoader::extractEquipmentModelCached(
    const EquipmentModelRef& ref, int modelId) {
    // T504: the entity-load callers (prep worker, zone-load thread) hit the
    // same few gequip archives item after item; one open handle + one parsed
    // WLD are reused under parseMutex_ instead of an open + a full parse per
    // item (~10 MiB and ~0.3 s each). Serialising the two callers here is
    // cheaper than the parses it saves.
    std::lock_guard<std::mutex> parseLock(parseMutex_);
    WldLoader* wld = parsedWldLocked(ref.archivePath, ref.wldName);
    if (!wld) {
        LOG_FATAL(MOD_GRAPHICS, "Failed to open/parse equipment archive: {} / {} (model IT{})",
                  ref.archivePath, ref.wldName, modelId);
        return nullptr;
    }
    return extractFromParsed(*cachedArchive_, *wld, ref, modelId);
}

std::shared_ptr<EquipmentModelData> EquipmentModelLoader::extractFromParsed(
    PfsArchive& archive, WldLoader& wldLoader, const EquipmentModelRef& ref, int modelId) {
    // T472 (B588): the actor's own reference chain, with each mesh's centre
    // and any rest DAG transform baked in (equipment_actor_geometry.cpp).
    std::shared_ptr<ZoneGeometry> combinedGeom = resolveEquipmentActorGeometry(wldLoader, ref.actorName);
    if (!combinedGeom) {
        LOG_WARN(MOD_GRAPHICS, "extractEquipmentModelOffThread: No geometry for IT{}", modelId);
        return nullptr;
    }

    auto equipModel = std::make_shared<EquipmentModelData>();
    equipModel->modelName = "IT" + std::to_string(modelId);
    equipModel->modelId = modelId;

    size_t lastSlash = ref.archivePath.find_last_of("/\\");
    equipModel->sourceArchive = (lastSlash != std::string::npos)
        ? ref.archivePath.substr(lastSlash + 1) : ref.archivePath;
    equipModel->sourceWld = ref.wldName;

    combinedGeom->name = equipModel->modelName;
    equipModel->geometryName = ref.actorName;
    equipModel->textureNames = combinedGeom->textureNames();
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
