#include <gtest/gtest.h>
#include "client/string_database.h"
#include "client/formatted_message.h"
#include <fstream>
#include <cstdlib>

class StringDatabaseTest : public ::testing::Test {
protected:
    EQT::StringDatabase db;

    // Path to EQ client string files
    std::string getEqClientPath() {
        // Check environment variable first
        const char* env_path = std::getenv("EQ_CLIENT_PATH");
        if (env_path) {
            return env_path;
        }
        // Default to known location
        return "/home/user/projects/claude/EverQuestP1999";
    }

    std::string getEqStrPath() {
        return getEqClientPath() + "/eqstr_us.txt";
    }

    std::string getDbStrPath() {
        return getEqClientPath() + "/dbstr_us.txt";
    }

    void SetUp() override {
        // Load string files if they exist
        std::ifstream test_file(getEqStrPath());
        if (test_file.good()) {
            db.loadEqStrFile(getEqStrPath());
        }
        test_file.close();

        test_file.open(getDbStrPath());
        if (test_file.good()) {
            db.loadDbStrFile(getDbStrPath());
        }
    }
};

// Test loading eqstr_us.txt
TEST_F(StringDatabaseTest, LoadEqStrFile) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    EXPECT_GT(db.getEqStrCount(), 5000);  // Should have ~5900 strings
}

// Test loading dbstr_us.txt
TEST_F(StringDatabaseTest, LoadDbStrFile) {
    if (!db.isDbStrLoaded()) {
        GTEST_SKIP() << "dbstr_us.txt not available";
    }

    EXPECT_GT(db.getDbStrCount(), 8000);  // Should have ~8300 strings
}

// Test basic string lookup
TEST_F(StringDatabaseTest, GetString_KnownIds) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Test known string IDs
    EXPECT_EQ(db.getString(100), "Your target is out of range, get closer!");
    EXPECT_EQ(db.getString(138), "You gain experience!!");
    EXPECT_EQ(db.getString(467), "--You have looted a %1.--");
    EXPECT_EQ(db.getString(554), "%1 says '%T2'");
    EXPECT_EQ(db.getString(1032), "%1 says '%2'");
    EXPECT_EQ(db.getString(1034), "%1 shouts '%2'");
    EXPECT_EQ(db.getString(1132), "Following you, Master.");
}

// Test string not found returns empty
TEST_F(StringDatabaseTest, GetString_NotFound) {
    EXPECT_EQ(db.getString(99999999), "");
}

// Test DB string lookup
TEST_F(StringDatabaseTest, GetDbString_KnownIds) {
    if (!db.isDbStrLoaded()) {
        GTEST_SKIP() << "dbstr_us.txt not available";
    }

    // Test known dbstr entries (category, sub_id)
    // From dbstr_us.txt: 1^11^Human
    EXPECT_EQ(db.getDbString(1, 11), "Human");
    // 1^12^Humans
    EXPECT_EQ(db.getDbString(1, 12), "Humans");
}

// Test direct placeholder substitution %1, %2
TEST_F(StringDatabaseTest, FormatTemplate_DirectSubstitution) {
    std::string tmpl = "%1 says '%2'";
    std::vector<std::string> args = {"Fippy Darkpaw", "I will destroy you!"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "Fippy Darkpaw says 'I will destroy you!'");
}

// Test %T placeholder (string ID lookup)
TEST_F(StringDatabaseTest, FormatTemplate_StringIdLookup) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Template: %1 says '%T2'
    // Args: "Xararn", "1132"
    // String 1132 = "Following you, Master."
    // Expected: "Xararn says 'Following you, Master.'"
    std::string tmpl = "%1 says '%T2'";
    std::vector<std::string> args = {"Xararn", "1132"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "Xararn says 'Following you, Master.'");
}

// Test nested placeholders in looked-up string
TEST_F(StringDatabaseTest, FormatTemplate_NestedPlaceholders) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Template: %1 says '%T2'
    // Args: "Fippy Darkpaw", "1095", "Xararn"
    // String 1095 = "I'll teach you to interfere with me %3."
    // Expected: "Fippy Darkpaw says 'I'll teach you to interfere with me Xararn.'"
    std::string tmpl = "%1 says '%T2'";
    std::vector<std::string> args = {"Fippy Darkpaw", "1095", "Xararn"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "Fippy Darkpaw says 'I'll teach you to interfere with me Xararn.'");
}

