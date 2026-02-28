#ifndef EQT_GRAPHICS_S3D_LOADER_H
#define EQT_GRAPHICS_S3D_LOADER_H

#include "pfs.h"
#include "wld_loader.h"
#include "placeable.h"
#include <string>
#include <memory>
#include <map>

namespace EQT {
namespace Graphics {

// Object instance with resolved geometry
struct ObjectInstance {
    std::shared_ptr<Placeable> placeable;
    std::shared_ptr<ZoneGeometry> geometry;
};

// Single texture frame data
struct TextureFrame {
    std::string name;
    std::vector<char> data;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Texture information from S3D (supports animated textures)
struct TextureInfo {
    std::string name;               // Primary texture name (first frame for animated)
    std::vector<char> data;         // Primary texture data (first frame for animated)
    uint32_t width = 0;
    uint32_t height = 0;

    // Animation support
    bool isAnimated = false;                     // True if this is an animated texture
    int animationDelayMs = 0;                    // Milliseconds between frames
    std::vector<TextureFrame> frames;            // All frames (including first)

    // Helper to get frame count
    size_t frameCount() const { return isAnimated ? frames.size() : 1; }

    // Get total raw data bytes (for memory reporting)
    size_t rawDataBytes() const {
        size_t total = data.capacity();
        for (const auto& f : frames) total += f.data.capacity();
        return total;
    }
};

// Character model part with bone transform
struct CharacterPart {
    std::shared_ptr<ZoneGeometry> geometry;
    // Accumulated bone transform (world space)
    float shiftX = 0.0f, shiftY = 0.0f, shiftZ = 0.0f;
    float rotateX = 0.0f, rotateY = 0.0f, rotateZ = 0.0f;
};

// Skeleton bone with animation data
struct AnimatedBone {
    std::string name;
    int parentIndex;                                    // -1 for root bones
    std::vector<int> childIndices;
    std::shared_ptr<TrackDef> poseTrack;               // Default pose keyframes
    std::map<std::string, std::shared_ptr<TrackDef>> animationTracks;  // Animations keyed by anim code
};

// Character skeleton with animations
struct CharacterSkeleton {
    std::string modelCode;                             // e.g., "huf", "elf"
    std::vector<AnimatedBone> bones;                   // Bones in hierarchy order
    std::map<std::string, std::shared_ptr<Animation>> animations;  // Animations keyed by code (e.g., "l01", "c01")

    // Get bone index by name (-1 if not found)
    int getBoneIndex(const std::string& name) const {
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }
};

// Character model geometry (flattened from skeleton)
struct CharacterModel {
    std::string name;
    std::vector<std::shared_ptr<ZoneGeometry>> parts;  // Geometry parts from bone meshes (legacy, no transforms)
    std::vector<CharacterPart> partsWithTransforms;    // Parts with bone transforms applied (skinned)
    std::vector<CharacterPart> rawParts;               // Parts WITHOUT transforms (for animation)
    std::shared_ptr<SkeletonTrack> skeleton;           // Original skeleton data
    std::shared_ptr<CharacterSkeleton> animatedSkeleton;  // Skeleton with animation data
};

// Complete S3D zone data
struct S3DZone {
    std::shared_ptr<ZoneGeometry> geometry;
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    std::string zoneName;

    // WLD loader - provides access to BSP tree and per-region geometry for PVS culling
    std::shared_ptr<WldLoader> wldLoader;

    // Placeable objects
    std::vector<ObjectInstance> objects;

    // All object geometries from _obj.s3d (keyed by uppercase name, e.g., "DOOR1")
    // This includes objects not placed in the zone (like doors which are placed dynamically)
    std::map<std::string, std::shared_ptr<ZoneGeometry>> objectGeometries;

    // Object textures loaded from _obj.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> objectTextures;

    // Character models loaded from _chr.s3d
    std::vector<std::shared_ptr<CharacterModel>> characters;

    // Character textures loaded from _chr.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> characterTextures;

    // Light sources loaded from lights.wld
    std::vector<std::shared_ptr<ZoneLight>> lights;

