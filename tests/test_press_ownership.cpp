// B1066 (GitLab #386): a mouse press is owned by the static UI OR the camera,
// never both. Will, 2026-10-14 XU4 play test: "right clicking on an inventory
// item is causing the view to change in response as though it's a mouse look
// event. the item inspect should consume the event."
//
// Three layers:
//   1. PressOwnership itself (press_ownership.h): pending -> UI / camera by
//      where the press landed, release clears, an owned press never moves.
//   2. RendererEventReceiver over real Irrlicht mouse events: the LOOK view
//      (isLeft/RightButtonDown(), what renderer_player_physics.cpp's mouse-look
//      reads) is false for a UI-owned press while the PHYSICAL hold
//      (isRightButtonHeld(), what the item display's lifetime reads) is true;
//      a view press looks and keeps looking when the pointer crosses a window;
//      the right press restarts the look baseline.
//   3. Source pins: the consumers read the right view, and the frame settles
//      the pending press from the static UI's hit test.
//
// No device, no server.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include <irrlicht.h>

#include "client/graphics/press_ownership.h"
#include "client/graphics/renderer_event_receiver.h"

using EQT::Graphics::PressButton;
using EQT::Graphics::PressOwner;
using EQT::Graphics::PressOwnership;
using EQT::Graphics::RendererEventReceiver;

