#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/emitters/rain_emitter.h"
#include "client/graphics/environment/emitters/rain_splash_emitter.h"
#include "client/graphics/environment/emitters/snow_emitter.h"
#include "client/graphics/environment/water_ripple_manager.h"
#include "client/graphics/environment/storm_cloud_layer.h"
#include "client/graphics/environment/snow_accumulation_system.h"
#include "client/graphics/environment/rain_overlay.h"
#include "client/graphics/environment/snow_overlay.h"
#include "common/logging.h"
#include <irrlicht.h>
#include <cmath>
#include <algorithm>

namespace EQT {
namespace Graphics {

WeatherEffectsController::WeatherEffectsController(
    irr::scene::ISceneManager* smgr,
    irr::video::IVideoDriver* driver,
    Environment::ParticleManager* particleManager,
    SkyRenderer* skyRenderer)
    : smgr_(smgr)
    , driver_(driver)
    , particleManager_(particleManager)
    , skyRenderer_(skyRenderer)
    , rng_(std::random_device{}())
{
}

WeatherEffectsController::~WeatherEffectsController() {
    // Unregister emitters from particle manager before they're destroyed
    if (particleManager_) {
        if (rainEmitter_) {
            particleManager_->unregisterExternalEmitter(rainEmitter_.get());
        }
        if (rainSplashEmitter_) {
            particleManager_->unregisterExternalEmitter(rainSplashEmitter_.get());
        }
        if (snowEmitter_) {
            particleManager_->unregisterExternalEmitter(snowEmitter_.get());
        }
    }
}

bool WeatherEffectsController::initialize(const std::string& eqClientPath) {
    LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController::initialize() starting");
    eqClientPath_ = eqClientPath;

    if (initialized_) {
        LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController: Already initialized, returning early");
        return true;
    }

    LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController: smgr={} driver={} particleManager={} skyRenderer={}",
             (void*)smgr_, (void*)driver_, (void*)particleManager_, (void*)skyRenderer_);

    // Load weather config from JSON
    auto& configLoader = WeatherConfigLoader::instance();
    configLoader.load("config/weather_effects.json");

    // Set up reload callback to apply settings when config reloads
    configLoader.setReloadCallback([this]() {
        applyConfigFromLoader();
    });

    // Create rain emitter
    rainEmitter_ = std::make_unique<Environment::RainEmitter>();

    // Create rain splash emitter
    rainSplashEmitter_ = std::make_unique<Environment::RainSplashEmitter>();

    // Create snow emitter
    snowEmitter_ = std::make_unique<Environment::SnowEmitter>();

    // Create water ripple manager (Phase 7)
    waterRippleManager_ = std::make_unique<Environment::WaterRippleManager>();
    if (particleManager_) {
        waterRippleManager_->initialize(driver_, smgr_, particleManager_->getAtlasTexture());
    }

    // Create storm cloud layer (Phase 8)
    stormCloudLayer_ = std::make_unique<Environment::StormCloudLayer>();
    if (stormCloudLayer_ && smgr_ && driver_) {
        stormCloudLayer_->initialize(smgr_, driver_, eqClientPath_);
    }

    // Create snow accumulation system (Phase 9)
    snowAccumulationSystem_ = std::make_unique<Environment::SnowAccumulationSystem>();
    if (snowAccumulationSystem_ && smgr_ && driver_ && particleManager_) {
        snowAccumulationSystem_->initialize(smgr_, driver_, particleManager_->getAtlasTexture());
    }

    // Create screen-space rain overlay (Phase 10)
    rainOverlay_ = std::make_unique<Environment::RainOverlay>();
    if (rainOverlay_ && driver_ && smgr_) {
        rainOverlay_->initialize(driver_, smgr_, eqClientPath_);
    }

    // Create screen-space snow overlay (Phase 11)
    snowOverlay_ = std::make_unique<Environment::SnowOverlay>();
    if (snowOverlay_ && driver_ && smgr_) {
        snowOverlay_->initialize(driver_, smgr_, eqClientPath_);
    }

    // Apply loaded config to emitters
    applyConfigFromLoader();

    // Register emitters with particle manager for rendering
    if (particleManager_) {
        if (rainEmitter_) {
            particleManager_->registerExternalEmitter(rainEmitter_.get());
        }
        if (rainSplashEmitter_) {
            particleManager_->registerExternalEmitter(rainSplashEmitter_.get());
        }
        if (snowEmitter_) {
            particleManager_->registerExternalEmitter(snowEmitter_.get());
        }
    }

