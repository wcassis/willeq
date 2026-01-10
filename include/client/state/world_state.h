#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

/**
 * WorldState - Contains zone and world-level state data.
 *
 * This class encapsulates state related to the current zone including
 * zone information, time of day, weather, and zone transition state.
 */
class WorldState {
public:
    WorldState();
    ~WorldState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Zone Information ==========

    const std::string& zoneName() const { return m_zoneName; }
    void setZoneName(const std::string& name);

    uint16_t zoneId() const { return m_zoneId; }
    void setZoneId(uint16_t id) { m_zoneId = id; }

    void setZone(const std::string& name, uint16_t id);

    // Zone loading state
    bool isZoneLoading() const { return m_isZoneLoading; }
    void setZoneLoading(bool loading) { m_isZoneLoading = loading; }

    float zoneLoadProgress() const { return m_zoneLoadProgress; }
    void setZoneLoadProgress(float progress, const std::string& statusMessage = "");

    const std::string& zoneLoadStatus() const { return m_zoneLoadStatus; }

    // ========== Time of Day ==========

    uint8_t timeHour() const { return m_timeHour; }
    uint8_t timeMinute() const { return m_timeMinute; }
    uint8_t timeDay() const { return m_timeDay; }
    uint8_t timeMonth() const { return m_timeMonth; }
    uint16_t timeYear() const { return m_timeYear; }

    void setTimeOfDay(uint8_t hour, uint8_t minute, uint8_t day, uint8_t month, uint16_t year);
    void setTimeOfDay(uint8_t hour, uint8_t minute);

    // Time queries
    bool isNight() const { return m_timeHour < 6 || m_timeHour >= 20; }
    bool isDay() const { return !isNight(); }

    // ========== Weather ==========

    enum class WeatherType {
        Clear = 0,
        Cloudy = 1,
        Rain = 2,
        Snow = 3
    };

    WeatherType weather() const { return m_weather; }
    void setWeather(WeatherType weather) { m_weather = weather; }

    uint8_t weatherIntensity() const { return m_weatherIntensity; }
    void setWeatherIntensity(uint8_t intensity) { m_weatherIntensity = intensity; }

    // ========== Zone Transition State ==========

    // Zone line detection
    bool zoneLineTriggered() const { return m_zoneLineTriggered; }
    void setZoneLineTriggered(bool triggered) { m_zoneLineTriggered = triggered; }

    std::chrono::steady_clock::time_point zoneLineTriggerTime() const { return m_zoneLineTriggerTime; }
    void setZoneLineTriggerTime(std::chrono::steady_clock::time_point time) { m_zoneLineTriggerTime = time; }

    // Last zone check position (for zone line detection)
    float lastZoneCheckX() const { return m_lastZoneCheckX; }
    float lastZoneCheckY() const { return m_lastZoneCheckY; }
    float lastZoneCheckZ() const { return m_lastZoneCheckZ; }
    void setLastZoneCheckPosition(float x, float y, float z);

    // Pending zone transition
    uint16_t pendingZoneId() const { return m_pendingZoneId; }
    float pendingZoneX() const { return m_pendingZoneX; }
    float pendingZoneY() const { return m_pendingZoneY; }
    float pendingZoneZ() const { return m_pendingZoneZ; }
    float pendingZoneHeading() const { return m_pendingZoneHeading; }

    void setPendingZone(uint16_t zoneId, float x, float y, float z, float heading);
    void clearPendingZone();
    bool hasPendingZone() const { return m_pendingZoneId != 0; }

    // Zone change state
    bool zoneChangeRequested() const { return m_zoneChangeRequested; }
    void setZoneChangeRequested(bool requested) { m_zoneChangeRequested = requested; }

    bool zoneChangeApproved() const { return m_zoneChangeApproved; }
    void setZoneChangeApproved(bool approved) { m_zoneChangeApproved = approved; }

    // ========== Connection State ==========

    bool isZoneConnected() const { return m_zoneConnected; }
    void setZoneConnected(bool connected) { m_zoneConnected = connected; }

    bool isClientReady() const { return m_clientReady; }
    void setClientReady(bool ready) { m_clientReady = ready; }

    bool isFullyZonedIn() const { return m_zoneConnected && m_clientReady; }

    // ========== Reset ==========

    // Reset zone state (for zone changes)
    void resetZoneState();

private:
    void fireZoneChangedEvent();
    void fireTimeOfDayChangedEvent();
    void fireZoneLoadingEvent();

    EventBus* m_eventBus = nullptr;

    // Zone information
    std::string m_zoneName;
    uint16_t m_zoneId = 0;
    bool m_isZoneLoading = false;
    float m_zoneLoadProgress = 0.0f;
    std::string m_zoneLoadStatus;

    // Time of day
    uint8_t m_timeHour = 12;
    uint8_t m_timeMinute = 0;
    uint8_t m_timeDay = 1;
    uint8_t m_timeMonth = 1;
    uint16_t m_timeYear = 3100;

    // Weather
    WeatherType m_weather = WeatherType::Clear;
    uint8_t m_weatherIntensity = 0;

    // Zone line detection
    bool m_zoneLineTriggered = false;
    std::chrono::steady_clock::time_point m_zoneLineTriggerTime;
    float m_lastZoneCheckX = 0.0f;
    float m_lastZoneCheckY = 0.0f;
    float m_lastZoneCheckZ = 0.0f;

    // Pending zone transition
    uint16_t m_pendingZoneId = 0;
    float m_pendingZoneX = 0.0f;
    float m_pendingZoneY = 0.0f;
    float m_pendingZoneZ = 0.0f;
    float m_pendingZoneHeading = 0.0f;

    // Zone change state
    bool m_zoneChangeRequested = false;
    bool m_zoneChangeApproved = false;

    // Connection state
    bool m_zoneConnected = false;
    bool m_clientReady = false;
};

} // namespace state
} // namespace eqt
