#include "client/state/world_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

WorldState::WorldState() = default;

// ========== Zone Information ==========

void WorldState::setZoneName(const std::string& name) {
    if (m_zoneName != name) {
        m_zoneName = name;
        fireZoneChangedEvent();
    }
}

void WorldState::setZone(const std::string& name, uint16_t id) {
    bool changed = (m_zoneName != name || m_zoneId != id);
    m_zoneName = name;
    m_zoneId = id;
    if (changed) {
        fireZoneChangedEvent();
    }
}

void WorldState::setZoneLoadProgress(float progress, const std::string& statusMessage) {
    m_zoneLoadProgress = progress;
    if (!statusMessage.empty()) {
        m_zoneLoadStatus = statusMessage;
    }
    fireZoneLoadingEvent();
}

// ========== Time of Day ==========

void WorldState::setTimeOfDay(uint8_t hour, uint8_t minute, uint8_t day, uint8_t month, uint16_t year) {
    bool changed = (m_timeHour != hour || m_timeMinute != minute ||
                    m_timeDay != day || m_timeMonth != month || m_timeYear != year);
    m_timeHour = hour;
    m_timeMinute = minute;
    m_timeDay = day;
    m_timeMonth = month;
    m_timeYear = year;
    if (changed) {
        fireTimeOfDayChangedEvent();
    }
}

void WorldState::setTimeOfDay(uint8_t hour, uint8_t minute) {
    bool changed = (m_timeHour != hour || m_timeMinute != minute);
    m_timeHour = hour;
    m_timeMinute = minute;
    if (changed) {
        fireTimeOfDayChangedEvent();
    }
}

// ========== Zone Transition State ==========

void WorldState::setLastZoneCheckPosition(float x, float y, float z) {
    m_lastZoneCheckX = x;
    m_lastZoneCheckY = y;
    m_lastZoneCheckZ = z;
}

void WorldState::setPendingZone(uint16_t zoneId, float x, float y, float z, float heading) {
    m_pendingZoneId = zoneId;
    m_pendingZoneX = x;
    m_pendingZoneY = y;
    m_pendingZoneZ = z;
    m_pendingZoneHeading = heading;
}

void WorldState::clearPendingZone() {
    m_pendingZoneId = 0;
    m_pendingZoneX = 0.0f;
    m_pendingZoneY = 0.0f;
    m_pendingZoneZ = 0.0f;
    m_pendingZoneHeading = 0.0f;
}

// ========== Reset ==========

void WorldState::resetZoneState() {
    // Clear zone transition state
    m_zoneLineTriggered = false;
    m_zoneChangeRequested = false;
    m_zoneChangeApproved = false;
    m_isZoning = false;
    clearPendingZone();

    // Reset connection state
    m_zoneConnected = false;
    m_clientReady = false;

    // Reset loading state
    m_isZoneLoading = false;
    m_zoneLoadProgress = 0.0f;
    m_zoneLoadStatus.clear();
}

// ========== Event Firing ==========

void WorldState::fireZoneChangedEvent() {
    if (m_eventBus) {
        ZoneChangedData data;
        data.zoneName = m_zoneName;
        data.zoneId = m_zoneId;
        data.x = m_pendingZoneX;
        data.y = m_pendingZoneY;
        data.z = m_pendingZoneZ;
        data.heading = m_pendingZoneHeading;
        m_eventBus->publish(GameEventType::ZoneChanged, data);
    }
}

void WorldState::fireTimeOfDayChangedEvent() {
    if (m_eventBus) {
        TimeOfDayChangedData data;
        data.hour = m_timeHour;
        data.minute = m_timeMinute;
        data.day = m_timeDay;
        data.month = m_timeMonth;
        data.year = m_timeYear;
        m_eventBus->publish(GameEventType::TimeOfDayChanged, data);
    }
}

void WorldState::fireZoneLoadingEvent() {
    if (m_eventBus) {
        ZoneLoadingData data;
        data.zoneName = m_zoneName;
        data.zoneId = m_zoneId;
        data.progress = m_zoneLoadProgress;
        data.statusMessage = m_zoneLoadStatus;
        m_eventBus->publish(GameEventType::ZoneLoading, data);
    }
}

} // namespace state
} // namespace eqt
