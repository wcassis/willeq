// T235 -- spell-gem hotkeys (Alt+1..8) must reach the game thread.
//
// RendererEventReceiver::getSpellGemCastRequest() is a destructive
// exchange(-1). It had TWO callers: IrrlichtRenderer::processChatInput()
// on the render thread (every frame; the value went to a callback that is
// never wired live -- review/T235/ BEFORE log shows "action=SpellGem1" and
// nothing else) and GraphicsInputHandler::consumeSpellCastRequest() on the
// game thread (InputActionBridge::processSpellCasts -> castSpell). The
// render thread polls far more often, so it won the race and every gem
// key was swallowed -- the exact shape of the T230 hotbar defect
// (review/T230/README.md, "two-consumer race").
//
// This drives the REAL receiver (OnEvent with Alt held + KEY_1, the
// default SpellGem1 binding from HotkeyManager::setupDefaults) and the
// REAL render-thread step (processChatInput on a bare, never-Init()'d
// IrrlichtRenderer, the T66/T233 idiom -- the chat-input branch touches
// no device state when nothing is focused), then asks the game-thread
// consumer whether the request is still there.
//
// Red (pre-fix): consumeSpellCastRequest() returns nullopt after one
// processChatInput() -- the render thread ate it.

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include "client/graphics/irrlicht_renderer.h"
#include "client/input/graphics_input_handler.h"
#include "client/input/hotkey_manager.h"

// IrrlichtRenderer's friend declarations name these classes unqualified,
// so they resolve against EQT::Graphics -- the whole file lives there
// (the T116 idiom, tests/test_simulation_output_lifetime.cpp).
namespace EQT {
namespace Graphics {

irr::SEvent KeyDown(irr::EKEY_CODE key, wchar_t ch) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_KEY_INPUT_EVENT;
    ev.KeyInput.Key = key;
    ev.KeyInput.Char = ch;
    ev.KeyInput.PressedDown = true;
    ev.KeyInput.Shift = false;
    ev.KeyInput.Control = false;
    return ev;
}

// Alt+<digit> exactly as the X11 device delivers it: LMENU down first
// (the receiver tracks Alt via keyIsDown_, Irrlicht's KeyInput has no
// Alt flag), then the digit.
void PressAltDigit(RendererEventReceiver& rx, irr::EKEY_CODE digit, wchar_t ch) {
    rx.OnEvent(KeyDown(irr::KEY_LMENU, 0));
    rx.OnEvent(KeyDown(digit, ch));
}

class SpellGemKeyRaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Bare renderer, no device: install a real receiver where Init()
        // would have.
        renderer.eventReceiver_ = std::make_unique<RendererEventReceiver>();
        receiver = renderer.eventReceiver_.get();
        eqt::input::HotkeyManager::instance().resetToDefaults();
    }

    IrrlichtRenderer renderer;
    RendererEventReceiver* receiver = nullptr;
};

// The defect: press Alt+1, let the render thread run its frame step, then
// the game thread's consumer must still see gem 0.
TEST_F(SpellGemKeyRaceTest, GemKeySurvivesRenderFrameForGameThread) {
    PressAltDigit(*receiver, irr::KEY_KEY_1, L'1');

    // Render thread: one frame's worth of processChatInput().
    renderer.Frame().processChatInput();

    // Game thread: InputActionBridge::processSpellCasts() polls this.
    eqt::input::GraphicsInputHandler handler(receiver);
    auto req = handler.consumeSpellCastRequest();
    ASSERT_TRUE(req.has_value()) << "render-thread processChatInput() consumed the spell-gem request";
    EXPECT_EQ(req->gemSlot, 0u);

    // And it is a one-shot: a second poll is empty.
    EXPECT_FALSE(handler.consumeSpellCastRequest().has_value());
}

// Several frames may elapse between the key and the game tick; every gem
// key (1..8) must survive an arbitrary number of render frames, and the
// game thread must see exactly the pressed gem.
TEST_F(SpellGemKeyRaceTest, RenderFrameWithChatUnfocusedLeavesRequestUntouched) {
    const irr::EKEY_CODE keys[] = {
        irr::KEY_KEY_1, irr::KEY_KEY_2, irr::KEY_KEY_3, irr::KEY_KEY_4,
        irr::KEY_KEY_5, irr::KEY_KEY_6, irr::KEY_KEY_7, irr::KEY_KEY_8};
    eqt::input::GraphicsInputHandler handler(receiver);

    for (int gem = 0; gem < 8; ++gem) {
        PressAltDigit(*receiver, keys[gem], static_cast<wchar_t>(L'1' + gem));
        for (int frame = 0; frame < 5; ++frame) {
            renderer.Frame().processChatInput();
        }
        auto req = handler.consumeSpellCastRequest();
        ASSERT_TRUE(req.has_value()) << "gem " << gem + 1 << " swallowed by the render thread";
        EXPECT_EQ(req->gemSlot, static_cast<uint8_t>(gem));
    }
}

}  // namespace Graphics
}  // namespace EQT
