#include <gtest/gtest.h>
#include "common/packet_structs.h"
#include "client/world_object.h"
#include "captured_packets.h"
#include <cstring>
#include <string>

// Test that all structures are properly packed and sized
class PacketStructsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Size validation tests
TEST_F(PacketStructsTest, SpawnStruct_Size) {
    // Spawn_Struct should be exactly 385 bytes for Titanium
    EXPECT_EQ(sizeof(EQT::Spawn_Struct), 385);
}

TEST_F(PacketStructsTest, Tint_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Tint_Struct), 4);
}

TEST_F(PacketStructsTest, TintProfile_Size) {
    EXPECT_EQ(sizeof(EQT::TintProfile), 36);
}

TEST_F(PacketStructsTest, Texture_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Texture_Struct), 4);
}

TEST_F(PacketStructsTest, TextureProfile_Size) {
    EXPECT_EQ(sizeof(EQT::TextureProfile), 36);
}

TEST_F(PacketStructsTest, LoginInfo_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::LoginInfo_Struct), 464);
}

TEST_F(PacketStructsTest, EnterWorld_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::EnterWorld_Struct), 72);
}

TEST_F(PacketStructsTest, EntityId_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::EntityId_Struct), 4);
}

TEST_F(PacketStructsTest, SpawnAppearance_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::SpawnAppearance_Struct), 8);
}

TEST_F(PacketStructsTest, SpellBuff_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::SpellBuff_Struct), 20);
}

TEST_F(PacketStructsTest, MoneyOnCorpse_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::MoneyOnCorpse_Struct), 20);
}

TEST_F(PacketStructsTest, Death_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Death_Struct), 32);
}

TEST_F(PacketStructsTest, DeleteSpawn_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::DeleteSpawn_Struct), 4);
}

TEST_F(PacketStructsTest, BindStruct_Size) {
    EXPECT_EQ(sizeof(EQT::BindStruct), 20);
}

TEST_F(PacketStructsTest, BeginCast_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::BeginCast_Struct), 8);
}

TEST_F(PacketStructsTest, CastSpell_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::CastSpell_Struct), 20);
}

TEST_F(PacketStructsTest, ManaChange_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::ManaChange_Struct), 16);
}

TEST_F(PacketStructsTest, Target_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Target_Struct), 4);
}

TEST_F(PacketStructsTest, LootRequest_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::LootRequest_Struct), 4);
}

// Field offset tests for Spawn_Struct
TEST_F(PacketStructsTest, SpawnStruct_FieldOffsets) {
    // Key field offsets based on titanium_structs.h comments
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, name), 7);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, deity), 71);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, size), 75);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, NPC), 83);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, curHp), 86);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, level), 151);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, runspeed), 233);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, guildID), 238);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, race), 284);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, lastName), 292);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, walkspeed), 324);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, class_), 331);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, gender), 334);
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, spawnId), 340);
}

// Test Tint_Struct union
TEST_F(PacketStructsTest, TintStruct_Union) {
    EQT::Tint_Struct tint;
    tint.Color = 0xFF112233;  // RGBA in memory: 0x33 0x22 0x11 0xFF

    EXPECT_EQ(tint.Blue, 0x33);
    EXPECT_EQ(tint.Green, 0x22);
    EXPECT_EQ(tint.Red, 0x11);
    EXPECT_EQ(tint.UseTint, 0xFF);
}

// Test position bitfield helpers
TEST_F(PacketStructsTest, PositionHelpers_ExtractCoord) {
    // Test positive coordinate extraction
    // Value 1000.0f * 8 = 8000 = 0x1F40 in 19-bit field shifted by 10
    uint32_t field = 0x1F40 << 10;
    float coord = EQT::Position::ExtractCoord(field, 10);
    EXPECT_NEAR(coord, 1000.0f, 0.5f);
}

TEST_F(PacketStructsTest, PositionHelpers_ExtractNegativeCoord) {
    // Test negative coordinate: -500.0f * 8 = -4000
    // -4000 in 19-bit signed = 0x7F060 (sign extended from 19 bits)
    int32_t raw = -4000;
    uint32_t field = (static_cast<uint32_t>(raw) & 0x7FFFF) << 10;
    float coord = EQT::Position::ExtractCoord(field, 10);
    EXPECT_NEAR(coord, -500.0f, 0.5f);
}

TEST_F(PacketStructsTest, PositionHelpers_ExtractHeading) {
    // Test heading extraction: 180 degrees = 2048 in 12-bit field
    uint32_t field = 2048 << 13;
    float heading = EQT::Position::ExtractHeading(field, 13);
    EXPECT_NEAR(heading, 180.0f, 0.1f);
}