namespace {

// ---- 1. the ownership logic -------------------------------------------------

TEST(PressOwnership, NewPressIsPendingAndNeitherOwnersUntilResolved) {
    PressOwnership p;
    for (PressButton b : {PressButton::Left, PressButton::Right}) {
        EXPECT_EQ(p.owner(b), PressOwner::None);
        p.press(b);
        EXPECT_EQ(p.owner(b), PressOwner::Pending);
        EXPECT_TRUE(p.held(b));
        EXPECT_FALSE(p.cameraHeld(b)) << "a pending press must not look";
    }
}

TEST(PressOwnership, PressOnUiIsIgnoredByLookUntilRelease) {
    for (PressButton b : {PressButton::Left, PressButton::Right}) {
        PressOwnership p;
        p.press(b);
        EXPECT_TRUE(p.resolve(/*pointerOnUi=*/true));
        EXPECT_EQ(p.owner(b), PressOwner::Ui);
        EXPECT_TRUE(p.held(b));
        EXPECT_FALSE(p.cameraHeld(b));
        // The pointer leaves the window while the button is still down: the
        // press stays the UI's -- look never starts mid-gesture.
        EXPECT_FALSE(p.resolve(/*pointerOnUi=*/false));
        EXPECT_EQ(p.owner(b), PressOwner::Ui);
        EXPECT_FALSE(p.cameraHeld(b));
        p.release(b);
        EXPECT_EQ(p.owner(b), PressOwner::None);
        EXPECT_FALSE(p.held(b));
    }
}

TEST(PressOwnership, PressOnViewLooksAndKeepsLookingAcrossAWindow) {
    for (PressButton b : {PressButton::Left, PressButton::Right}) {
        PressOwnership p;
        p.press(b);
        EXPECT_TRUE(p.resolve(/*pointerOnUi=*/false));
        EXPECT_TRUE(p.cameraHeld(b));
        // The drag crosses a window: the owner decides, not the position.
        EXPECT_FALSE(p.resolve(/*pointerOnUi=*/true));
        EXPECT_TRUE(p.cameraHeld(b));
        p.release(b);
        EXPECT_FALSE(p.cameraHeld(b));
        EXPECT_FALSE(p.held(b));
    }
}

TEST(PressOwnership, ReleaseClearsAndTheNextPressIsDecidedAfresh) {
    PressOwnership p;
    p.press(PressButton::Right);
    p.resolve(true);
    p.release(PressButton::Right);
    p.press(PressButton::Right);
    EXPECT_EQ(p.owner(PressButton::Right), PressOwner::Pending);
    p.resolve(false);
    EXPECT_TRUE(p.cameraHeld(PressButton::Right));
}

TEST(PressOwnership, ButtonsAreIndependent) {
    PressOwnership p;
    p.press(PressButton::Left);
    p.resolve(false);                    // left: camera
    p.press(PressButton::Right);
    p.resolve(true);                     // right: UI; left keeps its owner
    EXPECT_TRUE(p.cameraHeld(PressButton::Left));
    EXPECT_EQ(p.owner(PressButton::Right), PressOwner::Ui);
    p.release(PressButton::Left);
    EXPECT_EQ(p.owner(PressButton::Right), PressOwner::Ui);
}

TEST(PressOwnership, HeldSeenStartsAPressOnlyWhenNoneIsHeld) {
    PressOwnership p;
    p.heldSeen(PressButton::Right);
    EXPECT_EQ(p.owner(PressButton::Right), PressOwner::Pending);
    p.resolve(true);
    p.heldSeen(PressButton::Right);      // a move re-reporting the hold
    EXPECT_EQ(p.owner(PressButton::Right), PressOwner::Ui) << "an owned press keeps its owner";
}

TEST(PressOwnership, ResolveWithNothingPendingIsANoOp) {
    PressOwnership p;
    EXPECT_FALSE(p.resolve(true));
    EXPECT_FALSE(p.resolve(false));
    EXPECT_EQ(p.owner(PressButton::Left), PressOwner::None);
}

// ---- 2. the receiver over real Irrlicht mouse events --------------------

irr::SEvent mouse(irr::EMOUSE_INPUT_EVENT kind, int x, int y, irr::u32 buttons = 0) {
    irr::SEvent e{};
    e.EventType = irr::EET_MOUSE_INPUT_EVENT;
    e.MouseInput.Event = kind;
    e.MouseInput.X = x;
    e.MouseInput.Y = y;
    e.MouseInput.Wheel = 0.0f;
    e.MouseInput.ButtonStates = buttons;
    return e;
}

TEST(PressOwnershipReceiver, RightPressOnAnItemNeverReachesTheLookButHoldsTheDisplay) {
    RendererEventReceiver rx;
    rx.OnEvent(mouse(irr::EMIE_RMOUSE_PRESSED_DOWN, 300, 200, irr::EMBSM_RIGHT));
    EXPECT_TRUE(rx.isRightButtonHeld());
    EXPECT_FALSE(rx.isRightButtonDown()) << "pending: no look before the hit test";
    EXPECT_TRUE(rx.wasRightButtonClicked()) << "the click latch the item windows consume";
    rx.resolvePendingPresses(/*pointerOnUi=*/true);       // the press landed on a slot
    // The hand moves 20 px while the button is down (the #386 report).
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 320, 205, irr::EMBSM_RIGHT));
    EXPECT_FALSE(rx.isRightButtonDown()) << "mouse-look must ignore a UI-owned press";
    EXPECT_TRUE(rx.isRightButtonHeld()) << "the item display is held open by the physical hold";
    // Owned, not pending: a later frame with the pointer off every window
    // does not hand it to the camera.
    rx.resolvePendingPresses(/*pointerOnUi=*/false);
    EXPECT_FALSE(rx.isRightButtonDown());
    rx.OnEvent(mouse(irr::EMIE_RMOUSE_LEFT_UP, 320, 205));
    EXPECT_FALSE(rx.isRightButtonHeld());
}

TEST(PressOwnershipReceiver, LeftPressOnAWindowNeverReachesTheLook) {
    RendererEventReceiver rx;
    rx.OnEvent(mouse(irr::EMIE_LMOUSE_PRESSED_DOWN, 50, 400, irr::EMBSM_LEFT));
    rx.resolvePendingPresses(true);
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 90, 380, irr::EMBSM_LEFT));
    EXPECT_FALSE(rx.isLeftButtonDown());
    EXPECT_TRUE(rx.isLeftButtonHeld());
    rx.OnEvent(mouse(irr::EMIE_LMOUSE_LEFT_UP, 90, 380));
    EXPECT_FALSE(rx.isLeftButtonHeld());
}

