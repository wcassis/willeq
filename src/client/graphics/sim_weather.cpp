#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================================
// Sky State Computation
// ============================================================================

void SimulationWorker::computeSkyState(const SimulationInput& input, SimulationOutput& output) {
    if (!input.skyEnabled || !input.skyInitialized) return;

    output.skyState.cloudScrollOffset = input.skyCloudScrollOffset + input.deltaTime * 0.01f;
    if (output.skyState.cloudScrollOffset > 1.0f) {
        output.skyState.cloudScrollOffset -= 1.0f;
    }
    output.skyState.valid = true;
}

// ============================================================================
// Weather Effects State Computation
// ============================================================================

void SimulationWorker::computeWeatherEffectsState(const SimulationInput& input, SimulationOutput& output) {
    if (!input.weatherEnabled) return;

    auto& ws = output.weatherEffectsState;

    // Advance transition progress
    ws.transitionProgress = input.weatherTransitionProgress;
    if (ws.transitionProgress < 1.0f) {
        ws.transitionProgress += input.deltaTime / input.weatherTransitionDuration;
        ws.transitionProgress = std::min(1.0f, ws.transitionProgress);
    }

    // Advance storm darkening
    float darkeningSpeed = 0.5f;
    ws.currentDarkening = input.weatherCurrentDarkening;
    if (ws.currentDarkening < input.weatherTargetDarkening) {
        ws.currentDarkening += darkeningSpeed * input.deltaTime;
        ws.currentDarkening = std::min(ws.currentDarkening, input.weatherTargetDarkening);
    } else if (ws.currentDarkening > input.weatherTargetDarkening) {
        ws.currentDarkening -= darkeningSpeed * input.deltaTime;
        ws.currentDarkening = std::max(ws.currentDarkening, input.weatherTargetDarkening);
    }

    // Advance lightning timers
    ws.lightningFlashTimer = input.weatherLightningFlashTimer;
    ws.lightningBoltTimer = input.weatherLightningBoltTimer;
    ws.lightningActive = input.weatherLightningActive;
    ws.triggerLightningFlash = false;

    if (ws.lightningFlashTimer > 0.0f) {
        ws.lightningFlashTimer -= input.deltaTime;
    }
    if (ws.lightningBoltTimer > 0.0f) {
        ws.lightningBoltTimer -= input.deltaTime;
        if (ws.lightningBoltTimer <= 0.0f) {
            ws.lightningActive = false;
        }
    }

    // Check for next lightning strike
    if (input.weatherType == 1 && input.weatherIntensity >= 3 && input.weatherLightningEnabled) {
        float timer = input.weatherLightningTimer - input.deltaTime;
        if (timer <= 0.0f) {
            ws.triggerLightningFlash = true;
        }
    }

    ws.valid = true;
}

// ============================================================================
// Weather System (state machine + transitions, owned by worker)
// ============================================================================

// Alias to disambiguate from Environment::WeatherType (particle weather)
using TreeWeatherType = ::EQT::Graphics::WeatherType;

