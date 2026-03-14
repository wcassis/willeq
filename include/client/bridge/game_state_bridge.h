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
     * Thread-safe. Called from the renderer thread.
     * Returns the events and clears the queue atomically.
     * D24: Coalesces consecutive EntityMoved events per spawnId (keeps latest).
     */
    std::vector<state::GameEvent> drainEvents() {
        std::vector<state::GameEvent> result;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            result.swap(eventQueue_);
        }
        coalesceEvents(result);
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

    // D24: Coalesce EntityMoved events — keep only the latest per spawnId.
    // Preserves ordering of non-EntityMoved events and the final EntityMoved per entity.
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

    // D24: Coalesce PlayerPositionChanged intents — keep only the latest.
    static void coalesceIntents(std::vector<events::RendererIntent>& intents) {
        if (intents.size() < 2) return;

        // Find the last PlayerPositionChanged
        int lastPosIdx = -1;
        for (int i = static_cast<int>(intents.size()) - 1; i >= 0; --i) {
            if (std::holds_alternative<events::PlayerPositionChanged>(intents[i])) {
                lastPosIdx = i;
                break;
            }
        }

        if (lastPosIdx < 0) return;

        // Remove earlier PlayerPositionChanged intents
        std::vector<events::RendererIntent> filtered;
        filtered.reserve(intents.size());
        for (int i = 0; i < static_cast<int>(intents.size()); ++i) {
            if (std::holds_alternative<events::PlayerPositionChanged>(intents[i]) && i != lastPosIdx) {
                continue;  // Skip — superseded
            }
            filtered.push_back(std::move(intents[i]));
        }
        intents = std::move(filtered);
    }
};

} // namespace bridge
} // namespace eqt
