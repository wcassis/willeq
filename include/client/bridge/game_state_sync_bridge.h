#pragma once

#include "client/bridge/game_state_bridge.h"

#include <mutex>
#include <vector>

namespace eqt {
namespace bridge {

/**
 * GameStateSyncBridge - the ONE concrete bridge instance a run has. It
 * decorates the mode bridge (IrrlichtBridge / ConsoleBridge) and is what
 * Application wires both EverQuest and the renderer to, so events and
 * intents flow through exactly one queue.
 *
 * Why two hops (T220/T227). There is a single state::GameState --
 * EverQuest::m_game_state, written by the packet handlers on the GAME
 * thread and read by the action/command layer on that same thread (T227
 * deleted the second, event-projected copy Application used to own, and
 * with it the 40-case projection switch this class carried from T40 to
 * T221; the history is in refactor/T40..T227 and docs/decisions.md). The
 * wrapped IrrlichtBridge, however, MUST run on the RENDER thread (it calls
 * the renderer), while the base queue is drained on the game thread so the
 * D24 coalesce and the drain order stay single-producer. So graphics runs
 * deliver in two stages joined by a second FIFO owned here:
 *
 *   GAME thread   stagePendingEvents():  drainEvents() [base queue,
 *                 D24-coalesced] -> append the batch, in order, to
 *                 m_forwardQueue.
 *   RENDER thread forwardStagedEvents(): swap out m_forwardQueue,
 *                 re-run the D24 coalesce, m_wrapped.applyEvent() each.
 *
 * Both queues are single-producer FIFOs, so the renderer sees events in
 * exactly the order they were pushed. Headless runs and single-threaded
 * test harnesses use applyEvent() (forward on the calling thread) via
 * EverQuest::ProcessBridgeEvents(); do not call that from a graphics run.
 */
class GameStateSyncBridge : public GameStateBridge {
public:
    explicit GameStateSyncBridge(GameStateBridge& wrapped) : m_wrapped(wrapped) {}
    ~GameStateSyncBridge() override = default;

    /** Single-stage: forward to the wrapped bridge on the calling thread. */
    void applyEvent(const state::GameEvent& event) override;

    /** GAME thread (graphics runs): stage 1 -- drain the base queue, stage. */
    void stagePendingEvents();

    /** RENDER thread (graphics runs): stage 2 -- forward to the wrapped bridge. */
    void forwardStagedEvents();

    /**
     * B945: bytes the render-side queue's buffer still holds
     * (capacity * sizeof(state::GameEvent)), for the capacity-bounded test.
     * Thread-safe: reads under the same mutex the two stages do.
     */
    size_t forwardQueueCapacityBytes() const;

private:
    GameStateBridge& m_wrapped;

    // Staged-but-not-yet-forwarded events, game thread -> render thread.
    // Same swap-vector pattern as the base class's queues.
    //
    // B945: the capacity is not a fixed cap -- a burst (the zone-in spawn
    // burst, held here while the render thread is busy loading the zone) may
    // grow it -- but it is RETURNED once the burst has drained, exactly as
    // the base class's eventQueue_ (same halving rule, see drainEvents()).
    // Before the fix this queue kept whatever its burst-sized allocation
    // grew to for the whole session (timorous: a 3.1 MB block alive at
    // ready, B941's trace) even though a live frame holds at most a few
    // hundred events.
    std::mutex m_forwardMutex;
    std::vector<state::GameEvent> m_forwardQueue;
    // B945: high-water mark of m_forwardQueue's live capacity (read right
    // after each stage's append, under m_forwardMutex). forwardStagedEvents
    // gives the buffer back once a drain leaves the queue under half of it.
    size_t m_forwardHighWater = 0;
};

} // namespace bridge
} // namespace eqt
