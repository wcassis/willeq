#ifndef EQT_GRAPHICS_RACE_MODEL_LOADER_H
#define EQT_GRAPHICS_RACE_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include "animated_mesh_scene_node.h"
#include "race_codes.h"
#include "equipment_textures.h"
#include <irrlicht.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for EntityAppearance
namespace EQT { namespace Graphics { struct EntityAppearance; } }

namespace EQT {
namespace Graphics {

// Race model data - combined geometry for a race/gender combo
struct RaceModelData {
    std::shared_ptr<ZoneGeometry> combinedGeometry;      // Skinned geometry (bone transforms applied)
    std::shared_ptr<ZoneGeometry> rawGeometry;           // Raw geometry (no bone transforms, for animation)
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    std::string raceName;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    float scale = 1.0f;

    // Animation data
    std::shared_ptr<CharacterSkeleton> skeleton;     // Skeleton with animation tracks
    std::vector<VertexPiece> vertexPieces;           // Vertex-to-bone mapping for skinning
};

// Loads and caches character models by race ID
class RaceModelLoader {
public:
    RaceModelLoader(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    irr::io::IFileSystem* fileSystem);
    ~RaceModelLoader();

    // Set the base path for EQ client files
    void setClientPath(const std::string& path);

    // Load all global character models from global_chr.s3d
    bool loadGlobalModels();

    // Load additional numbered global#_chr.s3d files (global2-global7)
    bool loadNumberedGlobalModels();

    // Load armor textures from global17-23_amr.s3d
    bool loadArmorTextures();

    // Load zone-specific character models from zone_chr.s3d
    bool loadZoneModels(const std::string& zoneName);

    // Set the current zone name (for zone-specific model loading)
    void setCurrentZone(const std::string& zoneName);

    // Check if a race model is available
    bool hasRaceModel(uint16_t raceId, uint8_t gender = 0) const;

    // Get an Irrlicht mesh for a specific race/gender
    // Returns nullptr if not found (caller should use placeholder)
    irr::scene::IMesh* getMeshForRace(uint16_t raceId, uint8_t gender = 0);

    // Get an Irrlicht mesh for a specific race/gender with appearance variants
    // headVariant: which head mesh to use (0 = default, maps to HUMHE00, HUMHE01, etc.)
    // bodyVariant: which body mesh to use (0 = default, maps to HUM, HUM01, etc.)
    irr::scene::IMesh* getMeshForRaceWithAppearance(uint16_t raceId, uint8_t gender,
                                                     uint8_t headVariant, uint8_t bodyVariant);

    // Get an animated mesh for a specific race/gender
    // Returns nullptr if not found or no animation data available
    EQAnimatedMesh* getAnimatedMeshForRace(uint16_t raceId, uint8_t gender = 0);

    // Get an animated mesh with appearance-based variant selection
    // headVariant: which head mesh to use (0 = default)
    // bodyVariant: which body mesh to use (0 = default)
    // textureVariant: equipment texture (0=naked, 1=leather, 2=chain, 3=plate, 10+=robes)
    EQAnimatedMesh* getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                   uint8_t headVariant, uint8_t bodyVariant,
                                                   uint8_t textureVariant = 0);

    // Create an animated mesh scene node for a race
    // The caller is responsible for adding the node to the scene
    EQAnimatedMeshSceneNode* createAnimatedNode(uint16_t raceId, uint8_t gender,
                                                  irr::scene::ISceneNode* parent = nullptr,
                                                  irr::s32 id = -1);

    // Create an animated mesh scene node with appearance-based variants
    EQAnimatedMeshSceneNode* createAnimatedNodeWithAppearance(uint16_t raceId, uint8_t gender,
                                                               uint8_t headVariant, uint8_t bodyVariant,
                                                               irr::scene::ISceneNode* parent = nullptr,
                                                               irr::s32 id = -1);

    // Create an animated mesh scene node with full equipment appearance
    EQAnimatedMeshSceneNode* createAnimatedNodeWithEquipment(uint16_t raceId, uint8_t gender,
                                                              const EntityAppearance& appearance,
                                                              irr::scene::ISceneNode* parent = nullptr,
                                                              irr::s32 id = -1);

    // Get race model data
    std::shared_ptr<RaceModelData> getRaceModelData(uint16_t raceId, uint8_t gender = 0);

    // Get race scale factor (some races are larger/smaller)
    // Delegates to the free function in race_codes.h
    float getRaceScale(uint16_t raceId) const { return EQT::Graphics::getRaceScale(raceId); }

    // Get the S3D filename for a race (for loading from zone archives)
    // Delegates to the free function in race_codes.h
    static std::string getRaceModelFilename(uint16_t raceId, uint8_t gender) {
        return EQT::Graphics::getRaceModelFilename(raceId, gender);
    }

    // Get a 3-letter race code (HUM, ELF, DWF, etc.)
    // Delegates to the free function in race_codes.h
    static std::string getRaceCode(uint16_t raceId) {
        return EQT::Graphics::getRaceCode(raceId);
    }

    // Get number of loaded race models
    size_t getLoadedModelCount() const { return loadedModels_.size(); }

    // Old models mode - when true, only load from global_chr.s3d (classic models)
    // When false, prefer race-specific S3D files (Luclin+ models)
    void setUseOldModels(bool useOld);
    bool isUsingOldModels() const { return useOldModels_; }

