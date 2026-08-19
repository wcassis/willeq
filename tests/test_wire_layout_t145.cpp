// T145 -- wire-layout tests for the seven structs the wire-rot sweep found
// wrong, plus wire-shape tests for the four double-opcode-prefix senders.
//
// WHY offsetof AND NOT JUST sizeof:
// every one of these structs already had a *passing* static_assert on its
// size. A size assert catches neither field drift nor a wrong-but-self-
// consistent total -- that is exactly how Resurrect_Struct survived at 224
// until T134 found it live. So every named field here is pinned to its
// eqemu-derived byte offset, not just the total.
//
// GROUND TRUTH, and how each struct was arbitrated:
// the disambiguation rule this sweep established is that
// `common/patches/titanium.cpp` is the arbiter -- if an opcode has an
// ENCODE/DECODE there, the `Titanium::structs::` version is the wire format;
// otherwise the base `common/eq_packet_structs.h` struct goes on the wire raw
// and the zone handler's own size check is the gate. Each block below records
// which side of that rule it landed on, with file:line.
//
// WHAT THESE TESTS DO NOT PROVE:
// these are static layout assertions against eqemu's source. They are NOT
// wire observation. T145 ran without an eqemu lock and captured nothing.
// Confirming these packets are accepted by a live server is T146's job.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "common/packet_structs.h"
#include "common/net/packet.h"

// Opcode values, copied from `HC_OP_*` in include/client/eq.h. Duplicated
// deliberately rather than including eq.h: this is a layout/shape test and
// must not drag the whole client in to link.
namespace {
constexpr uint16_t kOpCamp           = 0x78c1;  // eq.h:321
constexpr uint16_t kOpLogout         = 0x61ff;  // eq.h:322
constexpr uint16_t kOpWhoAllRequest  = 0x5cdd;  // eq.h:329
constexpr uint16_t kOpInspectRequest = 0x775d;  // eq.h:332
} // namespace

// ============================================================================
// Part B -- struct layouts
// ============================================================================

// eqemu Titanium::structs::FaceChange_Struct
// (common/patches/titanium_structs.h:1860). Arbitrated by
// DECODE(OP_FaceChange) (common/patches/titanium.cpp:2779), which uses
// DECODE_LENGTH_EXACT -- a mismatch rewrites the opcode to OP_Unknown
// (common/patches/ss_define.h:165-171) and the packet is discarded before any
// handler sees it. The 24-byte base struct is NOT the Titanium wire format.
TEST(T145WireLayout, FaceChange_Struct) {
    EXPECT_EQ(sizeof(EQT::FaceChange_Struct), 7u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, haircolor), 0u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, beardcolor), 1u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, eyecolor1), 2u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, eyecolor2), 3u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, hairstyle), 4u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, beard), 5u);
    EXPECT_EQ(offsetof(EQT::FaceChange_Struct, face), 6u);
}

// eqemu EnvDamage2_Struct -- base common/eq_packet_structs.h:3020 and
// Titanium common/patches/titanium_structs.h:2251 are byte-identical, and
// titanium.cpp has no ENCODE/DECODE for OP_EnvDamage, so the raw struct is the
// wire format and Handle_OP_EnvDamage's exact-size check
// (zone/client_packet.cpp:6301) is the gate.
//
// The old layout was wrong in four independent ways at once, which is why a
// size-only assert at 32 could sit there passing: `damage` was uint16 at 4
// rather than uint32 at 6, `dmgtype` was at 16 rather than 22, and `constant`
// was uint8 at 31 rather than uint16 at 27.
TEST(T145WireLayout, EnvDamage2_Struct) {
    EXPECT_EQ(sizeof(EQT::EnvDamage2_Struct), 31u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, id), 0u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, unknown4), 4u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, damage), 6u);
    EXPECT_EQ(sizeof(EQT::EnvDamage2_Struct{}.damage), 4u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, unknown10), 10u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, dmgtype), 22u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, unknown2), 23u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, constant), 27u);
    EXPECT_EQ(sizeof(EQT::EnvDamage2_Struct{}.constant), 2u);
    EXPECT_EQ(offsetof(EQT::EnvDamage2_Struct, unknown29), 29u);
}

