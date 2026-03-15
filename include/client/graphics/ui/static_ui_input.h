/*
 * StaticUIInput — standalone input handler for the new static layout UI.
 *
 * U06j: Handles mouse hit-testing against UILayout regions, click actions
 * on slots/buttons, popup toggles, and chat focus. Does NOT depend on
 * WindowManager or any old UI code.
 *
 * Called per-frame from IrrlichtRenderer when newUIEnabled_ is true.
 * Pushes intents to the bridge for game actions (cast spell, activate
 * hotbar, etc.)
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace EQT {
namespace Graphics {

struct UILayout;
struct InventoryPanelState;
struct HotbarPanelState;
struct SpellGemPanelState;
struct SpellbookPopupState;
struct SkillsPopupState;

class StaticUIInput {
public:
    StaticUIInput() = default;
    ~StaticUIInput() = default;

    void init(eqt::bridge::GameStateBridge* bridge) { bridge_ = bridge; }

    /**
     * Process input for the current frame.
     * Updates hover states and handles clicks.
     * Returns true if input was consumed by the UI (don't pass to 3D scene).
     */
    bool processInput(int mouseX, int mouseY, bool leftClicked,
                      const UILayout& layout,
                      HotbarPanelState& hotbar,
                      SpellGemPanelState& spellGems,
                      InventoryPanelState& inventory,
                      SpellbookPopupState& spellbook,
                      SkillsPopupState& skills);

    /**
     * Handle a key press. Returns true if consumed.
     */
    bool processKey(irr::EKEY_CODE key, bool pressed, bool shift, bool ctrl,
                    InventoryPanelState& inventory,
                    SpellbookPopupState& spellbook,
                    SkillsPopupState& skills);

private:
    eqt::bridge::GameStateBridge* bridge_ = nullptr;

    // Hit test a point against a slot grid and return slot index, or -1
    int hitTestSlotRow(int mx, int my, int startX, int startY,
                       int slotSize, int padding, int count) const;
    int hitTestSlotGrid(int mx, int my, int startX, int startY,
                        int slotSize, int padding, int cols, int rows) const;
};

} // namespace Graphics
} // namespace EQT
