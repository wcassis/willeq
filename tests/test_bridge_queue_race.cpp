// T312 -- RendererEventReceiver's cross-thread state.
//
// Three defects, all in the same object, all reachable only from a real
// keyboard session (a --script run injects no Irrlicht input, which is why
// the suite and every prior TSAN tour were blind to them):
//
//  1. bridgeQueue_ was a plain std::vector<RendererEvent> with push_back on
//     the RENDER thread (OnEvent's decoded hotkeys; renderer_frame.cpp's
//     pushBridgeAction) and swap on the GAME thread
//     (GraphicsInputHandler::drainBridgeActions). A reallocating push_back
//     racing the swap is HEAP CORRUPTION, not a wrong value.
//     getBridgeQueueSize() -- a log line -- read size() unsynchronised on
//     top of that.
//  2. keyIsDown_[] / keyWasPressed_[] were plain bool arrays written in
//     OnEvent (render) and read by isKeyDown()/wasKeyPressed() (both
//     threads).
//  3. getMouseDeltaX/Y() is a destructive read-and-reset with TWO callers,
//     one per thread -- whichever ran first STOLE the delta from the other.
//
// This file is BOTH the ThreadSanitizer driver for 1 and 2 (build a
// -fsanitize=thread tree per the P23/T35 precedent: BEFORE the fix it
// reports races in irrlicht_renderer.h / .cpp, AFTER it must report none)
// and a plain unit test that fails without a sanitizer: the drain
// accounting in test 1 loses events on the racy vector, and test 3 is
// fully deterministic -- pre-fix the game-thread poll swallows the
// mouse-look delta every single time.
//
// No graphics device is needed: the receiver is a bare irr::IEventReceiver
// and GraphicsInputHandler only ever talks to it (the T66/T233/T235
// no-device idiom).

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/input/graphics_input_handler.h"
#include "client/input/hotkey_manager.h"

namespace EQT {
namespace Graphics {
namespace {

irr::SEvent KeyEvt(irr::EKEY_CODE key, bool down) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_KEY_INPUT_EVENT;
    ev.KeyInput.Key = key;
    ev.KeyInput.Char = 0;
    ev.KeyInput.PressedDown = down;
    ev.KeyInput.Shift = false;
    ev.KeyInput.Control = false;
    return ev;
}

irr::SEvent MouseMove(int x, int y) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_MOUSE_INPUT_EVENT;
    ev.MouseInput.Event = irr::EMIE_MOUSE_MOVED;
    ev.MouseInput.X = x;
    ev.MouseInput.Y = y;
    ev.MouseInput.ButtonStates = 0;
    return ev;
}

}  // namespace

// --- 1. bridgeQueue_: the heap corruptor -------------------------------
//
// Render thread pushes (both production writers' shapes: the OnEvent
// hotkey path and pushBridgeAction) and reads getBridgeQueueSize(); game
// thread swap-drains. Every pushed action must come back out exactly once.
TEST(BridgeQueueRaceTest, EveryPushedActionIsDrainedExactlyOnce) {
    constexpr int kPushes = 50000;

    RendererEventReceiver receiver;
    std::atomic<bool> stop{false};
    std::atomic<long> drained{0};

    // Game thread: GraphicsInputHandler::updateFromEventReceiver()'s drain.
    std::thread game([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            drained += static_cast<long>(receiver.drainBridgeActions().size());
        }
    });

    // Render thread (this one): the two production writers plus the log
    // line that reads size() while the game thread is swapping.
    for (int i = 0; i < kPushes; ++i) {
        receiver.pushBridgeAction({RendererAction::Hail});
        (void)receiver.getBridgeQueueSize();
    }

    stop.store(true, std::memory_order_relaxed);
    game.join();
    drained += static_cast<long>(receiver.drainBridgeActions().size());

    EXPECT_EQ(drained.load(), kPushes);
}

// --- 2. keyIsDown_ / keyWasPressed_ ------------------------------------
//
// TSAN driver: OnEvent writes the arrays on the render thread while the
// game thread polls them. The assertion is the one thing a non-sanitizer
// run can still check: a one-shot press must never be observed twice
// (wasKeyPressed is a read-and-reset), so the observed count can never
// exceed the number of presses.
TEST(BridgeQueueRaceTest, KeyStateSurvivesConcurrentPolling) {
    constexpr int kPresses = 50000;

    eqt::input::HotkeyManager::instance().resetToDefaults();

    RendererEventReceiver receiver;
    std::atomic<bool> stop{false};
    std::atomic<long> observedPresses{0};

    std::thread game([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)receiver.isKeyDown(irr::KEY_LSHIFT);
            if (receiver.wasKeyPressed(irr::KEY_SPACE)) {
                ++observedPresses;
            }
        }
    });

    for (int i = 0; i < kPresses; ++i) {
        receiver.OnEvent(KeyEvt(irr::KEY_SPACE, true));
        receiver.OnEvent(KeyEvt(irr::KEY_SPACE, false));
        receiver.OnEvent(KeyEvt(irr::KEY_LSHIFT, true));
        receiver.OnEvent(KeyEvt(irr::KEY_LSHIFT, false));
        // OnEvent also appends to the render-thread-only chat buffer;
        // keep it bounded (this is the render thread, same as
        // processChatInput()).
        if ((i & 0xFF) == 0) receiver.clearPendingKeyEvents();
    }

    stop.store(true, std::memory_order_relaxed);
    game.join();
    if (receiver.wasKeyPressed(irr::KEY_SPACE)) ++observedPresses;

    EXPECT_LE(observedPresses.load(), kPresses);
}

// --- 3. the mouse-delta double consumer --------------------------------
//
// Deterministic, no threads needed. getMouseDeltaX/Y() is destructive.
// The real consumer is the mouse-look in
// IrrlichtRenderer::updatePlayerMovement() (renderer_player_physics.cpp).
// GraphicsInputHandler also called it, storing the result in InputState --
// a struct with no reader anywhere in the tree. So the game-thread poll
// existed only to steal the delta: whenever it landed between two frames,
// the camera simply did not move.
TEST(MouseDeltaOwnershipTest, GameThreadPollDoesNotStealMouseLookDelta) {
    eqt::input::HotkeyManager::instance().resetToDefaults();

    RendererEventReceiver receiver;
    // Baseline the accumulator (constructed at 0,0).
    (void)receiver.getMouseDeltaX();
    (void)receiver.getMouseDeltaY();

    receiver.OnEvent(MouseMove(100, 50));

    // Game thread poll, exactly as GraphicsInputHandler::update() does it.
    eqt::input::GraphicsInputHandler handler(&receiver);
    handler.update();

    // Render thread mouse-look must still see the whole movement.
    EXPECT_EQ(receiver.getMouseDeltaX(), 100);
    EXPECT_EQ(receiver.getMouseDeltaY(), 50);
}

}  // namespace Graphics
}  // namespace EQT