    // Schedule first potential lightning
    scheduleLightning();

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController initialized with config from weather_effects.json");
    return true;
}

void WeatherEffectsController::setWeather(uint8_t type, uint8_t intensity) {
    // Decode EQ weather packet format
    // type: 0=rain off, 1=snow off, 2=snow on
    // When type=0 and intensity>0, it means rain is on
    // When type=1, snow is off
    // When type=2, snow is on

    uint8_t newType = 0;
    uint8_t newIntensity = 0;

    if (type == 0 && intensity > 0) {
        // Rain on
        newType = 1;
        newIntensity = intensity;
    } else if (type == 2) {
        // Snow on
        newType = 2;
        newIntensity = intensity > 0 ? intensity : 5;  // Default intensity if not specified
    }
    // type=0 with intensity=0, or type=1 means weather off

    LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController::setWeather: packet type={} intensity={} -> decoded newType={} newIntensity={}",
             type, intensity, newType, newIntensity);

    // Start transition if weather changed
    if (newType != currentType_ || newIntensity != currentIntensity_) {
        targetIntensity_ = newIntensity;

        // Quick transition for type change, slower for intensity change
        if (newType != currentType_) {
            transitionProgress_ = 0.0f;
            transitionDuration_ = 2.0f;
            currentType_ = newType;
        } else if (newIntensity != currentIntensity_) {
            transitionProgress_ = 0.0f;
            transitionDuration_ = 1.0f;
        }

        currentIntensity_ = newIntensity;

        // Update rain emitters (only if not using screen-space overlay)
        if (!useRainOverlay_) {
            if (rainEmitter_) {
                rainEmitter_->setIntensity(newType == 1 ? newIntensity : 0);
            }
            if (rainSplashEmitter_) {
                rainSplashEmitter_->setIntensity(newType == 1 ? newIntensity : 0);
            }
        } else {
            // Disable particle emitters when using overlay
            if (rainEmitter_) {
                rainEmitter_->setIntensity(0);
            }
            if (rainSplashEmitter_) {
                rainSplashEmitter_->setIntensity(0);
            }
        }

        // Update screen-space rain overlay
        if (rainOverlay_) {
            rainOverlay_->setIntensity(newType == 1 ? newIntensity : 0);
        }

        // Update snow emitter (only if not using screen-space overlay)
        if (!useSnowOverlay_) {
            if (snowEmitter_) {
                snowEmitter_->setIntensity(newType == 2 ? newIntensity : 0);
            }
        } else {
            // Disable particle emitter when using overlay
            if (snowEmitter_) {
                snowEmitter_->setIntensity(0);
            }
        }

        // Update screen-space snow overlay
        if (snowOverlay_) {
            snowOverlay_->setIntensity(newType == 2 ? newIntensity : 0);
        }

        // Update target darkening based on weather
        if (newType == 1 && newIntensity > 0) {
            // Rain: darken sky based on intensity
            targetDarkening_ = config_.storm.maxDarkening * (newIntensity / 10.0f);
        } else if (newType == 2 && newIntensity > 0) {
            // Snow: slight darkening (overcast)
            targetDarkening_ = config_.storm.maxDarkening * 0.3f * (newIntensity / 10.0f);
        } else {
            targetDarkening_ = 0.0f;
        }

        // Update particle manager weather state
        if (particleManager_) {
            if (newType == 1 && newIntensity > 0) {
                particleManager_->setWeather(Environment::WeatherType::Rain);
            } else if (newType == 2 && newIntensity > 0) {
                particleManager_->setWeather(Environment::WeatherType::Snow);
            } else {
                particleManager_->setWeather(Environment::WeatherType::Clear);
            }
        }
    }
}

void WeatherEffectsController::onWeatherChanged(WeatherType newWeather) {
    // Convert WeatherType to our format
    uint8_t type = 0;
    uint8_t intensity = currentIntensity_ > 0 ? currentIntensity_ : 5;

    switch (newWeather) {
        case WeatherType::Rain:
        case WeatherType::Storm:
            type = 1;
            break;
        case WeatherType::Calm:
        case WeatherType::Normal:
        default:
            type = 0;
            intensity = 0;
            break;
    }

    setWeather(type == 1 ? 0 : 1, type == 1 ? intensity : 0);
}

void WeatherEffectsController::setConfig(const WeatherEffectsConfig& config) {
    config_ = config;

    // Update rain emitter settings
    if (rainEmitter_) {
        Environment::RainSettings rainSettings;
        rainSettings.enabled = config.rain.enabled;
        rainSettings.dropSpeed = config.rain.dropSpeed;
        rainSettings.spawnRadius = config.rain.spawnRadius;
        rainSettings.spawnHeight = config.rain.spawnHeight;
        rainSettings.windInfluence = config.rain.windInfluence;
        rainEmitter_->setSettings(rainSettings);
    }
}

