// T446 -- bag CONTENTS: every gesture that moves a bag must carry the flat
// per-parent content range with it. Split out of test_eq_commands_equip.cpp
// (1,492 lines, one under the 1,500 ceiling). Same target, same suite, same
// case names; the shared fixture and the Seed*() helpers are in
// eq_commands_equip_test_fixture.h. See test_eq_commands_equip_moves.cpp for
// why /equip is tested at this seam.

#include "eq_commands_equip_test_fixture.h"

using eqt::tests::EqCommandsEquipTest;

namespace {

// ---------------------------------------------------------------------------
// T252 -- bag-on-bag swaps. eqemu's InventoryProfile::SwapItem
// (common/inventory_profile.cpp:392-393) swaps the two ItemInstances with
// _PutItem, and an ItemInstance CARRIES its bag contents, so both bags'
// contents follow their bag; there is no bag-on-bag refusal anywhere in
// SwapItem or Client::SwapItem. The client keeps contents FLAT by parent-
// slot range, so it must move both ranges without either overwriting the
// other -- in every gesture that moves a bag: direct /equip swap, direct
// move to an empty slot, and the cursor pickup/place pair.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, DirectSwap_BagOnBag_BothContentsFollowTheirBags) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 23, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 0), 7401, "Bp", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 1), 7402, "Bq", 0);

    eq.Commands().RequestEquip("23 24");

    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7210u);  // A now in 23 ...
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);  // ... with x
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);  // ... and y
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);  // B now in 22 ...
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);  // ... with p
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7402u);  // ... and q

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, 22);
    EXPECT_EQ(mv->toSlot, 23);
}

TEST_F(EqCommandsEquipTest, DirectMove_BagToEmptyGeneral_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);

    eq.Commands().RequestEquip("23 25");

    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(24), nullptr);
    EXPECT_EQ(inv->getItem(24)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBagSlotId(24, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(24, 0))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, CursorSwap_BagOnBag_BothContentsFollowTheirBags) {
    // The T250 scenario with BOTH bags non-empty. Server-side a bag on the
    // cursor keeps its contents (cursor bag range 331-340 on the wire,
    // CalcSlotId(slotCursor, idx)); the client mirrors that so the two
    // contents ranges never collide mid-gesture.
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 23, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 0), 7401, "Bp", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 1), 7402, "Bq", 0);

    // 1. cursor <- A: A's contents ride along in the cursor bag range.
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1)), nullptr);

    // 2. A -> 23, B displaced onto the cursor with ITS contents.
    ASSERT_TRUE(inv->placeItem(23));
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7402u);

    // 3. B -> 22 (A's old slot): B's contents land in 22's range, A's stay in 23's.
    ASSERT_TRUE(inv->placeItem(22));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7402u);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    // Wire: exactly the three moves the server needs, in order.
    auto intents = bridge.drainIntents();
    std::vector<std::pair<int16_t, int16_t>> moves;
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) {
            const auto& m = std::get<eqt::events::MoveItemIntent>(intent);
            moves.emplace_back(m.fromSlot, m.toSlot);
        }
    }
    ASSERT_EQ(moves.size(), 3u);
    EXPECT_EQ(moves[0], std::make_pair(int16_t(22), int16_t(CURSOR_SLOT)));
    EXPECT_EQ(moves[1], std::make_pair(int16_t(CURSOR_SLOT), int16_t(23)));
    EXPECT_EQ(moves[2], std::make_pair(int16_t(CURSOR_SLOT), int16_t(22)));
}

