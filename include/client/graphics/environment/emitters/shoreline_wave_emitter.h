#pragma once

#include "client/graphics/environment/particle_emitter.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/detail/surface_map.h"
#include <vector>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * ShorelineWaveEmitter - Foam and spray particles at water edges.
 *
 * Characteristics:
 * - Particles spawn along detected shoreline (water/land edges)
 * - Wave-like oscillating motion (sinusoidal height)
 * - Two particle types: foam (larger, longer-lived) and spray (smaller, short-lived)
 * - Intensity scales with wind strength
 * - Only active when player is near water in zones with shorelines
 *
 * Settings are loaded from config/environment_effects.json and can be
 * reloaded at runtime with /reloadeffects.
 */
class ShorelineWaveEmitter : public ParticleEmitter {
public:
    ShorelineWaveEmitter();
    ~ShorelineWaveEmitter() override = default;

    /**
     * Check if this emitter should be active.
     * Waves appear only when near water edges.
     */
    bool shouldBeActive(const EnvironmentState& env) const override;

    /**
     * Called when entering a zone.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome) override;

    /**
     * Reload settings from config file.
     */
    void reloadSettings() override;

    /**
     * Update particles and shoreline detection.
     */
    void update(float deltaTime, const EnvironmentState& env) override;

    /**
     * Set the surface map for shoreline detection.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap) override { surfaceMap_ = surfaceMap; }

protected:
    /**
     * Initialize a newly spawned wave particle.
     */
    void initParticle(Particle& p, const EnvironmentState& env) override;

    /**
     * Update a wave particle's movement.
     */
    void updateParticle(Particle& p, float deltaTime, const EnvironmentState& env) override;

    /**
     * Calculate spawn rate based on conditions.
     */
    float getSpawnRate(const EnvironmentState& env) const override;

private:
    /**
     * Shoreline point data.
     */
    struct ShorelinePoint {
        glm::vec3 position;     // World position of the shoreline
        glm::vec3 normal;       // Direction pointing toward land (away from water)
        float waterHeight;      // Water surface height at this point
    };

    /**
     * Detect shoreline points near the player.
     * Uses SurfaceMap to find water/land edges.
     */
    void detectShorelineNearPlayer(const glm::vec3& playerPos);

    /**
     * Check if a position is on a shoreline (water adjacent to non-water).
     * Also returns the estimated water surface Z (from adjacent land height).
     */
    bool isShorelineCell(float x, float y, glm::vec3& outNormal, float& outWaterSurfaceZ) const;

    /**
     * Get a random shoreline point from cached points.
     */
    const ShorelinePoint* getRandomShorelinePoint() const;

    // Cached settings from config (reloaded on reloadSettings())
    EnvironmentEffectsConfig::EmitterSettings settings_;

    // Surface map for water detection
    const Detail::SurfaceMap* surfaceMap_ = nullptr;

    // Cached shoreline points (updated periodically)
    std::vector<ShorelinePoint> shorelinePoints_;
    float shorelineCacheTimer_ = 0.0f;
    glm::vec3 lastCachePosition_{0.0f};

    // Wave animation state
    float wavePhase_ = 0.0f;
    float waveFrequency_ = 2.0f;    // Waves per second

    // State
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    bool zoneHasWater_ = false;

    // Detection parameters
    float detectionRadius_ = 40.0f;     // How far to scan for shorelines
    float cacheUpdateInterval_ = 2.0f;  // Seconds between shoreline rescans
    float cacheMovementThreshold_ = 10.0f;  // Rescan if player moves this far
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
