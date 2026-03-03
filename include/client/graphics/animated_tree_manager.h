#ifndef EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H
#define EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H

#include "client/graphics/tree_wind_controller.h"
#include "client/graphics/tree_identifier.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <cstddef>
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
class AnimatedTreeManager : public TextureEvictionListener {
public:
    AnimatedTreeManager(irr::scene::ISceneManager* smgr,
                        irr::video::IVideoDriver* driver);
    ~AnimatedTreeManager();

    // Set constrained texture cache for budget tracking
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // TextureEvictionListener
    void onTextureEvicted(const std::string& name) override;

    /**
     * Initialize the manager with placeable objects.
     * Identifies tree objects and creates animated copies.
     * @param objects Vector of placeable object instances
     * @param textures Texture map for creating textured meshes
     */
    void initialize(const std::vector<ObjectInstance>& objects,
                    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Progressive initialization: begin phase (identify trees, store pending list).
     * Call initializeNextBatch() repeatedly until isInitializing() returns false.
     */
    void beginInitialize(const std::vector<ObjectInstance>& objects,
                         const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    /**
     * Progressive initialization: process next batch of trees.
     * @param batchSize Number of trees to create this call
     * @return true if initialization is complete
     */
    bool initializeNextBatch(int batchSize = 8);

    /**
     * Check if progressive initialization is in progress.
     */
    bool isInitializing() const { return progressiveInitActive_; }

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

        // BSP region index for PVS culling (SIZE_MAX = unknown)
        size_t bspRegion = SIZE_MAX;

        // Whether the node is currently in the scene graph
        bool inSceneGraph = true;
    };

    /**
     * Get read-only access to animated trees (for SimulationWorker registration).
     */
    const std::vector<AnimatedTree>& getAnimatedTrees() const { return animatedTrees_; }

    /**
     * Assign BSP regions to all trees for PVS culling.
     * Call once after tree initialization when BSP data is available.
     * @param bspTree Shared pointer to the zone BSP tree
     */
    void assignBspRegions(std::shared_ptr<BspTree> bspTree);

    /**
     * Update PVS visibility for all trees based on camera's BSP region.
     * Trees in non-visible regions are removed from scene graph.
     * @param cameraRegion Current camera BSP region (SIZE_MAX = no PVS)
     * @param bspTree The zone BSP tree (needed for visibility lookup)
     */
    void updatePvsVisibility(size_t cameraRegion, const std::shared_ptr<BspTree>& bspTree);

    /**
     * Get count of trees currently in the scene graph.
     */
    size_t getVisibleTreeCount() const;

private:

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

    // Texture cache (textures are owned by driver or constrained cache)
    std::unordered_map<std::string, irr::video::ITexture*> textureCache_;

    // Constrained texture cache for budget tracking (non-owning, may be null)
    ConstrainedTextureCache* constrainedCache_ = nullptr;

    // Wind simulation
    TreeWindController windController_;

    // Tree identification
    TreeIdentifier treeIdentifier_;

    // All animated trees
    std::vector<AnimatedTree> animatedTrees_;

    // Progressive initialization state
    bool progressiveInitActive_ = false;
    std::vector<size_t> pendingTreeIndices_;        // Indices into pendingObjects_ for trees to create
    size_t pendingTreeCursor_ = 0;                  // Next index in pendingTreeIndices_ to process
    const std::vector<ObjectInstance>* pendingObjects_ = nullptr;
    const std::map<std::string, std::shared_ptr<TextureInfo>>* pendingTextures_ = nullptr;

    // Configuration
    float renderDistance_ = 300.0f;  // Max distance to render trees (synced from main renderer)
    float updateDistance_ = 300.0f;  // Max distance to animate trees
    float lodDistance_ = 150.0f;     // Distance for reduced animation quality
    bool initialized_ = false;
    irr::s32 shaderMaterialSolid_ = -1;
    irr::s32 shaderMaterialAlphaTest_ = -1;

public:
    /**
     * Set render distance for tree visibility culling.
     * Should be synced with the main renderer's render distance.
     */
    void setRenderDistance(float distance) { renderDistance_ = distance; }
    float getRenderDistance() const { return renderDistance_; }

    // Set GLSL shader material type IDs for tree meshes (-1 = not available)
    void setShaderMaterialTypes(irr::s32 solidType, irr::s32 alphaTestType) {
        shaderMaterialSolid_ = solidType;
        shaderMaterialAlphaTest_ = alphaTestType;
    }
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ANIMATED_TREE_MANAGER_H
