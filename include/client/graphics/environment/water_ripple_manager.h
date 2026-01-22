#pragma once

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/detail/surface_map.h"
#include <glm/glm.hpp>
#include <vector>
#include <random>

namespace irr {
namespace video {
class IVideoDriver;
class ITexture;
}
namespace scene {
class ISceneManager;
}
}

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * WaterRippleSettings - Configuration for water ripple effects.
 */
struct WaterRippleSettings {
    bool enabled = true;
    int maxRipples = 100;           // Maximum concurrent ripples
    float spawnRate = 30.0f;        // Ripples per second at max intensity
    float spawnRadius = 40.0f;      // Radius around player to spawn ripples
    float minSize = 0.3f;           // Initial ripple size
    float maxSize = 2.0f;           // Maximum ripple size (at end of life)
    float lifetime = 1.2f;          // Ripple lifetime in seconds
    float expansionSpeed = 1.5f;    // How fast ripple expands
    float heightOffset = 0.05f;     // Height above water surface
    // Color (light blue-white)
    float colorR = 0.85f;
    float colorG = 0.92f;
    float colorB = 1.0f;
    float colorA = 0.6f;
};

/**
 * WaterRipple - A single expanding ripple ring on water surface.
 */
struct WaterRipple {
    glm::vec3 position;     // World position (on water surface)
    float age = 0.0f;       // Time since spawn
    float maxAge = 1.2f;    // Lifetime
    float size = 0.3f;      // Current ring size
    float maxSize = 2.0f;   // Maximum size at end of life
    float alpha = 1.0f;     // Current opacity
    bool active = false;    // Is this ripple in use
};

/**
 * WaterRippleManager - Manages rain ripples on water surfaces.
 *
 * Creates animated ripple rings where raindrops hit water surfaces.
 * Uses the SurfaceMap to detect water cells and spawns ripples
 * at random positions within range when rain is active.
 *
 * Features:
 * - Pool-based ripple management for efficiency
 * - Distance-based culling
 * - Intensity-scaled spawn rate
 * - Smooth expansion and fade animation
 */
class WaterRippleManager {
public:
    WaterRippleManager();
    ~WaterRippleManager() = default;

    // Non-copyable
    WaterRippleManager(const WaterRippleManager&) = delete;
    WaterRippleManager& operator=(const WaterRippleManager&) = delete;

    /**
     * Initialize the ripple manager.
     * @param driver Video driver for rendering
     * @param smgr Scene manager
     * @param atlasTexture Particle atlas texture
     * @return true if initialized successfully
     */
    bool initialize(irr::video::IVideoDriver* driver,
                    irr::scene::ISceneManager* smgr,
                    irr::video::ITexture* atlasTexture);

    /**
     * Set the surface map for water detection.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap) { surfaceMap_ = surfaceMap; }

    /**
     * Set rain intensity (0-10, controls spawn rate).
     */
    void setRainIntensity(uint8_t intensity) { rainIntensity_ = intensity; }

    /**
     * Enable/disable ripples.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_ && settings_.enabled; }

    /**
     * Update ripple settings.
     */
    void setSettings(const WaterRippleSettings& settings);
    const WaterRippleSettings& getSettings() const { return settings_; }

    /**
     * Update all ripples.
     * @param deltaTime Time since last update
     * @param playerPos Current player position
     * @param cameraPos Current camera position (for culling)
     */
    void update(float deltaTime, const glm::vec3& playerPos, const glm::vec3& cameraPos);

    /**
     * Render all active ripples.
     * Should be called during alpha-blended pass.
     * @param cameraPos Camera position for billboard orientation
     * @param cameraUp Camera up vector
     */
    void render(const glm::vec3& cameraPos, const glm::vec3& cameraUp);

    /**
     * Called when entering a new zone.
     */
    void onZoneEnter(const std::string& zoneName);

    /**
     * Called when leaving a zone.
     */
    void onZoneLeave();

    /**
     * Get count of active ripples (for debug display).
     */
    int getActiveRippleCount() const;

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    /**
     * Spawn a new ripple at a random water position.
     */
    void spawnRipple(const glm::vec3& playerPos);

    /**
     * Find a water position near the given point.
     * @param center Center of search area
     * @param radius Search radius
     * @param outPos Output position if found
     * @return true if valid water position found
     */
    bool findWaterPosition(const glm::vec3& center, float radius, glm::vec3& outPos);

    /**
     * Get an inactive ripple from the pool.
     * @return Pointer to ripple, or nullptr if pool exhausted
     */
    WaterRipple* getInactiveRipple();

    /**
     * Render a single ripple as a horizontal billboard.
     */
    void renderRipple(const WaterRipple& ripple);

    // Settings
    WaterRippleSettings settings_;
    bool enabled_ = true;
    bool initialized_ = false;

    // External references
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;
    const Detail::SurfaceMap* surfaceMap_ = nullptr;

    // Ripple pool
    std::vector<WaterRipple> ripples_;

    // Spawn state
    float spawnAccumulator_ = 0.0f;
    uint8_t rainIntensity_ = 0;

    // Random number generation
    std::mt19937 rng_;

    // Zone state
    std::string currentZoneName_;
    bool zoneHasWater_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
