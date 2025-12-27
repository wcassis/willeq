#include <gtest/gtest.h>
#include "client/eq.h"
#include "client/position.h"
#include <cmath>

// Test Entity struct functionality
class EntityTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(EntityTest, DefaultConstruction) {
    Entity e{};  // Value-initialize to zero
    EXPECT_EQ(e.spawn_id, 0);
    EXPECT_TRUE(e.name.empty());
    // Note: floats may not be zero-initialized without explicit {}
    // Test that the struct is properly defined
    EXPECT_FALSE(e.is_corpse);
}

TEST_F(EntityTest, EntityWithValues) {
    Entity e;
    e.spawn_id = 12345;
    e.name = "TestPlayer";
    e.x = 100.0f;
    e.y = 200.0f;
    e.z = 50.0f;
    e.heading = 128.0f;
    e.level = 60;
    e.class_id = 1;  // Warrior
    e.race_id = 1;   // Human
    e.hp_percent = 100;

    EXPECT_EQ(e.spawn_id, 12345);
    EXPECT_EQ(e.name, "TestPlayer");
    EXPECT_FLOAT_EQ(e.x, 100.0f);
    EXPECT_FLOAT_EQ(e.y, 200.0f);
    EXPECT_FLOAT_EQ(e.z, 50.0f);
    EXPECT_FLOAT_EQ(e.heading, 128.0f);
    EXPECT_EQ(e.level, 60);
    EXPECT_EQ(e.class_id, 1);
    EXPECT_EQ(e.race_id, 1);
    EXPECT_EQ(e.hp_percent, 100);
}

TEST_F(EntityTest, DeltaTracking) {
    Entity e;
    e.delta_x = 1.0f;
    e.delta_y = 2.0f;
    e.delta_z = 0.5f;
    e.delta_heading = 0.1f;
    e.last_update_time = 1000;

    EXPECT_FLOAT_EQ(e.delta_x, 1.0f);
    EXPECT_FLOAT_EQ(e.delta_y, 2.0f);
    EXPECT_FLOAT_EQ(e.delta_z, 0.5f);
    EXPECT_FLOAT_EQ(e.delta_heading, 0.1f);
    EXPECT_EQ(e.last_update_time, 1000u);
}

// Test WorldServer struct
class WorldServerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(WorldServerTest, DefaultConstruction) {
    WorldServer ws{};  // Value-initialize
    EXPECT_TRUE(ws.long_name.empty());
    EXPECT_TRUE(ws.address.empty());
    // Note: POD types may not be zero-initialized without explicit {}
    EXPECT_TRUE(ws.lang.empty());
    EXPECT_TRUE(ws.region.empty());
}

TEST_F(WorldServerTest, WithValues) {
    WorldServer ws;
    ws.long_name = "Test Server";
    ws.address = "192.168.1.1:9000";
    ws.type = 1;
    ws.lang = "en";
    ws.region = "US";
    ws.status = 1;
    ws.players = 100;

    EXPECT_EQ(ws.long_name, "Test Server");
    EXPECT_EQ(ws.address, "192.168.1.1:9000");
    EXPECT_EQ(ws.type, 1);
    EXPECT_EQ(ws.lang, "en");
    EXPECT_EQ(ws.region, "US");
    EXPECT_EQ(ws.status, 1);
    EXPECT_EQ(ws.players, 100);
}

// Test Animation enum values
class AnimationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(AnimationTest, BasicAnimations) {
    EXPECT_EQ(ANIM_STAND, 0);
    EXPECT_EQ(ANIM_WALK, 1);
    EXPECT_EQ(ANIM_RUN, 27);
    EXPECT_EQ(ANIM_JUMP, 20);
    EXPECT_EQ(ANIM_DEATH, 16);
}

TEST_F(AnimationTest, EmoteAnimations) {
    EXPECT_EQ(ANIM_CRY, 18);
    EXPECT_EQ(ANIM_KNEEL, 19);
    EXPECT_EQ(ANIM_LAUGH, 63);
    EXPECT_EQ(ANIM_POINT, 64);
    EXPECT_EQ(ANIM_SALUTE, 67);
    EXPECT_EQ(ANIM_SHRUG, 65);
    EXPECT_EQ(ANIM_WAVE, 29);
    EXPECT_EQ(ANIM_DANCE, 58);
}

