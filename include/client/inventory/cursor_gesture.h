/*
 * CursorGesture -- the click-to-cursor item gesture, ONE implementation for
 * every item window (B442, GitLab #151).
 *
 * WHAT WAS MISSING. InventoryManager::pickupItem / placeItem /
 * pickupPartialStack / placeOnMatchingStack / pickupMoney all existed and all
 * push MoveItemIntent (-> SendMoveItem), and nothing but tests called them:
 * the static UI latched no pick-up and no put-down, a bank slot could be
 * hovered and never used, and an inventory slot's left click opened the item
 * display (fable's old deviation; the oracle opens it on a RIGHT click).
 *
 * THE OFFICIAL CLIENT'S GESTURE, MEASURED (Titanium is click-to-cursor, not
 * drag and drop -- the item rides the pointer as CA_Anim between two clicks).
 * Source: review/UIGAP-ORACLE/151/gestures.tsv (#151 Leg O, 2026-10-05); the
 * row numbers below are that ledger's.
 *   - LEFT on a single item, cursor empty         -> pick up (rows 1, 15)
 *   - LEFT on a STACK of 2+, cursor empty         -> the QuantityWnd, prefilled
 *     with the whole stack (row 3); Accept picks that many up (rows 4, 5)
 *   - SHIFT+LEFT on a stack                       -> the whole stack, no
 *     dialog (row 10); CTRL+LEFT -> exactly one (row 11)
 *   - LEFT with an item held, any modifier        -> place; on an occupied
 *     slot ONE move and the displaced item is on the cursor (row 7); on a
 *     matching stack the held count merges (rows 9, 12); CTRL just places
 *     (row 13)
 *   - ALT+LEFT on an item                         -> the item display, kept
 *     open (row 26)
 *   - RIGHT on an item                            -> the item display, shown
 *     only WHILE the button is held (row 25; B442b's held mode,
 *     ItemDisplayPanelState), or for a carried bag the bag window, which
 *     right click toggles (rows 19-21; StaticUIInput installs the hook below
 *     and it pushes CursorGestureOp::ToggleBag -> the /bag body)
 *   - a purse row                                 -> the QuantityWnd for coin
 *     (row 28), held coin goes back (row 30); the bank's coin rows the same
 *     on OP_MoveCoin slot 2 (rows 43-44)
 *
 * THE SPLIT. Two halves, one per thread, joined by ONE intent:
 *   render thread  CursorGesture::click() maps (slot, button, modifiers, what
 *                  the panel shows) to a GestureAction and pushes
 *                  CursorGestureIntent (or ItemDisplayRequestIntent). It never
 *                  touches a panel's slot state: the panels change when the
 *                  game thread republishes InventorySlotChanged.
 *   game thread    ApplyCursorGesture() resolves the intent against
 *                  InventoryManager's model -- the only code that knows
 *                  whether the held item stacks onto the target -- and calls
 *                  the existing pickupItem / placeItem / pickupPartialStack /
 *                  placeOnMatchingStack, which push the MoveItemIntent(s).
 * /pickup, /putdown and /coin push the SAME CursorGestureIntent, so a
 * --script run and a click are one path.
 *
 * SERVER AUTHORITY. eqemu answers a successful OP_MoveItem with SILENCE and a
 * refused one with a resync (Client::SwapItem / SwapItemResync), so the model
 * moves when the request is sent, exactly as the official client's own cursor
 * does and exactly as /equip has since T71. The renderer never moves an item
 * on its own: what it draws is what the game thread republishes.
 *
 * This header is device-free and graphics-free on purpose: the bag window
 * (B899) and the trade/give windows call click() from their own slot arms.
 * KEEP THE INTERFACE STABLE -- other units build against it.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "client/events/renderer_intents.h"   // CursorGestureIntent

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace eqt {
namespace inventory {

class InventoryManager;

enum class ClickButton : uint8_t { Left = 0, Right };

struct ClickModifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

/** What the clicked window's panel shows for the slot (render-thread cache). */
struct SlotSnapshot {
    bool occupied = false;
    int32_t quantity = 0;   // the stack count the panel holds; 0/1 = not a stack
};

