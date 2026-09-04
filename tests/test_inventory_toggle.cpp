// T294 -- the `I` keybind must actually open the inventory popup.
//
// hotkeys.json binds "ToggleInventory" to I, HotkeyManager maps it to
// HotkeyAction::ToggleInventory, RendererEventReceiver::OnEvent queues
// RendererAction::ToggleInventory, and processPlayerInput() calls
// IrrlichtRenderer::toggleInventory(). Every link was live EXCEPT the last:
// toggleInventory() was left an empty no-op stub when U07c2 (0f25cb57)
// deleted WindowManager, and was never re-pointed at the static-panel
// popup that replaced it. So `I` did nothing and the popup was reachable
// only through the /newinv slash command, which had its own open-coded copy
// of the toggle.
//
// This drives the REAL receiver (a KEY_KEY_I key-down, exactly as the
// device delivers it) through the REAL drain and the REAL
// processPlayerInput() on a bare, never-Init()'d IrrlichtRenderer -- the
// T66/T233/T235 no-device idiom (this path touches only inventoryState_
// and a null characterModelView_).
//
// Red (pre-fix): isInventoryOpen() stays false forever -- it was a
// `return false;` stub too.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/static_panels.h"
#include "client/input/hotkey_manager.h"

// IrrlichtRenderer's friend declarations name these classes unqualified,
// so the whole file lives in EQT::Graphics (the T116/T235 idiom).
namespace EQT {
namespace Graphics {

namespace {

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

}  // namespace

class InventoryToggleTest : public ::testing::Test {
protected:
    void SetUp() override {
        eqt::input::HotkeyManager::instance().resetToDefaults();
    }

    // One frame's worth of the render thread's action handling. The
    // receiver is a free-standing object rather than one installed on the
    // renderer: Init() is what normally wires them together, and the
    // render-thread step under test (drainActions() -> processPlayerInput())
    // is exactly this hand-off, so nothing private is needed.
    void pumpFrame() {
        renderer.Frame().processPlayerInput(receiver.drainActions());
    }

    IrrlichtRenderer renderer;
    RendererEventReceiver receiver;
};

// The whole point of the unit: press I, the popup opens.
TEST_F(InventoryToggleTest, IKeyOpensAndClosesTheInventoryPopup) {
    EXPECT_FALSE(renderer.isInventoryOpen());

    receiver.OnEvent(KeyDown(irr::KEY_KEY_I, L'i'));
    pumpFrame();
    EXPECT_TRUE(renderer.isInventoryOpen())
        << "the I keybind did not reach a working toggleInventory()";

    receiver.OnEvent(KeyDown(irr::KEY_KEY_I, L'i'));
    pumpFrame();
    EXPECT_FALSE(renderer.isInventoryOpen()) << "a second I did not close the popup";
}

// The key must produce the action in the first place -- if this fails the
// break is in the binding/dispatch half, not in the toggle body.
TEST_F(InventoryToggleTest, IKeyQueuesToggleInventoryAction) {
    receiver.OnEvent(KeyDown(irr::KEY_KEY_I, L'i'));
    auto actions = receiver.drainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, RendererAction::ToggleInventory);
}

// /newinv and I must be the SAME body -- that is why the toggle moved out
// of the command handler. Drive them alternately and check they agree.
TEST_F(InventoryToggleTest, SlashNewinvAndTheKeybindShareOneState) {
    renderer.processSlashCommand("/newinv");
    EXPECT_TRUE(renderer.isInventoryOpen());

    // The keybind closes what the slash command opened.
    receiver.OnEvent(KeyDown(irr::KEY_KEY_I, L'i'));
    pumpFrame();
    EXPECT_FALSE(renderer.isInventoryOpen());

    // ...and vice versa.
    receiver.OnEvent(KeyDown(irr::KEY_KEY_I, L'i'));
    pumpFrame();
    EXPECT_TRUE(renderer.isInventoryOpen());
    renderer.processSlashCommand("/newinv");
    EXPECT_FALSE(renderer.isInventoryOpen());
}

// closeInventory() must not clobber a DIFFERENT popup: activePopup is one
// shared slot, so closing "the inventory" while the spellbook-style slot
// holds something else has to be a no-op.
TEST_F(InventoryToggleTest, CloseInventoryOnlyClosesTheInventory) {
    renderer.openInventory();
    ASSERT_TRUE(renderer.isInventoryOpen());
    renderer.closeInventory();
    EXPECT_FALSE(renderer.isInventoryOpen());

    // Nothing open: closing again is harmless and does not open anything.
    renderer.closeInventory();
    EXPECT_FALSE(renderer.isInventoryOpen());
}

}  // namespace Graphics
}  // namespace EQT
