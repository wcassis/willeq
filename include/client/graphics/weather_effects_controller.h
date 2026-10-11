#pragma once

#include "client/graphics/weather_system.h"
#include "client/graphics/environment/particle_types.h"
#include "client/graphics/constrained_texture_cache.h"
#include <glm/glm.hpp>
#include <memory>
#include <functional>
#include <random>
#include <set>
#include <vector>
#include <cstdint>

namespace irr {
namespace scene {
class ISceneNode;
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
class StormCloudLayer;
class RainOverlay;
class SnowOverlay;
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
     * @param eqClientPath Path to EQ client for loading textures
     * @return true if initialized successfully
     */
    bool initialize(const std::string& eqClientPath = "");

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
     * Compute/apply split for worker thread offloading.
     *
     * T423 (board B361): computeState() and update() are GONE. There were
     * THREE copies of the weather ramp -- this class's computeState(), this
     * class's update()/updateStormAtmosphere(), and SimulationWorker's
     * computeWeatherEffectsState() -- and the only one PRODUCTION ran was the
     * worker's. update() had no caller at all; computeState() had none but
     * T396's and T418's tests, so for years the only ramp under test was one
     * nothing executed, and a change to the copy the client runs could not
     * turn a test red (proved by planting a 4x rate in it: all nineteen
     * weather cases stayed green). The worker's copy is now the only one, and
     * the tests drive it through the same snapshot/apply hop
     * renderer_sim_bridge.cpp makes (tests/weather_worker_step.h).
     *
     * applyState() applies the computed state to the scene (render thread
     * only) and stays: it is the render-thread half of that hop.
     */
    struct WeatherEffectsState {
        float transitionProgress = 1.0f;
        float currentDarkening = 0.0f;
        float lightningFlashTimer = 0.0f;
        float lightningBoltTimer = 0.0f;
        bool lightningActive = false;
        bool triggerLightningFlash = false;  // Signal for render thread
    };
    void applyState(const WeatherEffectsState& state);

    /**
     * Update only the render-thread-only parts (rain/snow overlays, storm cloud layer).
     * Call after applyState() when worker handles timer/darkening state.
     */
    void updateRenderOnly(float deltaTime);

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

    // State getters for worker thread snapshotting
    float getTransitionProgress() const { return transitionProgress_; }
    float getTransitionDuration() const { return transitionDuration_; }
    float getCurrentDarkening() const { return currentDarkening_; }
    float getTargetDarkening() const { return targetDarkening_; }
    float getDarkeningRate() const { return darkeningRate_; }
    float getLightningFlashTimer() const { return lightningFlashTimer_; }
    float getLightningBoltTimer() const { return lightningBoltTimer_; }
    float getLightningTimer() const { return lightningTimer_; }
    bool isLightningActive() const { return lightningActive_; }
    bool isLightningEnabled() const { return config_.storm.lightningEnabled; }

    /**
     * Set constrained texture cache (forwarded to storm cloud layer).
     */
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    /**
     * Set surface map for water detection.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap);

    /**
     * Check if storm cloud overlay is enabled.
     */
    bool isCloudOverlayEnabled() const;

    /**
     * Check if screen-space rain overlay is enabled (vs particle rain).
     */
    bool isRainOverlayEnabled() const;

    /**
     * Check if screen-space snow overlay is enabled (vs particle snow).
     */
    bool isSnowOverlayEnabled() const;

    /**
     * Get fog distance for rain effect (original EQ behavior).
     * @param outFogStart Output fog start distance
     * @param outFogEnd Output fog end distance
     * @return true if rain fog should override normal fog
     */
    bool getRainFogSettings(float& outFogStart, float& outFogEnd) const;

    /**
     * Get daylight multiplier for rain effect (darker during rain).
     * @param outMultiplier Output daylight multiplier (0.0-1.0)
     * @return true if daylight should be reduced
     */
    bool getRainDaylightMultiplier(float& outMultiplier) const;

    /**
     * Get fog distance for snow effect.
     * @param outFogStart Output fog start distance
     * @param outFogEnd Output fog end distance
     * @return true if snow fog should override normal fog
     */
    bool getSnowFogSettings(float& outFogStart, float& outFogEnd) const;

    /**
     * Get sky brightness multiplier for snow effect (overcast during snow).
     * @param outMultiplier Output brightness multiplier (0.0-1.0)
     * @return true if sky brightness should be reduced
     */
    bool getSnowBrightnessMultiplier(float& outMultiplier) const;

    /**
     * Called when entering a new zone.
     *
     * T396 (board B211): this RESETS the weather to clear. Weather is
     * per-zone server state; the previous zone's rain must not follow the
     * player through the zone line and keep the global ambient darkened.
     */
    void onZoneEnter(const std::string& zoneName);

    /**
     * Called when leaving a zone. Also resets the weather to clear.
     */
    void onZoneLeave();

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

    /**
     * Get count of active scene nodes managed by weather effects (for dumpScene accounting).
     */
    int getSceneNodeCount() const;

    // Collect scene node pointers into a set (for dumpScene node identification)
    void collectSceneNodes(std::set<irr::scene::ISceneNode*>& nodes) const;

private:
    /**
     * Apply settings from WeatherConfigLoader singleton.
     */
    void applyConfigFromLoader();

    /**
     * T396 (board B211): return every weather field to "clear". Called by
     * BOTH zone hooks so the two can never drift apart -- onZoneEnter()
     * used to reset nothing, and the previous zone's darkening survived.
     */
    void resetWeatherState();

    /**
     * Update rain effects.
     */
    void updateRain(float deltaTime);

    /**
     * Update snow effects.
     */
    void updateSnow(float deltaTime);

    // T423 (board B361): updateStormAtmosphere() and updateLightning() went
    // with update(), their only caller. The darkening ramp lives in
    // SimulationWorker::computeWeatherEffectsState() and the lightning timers
    // with it; triggerLightning()/scheduleLightning() below are still reached,
    // from applyState() when the worker raises triggerLightningFlash.

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
    ConstrainedTextureCache* constrainedCache_ = nullptr;

    // Storm cloud layer
    std::unique_ptr<Environment::StormCloudLayer> stormCloudLayer_;

    // Screen-space rain overlay
    std::unique_ptr<Environment::RainOverlay> rainOverlay_;

    // Screen-space snow overlay
    std::unique_ptr<Environment::SnowOverlay> snowOverlay_;

    // Surface map for water detection (not owned)
    const Detail::SurfaceMap* surfaceMap_ = nullptr;

    // Configuration
    WeatherEffectsConfig config_;
    bool enabled_ = true;
    bool initialized_ = false;
    std::string eqClientPath_;

    // Current weather state
    uint8_t currentType_ = 0;       // 0=none, 1=rain, 2=snow
    uint8_t currentIntensity_ = 0;  // 1-10
    // B339 deleted targetIntensity_ ("For smooth transitions"): the
    // interpolation it was for does not exist, weather is applied outright.
    float transitionProgress_ = 1.0f;
    float transitionDuration_ = 2.0f;

    // Storm atmosphere
    float currentDarkening_ = 0.0f;  // 0-1, current sky darkening
    float targetDarkening_ = 0.0f;   // Target darkening for current intensity
    // T418: units per second for the move currently in flight, so every
    // transition lasts the oracle's measured kOracleWeatherTransitionSeconds.
    float darkeningRate_ = 0.0f;

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