// ---------------------------------------------------------------------------
// T255 -- bank bag moves. eqemu's SwapItem is the same wholesale ItemInstance
// swap for a bank parent as for a general one; the wire numbering for bank
// bag contents is Titanium 2031 + (bank-2000)*10 + idx (titanium_limits.h,
// CalcSlotId), server-side 6210 + (bank-2000)*200 + idx (emu_constants.h:
// BANK_BAGS_BEGIN = CURSOR_BAG_END + 1 with RoF2 SLOT_COUNT 200). The client
// keeps contents flat by parent range, so a bag moving general<->bank must
// carry its range across exactly as T252 made general<->general do.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, DirectMove_BagToEmptyBank_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);

    eq.Commands().RequestEquip("23 2000");

    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    ASSERT_NE(inv->getItem(2000), nullptr);
    EXPECT_EQ(inv->getItem(2000)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7302u);
    EXPECT_EQ(calcBankBagSlotId(2000, 0), 2031);

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, 22);
    EXPECT_EQ(mv->toSlot, 2000);
}

TEST_F(EqCommandsEquipTest, DirectMove_BagFromBankToGeneral_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 2001, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBankBagSlotId(2001, 3), 7301, "Ax", 0);

    eq.Commands().RequestEquip("2001 25");

    EXPECT_EQ(inv->getItem(2001), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2001, 3)), nullptr);
    ASSERT_NE(inv->getItem(24), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(24, 3)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(24, 3))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, DirectSwap_GeneralBagOnBankBag_BothContentsFollowTheirBags) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 2000, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBankBagSlotId(2000, 0), 7401, "Bp", 0);

    eq.Commands().RequestEquip("23 2000");

    ASSERT_NE(inv->getItem(2000), nullptr);
    EXPECT_EQ(inv->getItem(2000)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);
}

TEST_F(EqCommandsEquipTest, CursorMove_BagGeneralToBankAndBack_ContentsRideTheCursor) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 2), 7301, "Ax", 0);

    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_TRUE(inv->placeItem(2005));
    EXPECT_FALSE(inv->hasCursorItem());
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_NE(inv->getItem(2005), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2005, 2)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2005, 2))->itemId, 7301u);

    ASSERT_TRUE(inv->pickupItem(2005));
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2005, 2)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_TRUE(inv->placeItem(23));
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 2)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 2))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2)), nullptr);
}

// ---------------------------------------------------------------------------
// T255 item 2 -- more than one bag queued on the cursor. The server keeps
// every queued bag's contents inside that bag's own ItemInstance and
// serialises each cursor item (with its sub-items) at slot 33 in
// CharInventory, so two queued bags both address the cursor bag range
// 331-340 on the wire. Only the FRONT bag's contents may live in that
// range client-side; the rest are held per queued bag and promoted into
// the range when that bag becomes the front.
// ---------------------------------------------------------------------------

// Titanium serialized item (T43's builder shape) with the container fields
// set so parseStaticData() yields a bag, plus optional depth-1 sub-items.
std::string BuildBagString(int16_t slot, uint32_t itemId, const std::string& name, uint8_t bagSlots,
                           const std::vector<std::pair<int16_t, std::string>>& subItems) {
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "1";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    fields[8] = "0";
    fields[9] = "0";
    fields[10] = "0";
    fields[11] = "0";
    fields[96] = "0";
    fields[97] = std::to_string(bagSlots);
    fields[98] = "4";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    std::string out = instance + "\"" + staticData + "\"";
    // Sub-item block: |sub0|sub1|...|sub9, each occupied index wrapped as
    // "instance|\"static\"" (titanium.cpp SerializeItem, depth 1).
    for (int16_t idx = 0; idx < bagSlots; ++idx) {
        out += "|";
        for (const auto& [subIdx, subStr] : subItems) {
            if (subIdx != idx) continue;
            std::string escaped;
            for (char c : subStr) {
                if (c == '"') escaped += "\\\"";
                else escaped += c;
            }
            out += "\"" + escaped + "\"";
        }
    }
    return out;
}