// eqemu GMKick_Struct -- base common/eq_packet_structs.h:2766 and Titanium
// common/patches/titanium_structs.h:2042 are byte-identical; no ENCODE/DECODE
// in titanium.cpp, so raw. eqemu's GMKill_Struct has the identical three
// fields, so one struct here serves both opcodes -- checked, not assumed.
//
// Handle_OP_GMKick (zone/client_packet.cpp:6729) does a BARE `return` on a
// size mismatch with no log line at all, so the old 132-byte packet produced
// zero server-side evidence.
TEST(T145WireLayout, GMKick_Struct) {
    EXPECT_EQ(sizeof(EQT::GMKick_Struct), 129u);
    EXPECT_EQ(offsetof(EQT::GMKick_Struct, name), 0u);
    EXPECT_EQ(offsetof(EQT::GMKick_Struct, gmname), 64u);
    EXPECT_EQ(offsetof(EQT::GMKick_Struct, unknown), 128u);
}

// eqemu ControlBoat_Struct (common/eq_packet_structs.h:5327); no
// ENCODE/DECODE in titanium.cpp, so raw, gated by Handle_OP_ControlBoat
// (zone/client_packet.cpp:5421) which logs a visible
// "Wrong size: OP_ControlBoat, size=[20], expected [8]" on mismatch.
//
// This one was wrong in SEMANTICS as well as size: the old struct had a
// `heading` float and a `type` control byte, neither of which exists on the
// wire. The real payload is a boat entity id plus a single take/release flag.
//
// Note: eqemu's own offset comment marks `unknown` as /*007*/, but under
// #pragma pack(1) a 1-byte bool at offset 4 puts it at 5. The total, 8, is
// what the handler checks and both agree on that.
TEST(T145WireLayout, ControlBoat_Struct) {
    EXPECT_EQ(sizeof(EQT::ControlBoat_Struct), 8u);
    EXPECT_EQ(offsetof(EQT::ControlBoat_Struct, boatid), 0u);
    EXPECT_EQ(offsetof(EQT::ControlBoat_Struct, take_control), 4u);
    EXPECT_EQ(sizeof(EQT::ControlBoat_Struct{}.take_control), 1u);
    EXPECT_EQ(offsetof(EQT::ControlBoat_Struct, unknown), 5u);
}

// eqemu PopupResponse_Struct (common/eq_packet_structs.h:1903); no
// ENCODE/DECODE in titanium.cpp, so raw, gated by Handle_OP_PopupResponse
// (zone/client_packet.cpp:11976).
//
// eqemu has exactly two uint32 fields and NO third field -- the button index
// is not sent; the server dispatches on the popup id alone. Our old
// `response` field was fictional.
TEST(T145WireLayout, PopupResponse_Struct) {
    EXPECT_EQ(sizeof(EQT::PopupResponse_Struct), 8u);
    EXPECT_EQ(offsetof(EQT::PopupResponse_Struct, sender), 0u);
    EXPECT_EQ(offsetof(EQT::PopupResponse_Struct, popup_id), 4u);
}

// eqemu DisciplineTimer_Struct (common/eq_packet_structs.h:5418) -- TimerID,
// Duration, Unknown08. Low severity today (the only consumer is a read whose
// guard is `<`, and both consumed fields sit at correct offsets), but a live
// bug the moment this struct is used to send.
TEST(T145WireLayout, DisciplineTimer_Struct) {
    EXPECT_EQ(sizeof(EQT::DisciplineTimer_Struct), 12u);
    EXPECT_EQ(offsetof(EQT::DisciplineTimer_Struct, timer_id), 0u);
    EXPECT_EQ(offsetof(EQT::DisciplineTimer_Struct, timer_value), 4u);
    EXPECT_EQ(offsetof(EQT::DisciplineTimer_Struct, unknown08), 8u);
}

