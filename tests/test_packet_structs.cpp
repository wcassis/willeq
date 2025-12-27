#include <gtest/gtest.h>
#include "common/packet_structs.h"
#include <cstring>

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

TEST_F(PacketStructsTest, Consider_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Consider_Struct), 24);
}

TEST_F(PacketStructsTest, Action_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Action_Struct), 44);
}

TEST_F(PacketStructsTest, CombatDamage_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::CombatDamage_Struct), 32);
}

TEST_F(PacketStructsTest, MoneyOnCorpse_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::MoneyOnCorpse_Struct), 20);
}

TEST_F(PacketStructsTest, Death_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Death_Struct), 32);
}

TEST_F(PacketStructsTest, HPUpdate_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::HPUpdate_Struct), 8);
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

TEST_F(PacketStructsTest, Animation_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Animation_Struct), 8);
}

TEST_F(PacketStructsTest, Target_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Target_Struct), 4);
}

TEST_F(PacketStructsTest, Attack_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::Attack_Struct), 4);
}

TEST_F(PacketStructsTest, LootRequest_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::LootRequest_Struct), 4);
}

TEST_F(PacketStructsTest, LootItem_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::LootItem_Struct), 12);
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

// Test ChannelMessage_Struct has correct fixed portion size
TEST_F(PacketStructsTest, ChannelMessage_FixedSize) {
    // Fixed portion is 144 bytes (before variable-length message)
    EXPECT_EQ(offsetof(EQT::ChannelMessage_Struct, message), 144);
}

// Test ZoneChange_Struct size
TEST_F(PacketStructsTest, ZoneChange_Struct_Size) {
    EXPECT_EQ(sizeof(EQT::ZoneChange_Struct), 88);
}