/** What the renderer knows about the cursor (CursorAttachmentState). */
struct CursorSnapshot {
    bool hasItem = false;
};

enum class GestureAction : uint8_t {
    None = 0,       // nothing to act on (an empty slot with an empty cursor)
    CursorClick,    // CursorGestureIntent{Click} pushed
    OpenQuantity,   // the caller opens the QuantityWnd for this slot
    ItemDisplay,    // ItemDisplayRequestIntent pushed
    OpenContainer,  // the open-container hook claimed the right click
    PickUpOne,      // CTRL+LEFT on a stack: CursorGestureIntent{PickUp, qty 1} pushed
};

/**
 * The button map, as a pure function -- no bridge, no hook. Every window's
 * click arm and every test reads the rule from here, so there is one.
 * A RIGHT click classifies as ItemDisplay; click() then offers it to the
 * open-container hook first (ALT+LEFT is the display alone).
 */
GestureAction classifyClick(ClickButton button, ClickModifiers mods,
                            const SlotSnapshot& slot, const CursorSnapshot& cursor);

class CursorGesture {
public:
    /**
     * Return true when `slotId` holds a container and a bag window was opened
     * (or closed) for it; false to fall back to the item display. Installed by
     * the bag-window unit (B899). With no hook a right click always displays.
     */
    using OpenContainerHook = std::function<bool(int16_t slotId)>;

    void setBridge(eqt::bridge::GameStateBridge* bridge) { bridge_ = bridge; }
    void setOpenContainerHook(OpenContainerHook hook) { openContainer_ = std::move(hook); }

    /**
     * One slot click in any item window. `slotId` is the Titanium (model) slot
     * id -- 0-21 worn, 22-29 general, 251-330 bag contents, 2000-2007 bank,
     * 3000-3007 trade -- never a server/RoF2 number. Pushes what the action
     * needs and returns it; OpenQuantity pushes nothing (the caller owns the
     * dialog and later calls pickUpQuantity()).
     */
    GestureAction click(int16_t slotId, ClickButton button, ClickModifiers mods,
                        const SlotSnapshot& slot, const CursorSnapshot& cursor);

    /** QuantityWnd Accept: pick `quantity` of the stack in `slotId` up. */
    void pickUpQuantity(int16_t slotId, int32_t quantity);

    /**
     * A purse row (IW_Money0..3). `coinType` is EQT::COINTYPE_* (0 cp .. 3
     * pp); `amount` 0 = the whole pile. Held coin goes back instead.
     * B442b: `source` is the store the row belongs to, EQT::COINSLOT_INVENTORY
     * (1, the purse) or EQT::COINSLOT_BANK (2, BW_Money0..3, rows 43-44).
     */
    void coinClick(uint8_t coinType, int32_t amount, uint8_t source = 1);

private:
    eqt::bridge::GameStateBridge* bridge_ = nullptr;
    OpenContainerHook openContainer_;
};

// ---------------------------------------------------------------------------
// Game-thread half
// ---------------------------------------------------------------------------

struct CursorGestureOutcome {
    bool success = false;
    // Every model slot whose contents changed (the cursor included), in the
    // order the game thread must republish InventorySlotChanged for them.
    std::vector<int16_t> touched;
    std::string message;   // what a slash command prints; empty on a silent no-op
};

/**
 * Resolve one item op (Click / PickUp / PutDown) against the model and run
 * it through the existing InventoryManager calls. CoinClick is not an item op
 * -- see PlanCoinGesture(). Never sends a packet itself.
 */
CursorGestureOutcome ApplyCursorGesture(InventoryManager& inv,
                                        const eqt::events::CursorGestureIntent& intent);

/**
 * The OP_MoveCoin a purse click / `/coin` needs (eqemu Client::OPMoveCoin:
 * slot 0 = cursor, 1 = inventory). `purseOfType` is the player's on-hand
 * count of `coinType`. When the cursor already holds coin the plan returns it
 * to the purse (cursor -> inventory, its own type and amount) whatever was
 * clicked; otherwise it moves min(requested or all, purse) onto the cursor.
 * Pure: the caller sends it and updates the purse and the cursor model.
 */
