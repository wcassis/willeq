#pragma once

#include "boids_types.h"
#include "flock_controller.h"
#include "particle_types.h"  // For EnvironmentState, ZoneBiome
#include "tumbleweed_manager.h"  // For PlaceableBounds
#include <irrlicht.h>
#include <memory>
#include <vector>
#include <string>
#include <random>

namespace EQT {
namespace Graphics {
namespace Detail {
class SurfaceMap;
}
}
}

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * BoidsManager - Central manager for ambient creature flocking system.
 *
 * Responsibilities:
 * - Manages all active flocks
 * - Handles zone transitions and biome-based spawning
 * - Renders creatures as billboards
 * - Enforces creature budget limits
 * - Responds to quality settings changes
 */
class BoidsManager {
public:
    BoidsManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~BoidsManager();

    // Non-copyable
    BoidsManager(const BoidsManager&) = delete;
    BoidsManager& operator=(const BoidsManager&) = delete;

    /**
     * Initialize the boids system.
     * Call after renderer is fully initialized.
     * @param eqClientPath Path to EQ client for loading textures
     * @return true if initialized successfully
     */
    bool init(const std::string& eqClientPath);

    /**
     * Update all flocks.
     * @param deltaTime Time since last update (seconds)
     */
    void update(float deltaTime);

    /**
     * Render all creatures.
     * Call during the main render pass.
     */
    void render();

    // === Zone Transitions ===

    /**
     * Called when entering a new zone.
     * Sets up appropriate creature types based on zone biome.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);

    /**
     * Called when leaving a zone.
     * Clears all flocks.
     */
    void onZoneLeave();

    // === Settings ===

    /**
     * Set the overall quality level.
     * Affects creature budget and spawn frequency.
     */
    void setQuality(int quality);
    int getQuality() const { return quality_; }

    /**
     * Set the density multiplier (0-1).
     * Stacks with quality setting.
     */
    void setDensity(float density);
    float getDensity() const { return userDensity_; }

    /**
     * Enable or disable a specific creature type.
     */
    void setTypeEnabled(CreatureType type, bool enabled);
    bool isTypeEnabled(CreatureType type) const;

    /**
     * Enable or disable the entire boids system.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // === Environment State ===

    /**
     * Update time of day (0-24).
     */
    void setTimeOfDay(float hour);
    float getTimeOfDay() const { return envState_.timeOfDay; }

    /**
     * Update current weather.
     */
    void setWeather(WeatherType weather);
    WeatherType getWeather() const { return envState_.weather; }

    /**
     * Update wind parameters.
     */
    void setWind(const glm::vec3& direction, float strength);

    /**
     * Update player position (affects scatter behavior).
     */
    void setPlayerPosition(const glm::vec3& pos, float heading);

    /**
     * Set zone bounds for flock navigation.
     */
    void setZoneBounds(const glm::vec3& min, const glm::vec3& max);

    // === Collision Detection ===

    /**
     * Set the zone collision selector for terrain/obstacle avoidance.
     */
    void setCollisionSelector(irr::scene::ITriangleSelector* selector) {
        collisionSelector_ = selector;
    }

    /**
     * Set the surface map for ground height queries.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap) {
        surfaceMap_ = surfaceMap;
    }

    /**
     * Set placeable object bounds for collision avoidance.
     */
    void setPlaceableObjects(const std::vector<PlaceableBounds>& objects) {
        placeableObjects_ = objects;
    }

    /**
     * Add a single placeable object's bounds.
     */
    void addPlaceableBounds(const glm::vec3& min, const glm::vec3& max) {
        placeableObjects_.push_back({min, max});
    }

    /**
     * Clear all placeable object bounds.
     */
    void clearPlaceableObjects() { placeableObjects_.clear(); }

    // === Statistics ===

    /**
     * Get total number of active creatures across all flocks.
     */
    int getTotalActiveCreatures() const;

    /**
     * Get number of active flocks.
     */
    int getActiveFlockCount() const { return static_cast<int>(flocks_.size()); }

    /**
     * Get current creature budget (max creatures).
     */
    int getCreatureBudget() const { return budget_.maxCreatures; }

    /**
     * Get current biome.
     */
    ZoneBiome getCurrentBiome() const { return currentBiome_; }

    /**
     * Get debug info string for HUD display.
     */
    std::string getDebugInfo() const;

    /**
     * Reload settings from config file.
     * Call after editing config/environment_effects.json.
     */
    void reloadSettings();

private:
    /**
     * Determine which creature types are valid for the given biome.
     */
    std::vector<CreatureType> getCreatureTypesForBiome(ZoneBiome biome, bool isDay) const;

    /**
     * Try to spawn a new flock if conditions allow.
     */
    void trySpawnFlock();

    /**
     * Spawn a flock of the specified type.
     */
    void spawnFlock(CreatureType type);

    /**
     * Remove flocks that have exited bounds or expired.
     */
    void removeExpiredFlocks();

    /**
     * Update spawn timer and trigger spawns.
     */
    void updateSpawning(float deltaTime);

    /**
     * Check if player is near any flock (for scatter behavior).
     */
    void checkPlayerProximity();

    /**
     * Load the creature texture atlas.
     */
    bool loadCreatureAtlas(const std::string& path);

    /**
     * Create a default creature atlas (procedurally generated).
     */
    void createDefaultAtlas();

    /**
     * Render a single billboard-oriented quad.
     */
    void renderBillboard(const Creature& c, const irr::core::vector3df& cameraPos,
                         const irr::core::vector3df& cameraUp);

    /**
     * Get UV coordinates for a tile in the atlas.
     */
    void getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const;

    /**
     * Get a random spawn position for a new flock.
     */
    glm::vec3 getRandomSpawnPosition() const;

    // Irrlicht components
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;
    irr::video::SMaterial creatureMaterial_;

    // Flocks
    std::vector<std::unique_ptr<FlockController>> flocks_;

    // State
    bool enabled_ = true;
    bool initialized_ = false;
    int quality_ = 2;  // 0=Off, 1=Low, 2=Medium, 3=High
    float userDensity_ = 1.0f;
    BoidsBudget budget_;
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    std::string currentZoneName_;

    // Zone bounds
    glm::vec3 zoneBoundsMin_{-1000.0f};
    glm::vec3 zoneBoundsMax_{1000.0f};
    bool hasZoneBounds_ = false;

    // Spawning
    float spawnTimer_ = 0.0f;
    float spawnCooldown_ = 30.0f;  // Seconds between spawn attempts
    float scatterRadius_ = 20.0f;  // Distance to trigger scatter

    // Type enable flags
    bool typeEnabled_[static_cast<size_t>(CreatureType::Count)];

    // Environment state
    EnvironmentState envState_;

    // Collision detection
    irr::scene::ITriangleSelector* collisionSelector_ = nullptr;
    const Detail::SurfaceMap* surfaceMap_ = nullptr;
    std::vector<PlaceableBounds> placeableObjects_;

    // RNG
    std::mt19937 rng_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
