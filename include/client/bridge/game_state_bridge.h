#pragma once

#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include <mutex>
#include <vector>
#include <unordered_map>

namespace eqt {
namespace bridge {

/**
 * GameStateBridge - Abstract interface for cross-thread communication between
 * the game thread and a renderer.
 *
 * The game thread pushes events (state changes) and the renderer pushes intents
 * (user actions). Both sides drain the opposite queue each tick. The bridge owns
 * two thread-safe queues using the swap-vector pattern for O(1) lock time.
 *
 * Multiple renderers are supported — each gets its own bridge instance. Zero
 * renderers (headless mode) is valid — no bridge attached, events are discarded.
 *
 * No renderer or EverQuest includes in this file.
 */
class GameStateBridge {
public:
    virtual ~GameStateBridge() = default;

    // --- Game thread calls ---

    /**
     * Push an event for the renderer to consume.
     * Thread-safe. Called from the game thread.
     */
    void pushEvent(state::GameEvent event) {
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventQueue_.push_back(std::move(event));
    }

    /**
     * Drain all pending intents from the renderer.
     * Thread-safe. Called from the game thread.
     * Returns the intents and clears the queue atomically.
     * D24: Coalesces consecutive PlayerPositionChanged intents (keeps latest).
     */
    std::vector<events::RendererIntent> drainIntents() {
        std::vector<events::RendererIntent> result;
        {
            std::lock_guard<std::mutex> lock(intentMutex_);
            result.swap(intentQueue_);
        }
        coalesceIntents(result);
        return result;
    }

    // --- Renderer thread calls ---

    /**
     * Push an intent for the game thread to consume.
     * Thread-safe. Called from the renderer thread.
     */
    void pushIntent(events::RendererIntent intent) {
        std::lock_guard<std::mutex> lock(intentMutex_);
        intentQueue_.push_back(std::move(intent));
    }

    /**
     * Drain all pending events from the game thread.
     * Thread-safe. Called from the consumer thread -- which thread that is
     * depends on the bridge: for the plain single-stage bridges
     * (ConsoleBridge, test bridges) it is whoever drains and applies;
     * for GameStateSyncBridge in a graphics run it is the GAME thread
     * (GameStateSyncBridge::stagePendingEvents(), T220), never the
     * render thread -- the render thread drains the projected queue via
     * GameStateSyncBridge::forwardStagedEvents() instead.
     * Returns the events and clears the queue atomically.
     * D24: Coalesces consecutive EntityMoved events per spawnId (keeps latest).
     */
    std::vector<state::GameEvent> drainEvents() {
        std::vector<state::GameEvent> result;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            // B945: the queue's capacity is not a fixed cap -- a burst (the
            // zone-in spawn burst) may grow it -- but it is RETURNED once the
            // burst has drained. eventQueueHighWater_ is the peak of the
            // queue's live capacity, read BEFORE the swap (after it, the
            // queue holds the drained vector's buffer, not the queue's own);
            // whenever a drain leaves the queue under half of that mark, the
            // drained events move off into a vector at their exact drained
            // size (a single copy) and the old, larger buffer frees -- but
            // only if it truly retained more than the drained count (a
            // growth step that allocated exactly the burst size is not an
            // excess to release). The halving rule means a live burst can
            // never force its own shrink (the peak grows with the burst) and
            // steady state keeps one allocation whose size tracks the live
            // queue, so only the burst's peak is released -- never a re-alloc
            // per frame. (A std::vector's capacity is a non-binding hint, so
            // this uses an explicit move-to-accurate-size rather than
            // shrink_to_fit, which libstdc++ may ignore.)
            //
            // What this bounds: the buffer the drain's OWN (returned) vector
            // keeps across the burst->steady transition. Note it is NOT the
            // fix for the 3.1 MB "GameEvent staging buffer at ready" the issue
            // named -- that block is the in-flight zone-in burst sitting in
            // the render-side staging queue (m_forwardQueue) while the render
            // thread is busy loading the zone, and it is freed at the FIRST
            // forwardStagedEvents() after the load -- the base already does
            // this (B945's x86 trace: the block is alive at the `ready` point
            // and gone by `steady_60s`, in BOTH the base and this build). See
            // refactor/B945/README.md for the before/after pair.
            const size_t liveCapacity = eventQueue_.capacity();
            result.swap(eventQueue_);
            const size_t drained = result.size();
            if (liveCapacity > eventQueueHighWater_) {
                eventQueueHighWater_ = liveCapacity;
            } else if (drained * 2 < eventQueueHighWater_ &&
                       liveCapacity > drained) {
                result = std::vector<state::GameEvent>(
                    std::make_move_iterator(result.begin()),
                    std::make_move_iterator(result.end()));
            }
        }
        coalesceEvents(result);
        return result;
    }