void WeatherEffectsController::applyConfigFromLoader() {
    auto& loader = WeatherConfigLoader::instance();
    if (!loader.isLoaded()) {
        LOG_WARN(MOD_GRAPHICS, "WeatherEffectsController: Config not loaded, using defaults");
        return;
    }

    // Apply rain settings
    if (rainEmitter_) {
        rainEmitter_->setSettings(loader.getRainSettings());
        LOG_DEBUG(MOD_GRAPHICS, "Applied rain config: maxParticles={}, dropSpeed={}",
                  loader.getRainSettings().maxParticles, loader.getRainSettings().dropSpeed);
    }

    // Apply rain splash settings
    if (rainSplashEmitter_) {
        rainSplashEmitter_->setSettings(loader.getRainSplashSettings());
        LOG_DEBUG(MOD_GRAPHICS, "Applied rain splash config: maxParticles={}, lifetime={}",
                  loader.getRainSplashSettings().maxParticles, loader.getRainSplashSettings().lifetime);
    }

    // Apply snow settings
    if (snowEmitter_) {
        snowEmitter_->setSettings(loader.getSnowSettings());
        LOG_DEBUG(MOD_GRAPHICS, "Applied snow config: maxParticles={}, fallSpeed={}",
                  loader.getSnowSettings().maxParticles, loader.getSnowSettings().fallSpeed);
    }

    // Apply weather effects config (storm/lightning settings)
    config_ = loader.getWeatherEffectsConfig();

    // Apply storm cloud settings
    if (stormCloudLayer_) {
        stormCloudLayer_->setSettings(loader.getStormCloudSettings());
        LOG_DEBUG(MOD_GRAPHICS, "Applied storm cloud config: dayBrightness={}, nightBrightness={}",
                  loader.getStormCloudSettings().dayBrightness, loader.getStormCloudSettings().nightBrightness);
    }

    // Apply rain overlay settings
    if (rainOverlay_) {
        const auto& rainSettings = loader.getRainOverlaySettings();
        rainOverlay_->setSettings(rainSettings);
        LOG_DEBUG(MOD_GRAPHICS, "Applied rain overlay config: numLayers={}, baseOpacity={}, maxOpacity={}",
                  rainSettings.numLayers, rainSettings.baseOpacity, rainSettings.maxOpacity);
        LOG_DEBUG(MOD_GRAPHICS, "Rain overlay daylight settings: enabled={}, daylightMin={:.3f}, daylightMax={:.3f}",
                  rainSettings.daylightReductionEnabled, rainSettings.daylightMin, rainSettings.daylightMax);
    }

    // Apply snow overlay settings
    if (snowOverlay_) {
        const auto& snowSettings = loader.getSnowOverlaySettings();
        snowOverlay_->setSettings(snowSettings);
        LOG_DEBUG(MOD_GRAPHICS, "Applied snow overlay config: baseOpacity={}, maxOpacity={}, swayAmplitude={}",
                  snowSettings.baseOpacity, snowSettings.maxOpacity, snowSettings.swayAmplitude);
        LOG_DEBUG(MOD_GRAPHICS, "Snow overlay sky settings: enabled={}, brightnessMin={:.3f}, brightnessMax={:.3f}",
                  snowSettings.skyDarkeningEnabled, snowSettings.skyBrightnessMin, snowSettings.skyBrightnessMax);
    }

    LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController: Applied config from loader");
}

bool WeatherEffectsController::reloadConfig() {
    auto& loader = WeatherConfigLoader::instance();
    bool result = loader.reload();
    if (result) {
        LOG_INFO(MOD_GRAPHICS, "Weather config reloaded successfully");
    } else {
        LOG_ERROR(MOD_GRAPHICS, "Failed to reload weather config");
    }
    return result;
}

void WeatherEffectsController::setLightningCallback(std::function<void()> callback) {
    lightningCallback_ = callback;
}

void WeatherEffectsController::setSurfaceMap(const Detail::SurfaceMap* surfaceMap) {
    surfaceMap_ = surfaceMap;
    if (waterRippleManager_) {
        waterRippleManager_->setSurfaceMap(surfaceMap);
    }
}

bool WeatherEffectsController::areRipplesEnabled() const {
    // Check both the quality preset flag and the ripple manager state
    const auto& preset = WeatherQualityManager::instance().getCurrentPresetValues();
    return preset.ripplesEnabled && waterRippleManager_ && waterRippleManager_->isEnabled();
}

bool WeatherEffectsController::isCloudOverlayEnabled() const {
    // Check both the quality preset flag and the cloud layer state
    const auto& preset = WeatherQualityManager::instance().getCurrentPresetValues();
    return preset.cloudOverlayEnabled && stormCloudLayer_ && stormCloudLayer_->isEnabled();
}

bool WeatherEffectsController::isSnowAccumulationEnabled() const {
    // Check both the quality preset flag and the accumulation system state
    const auto& preset = WeatherQualityManager::instance().getCurrentPresetValues();
    return preset.snowAccumulationEnabled && snowAccumulationSystem_ && snowAccumulationSystem_->isEnabled();
}

bool WeatherEffectsController::isRainOverlayEnabled() const {
    return useRainOverlay_ && rainOverlay_ && rainOverlay_->isEnabled();
}

bool WeatherEffectsController::isSnowOverlayEnabled() const {
    return useSnowOverlay_ && snowOverlay_ && snowOverlay_->isEnabled();
}