struct CoinMovePlan {
    bool send = false;
    bool toCursor = false;    // true: inventory -> cursor; false: cursor -> inventory
    int32_t coinType = 0;
    int32_t amount = 0;
    std::string message;
};
CoinMovePlan PlanCoinGesture(const InventoryManager& inv, uint8_t coinType,
                             int32_t requested, uint32_t purseOfType);

/**
 * B442b (#151 rows 43-44): the same plan for either coin store. `bank` names
 * the store the clicked row belongs to -- the bank's BW_Money rows
 * (OP_MoveCoin slot 2) or the purse (slot 1): coin comes OFF that store, and
 * held coin goes back ONTO it (row 44 returned bank coin to the purse by
 * clicking the purse). `storeOfType` is that store's count of `coinType`.
 * Only the message differs from the purse overload above.
 */
CoinMovePlan PlanCoinGesture(const InventoryManager& inv, uint8_t coinType,
                             int32_t requested, uint32_t storeOfType, bool bank);

// ---------------------------------------------------------------------------
// B442b (#151 rows 31-35): the drop and destroy confirmations
// ---------------------------------------------------------------------------

/** The asker's token on ConfirmationRequestedData::purpose. */
enum class ConfirmPurpose : uint8_t { None = 0, DropItem = 1, DestroyItem = 2 };

// The official prompts, by eqstr id (eqstr_us.txt, read through
// EQT::StringDatabase -- never transcribed, B390's rule):
//   9057 "ARE YOU SURE you wish to drop your %1 on the ground?"  (row 31)
//   2043 "Are you absolutely sure you want to destroy your %1?"  (row 34)
constexpr uint32_t kEqStrDropOnGround = 9057;
constexpr uint32_t kEqStrDestroyItem = 2043;

// How long each prompt stays up unanswered before it closes itself, sending
// nothing and leaving the item on the cursor. MEASURED twice each by
// UIGAP-live2 (review/UIGAP-ORACLE/151/README.md, run3 + run5): Destroy 10.1 s,
// Drop 20.1 s.
constexpr float kDropPromptTimeoutSeconds = 20.0f;
constexpr float kDestroyPromptTimeoutSeconds = 10.0f;

/**
 * What a RequestDrop / RequestDestroy needs: whether to prompt at all (an
 * item must be held), which eqstr row, and the held item's name for %1.
 * Pure -- the bridge formats the line and publishes ConfirmationRequested.
 */
struct ConfirmPromptPlan {
    bool prompt = false;
    ConfirmPurpose purpose = ConfirmPurpose::None;
    uint32_t eqstrId = 0;
    std::string itemName;
    std::string message;   // why nothing is asked, when prompt is false
    float timeoutSeconds = 0.0f;   // kDrop/kDestroyPromptTimeoutSeconds
};
ConfirmPromptPlan PlanConfirmPrompt(const InventoryManager& inv,
                                    eqt::events::CursorGestureOp op);

// ---------------------------------------------------------------------------
// The slash-command surface: /pickup <slot> [qty], /putdown <slot>, /cursor,
// /coin <pp|gp|sp|cp> <n>. Slot numbers are inventory.slot_id, read through
// parseUserSlot() -- the repo's ONE inbound conversion.
// ---------------------------------------------------------------------------

struct CursorCommandParse {
    bool ok = false;
    eqt::events::CursorGestureIntent intent;   // echo = true when ok
    std::string error;                          // usage / refusal text otherwise
};

/** `verb` is "pickup", "putdown" or "coin" (lower case, no slash). */
CursorCommandParse ParseCursorCommand(const std::string& verb, const std::string& args);

/** /cursor's reply: what the cursor holds, item queue and coin. */
std::string DescribeCursor(const InventoryManager& inv);

}  // namespace inventory
}  // namespace eqt