TEST(PressOwnershipReceiver, RightDragOnTheViewLooksAndCrossesAWindow) {
    RendererEventReceiver rx;
    // The pointer wandered before the press: none of that travel may count.
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 10, 10));
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 400, 300));
    rx.OnEvent(mouse(irr::EMIE_RMOUSE_PRESSED_DOWN, 400, 300, irr::EMBSM_RIGHT));
    // Motion while still pending is kept for the first look frame.
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 405, 300, irr::EMBSM_RIGHT));
    rx.resolvePendingPresses(/*pointerOnUi=*/false);
    ASSERT_TRUE(rx.isRightButtonDown());
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 420, 296, irr::EMBSM_RIGHT));
    EXPECT_EQ(rx.getMouseDeltaX(), 20) << "baseline is the press, not the pre-press travel";
    EXPECT_EQ(rx.getMouseDeltaY(), -4);
    // Now over a window: still the camera's.
    rx.resolvePendingPresses(/*pointerOnUi=*/true);
    EXPECT_TRUE(rx.isRightButtonDown());
    rx.OnEvent(mouse(irr::EMIE_RMOUSE_LEFT_UP, 420, 296));
    EXPECT_FALSE(rx.isRightButtonDown());
    EXPECT_FALSE(rx.isRightButtonHeld());
}

TEST(PressOwnershipReceiver, AMoveReportingAHoldWithNoPressStartsAPendingPress) {
    RendererEventReceiver rx;
    rx.OnEvent(mouse(irr::EMIE_MOUSE_MOVED, 100, 100, irr::EMBSM_RIGHT | irr::EMBSM_LEFT));
    EXPECT_TRUE(rx.isRightButtonHeld());
    EXPECT_TRUE(rx.isLeftButtonHeld());
    EXPECT_FALSE(rx.isRightButtonDown()) << "pending until the frame settles it";
    EXPECT_TRUE(rx.resolvePendingPresses(/*pointerOnUi=*/false));
    EXPECT_TRUE(rx.isRightButtonDown());
    EXPECT_TRUE(rx.isLeftButtonDown());
}

// ---- 3. source pins ------------------------------------------------------

std::string slurp(const std::string& rel) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST(PressOwnershipWiring, TheItemDisplayReadsThePhysicalHold) {
    const std::string src = slurp("src/client/graphics/frame/renderer_frame_cursor.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("updateHeldItemDisplay(itemDisplayState_, ctx_.eventReceiver->isRightButtonHeld())"),
              std::string::npos);
    EXPECT_EQ(src.find("isRightButtonDown()"), std::string::npos)
        << "the item display must not read the camera-owned look view";
}

TEST(PressOwnershipWiring, TheFrameSettlesPendingPressesFromTheHitTest) {
    const std::string cursor = slurp("src/client/graphics/frame/renderer_frame_cursor.cpp");
    EXPECT_NE(cursor.find("resolvePendingPresses(pointerOwnedByUI("), std::string::npos);
    const std::string input = slurp("src/client/graphics/frame/renderer_frame_input.cpp");
    const auto fallback = input.find("resolvePendingPresses(false)");
    const auto movement = input.find("ctx_.PlayerCamera().updatePlayerMovement(deltaTime);");
    ASSERT_NE(fallback, std::string::npos) << "the no-static-UI path must still give presses to the camera";
    ASSERT_NE(movement, std::string::npos);
    EXPECT_LT(fallback, movement);
}

TEST(PressOwnershipWiring, MouseLookReadsOnlyTheCameraOwnedView) {
    const std::string src = slurp("src/client/graphics/player/renderer_player_physics.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("ctx_.eventReceiver->isRightButtonDown()"), std::string::npos);
    EXPECT_EQ(src.find("ButtonHeld()"), std::string::npos)
        << "mouse-look reading the physical hold is the #386 defect";
    const std::string rx = slurp("include/client/graphics/renderer_event_receiver.h");
    EXPECT_NE(rx.find("bool isRightButtonDown() const { return presses_.cameraHeld(PressButton::Right); }"),
              std::string::npos);
    EXPECT_NE(rx.find("bool isLeftButtonDown() const { return presses_.cameraHeld(PressButton::Left); }"),
              std::string::npos);
}

}  // namespace