// Test formatString (lookup template by ID then format)
TEST_F(StringDatabaseTest, FormatString_NpcDialogue) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 1032 = "%1 says '%2'"
    std::vector<std::string> args = {"Guard Hanns", "Halt! Who goes there?"};

    std::string result = db.formatString(1032, args);
    EXPECT_EQ(result, "Guard Hanns says 'Halt! Who goes there?'");
}

// Test formatString with %T lookup
TEST_F(StringDatabaseTest, FormatString_WithLookup) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 554 = "%1 says '%T2'"
    std::vector<std::string> args = {"Xararn", "1132"};

    std::string result = db.formatString(554, args);
    EXPECT_EQ(result, "Xararn says 'Following you, Master.'");
}

// Test loot message formatting
TEST_F(StringDatabaseTest, FormatString_LootMessage) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 467 = "--You have looted a %1.--"
    std::vector<std::string> args = {"Rusty Short Sword"};

    std::string result = db.formatString(467, args);
    EXPECT_EQ(result, "--You have looted a Rusty Short Sword.--");
}

// Test missing arg returns empty for that placeholder
TEST_F(StringDatabaseTest, FormatTemplate_MissingArg) {
    std::string tmpl = "%1 says '%2'";
    std::vector<std::string> args = {"OnlyOneName"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "OnlyOneName says ''");
}

// Test formatString with invalid string_id returns empty
TEST_F(StringDatabaseTest, FormatString_InvalidId) {
    std::vector<std::string> args = {"arg1", "arg2"};
    std::string result = db.formatString(99999999, args);
    EXPECT_EQ(result, "");
}

// Test # placeholder (numeric variant)
TEST_F(StringDatabaseTest, FormatTemplate_HashPlaceholder) {
    std::string tmpl = "You deal #1 damage";
    std::vector<std::string> args = {"42"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "You deal 42 damage");
}

// Test @ placeholder (another variant)
TEST_F(StringDatabaseTest, FormatTemplate_AtPlaceholder) {
    std::string tmpl = "Critical hit for @1 damage!";
    std::vector<std::string> args = {"100"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "Critical hit for 100 damage!");
}

// Test spell worn off message
TEST_F(StringDatabaseTest, FormatString_SpellWornOff) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 436 = "Your %1 spell has worn off of %2."
    std::vector<std::string> args = {"Flame Lick", "a gnoll pup"};

    std::string result = db.formatString(436, args);
    EXPECT_EQ(result, "Your Flame Lick spell has worn off of a gnoll pup.");
}

// ============================================================================
// Integration Tests: Packet Parsing + StringDatabase Formatting
// These tests verify the complete chain from raw packet data to formatted output
// ============================================================================

// Helper to build packet data with null-byte delimited args
static std::vector<uint8_t> buildPacketArgs(const std::vector<std::string>& args) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < args.size(); ++i) {
        for (char c : args[i]) {
            data.push_back(static_cast<uint8_t>(c));
        }
        if (i < args.size() - 1) {
            data.push_back(0x00);  // Null byte delimiter
        }
    }
    return data;
}

// Integration test: SimpleMessage with "You gain experience!!"
TEST_F(StringDatabaseTest, Integration_SimpleMessage_Experience) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 138 = "You gain experience!!"
    // SimpleMessage packets typically have no args for this message
    std::string result = db.getString(138);
    EXPECT_EQ(result, "You gain experience!!");
}

// Integration test: SimpleMessage with "Your target is out of range"
TEST_F(StringDatabaseTest, Integration_SimpleMessage_OutOfRange) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 100 = "Your target is out of range, get closer!"
    std::string result = db.getString(100);
    EXPECT_EQ(result, "Your target is out of range, get closer!");
}

// Integration test: FormattedMessage NPC dialogue with direct substitution
TEST_F(StringDatabaseTest, Integration_FormattedMessage_NpcSays) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with: "Guard Hanns\x00Halt! Who goes there?"
    std::vector<std::string> packetArgs = {"Guard Hanns", "Halt! Who goes there?"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    // Parse packet to get args
    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 2u);
    EXPECT_EQ(parsed.args[0], "Guard Hanns");
    EXPECT_EQ(parsed.args[1], "Halt! Who goes there?");

    // String 1032 = "%1 says '%2'"
    std::string result = db.formatString(1032, parsed.args);
    EXPECT_EQ(result, "Guard Hanns says 'Halt! Who goes there?'");
}

// Integration test: FormattedMessage NPC shout
TEST_F(StringDatabaseTest, Integration_FormattedMessage_NpcShout) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with: "Fippy Darkpaw\x00I will gnaw on your bones!"
    std::vector<std::string> packetArgs = {"Fippy Darkpaw", "I will gnaw on your bones!"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 2u);

    // String 1034 = "%1 shouts '%2'"
    std::string result = db.formatString(1034, parsed.args);
    EXPECT_EQ(result, "Fippy Darkpaw shouts 'I will gnaw on your bones!'");
}