bool WeatherEffectsController::getRainFogSettings(float& outFogStart, float& outFogEnd) const {
    if (!useRainOverlay_ || !rainOverlay_) {
        return false;
    }
    return rainOverlay_->getFogSettings(outFogStart, outFogEnd);
}

bool WeatherEffectsController::getRainDaylightMultiplier(float& outMultiplier) const {
    if (!useRainOverlay_ || !rainOverlay_) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            LOG_DEBUG(MOD_GRAPHICS, "getRainDaylightMultiplier: skipped (useRainOverlay_={}, rainOverlay_={})",
                      useRainOverlay_, rainOverlay_ != nullptr);
            loggedOnce = true;
        }
        return false;
    }
    bool result = rainOverlay_->getDaylightMultiplier(outMultiplier);
    static float lastLogged = -1.0f;
    if (result && std::abs(outMultiplier - lastLogged) > 0.01f) {
        LOG_DEBUG(MOD_GRAPHICS, "getRainDaylightMultiplier: result={}, outMultiplier={:.3f}",
                  result, outMultiplier);
        lastLogged = outMultiplier;
    }
    return result;
}

bool WeatherEffectsController::getSnowFogSettings(float& outFogStart, float& outFogEnd) const {
    if (!useSnowOverlay_ || !snowOverlay_) {
        return false;
    }
    return snowOverlay_->getFogSettings(outFogStart, outFogEnd);
}

bool WeatherEffectsController::getSnowBrightnessMultiplier(float& outMultiplier) const {
    if (!useSnowOverlay_ || !snowOverlay_) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            LOG_DEBUG(MOD_GRAPHICS, "getSnowBrightnessMultiplier: skipped (useSnowOverlay_={}, snowOverlay_={})",
                      useSnowOverlay_, snowOverlay_ != nullptr);
            loggedOnce = true;
        }
        return false;
    }
    bool result = snowOverlay_->getSkyBrightnessMultiplier(outMultiplier);
    static float lastLogged = -1.0f;
    if (result && std::abs(outMultiplier - lastLogged) > 0.01f) {
        LOG_DEBUG(MOD_GRAPHICS, "getSnowBrightnessMultiplier: result={}, outMultiplier={:.3f}",
                  result, outMultiplier);
        lastLogged = outMultiplier;
    }
    return result;
}

void WeatherEffectsController::setRaycastMesh(RaycastMesh* raycastMesh) {
    raycastMesh_ = raycastMesh;
    if (snowAccumulationSystem_) {
        snowAccumulationSystem_->setRaycastMesh(raycastMesh);
    }
}

void WeatherEffectsController::update(float deltaTime) {
    if (!enabled_ || !initialized_) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            LOG_WARN(MOD_GRAPHICS, "WeatherEffectsController::update skipped - enabled={} initialized={}",
                     enabled_, initialized_);
            warnedOnce = true;
        }
        return;
    }

    // Update transition progress
    if (transitionProgress_ < 1.0f) {
        transitionProgress_ += deltaTime / transitionDuration_;
        transitionProgress_ = std::min(1.0f, transitionProgress_);
    }

    // Update rain
    if (currentType_ == 1 && currentIntensity_ > 0) {
        static bool rainStartLogged = false;
        if (!rainStartLogged) {
            LOG_INFO(MOD_GRAPHICS, "WeatherEffectsController: Rain effects starting - type={} intensity={}",
                     currentType_, currentIntensity_);
            rainStartLogged = true;
        }
        updateRain(deltaTime);
    }

    // Update snow
    if (currentType_ == 2 && currentIntensity_ > 0) {
        updateSnow(deltaTime);
    }

    // Update storm atmosphere
    updateStormAtmosphere(deltaTime);

    // Update lightning during storms
    if (currentType_ == 1 && currentIntensity_ >= 3 && config_.storm.lightningEnabled) {
        updateLightning(deltaTime);
    }

    // Update storm cloud layer (Phase 8)
    if (stormCloudLayer_ && isCloudOverlayEnabled()) {
        // Get wind direction and time of day from particle manager
        glm::vec3 windDirection(1.0f, 0.0f, 0.0f);  // Default wind direction
        float windStrength = 0.5f;
        float timeOfDay = 12.0f;  // Default to noon

        if (particleManager_) {
            const auto& env = particleManager_->getEnvironmentState();
            windDirection = env.windDirection;
            windStrength = env.windStrength;
            timeOfDay = env.timeOfDay;
            stormCloudLayer_->update(deltaTime, env.playerPosition, windDirection, windStrength, currentIntensity_, timeOfDay);
        }
    }
}

