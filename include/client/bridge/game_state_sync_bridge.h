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
 *
 * T81 added entities() -- unlike pet/inventory/spells, nothing polled
 * entities before this unit, so Application::m_gameState->entities() was
 * permanently empty ("/who" printed nothing, /face and friends always
 * reported "Entity not found"). EverQuest::SyncEntityToGameState()
 * (eq_player_info.cpp:261) already holds the complete Entity struct on
 * every spawn/self-entity call, so this is add-or-replace snapshot
 * publishing, not accumulation -- verified reconstructible the same way
 * pet/inventory/spells were, with one caveat: ZoneProcessWearChange()
 * mutates equipment directly without re-invoking SyncEntityToGameState(),
 * so a spawn-only snapshot would go stale after a runtime gear swap (this
 * staleness pre-exists in EverQuest::m_game_state too, not introduced by
 * this projection). EntityAppearanceChanged, already published for that
 * exact case, closes it with a partial field update. Volume: a full
 * snapshot is NOT published per tick -- SyncEntityToGameState() itself is
 * only ever called on spawn/self-entity (3 call sites total, none in a
 * movement handler); the existing D24-coalesced EntityMoved event carries
 * position instead, applied as a partial update via
 * EntityManager::updateEntityPosition(). ZoneUnloading (already published,
 * before eq_zone.cpp:270's clear) is the trigger to clear this projection's
 * entities -- the source-side clear only fires events on
 * EverQuest::m_game_state's own private bus, which nothing reads, so it
 * would never otherwise reach this decorator. See
 * refactor/T81_entity_projection.md for the reconstructibility verification
 * and the double-publish finding (EntityManager::addEntity()/removeEntity()
 * re-fire onto Application::m_gameState's own EventBus, which ConsoleRenderer
 * subscribes to -- a real, cosmetic-only duplicate console line on
 * spawn/despawn in console-connected modes, not a data-correctness issue).
 *
 * T103 added trade() -- unlike every case above, TradeManager
 * (client/trade_manager.h) already published a complete, typed event for
 * every state transition it has (TradeRequestReceived, TradeStarted,
 * TradeItemUpdated, TradeMoneyUpdated, TradeAcceptStateChanged,
 * TradeCancelled, TradeCompleted -- eq_merchant.cpp's
 * SetupTradeWindowCallbacks(), predating this unit); the gap was purely
 * that nothing consumed them (IrrlichtBridge only LOG_TRACE'd each case).
 * No new event type was needed. Two fields are NOT reconstructible from
 * the existing event stream and are NOT tracked in TradeState, both
 * verified rather than assumed missing -- see trade_state.h's class
 * comment and refactor/T103_trade_state_domain.md's Review: the player's
 * own offered items (TradeManager's own-item path has zero production
 * callers; items move via the ordinary inventory MoveItem flow instead,
 * which InventoryState does not model either), and the outgoing
 * "request sent, awaiting response" sub-state (its transition publishes no
 * event at all). This same trade domain is also populated directly and
 * synchronously on EverQuest's OWN GameState instance (not through this
 * bridge -- see EverQuest::SetupTradeWindowCallbacks()), which is what
 * EqActionHandler reads; that mirrors how SyncEntityToGameState() already
 * populates EverQuest::m_game_state.entities() directly while this
 * decorator separately populates Application::m_gameState.entities() from
 * the same underlying data via events (T81) -- two instances, two
 * mechanisms, same source of truth.
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
