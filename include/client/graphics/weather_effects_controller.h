#pragma once

#include "client/graphics/weather_system.h"
#include "client/graphics/environment/particle_types.h"
#include <glm/glm.hpp>
#include <memory>
#include <functional>
#include <random>
#include <vector>
#include <cstdint>

namespace irr {
namespace scene {
class ISceneManager;
}
namespace video {
class IVideoDriver;
class SColor;
}
}

namespace EQT {
namespace Graphics {

class SkyRenderer;

namespace Environment {
class ParticleManager;
class RainEmitter;
class RainSplashEmitter;
class SnowEmitter;
class WaterRippleManager;
class StormCloudLayer;
class SnowAccumulationSystem;
}  // namespace Environment

namespace Detail {
class SurfaceMap;
}  // namespace Detail

}  // namespace Graphics
}  // namespace EQT

// Forward declaration at global scope
class RaycastMesh;

namespace EQT {
namespace Graphics {

/**
 * WeatherEffectsConfig - Configuration for weather visual effects.
 */
struct WeatherEffectsConfig {
    // Rain settings
    struct Rain {
        bool enabled = true;
        float dropSpeed = 25.0f;
        float spawnRadius = 50.0f;
        float spawnHeight = 80.0f;
        float windInfluence = 0.3f;
        bool splashEnabled = true;
        int splashParticles = 3;
    } rain;

    // Snow settings
    struct Snow {
        bool enabled = true;
        float fallSpeed = 3.0f;
        float swayAmplitude = 2.0f;
        float swayFrequency = 0.5f;
        bool accumulationEnabled = false;  // Future feature
        float maxAccumulationDepth = 0.5f;
    } snow;

    // Storm/atmosphere settings
    struct Storm {
        bool skyDarkeningEnabled = true;
        float maxDarkening = 0.6f;         // 0-1, how dark sky gets
        bool lightningEnabled = true;
        float lightningMinInterval = 10.0f;
        float lightningMaxInterval = 30.0f;
        float flashDuration = 0.1f;
        float boltDuration = 0.15f;
    } storm;
};

/**
 * WeatherEffectsController - Central controller for weather visual effects.
 *
 * Coordinates rain/snow particles, sky darkening, lightning, and other
 * weather-related visual effects. Responds to OP_Weather packets from server.
 */
class WeatherEffectsController : public IWeatherListener {
public:
    WeatherEffectsController(
        irr::scene::ISceneManager* smgr,
        irr::video::IVideoDriver* driver,
        Environment::ParticleManager* particleManager,
        SkyRenderer* skyRenderer
    );
    ~WeatherEffectsController();

    // Non-copyable
    WeatherEffectsController(const WeatherEffectsController&) = delete;
    WeatherEffectsController& operator=(const WeatherEffectsController&) = delete;

    /**
     * Initialize weather effects system.
     * @return true if initialized successfully
     */
    bool initialize();

    /**
     * Set weather from OP_Weather packet.
     * @param type Weather type (0=none, 1=rain, 2=snow)
     * @param intensity Weather intensity (1-10)
     */
    void setWeather(uint8_t type, uint8_t intensity);

    /**
     * IWeatherListener implementation - called when WeatherSystem changes.
     */
    void onWeatherChanged(WeatherType newWeather) override;

    /**
     * Update weather effects each frame.
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * Render weather effects (lightning bolts, etc.).
     * Call after main scene render but before UI.
     */
    void render();

    /**
     * Get current weather type.
     */
    uint8_t getCurrentType() const { return currentType_; }

    /**
     * Get current weather intensity.
     */
    uint8_t getCurrentIntensity() const { return currentIntensity_; }

    /**
     * Check if it's currently raining.
     */
    bool isRaining() const { return currentType_ == 1 && currentIntensity_ > 0; }

    /**
     * Check if it's currently snowing.
     */
    bool isSnowing() const { return currentType_ == 2 && currentIntensity_ > 0; }

    /**
     * Set configuration.
     */
    void setConfig(const WeatherEffectsConfig& config);
    const WeatherEffectsConfig& getConfig() const { return config_; }

    /**
     * Reload configuration from JSON file.
     * @return true if reload successful
     */
    bool reloadConfig();

    /**
     * Enable/disable weather effects.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /**
     * Set callback for lightning flash (for audio sync).
     * Called just before a lightning flash occurs.
     */
    void setLightningCallback(std::function<void()> callback);

    /**
     * Trigger a lightning strike manually (for testing or audio sync).
     */
    void triggerLightning();

    /**
     * Get ambient light modifier for current weather.
     * Returns multiplier (1.0 = normal, <1.0 = darker).
     */
    float getAmbientLightModifier() const;

    /**
     * Get fog color modifier for current weather.
     * Returns color to blend with normal fog.
     */
    irr::video::SColor getWeatherFogColor() const;