void WeatherEffectsController::updateRain(float deltaTime) {
    if (isIndoorZone_) return;

    // Get environment state from particle manager (includes player position)
    if (particleManager_) {
        const auto& env = particleManager_->getEnvironmentState();

        // Update screen-space rain overlay (if enabled)
        if (useRainOverlay_ && rainOverlay_) {
            glm::vec3 cameraDir(1, 0, 0);  // Default direction
            // Get actual camera direction if available from scene manager
            if (smgr_ && smgr_->getActiveCamera()) {
                irr::core::vector3df target = smgr_->getActiveCamera()->getTarget();
                irr::core::vector3df pos = smgr_->getActiveCamera()->getPosition();
                irr::core::vector3df dir = target - pos;
                dir.normalize();
                // Convert from Irrlicht (Y-up) to EQ (Z-up)
                cameraDir = glm::vec3(dir.X, dir.Z, dir.Y);
            }
            rainOverlay_->update(deltaTime, env.playerPosition, cameraDir);
        }

        // Update particle rain (only if not using overlay)
        if (!useRainOverlay_) {
            // Update rain drops
            if (rainEmitter_) {
                rainEmitter_->update(deltaTime, env);

                // Debug: periodically log particle state
                static float debugTimer = 0.0f;
                debugTimer += deltaTime;
                if (debugTimer >= 2.0f) {
                    debugTimer = 0.0f;
                    int activeCount = 0;
                    for (const auto& p : rainEmitter_->getParticles()) {
                        if (p.isAlive()) ++activeCount;
                    }
                    LOG_INFO(MOD_GRAPHICS, "Rain debug: intensity={} enabled={} active_particles={} player_pos=({:.1f},{:.1f},{:.1f})",
                             rainEmitter_->getIntensity(),
                             rainEmitter_->isEnabled(),
                             activeCount,
                             env.playerPosition.x, env.playerPosition.y, env.playerPosition.z);
                }
            }

            // Update rain splashes
            if (rainSplashEmitter_) {
                rainSplashEmitter_->update(deltaTime, env);
            }
        }

        // Update water ripples (Phase 7) - always update, even with overlay
        if (waterRippleManager_ && areRipplesEnabled()) {
            waterRippleManager_->setRainIntensity(currentIntensity_);
            // Use player position for both - ripples spawn around player
            waterRippleManager_->update(deltaTime, env.playerPosition, env.playerPosition);
        }
    }
}

void WeatherEffectsController::updateSnow(float deltaTime) {
    if (isIndoorZone_) return;

    // Get environment state from particle manager (includes player position)
    if (particleManager_) {
        const auto& env = particleManager_->getEnvironmentState();

        // Update screen-space snow overlay (if enabled)
        if (useSnowOverlay_ && snowOverlay_) {
            glm::vec3 cameraDir(1, 0, 0);  // Default direction
            // Get actual camera direction if available from scene manager
            if (smgr_ && smgr_->getActiveCamera()) {
                irr::core::vector3df target = smgr_->getActiveCamera()->getTarget();
                irr::core::vector3df pos = smgr_->getActiveCamera()->getPosition();
                irr::core::vector3df dir = target - pos;
                dir.normalize();
                // Convert from Irrlicht (Y-up) to EQ (Z-up)
                cameraDir = glm::vec3(dir.X, dir.Z, dir.Y);
            }
            snowOverlay_->update(deltaTime, env.playerPosition, cameraDir);
        }

        // Update particle snow (only if not using overlay)
        if (!useSnowOverlay_ && snowEmitter_) {
            snowEmitter_->update(deltaTime, env);
        }

        // Update snow accumulation (Phase 9) - always update, even with overlay
        if (snowAccumulationSystem_ && isSnowAccumulationEnabled()) {
            snowAccumulationSystem_->setSnowIntensity(currentIntensity_);
            snowAccumulationSystem_->update(deltaTime, env.playerPosition);
        }
    }
}

void WeatherEffectsController::updateStormAtmosphere(float deltaTime) {
    // Smoothly transition darkening
    float darkeningSpeed = 0.5f;  // Units per second
    if (currentDarkening_ < targetDarkening_) {
        currentDarkening_ += darkeningSpeed * deltaTime;
        currentDarkening_ = std::min(currentDarkening_, targetDarkening_);
    } else if (currentDarkening_ > targetDarkening_) {
        currentDarkening_ -= darkeningSpeed * deltaTime;
        currentDarkening_ = std::max(currentDarkening_, targetDarkening_);
    }
}

void WeatherEffectsController::updateLightning(float deltaTime) {
    // Update flash timer
    if (lightningFlashTimer_ > 0.0f) {
        lightningFlashTimer_ -= deltaTime;
    }

    // Update bolt visibility timer
    if (lightningBoltTimer_ > 0.0f) {
        lightningBoltTimer_ -= deltaTime;
        if (lightningBoltTimer_ <= 0.0f) {
            lightningBolt_.clear();
            lightningActive_ = false;
        }
    }

    // Check for next lightning strike
    lightningTimer_ -= deltaTime;
    if (lightningTimer_ <= 0.0f) {
        triggerLightning();
        scheduleLightning();
    }
}

