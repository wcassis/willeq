#pragma once

#include "particle_types.h"
#include "particle_emitter.h"
#include <irrlicht.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace EQT {
namespace Graphics {
namespace Detail {
    class SurfaceMap;  // Forward declaration
}
namespace Environment {

/**
 * ParticleManager - Central manager for all environmental particle effects.
 *
 * Responsibilities:
 * - Manages all particle emitters
 * - Handles zone transitions and biome selection
 * - Renders all particles using billboards
 * - Enforces particle budget limits
 * - Responds to quality settings changes
 */
class ParticleManager {
public:
    ParticleManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~ParticleManager();

    // Non-copyable
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    /**
     * Initialize the particle system.
     * Call after renderer is fully initialized.
     * @param eqClientPath Path to EQ client for loading textures
     * @return true if initialized successfully
     */
    bool init(const std::string& eqClientPath);

    /**
     * Update all particles.
     * @param deltaTime Time since last update (seconds)
     */
    void update(float deltaTime);

    /**
     * Render all particles.
     * Call during the main render pass.
     */
    void render();

    // === Zone Transitions ===

    /**
     * Called when entering a new zone.
     * Sets up appropriate emitters based on zone biome.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);

    /**
     * Called when leaving a zone.
     * Clears all particles and resets emitters.
     */
    void onZoneLeave();

    // === Settings ===

    /**
     * Set the overall quality level.
     * Affects particle budget and visual complexity.
     */
    void setQuality(EffectQuality quality);
    EffectQuality getQuality() const { return quality_; }

    /**
     * Set the density multiplier (0-1).
     * Stacks with quality setting.
     */
    void setDensity(float density);
    float getDensity() const { return userDensity_; }

    /**
     * Enable or disable a specific particle type.
     */
    void setTypeEnabled(ParticleType type, bool enabled);
    bool isTypeEnabled(ParticleType type) const;

    /**
     * Enable or disable the entire particle system.
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
    glm::vec3 getWindDirection() const { return envState_.windDirection; }
    float getWindStrength() const { return envState_.windStrength; }

    /**
     * Update player position for spawning particles around player.
     */
    void setPlayerPosition(const glm::vec3& pos, float heading);

    // === Statistics ===

    /**
     * Get total number of active particles across all emitters.
     */
    int getTotalActiveParticles() const;

    /**
     * Get current particle budget (max particles).
     */
    int getParticleBudget() const { return budget_.maxTotal; }

    /**
     * Get current biome.
     */
    ZoneBiome getCurrentBiome() const { return currentBiome_; }

    /**
     * Get debug info string for HUD display.
     */
    std::string getDebugInfo() const;

    /**
     * Reload settings from config file for all emitters.
     * Call after editing config/environment_effects.json.
     */
    void reloadSettings();

    /**
     * Set the surface map for shoreline detection.
     * Called by renderer after loading zone surface data.
     * Propagates to all emitters that need terrain data.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap);

private:
    /**
     * Create emitters appropriate for the given biome.
     */
    void setupEmittersForBiome(ZoneBiome biome);

    /**
     * Clear all emitters.
     */
    void clearEmitters();

    /**
     * Load the particle texture atlas.
     */
    bool loadParticleAtlas(const std::string& path);

    /**
     * Create a default particle atlas (procedurally generated).
     */
    void createDefaultAtlas();

    /**
     * Render a single billboard-oriented quad.
     */
    void renderBillboard(const Particle& p, const irr::core::vector3df& cameraPos,
                         const irr::core::vector3df& cameraUp);

    /**
     * Get UV coordinates for a tile in the atlas.
     */
    void getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const;

    // Irrlicht components
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;
    irr::video::SMaterial particleMaterial_;

    // Emitters
    std::vector<std::unique_ptr<ParticleEmitter>> emitters_;

    // State
    bool enabled_ = true;
    bool initialized_ = false;
    EffectQuality quality_ = EffectQuality::Medium;
    float userDensity_ = 1.0f;
    ParticleBudget budget_;
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    std::string currentZoneName_;

    // Type enable flags
    bool typeEnabled_[static_cast<size_t>(ParticleType::Count)];

    // Environment state
    EnvironmentState envState_;

    // Surface map for shoreline detection (owned externally, e.g., by DetailObjectManager)
    const Detail::SurfaceMap* surfaceMap_ = nullptr;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