    // CPU-side memory usage of all zone source data
    size_t getMemoryUsage() const {
        size_t total = sizeof(S3DZone);

        // Combined zone geometry (standalone, not shared via WldLoader)
        if (geometry) total += geometry->getFullMemoryUsage();

        // Zone texture raw pixel data (may be released after GPU upload)
        for (const auto& [name, tex] : textures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // WLD loader data (BSP tree, per-region geometries, placeables, tracks, etc.)
        if (wldLoader) total += wldLoader->getMemoryUsage();

        // Placeable objects (ObjectInstance = shared_ptr pair, geometry may be shared)
        total += objects.capacity() * sizeof(ObjectInstance);

        // Object geometries from _obj.s3d (standalone, not shared via WldLoader)
        for (const auto& [name, geom] : objectGeometries)
            if (geom) total += geom->getFullMemoryUsage();

        // Object textures
        for (const auto& [name, tex] : objectTextures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // Character models
        for (const auto& model : characters) {
            if (!model) continue;
            total += sizeof(CharacterModel);
            for (const auto& part : model->parts)
                if (part) total += part->getFullMemoryUsage();
            total += model->partsWithTransforms.capacity() * sizeof(CharacterPart);
            total += model->rawParts.capacity() * sizeof(CharacterPart);
            // Skeleton and animation data accounted via CharacterSkeleton below
            if (model->animatedSkeleton) {
                total += sizeof(CharacterSkeleton);
                for (const auto& bone : model->animatedSkeleton->bones) {
                    total += sizeof(AnimatedBone) + bone.name.capacity();
                    total += bone.childIndices.capacity() * sizeof(int);
                    if (bone.poseTrack)
                        total += bone.poseTrack->frames.capacity() * sizeof(BoneTransform);
                    for (const auto& [code, track] : bone.animationTracks)
                        if (track) total += track->frames.capacity() * sizeof(BoneTransform);
                }
                for (const auto& [code, anim] : model->animatedSkeleton->animations)
                    if (anim) total += sizeof(Animation) + anim->tracks.size() * 64;
            }
        }

        // Character textures
        for (const auto& [name, tex] : characterTextures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // Lights
        for (const auto& light : lights) {
            if (!light) continue;
            total += sizeof(ZoneLight);
            total += light->lightLevels.capacity() * sizeof(float);
            total += light->colors.capacity() * sizeof(std::tuple<float,float,float>);
        }

        return total;
    }

    // Release character model data (geometry, skeletons, textures).
    // The renderer's RaceModelLoader independently loads _chr.s3d into its own cache,
    // so zone->characters/characterTextures are unused duplicates after zone loading.
    void clearCharacterData() {
        characters.clear();
        characters.shrink_to_fit();
        characterTextures.clear();
    }

    // Release raw pixel data from all texture infos (zone, object, character).
    // Call after all Irrlicht textures and animated texture frames have been uploaded.
    // Returns the number of bytes freed.
    size_t releaseTexturePixelData() {
        size_t freed = 0;
        auto releaseMap = [&freed](std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) {
            for (auto& [name, texInfo] : texMap) {
                if (!texInfo) continue;
                freed += texInfo->data.capacity();
                texInfo->data.clear();
                texInfo->data.shrink_to_fit();
                for (auto& frame : texInfo->frames) {
                    freed += frame.data.capacity();
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
        };
        releaseMap(textures);
        releaseMap(objectTextures);
        releaseMap(characterTextures);
        return freed;
    }
};

// High-level S3D zone loader
class S3DLoader {
public:
    S3DLoader() = default;
    ~S3DLoader() = default;

    // Load a zone from an S3D file
    // archivePath: path to the .s3d file
    // Returns true on success
    bool loadZone(const std::string& archivePath);

    // Get the loaded zone data
    std::shared_ptr<S3DZone> getZone() const { return zone_; }

    // Get the zone geometry directly
    std::shared_ptr<ZoneGeometry> getGeometry() const {
        return zone_ ? zone_->geometry : nullptr;
    }

    // Get zone name extracted from filename
    const std::string& getZoneName() const { return zoneName_; }

    // Get error message if loading failed
    const std::string& getError() const { return error_; }

    // Get object instances
    const std::vector<ObjectInstance>& getObjects() const {
        return zone_ ? zone_->objects : emptyObjects_;
    }

    // Get object count
    size_t getObjectCount() const {
        return zone_ ? zone_->objects.size() : 0;
    }

    // Get character models
    const std::vector<std::shared_ptr<CharacterModel>>& getCharacters() const {
        return zone_ ? zone_->characters : emptyCharacters_;
    }

    // Get character count
    size_t getCharacterCount() const {
        return zone_ ? zone_->characters.size() : 0;
    }

    // Get light sources
    const std::vector<std::shared_ptr<ZoneLight>>& getLights() const {
        return zone_ ? zone_->lights : emptyLights_;
    }

    // Get light count
    size_t getLightCount() const {
        return zone_ ? zone_->lights.size() : 0;
    }

private:
    bool loadTextures(PfsArchive& archive);
    bool loadObjectTextures(PfsArchive& archive);
    bool loadCharacterTextures(PfsArchive& archive);
    bool loadObjects(const std::string& archivePath);
    bool loadCharacters(const std::string& archivePath);
    bool loadLights(const std::string& archivePath);
    void flattenSkeleton(const std::shared_ptr<SkeletonBone>& bone,
                         std::shared_ptr<CharacterModel>& model,
                         WldLoader& wldLoader,
                         float parentShiftX = 0.0f, float parentShiftY = 0.0f, float parentShiftZ = 0.0f,
                         float parentRotX = 0.0f, float parentRotY = 0.0f, float parentRotZ = 0.0f);

    // Apply skeleton bone transforms to mesh vertices based on vertex pieces
    void applySkinning(std::shared_ptr<ZoneGeometry>& mesh,
                       const std::shared_ptr<SkeletonTrack>& skeleton);

    // Build bone transforms array by traversing skeleton hierarchy
    void buildBoneTransforms(const std::shared_ptr<SkeletonBone>& bone,
                             std::vector<CharacterPart>& transforms,
                             int boneIndex,
                             float parentShiftX, float parentShiftY, float parentShiftZ,
                             float parentRotX, float parentRotY, float parentRotZ);

    // Build animated skeleton from WLD track data
    std::shared_ptr<CharacterSkeleton> buildAnimatedSkeleton(
        const std::string& modelCode,
        const std::shared_ptr<SkeletonTrack>& skeleton,
        WldLoader& wldLoader);

    std::string extractZoneName(const std::string& path);

    static const std::vector<ObjectInstance> emptyObjects_;
    static const std::vector<std::shared_ptr<CharacterModel>> emptyCharacters_;
    static const std::vector<std::shared_ptr<ZoneLight>> emptyLights_;

    std::shared_ptr<S3DZone> zone_;
    std::string zoneName_;
    std::string error_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_S3D_LOADER_H
