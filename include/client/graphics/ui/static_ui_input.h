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

#include "client/graphics/ui/options_layout.h"

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
struct TradeskillPanelState;
struct CombatAbilityPanelState;
struct ItemDisplayPanelState;
struct OptionsPanelState;

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
    Loot,
    // T421 (board B298, item half): IDW_ItemDescription is `Style_VScroll
    // true` and a loaded item composes more lines than the oracle's 190px
    // window shows. Added here rather than given a private scroll writer,
    // because T313's rule is that there is ONE scroll router.
    ItemDisplay
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
                      LootPanelState& loot,
                      // T425 (board B298, spell half): SHIFT is a MODIFIER on
                      // the two clicks that already had a job -- a gem click
                      // casts (T272) and a spellbook click memorises -- so it
                      // cannot be its own entry point the way T415's and
                      // T421's are: the plain arms would fire first. It is
                      // appended with a default instead, which leaves every
                      // existing caller and every existing test compiling and
                      // asserting exactly what they did before.
                      //
                      // DEVIATION from the oracle, flagged: the official
                      // client opens the spell display on a RIGHT click.
                      // fable binds the right button to mouselook and its
                      // static UI latches no right click (T421 recorded the
                      // same wall); unlike T421's inventory slot, which did
                      // NOTHING before, left is already taken here, so the
                      // free gesture is shift+left.
                      bool shift = false);

    /**
     * T415 (boards B321/B320): the two panels added after processInput()'s
     * parameter list was already ten wide, kept as their own entry point
     * rather than growing it to twelve -- every existing caller (three tests
     * and renderStaticUI) is untouched.
     *
     * Hover + click for the tradeskill container window and the combat
     * abilities window. Returns true if the UI consumed the position.
     *
     * The intents are the ones that already existed and had exactly one
     * pusher each: TradeskillCombineIntent / TradeskillCloseIntent (the
     * `/combine` and `/closecontainer` commands, T278) and
     * SkillActivateIntent. Nothing new crosses the bridge.
     */
    bool processPanelInput(int mouseX, int mouseY, bool leftClicked,
                           const UILayout& layout,
                           TradeskillPanelState& tradeskill,
                           CombatAbilityPanelState& combatAbilities);

    /**
     * T421 (board B298, item half): the item display window.
     *
     * Its own entry point for the same reason processPanelInput() is: the
     * window is not one of the mutually-exclusive centre popups (the oracle
     * places it at its own absolute (0,80), `RelativePosition false`), so it
     * is drawn and hit-tested on top of them and every existing caller of
     * the two entry points above stays untouched.
     *
     * The only click it takes is Style_Closebox; the rest of the window is
     * consumed so a click on it does not fall through to the 3D scene.
     */
    bool processItemDisplayInput(int mouseX, int mouseY, bool leftClicked,
                                 const UILayout& layout,
                                 ItemDisplayPanelState& itemDisplay);

    /**
     * T434: the Options window.
     *
     * Its own entry point for the same reason processPanelInput() and
     * processItemDisplayInput() are: the oracle's OptionsWindow is
     * `RelativePosition false` at its own <Location>, not one of the
     * mutually-exclusive centre popups, so it is drawn and hit-tested on
     * top of them and no existing caller changes.
     *
     * It pushes NO intent. Two of the three wired controls need one
     * (MusicVolumeChangeIntent / EffectsVolumeChangeIntent) and the third
     * needs a renderer call, and all three need the DELTA against what the
     * audio manager was last told -- which is FrameComponent's bookkeeping,
     * not this class's. So the click is resolved here into "this setting
     * moved" and `changed` names it; FrameComponent::applyOptionsSetting()
     * is the one place that acts on it. `changed` is set to
     * OptionsLayout::Setting::None when nothing moved.
     *
     * The whole window consumes the position so a click on it cannot fall
     * through to the 3D scene, but only an ENABLED control is hit-tested --
     * see hitTestOptionsControl().
     */
    bool processOptionsInput(int mouseX, int mouseY, bool leftClicked,
                             const UILayout& layout,
                             OptionsPanelState& options,
                             OptionsLayout::Setting& changed);

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
                                       const LootPanelState& loot,
                                       const ItemDisplayPanelState& itemDisplay);

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
                            LootPanelState& loot,
                            ItemDisplayPanelState& itemDisplay);

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
                      LootPanelState& loot,
                      ItemDisplayPanelState& itemDisplay);

    /**
     * PageUp / PageDown. Same routing as the wheel, a whole page at a time.
     * Returns true if consumed.
     */
    bool processScrollKey(bool up, int mouseX, int mouseY,
                          const UILayout& layout,
                          ChatPanelState& chat,
                          SkillsPopupState& skills,
                          LootPanelState& loot,
                          ItemDisplayPanelState& itemDisplay);

    /** One page of `target`, in lines. */
    static int pageSize(ScrollTarget target, const UILayout& layout);

    /**
     * The `/scroll` command body: parse `args`, route, scroll, and return
     * the line to echo. Lives here rather than in the renderer's slash
     * switch so the parsing sits beside the router it drives (and out of
     * irrlicht_renderer.cpp, which was over the file-size ceiling at the
     * time; J11 has since split it).
     *
     * Grammar: [chat|skills|loot|item] [up|down|top|bottom] [lines]
     * An omitted panel means "whatever is under the cursor", the same rule
     * the wheel follows.
     */
    static std::string runScrollCommand(const std::string& args,
                                        int mouseX, int mouseY,
                                        const UILayout& layout,
                                        ChatPanelState& chat,
                                        SkillsPopupState& skills,
                                        LootPanelState& loot,
                                        ItemDisplayPanelState& itemDisplay);

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