TEST_F(PacketStructsTest, PositionHelpers_ExtractAnimation) {
    // Test animation extraction
    uint32_t field = 100 << 19;
    uint16_t anim = EQT::Position::ExtractAnimation(field, 19);
    EXPECT_EQ(anim, 100);
}

// Test NewSpawn_Struct wraps Spawn_Struct
TEST_F(PacketStructsTest, NewSpawnStruct_Size) {
    EXPECT_EQ(sizeof(EQT::NewSpawn_Struct), sizeof(EQT::Spawn_Struct));
}

// Test constants
TEST_F(PacketStructsTest, Constants) {
    EXPECT_EQ(EQT::BUFF_COUNT, 25);
    EXPECT_EQ(EQT::MAX_PP_SKILL, 100);
    EXPECT_EQ(EQT::MAX_PP_LANGUAGE, 28);
    EXPECT_EQ(EQT::SPELLBOOK_SIZE, 400);
    EXPECT_EQ(EQT::SPELL_GEM_COUNT, 9);
    EXPECT_EQ(EQT::TEXTURE_COUNT, 9);
}

// Test AppearanceType enum values
TEST_F(PacketStructsTest, AppearanceTypes) {
    EXPECT_EQ(static_cast<uint16_t>(EQT::AT_Die), 0);
    EXPECT_EQ(static_cast<uint16_t>(EQT::AT_HP), 17);
    EXPECT_EQ(static_cast<uint16_t>(EQT::AT_Anon), 21);
    EXPECT_EQ(static_cast<uint16_t>(EQT::AT_AFK), 24);
    EXPECT_EQ(static_cast<uint16_t>(EQT::AT_Size), 29);
}

// Test TintProfile union
TEST_F(PacketStructsTest, TintProfile_Union) {
    EQT::TintProfile profile;
    memset(&profile, 0, sizeof(profile));

    profile.Head.Color = 0x11111111;
    profile.Chest.Color = 0x22222222;

    EXPECT_EQ(profile.Slot[0].Color, 0x11111111);
    EXPECT_EQ(profile.Slot[1].Color, 0x22222222);
}

// Test TextureProfile union
TEST_F(PacketStructsTest, TextureProfile_Union) {
    EQT::TextureProfile profile;
    memset(&profile, 0, sizeof(profile));

    profile.Head.Material = 100;
    profile.Primary.Material = 200;

    EXPECT_EQ(profile.Slot[0].Material, 100);
    EXPECT_EQ(profile.Slot[7].Material, 200);  // Primary is index 7
}

// Test Spawn_Struct serialization compatibility
TEST_F(PacketStructsTest, SpawnStruct_Serialization) {
    // Create a spawn struct and verify we can read expected fields
    EQT::Spawn_Struct spawn;
    memset(&spawn, 0, sizeof(spawn));

    // Set some values
    strcpy(spawn.name, "TestPlayer");
    spawn.level = 50;
    spawn.race = 1;  // Human
    spawn.class_ = 3;  // Paladin
    spawn.gender = 0;  // Male
    spawn.spawnId = 12345;
    spawn.curHp = 100;

    // Verify values
    EXPECT_STREQ(spawn.name, "TestPlayer");
    EXPECT_EQ(spawn.level, 50);
    EXPECT_EQ(spawn.race, 1);
    EXPECT_EQ(spawn.class_, 3);
    EXPECT_EQ(spawn.gender, 0);
    EXPECT_EQ(spawn.spawnId, 12345);
    EXPECT_EQ(spawn.curHp, 100);
}

// T147: the structs deleted from this block were dead in production AND wrong
// against eqemu -- Action_Struct (44 vs a 31-byte wire packet; it is now the
// in-memory DTO EQT::SpellActionInfo in include/client/spell/spell_manager.h),
// CombatDamage_Struct (32 vs 23), HPUpdate_Struct (8, and the field order of
// eqemu's SpawnHPUpdate_Struct is cur_hp/max_hp/spawn_id at 10 bytes),
// Animation_Struct (8 vs 4), Attack_Struct (no eqemu counterpart; a duplicate
// of Target_Struct) and LootItem_Struct (12 vs LootingItem_Struct's 16).
// See refactor/T147_packet_structs_trap_removal.md.

// T147: Consider_Struct is 28, not 24 -- it was missing pvpcon + unknown3[3]
// (eqemu eq_packet_structs.h:1351 == titanium_structs.h:1160). The hand-rolled
// parser in eq_combat_actions.cpp:40 already guarded on the correct 28.
TEST_F(PacketStructsTest, Consider_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Consider_Struct), 28);
}