void WeatherEffectsController::triggerLightning() {
    if (!config_.storm.lightningEnabled || isIndoorZone_) return;

    // Generate lightning bolt shape
    generateLightningBolt();

    // Start flash and bolt timers
    lightningFlashTimer_ = config_.storm.flashDuration;
    lightningBoltTimer_ = config_.storm.boltDuration;
    lightningActive_ = true;

    // Illuminate storm clouds during lightning flash
    if (stormCloudLayer_) {
        stormCloudLayer_->setLightningFlash(1.0f);
    }

    // Notify callback (for audio sync)
    if (lightningCallback_) {
        lightningCallback_();
    }
}

void WeatherEffectsController::scheduleLightning() {
    // Random interval based on intensity
    float minInterval = config_.storm.lightningMinInterval;
    float maxInterval = config_.storm.lightningMaxInterval;

    // More frequent lightning at higher intensities
    float intensityFactor = 1.0f - (currentIntensity_ / 10.0f) * 0.5f;
    minInterval *= intensityFactor;
    maxInterval *= intensityFactor;

    std::uniform_real_distribution<float> dist(minInterval, maxInterval);
    lightningTimer_ = dist(rng_);
}

void WeatherEffectsController::generateLightningBolt() {
    lightningBolt_.clear();

    // Random start position in sky (above and around player)
    std::uniform_real_distribution<float> angleDist(0.0f, 6.283185f);
    std::uniform_real_distribution<float> radiusDist(20.0f, 80.0f);
    std::uniform_real_distribution<float> heightDist(60.0f, 100.0f);

    float angle = angleDist(rng_);
    float radius = radiusDist(rng_);
    float height = heightDist(rng_);

    glm::vec3 startPos(
        std::cos(angle) * radius,
        std::sin(angle) * radius,
        height
    );

    // End position (toward ground with offset)
    glm::vec3 endPos(
        startPos.x + (angleDist(rng_) - 3.14f) * 10.0f,
        startPos.y + (angleDist(rng_) - 3.14f) * 10.0f,
        -5.0f  // Below ground level
    );

    // Generate main bolt with jagged segments
    generateBoltSegments(startPos, endPos, 0, 1.0f);
}

void WeatherEffectsController::generateBoltSegments(
    const glm::vec3& start,
    const glm::vec3& end,
    int branchLevel,
    float brightness)
{
    if (branchLevel > 3) return;  // Max recursion depth

    glm::vec3 direction = end - start;
    float length = glm::length(direction);
    if (length < 5.0f) {
        // Too short, just add as single segment
        lightningBolt_.push_back({start, end, brightness, branchLevel});
        return;
    }

    direction = glm::normalize(direction);

    // Number of segments based on length
    int numSegments = std::max(3, static_cast<int>(length / 10.0f));
    float segmentLength = length / numSegments;

    glm::vec3 currentPos = start;
    std::uniform_real_distribution<float> jitterDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> branchDist(0.0f, 1.0f);

    for (int i = 0; i < numSegments; ++i) {
        glm::vec3 nextPos;

        if (i == numSegments - 1) {
            nextPos = end;
        } else {
            // Move along direction with jitter
            nextPos = currentPos + direction * segmentLength;

            // Add perpendicular jitter
            float jitterAmount = segmentLength * 0.4f;
            glm::vec3 perpX(-direction.y, direction.x, 0);
            glm::vec3 perpY = glm::cross(direction, perpX);
            perpX = glm::normalize(perpX);
            perpY = glm::normalize(perpY);

            nextPos += perpX * jitterDist(rng_) * jitterAmount;
            nextPos += perpY * jitterDist(rng_) * jitterAmount * 0.5f;
        }

        // Add segment
        lightningBolt_.push_back({currentPos, nextPos, brightness, branchLevel});

        // Random chance to spawn branch (only from main bolt)
        if (branchLevel == 0 && i > 0 && i < numSegments - 1) {
            if (branchDist(rng_) < 0.25f) {  // 25% chance per segment
                // Branch direction (angled away from main bolt)
                glm::vec3 branchDir = direction;
                branchDir.x += jitterDist(rng_) * 0.8f;
                branchDir.y += jitterDist(rng_) * 0.8f;
                branchDir.z -= 0.3f;  // Slightly downward
                branchDir = glm::normalize(branchDir);

                glm::vec3 branchEnd = currentPos + branchDir * segmentLength * 3.0f;
                generateBoltSegments(currentPos, branchEnd, branchLevel + 1, brightness * 0.6f);
            }
        }

        currentPos = nextPos;
    }
}

