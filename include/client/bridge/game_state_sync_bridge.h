#pragma once

#include "client/bridge/game_state_bridge.h"

namespace eqt {
namespace state {
class GameState;
} // namespace state
} // namespace eqt

namespace eqt {
namespace bridge {

/**
 * GameStateSyncBridge - Decorator that feeds Application::m_gameState from
 * typed bridge events, then delegates to the wrapped mode bridge
 * (IrrlichtBridge / ConsoleBridge).
 *
 * T40: this replaces Application::syncPetState() (a once-per-tick poll of
 * EverQuest accessors) with event-driven state. GameStateBridge is an
 * abstract base with a single, destructively-drained queue and exactly one
 * concrete bridge instance per run, so a second independent consumer would
 * never see events (the first drainEvents() empties the queue). This
 * decorator is the seam that avoids that: it IS the bridge instance
 * EverQuest pushes events onto (Application assigns it to m_bridge and
 * passes it to both EverQuest and the renderer), so events/intents flow
 * through exactly one queue, and it intercepts applyEvent() to update state
 * before forwarding the event unchanged to the wrapped bridge.
 *
 * Keep the switch in the .cpp NARROW: only events a sync*State() replacement
 * has verified as fully reconstructible should be handled here. Every event
 * this class doesn't recognize passes through to the wrapped bridge
 * untouched. This unit (T40) handles pet events only:
 * PetCreated, PetRemoved, PetStatsChanged, PetButtonStateChanged.
 */
class GameStateSyncBridge : public GameStateBridge {
public:
    GameStateSyncBridge(GameStateBridge& wrapped, state::GameState& state)
        : m_wrapped(wrapped), m_state(state) {}
    ~GameStateSyncBridge() override = default;

    void applyEvent(const state::GameEvent& event) override;

private:
    GameStateBridge& m_wrapped;
    state::GameState& m_state;
};

} // namespace bridge
} // namespace eqt
