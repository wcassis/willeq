/*
 * test_skill_packet_bodies.cpp -- T152
 *
 * Pins the two skill-opcode bodies this client had to build from scratch
 * (OP_Disarm, OP_PickPocket) to the eqemu structs that decode them, field by
 * field at named offsets rather than by a bare sizeof().
 *
 * WHAT THE AUTHORITY IS, AND WHAT IT IS NOT.
 *
 *   T147's tests/captured_packets.h convention pins bytes OBSERVED ON A LIVE
 *   WIRE. There is no capture for either of these opcodes -- T152 held no
 *   eqemu lock and neither packet has ever been sent by this client with a
 *   body. So the authority here is eqemu SOURCE:
 *
 *     Disarm_Struct      common/eq_packet_structs.h:2001   (16 bytes)
 *     PickPocket_Struct  common/eq_packet_structs.h:3556   (18 bytes, and the
 *                                                           header says so)
 *
 *   Both headers are `#pragma pack(1)` and neither opcode has an
 *   ENCODE/DECODE in common/patches/titanium.cpp, so the base struct goes on
 *   the wire raw and the offsets are byte-exact. That is a weaker authority
 *   than a capture and is flagged as such in T152's Review; the follow-up wire
 *   unit replaces it.
 *
 * If one of these fails, the struct changed. Do not "fix" the test.
 */

#include <gtest/gtest.h>

#include "client/skill/skill_packet_bodies.h"

#include <cstdint>
#include <cstring>