// Integration test: FormattedMessage with %T lookup (pet dialogue)
TEST_F(StringDatabaseTest, Integration_FormattedMessage_PetDialogue) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with: "Xararn\x001132"
    // String 554 = "%1 says '%T2'"
    // String 1132 = "Following you, Master."
    std::vector<std::string> packetArgs = {"Xararn", "1132"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 2u);

    // String 554 = "%1 says '%T2'" - %T2 looks up string ID from arg 2
    std::string result = db.formatString(554, parsed.args);
    EXPECT_EQ(result, "Xararn says 'Following you, Master.'");
}

// Integration test: FormattedMessage with nested placeholders
TEST_F(StringDatabaseTest, Integration_FormattedMessage_NestedPlaceholders) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with: "Fippy Darkpaw\x001095\x00Xararn"
    // String 554 = "%1 says '%T2'"
    // String 1095 = "I'll teach you to interfere with me %3."
    std::vector<std::string> packetArgs = {"Fippy Darkpaw", "1095", "Xararn"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 3u);

    std::string result = db.formatString(554, parsed.args);
    EXPECT_EQ(result, "Fippy Darkpaw says 'I'll teach you to interfere with me Xararn.'");
}

// Integration test: Loot message
TEST_F(StringDatabaseTest, Integration_FormattedMessage_Loot) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with item name
    std::vector<std::string> packetArgs = {"Rusty Short Sword"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 1u);

    // String 467 = "--You have looted a %1.--"
    std::string result = db.formatString(467, parsed.args);
    EXPECT_EQ(result, "--You have looted a Rusty Short Sword.--");
}

// Integration test: Spell worn off message
TEST_F(StringDatabaseTest, Integration_FormattedMessage_SpellWornOff) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Simulate packet with: "Minor Healing\x00a gnoll pup"
    std::vector<std::string> packetArgs = {"Minor Healing", "a gnoll pup"};
    std::vector<uint8_t> packetData = buildPacketArgs(packetArgs);

    eqt::ParsedFormattedMessageWithArgs parsed =
        eqt::parseFormattedMessageArgs(packetData.data(), packetData.size());

    ASSERT_EQ(parsed.args.size(), 2u);

    // String 436 = "Your %1 spell has worn off of %2."
    std::string result = db.formatString(436, parsed.args);
    EXPECT_EQ(result, "Your Minor Healing spell has worn off of a gnoll pup.");
}

// Integration test: Tradeskill success message
TEST_F(StringDatabaseTest, Integration_FormattedMessage_Tradeskill) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 339 = "You have fashioned the items together to create something new!"
    // This is typically sent with no args
    std::string result = db.getString(339);
    EXPECT_EQ(result, "You have fashioned the items together to create something new!");
}

// Integration test: Combat "slain" message
TEST_F(StringDatabaseTest, Integration_SimpleMessage_Slain) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 12113 = "You have slain %1!"
    std::vector<std::string> args = {"Fippy Darkpaw"};
    std::string result = db.formatString(12113, args);
    EXPECT_EQ(result, "You have slain Fippy Darkpaw!");
}

// Integration test: Verify common spell error messages exist
TEST_F(StringDatabaseTest, Integration_SpellErrors) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // Verify common spell error string IDs are loaded
    EXPECT_FALSE(db.getString(199).empty());  // Insufficient mana
    EXPECT_FALSE(db.getString(207).empty());  // Cannot cast while stunned
    EXPECT_FALSE(db.getString(214).empty());  // Cannot reach target
    EXPECT_FALSE(db.getString(236).empty());  // Your spell is interrupted
    EXPECT_FALSE(db.getString(237).empty());  // You cannot cast spells while swimming
}

// Integration test: Verify "You have slain" message variants
TEST_F(StringDatabaseTest, Integration_SlainMessageVariants) {
    if (!db.isEqStrLoaded()) {
        GTEST_SKIP() << "eqstr_us.txt not available";
    }

    // String 12113 = "You have slain %1!"
    std::string slain12113 = db.getString(12113);
    EXPECT_EQ(slain12113, "You have slain %1!");

    // Test with formatting
    std::vector<std::string> args = {"a giant rat"};
    std::string formatted = db.formatString(12113, args);
    EXPECT_EQ(formatted, "You have slain a giant rat!");
}
