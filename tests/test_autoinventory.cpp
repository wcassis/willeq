// B1140: /autoinventory -- InventoryManager::autoInventoryCursor() against the
// official client's own placements on lane 5 (review/B1140/oracle/O1_wiz and
// O2_wiz; Testcaster: general row 22-29 full but for what each leg freed, a
// Backpack (10 slots) in general 28 -> bag slots 311-320, Crimson Robe of
// Alendine on the cursor at login).
//
//   O1 (row full): /autoinventory x3 with robe, Imbued Fire Opal, Fire Opal
//      queued -> C->S OP_MoveItem 30->311, 30->30; 30->312, 30->30; 30->313.
//   O2 (general 25 free): robe -> 30->25, 30->30; Fire Opal -> 30->311; a
//      second Fire Opal -> 30->311 qty=1 (onto the stack, 312 empty).
//
// dacd9534 had no /autoinventory at all ("CMD outcome=unrecognized", B1137's
// gate2/fable_WIZ2/fable.log), which left the summoned opal in the cursor
// queue where neither fable's count nor the server's worn+personal check sees
// it. These tests do not compile on dacd9534 (no autoInventoryCursor()).
#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"

#include <array>
#include <variant>
#include <vector>

using eqt::events::MoveItemIntent;
using eqt::inventory::InventoryManager;
using eqt::inventory::ItemInstance;
namespace inv = eqt::inventory;

namespace {

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

ItemInstance item(uint32_t id, const char* name) {
    ItemInstance it;
    it.itemId = id;
    it.name = name;
    it.slots = 0xFFFFFFFFu;
    it.size = 1;
    return it;
}

ItemInstance stackOf(uint32_t id, const char* name, int32_t qty, int32_t size) {
    ItemInstance it = item(id, name);
    it.stackable = true;
    it.quantity = qty;
    it.stackSize = size;
    return it;
}

std::vector<MoveItemIntent> moves(CapturingBridge& b) {
    std::vector<MoveItemIntent> out;
    for (const auto& i : b.drainIntents()) {
        if (const auto* m = std::get_if<MoveItemIntent>(&i)) out.push_back(*m);
    }
    return out;
}

struct Rig {
    CapturingBridge bridge;
    InventoryManager mgr;
    // Testcaster's general row at login (Titanium 22-29), Backpack in 28.
    explicit Rig(bool freeSlot25) {
        mgr.setBridge(&bridge);
        mgr.setItem(22, stackOf(9990, "Skin of Milk", 16, 20));
        mgr.setItem(23, stackOf(9991, "Bread Cakes", 15, 20));
        mgr.setItem(24, item(15288, "Spell: Minor Shielding"));
        if (!freeSlot25) mgr.setItem(25, item(15372, "Spell: Blast of Cold"));
        mgr.setItem(26, item(18755, "Tattered Note"));
        mgr.setItem(27, stackOf(21779, "Bandages", 20, 20));
        ItemInstance pack = item(32601, "Backpack");
        pack.bagSlots = 10;
        pack.bagSize = inv::SIZE_GIANT;
        mgr.setItem(28, pack);
        mgr.setItem(29, item(14337, "Blue Crystal Staff"));
        mgr.setItem(inv::CURSOR_SLOT, item(11641, "Crimson Robe of Alendine"));
        bridge.drainIntents();
    }
    void summon(const ItemInstance& it) { mgr.setItem(inv::CURSOR_SLOT, it); }
};

void expectMoves(const std::vector<MoveItemIntent>& got,
                 const std::vector<std::array<int64_t, 3>>& want) {
    ASSERT_EQ(got.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        EXPECT_EQ(got[i].fromSlot, want[i][0]) << "move " << i;
        EXPECT_EQ(got[i].toSlot, want[i][1]) << "move " << i;
        EXPECT_EQ(got[i].quantity, static_cast<uint32_t>(want[i][2])) << "move " << i;
    }
}

}  // namespace

TEST(InventoryAutoInventory, O1_GeneralRowFull_FillsTheBackpackInOrder) {
    Rig r(/*freeSlot25=*/false);
    r.summon(item(22552, "Imbued Fire Opal"));
    r.summon(stackOf(10031, "Fire Opal", 1, 20));

    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 311, 0}, {30, 30, 0}});
    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 312, 0}, {30, 30, 0}});
    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 313, 0}});

    EXPECT_EQ(r.mgr.getItem(311)->itemId, 11641u);
    EXPECT_EQ(r.mgr.getItem(313)->itemId, 10031u);
    EXPECT_FALSE(r.mgr.hasCursorItem());
    // An empty cursor: nothing to place, nothing sent.
    EXPECT_FALSE(r.mgr.autoInventoryCursor());
    EXPECT_TRUE(moves(r.bridge).empty());
}

TEST(InventoryAutoInventory, O2_FreeGeneralSlotFirst_ThenBag_ThenOntoTheMatchingStack) {
    Rig r(/*freeSlot25=*/true);
    r.summon(stackOf(10031, "Fire Opal", 1, 20));

    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 25, 0}, {30, 30, 0}});
    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 311, 0}});

    r.summon(stackOf(10031, "Fire Opal", 1, 20));
    ASSERT_TRUE(r.mgr.autoInventoryCursor());
    expectMoves(moves(r.bridge), {{30, 311, 1}});
    EXPECT_EQ(r.mgr.getItem(311)->quantity, 2);
    EXPECT_FALSE(r.mgr.hasItem(312));
    EXPECT_FALSE(r.mgr.hasCursorItem());
}