void WeatherEffectsController::render() {
    if (!enabled_ || !initialized_) return;

    // Render lightning bolt if active
    if (lightningActive_ && !lightningBolt_.empty()) {
        renderLightningBolt();
    }

    // Render screen-space snow overlay (if enabled) - render before rain for layering
    if (useSnowOverlay_ && snowOverlay_ && currentType_ == 2 && currentIntensity_ > 0) {
        snowOverlay_->render();
    }

    // Render screen-space rain overlay (if enabled)
    if (useRainOverlay_ && rainOverlay_ && currentType_ == 1 && currentIntensity_ > 0) {
        rainOverlay_->render();
    }

    // Render rain particles (only if not using overlay)
    if (!useRainOverlay_ && rainEmitter_ && currentType_ == 1 && currentIntensity_ > 0) {
        // Rain particles are rendered through the particle system
        // but we could add additional effects here
    }

    // Snow particles are rendered through the particle system when not using overlay

    // Render water ripples (Phase 7)
    if (waterRippleManager_ && areRipplesEnabled() && currentType_ == 1 && currentIntensity_ > 0) {
        if (particleManager_) {
            const auto& env = particleManager_->getEnvironmentState();
            glm::vec3 cameraUp(0, 0, 1);  // EQ uses Z-up
            // Ripples are flat on water, use player position for reference
            waterRippleManager_->render(env.playerPosition, cameraUp);
        }
    }

    // Render snow accumulation decals (Phase 9)
    if (snowAccumulationSystem_ && isSnowAccumulationEnabled() && snowAccumulationSystem_->hasVisibleSnow()) {
        if (particleManager_) {
            const auto& env = particleManager_->getEnvironmentState();
            snowAccumulationSystem_->render(env.playerPosition);
        }
    }
}

void WeatherEffectsController::renderLightningBolt() {
    if (!driver_ || lightningBolt_.empty()) return;

    // Set up material for lightning (additive blend, no lighting)
    irr::video::SMaterial material;
    material.Lighting = false;
    material.ZWriteEnable = false;
    material.ZBuffer = irr::video::ECFN_LESSEQUAL;
    material.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
    material.Thickness = 2.0f;

    driver_->setMaterial(material);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Calculate fade based on bolt timer
    float fade = std::min(1.0f, lightningBoltTimer_ / config_.storm.boltDuration);

    for (const auto& segment : lightningBolt_) {
        // Color based on brightness and fade
        uint8_t intensity = static_cast<uint8_t>(255 * segment.brightness * fade);
        irr::video::SColor color(255, intensity, intensity, static_cast<uint8_t>(intensity * 0.9f));

        // Wider for main bolt, thinner for branches
        if (segment.branchLevel == 0) {
            // Draw as thick line (or multiple lines for thickness)
            irr::core::vector3df start(segment.start.x, segment.start.z, segment.start.y);
            irr::core::vector3df end(segment.end.x, segment.end.z, segment.end.y);
            driver_->draw3DLine(start, end, color);

            // Draw slightly offset lines for thickness
            irr::core::vector3df offset(0.2f, 0, 0);
            driver_->draw3DLine(start + offset, end + offset, color);
            driver_->draw3DLine(start - offset, end - offset, color);
        } else {
            // Branch - single line
            irr::core::vector3df start(segment.start.x, segment.start.z, segment.start.y);
            irr::core::vector3df end(segment.end.x, segment.end.z, segment.end.y);
            driver_->draw3DLine(start, end, color);
        }
    }
}

float WeatherEffectsController::getAmbientLightModifier() const {
    // Base modifier from storm darkening
    float modifier = 1.0f - currentDarkening_;

    // Apply rain daylight reduction (darker during rain)
    float rainMultiplier = 1.0f;
    if (getRainDaylightMultiplier(rainMultiplier)) {
        modifier *= rainMultiplier;
        static float lastLoggedRain = -1.0f;
        if (std::abs(rainMultiplier - lastLoggedRain) > 0.01f) {
            LOG_DEBUG(MOD_GRAPHICS, "getAmbientLightModifier: rainMultiplier={:.3f}, modifier={:.3f}",
                      rainMultiplier, modifier);
            lastLoggedRain = rainMultiplier;
        }
    }

    // Apply snow brightness reduction (overcast during snow)
    float snowMultiplier = 1.0f;
    if (getSnowBrightnessMultiplier(snowMultiplier)) {
        modifier *= snowMultiplier;
        static float lastLoggedSnow = -1.0f;
        if (std::abs(snowMultiplier - lastLoggedSnow) > 0.01f) {
            LOG_DEBUG(MOD_GRAPHICS, "getAmbientLightModifier: snowMultiplier={:.3f}, modifier={:.3f}",
                      snowMultiplier, modifier);
            lastLoggedSnow = snowMultiplier;
        }
    }

    // Lightning flash temporarily brightens everything
    if (lightningFlashTimer_ > 0.0f) {
        float flashIntensity = lightningFlashTimer_ / config_.storm.flashDuration;
        modifier = std::min(1.5f, modifier + flashIntensity * 2.0f);
    }

    return modifier;
}

irr::video::SColor WeatherEffectsController::getWeatherFogColor() const {
    // Normal fog color
    irr::video::SColor fogColor(255, 200, 200, 200);

    if (currentType_ == 1 && currentIntensity_ > 0) {
        // Rain: blue-grey fog
        float t = currentIntensity_ / 10.0f;
        fogColor = irr::video::SColor(
            255,
            static_cast<uint8_t>(200 - t * 50),
            static_cast<uint8_t>(200 - t * 40),
            static_cast<uint8_t>(200 - t * 20)
        );
    } else if (currentType_ == 2 && currentIntensity_ > 0) {
        // Snow: white fog
        float t = currentIntensity_ / 10.0f;
        fogColor = irr::video::SColor(
            255,
            static_cast<uint8_t>(220 + t * 35),
            static_cast<uint8_t>(220 + t * 35),
            static_cast<uint8_t>(230 + t * 25)
        );
    }

    return fogColor;
}

