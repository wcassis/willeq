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

private:
    GameStateBridge& m_wrapped;

    // Staged-but-not-yet-forwarded events, game thread -> render thread.
    // Same swap-vector pattern as the base class's queues.
    std::mutex m_forwardMutex;
    std::vector<state::GameEvent> m_forwardQueue;
};

} // namespace bridge
} // namespace eqt
