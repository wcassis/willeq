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
#include "client/graphics/ui/quantity_window.h"   // B442
#include "client/inventory/cursor_gesture.h"      // B442

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
// T436 (board B429).
struct MerchantPanelState;
struct BankPanelState;
struct CursorAttachmentState;
struct OptionsPanelState;
struct TrainerPanelState;  // B898
// B899 (#215 / #151 Leg B).
struct ContainerWindowsState;

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

    void init(eqt::bridge::GameStateBridge* bridge) {
        bridge_ = bridge;
        cursorGesture_.setBridge(bridge);
    }

    // --- B442 (#151): the click-to-cursor gesture --------------------------
    //
    // The per-frame click state the item-window arms need beyond the left
    // click every entry point already takes: the RIGHT click (latched by
    // RendererEventReceiver::wasRightButtonClicked(), consumed only by the
    // static UI -- a right click off every panel still mouselooks, because
    // mouselook reads isRightButtonDown(), not this latch), the modifiers,
    // and what the cursor holds. Set once per frame before the entry points
    // run; kept off their parameter lists so no existing caller changes.
    void setFrameClickState(bool rightClicked, eqt::inventory::ClickModifiers mods,
                            const CursorAttachmentState* cursor) {
        rightClicked_ = rightClicked;
        mods_ = mods;
        cursor_ = cursor;
    }

    /**
     * The QuantityWnd, hit-tested FIRST (it is drawn last, `RelativePosition
     * false` above everything). Returns true when the position is over it; the
     * caller then withholds the click from every other entry point. Accept
     * pushes the pick-up through cursorGesture(). Measured on the official
     * client: ESC does NOT close it (gestures.tsv row 16) -- only its X and
     * Accept do -- and while it is up other item-slot clicks are ignored
     * (row 18).
     */
    bool processQuantityInput(int mouseX, int mouseY, bool leftClicked,
                              const UILayout& layout);

    /** The shared gesture -- the bag window (B899) calls click() on it. */
    eqt::inventory::CursorGesture& cursorGesture() { return cursorGesture_; }
    QuantityWindowState& quantityWindow() { return quantity_; }
    const QuantityWindowState& quantityWindow() const { return quantity_; }

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
     * T436 (board B429): the merchant and bank windows.
     *
     * ONE entry point for both, because they are the two halves of the same
     * commerce gesture and neither is ever open with the other (a merchant
     * is class 41, a banker is not). The BANK is asked first: the oracle's
     * BankWnd is `Style_Sizable false` with its own placement, so it is
     * drawn above the centre popups, while the merchant window IS a centre
     * popup -- hit-test order mirrors draw order.
     *
     * The cursor attachment is passed CONST: it is not clickable (the
     * oracle's CursorAttachment is `Style_Transparent true` with no
     * control in it at all), but the merchant window's shared Buy/Sell rect
     * reads it -- a held item is what SELL acts on.
     *
     * Intents pushed, all of them pre-existing: `VendorBuyIntent` and
     * `VendorSellIntent` (the same two `/buy` and `/sell` push),
     * `VendorToggleIntent` for MW_Done_Button (its handler closes an open
     * session) and T436's own `CloseBankIntent` for BW_DoneButton. No panel
     * closes itself -- each waits for the server's own Closed event, T290's
     * rule.
     */
    bool processCommerceInput(int mouseX, int mouseY, bool leftClicked,
                              const UILayout& layout,
                              MerchantPanelState& merchant,
                              BankPanelState& bank,
                              const CursorAttachmentState& cursor);

    /**
     * B898 (#214): the skill trainer window. Its own entry point (the T415 /
     * T421 shape), defined in trainer_panel.cpp beside its hit tests. The
     * window is not a centre popup (Style_Sizable false, 580x280), so it is
     * hit-tested above them like the bank. Pushes only pre-existing intents:
     * `TrainSkillIntent` (Train, the selected row's skill id -- what
     * `/train <id>` pushes) and `CloseTrainerIntent` (Done and the close box
     * -- what `/trainclose` pushes). Closes only on TrainerWindowClosed.
     */
    bool processTrainerInput(int mouseX, int mouseY, bool leftClicked,
                             const UILayout& layout, TrainerPanelState& trainer);

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
     * B899 (#215 / #151 Leg B): the container windows -- world and carried.
     * Defined in src/client/graphics/ui/container_input.cpp.
     *
     * Its own entry point, the T415/T421/T436 shape: the oracle's
     * ContainerWindow is `RelativePosition false` at its own <Location>, so
     * it is not a centre popup and is hit-tested beside the bank.
     *
     * Combine pushes the existing TradeskillCombineIntent with the world
     * (1000) or the carried (the bag's own general slot) container slot;
     * Done and the close box push TradeskillCloseIntent and leave the window
     * up until TradeskillContainerClosed comes back. A left or right click
     * on a cell goes through B442's shared slotClick() -> CursorGesture,
     * with the cell's wire slot (bag sub-slot 251-330 or world 4000-4009).
     */
    bool processContainerInput(int mouseX, int mouseY, bool leftClicked,
                               const UILayout& layout,
                               ContainerWindowsState& containers);

    /**
     * B899: ESC on the container windows. The world container first (server
     * owned, T443), else the most recently opened bag; each pushes
     * TradeskillCloseIntent and nothing is hidden locally. Returns true if a
     * window was addressed.
     */
    bool processContainerEscape(ContainerWindowsState& containers);

    /**
     * Handle a key press. Returns true if consumed.
     *
     * ESC dismisses the topmost open centre popup, in the order they are
     * DRAWN (renderer_frame_render.cpp): tradeskill, then loot, then
     * skills, then spellbook, then the inventory popup.
     *
     * T443 (board B454): the two SERVER-OWNED windows in that list --
     * the tradeskill container and the loot window -- do NOT close
     * themselves. T290's rule: the UI never closes itself, because an
     * optimistic local close desyncs the server's "this container/corpse
     * is open" state (`ctx_.active_tradeskill_object_id`, which also
     * gates InventoryManager::setWorldContainerOpen). Each pushes its
     * close intent and leaves the panel up until the server's own
     * TradeskillContainerClosed / LootWindowClosed comes back. The other
     * three are pure client views and are hidden here.
     */
    bool processKey(irr::EKEY_CODE key, bool pressed, bool shift, bool ctrl,
                    InventoryPanelState& inventory,
                    SpellbookPopupState& spellbook,
                    SkillsPopupState& skills,
                    LootPanelState& loot,
                    TradeskillPanelState& tradeskill);

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

    // B442: see setFrameClickState().
    eqt::inventory::CursorGesture cursorGesture_;
    QuantityWindowState quantity_;
    bool rightClicked_ = false;
    eqt::inventory::ClickModifiers mods_;
    const CursorAttachmentState* cursor_ = nullptr;
    // Coin this UI put on the cursor and has not yet put back. The renderer
    // has no coin feed (CursorAttachmentState carries items); the game thread
    // stays authoritative -- PlanCoinGesture() returns held coin on ANY purse
    // click -- so this only chooses between "open the dialog" and "put back".
    bool coinHeld_ = false;
    bool cursorHoldsItem() const;
    // One item-slot click through the shared gesture: opens the QuantityWnd
    // when the gesture says so, otherwise the gesture pushed the intent.
    void slotClick(int16_t slotId, bool leftClicked,
                   const eqt::inventory::SlotSnapshot& slot, const std::string& name);
    // A purse row (0 = platinum .. 3 = copper, the oracle's IW_Money order).
    void coinRowClick(int denom, int32_t onHand);

    // Hit test a point against a horizontal slot row and return slot index,
    // or -1. Used by the hotbar; the inventory popup's grid geometry comes
    // from InventoryLayout::slotRect() instead (T57 — single shared source).
    int hitTestSlotRow(int mx, int my, int startX, int startY,
                       int slotSize, int padding, int count) const;
};

} // namespace Graphics
} // namespace EQT
