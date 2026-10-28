// B1164 (#483) fix leg, review note 7941 finding 7: the local player's own
// spawn size, which feeds the attach scale s of the player's own spell slots
// (EQ::SpellFxAttachScale, spell_fx_attach_scale.h).
//
// ZoneProcessPlayerProfile (src/client/zone/zone_entry.cpp) gives our entity
// a placeholder size of 6.0, and on a graphical zone-in no other spawn packet
// for ourselves arrives (T332, measured live). The official client takes the
// size from the server's OP_ZoneEntry Spawn_Struct (+75), which follows
// OP_PlayerProfile (review/B1164/worker/dev5/oracle/pin_s_25fps/capture.pcap:
// S->C OP_PlayerProfile, then S->C OP_ZoneEntry len 385, Testcaster size
// 0.000 = none -> the race default; refactor/B1164/scripts/b1164_spawns.py).
//
// Merged with B1172's fix leg (a7798e47): ONE source of truth for our size.
// The profile now holds what the client holds for a size word of 0 -- the
// race/gender default resolved by eqgame 0x47fb30
// (EQT::Graphics::clientResolvedActorSize) -- and OnZoneEntry replaces it
// with the wire size, resolved the same way. Before B1164/B1172 the profile
// left the placeholder 6.0 (s = 1.2 on a dark elf, where the client's is 1.0)
// and OnZoneEntry only logged: review/B1164/worker/dev5/fix/local_player_red.txt.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/spell/spell_fx_attach_scale.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstddef>
#include <cstring>
#include <vector>

using namespace zonetest;

namespace {

constexpr uint16_t kSelfId = 311;
constexpr uint32_t kRaceDarkElf = 6;
constexpr uint32_t kGenderFemale = 1;

// A zero-filled PlayerProfile (opcode + the 19,588-byte struct): gender at
// +4, race at +8 (zone_entry.cpp's reads), our spawn id at +14384.
std::vector<uint8_t> Profile() {
    return BuildPayload(2 + 19588, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 4, kGenderFemale);
        p.PutUInt32(2 + 8, kRaceDarkElf);
        p.PutUInt32(2 + 14384, kSelfId);
    });
}

// The server's OP_ZoneEntry: opcode + our Spawn_Struct.
std::vector<uint8_t> ServerZoneEntry(float size, uint32_t spawn_id = kSelfId) {
    return BuildPayload(2 + sizeof(EQT::Spawn_Struct), [&](EQ::Net::DynamicPacket& p) {
        std::memcpy(static_cast<char*>(p.Data()) + 2 + offsetof(EQT::Spawn_Struct, size), &size,
                    sizeof(size));
        p.PutUInt32(2 + offsetof(EQT::Spawn_Struct, race), kRaceDarkElf);
        p.PutUInt8(2 + offsetof(EQT::Spawn_Struct, gender), kGenderFemale);
        p.PutUInt32(2 + offsetof(EQT::Spawn_Struct, spawnId), spawn_id);
    });
}

void Prep(EverQuest& eq) {
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);
}

float SelfAttachScale(const EverQuest& eq) {
    const auto& e = eq.GetEntities().at(kSelfId);
    return EQ::SpellFxAttachScale(e.race_id, e.gender, e.size);
}

}  // namespace

TEST(LocalPlayerSpawnSize, ProfileHoldsTheResolvedRaceDefault) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ASSERT_EQ(eq.GetEntities().count(kSelfId), 1u);
    EXPECT_FLOAT_EQ(eq.GetEntities().at(kSelfId).size, 5.0f) << "dark elf default, not the old 6.0";
    EXPECT_FLOAT_EQ(eq.GetGameState().player().size(), 5.0f) << "PlayerState holds the same number";
    EXPECT_FLOAT_EQ(SelfAttachScale(eq), 1.0f);
}

TEST(LocalPlayerSpawnSize, ServerZoneEntryCarriesTheOracleSize) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(0.0f));
    EXPECT_FLOAT_EQ(eq.GetEntities().at(kSelfId).size, 5.0f) << "size word 0 -> the race default";
    EXPECT_FLOAT_EQ(eq.GetGameState().player().size(), 5.0f);
    EXPECT_FLOAT_EQ(SelfAttachScale(eq), 1.0f)
        << "the oracle's dark-elf caster: size 0 -> race default 5.0 -> s 1.000000";
}

TEST(LocalPlayerSpawnSize, ServerZoneEntrySizeIsTakenAsSent) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(3.0f));
    EXPECT_FLOAT_EQ(eq.GetEntities().at(kSelfId).size, 3.0f);
    EXPECT_FLOAT_EQ(eq.GetGameState().player().size(), 3.0f);
    EXPECT_NEAR(SelfAttachScale(eq), 0.6f, 1e-6f);
}

TEST(LocalPlayerSpawnSize, AnotherSpawnsZoneEntryIsIgnored) {
    EverQuest eq = MakeEq();
    Prep(eq);
    ReplayZone(eq, HC_OP_PlayerProfile, Profile());
    ReplayZone(eq, HC_OP_ZoneEntry, ServerZoneEntry(3.0f, kSelfId + 1));
    EXPECT_FLOAT_EQ(eq.GetEntities().at(kSelfId).size, 5.0f);
}