void SimulationWorker::computeWeather(const SimulationInput& input, SimulationOutput& output) {
    const auto& wi = input.weatherInput;
    if (!wi.initialized) {
        output.weatherOutput.valid = false;
        return;
    }

    // Track weather type at start for change detection
    uint8_t weatherAtStart = weatherCurrentWeather_;

    // Process commands
    for (const auto& cmd : wi.commands) {
        switch (cmd.type) {
            case WeatherCommand::SetZoneConfig: {
                weatherZoneConfig_ = cmd.zoneConfig;
                weatherTimeSinceLastCheck_ = 0.0f;
                weatherCurrentElapsed_ = 0.0f;
                weatherCurrentDuration_ = 0.0f;
                // Set initial weather based on zone default
                if (cmd.zoneConfig.enabled) {
                    uint8_t newType = static_cast<uint8_t>(cmd.zoneConfig.defaultWeather);
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetWeatherFromZone: {
                ZoneWeatherConfig config;
                if (loadZoneWeatherConfig(cmd.zoneName, config)) {
                    LOG_INFO(MOD_GRAPHICS, "WeatherSystem(worker): Loaded weather config for zone '{}'", cmd.zoneName);
                } else {
                    config.zoneName = cmd.zoneName;
                    config.defaultWeather = TreeWeatherType::Normal;
                    config.enabled = true;
                }
                weatherZoneConfig_ = config;
                weatherTimeSinceLastCheck_ = 0.0f;
                weatherCurrentElapsed_ = 0.0f;
                weatherCurrentDuration_ = 0.0f;
                if (config.enabled) {
                    uint8_t newType = static_cast<uint8_t>(config.defaultWeather);
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetWeatherImmediate: {
                uint8_t newType = cmd.weatherType;
                if (newType != weatherCurrentWeather_ || weatherTransitionProgress_ < 1.0f) {
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::TransitionToWeather: {
                uint8_t newType = cmd.weatherType;
                if (newType != weatherTargetWeather_ || weatherTransitionProgress_ < 1.0f) {
                    weatherTargetWeather_ = newType;
                    weatherTransitionDuration_ = std::max(0.1f, cmd.transitionTime);
                    weatherTransitionProgress_ = 0.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetSimulationEnabled:
                weatherSimulationEnabled_ = cmd.simulationEnabled;
                break;
        }
    }

    float dt = wi.deltaTime;

    // Advance transition if in progress
    if (weatherTransitionProgress_ < 1.0f) {
        weatherTransitionProgress_ += dt / weatherTransitionDuration_;
        if (weatherTransitionProgress_ >= 1.0f) {
            weatherTransitionProgress_ = 1.0f;
            weatherCurrentWeather_ = weatherTargetWeather_;
        }
    }

    // Run simulation timer
    if (weatherSimulationEnabled_ && weatherZoneConfig_.enabled) {
        weatherTimeSinceLastCheck_ += dt;
        weatherCurrentElapsed_ += dt;

        bool shouldCheck = (weatherTimeSinceLastCheck_ >= weatherZoneConfig_.checkIntervalSeconds);
        bool durationExpired = (weatherCurrentDuration_ > 0 &&
                               weatherCurrentElapsed_ >= weatherCurrentDuration_);

        if (shouldCheck || durationExpired) {
            weatherTimeSinceLastCheck_ = 0.0f;
            weatherCheckChange();
        }
    }

    // Compute wind intensity with smoothstep interpolation
    float currentIntensity = weatherGetWindIntensity(weatherCurrentWeather_);
    float targetIntensity = weatherGetWindIntensity(weatherTargetWeather_);
    if (weatherTransitionProgress_ < 1.0f) {
        float t = weatherTransitionProgress_;
        float smooth = t * t * (3.0f - 2.0f * t);
        weatherWindIntensity_ = currentIntensity + (targetIntensity - currentIntensity) * smooth;
    } else {
        weatherWindIntensity_ = currentIntensity;
    }

    // Detect weather changes (from commands or simulation rolls)
    bool changed = (weatherCurrentWeather_ != weatherAtStart) ||
                   (weatherTargetWeather_ != weatherAtStart && weatherTransitionProgress_ < 1.0f);

    // Populate output
    auto& wo = output.weatherOutput;
    wo.currentWeather = weatherCurrentWeather_;
    wo.targetWeather = weatherTargetWeather_;
    wo.transitionProgress = weatherTransitionProgress_;
    wo.windIntensity = weatherWindIntensity_;
    wo.weatherChanged = changed;
    wo.newWeatherType = changed ? weatherTargetWeather_ : weatherCurrentWeather_;
    wo.valid = true;
}

void SimulationWorker::weatherCheckChange() {
    // If we're in non-default weather and duration expired, return to default
    uint8_t defaultType = static_cast<uint8_t>(weatherZoneConfig_.defaultWeather);
    if (weatherCurrentWeather_ != defaultType &&
        weatherCurrentDuration_ > 0 &&
        weatherCurrentElapsed_ >= weatherCurrentDuration_) {

        weatherTargetWeather_ = defaultType;
        weatherTransitionDuration_ = 10.0f;
        weatherTransitionProgress_ = 0.0f;
        weatherCurrentElapsed_ = 0.0f;
        weatherCurrentDuration_ = 0.0f;
        return;
    }

    // Roll for new weather
    uint8_t newWeather = weatherRollForWeather();

    if (newWeather != weatherCurrentWeather_) {
        weatherTargetWeather_ = newWeather;
        weatherTransitionDuration_ = 10.0f;
        weatherTransitionProgress_ = 0.0f;
        weatherCurrentElapsed_ = 0.0f;

        // Set duration for the new weather
        if (newWeather == static_cast<uint8_t>(TreeWeatherType::Rain)) {
            std::uniform_int_distribution<int> slot(0, 3);
            int idx = slot(weatherRng_);
            weatherCurrentDuration_ = weatherZoneConfig_.rainDuration[idx] * 60.0f;
        } else if (newWeather == static_cast<uint8_t>(TreeWeatherType::Storm)) {
            std::uniform_real_distribution<float> duration(60.0f, 300.0f);
            weatherCurrentDuration_ = duration(weatherRng_);
        } else {
            weatherCurrentDuration_ = 0.0f;
        }
    }
}

uint8_t SimulationWorker::weatherRollForWeather() {
    // Check rain chance
    int totalRainChance = 0;
    for (int i = 0; i < 4; i++) {
        totalRainChance += weatherZoneConfig_.rainChance[i];
    }

    if (totalRainChance > 0) {
        std::uniform_int_distribution<int> roll(0, 399);
        int rainRoll = roll(weatherRng_);

        if (rainRoll < totalRainChance) {
            std::uniform_int_distribution<int> intensity(0, 99);
            int intensityRoll = intensity(weatherRng_);

            if (intensityRoll > 80) {
                return static_cast<uint8_t>(TreeWeatherType::Storm);
            } else if (intensityRoll > 40) {
                return static_cast<uint8_t>(TreeWeatherType::Rain);
            } else {
                return static_cast<uint8_t>(TreeWeatherType::Normal);
            }
        }
    }

    // Check for calm conditions
    std::uniform_int_distribution<int> calmRoll(0, 99);
    if (calmRoll(weatherRng_) < 20) {
        return static_cast<uint8_t>(TreeWeatherType::Calm);
    }

    return static_cast<uint8_t>(weatherZoneConfig_.defaultWeather);
}

float SimulationWorker::weatherGetWindIntensity(uint8_t type) {
    switch (static_cast<TreeWeatherType>(type)) {
        case TreeWeatherType::Calm:   return 0.3f;
        case TreeWeatherType::Normal: return 0.6f;
        case TreeWeatherType::Rain:   return 0.8f;
        case TreeWeatherType::Storm:  return 1.0f;
        default:                      return 0.6f;
    }
}

} // namespace Graphics
} // namespace EQT