std::string BuildPlainItemString(int16_t slot, uint32_t itemId, const std::string& name) {
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "0";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

EQ::Net::DynamicPacket CharInventoryPacket(const std::vector<std::string>& items) {
    std::string body;
    for (const auto& s : items) {
        body += s;
        body.push_back('\0');
    }
    EQ::Net::DynamicPacket p;
    p.Resize(body.size());
    p.PutData(0, const_cast<char*>(body.data()), body.size());
    return p;
}

// T282: processItemPacket() is only ever handed a packet that still carries
// its 2-byte OPCODE -- ZoneComponent::DispatchZonePacket() passes the dispatched
// packet straight through, and the loot path forwards the same one. This
// helper used to omit it, which hid a 2-byte misalignment in the parse
// (instance field 0, the stack count, was the only casualty).
constexpr uint16_t kOpItemPacket = 0x3397;  // HC_OP_ItemPacket (client/eq.h)

EQ::Net::DynamicPacket ItemPacketPacket(const std::string& item,
                                        uint32_t packetType = EQT::ITEM_PACKET_INVENTORY) {
    EQ::Net::DynamicPacket p;
    p.Resize(2 + 4 + item.size() + 1);
    p.PutUInt16(0, kOpItemPacket);
    p.PutUInt32(2, packetType);
    p.PutData(2 + 4, const_cast<char*>(item.data()), item.size());
    p.PutUInt8(2 + 4 + item.size(), 0);
    return p;
}

TEST_F(EqCommandsEquipTest, CharInventory_SingleCursorBag_ContentsLandInCursorRange) {
    // Found while writing the two-bag case: the server serialises a cursor
    // item at ITS cursor slot (33), and parseSubItems() fed that raw slot to
    // calcContainerSlotId(), which knows only the client cursor (30) -- so
    // every cursor bag's contents collapsed onto SLOT_INVALID and were lost
    // at zone-in, one bag or many. Pin the one-bag case on its own.
    auto* inv = eq.GetInventoryManager();
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{2, BuildPlainItemString(2, 7301, "Ax")}}),
    });
    inv->processCharInventory(pkt);

    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(SLOT_INVALID), nullptr);
}

TEST_F(EqCommandsEquipTest, CharInventory_TwoQueuedCursorBags_EachKeepsItsOwnContents) {
    auto* inv = eq.GetInventoryManager();
    // Server cursor queue: bag A{Ax @0} in front, bag B{Bp @0, Bq @1} behind.
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{0, BuildPlainItemString(0, 7301, "Ax")}}),
        BuildBagString(SERVER_CURSOR_SLOT, 7211, "Bag B", 4, {{0, BuildPlainItemString(0, 7401, "Bp")},
                                                             {1, BuildPlainItemString(1, 7402, "Bq")}}),
    });
    inv->processCharInventory(pkt);

    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    ASSERT_NE(inv->getCursorItem(), nullptr);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7210u);
    // Front bag's contents are the cursor range; B's must NOT have clobbered them.
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    // Place A into 22: Ax goes with it; B becomes the front WITH its contents.
    ASSERT_TRUE(inv->placeItem(22));
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7402u);

    // Place B into 23: Bp/Bq follow; nothing left in the cursor range.
    ASSERT_TRUE(inv->placeItem(23));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7402u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    // A's content stayed under A.
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, ItemPacket_BagQueuedBehindCursorFront_KeepsContentsUntilPromoted) {
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, CURSOR_SLOT, 7001, "Rusty Mace", EQUIPSLOT_PRIMARY);  // front, from server
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);

    // A bag with one item arrives on the cursor behind the mace (OP_ItemPacket).
    auto pkt = ItemPacketPacket(
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{1, BuildPlainItemString(1, 7301, "Ax")}}));
    EXPECT_TRUE(isCursorSlot(inv->processItemPacket(pkt)));  // returns the wire slot (33)
    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7001u);
    // The mace is not a bag; the cursor range stays EMPTY while it is the front.
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    ASSERT_TRUE(inv->placeItem(22));  // mace -> 22; bag promoted with Ax
    EXPECT_EQ(inv->getCursorItem()->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7301u);

    ASSERT_TRUE(inv->placeItem(2000));  // bag -> bank 2000 with Ax
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);
}