namespace {

uint32_t U32(const uint8_t* p, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

uint16_t U16(const uint8_t* p, size_t off) {
    uint16_t v = 0;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// OP_Disarm
// ---------------------------------------------------------------------------

TEST(SkillPacketBodies, DisarmBodyIsSixteenBytes) {
    // sizeof(Disarm_Struct) == 4 uint32s. Handle_OP_Disarm gates on this with
    // an early return (zone/client_packet.cpp:5754), so a wrong length is a
    // silent drop, not an error.
    auto body = EQ::skill::MakeDisarmBody(1, 2, 3);
    EXPECT_EQ(body.size(), 16u);
    EXPECT_EQ(EQ::skill::kDisarmBodyLen, 16u);
}

TEST(SkillPacketBodies, DisarmFieldsLandAtEqemuOffsets) {
    constexpr uint32_t kSource = 0x1234;
    constexpr uint32_t kTarget = 0x5678;
    constexpr uint32_t kSkill  = 137;

    auto body = EQ::skill::MakeDisarmBody(kSource, kTarget, kSkill);
    const uint8_t* b = body.data();

    // /*000*/ uint32 source -- MUST be our own entity id; a mismatch makes the
    // server file a POSSIBLE_HACK player-event (client_packet.cpp:5775).
    EXPECT_EQ(U32(b, EQ::skill::kDisarmOffSource), kSource);
    EXPECT_EQ(EQ::skill::kDisarmOffSource, 0u);

    // /*004*/ uint32 target
    EXPECT_EQ(U32(b, EQ::skill::kDisarmOffTarget), kTarget);
    EXPECT_EQ(EQ::skill::kDisarmOffTarget, 4u);

    // /*008*/ uint32 skill -- compared for equality against the server's own
    // GetSkill(SkillDisarm) (client_packet.cpp:5804). Wrong value == silent
    // DISARM_NO_TARGET, not a size error.
    EXPECT_EQ(U32(b, EQ::skill::kDisarmOffSkill), kSkill);
    EXPECT_EQ(EQ::skill::kDisarmOffSkill, 8u);

    // /*012*/ uint32 unknown -- eqemu never reads it; pinned to zero rather
    // than left to whatever the allocator had.
    EXPECT_EQ(U32(b, EQ::skill::kDisarmOffUnknown), 0u);
    EXPECT_EQ(EQ::skill::kDisarmOffUnknown, 12u);
}

TEST(SkillPacketBodies, DisarmIsLittleEndian) {
    auto body = EQ::skill::MakeDisarmBody(0x04030201u, 0, 0);
    EXPECT_EQ(body[0], 0x01);
    EXPECT_EQ(body[1], 0x02);
    EXPECT_EQ(body[2], 0x03);
    EXPECT_EQ(body[3], 0x04);
}

// ---------------------------------------------------------------------------
// OP_PickPocket
// ---------------------------------------------------------------------------

TEST(SkillPacketBodies, PickPocketBodyIsEighteenBytes) {
    // 4 + 4 + 2 + 1 + 1 + 4 + 2. eqemu's own header comments the struct
    // "// Size 18".
    auto body = EQ::skill::MakePickPocketBody(1, 2, 3);
    EXPECT_EQ(body.size(), 18u);
    EXPECT_EQ(EQ::skill::kPickPocketBodyLen, 18u);
}

TEST(SkillPacketBodies, PickPocketFieldsLandAtEqemuOffsets) {
    constexpr uint32_t kTo    = 0x0BAD;
    constexpr uint32_t kFrom  = 0x0F00;
    constexpr uint16_t kSkill = 55;

    auto body = EQ::skill::MakePickPocketBody(kTo, kFrom, kSkill);
    const uint8_t* b = body.data();

    // /*000*/ uint32 to -- the ONLY field Handle_OP_PickPocket reads
    // (client_packet.cpp:11932). Everything else is response payload.
    EXPECT_EQ(U32(b, EQ::skill::kPickPocketOffTo), kTo);
    EXPECT_EQ(EQ::skill::kPickPocketOffTo, 0u);

    // /*004*/ uint32 from
    EXPECT_EQ(U32(b, EQ::skill::kPickPocketOffFrom), kFrom);
    EXPECT_EQ(EQ::skill::kPickPocketOffFrom, 4u);

    // /*008*/ uint16 myskill
    EXPECT_EQ(U16(b, EQ::skill::kPickPocketOffSkill), kSkill);
    EXPECT_EQ(EQ::skill::kPickPocketOffSkill, 8u);
}

TEST(SkillPacketBodies, PickPocketResponseFieldsAreZeroNotGuessed) {
    // type / unknown1 / coin / lastsix are what the SERVER fills in on the way
    // back. Sending a guess there would be inventing wire content, so they are
    // zeroed. Pinned so a future edit cannot quietly start guessing.
    auto body = EQ::skill::MakePickPocketBody(0xFFFF, 0xFFFF, 0xFFFF);
    const uint8_t* b = body.data();

    EXPECT_EQ(b[EQ::skill::kPickPocketOffType], 0);        // /*010*/ type
    EXPECT_EQ(b[EQ::skill::kPickPocketOffType + 1], 0);    // /*011*/ unknown1
    EXPECT_EQ(U32(b, EQ::skill::kPickPocketOffCoin), 0u);  // /*012*/ coin
    EXPECT_EQ(b[EQ::skill::kPickPocketOffLast6], 0);       // /*016*/ lastsix[0]
    EXPECT_EQ(b[EQ::skill::kPickPocketOffLast6 + 1], 0);   // /*017*/ lastsix[1]

    EXPECT_EQ(EQ::skill::kPickPocketOffType, 10u);
    EXPECT_EQ(EQ::skill::kPickPocketOffCoin, 12u);
    EXPECT_EQ(EQ::skill::kPickPocketOffLast6, 16u);
}

// ---------------------------------------------------------------------------
// The poison item-type constant the ApplyPoison route depends on.
// ---------------------------------------------------------------------------

TEST(SkillPacketBodies, PoisonItemTypeMatchesEqemu) {
    // EQ::item::ItemTypePoison is the 43rd entry (index 42) of the ItemType
    // enum in common/item_data.h:57-100. SkillManager::findPoisonInventorySlot
    // compares ItemInstance::skillType against it -- that field is parsed from
    // serialized item field 56, which titanium.cpp:3429 writes as
    // `item->ItemType`.
    EXPECT_EQ(EQ::skill::kItemTypePoison, 42);
}