TEST_F(AnimationTest, ZoneServerAnimations) {
    EXPECT_EQ(ANIM_STANDING, 100);
    EXPECT_EQ(ANIM_FREEZE, 102);
    EXPECT_EQ(ANIM_SITTING, 110);
    EXPECT_EQ(ANIM_CROUCHING, 111);
    EXPECT_EQ(ANIM_LYING, 115);
}

TEST_F(AnimationTest, CombatAnimations) {
    EXPECT_EQ(ANIM_KICK, 11);
    EXPECT_EQ(ANIM_BASH, 12);
    EXPECT_EQ(ANIM_LOOT, 105);
}

TEST_F(AnimationTest, SwimAnimations) {
    EXPECT_EQ(ANIM_SWIM_IDLE, 6);
    EXPECT_EQ(ANIM_SWIM, 7);
    EXPECT_EQ(ANIM_SWIM_ATTACK, 8);
    EXPECT_EQ(ANIM_FLY, 9);
}

// Test HCAppearanceType enum
class AppearanceTypeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(AppearanceTypeTest, CoreTypes) {
    EXPECT_EQ(AT_DIE, 0);
    EXPECT_EQ(AT_WHO_LEVEL, 1);
    EXPECT_EQ(AT_MAX_HEALTH, 2);
    EXPECT_EQ(AT_INVISIBLE, 3);
    EXPECT_EQ(AT_PVP, 4);
    EXPECT_EQ(AT_LIGHT, 5);
}

TEST_F(AppearanceTypeTest, AnimationAndState) {
    EXPECT_EQ(AT_ANIMATION, 14);
    EXPECT_EQ(AT_SNEAK, 15);
    EXPECT_EQ(AT_SPAWN_ID, 16);
    EXPECT_EQ(AT_HP_UPDATE, 17);
    EXPECT_EQ(AT_LINKDEAD, 18);
    EXPECT_EQ(AT_FLYMODE, 19);
}

TEST_F(AppearanceTypeTest, PlayerState) {
    EXPECT_EQ(AT_GM, 20);
    EXPECT_EQ(AT_ANONYMOUS, 21);
    EXPECT_EQ(AT_GUILD_ID, 22);
    EXPECT_EQ(AT_GUILD_RANK, 23);
    EXPECT_EQ(AT_AFK, 24);
    EXPECT_EQ(AT_PET, 25);
    EXPECT_EQ(AT_SUMMONED, 27);
    EXPECT_EQ(AT_SPLIT, 28);
    EXPECT_EQ(AT_SIZE, 29);
}

// Test MovementMode enum
class MovementModeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MovementModeTest, AllModes) {
    EXPECT_EQ(MOVE_MODE_RUN, 0);
    EXPECT_EQ(MOVE_MODE_WALK, 1);
    EXPECT_EQ(MOVE_MODE_SNEAK, 2);
}

// Test PositionState enum
class PositionStateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(PositionStateTest, AllStates) {
    EXPECT_EQ(POS_STANDING, 0);
    EXPECT_EQ(POS_SITTING, 1);
    EXPECT_EQ(POS_CROUCHING, 2);
    EXPECT_EQ(POS_FEIGN_DEATH, 3);
    EXPECT_EQ(POS_DEAD, 4);
}

// Test ChatChannelType enum
class ChatChannelTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ChatChannelTest, AllChannels) {
    EXPECT_EQ(CHAT_CHANNEL_GUILD, 0);
    EXPECT_EQ(CHAT_CHANNEL_GROUP, 2);
    EXPECT_EQ(CHAT_CHANNEL_SHOUT, 3);
    EXPECT_EQ(CHAT_CHANNEL_AUCTION, 4);
    EXPECT_EQ(CHAT_CHANNEL_OOC, 5);
    EXPECT_EQ(CHAT_CHANNEL_BROADCAST, 6);
    EXPECT_EQ(CHAT_CHANNEL_TELL, 7);
    EXPECT_EQ(CHAT_CHANNEL_SAY, 8);
    EXPECT_EQ(CHAT_CHANNEL_PETITION, 10);
    EXPECT_EQ(CHAT_CHANNEL_GMSAY, 11);
    EXPECT_EQ(CHAT_CHANNEL_RAID, 15);
    EXPECT_EQ(CHAT_CHANNEL_EMOTE, 22);
}

