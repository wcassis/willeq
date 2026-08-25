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
 * T104 added TargetChanged and AutoAttackChanged, replacing
 * Application::syncCombatState() -- the last poll in the sweep this class
 * started (T39: pet/inventory/spells/NPC-interaction; T81/T99: entities/
 * group; doors remain separately blocked on routing). TargetChanged was
 * already published end-to-end for the graphics/renderer target HUD before
 * this unit (eq_bridge.cpp's TargetIntent handler, OnSpawnRemovedGraphics,
 * and eq_entities.cpp's ZoneProcessMobHealth re-publish when the updated
 * entity is the current target) -- the gap was that nothing fed it into
 * this projection, and that most SetTarget()/ClearTarget() callers (the
 * /target command, targetNearest*, cycleTargets, assist/duel/taunt,
 * auto-hunt) never published it at all. This unit closes both gaps at once
 * by moving the publish into CombatManager::SetTarget(uint16_t)/
 * ClearTarget()/EnableAutoAttack()/DisableAutoAttack() (combat.cpp) -- the
 * sole choke points that mutate m_current_target_id/m_auto_attack_enabled --
 * so every caller is covered without touching any of them individually.
 * This does create a harmless double-publish for the two paths that already
 * published manually (TargetIntent, despawn-clears-target) -- same
 * cosmetic-only shape as T81's entity double-publish finding, not a
 * correctness issue (CombatState::setTarget()/clearTarget() only fire their
 * own downstream event on an actual value transition). Target HP tracking
 * under damage deliberately reuses the existing ZoneProcessMobHealth()
 * TargetChanged re-publish rather than adding a second correlation
 * mechanism or moving HP reads to a live entities() lookup at each call
 * site -- see refactor/T104_combat_poll_to_bridge.md's Review for the full
 * reasoning and the falsification-test result.
 */
class GameStateSyncBridge : public GameStateBridge {
public:
    GameStateSyncBridge(GameStateBridge& wrapped, state::GameState& state)
        : m_wrapped(wrapped), m_state(state) {}
    ~GameStateSyncBridge() override = default;

    /**
     * Single-stage apply: project into GameState, then forward to the
     * wrapped bridge -- both on the CALLING thread. This is the headless
     * path (Application::updateGameState() -> EverQuest::ProcessBridgeEvents()
     * on the game thread, ConsoleBridge wrapped) and what the unit tests
     * drive directly. Graphics runs must NOT use it: see the two-stage
     * entry points below.
     */
    void applyEvent(const state::GameEvent& event) override;

    // ---- T220: two-stage delivery for graphics runs ----------------------
    //
    // Before T220, graphics builds drained this bridge from the RENDER
    // thread (Application::render() -> EverQuest::PreRenderTick() ->
    // ProcessBridgeEvents() -> applyEvent()), so the projection into
    // Application::m_gameState -- the READ model the action/command layer
    // consults from the GAME thread (InputActionBridge / ActionDispatcher /
    // CommandProcessor: /afk reads player().isAFK(), /door reads doors(),
    // /target reads entities()) -- was a cross-thread write with no lock on
    // any state:: object. TSAN-confirmed (refactor/T220_state_projection_on_
    // game_thread.md). The wrapped IrrlichtBridge, however, MUST keep running
    // on the render thread (it calls the renderer). So the single drain is
    // split in two, joined by a second FIFO owned by this decorator:
    //
    //   GAME thread   projectPendingEvents(): drainEvents() [base queue,
    //                 D24-coalesced] -> projectEvent() each -> append the
    //                 whole batch, in order, to m_forwardQueue.
    //   RENDER thread forwardProjectedEvents(): swap out m_forwardQueue,
    //                 re-run the D24 coalesce (several game-thread batches
    //                 may have accumulated), m_wrapped.applyEvent() each.
    //
    // Renderer-visible ORDER is unchanged: both queues are single-producer
    // FIFOs and the game thread is the only writer of m_forwardQueue, so the
    // renderer sees events in exactly the sequence they were pushed. What
    // changes is only WHEN the projection lands (as soon as the game thread
    // ticks, instead of at the next rendered frame) -- strictly earlier, on
    // the thread that reads it. Zone-load ordering (application.cpp's P17
    // comment at loadZoneGraphicsOnThread) is untouched: while
    // Application::render() is on one of its early-return branches nothing
    // forwards, exactly as nothing drained before, and the old zone's
    // renderer state is still torn down synchronously on the loading thread.
    //
    // Headless runs keep the single-stage applyEvent() path -- ConsoleBridge
    // is game-thread already, so there is nothing to split.

    /** GAME thread (graphics runs): stage 1 -- project, then hand off. */
    void projectPendingEvents();

    /** RENDER thread (graphics runs): stage 2 -- forward to the wrapped bridge. */
    void forwardProjectedEvents();

private:
    /** The projection switch (T40..T219 cases). Mutates m_state only. */
    void projectEvent(const state::GameEvent& event);

    GameStateBridge& m_wrapped;
    state::GameState& m_state;

    // T220: projected-but-not-yet-forwarded events, game thread -> render
    // thread. Same swap-vector pattern as the base class's queues.
    std::mutex m_forwardMutex;
    std::vector<state::GameEvent> m_forwardQueue;
};

} // namespace bridge
} // namespace eqt
