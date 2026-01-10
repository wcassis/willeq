#include "client/state/event_bus.h"
#include <algorithm>

namespace eqt {
namespace state {

ListenerHandle EventBus::subscribe(EventListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ListenerHandle handle = m_nextHandle++;
    m_listeners.push_back({handle, std::move(listener), GameEventType::PlayerMoved, false});
    return handle;
}

ListenerHandle EventBus::subscribe(GameEventType type, EventListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ListenerHandle handle = m_nextHandle++;
    m_listeners.push_back({handle, std::move(listener), type, true});
    return handle;
}

void EventBus::unsubscribe(ListenerHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [handle](const ListenerEntry& entry) {
                return entry.handle == handle;
            }),
        m_listeners.end()
    );
}

void EventBus::publish(const GameEvent& event) {
    // Copy listeners under lock, then invoke without lock to prevent deadlocks
    std::vector<EventListener> listenersToCall;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_listeners) {
            if (!entry.hasFilter || entry.filterType == event.type) {
                listenersToCall.push_back(entry.listener);
            }
        }
    }

    // Invoke listeners outside of lock
    for (const auto& listener : listenersToCall) {
        listener(event);
    }
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.clear();
}

} // namespace state
} // namespace eqt
