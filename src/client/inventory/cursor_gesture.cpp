/*
 * CursorGesture -- B442 (GitLab #151). See include/client/inventory/
 * cursor_gesture.h for the gesture, the render/game split and why the model
 * moves when the request is sent.
 */

#include "client/inventory/cursor_gesture.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "common/logging.h"
#include "common/packets/merchant.h"   // EQT::COINTYPE_*

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace eqt {
namespace inventory {

// ---------------------------------------------------------------------------
// Render-thread half
// ---------------------------------------------------------------------------

GestureAction classifyClick(ClickButton button, ClickModifiers mods,
                            const SlotSnapshot& slot, const CursorSnapshot& cursor) {
    if (button == ClickButton::Right) {
        // The oracle's right click inspects (or opens a bag); it never moves
        // an item, held or not.
        return slot.occupied ? GestureAction::ItemDisplay : GestureAction::None;
    }
    // LEFT. With an item held every slot is a destination -- empty (place),
    // occupied (swap) or a matching stack (merge), whatever the modifier
    // (oracle rows 6-9, 13); the game thread decides which, because only the
    // model knows whether the two stack.
    if (cursor.hasItem) {
        return GestureAction::CursorClick;
    }
    if (!slot.occupied) {
        return GestureAction::None;
    }
    if (mods.alt) {
        return GestureAction::ItemDisplay;          // row 26
    }
    if (slot.quantity > 1) {
        if (mods.shift) return GestureAction::CursorClick;   // row 10: all
        if (mods.ctrl) return GestureAction::PickUpOne;      // row 11: one
        return GestureAction::OpenQuantity;                  // row 3
    }
    return GestureAction::CursorClick;              // rows 1, 15
}

GestureAction CursorGesture::click(int16_t slotId, ClickButton button, ClickModifiers mods,
                                   const SlotSnapshot& slot, const CursorSnapshot& cursor) {
    const GestureAction action = classifyClick(button, mods, slot, cursor);
    switch (action) {
        case GestureAction::CursorClick:
            if (bridge_) {
                eqt::events::CursorGestureIntent intent;
                intent.op = eqt::events::CursorGestureOp::Click;
                intent.slot = slotId;
                bridge_->pushIntent(intent);
            }
            return action;
        case GestureAction::ItemDisplay:
            // A container's RIGHT click belongs to the bag window when one is
            // installed; it says whether it took the click. ALT+LEFT is the
            // display alone.
            if (button == ClickButton::Right && openContainer_ && openContainer_(slotId)) {
                return GestureAction::OpenContainer;
            }
            if (bridge_) {
                bridge_->pushIntent(eqt::events::ItemDisplayRequestIntent{slotId});
            }
            return action;
        case GestureAction::PickUpOne:
            pickUpQuantity(slotId, 1);
            return action;
        case GestureAction::OpenQuantity:
        case GestureAction::OpenContainer:
        case GestureAction::None:
        default:
            return action;
    }
}

void CursorGesture::pickUpQuantity(int16_t slotId, int32_t quantity) {
    if (!bridge_ || quantity <= 0) {
        return;
    }
    eqt::events::CursorGestureIntent intent;
    intent.op = eqt::events::CursorGestureOp::PickUp;
    intent.slot = slotId;
    intent.quantity = quantity;
    bridge_->pushIntent(intent);
}

void CursorGesture::coinClick(uint8_t coinType, int32_t amount) {
    if (!bridge_ || coinType > EQT::COINTYPE_PP) {
        return;
    }
    eqt::events::CursorGestureIntent intent;
    intent.op = eqt::events::CursorGestureOp::CoinClick;
    intent.coinType = coinType;
    intent.quantity = std::max<int32_t>(0, amount);
    bridge_->pushIntent(intent);
}

// ---------------------------------------------------------------------------
// Game-thread half
// ---------------------------------------------------------------------------

namespace {

// A bag's contents ride with it (T252/T255), so a move that involves a bag
// changes the ten child slots under both ends as well.
void appendWithBagChildren(std::vector<int16_t>& out, int16_t parent, bool bagInvolved) {
    out.push_back(parent);
    if (!bagInvolved || !isContainerParentSlot(parent)) {
        return;
    }
    for (int16_t i = 0; i < BAG_SLOT_COUNT; ++i) {
        const int16_t child = calcContainerSlotId(parent, i);
        if (child != SLOT_INVALID) {
            out.push_back(child);
        }
    }
}

const char* coinName(int32_t coinType) {
    switch (coinType) {
        case EQT::COINTYPE_PP: return "platinum";
        case EQT::COINTYPE_GP: return "gold";
        case EQT::COINTYPE_SP: return "silver";
        case EQT::COINTYPE_CP: return "copper";
        default: return "coin";
    }
}

int32_t coinTypeOf(InventoryManager::CursorMoneyType t) {
    switch (t) {
        case InventoryManager::CursorMoneyType::Platinum: return EQT::COINTYPE_PP;
        case InventoryManager::CursorMoneyType::Gold: return EQT::COINTYPE_GP;
        case InventoryManager::CursorMoneyType::Silver: return EQT::COINTYPE_SP;
        case InventoryManager::CursorMoneyType::Copper: return EQT::COINTYPE_CP;
        case InventoryManager::CursorMoneyType::None:
        default: return -1;
    }
}

}  // namespace

CursorGestureOutcome ApplyCursorGesture(InventoryManager& inv,
                                        const eqt::events::CursorGestureIntent& intent) {
    using Op = eqt::events::CursorGestureOp;
    CursorGestureOutcome out;

    if (intent.op == Op::CoinClick) {
        out.message = "A coin gesture is not an item move.";
        return out;
    }
    const int16_t slot = intent.slot;
    if (slot < 0 || isCursorSlot(slot) ||
        (isContainerSlot(slot) && getParentContainerSlot(slot) == CURSOR_SLOT)) {
        out.message = "Pick a slot to move to or from -- the cursor itself is not one.";
        return out;
    }

    const ItemInstance* held = inv.getCursorItem();
    const ItemInstance* target = inv.getItem(slot);

    if (intent.op == Op::PickUp && held) {
        out.message = "You are already holding " + held->name + " -- /putdown <slot> first.";
        return out;
    }
    if (intent.op == Op::PutDown && !held) {
        out.message = "Your cursor is empty.";
        return out;
    }

    if (held) {
        // Copies: the calls below move the instances these point at.
        const std::string heldName = held->name;
        const std::string targetName = target ? target->name : std::string();
        const bool merge = target && held->stackable && target->stackable &&
                           held->itemId == target->itemId &&
                           target->quantity < target->stackSize;
        const bool bag = held->isContainer() || (target && target->isContainer());
        // placeOnMatchingStack() is the one entry point for all three
        // outcomes: it merges when the two stack with room, and otherwise
        // delegates to placeItem(), which places or swaps.
        if (!inv.placeOnMatchingStack(slot)) {
            out.message = heldName + " cannot go in " + describeSlot(slot) + ".";
            return out;
        }
        out.success = true;
        appendWithBagChildren(out.touched, slot, bag);
        appendWithBagChildren(out.touched, CURSOR_SLOT, bag);
        if (merge) {
            out.message = "Stacked " + heldName + " onto " + describeSlot(slot) + ".";
        } else if (!targetName.empty()) {
            out.message = "Put " + heldName + " in " + describeSlot(slot) + "; now holding " +
                          targetName + ".";
        } else {
            out.message = "Put " + heldName + " in " + describeSlot(slot) + ".";
        }
        return out;
    }

    if (!target) {
        out.message = "Nothing to pick up in " + describeSlot(slot) + ".";
        return out;
    }
    const std::string name = target->name;
    const int32_t have = target->quantity;
    const bool bag = target->isContainer();
    const bool partial = intent.quantity > 0 && intent.quantity < have;
    const bool ok = partial ? inv.pickupPartialStack(slot, intent.quantity)
                            : inv.pickupItem(slot);
    if (!ok) {
        out.message = "Could not pick up " + name + " from " + describeSlot(slot) + ".";
        return out;
    }
    out.success = true;
    appendWithBagChildren(out.touched, slot, bag);
    appendWithBagChildren(out.touched, CURSOR_SLOT, bag);
    out.message = partial
        ? "Picked up " + std::to_string(intent.quantity) + " " + name + " from " + describeSlot(slot) + "."
        : "Picked up " + name + " from " + describeSlot(slot) + ".";
    return out;
}

CoinMovePlan PlanCoinGesture(const InventoryManager& inv, uint8_t coinType,
                             int32_t requested, uint32_t purseOfType) {
    CoinMovePlan plan;
    if (inv.hasCursorMoney()) {
        // Held coin goes home, whatever row was clicked: its own type, its
        // whole amount (eqemu's cursor buckets hold one pile per type).
        plan.send = true;
        plan.toCursor = false;
        plan.coinType = coinTypeOf(inv.getCursorMoneyType());
        plan.amount = static_cast<int32_t>(inv.getCursorMoneyAmount());
        plan.message = "Returned " + std::to_string(plan.amount) + " " +
                       coinName(plan.coinType) + " to your purse.";
        return plan;
    }
    if (coinType > EQT::COINTYPE_PP) {
        plan.message = "Unknown coin -- use pp, gp, sp or cp.";
        return plan;
    }
    if (const ItemInstance* held = inv.getCursorItem()) {
        plan.message = "You are holding " + held->name + " -- put it down first.";
        return plan;
    }
    if (purseOfType == 0) {
        plan.message = std::string("You have no ") + coinName(coinType) + ".";
        return plan;
    }
    if (requested > 0 && static_cast<uint32_t>(requested) > purseOfType) {
        // The server would take only what is there; refuse instead so the
        // projection and the wire never disagree (the /bank precedent, T269).
        plan.message = "You do not have " + std::to_string(requested) + " " +
                       coinName(coinType) + ".";
        return plan;
    }
    plan.send = true;
    plan.toCursor = true;
    plan.coinType = coinType;
    plan.amount = requested > 0 ? requested : static_cast<int32_t>(purseOfType);
    plan.message = "Picked up " + std::to_string(plan.amount) + " " + coinName(coinType) + ".";
    return plan;
}

// ---------------------------------------------------------------------------
// Slash commands
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kPickupUsage =
    "Usage: /pickup <slot> [quantity] (slot numbers are inventory.slot_id: "
    "0-20 worn, 22 ammo, 23-30 general, 4010+ bag contents, 2000+ bank)";
constexpr const char* kPutdownUsage =
    "Usage: /putdown <slot> (slot numbers are inventory.slot_id: "
    "0-20 worn, 22 ammo, 23-30 general, 4010+ bag contents, 2000+ bank)";
constexpr const char* kCoinUsage = "Usage: /coin <pp|gp|sp|cp> <amount>";

std::vector<std::string> tokens(const std::string& args) {
    std::vector<std::string> out;
    std::istringstream iss(args);
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

bool parsePositive(const std::string& tok, int32_t& out) {
    if (tok.empty() || tok.size() > 9) return false;
    for (char c : tok) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    out = static_cast<int32_t>(std::stol(tok));
    return out > 0;
}

}  // namespace

CursorCommandParse ParseCursorCommand(const std::string& verb, const std::string& args) {
    using Op = eqt::events::CursorGestureOp;
    CursorCommandParse r;
    const auto tok = tokens(args);
    r.intent.echo = true;

    if (verb == "coin") {
        if (tok.size() != 2) { r.error = kCoinUsage; return r; }
        std::string t = tok[0];
        std::transform(t.begin(), t.end(), t.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        int type = -1;
        if (t == "pp") type = EQT::COINTYPE_PP;
        else if (t == "gp") type = EQT::COINTYPE_GP;
        else if (t == "sp") type = EQT::COINTYPE_SP;
        else if (t == "cp") type = EQT::COINTYPE_CP;
        int32_t n = 0;
        if (type < 0 || !parsePositive(tok[1], n)) { r.error = kCoinUsage; return r; }
        r.intent.op = Op::CoinClick;
        r.intent.coinType = static_cast<uint8_t>(type);
        r.intent.quantity = n;
        r.ok = true;
        return r;
    }

    const bool pickup = verb == "pickup";
    if (!pickup && verb != "putdown") {
        r.error = "Unknown cursor command: " + verb;
        return r;
    }
    const char* usage = pickup ? kPickupUsage : kPutdownUsage;
    if (tok.empty() || tok.size() > (pickup ? 2u : 1u)) { r.error = usage; return r; }

    int16_t slot = SLOT_INVALID;
    switch (parseUserSlot(tok[0], slot)) {
        case UserSlotResult::Ok: break;
        case UserSlotResult::NotAddressable: r.error = describeUnaddressableSlot(tok[0]); return r;
        case UserSlotResult::NotANumber:
        default: r.error = usage; return r;
    }
    if (isCursorSlot(slot)) {
        r.error = std::string("The cursor is not a slot to ") +
                  (pickup ? "pick up from" : "put down in") + " -- /cursor shows what it holds.";
        return r;
    }
    int32_t qty = 0;
    if (tok.size() == 2 && !parsePositive(tok[1], qty)) { r.error = usage; return r; }

    r.intent.op = pickup ? Op::PickUp : Op::PutDown;
    r.intent.slot = slot;
    r.intent.quantity = qty;
    r.ok = true;
    return r;
}

std::string DescribeCursor(const InventoryManager& inv) {
    std::string out;
    if (const ItemInstance* held = inv.getCursorItem()) {
        out = "Cursor: " + held->name;
        if (held->stackable && held->quantity > 1) {
            out += " x" + std::to_string(held->quantity);
        }
        const size_t queued = inv.getCursorQueueSize();
        if (queued > 1) {
            out += " (" + std::to_string(queued - 1) + " more queued behind it)";
        }
    }
    if (inv.hasCursorMoney()) {
        const std::string coin = std::to_string(inv.getCursorMoneyAmount()) + " " +
                                 coinName(coinTypeOf(inv.getCursorMoneyType()));
        out += out.empty() ? "Cursor: " + coin : "; " + coin;
    }
    return out.empty() ? "Your cursor is empty." : out + ".";
}

}  // namespace inventory
}  // namespace eqt
