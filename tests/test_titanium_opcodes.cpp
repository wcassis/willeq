#include <gtest/gtest.h>
#include "client/eq.h"

class TitaniumOpcodesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Verify Titanium login opcodes are correctly defined
TEST_F(TitaniumOpcodesTest, LoginOpcodes) {
    EXPECT_EQ(HC_OP_SessionReady, 0x0001);
    EXPECT_EQ(HC_OP_Login, 0x0002);
    EXPECT_EQ(HC_OP_ServerListRequest, 0x0004);
    EXPECT_EQ(HC_OP_PlayEverquestRequest, 0x000d);
    EXPECT_EQ(HC_OP_ChatMessage, 0x0016);
    EXPECT_EQ(HC_OP_LoginAccepted, 0x0017);
    EXPECT_EQ(HC_OP_ServerListResponse, 0x0018);
    EXPECT_EQ(HC_OP_PlayEverquestResponse, 0x0021);
}

// Verify Titanium world opcodes are correctly defined
TEST_F(TitaniumOpcodesTest, WorldOpcodes) {
    EXPECT_EQ(HC_OP_SendLoginInfo, 0x4dd0);
    EXPECT_EQ(HC_OP_GuildsList, 0x6957);
    EXPECT_EQ(HC_OP_LogServer, 0x0fa6);
    EXPECT_EQ(HC_OP_ApproveWorld, 0x3c25);
    EXPECT_EQ(HC_OP_EnterWorld, 0x7cba);
    EXPECT_EQ(HC_OP_PostEnterWorld, 0x52a4);
    EXPECT_EQ(HC_OP_ExpansionInfo, 0x04ec);
    EXPECT_EQ(HC_OP_SendCharInfo, 0x4513);
    EXPECT_EQ(HC_OP_MOTD, 0x024d);
    EXPECT_EQ(HC_OP_SetChatServer, 0x00d7);
    EXPECT_EQ(HC_OP_SetChatServer2, 0x6536);
    EXPECT_EQ(HC_OP_ZoneServerInfo, 0x61b6);
    EXPECT_EQ(HC_OP_WorldComplete, 0x509d);
}

// Verify Titanium zone opcodes are correctly defined
TEST_F(TitaniumOpcodesTest, ZoneOpcodes) {
    EXPECT_EQ(HC_OP_ZoneEntry, 0x7213);
    EXPECT_EQ(HC_OP_NewZone, 0x0920);
    EXPECT_EQ(HC_OP_ReqClientSpawn, 0x0322);
    EXPECT_EQ(HC_OP_ZoneSpawns, 0x2e78);
    EXPECT_EQ(HC_OP_SendZonepoints, 0x3eba);
    EXPECT_EQ(HC_OP_ReqNewZone, 0x7ac5);
    EXPECT_EQ(HC_OP_PlayerProfile, 0x75df);
    EXPECT_EQ(HC_OP_CharInventory, 0x5394);
    EXPECT_EQ(HC_OP_TimeOfDay, 0x1580);
    EXPECT_EQ(HC_OP_SpawnDoor, 0x4c24);
    EXPECT_EQ(HC_OP_ClientReady, 0x5e20);
    EXPECT_EQ(HC_OP_ZoneChange, 0x5dd8);
    EXPECT_EQ(HC_OP_SetServerFilter, 0x6563);
    EXPECT_EQ(HC_OP_GroundSpawn, 0x0f47);
    EXPECT_EQ(HC_OP_Weather, 0x254d);
}

// Verify Titanium update opcodes
TEST_F(TitaniumOpcodesTest, UpdateOpcodes) {
    EXPECT_EQ(HC_OP_ClientUpdate, 0x14cb);
    EXPECT_EQ(HC_OP_SpawnAppearance, 0x7c32);
    EXPECT_EQ(HC_OP_NewSpawn, 0x1860);
    EXPECT_EQ(HC_OP_DeleteSpawn, 0x55bc);
    EXPECT_EQ(HC_OP_MobHealth, 0x0695);
    EXPECT_EQ(HC_OP_HPUpdate, 0x3bcf);
}

// Verify Titanium combat opcodes
TEST_F(TitaniumOpcodesTest, CombatOpcodes) {
    EXPECT_EQ(HC_OP_AutoAttack, 0x5e55);
    EXPECT_EQ(HC_OP_AutoAttack2, 0x0701);
    EXPECT_EQ(HC_OP_CastSpell, 0x304b);
    EXPECT_EQ(HC_OP_InterruptCast, 0x0b97);
    EXPECT_EQ(HC_OP_Damage, 0x5c78);
    EXPECT_EQ(HC_OP_Consider, 0x65ca);
    EXPECT_EQ(HC_OP_TargetMouse, 0x6c47);
}

// Verify Titanium loot opcodes
TEST_F(TitaniumOpcodesTest, LootOpcodes) {
    EXPECT_EQ(HC_OP_LootRequest, 0x6f90);
    EXPECT_EQ(HC_OP_LootItem, 0x7081);
    EXPECT_EQ(HC_OP_LootComplete, 0x0a94);
    EXPECT_EQ(HC_OP_MoneyOnCorpse, 0x7fe4);
}

// Verify Titanium chat opcodes
TEST_F(TitaniumOpcodesTest, ChatOpcodes) {
    EXPECT_EQ(HC_OP_ChannelMessage, 0x1004);
}

// Test that opcodes don't collide (important for packet handling)
TEST_F(TitaniumOpcodesTest, OpcodeUniqueness) {
    // Login opcodes should be distinct from world opcodes
    EXPECT_NE(HC_OP_SessionReady, HC_OP_SendLoginInfo);
    EXPECT_NE(HC_OP_Login, HC_OP_ZoneEntry);

    // Zone opcodes should be distinct from each other
    EXPECT_NE(HC_OP_ZoneEntry, HC_OP_NewZone);
    EXPECT_NE(HC_OP_ClientUpdate, HC_OP_NewSpawn);
    EXPECT_NE(HC_OP_AutoAttack, HC_OP_CastSpell);
}

// Test opcode sizes (verify they fit in uint16_t)
TEST_F(TitaniumOpcodesTest, OpcodeSize) {
    // All opcodes should fit in 16 bits
    EXPECT_LE(HC_OP_SessionReady, 0xFFFF);
    EXPECT_LE(HC_OP_ZoneEntry, 0xFFFF);
    EXPECT_LE(HC_OP_ClientUpdate, 0xFFFF);
    EXPECT_LE(HC_OP_AutoAttack, 0xFFFF);
    EXPECT_LE(HC_OP_ChannelMessage, 0xFFFF);
}
