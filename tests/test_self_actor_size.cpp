// B1172 (#491) fix leg, review note 7955 finding 3: ONE number for our own
// size. The official client keeps one size per spawn, EQPlayer+0x23c: the
// Spawn_Struct's size word (+75, copied at 0x4816fd) or, when that is not
// > 0, the race/gender default written back over it (FUN_0047fa9e,
// 0x47fb07..0x47fb30). Its actor scale and the z it transmits both follow
// that number. MEASURED at the qeynos2 spot (-74, 428), collision ground 0.00
// (review/T311/legA/README.md, its `#loc` table):
//   * T311 Leg A, Oraclebravo (Barbarian), S->C OP_ZoneEntry size word 0.000,
//     no AT_Size: transmits z 4.376 = 0.625 x 7, the Barbarian default.
//   * B1130 r51 / B1118 oracle, Testcaster (Dark Elf), size word 0.000:
//     transmits z 3.128 = 0.625 x 5, the Dark Elf default; fable before this
//     leg sent 3.750 = 0.625 x 6 (B1118 fable_A, B1142 fable_N93).
// (refactor/B1172/README.md sec. 3; scripts refactor/B1172/scripts/
// t311_zoneentry_sizes.py and pcap_self_z.py.)

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/graphics/actor_scale.h"
#include "client/position_frames.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstddef>
#include <cstring>
#include <vector>

using namespace zonetest;
using EQT::Graphics::clientActorScale;
using EQT::Graphics::clientResolvedActorSize;

namespace {

constexpr uint16_t kSelfId = 311;
constexpr uint32_t kRaceDarkElf = 6;
constexpr uint32_t kGenderFemale = 1;

// The measured transmitted z above ground 0.00 (see the header) and the
// tolerance both captures share with the rule (+0.001..+0.003, T311).
constexpr float kOracleDarkElfWireZ = 3.128f;
constexpr float kT311BarbarianWireZ = 4.376f;
constexpr float kT311HumanWireZ = 3.751f;
constexpr float kWireTol = 0.004f;

// A zero-filled PlayerProfile (opcode + the 19,588-byte struct): gender at
// +4, race at +8 (zone_entry.cpp's reads), our spawn id at +14384.
std::vector<uint8_t> Profile(uint32_t race = kRaceDarkElf, uint32_t gender = kGenderFemale) {
    return BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 4, gender);
        p.PutUInt32(2 + 8, race);
        p.PutUInt32(2 + 14384, kSelfId);
    });
}

// The server's OP_ZoneEntry: opcode + our Spawn_Struct.
std::vector<uint8_t> ServerZoneEntry(float size, uint32_t spawn_id = kSelfId,
                                     uint32_t race = kRaceDarkElf, uint8_t gender = kGenderFemale) {
    return BuildPayload(2 + sizeof(EQT::Spawn_Struct), [&](EQ::Net::DynamicPacket& p) {
        std::memcpy(static_cast<char*>(p.Data()) + 2 + offsetof(EQT::Spawn_Struct, size), &size,
                    sizeof(size));
        p.PutUInt32(2 + offsetof(EQT::Spawn_Struct, race), race);
        p.PutUInt8(2 + offsetof(EQT::Spawn_Struct, gender), gender);
        p.PutUInt32(2 + offsetof(EQT::Spawn_Struct, spawnId), spawn_id);
    });
}

void Prep(EverQuest& eq) {
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);
}

float EntitySize(const EverQuest& eq) { return eq.GetEntities().at(kSelfId).size; }

}  // namespace

// FUN_0047fa9e's write-back: a size word not > 0 becomes the default.
TEST(SelfActorSize, TheResolvedSizeIsTheWrittenBackDefault) {
    EXPECT_FLOAT_EQ(clientResolvedActorSize(6, 1, 0.0f), 5.0f);   // Dark Elf
    EXPECT_FLOAT_EQ(clientResolvedActorSize(2, 0, 0.0f), 7.0f);   // Barbarian
    EXPECT_FLOAT_EQ(clientResolvedActorSize(1, 0, 0.0f), 6.0f);   // Human
    EXPECT_FLOAT_EQ(clientResolvedActorSize(8, 0, -1.0f), 4.0f);  // Dwarf, < 0 too
    EXPECT_FLOAT_EQ(clientResolvedActorSize(1, 0, 7.0f), 7.0f);   // a said size stands
}

// The z the client transmits for itself follows the resolved size: the rule
// (0.625 x size) at each measured figure's resolved size reproduces the wire.
TEST(SelfActorSize, TheWireZOffsetFollowsTheResolvedDefaultAsMeasured) {
    EXPECT_NEAR(eqt::wireZOffsetForSize(clientResolvedActorSize(6, 1, 0.0f)), kOracleDarkElfWireZ, kWireTol);
    EXPECT_NEAR(eqt::wireZOffsetForSize(clientResolvedActorSize(2, 0, 0.0f)), kT311BarbarianWireZ, kWireTol);
    EXPECT_NEAR(eqt::wireZOffsetForSize(clientResolvedActorSize(1, 0, 0.0f)), kT311HumanWireZ, kWireTol);
    // The old "no size -> 6.0 for every race" misses the dark elf by 0.625.
    EXPECT_GT(eqt::wireZOffsetForSize(0.0f) - kOracleDarkElfWireZ, 0.6f);
}

// After OP_PlayerProfile alone (no size on it) the entity record and
// PlayerState hold the same resolved default.
TEST(SelfActorSize, TheProfileSetsOneNumber) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ASSERT_EQ(eq.GetEntities().count(kSelfId), 1u);
    EXPECT_FLOAT_EQ(EntitySize(eq), 5.0f);
    EXPECT_FLOAT_EQ(eq.GetSize(), 5.0f);
    EXPECT_FLOAT_EQ(clientActorScale(kRaceDarkElf, kGenderFemale, EntitySize(eq)), 1.0f);
    EXPECT_NEAR(eqt::wireZOffsetForSize(eq.GetSize()), kOracleDarkElfWireZ, kWireTol);
}

TEST(SelfActorSize, AHumanProfileResolvesToSix) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile(1, 0));
    EXPECT_FLOAT_EQ(EntitySize(eq), 6.0f);
    EXPECT_FLOAT_EQ(eq.GetSize(), 6.0f);
}

// The server's OP_ZoneEntry Spawn_Struct is read and resolved into both.
TEST(SelfActorSize, TheServerZoneEntrySizeWordIsResolvedIntoBoth) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(0.0f));
    EXPECT_FLOAT_EQ(EntitySize(eq), 5.0f) << "the oracle's dark elf: size word 0.000 -> default 5";
    EXPECT_FLOAT_EQ(eq.GetSize(), 5.0f);

    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(3.0f));
    EXPECT_FLOAT_EQ(EntitySize(eq), 3.0f);
    EXPECT_FLOAT_EQ(eq.GetSize(), 3.0f);
    EXPECT_NEAR(clientActorScale(kRaceDarkElf, kGenderFemale, EntitySize(eq)), 0.6f, 1e-6f);
}

TEST(SelfActorSize, AnotherSpawnsZoneEntryIsIgnored) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(9.0f, kSelfId + 1));
    EXPECT_FLOAT_EQ(EntitySize(eq), 5.0f);
    EXPECT_FLOAT_EQ(eq.GetSize(), 5.0f);
}