TEST_F(EqCommandsEquipTest, ItemPacket_BankBagWithContents_ContentsLandInBankRange) {
    // Found in the T255 live leg (run 2): at zone-in eqemu delivers BANK
    // items as individual OP_ItemPackets, each bag serialised WITH its
    // contents -- not via CharInventory. processItemPacket() never parsed
    // sub-items, so a bank bag always loaded empty client-side and a later
    // bank->general move had nothing to relocate (the server still moved
    // them). An ItemPacket for a bag is authoritative for that bag's whole
    // contents: any stale range under that parent is replaced, never merged.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, calcBankBagSlotId(2000, 0), 7999, "Stale", 0);  // must be replaced, not kept
    auto pkt = ItemPacketPacket(
        BuildBagString(2000, 7210, "Large Bag", 6, {{0, BuildPlainItemString(0, 7301, "Rusty Short Sword")},
                                                    {1, BuildPlainItemString(1, 7302, "Rusty Long Sword")}}));
    EXPECT_EQ(inv->processItemPacket(pkt), 2000);
    ASSERT_NE(inv->getItem(2000), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7302u);

    eq.Commands().RequestEquip("2000 23");
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
}

TEST_F(EqCommandsEquipTest, CursorSwap_DisplacedBagOntoOccupiedQueue_QueuedBagContentsSurvive) {
    // Queue: A{Ax} front, B{Bp} behind (CharInventory). A dropped onto bag
    // C{Cz} at 22: C is displaced to the front with Cz; B still waits with
    // Bp; when C is placed and B promoted, Bp is intact.
    auto* inv = eq.GetInventoryManager();
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{0, BuildPlainItemString(0, 7301, "Ax")}}),
        BuildBagString(SERVER_CURSOR_SLOT, 7211, "Bag B", 4, {{0, BuildPlainItemString(0, 7401, "Bp")}}),
    });
    inv->processCharInventory(pkt);
    SeedBag(eq, 22, 7212, "Bag C", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7501, "Cz", 0);

    ASSERT_TRUE(inv->placeItem(22));  // A -> 22 (Ax), C -> cursor front (Cz), B behind
    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7212u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7501u);

    ASSERT_TRUE(inv->placeItem(23));  // C -> 23 (Cz); B promoted (Bp)
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7501u);
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
}

// ---------------------------------------------------------------------------
// T259 -- trade-window slots (3000-3007) are bag parents too. Server model
// (eqemu zone/trading.cpp): a bag reaches a trade slot ONLY from the cursor
// (zone/inventory.cpp Client::SwapItem "Trade with non-cursor item" -> Kick);
// Trade::AddEntity PutItemInInventory()s the whole ItemInstance, contents
// included, addressed on the wire as 3031 + (trade-3000)*10 + idx
// (common/patches/titanium_limits.h TRADE_BAGS_BEGIN). On completion the
// giver gets NOTHING for its trade slots (Client::FinishTrade ->
// DeleteItemInInventory(trade_slot) without client_update) and the receiver
// gets one ItemPacketTrade for the bag WITH contents at a free general slot
// (PutItemInInventory(free_slot, inst, true)). On cancel the canceller's own
// items come back the same way (FinishTrade(this)); the OTHER side's items
// come back only after that client echoes OP_CancelTrade (Handle_OP_
// CancelTrade forwards the packet and FinishTrade()s only the sender).
// ---------------------------------------------------------------------------

void ActivateTradeWith(EverQuest& eq, uint32_t partnerSpawnId, const std::string& partnerName) {
    EQT::TradeRequest_Struct req{};
    req.target_spawn_id = 0;
    req.from_spawn_id = partnerSpawnId;
    eq.GetTradeManager()->handleTradeRequest(req, partnerName);
    eq.GetTradeManager()->acceptTradeRequest();
    ASSERT_EQ(eq.GetTradeManager()->getState(), TradeState::Active);
}