// Test UCS opcodes
class UCSOpcodesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(UCSOpcodesTest, ChatOpcodes) {
    EXPECT_EQ(HC_OP_UCS_MailLogin, 0x00);
    EXPECT_EQ(HC_OP_UCS_ChatMessage, 0x01);
    EXPECT_EQ(HC_OP_UCS_ChatJoin, 0x02);
    EXPECT_EQ(HC_OP_UCS_ChatLeave, 0x03);
    EXPECT_EQ(HC_OP_UCS_ChatWho, 0x04);
    EXPECT_EQ(HC_OP_UCS_ChatInvite, 0x05);
    EXPECT_EQ(HC_OP_UCS_ChatModerate, 0x06);
    EXPECT_EQ(HC_OP_UCS_ChatGrant, 0x07);
    EXPECT_EQ(HC_OP_UCS_ChatVoice, 0x08);
    EXPECT_EQ(HC_OP_UCS_ChatKick, 0x09);
    EXPECT_EQ(HC_OP_UCS_ChatSetOwner, 0x0a);
    EXPECT_EQ(HC_OP_UCS_ChatOPList, 0x0b);
    EXPECT_EQ(HC_OP_UCS_ChatList, 0x0c);
}

TEST_F(UCSOpcodesTest, MailOpcodes) {
    EXPECT_EQ(HC_OP_UCS_MailHeaderCount, 0x20);
    EXPECT_EQ(HC_OP_UCS_MailHeader, 0x21);
    EXPECT_EQ(HC_OP_UCS_MailGetBody, 0x22);
    EXPECT_EQ(HC_OP_UCS_MailSendBody, 0x23);
    EXPECT_EQ(HC_OP_UCS_MailDeleteMsg, 0x24);
    EXPECT_EQ(HC_OP_UCS_MailNew, 0x25);
}

TEST_F(UCSOpcodesTest, BuddyOpcodes) {
    EXPECT_EQ(HC_OP_UCS_Buddy, 0x40);
    EXPECT_EQ(HC_OP_UCS_Ignore, 0x41);
}

// Note: We can't test EverQuest class methods without linking the full client,
// but we can verify the header compiles and all enums/structs are properly defined.

// Test additional emote animations used by main.cpp commands
TEST_F(AnimationTest, AdditionalEmotes) {
    // Emotes used by the emote command
    EXPECT_EQ(ANIM_CHEER, 27);  // Note: same as ANIM_RUN in Titanium client
    EXPECT_EQ(ANIM_CRY, 18);
    EXPECT_EQ(ANIM_KNEEL, 19);
    EXPECT_EQ(ANIM_LAUGH, 63);
    EXPECT_EQ(ANIM_POINT, 64);
    EXPECT_EQ(ANIM_SALUTE, 67);
    EXPECT_EQ(ANIM_SHRUG, 65);
    EXPECT_EQ(ANIM_WAVE, 29);
    EXPECT_EQ(ANIM_DANCE, 58);
}

// Test position state values used by sit/stand/crouch/feign commands
TEST_F(PositionStateTest, CommandStates) {
    // Position states used by main.cpp commands
    EXPECT_EQ(POS_STANDING, 0);
    EXPECT_EQ(POS_SITTING, 1);
    EXPECT_EQ(POS_CROUCHING, 2);
    EXPECT_EQ(POS_FEIGN_DEATH, 3);
}

// Test movement modes used by walk/run/sneak commands
TEST_F(MovementModeTest, CommandModes) {
    // Movement modes used by main.cpp commands
    EXPECT_EQ(MOVE_MODE_RUN, 0);
    EXPECT_EQ(MOVE_MODE_WALK, 1);
    EXPECT_EQ(MOVE_MODE_SNEAK, 2);
}

// Test chat channels used by say/shout/ooc/auction/tell commands
TEST_F(ChatChannelTest, CommandChannels) {
    // Chat channels used by main.cpp commands
    EXPECT_EQ(CHAT_CHANNEL_SAY, 8);
    EXPECT_EQ(CHAT_CHANNEL_SHOUT, 3);
    EXPECT_EQ(CHAT_CHANNEL_OOC, 5);
    EXPECT_EQ(CHAT_CHANNEL_AUCTION, 4);
    EXPECT_EQ(CHAT_CHANNEL_TELL, 7);
}