    /**
     * Get fog density modifier for current weather.
     * Returns multiplier (1.0 = normal, >1.0 = denser fog).
     */
    float getFogDensityModifier() const;

    /**
     * Check if lightning flash is active (for fullbright effect).
     */
    bool isLightningFlashActive() const { return lightningFlashTimer_ > 0.0f; }

    /**
     * Get lightning flash intensity (0-1).
     */
    float getLightningFlashIntensity() const;

    /**
     * Set surface map for water detection (used by ripple system).
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap);

    /**
     * Check if water ripples are enabled.
     */
    bool areRipplesEnabled() const;

    /**
     * Check if storm cloud overlay is enabled.
     */
    bool isCloudOverlayEnabled() const;

    /**
     * Check if snow accumulation is enabled.
     */
    bool isSnowAccumulationEnabled() const;

    /**
     * Set the raycast mesh for shelter detection in snow accumulation.
     * Note: RaycastMesh::raycast is not const, so we need a non-const pointer.
     */
    void setRaycastMesh(RaycastMesh* raycastMesh);

    /**
     * Called when entering a new zone.
     */
    void onZoneEnter(const std::string& zoneName);

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
     * Apply settings from WeatherConfigLoader singleton.
     */
    void applyConfigFromLoader();

    /**
     * Update rain effects.
     */
    void updateRain(float deltaTime);

    /**
     * Update snow effects.
     */
    void updateSnow(float deltaTime);

    /**
     * Update storm atmosphere (sky darkening).
     */
    void updateStormAtmosphere(float deltaTime);

    /**
     * Update lightning system.
     */
    void updateLightning(float deltaTime);

    /**
     * Generate random lightning bolt shape.
     */
    void generateLightningBolt();

    /**
     * Recursively generate bolt segments with branches.
     */
    void generateBoltSegments(const glm::vec3& start, const glm::vec3& end,
                               int branchLevel, float brightness);

    /**
     * Render lightning bolt.
     */
    void renderLightningBolt();

    /**
     * Schedule next lightning strike.
     */
    void scheduleLightning();

    // External references
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    Environment::ParticleManager* particleManager_ = nullptr;
    SkyRenderer* skyRenderer_ = nullptr;

    // Rain emitter (owned by us, but updates through ParticleManager)
    std::unique_ptr<Environment::RainEmitter> rainEmitter_;

    // Rain splash emitter (owned by us, but updates through ParticleManager)
    std::unique_ptr<Environment::RainSplashEmitter> rainSplashEmitter_;

    // Snow emitter (owned by us, but updates through ParticleManager)
    std::unique_ptr<Environment::SnowEmitter> snowEmitter_;

    // Water ripple manager (Phase 7)
    std::unique_ptr<Environment::WaterRippleManager> waterRippleManager_;

    // Storm cloud layer (Phase 8)
    std::unique_ptr<Environment::StormCloudLayer> stormCloudLayer_;

    // Snow accumulation system (Phase 9)
    std::unique_ptr<Environment::SnowAccumulationSystem> snowAccumulationSystem_;

    // Surface map for water detection (not owned)
    const Detail::SurfaceMap* surfaceMap_ = nullptr;

    // Raycast mesh for shelter detection (not owned)
    RaycastMesh* raycastMesh_ = nullptr;

    // Configuration
    WeatherEffectsConfig config_;
    bool enabled_ = true;
    bool initialized_ = false;

    // Current weather state
    uint8_t currentType_ = 0;       // 0=none, 1=rain, 2=snow
    uint8_t currentIntensity_ = 0;  // 1-10
    uint8_t targetIntensity_ = 0;   // For smooth transitions
    float transitionProgress_ = 1.0f;
    float transitionDuration_ = 2.0f;

    // Storm atmosphere
    float currentDarkening_ = 0.0f;  // 0-1, current sky darkening
    float targetDarkening_ = 0.0f;   // Target darkening for current intensity

    // Lightning state
    float lightningTimer_ = 0.0f;       // Time until next lightning
    float lightningFlashTimer_ = 0.0f;  // Time remaining in flash
    float lightningBoltTimer_ = 0.0f;   // Time remaining for bolt visibility
    bool lightningActive_ = false;

    // Lightning bolt geometry (start/end points for line segments)
    struct BoltSegment {
        glm::vec3 start;
        glm::vec3 end;
        float brightness;
        int branchLevel;
    };
    std::vector<BoltSegment> lightningBolt_;

    // Lightning callback
    std::function<void()> lightningCallback_;

    // Random number generation
    std::mt19937 rng_;

    // Zone state
    std::string currentZoneName_;
    bool isIndoorZone_ = false;
};

} // namespace Graphics
} // namespace EQT
