#include "client/bridge/game_state_sync_bridge.h"
#include "client/core/memory_ledger.h"  // B919: the gs.event_stage owner row

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
    // B919 (#238): named `game_state` owner row (WILLEQ_LEDGER_OWNERS=game_state):
    // the game->render staging FIFO (m_forwardQueue) and the transient batch
    // vector are gs.event_stage. The render thread's own copy is TRANSIENT
    // (freed at the end of each forward), so the row's measured content at
    // `ready` is the burst the renderer has not yet consumed.
    eqt::memory::StartupScope owner("gs.event_stage");
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
        // B945: give the staging buffer back once the burst has drained --
        // the same halving rule as the base class's eventQueue_
        // (drainEvents()). m_forwardHighWater is the peak of the queue's
        // live capacity, read BEFORE the swap; a live burst can never force
        // its own shrink (the peak grows with the burst) and steady state
        // keeps one allocation whose size tracks the live queue. If a drain
        // leaves the queue under half the peak and the burst's buffer truly
        // retained more than the drained count, the drained events move off
        // into a vector at their exact drained size and the old, larger
        // buffer frees. (A std::vector's capacity is a non-binding hint, so
        // this uses an explicit move-to-accurate-size rather than
        // shrink_to_fit, which libstdc++ may ignore.)
        const size_t liveCapacity = m_forwardQueue.capacity();
        events.swap(m_forwardQueue);
        const size_t drained = events.size();
        if (liveCapacity > m_forwardHighWater) {
            m_forwardHighWater = liveCapacity;
        } else if (drained * 2 < m_forwardHighWater && liveCapacity > drained) {
            events = std::vector<state::GameEvent>(
                std::make_move_iterator(events.begin()),
                std::make_move_iterator(events.end()));
        }
    }
    // Re-coalesce: the queue may hold several game-thread batches, and the
    // renderer's contract is at most one EntityMoved per entity per drain
    // (D24).
    coalesceEvents(events);
    for (const auto& event : events) {
        m_wrapped.applyEvent(event);
    }
}

size_t GameStateSyncBridge::forwardQueueCapacityBytes() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_forwardMutex));
    return m_forwardQueue.capacity() * sizeof(state::GameEvent);
}

} // namespace bridge
} // namespace eqt
