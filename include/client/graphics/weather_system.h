#ifndef EQT_GRAPHICS_WEATHER_SYSTEM_H
#define EQT_GRAPHICS_WEATHER_SYSTEM_H

#include "client/graphics/tree_wind_config.h"
#include <string>
#include <vector>
#include <functional>
#include <random>

namespace EQT {
namespace Graphics {

/**
 * Interface for objects that want to be notified of weather changes.
 */
class IWeatherListener {
public:
    virtual ~IWeatherListener() = default;

    /**
     * Called when weather type changes.
     * @param newWeather The new weather type
     */
    virtual void onWeatherChanged(WeatherType newWeather) = 0;
};

/**
 * Zone weather configuration loaded from NewZone data or config files.
 */
struct ZoneWeatherConfig {
    // Rain probabilities (0-100) for each weather slot
    uint8_t rainChance[4] = {0, 0, 0, 0};
    uint8_t rainDuration[4] = {0, 0, 0, 0};  // Duration in minutes

    // Snow probabilities (for future use)
    uint8_t snowChance[4] = {0, 0, 0, 0};
    uint8_t snowDuration[4] = {0, 0, 0, 0};

    // Default weather for this zone when no rain/snow
    WeatherType defaultWeather = WeatherType::Normal;

    // Whether weather simulation is enabled for this zone
    bool enabled = true;

    // How often to check for weather changes (seconds)
    float checkIntervalSeconds = 60.0f;

    // Zone name (for logging)
    std::string zoneName;
};

/**
 * Manages weather state and transitions for the game world.
 *
 * The weather system tracks current weather conditions, handles smooth
 * transitions between weather types, and notifies listeners when weather
 * changes. Weather can be set manually or simulated based on zone configuration.
 */
class WeatherSystem {
public:
    WeatherSystem();
    ~WeatherSystem() = default;

    /**
     * Update weather state (call each frame).
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * Set the current weather type immediately.
     * @param type The new weather type
     */
    void setWeather(WeatherType type);

    /**
     * Transition to a new weather type over time.
     * @param type The target weather type
     * @param transitionTime Time in seconds for the transition
     */
    void transitionToWeather(WeatherType type, float transitionTime = 5.0f);

    /**
     * Get the current weather type.
     */
    WeatherType getCurrentWeather() const { return currentWeather_; }

    /**
     * Get the target weather type (during transitions).
     */
    WeatherType getTargetWeather() const { return targetWeather_; }

    /**
     * Check if a weather transition is in progress.
     */
    bool isTransitioning() const { return transitionProgress_ < 1.0f; }

    /**
     * Get current wind intensity (0.0 - 1.0).
     * Interpolates during transitions.
     */
    float getWindIntensity() const;

    /**
     * Configure weather for a zone.
     * @param config Zone weather configuration
     */
    void setZoneConfig(const ZoneWeatherConfig& config);

    /**
     * Set weather based on zone name (loads config automatically).
     * @param zoneName The zone short name
     */
    void setWeatherFromZone(const std::string& zoneName);

    /**
     * Enable or disable weather simulation.
     * When disabled, weather stays at current type.
     */
    void setSimulationEnabled(bool enabled) { simulationEnabled_ = enabled; }
    bool isSimulationEnabled() const { return simulationEnabled_; }

    /**
     * Add a listener to receive weather change notifications.
     * @param listener The listener to add (caller retains ownership)
     */
    void addListener(IWeatherListener* listener);

    /**
     * Remove a previously added listener.
     * @param listener The listener to remove
     */
    void removeListener(IWeatherListener* listener);

    /**
     * Add a callback for weather changes (alternative to listener interface).
     * @param callback Function to call when weather changes
     */
    void addCallback(std::function<void(WeatherType)> callback);

    /**
     * Get debug information string.
     */
    std::string getDebugInfo() const;

    /**
     * Get weather type name as string.
     */
    static const char* getWeatherName(WeatherType type);

private:
    /**
     * Notify all listeners of a weather change.
     */
    void notifyListeners(WeatherType newWeather);

    /**
     * Check if weather should change based on zone config.
     */
    void checkWeatherChange();

    /**
     * Roll for weather change based on zone probabilities.
     */
    WeatherType rollForWeather();

    /**
     * Get wind intensity for a weather type.
     */
    static float getWindIntensityForWeather(WeatherType type);

    // Current state
    WeatherType currentWeather_ = WeatherType::Normal;
    WeatherType targetWeather_ = WeatherType::Normal;
    float transitionProgress_ = 1.0f;  // 1.0 = complete
    float transitionDuration_ = 5.0f;

    // Simulation state
    ZoneWeatherConfig zoneConfig_;
    bool simulationEnabled_ = true;
    float timeSinceLastCheck_ = 0.0f;
    float currentWeatherDuration_ = 0.0f;  // How long current weather lasts
    float currentWeatherElapsed_ = 0.0f;   // How long we've been in current weather

    // Random number generator for weather simulation
    std::mt19937 rng_;

    // Listeners
    std::vector<IWeatherListener*> listeners_;
    std::vector<std::function<void(WeatherType)>> callbacks_;
};

/**
 * Load zone weather configuration from a JSON file.
 * @param zoneName The zone short name
 * @param config Output configuration
 * @return true if config was loaded, false if using defaults
 */
bool loadZoneWeatherConfig(const std::string& zoneName, ZoneWeatherConfig& config);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_WEATHER_SYSTEM_H