// Test ChannelMessage_Struct has correct fixed portion size
TEST_F(PacketStructsTest, ChannelMessage_FixedSize) {
    // T147: 148, not 144. cm_unknown4 is uint32[2] (eqemu
    // titanium_structs.h:1020), which eq_chat.cpp:31-32 already documented
    // correctly while this struct declared a single uint32.
    EXPECT_EQ(offsetof(EQT::ChannelMessage_Struct, message), 148);
    EXPECT_EQ(sizeof(EQT::ChannelMessage_Struct), 148);
}

// Test ZoneChange_Struct size
TEST_F(PacketStructsTest, ZoneChange_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::ZoneChange_Struct), 88);
}

// ============================================================================
// T144: captured-bytes regression tests for OP_GroundSpawn / Object_Struct.
//
// These are REAL BYTES off the wire, captured from a live eqemu session in
// qeynos on 2026-08-19 (annotated dump: review/T144/). They exist because
// Object_Struct carried a passing `static_assert(sizeof(...) == 100)` while
// being wrong from offset 8 onward: it had been derived from eqemu's
// server-internal struct, not the Titanium wire struct. A sizeof assert
// cannot catch that. Bytes can.
//
// The expected values below are independently corroborated by the server's
// own `object` DB table for zone 1 (qeynos) -- xpos/ypos/heading/type -- so
// these assertions are anchored to something outside the client entirely.
//
// If one of these fails, the struct layout changed. Do not "fix" the test.
// ============================================================================

// object.id=228452: IT10804_ACTORDEF, type 17 (blacksmithing), x=-177 y=484 h=117.
// 109 bytes on the wire INCLUDING the 2-byte opcode (0x0f47) => 107-byte body
// == strlen("IT10804_ACTORDEF") + 91.

// object.id=201: IT66_ACTORDEF, type 17 (blacksmithing), x=-296 y=179 h=369.
// 106 bytes => 104-byte body == strlen("IT66_ACTORDEF") + 91. Note the bytes
// immediately after the name's NUL are "_ACTORDEF" left over in the server's
// uninitialised send buffer -- proof the name must be read as NUL-terminated,
// never as a fixed 32-byte field.

// A real dropped ground item (not a container): IT63, type 0, at
// x=155 y=-144.7 z=-36.9. 97 bytes => 95-byte body == strlen("IT63") + 91.
// THIS is the packet the old code's comment called "97 bytes instead of 102"
// and dismissed as server jitter. It is not jitter; it is the format.

TEST_F(PacketStructsTest, Object_Struct_Size) {
    // The Titanium wire struct is 92 bytes. It was 100 for as long as this
    // repo existed, and the static_assert passed the whole time.
    EXPECT_EQ(sizeof(EQT::Object_Struct), 92u);
}

TEST_F(PacketStructsTest, Object_Struct_FieldOffsets) {
    EXPECT_EQ(offsetof(EQT::Object_Struct, linked_list_addr), 0u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, unknown008), 8u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, drop_id), 12u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, zone_id), 16u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, zone_instance), 18u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, unknown020), 20u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, unknown024), 24u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, heading), 28u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, z), 32u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, x), 36u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, y), 40u);
    // The single most load-bearing number in this file: the live capture shows
    // the object name starting here, not at 56.
    EXPECT_EQ(offsetof(EQT::Object_Struct, object_name), 44u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, unknown076), 76u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, object_type), 80u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, unknown084), 84u);
    EXPECT_EQ(offsetof(EQT::Object_Struct, spawn_id), 88u);
}

TEST_F(PacketStructsTest, GroundSpawn_CapturedBytes_TradeskillForge) {
    eqt::WorldObject obj;
    ASSERT_TRUE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnForgeBytes + 2,
                                      sizeof(eqt_test::kGroundSpawnForgeBytes) - 2, obj));
    EXPECT_EQ(obj.drop_id, 190u);
    EXPECT_EQ(obj.zone_id, 1u);          // qeynos
    EXPECT_EQ(obj.zone_instance, 0u);
    EXPECT_EQ(obj.name, "IT66_ACTORDEF");
    EXPECT_FLOAT_EQ(obj.heading, 369.0f);
    EXPECT_FLOAT_EQ(obj.z, 0.0f);
    EXPECT_FLOAT_EQ(obj.x, -296.0f);     // object.xpos for qeynos object 201
    EXPECT_FLOAT_EQ(obj.y, 179.0f);      // object.ypos for qeynos object 201
    EXPECT_EQ(obj.object_type, 17u);     // CONTAINER_BLACKSMITHING
    EXPECT_TRUE(obj.isTradeskillContainer());
}

