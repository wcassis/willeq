#include "client/bridge/game_state_sync_bridge.h"

namespace eqt {
namespace bridge {

void GameStateSyncBridge::applyEvent(const state::GameEvent& event) {
    // Single-stage path (headless + unit tests): forward on the calling
    // thread. Graphics runs use stagePendingEvents() / forwardStagedEvents()
    // instead -- see the header.
    m_wrapped.applyEvent(event);
}

void GameStateSyncBridge::stagePendingEvents() {
    // Stage 1, GAME thread. drainEvents() is the base class's D24-coalescing
    // swap-drain of the queue EverQuest (and the loading thread, and the
    // renderer's own SystemMessage pushes) push onto.
    auto events = drainEvents();
    if (events.empty()) return;
    std::lock_guard<std::mutex> lock(m_forwardMutex);
    if (m_forwardQueue.empty()) {
        m_forwardQueue = std::move(events);
    } else {
        m_forwardQueue.insert(m_forwardQueue.end(),
                              std::make_move_iterator(events.begin()),
                              std::make_move_iterator(events.end()));
    }
}

void GameStateSyncBridge::forwardStagedEvents() {
    // Stage 2, RENDER thread.
    std::vector<state::GameEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_forwardMutex);
        events.swap(m_forwardQueue);
    }
    // Re-coalesce: the queue may hold several game-thread batches, and the
    // renderer's contract is at most one EntityMoved per entity per drain
    // (D24).
    coalesceEvents(events);
    for (const auto& event : events) {
        m_wrapped.applyEvent(event);
    }
}

} // namespace bridge
} // namespace eqt
