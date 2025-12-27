#ifndef EQT_GRAPHICS_ANIMATED_TEXTURE_MANAGER_H
#define EQT_GRAPHICS_ANIMATED_TEXTURE_MANAGER_H

#include <irrlicht.h>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"

namespace EQT {
namespace Graphics {

// Tracks a scene node material that uses an animated texture
struct AnimatedMaterial {
    irr::scene::ISceneNode* node;
    irr::u32 materialIndex;
};

// State for a single animated texture
struct AnimatedTextureState {
    std::vector<irr::video::ITexture*> frameTextures;  // All frame textures
    int animationDelayMs = 100;                        // Milliseconds between frames
    int currentFrame = 0;                              // Current frame index
    float elapsedMs = 0;                               // Time since last frame change
    std::vector<irr::scene::IMeshBuffer*> affectedBuffers;  // Mesh buffers using this texture (legacy)
    std::vector<AnimatedMaterial> affectedMaterials;  // Scene node materials to update
};

// Manages animated textures and updates them over time
class AnimatedTextureManager {
public:
    AnimatedTextureManager(irr::video::IVideoDriver* driver, irr::io::IFileSystem* fileSystem);
    ~AnimatedTextureManager() = default;

    // Initialize from zone geometry and textures
    // Returns the number of animated textures found
    int initialize(const ZoneGeometry& geometry,
                   const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                   irr::scene::IMesh* mesh);

    // Add additional mesh buffers to existing animated textures
    // (e.g., for object meshes that share textures with the zone)
    void addMesh(const ZoneGeometry& geometry,
                 const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                 irr::scene::IMesh* mesh);

    // Register a scene node for animated texture updates
    // This scans the node's materials and registers any that use animated textures
    void addSceneNode(irr::scene::ISceneNode* node);

    // Update all animated textures based on elapsed time
    // Call this once per frame with the frame delta time
    void update(float deltaMs);

    // Get current texture for an animated texture by primary name
    irr::video::ITexture* getCurrentTexture(const std::string& primaryName) const;

    // Check if a texture name is animated
    bool isAnimated(const std::string& textureName) const;

    // Get number of animated textures being managed
    size_t getAnimatedTextureCount() const { return animatedTextures_.size(); }

    // Get debug info
    std::string getDebugInfo() const;

private:
    // Load a texture from raw data
    irr::video::ITexture* loadTexture(const std::string& name,
                                       const std::vector<char>& data);

    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::map<std::string, AnimatedTextureState> animatedTextures_;  // Keyed by primary texture name

    // Cache of all loaded textures (including frame textures)
    std::map<std::string, irr::video::ITexture*> textureCache_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ANIMATED_TEXTURE_MANAGER_H
