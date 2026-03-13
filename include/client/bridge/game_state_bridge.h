#pragma once

#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include <mutex>
#include <vector>

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
     */
    std::vector<events::RendererIntent> drainIntents() {
        std::vector<events::RendererIntent> result;
        {
            std::lock_guard<std::mutex> lock(intentMutex_);
            result.swap(intentQueue_);
        }
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
     * Thread-safe. Called from the renderer thread.
     * Returns the events and clears the queue atomically.
     */
    std::vector<state::GameEvent> drainEvents() {
        std::vector<state::GameEvent> result;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            result.swap(eventQueue_);
        }
        return result;
    }

    /**
     * Apply a single game event to the renderer.
     * Called by the renderer after draining events.
     * Subclasses implement this to translate events into renderer commands.
     */
    virtual void applyEvent(const state::GameEvent& event) = 0;

private:
    std::mutex eventMutex_;
    std::vector<state::GameEvent> eventQueue_;

    std::mutex intentMutex_;
    std::vector<events::RendererIntent> intentQueue_;
};

} // namespace bridge
} // namespace eqt