TEST_F(EqCommandsEquipTest, CursorMove_BagIntoTradeSlot_ContentsLandInTradeRange) {
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    (void)bridge.drainIntents();

    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN)->itemId, 7210u);
    // Wire numbering: 3031/3032 for trade slot 3000.
    ASSERT_NE(inv->getItem(3031), nullptr);
    ASSERT_NE(inv->getItem(3032), nullptr);
    EXPECT_EQ(inv->getItem(3031)->itemId, 7301u);
    EXPECT_EQ(inv->getItem(3032)->itemId, 7302u);
    EXPECT_EQ(calcContainerSlotId(TRADE_BEGIN, 0), 3031);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);

    // Exactly the two MoveItems the server needs (22->30, 30->3000), no
    // packet for the contents (they travel inside the bag server-side).
    auto intents = bridge.drainIntents();
    int moves = 0;
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) moves++;
    }
    EXPECT_EQ(moves, 2);
}

TEST_F(EqCommandsEquipTest, DirectMove_GeneralBagStraightIntoTradeSlot_RefusedBeforeSend) {
    // eqemu Client::SwapItem: a MoveItem into a trade slot whose source is
    // not the cursor is answered with Kick("Trade with non-cursor item").
    // Pre-flight refuse it locally, nothing mutated, nothing sent.
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    (void)bridge.drainEvents();
    (void)bridge.drainIntents();

    eq.Commands().RequestEquip("23 3000");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("cursor"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
}

TEST_F(EqCommandsEquipTest, TradeFinish_OwnTradeSlotsAndContentsClearedCleanly) {
    // Giver's side: the server deletes the trade slots without any client
    // update and sends OP_FinishTrade. The client must drop the bag AND its
    // 3031+ range itself, leaving nothing behind anywhere.
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);
    (void)bridge.drainEvents();

    eq.GetTradeManager()->handleFinishTrade();

    // B896 (#212): a completed trade prints NO chat line on either side
    // (MEASURED, review/UIGAP-ORACLE/212/trade.tsv row 7); T259's "You give
    // Large Bag to Buddy." summary was fable's own and is gone.
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
    EXPECT_EQ(inv->getItem(3032), nullptr);
    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_FALSE(inv->hasCursorItem());
}

TEST_F(EqCommandsEquipTest, TradeCancelledByPartner_EchoesCancel_ClearsRange_ReturnedBagKeepsContents) {
    auto* inv = eq.GetInventoryManager();
    int cancelsSent = 0;
    eq.GetTradeManager()->setSendCancelTrade([&](const EQT::CancelTrade_Struct&) { cancelsSent++; });
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);
    ASSERT_EQ(cancelsSent, 0);

    // Partner cancelled: the server only returns the CANCELLER's items on its
    // own; ours come back after we echo OP_CancelTrade (eqemu
    // Handle_OP_CancelTrade -> FinishTrade(this) for the sender only).
    EQT::CancelTrade_Struct cancel{};
    cancel.spawn_id = 555;
    cancel.action = 7;
    eq.GetTradeManager()->handleCancelTrade(cancel);

    EXPECT_EQ(cancelsSent, 1);
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
    EXPECT_EQ(inv->getItem(3032), nullptr);

    // A second OP_CancelTrade (the server forwards our echo back) is ignored:
    // no second echo, no loop.
    eq.GetTradeManager()->handleCancelTrade(cancel);
    EXPECT_EQ(cancelsSent, 1);

    // The returned bag arrives as ItemPacketTrade at a free general slot
    // (server 23, already translated to Titanium 22 on the wire by
    // titanium.cpp), serialised WITH its contents; T255's ItemPacket
    // sub-item parsing lands them in that slot's range.
    auto pkt = ItemPacketPacket(
        BuildBagString(22, 7210, "Large Bag", 6, {{0, BuildPlainItemString(0, 7301, "Rusty Short Sword")},
                                                  {1, BuildPlainItemString(1, 7302, "Rusty Long Sword")}}));
    EXPECT_EQ(inv->processItemPacket(pkt), 22);
    ASSERT_NE(inv->getItem(22), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7302u);
}

TEST_F(EqCommandsEquipTest, LocalCancel_ClearsOwnTradeRange) {
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);

    eq.GetTradeManager()->cancelTrade();

    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
}

}  // namespace
