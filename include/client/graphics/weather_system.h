#ifndef EQT_GRAPHICS_WEATHER_SYSTEM_H
#define EQT_GRAPHICS_WEATHER_SYSTEM_H

#include "client/graphics/tree_wind_config.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace EQT {
namespace Graphics {

// Forward declarations
struct WeatherCommandData;

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
 * Thin facade over the weather state machine, which runs on the SimulationWorker
 * background thread. Setters queue commands for the worker; getters return cached
 * state from the last applyWorkerResults() call. Listener dispatch happens on the
 * main thread when worker results are applied.
 */
class WeatherSystem {
public:
    WeatherSystem();
    ~WeatherSystem() = default;

    /**
     * Set the current weather type immediately (queues command for worker).
     */
    void setWeather(WeatherType type);

    /**
     * Transition to a new weather type over time (queues command for worker).
     */
    void transitionToWeather(WeatherType type, float transitionTime = 5.0f);

    /**
     * Get the current weather type (cached from worker).
     */
    WeatherType getCurrentWeather() const { return currentWeather_; }

    /**
     * Get the target weather type (cached from worker).
     */
    WeatherType getTargetWeather() const { return targetWeather_; }

    /**
     * Check if a weather transition is in progress (cached from worker).
     */
    bool isTransitioning() const { return transitionProgress_ < 1.0f; }

    /**
     * Get current wind intensity (cached from worker).
     */
    float getWindIntensity() const { return windIntensity_; }

    /**
     * Configure weather for a zone (queues command for worker).
     */
    void setZoneConfig(const ZoneWeatherConfig& config);

    /**
     * Set weather based on zone name (queues command for worker).
     */
    void setWeatherFromZone(const std::string& zoneName);

    /**
     * Enable or disable weather simulation (queues command for worker).
     */
    void setSimulationEnabled(bool enabled);
    bool isSimulationEnabled() const { return simulationEnabled_; }

    /**
     * Drain pending commands for the worker thread.
     * Called from postSimulationInput() on the main thread.
     */
    std::vector<WeatherCommandData> drainCommands();

    /**
     * Apply results from the worker thread.
     * Called from applySimulationResults() on the main thread.
     * Fires listener notifications if weather changed.
     */
    void applyWorkerResults(uint8_t currentWeather, uint8_t targetWeather,
                            float transitionProgress, float windIntensity,
                            bool weatherChanged, uint8_t newWeatherType);

    /**
     * Add a listener to receive weather change notifications.
     */
    void addListener(IWeatherListener* listener);

    /**
     * Remove a previously added listener.
     */
    void removeListener(IWeatherListener* listener);

    /**
     * Add a callback for weather changes.
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
    void notifyListeners(WeatherType newWeather);

    // Cached state (updated from worker via applyWorkerResults)
    WeatherType currentWeather_ = WeatherType::Normal;
    WeatherType targetWeather_ = WeatherType::Normal;
    float transitionProgress_ = 1.0f;
    float windIntensity_ = 0.6f;

    // Local cache for getter
    bool simulationEnabled_ = true;

    // Zone config cached for debug info
    ZoneWeatherConfig zoneConfig_;

    // Command queue (main thread produces, drained for worker each frame)
    std::vector<WeatherCommandData> pendingCommands_;

    // Listeners
    std::vector<IWeatherListener*> listeners_;
    std::vector<std::function<void(WeatherType)>> callbacks_;
};

/**
 * Load zone weather configuration from a JSON file.
 */
bool loadZoneWeatherConfig(const std::string& zoneName, ZoneWeatherConfig& config);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_WEATHER_SYSTEM_H