    /**
     * B945: bytes the event queue's buffer still holds
     * (capacity * sizeof(state::GameEvent)), for the capacity-bounded test.
     * Thread-safe: reads under the same mutex pushEvent()/drainEvents() do.
     */
    size_t eventQueueCapacityBytes() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(eventMutex_));
        return eventQueue_.capacity() * sizeof(state::GameEvent);
    }

    /**
     * Apply a single game event to the renderer.
     * Called by the renderer after draining events.
     * Subclasses implement this to translate events into renderer commands.
     */
    virtual void applyEvent(const state::GameEvent& event) = 0;

protected:
    // D24: Coalesce EntityMoved events — keep only the latest per spawnId.
    // Preserves ordering of non-EntityMoved events and the final EntityMoved per entity.
    // T220: protected (was private) so GameStateSyncBridge can re-apply it
    // to its render-side queue -- that queue accumulates several game-thread
    // batches between render drains, and the renderer must keep seeing at
    // most one EntityMoved per entity per drain, exactly as it did when it
    // drained eventQueue_ directly.
    static void coalesceEvents(std::vector<state::GameEvent>& events) {
        if (events.size() < 2) return;

        // Find the last EntityMoved index per spawnId
        std::unordered_map<uint16_t, size_t> lastMoveIndex;
        for (size_t i = 0; i < events.size(); ++i) {
            if (events[i].type == state::GameEventType::EntityMoved) {
                auto& d = std::get<state::EntityMovedData>(events[i].data);
                lastMoveIndex[d.spawnId] = i;
            }
        }

        if (lastMoveIndex.empty()) return;

        // Remove earlier EntityMoved events for entities that have a later one
        std::vector<state::GameEvent> filtered;
        filtered.reserve(events.size());
        for (size_t i = 0; i < events.size(); ++i) {
            if (events[i].type == state::GameEventType::EntityMoved) {
                auto& d = std::get<state::EntityMovedData>(events[i].data);
                if (lastMoveIndex[d.spawnId] != i) continue;  // Skip — superseded
            }
            filtered.push_back(std::move(events[i]));
        }
        events = std::move(filtered);
    }

private:
    std::mutex eventMutex_;
    std::vector<state::GameEvent> eventQueue_;

    // B945: high-water mark of eventQueue_'s live capacity -- the burst that
    // last grew it (read right after each drain's swap, under eventMutex_).
    // drainEvents() gives the queue's buffer back once a drain leaves the
    // queue under half of that mark.
    size_t eventQueueHighWater_ = 0;

    std::mutex intentMutex_;
    std::vector<events::RendererIntent> intentQueue_;

    // D24: Coalesce PlayerPositionChanged intents — keep only the latest.
    static void coalesceIntents(std::vector<events::RendererIntent>& intents) {
        if (intents.size() < 2) return;

        // Find the last PlayerPositionChanged, and the last
        // PlayerModelGeometryChanged (T311: the renderer republishes the
        // player's measured origin-to-feet distance every frame -- it has
        // nowhere to cache "did this change" without a renderer member -- so
        // this queue would otherwise grow one entry per rendered frame between
        // game-thread drains. Both are pure latest-wins state.)
        int lastPosIdx = -1;
        int lastGeomIdx = -1;
        for (int i = static_cast<int>(intents.size()) - 1; i >= 0; --i) {
            if (lastPosIdx < 0 && std::holds_alternative<events::PlayerPositionChanged>(intents[i])) {
                lastPosIdx = i;
            }
            if (lastGeomIdx < 0 && std::holds_alternative<events::PlayerModelGeometryChanged>(intents[i])) {
                lastGeomIdx = i;
            }
            if (lastPosIdx >= 0 && lastGeomIdx >= 0) break;
        }

        if (lastPosIdx < 0 && lastGeomIdx < 0) return;

        // Remove the superseded ones
        std::vector<events::RendererIntent> filtered;
        filtered.reserve(intents.size());
        for (int i = 0; i < static_cast<int>(intents.size()); ++i) {
            if (std::holds_alternative<events::PlayerPositionChanged>(intents[i]) && i != lastPosIdx) {
                continue;  // Skip — superseded
            }
            if (std::holds_alternative<events::PlayerModelGeometryChanged>(intents[i]) && i != lastGeomIdx) {
                continue;  // Skip — superseded
            }
            filtered.push_back(std::move(intents[i]));
        }
        intents = std::move(filtered);
    }
};

} // namespace bridge
} // namespace eqt
