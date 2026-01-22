#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <random>

namespace irr {
namespace scene {
class ISceneManager;
class IMeshSceneNode;
class IMesh;
}
namespace video {
class IVideoDriver;
class ITexture;
}
}

// Forward declaration at global scope
class RaycastMesh;

namespace EQT {

namespace Graphics {
namespace Environment {

class AccumulationHeightmap;

/**
 * SnowAccumulationSettings - Configuration for snow accumulation.
 */
struct SnowAccumulationSettings {
    bool enabled = true;

    // Accumulation rates
    float accumulationRate = 0.01f;     // Units per second at intensity 10
    float meltRate = 0.005f;            // Units per second when not snowing
    float maxDepth = 0.5f;              // Maximum snow depth

    // Visual thresholds
    float dustingThreshold = 0.05f;     // Light dusting visible
    float partialThreshold = 0.15f;     // Partial coverage
    float fullThreshold = 0.3f;         // Full snow cover

    // Rendering
    float decalSize = 4.0f;             // Size of snow decals
    float decalSpacing = 3.0f;          // Spacing between decals
    int maxDecals = 500;                // Maximum visible decals
    float viewDistance = 100.0f;        // Max distance to render decals

    // Update frequency
    float updateInterval = 0.25f;       // Seconds between accumulation updates

    // Shelter detection
    bool shelterDetectionEnabled = true;
    float shelterRayHeight = 50.0f;     // Height to check for overhead cover
    float slopeThreshold = 0.7f;        // Slope angle threshold (cos) for sliding

    // Colors
    float snowColorR = 0.95f;
    float snowColorG = 0.97f;
    float snowColorB = 1.0f;
    float snowColorA = 0.85f;
};

/**
 * SnowDecal - Single snow patch decal for rendering.
 */
struct SnowDecal {
    glm::vec3 position;
    float size;
    float depth;
    float alpha;
    bool active = false;
};

/**
 * SnowAccumulationSystem - Heightmap-based snow buildup system.
 *
 * Tracks snow accumulation depth across a grid around the player,
 * handles gradual buildup during snowfall and melting when it stops,
 * and renders snow coverage using ground decals.
 *
 * Features:
 * - Grid-based accumulation tracking that follows the player
 * - Gradual buildup based on snow intensity
 * - Melting when snow stops
 * - Shelter detection (no accumulation under cover)
 * - Slope-based sliding (steep areas don't accumulate)
 * - Visual rendering via ground decals
 */
class SnowAccumulationSystem {
public:
    SnowAccumulationSystem();
    ~SnowAccumulationSystem();

    // Non-copyable
    SnowAccumulationSystem(const SnowAccumulationSystem&) = delete;
    SnowAccumulationSystem& operator=(const SnowAccumulationSystem&) = delete;

    /**
     * Initialize the accumulation system.
     * @param smgr Scene manager
     * @param driver Video driver
     * @param atlasTexture Particle atlas texture (for snow decal)
     * @return true if initialized successfully
     */
    bool initialize(irr::scene::ISceneManager* smgr,
                    irr::video::IVideoDriver* driver,
                    irr::video::ITexture* atlasTexture);

    /**
     * Clean up resources.
     */
    void shutdown();

    /**
     * Update accumulation settings.
     */
    void setSettings(const SnowAccumulationSettings& settings);
    const SnowAccumulationSettings& getSettings() const { return settings_; }

    /**
     * Set the raycast mesh for shelter detection.
     * Note: RaycastMesh::raycast is not const, so we need a non-const pointer.
     */
    void setRaycastMesh(RaycastMesh* raycastMesh) { raycastMesh_ = raycastMesh; }

    /**
     * Set current snow intensity (0-10).
     */
    void setSnowIntensity(uint8_t intensity) { snowIntensity_ = intensity; }

    /**
     * Update the accumulation system.
     * @param deltaTime Time since last update
     * @param playerPos Current player position
     */
    void update(float deltaTime, const glm::vec3& playerPos);

    /**
     * Render snow decals.
     * @param cameraPos Camera position for distance culling
     */
    void render(const glm::vec3& cameraPos);

    /**
     * Enable/disable the system.
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_ && settings_.enabled; }

    /**
     * Check if snow is currently visible.
     */
    bool hasVisibleSnow() const;

    /**
     * Get snow depth at a world position.
     */
    float getSnowDepth(float worldX, float worldY) const;

    /**
     * Called when entering a new zone.
     */
    void onZoneEnter(const std::string& zoneName, bool isIndoor);

    /**
     * Called when leaving a zone.
     */
    void onZoneLeave();

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    /**
     * Update accumulation based on current snow intensity.
     */
    void updateAccumulation(float deltaTime);

    /**
     * Update melting when not snowing.
     */
    void updateMelting(float deltaTime);

    /**
     * Update shelter detection for nearby cells.
     */
    void updateShelterDetection(const glm::vec3& playerPos);

    /**
     * Check if a position has overhead shelter.
     */
    bool checkShelter(float worldX, float worldY, float worldZ) const;

    /**
     * Update visible decals based on current accumulation.
     */
    void updateDecals(const glm::vec3& playerPos);

    /**
     * Render a single snow decal.
     */
    void renderDecal(const SnowDecal& decal);

    // Settings
    SnowAccumulationSettings settings_;
    bool enabled_ = true;
    bool initialized_ = false;

    // Irrlicht objects
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;

    // Accumulation heightmap
    std::unique_ptr<AccumulationHeightmap> heightmap_;

    // Raycast mesh for shelter detection (not owned)
    RaycastMesh* raycastMesh_ = nullptr;

    // Current state
    uint8_t snowIntensity_ = 0;
    glm::vec3 lastPlayerPos_{0.0f};
    float updateAccumulator_ = 0.0f;
    float shelterUpdateAccumulator_ = 0.0f;

    // Decal pool
    std::vector<SnowDecal> decals_;

    // Zone state
    std::string currentZoneName_;
    bool isIndoorZone_ = false;

    // Random number generation
    std::mt19937 rng_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
