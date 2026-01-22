#pragma once

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/detail/surface_map.h"
#include <irrlicht.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * Configuration for tumbleweeds loaded from JSON.
 */
struct TumbleweedSettings {
    bool enabled = true;
    int maxActive = 10;              // Max simultaneous tumbleweeds
    float spawnRate = 0.1f;          // Spawns per second
    float spawnDistance = 80.0f;     // Spawn distance from player
    float despawnDistance = 120.0f;  // Remove when this far from player
    float minSpeed = 2.0f;           // Minimum roll speed
    float maxSpeed = 8.0f;           // Maximum roll speed
    float windInfluence = 1.5f;      // How much wind affects speed
    float bounceDecay = 0.6f;        // Velocity retention on bounce
    float maxLifetime = 60.0f;       // Max seconds before despawn
    float groundOffset = 0.3f;       // Height above ground
    float sizeMin = 0.6f;            // Minimum scale
    float sizeMax = 1.4f;            // Maximum scale
    int maxBounces = 20;             // Max bounces before despawn
};

/**
 * A single tumbleweed instance.
 */
struct TumbleweedInstance {
    glm::vec3 position{0.0f};        // World position (EQ coords)
    glm::vec3 velocity{0.0f};        // Current velocity
    glm::vec3 rotation{0.0f};        // Current rotation (degrees)
    glm::vec3 angularVelocity{0.0f}; // Rotation speed (degrees/sec)
    float radius = 0.5f;             // Collision radius
    float size = 1.0f;               // Scale factor
    float lifetime = 0.0f;           // Time alive
    float distanceTraveled = 0.0f;   // Total distance traveled
    uint32_t bounceCount = 0;        // Number of collisions
    bool active = false;             // Is this instance in use
    irr::scene::IMeshSceneNode* node = nullptr;  // Visual representation
};

/**
 * Collision result for tumbleweed physics.
 */
enum class TumbleweedCollisionType {
    None,
    Geometry,   // Zone walls/terrain features
    Object,     // Placeable objects
    Water,      // Water surface (sink)
    Boundary    // Zone boundary
};

struct TumbleweedCollision {
    bool hit = false;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    TumbleweedCollisionType type = TumbleweedCollisionType::None;
};

/**
 * Bounding box for placeable objects.
 */
struct PlaceableBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/**
 * TumbleweedManager - Manages rolling tumbleweeds in desert/plains zones.
 *
 * Tumbleweeds spawn upwind from the player and roll across the terrain,
 * bouncing off obstacles and despawning when they travel too far,
 * hit water, or exceed their lifetime.
 */
class TumbleweedManager {
public:
    TumbleweedManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~TumbleweedManager();

    /**
     * Initialize the manager. Creates mesh pool.
     */
    bool init();

    /**
     * Update all tumbleweeds. Call each frame.
     */
    void update(float deltaTime);

    /**
     * Set the current environment state (wind, player position, etc.)
     */
    void setEnvironmentState(const EnvironmentState& state) { envState_ = state; }

    /**
     * Set the zone collision selector for geometry collision.
     */
    void setCollisionSelector(irr::scene::ITriangleSelector* selector) {
        zoneCollisionSelector_ = selector;
    }

    /**
     * Set the surface map for ground height and surface type queries.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap) {
        surfaceMap_ = surfaceMap;
    }

    /**
     * Set placeable object bounds for collision.
     */
    void setPlaceableObjects(const std::vector<PlaceableBounds>& objects) {
        placeableObjects_ = objects;
    }

    /**
     * Add a single placeable object's bounds.
     */
    void addPlaceableBounds(const glm::vec3& min, const glm::vec3& max);

    /**
     * Clear all placeable object bounds.
     */
    void clearPlaceableObjects() { placeableObjects_.clear(); }

    /**
     * Reload settings from config file.
     */
    void reloadSettings();

    /**
     * Enable/disable tumbleweeds.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_ && settings_.enabled; }

    /**
     * Get number of active tumbleweeds.
     */
    int getActiveCount() const;

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

    /**
     * Called when entering a new zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);

    /**
     * Called when leaving a zone.
     */
    void onZoneLeave();

private:
    /**
     * Try to spawn a new tumbleweed.
     */
    void trySpawn();

    /**
     * Spawn a tumbleweed at the given position.
     */
    bool spawnTumbleweed(const glm::vec3& position);

    /**
     * Select a valid spawn position.
     */
    glm::vec3 selectSpawnPosition();

    /**
     * Check if a position is valid for spawning.
     */
    bool isValidSpawnPosition(const glm::vec3& pos);

    /**
     * Update a single tumbleweed's physics.
     */
    void updateTumbleweed(TumbleweedInstance& tw, float deltaTime);

    /**
     * Check for collisions along a movement path.
     */
    TumbleweedCollision checkCollisions(const glm::vec3& from, const glm::vec3& to, float radius);

    /**
     * Handle a collision (bounce, despawn, etc.)
     */
    void handleCollision(TumbleweedInstance& tw, const TumbleweedCollision& collision);

    /**
     * Get ground height at a position.
     */
    float getGroundHeight(float x, float y);

    /**
     * Update the visual representation of a tumbleweed.
     */
    void updateVisuals(TumbleweedInstance& tw, float deltaTime);

    /**
     * Despawn a tumbleweed (return to pool).
     */
    void despawnTumbleweed(TumbleweedInstance& tw);

    /**
     * Create the tumbleweed mesh.
     */
    irr::scene::IMesh* createTumbleweedMesh();

    /**
     * Get a tumbleweed instance from the pool.
     */
    TumbleweedInstance* getInactiveInstance();

    /**
     * Random float in range.
     */
    float randomFloat(float min, float max) const;

    // Irrlicht references
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    // Collision
    irr::scene::ITriangleSelector* zoneCollisionSelector_ = nullptr;
    const Detail::SurfaceMap* surfaceMap_ = nullptr;
    std::vector<PlaceableBounds> placeableObjects_;

    // Tumbleweed instances (pool)
    std::vector<TumbleweedInstance> tumbleweeds_;
    irr::scene::IMesh* tumbleweedMesh_ = nullptr;
    irr::video::ITexture* tumbleweedTexture_ = nullptr;

    /**
     * Create procedural tumbleweed texture with branchy pattern.
     */
    irr::video::ITexture* createTumbleweedTexture();

    // State
    EnvironmentState envState_;
    TumbleweedSettings settings_;
    bool enabled_ = true;
    bool initialized_ = false;
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    std::string currentZoneName_;

    // Spawning
    float spawnTimer_ = 0.0f;
    float spawnCooldown_ = 1.0f;  // Minimum time between spawns
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
