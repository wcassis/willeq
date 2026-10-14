// T230 -- real-EverQuest/EqActionHandler coverage for the keyboard hotbar
// Item route's two halves below the action layer:
//
//   EqActionHandler::findItemSlotByItemId(itemId)
//       -> InventoryManager::findItemSlotByItemId (the mouse path's lookup)
//   EqActionHandler::useItem(slot)
//       -> EverQuest::UseInventoryItem(slot) (eq_bridge.cpp; the ONE
//          click-effect sender, shared with the mouse HotbarActivateIntent arm)
//
// Before T230 useItem() was a stub ("Use item not yet implemented"), so a
// keyboard press could never have reached the wire even with the id->slot
// hop in place. Observability is the same as test_eq_action_handler_
// tradeskill.cpp: the replay harness has no zone connection, so the
// OP_CastSpell bytes are NOT asserted here -- the live leg in
// refactor/T230_target_resolver_and_hotbar_items.md covers the wire. What
// IS asserted is every bridge-visible effect: the "Using <item>" system
// message (emitted only after QueuePacket), the refusal messages, and the
// HotbarCooldownStarted event for the hotbar slot holding the item.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/hotbar/hotbar_model.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"

using namespace eqt;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::string lastSystemMessage(const std::vector<eqt::state::GameEvent>& events) {
    std::string last;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            last = std::get<eqt::state::ChatMessageData>(e.data).message;
        }
    }
    return last;
}

std::vector<int> cooldownIndices(const std::vector<eqt::state::GameEvent>& events) {
    std::vector<int> out;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::HotbarCooldownStarted) {
            out.push_back(std::get<eqt::state::HotbarCooldownStartedData>(e.data).index);
        }
    }
    return out;
}

constexpr uint32_t kPotionId = 1234;
constexpr int16_t kPotionSlot = 24;  // a general inventory slot

eqt::inventory::ItemInstance makePotion(int32_t effectId, int32_t recastMs) {
    eqt::inventory::ItemInstance item;
    item.itemId = kPotionId;
    item.name = "Test Potion";
    item.clickEffect.effectId = effectId;
    item.clickEffect.type = 0;
    item.clickEffect.recastDelay = recastMs;
    return item;
}

struct Harness {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    RecordingBridge bridge;
    EqActionHandler handler{eq};
    Harness() { eq.setBridge(&bridge); }
};

} // namespace

TEST(EqActionHandlerHotbarItem, FindItemSlotByItemId_ResolvesCarriedItem) {
    Harness h;
    h.eq.GetInventoryManager()->setItem(kPotionSlot, makePotion(/*effect=*/500, /*recast=*/0));

    EXPECT_EQ(h.handler.findItemSlotByItemId(kPotionId), kPotionSlot);
}

TEST(EqActionHandlerHotbarItem, FindItemSlotByItemId_AbsentItemIsInvalidSlot) {
    Harness h;
    EXPECT_EQ(h.handler.findItemSlotByItemId(kPotionId), eqt::inventory::SLOT_INVALID);
}

TEST(EqActionHandlerHotbarItem, UseItem_SendsClickEffectAndReportsUsing) {
    Harness h;
    h.eq.GetInventoryManager()->setItem(kPotionSlot, makePotion(500, 0));
    h.bridge.drainEvents();

    h.handler.useItem(kPotionSlot);

    // "Using ..." is emitted only after QueuePacket(HC_OP_CastSpell) in
    // UseInventoryItem(); the pre-T230 stub emitted nothing at all.
    EXPECT_EQ(lastSystemMessage(h.bridge.drainEvents()), "Using Test Potion");
}

TEST(EqActionHandlerHotbarItem, UseItem_CooldownReachesEveryHotbarSlotHoldingTheItem) {
    Harness h;
    h.eq.GetInventoryManager()->setItem(kPotionSlot, makePotion(500, /*recast=*/30000));
    h.eq.GetHotbarModel()->assignSlot({0, 3}, eqt::HotbarSlotType::Item, kPotionId);
    h.eq.GetHotbarModel()->assignSlot({0, 7}, eqt::HotbarSlotType::Item, kPotionId);
    h.eq.GetHotbarModel()->assignSlot({0, 5}, eqt::HotbarSlotType::Item, kPotionId + 1);  // other item
    h.bridge.drainEvents();

    h.handler.useItem(kPotionSlot);

    auto events = h.bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "Using Test Potion");
    EXPECT_EQ(cooldownIndices(events), (std::vector<int>{3, 7}));
}

TEST(EqActionHandlerHotbarItem, UseItem_NoClickEffect_RefusesWithoutCooldown) {
    Harness h;
    h.eq.GetInventoryManager()->setItem(kPotionSlot, makePotion(/*effect=*/0, 30000));
    h.eq.GetHotbarModel()->assignSlot({0, 3}, eqt::HotbarSlotType::Item, kPotionId);
    h.bridge.drainEvents();

    h.handler.useItem(kPotionSlot);

    auto events = h.bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "Test Potion has no click effect");
    EXPECT_TRUE(cooldownIndices(events).empty());
}

TEST(EqActionHandlerHotbarItem, UseItem_EmptySlot_RefusesItemNotFound) {
    Harness h;
    h.bridge.drainEvents();

    h.handler.useItem(kPotionSlot);

    EXPECT_EQ(lastSystemMessage(h.bridge.drainEvents()), "Item not found");
}