TEST_F(PacketStructsTest, GroundSpawn_CapturedBytes_LongName) {
    eqt::WorldObject obj;
    ASSERT_TRUE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnKilnBytes + 2,
                                      sizeof(eqt_test::kGroundSpawnKilnBytes) - 2, obj));
    EXPECT_EQ(obj.drop_id, 199u);
    EXPECT_EQ(obj.zone_id, 1u);
    EXPECT_EQ(obj.name, "IT10804_ACTORDEF");
    EXPECT_FLOAT_EQ(obj.heading, 117.0f);
    EXPECT_FLOAT_EQ(obj.x, -177.0f);
    EXPECT_FLOAT_EQ(obj.y, 484.0f);
    EXPECT_EQ(obj.object_type, 17u);
    EXPECT_TRUE(obj.isTradeskillContainer());
}

TEST_F(PacketStructsTest, GroundSpawn_CapturedBytes_ShortPacketGroundItem) {
    // 95-byte body -- well under the 102 bytes the old code demanded before it
    // would read object_type, and under the 100-byte struct it declared.
    eqt::WorldObject obj;
    ASSERT_TRUE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnItemBytes + 2,
                                      sizeof(eqt_test::kGroundSpawnItemBytes) - 2, obj));
    EXPECT_EQ(obj.drop_id, 189u);
    EXPECT_EQ(obj.name, "IT63");
    EXPECT_FLOAT_EQ(obj.x, 155.0f);
    EXPECT_FLOAT_EQ(obj.y, -144.7f);
    EXPECT_FLOAT_EQ(obj.z, -36.9f);
    EXPECT_EQ(obj.object_type, 0u);      // a plain dropped item, not a container
    EXPECT_FALSE(obj.isTradeskillContainer());
}

TEST_F(PacketStructsTest, GroundSpawn_NameIsNulTerminatedNotFixedWidth) {
    // The forge capture has "_ACTORDEF" sitting in the object_name field after
    // the terminating NUL -- uninitialised heap in the server's send buffer,
    // because ENCODE only strcpy's the name. A reader taking 32 fixed bytes
    // would produce "IT66_ACTORDEF\0_ACTORDEF".
    const char* raw = reinterpret_cast<const char*>(eqt_test::kGroundSpawnForgeBytes + 2 + 44);
    EXPECT_EQ(std::string(raw + 14, 9), "_ACTORDEF");   // the garbage really is there
    eqt::WorldObject obj;
    ASSERT_TRUE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnForgeBytes + 2,
                                      sizeof(eqt_test::kGroundSpawnForgeBytes) - 2, obj));
    EXPECT_EQ(obj.name.size(), 13u);
}

TEST_F(PacketStructsTest, GroundSpawn_BodyLengthIsStrlenPlus91) {
    // eqemu ENCODE(OP_GroundSpawn):
    //   in->size = strlen(emu->object_name) + sizeof(structs::Object_Struct) - 1
    // Held for all 11 packets captured in the live qeynos session; the three
    // kept here span the range (95 / 104 / 107 body bytes).
    struct { const uint8_t* pkt; size_t len; const char* name; } cases[] = {
        { eqt_test::kGroundSpawnKilnBytes,  sizeof(eqt_test::kGroundSpawnKilnBytes),  "IT10804_ACTORDEF" },
        { eqt_test::kGroundSpawnForgeBytes, sizeof(eqt_test::kGroundSpawnForgeBytes), "IT66_ACTORDEF" },
        { eqt_test::kGroundSpawnItemBytes,  sizeof(eqt_test::kGroundSpawnItemBytes),  "IT63" },
    };
    for (const auto& c : cases) {
        EXPECT_EQ(c.len - 2, std::strlen(c.name) + 91) << "for " << c.name;
    }
}

TEST_F(PacketStructsTest, GroundSpawn_RejectsBodyTooShortForObjectType) {
    // 84 body bytes are needed to reach the end of object_type (offset 80).
    EXPECT_EQ(eqt::GroundSpawnMinBodyBytes(), 84u);
    eqt::WorldObject obj;
    obj.name = "untouched";
    EXPECT_FALSE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnForgeBytes + 2, 83, obj));
    EXPECT_EQ(obj.name, "untouched");   // out param left alone on failure
    EXPECT_TRUE(eqt::ParseGroundSpawn(eqt_test::kGroundSpawnForgeBytes + 2, 84, obj));
    EXPECT_EQ(obj.object_type, 17u);
    EXPECT_FALSE(eqt::ParseGroundSpawn(nullptr, 200, obj));
}