// The seventh "struct" in the batch is a NON-change: the merchant pair.
//
// Our Merchant_Purchase_Struct/Merchant_Sell_Struct names are inverted
// relative to eqemu's, and that must stay that way, because eqemu's own
// naming is counterintuitive:
//   Handle_OP_ShopPlayerBuy  (zone/client_packet.cpp:14026) expects eqemu's
//     Merchant_Sell_Struct     = 24 bytes = our Merchant_Purchase_Struct
//   Handle_OP_ShopPlayerSell (zone/client_packet.cpp:14274) expects eqemu's
//     Merchant_Purchase_Struct = 16 bytes = our Merchant_Sell_Struct
// eq_merchant.cpp sends our 24-byte struct on HC_OP_ShopPlayerBuy and our
// 16-byte struct on HC_OP_ShopPlayerSell, so both directions are correct
// today. A future cleanup that "fixes" the names to agree with eqemu breaks
// both merchant directions at once -- this test exists to fail loudly if the
// sizes ever get swapped to match the names.
TEST(T145WireLayout, MerchantPair_SizesAreInvertedRelativeToEqemuNames_ByDesign) {
    EXPECT_EQ(sizeof(EQT::Merchant_Purchase_Struct), 24u)
        << "our *Purchase* struct is what OP_ShopPlayerBUY sends (eqemu calls "
           "this shape Merchant_Sell_Struct); see packet_structs.h";
    EXPECT_EQ(sizeof(EQT::Merchant_Sell_Struct), 16u)
        << "our *Sell* struct is what OP_ShopPlayerSELL sends (eqemu calls "
           "this shape Merchant_Purchase_Struct); see packet_structs.h";

    EXPECT_EQ(offsetof(EQT::Merchant_Purchase_Struct, npc_id), 0u);
    EXPECT_EQ(offsetof(EQT::Merchant_Purchase_Struct, player_id), 4u);
    EXPECT_EQ(offsetof(EQT::Merchant_Purchase_Struct, itemslot), 8u);
    EXPECT_EQ(offsetof(EQT::Merchant_Purchase_Struct, quantity), 16u);

    EXPECT_EQ(offsetof(EQT::Merchant_Sell_Struct, npc_id), 0u);
    EXPECT_EQ(offsetof(EQT::Merchant_Sell_Struct, itemslot), 4u);
    EXPECT_EQ(offsetof(EQT::Merchant_Sell_Struct, quantity), 8u);
}

// ============================================================================
// Part A -- wire shape of the four double-opcode-prefix senders
// ============================================================================
//
// EverQuest::QueuePacket(uint16_t opcode, DynamicPacket*) (eq.cpp) ALWAYS
// prepends the opcode itself, so its callers must hand it a BODY-ONLY packet.
// Four callers in eq_player_info.cpp also wrote the opcode into the body,
// producing a packet two bytes too long with the opcode duplicated at the
// front of the payload. This is the same defect class T133 found across
// eq_guild.cpp's senders and T134 found in SendRezzAnswer.
//
// These tests reconstruct the body each fixed sender now builds and assert
// (a) the body length, (b) the framed length QueuePacket will produce, both
// against sizes taken from the eqemu handlers rather than from our own
// sizeof, and (c) that the body does NOT begin with the opcode.
//
// HONESTY BOUND: this pins the shape the sender constructs. It is not proof
// that the server accepted it -- nothing here touches a socket. T146 closes
// that leg.

namespace {

// The framing QueuePacket(opcode, body) applies.
size_t FramedLength(size_t body_length) { return body_length + 2; }

bool BodyStartsWithOpcode(const uint8_t* body, size_t len, uint16_t opcode) {
    if (len < 2) {
        return false;
    }
    uint16_t leading = static_cast<uint16_t>(body[0]) |
                       static_cast<uint16_t>(body[1]) << 8;
    return leading == opcode;
}

} // namespace

// Handle_OP_Camp / Handle_OP_Logout (zone/client_packet.cpp:4269, :10264) take
// no payload at all. The old senders wrote the opcode into an otherwise-empty
// DynamicPacket -- Packet::PutInt16 silently auto-resizes
// (src/common/net/packet.cpp) -- so the "empty packet" was 2 bytes of body and
// 4 bytes on the wire. Correct shape: zero-length body, matching
// SendGetGuildMOTD (eq_guild.cpp).
TEST(T145SenderWireShape, Camp_IsAZeroLengthBody) {
    EQ::Net::DynamicPacket packet;  // exactly what SendCamp() now builds
    EXPECT_EQ(packet.Length(), 0u);
    EXPECT_EQ(FramedLength(packet.Length()), 2u);

    // Pin the regression: the OLD sender's one extra line produced a 2-byte
    // body that begins with the opcode, i.e. 4 bytes on the wire.
    EQ::Net::DynamicPacket old_shape;
    old_shape.PutUInt16(0, kOpCamp);
    EXPECT_EQ(old_shape.Length(), 2u) << "PutUInt16 silently auto-resizes";
    EXPECT_EQ(FramedLength(old_shape.Length()), 4u);
    EXPECT_TRUE(BodyStartsWithOpcode(
        static_cast<const uint8_t*>(old_shape.Data()), old_shape.Length(), kOpCamp));
}