    // Clear cached meshes (call after toggling old/new models)
    void clearCache();

private:
    // Load model from a specific S3D file
    bool loadModelFromS3D(const std::string& s3dPath, uint16_t raceId, uint8_t gender);

    // Load model from global_chr.s3d by searching for race code
    bool loadModelFromGlobalChr(uint16_t raceId, uint8_t gender);

    // Load model from a numbered global#_chr.s3d file
    bool loadModelFromNumberedGlobal(int globalNum, uint16_t raceId, uint8_t gender);

    // Load model from zone-specific _chr.s3d file
    bool loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender);

    // Search all loaded global character archives for a model
    bool searchAllGlobalsForModel(uint16_t raceId, uint8_t gender);

    // Search all zone _chr.s3d files in the client directory for a model
    bool searchZoneChrFilesForModel(uint16_t raceId, uint8_t gender);

    // Build an Irrlicht mesh from geometry data
    // bodyTextureVariant: 0=naked, 1=leather, 2=chain, 3=plate, 10+=robes
    // raceCode: 3-letter code like "QCF", "HUM" for texture name transformation
    irr::scene::IMesh* buildMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint8_t bodyTextureVariant = 0,
        const std::string& raceCode = "");

    // Build an Irrlicht mesh with equipment texture overrides
    irr::scene::IMesh* buildMeshWithEquipment(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        const std::string& raceCode,
        const uint32_t* equipment);  // Array of 9 material IDs

    // Create cache key for race/gender combo
    static uint32_t makeCacheKey(uint16_t raceId, uint8_t gender) {
        return (static_cast<uint32_t>(raceId) << 8) | gender;
    }

    // Create cache key for race/gender/variant combo (includes head, body, and texture variant)
    static uint64_t makeVariantCacheKey(uint16_t raceId, uint8_t gender, uint8_t headVariant, uint8_t bodyVariant, uint8_t textureVariant = 0) {
        return (static_cast<uint64_t>(raceId) << 32) |
               (static_cast<uint64_t>(gender) << 24) |
               (static_cast<uint64_t>(headVariant) << 16) |
               (static_cast<uint64_t>(bodyVariant) << 8) |
               textureVariant;
    }

    // Load model from global_chr.s3d with specific variants
    bool loadModelFromGlobalChrWithVariants(uint16_t raceId, uint8_t gender,
                                            uint8_t headVariant, uint8_t bodyVariant);

    // Load model from global_chr.s3d with specific variants including raw geometry for animation
    bool loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,
                                                         uint8_t headVariant, uint8_t bodyVariant);

    // Build a merged texture map from all sources (global + numbered globals + zone)
    // Order: global_chr.s3d -> global2-7_chr.s3d (new only) -> zone_chr.s3d (overrides)
    std::map<std::string, std::shared_ptr<TextureInfo>> getMergedTextures();

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::unique_ptr<ZoneMeshBuilder> meshBuilder_;

    std::string clientPath_;

    // Cache of loaded race model data
    std::map<uint32_t, std::shared_ptr<RaceModelData>> loadedModels_;

    // Cache of Irrlicht meshes (separate from model data for memory management)
    std::map<uint32_t, irr::scene::IMesh*> meshCache_;

    // Cache of animated meshes
    std::map<uint32_t, EQAnimatedMesh*> animatedMeshCache_;

    // Cache of variant animated meshes (key includes head/body variant)
    std::map<uint64_t, EQAnimatedMesh*> variantAnimatedMeshCache_;

    // Cache for variant-specific meshes (key includes head/body variant)
    std::map<uint64_t, std::shared_ptr<RaceModelData>> variantModels_;
    std::map<uint64_t, irr::scene::IMesh*> variantMeshCache_;

    // Global character data loaded from global_chr.s3d
    std::vector<std::shared_ptr<CharacterModel>> globalCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> globalTextures_;
    bool globalModelsLoaded_ = false;

    // Numbered global character data (global2-7_chr.s3d)
    std::map<int, std::vector<std::shared_ptr<CharacterModel>>> numberedGlobalCharacters_;
    std::map<int, std::map<std::string, std::shared_ptr<TextureInfo>>> numberedGlobalTextures_;
    bool numberedGlobalsLoaded_ = false;

    // Armor textures from global17-23_amr.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> armorTextures_;
    bool armorTexturesLoaded_ = false;

    // Zone-specific character data
    std::string currentZoneName_;
    std::vector<std::shared_ptr<CharacterModel>> zoneCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> zoneTextures_;
    bool zoneModelsLoaded_ = false;

    // Old models mode (classic models from global_chr.s3d only)
    bool useOldModels_ = true;  // Default to old models

    // Cache for other _chr.s3d files loaded during searchZoneChrFilesForModel
    // Key is lowercase filename (e.g., "crushbone_chr.s3d")
    struct OtherChrCache {
        std::vector<std::shared_ptr<CharacterModel>> characters;
        std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    };
    std::map<std::string, OtherChrCache> otherChrCaches_;

    // Temporary storage for vertex data during animated mesh building
    // (populated by buildMeshFromGeometry, consumed by getAnimatedMeshForRace)
    std::vector<irr::video::S3DVertex> originalVerticesForAnimation_;
    std::vector<VertexMapping> vertexMappingForAnimation_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RACE_MODEL_LOADER_H
