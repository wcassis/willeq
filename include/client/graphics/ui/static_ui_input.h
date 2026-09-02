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
#include <string>

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace EQT {
namespace Graphics {

struct UILayout;
struct InventoryPanelState;
struct HotbarPanelState;
struct SpellGemPanelState;
struct SpellbookPopupState;
struct SkillsPopupState;
struct BuffBarState;
struct LootPanelState;
struct ChatPanelState;

/**
 * T313 -- the three panels that carry a scrollOffset.
 *
 * Chat, skills and loot each honoured a scrollOffset in BOTH their render
 * body and their hit test, and nothing anywhere ever wrote one: there was
 * no wheel handler in the static UI at all and no page-key binding, so
 * chat scrollback was unreachable, the skills window showed only its first
 * page, and a corpse with more than LootPanelState::VISIBLE_ROWS items was
 * partly UNLOOTABLE. StaticUIInput::scroll*() below is the missing writer.
 * The clamps were already there and are the semantics; nothing here
 * invents a second scrolling model.
 */
enum class ScrollTarget : uint8_t {
    None = 0,
    Chat,
    Skills,
    Loot
};

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
                      SkillsPopupState& skills,
                      const BuffBarState& buffs,
                      LootPanelState& loot);

    /**
     * Handle a key press. Returns true if consumed.
     */
    bool processKey(irr::EKEY_CODE key, bool pressed, bool shift, bool ctrl,
                    InventoryPanelState& inventory,
                    SpellbookPopupState& spellbook,
                    SkillsPopupState& skills,
                    LootPanelState& loot);

    // --- T313: the one scroll input path -------------------------------
    //
    // ONE router, not three bespoke handlers. Every entry point below ends
    // up in scrollPanel(), which is the only code in the tree that writes a
    // scrollOffset by intent (the panels' own clamps and resets stay where
    // they are). The wheel, the PageUp/PageDown keys and the `/scroll`
    // command differ only in how they pick the target and the step.

    /**
     * Which scrollable panel is under the cursor, in the same top-down
     * order the panels are DRAWN and the same order processKey() dismisses
     * them: the loot popup is on top, then the skills popup (both live in
     * layout.centerPopup and are never open together), then the chat panel.
     * A closed popup is not a target even when the cursor is over where it
     * would be.
     */
    static ScrollTarget scrollTargetAt(int mouseX, int mouseY,
                                       const UILayout& layout,
                                       const SkillsPopupState& skills,
                                       const LootPanelState& loot);

    /**
     * Move `target`'s scrollOffset by `lines`, clamped.
     *
     * `lines > 0` always means "towards OLDER / EARLIER content" -- what a
     * wheel-up or a PageUp does -- regardless of which end the panel counts
     * from. Chat measures its offset from the BOTTOM (0 = pinned to the
     * newest line), so up is +; the skills and loot lists measure theirs
     * from the TOP, so up is -. The two conventions are the panels' own and
     * are deliberately NOT harmonised here.
     *
     * Returns true if a target was addressed (even if it was already at the
     * end of its travel), false for ScrollTarget::None.
     */
    static bool scrollPanel(ScrollTarget target, int lines,
                            const UILayout& layout,
                            ChatPanelState& chat,
                            SkillsPopupState& skills,
                            LootPanelState& loot);

    /**
     * A mouse wheel notch over the UI. `wheelDelta` is Irrlicht's
     * SEvent::SMouseInput::Wheel (positive = away from the user = up),
     * accumulated across the frame. Returns true if the UI consumed it --
     * the caller must not then feed the same wheel to the camera.
     */
    bool processWheel(int mouseX, int mouseY, float wheelDelta,
                      const UILayout& layout,
                      ChatPanelState& chat,
                      SkillsPopupState& skills,
                      LootPanelState& loot);

    /**
     * PageUp / PageDown. Same routing as the wheel, a whole page at a time.
     * Returns true if consumed.
     */
    bool processScrollKey(bool up, int mouseX, int mouseY,
                          const UILayout& layout,
                          ChatPanelState& chat,
                          SkillsPopupState& skills,
                          LootPanelState& loot);

    /** One page of `target`, in lines. */
    static int pageSize(ScrollTarget target, const UILayout& layout);

    /**
     * The `/scroll` command body: parse `args`, route, scroll, and return
     * the line to echo. Lives here rather than in the renderer's slash
     * switch so the parsing sits beside the router it drives (and out of
     * irrlicht_renderer.cpp, which is already over the file-size ceiling).
     *
     * Grammar: [chat|skills|loot] [up|down|top|bottom] [lines]
     * An omitted panel means "whatever is under the cursor", the same rule
     * the wheel follows.
     */
    static std::string runScrollCommand(const std::string& args,
                                        int mouseX, int mouseY,
                                        const UILayout& layout,
                                        ChatPanelState& chat,
                                        SkillsPopupState& skills,
                                        LootPanelState& loot);

    /** Lines moved per wheel notch. */
    static constexpr int WHEEL_LINES = 3;

private:
    eqt::bridge::GameStateBridge* bridge_ = nullptr;

    // Hit test a point against a horizontal slot row and return slot index,
    // or -1. Used by the hotbar; the inventory popup's grid geometry comes
    // from InventoryLayout::slotRect() instead (T57 — single shared source).
    int hitTestSlotRow(int mx, int my, int startX, int startY,
                       int slotSize, int padding, int count) const;
};

} // namespace Graphics
} // namespace EQT
