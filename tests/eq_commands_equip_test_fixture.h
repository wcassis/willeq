// Shared fixture + seeding helpers for the test_eq_commands_equip family.
//
// T446: test_eq_commands_equip.cpp reached 1,492 lines, one breath under
// the 1,500-line FileSizeCeiling FAIL (M00, docs/decisions.md 2026-09-04).
// The cases are now split across test_eq_commands_equip_*.cpp, all of them
// sources of the SINGLE test_eq_commands_equip target (T165's precedent
// from the test_command_processor family), so the split costs no extra
// binary, no extra ctest discovery pass, and -- because every case keeps
// its suite AND its name -- no ctest test name changes at all.
//
// The fixture lives in a NAMED namespace on purpose. GTest requires every
// test in a suite to share one fixture TYPE; an anonymous-namespace fixture
// in a header would give each translation unit its own distinct type and
// TEST_F(EqCommandsEquipTest, ...) spread across files would abort at
// startup with "All tests in the same test suite must use the same test
// fixture class". The free helpers below stay in an anonymous namespace --
// they are plain functions, so a per-TU copy is correct and costs nothing.
//
// The unit's own history (why /equip is tested at this seam at all) is at
// the top of test_eq_commands_equip_moves.cpp.

#pragma once

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/trade_manager.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using namespace eqt::inventory;

namespace eqt {
namespace tests {

class EqCommandsEquipTest : public ::testing::Test {
protected:
    EqCommandsEquipTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsEquipTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

}  // namespace tests
}  // namespace eqt

namespace {

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// T71 -- /equip's local-state sync fix (see the plan's Review) requires a
// real item at fromSlot (or a real cursor item, for cursor-source moves)
// before it will push anything -- matching InventoryManager::pickupItem()'s
// own "nothing there to pick up" refusal. Tests exercising the happy path
// now seed the item they intend to move first, the same way a live session
// would have one (via CharInventory/#summonitem) before ever running /equip.
void SeedItem(EverQuest& eq, int16_t slot, uint32_t itemId) {
    ItemInstance item;
    item.itemId = itemId;
    item.name = "Test Item";
    item.slots = 0xFFFFFFFFu;  // fits any equipment slot -- these tests exercise
                                // /equip's wiring, not canEquipItem()'s own
                                // (already-covered-elsewhere) legality logic.
    eq.GetInventoryManager()->setItem(slot, item);
}

// T71 -- the local-state sync fix also pushes EquipmentChangedIntent
// alongside MoveItemIntent whenever an endpoint is an equipment slot (the
// same pairing InventoryManager::pickupItem()/placeItem() already do for
// drag/drop), so happy-path intent counts grew from 1 to (up to) 2. This
// pulls the MoveItemIntent out of a mixed intent list rather than assuming
// its position.
const eqt::events::MoveItemIntent* FindMoveItemIntent(
    const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) {
            return &std::get<eqt::events::MoveItemIntent>(intent);
        }
    }
    return nullptr;
}

// Seeds an item with full control over name/slots-bitmask/reqLevel, unlike
// SeedItem() above (which deliberately fits any equip slot to isolate the
// wiring tests above from legality). slots=0 by default means "fits no
// equipment slot at all" (ItemInstance's own default) -- callers pass the
// EQUIPSLOT_* bit(s) they want.
void SeedNamedItem(EverQuest& eq, int16_t slot, uint32_t itemId, const std::string& name,
                    uint32_t slotsBitmask, uint8_t reqLevel = 0) {
    ItemInstance item;
    item.itemId = itemId;
    item.name = name;
    item.slots = slotsBitmask;
    item.reqLevel = reqLevel;
    eq.GetInventoryManager()->setItem(slot, item);
}

void SeedBag(EverQuest& eq, int16_t slot, uint32_t itemId, const std::string& name, uint8_t bagSlots) {
    ItemInstance bag;
    bag.itemId = itemId;
    bag.name = name;
    bag.slots = 0;
    bag.bagSlots = bagSlots;
    eq.GetInventoryManager()->setItem(slot, bag);
}

}  // namespace
