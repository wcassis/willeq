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
 * untouched. T40 handles pet events: PetCreated, PetRemoved,
 * PetStatsChanged, PetButtonStateChanged. T42 added InventorySlotChanged,
 * replacing Application::syncInventoryState() -- reconstructible only once
 * vendor buy/sell and bank-window population were made to publish it (see
 * refactor/T42_inventory_publishes_and_convert.md). T44 added
 * PlayerSpawnIdSet (so the spell cases below can recognize our own casts),
 * SpellGemChanged, SpellCastStarted, and SpellCastComplete, replacing
 * Application::syncSpellState() -- SpellCastComplete was defined but never
 * published before this unit, the one coverage gap found (see
 * refactor/T44_spell_state_via_bridge.md). Continuous values (cast
 * progress, gem cooldown countdown) are NOT event-driven here: SpellState
 * stores a start-time + total from a single event and computes elapsed on
 * read (spell_state.h), so there is nothing for this decorator to do on a
 * per-tick basis -- publishing one event per tick was explicitly rejected
 * as strictly worse than the poll it replaces. T49 added
 * VendorWindowOpened/Closed, BankWindowOpened/Closed, and
 * TrainerWindowOpened/Closed, replacing
 * Application::syncNPCInteractionState() -- the last poll in the
 * state-layer sweep (T39->T49). All six events were already published
 * end-to-end before this unit; the gap this unit closed was that nothing
 * read the resulting state (see the reader command in
 * action::CommandProcessor).
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