TEST(T145SenderWireShape, Logout_IsAZeroLengthBody) {
    EQ::Net::DynamicPacket packet;  // exactly what SendLogout() now builds
    EXPECT_EQ(packet.Length(), 0u);
    EXPECT_EQ(FramedLength(packet.Length()), 2u);

    EQ::Net::DynamicPacket old_shape;
    old_shape.PutUInt16(0, kOpLogout);
    EXPECT_EQ(FramedLength(old_shape.Length()), 4u);
    EXPECT_TRUE(BodyStartsWithOpcode(
        static_cast<const uint8_t*>(old_shape.Data()), old_shape.Length(), kOpLogout));
}

// Handle_OP_WhoAllRequest (zone/client_packet.cpp:15999) is preceded by
// DECODE(OP_WhoAllRequest) (common/patches/titanium.cpp:3312), which does
// DECODE_LENGTH_EXACT(structs::Who_All_Struct). eqemu's Titanium
// Who_All_Struct (titanium_structs.h:1939) is 64 + 6*4 + 64 = 152 bytes; its
// "// 76 length total" comment is stale. Our struct already matched at 152 --
// only the sender was malformed, at 154.
TEST(T145SenderWireShape, WhoAllRequest_BodyIsStructOnly) {
    constexpr size_t kEqemuWhoAllSize = 152;  // titanium_structs.h:1939
    ASSERT_EQ(sizeof(EQT::Who_All_Struct), kEqemuWhoAllSize);

    EQT::Who_All_Struct who;
    memset(&who, 0, sizeof(who));
    who.wrace = 0xFFFFFFFFu;

    EQ::Net::DynamicPacket packet;  // exactly what SendWhoAllRequest() now builds
    packet.Resize(sizeof(EQT::Who_All_Struct));
    packet.PutData(0, &who, sizeof(EQT::Who_All_Struct));

    EXPECT_EQ(packet.Length(), kEqemuWhoAllSize);
    EXPECT_EQ(FramedLength(packet.Length()), kEqemuWhoAllSize + 2);
    EXPECT_FALSE(BodyStartsWithOpcode(static_cast<const uint8_t*>(packet.Data()),
                                      packet.Length(), kOpWhoAllRequest))
        << "body must not repeat the opcode -- QueuePacket prepends it";
}

// Handle_OP_InspectRequest (zone/client_packet.cpp:9073) is preceded by
// DECODE(OP_InspectRequest) (common/patches/titanium.cpp:2848), also
// DECODE_LENGTH_EXACT. eqemu's Inspect_Struct is two uint32s = 8 bytes; ours
// matched -- only the sender was malformed, at 10.
TEST(T145SenderWireShape, InspectRequest_BodyIsStructOnly) {
    constexpr size_t kEqemuInspectSize = 8;  // titanium_structs.h:1971
    ASSERT_EQ(sizeof(EQT::Inspect_Struct), kEqemuInspectSize);

    EQT::Inspect_Struct inspect;
    inspect.TargetID = 0x11223344u;
    inspect.PlayerID = 0x55667788u;

    EQ::Net::DynamicPacket packet;  // exactly what SendInspectRequest() now builds
    packet.Resize(sizeof(EQT::Inspect_Struct));
    packet.PutData(0, &inspect, sizeof(EQT::Inspect_Struct));

    EXPECT_EQ(packet.Length(), kEqemuInspectSize);
    EXPECT_EQ(FramedLength(packet.Length()), kEqemuInspectSize + 2);
    EXPECT_FALSE(BodyStartsWithOpcode(static_cast<const uint8_t*>(packet.Data()),
                                      packet.Length(), kOpInspectRequest))
        << "body must not repeat the opcode -- QueuePacket prepends it";

    // The first four bytes must be TargetID, not the opcode.
    uint32_t first = 0;
    memcpy(&first, packet.Data(), sizeof(first));
    EXPECT_EQ(first, 0x11223344u);
}