float WeatherEffectsController::getFogDensityModifier() const {
    if (currentType_ == 0 || currentIntensity_ == 0) {
        return 1.0f;
    }

    // Increase fog density based on intensity
    // intensity 1 = 1.1x, intensity 10 = 2.0x
    return 1.0f + (currentIntensity_ / 10.0f);
}

float WeatherEffectsController::getLightningFlashIntensity() const {
    if (lightningFlashTimer_ <= 0.0f) return 0.0f;
    return lightningFlashTimer_ / config_.storm.flashDuration;
}

void WeatherEffectsController::onZoneEnter(const std::string& zoneName) {
    currentZoneName_ = zoneName;

    // Check for indoor zones (dungeons, caves)
    // This is a simplified check - could be expanded
    isIndoorZone_ = (zoneName.find("guk") != std::string::npos ||
                    zoneName.find("sol") != std::string::npos ||
                    zoneName.find("befallen") != std::string::npos ||
                    zoneName.find("blackburrow") != std::string::npos ||
                    zoneName.find("crushbone") != std::string::npos ||
                    zoneName.find("najena") != std::string::npos ||
                    zoneName.find("permafrost") != std::string::npos ||
                    zoneName.find("kedge") != std::string::npos);

    // Notify weather emitters
    Environment::ZoneBiome biome = isIndoorZone_ ?
        Environment::ZoneBiome::Dungeon : Environment::ZoneBiome::Unknown;

    if (rainEmitter_) {
        rainEmitter_->onZoneEnter(zoneName, biome);
    }
    if (rainSplashEmitter_) {
        rainSplashEmitter_->onZoneEnter(zoneName, biome);
    }
    if (snowEmitter_) {
        snowEmitter_->onZoneEnter(zoneName, biome);
    }
    if (waterRippleManager_) {
        waterRippleManager_->onZoneEnter(zoneName);
    }
    if (stormCloudLayer_) {
        stormCloudLayer_->onZoneEnter(zoneName, isIndoorZone_);
    }
    if (snowAccumulationSystem_) {
        snowAccumulationSystem_->onZoneEnter(zoneName, isIndoorZone_);
    }
}

void WeatherEffectsController::onZoneLeave() {
    // Clear weather state
    currentType_ = 0;
    currentIntensity_ = 0;
    currentDarkening_ = 0.0f;
    targetDarkening_ = 0.0f;

    // Clear lightning
    lightningBolt_.clear();
    lightningActive_ = false;

    // Clear weather emitters
    if (rainEmitter_) {
        rainEmitter_->setIntensity(0);
        rainEmitter_->onZoneLeave();
    }
    if (rainSplashEmitter_) {
        rainSplashEmitter_->setIntensity(0);
        rainSplashEmitter_->onZoneLeave();
    }
    if (snowEmitter_) {
        snowEmitter_->setIntensity(0);
        snowEmitter_->onZoneLeave();
    }
    if (waterRippleManager_) {
        waterRippleManager_->setRainIntensity(0);
        waterRippleManager_->onZoneLeave();
    }
    if (stormCloudLayer_) {
        stormCloudLayer_->onZoneLeave();
    }
    if (snowAccumulationSystem_) {
        snowAccumulationSystem_->onZoneLeave();
    }
}

std::string WeatherEffectsController::getDebugInfo() const {
    std::string info = "Weather: ";

    if (currentType_ == 0 || currentIntensity_ == 0) {
        info += "Clear";
    } else if (currentType_ == 1) {
        info += "Rain (intensity " + std::to_string(currentIntensity_) + ")";
    } else if (currentType_ == 2) {
        info += "Snow (intensity " + std::to_string(currentIntensity_) + ")";
    }

    if (currentDarkening_ > 0.01f) {
        info += ", darkening " + std::to_string(static_cast<int>(currentDarkening_ * 100)) + "%";
    }

    if (lightningActive_) {
        info += ", LIGHTNING";
    }

    if (waterRippleManager_ && areRipplesEnabled() && currentType_ == 1 && currentIntensity_ > 0) {
        info += ", ripples " + std::to_string(waterRippleManager_->getActiveRippleCount());
    }

    if (stormCloudLayer_ && isCloudOverlayEnabled() && stormCloudLayer_->isVisible()) {
        info += ", " + stormCloudLayer_->getDebugInfo();
    }

    if (snowAccumulationSystem_ && isSnowAccumulationEnabled() && snowAccumulationSystem_->hasVisibleSnow()) {
        info += ", " + snowAccumulationSystem_->getDebugInfo();
    }

    return info;
}

} // namespace Graphics
} // namespace EQT
