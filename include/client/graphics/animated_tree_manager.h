#ifndef EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H
#define EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H

#include "client/graphics/tree_wind_controller.h"
#include "client/graphics/tree_identifier.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <irrlicht.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace EQT {
namespace Graphics {

/**
 * Manages animated tree meshes for wind-driven movement.
 *
 * This class identifies tree meshes from zone geometry, creates animated
 * copies, and updates vertex positions each frame based on wind simulation.
 * Uses CPU vertex animation for software renderer compatibility.
 */
class AnimatedTreeManager {
public:
    AnimatedTreeManager(irr::scene::ISceneManager* smgr,
                        irr::video::IVideoDriver* driver);
    ~AnimatedTreeManager();

    /**
     * Initialize the manager with placeable objects.
     * Identifies tree objects and creates animated copies.
     * @param objects Vector of placeable object instances
     * @param textures Texture map for creating textured meshes
     */
    void initialize(const std::vector<ObjectInstance>& objects,
                    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Load wind configuration.
     * @param configPath Command-line specified config path (optional)
     * @param zoneName Current zone name for zone-specific config (optional)
     */
    void loadConfig(const std::string& configPath = "", const std::string& zoneName = "");

    /**
     * Set the current weather type.
     * @param weather The new weather type
     */
    void setWeather(WeatherType weather);

    /**
     * Update tree animations (call each frame).
     * @param deltaTime Time since last update in seconds
     * @param cameraPos Current camera position for LOD/culling
     */
    void update(float deltaTime, const irr::core::vector3df& cameraPos);

    /**
     * Clean up all animated tree meshes.
     */
    void cleanup();

    /**
     * Check if tree animation is enabled.
     */
    bool isEnabled() const { return windController_.isEnabled(); }

    /**
     * Set enabled state.
     */
    void setEnabled(bool enabled) { windController_.setEnabled(enabled); }

    /**
     * Get number of animated trees.
     */
    size_t getAnimatedTreeCount() const { return animatedTrees_.size(); }

    /**
     * Get debug information string.
     */
    std::string getDebugInfo() const;

    /**
     * Check if an object name/texture matches tree patterns.
     * Used by static object renderer to skip trees that will be animated.
     */
    bool isTreeObject(const std::string& name, const std::string& texture) const {
        return treeIdentifier_.isTreeMesh(name, texture);
    }

    /**
     * Access the wind controller for external configuration.
     */
    TreeWindController& getWindController() { return windController_; }
    const TreeWindController& getWindController() const { return windController_; }

    /**
     * Access the tree identifier for external configuration.
     */
    TreeIdentifier& getTreeIdentifier() { return treeIdentifier_; }
    const TreeIdentifier& getTreeIdentifier() const { return treeIdentifier_; }

private:
    /**
     * Data for a single mesh buffer within an animated tree.
     */
    struct AnimatedBuffer {
        irr::scene::SMeshBuffer* buffer = nullptr;
        std::vector<irr::core::vector3df> basePositions;
        std::vector<float> vertexHeights;
    };

    /**
     * Data for a single animated tree mesh.
     */
    struct AnimatedTree {
        // Irrlicht scene node for this tree
        irr::scene::IMeshSceneNode* node = nullptr;

        // The mesh we created (owned by this struct)
        irr::scene::SMesh* mesh = nullptr;

        // Animated buffers (one per texture)
        std::vector<AnimatedBuffer> buffers;

        // Unique seed for this tree (for wind phase variation)
        float meshSeed = 0.0f;

        // World position of the mesh center
        irr::core::vector3df worldPosition;

        // Bounding box for distance culling
        irr::core::aabbox3df bounds;

        // Source geometry name (for debugging)
        std::string name;
    };

    /**
     * Identify and process tree objects from placeables.
     */
    void identifyTrees(const std::vector<ObjectInstance>& objects,
                       const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Create an animated tree from a placeable object instance.
     */
    void createAnimatedTree(const ObjectInstance& object,
                            const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Update animation for a single tree.
     */
    void updateTreeAnimation(AnimatedTree& tree);

    /**
     * Calculate normalized height for a vertex within the mesh bounds.
     */
    float calculateVertexHeight(const irr::core::vector3df& vertex,
                                float minY, float maxY) const;

    /**
     * Generate a unique seed for a tree based on its position.
     */
    float generateTreeSeed(const irr::core::vector3df& position) const;

    /**
     * Build an Irrlicht mesh from zone geometry for animation.
     */
    irr::scene::SMesh* buildAnimatedMesh(const ZoneGeometry& geometry,
                                          const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Load or retrieve a cached texture.
     */
    irr::video::ITexture* getOrLoadTexture(const std::string& name,
                                            const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    // Irrlicht components
    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;

    // Texture cache (textures are owned by driver, we just cache pointers)
    std::unordered_map<std::string, irr::video::ITexture*> textureCache_;

    // Wind simulation
    TreeWindController windController_;

    // Tree identification
    TreeIdentifier treeIdentifier_;

    // All animated trees
    std::vector<AnimatedTree> animatedTrees_;

    // Configuration
    float updateDistance_ = 300.0f;  // Max distance to animate trees
    float lodDistance_ = 150.0f;     // Distance for reduced animation quality
    bool initialized_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H
