// B606 (#189) rewrote this suite over a throwaway SQLite db.
//
// The production StringDatabase is SQLite-backed ONLY: initFromDatabase()
// (called by eq_strings.cpp's LoadStringFiles) is the client's one
// initialization. The legacy eqstr_us.txt / dbstr_us.txt file loaders
// (loadEqStrFile/loadDbStrFile) were zero-caller dead weight, so this file
// no longer opens them. Every assertion the suite made over the legacy
// files now runs against a temp database the fixture builds, so nothing
// skips: the values below are the legacy file's own rows (the eqstr ids the
// tests name), quoted here instead of re-read from the EQ client files.
//
// The format-engine cases (formatTemplate and friends) never touched the
// storage at all and are unchanged.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "client/formatted_message.h"
#include "client/string_database.h"
#include "client/willeq_database.h"

namespace {

std::string TempDbPath(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = tmp ? tmp : "/tmp";
    return dir + "/string_db_test_" + name + "_" + std::to_string(::getpid()) + ".sqlite";
}

// A StringDatabase over a throwaway SQLite db, the production mode. The
// fixture builds the db with a standalone sqlite3 handle, then opens it
// through WilleqDatabase exactly as the client does (initFromDatabase reads
// the global instance).
class StringDatabaseTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase raw;
    EQT::StringDatabase db;
    std::string path;

    void SetUp() override {
        path = TempDbPath("strings");
        std::remove(path.c_str());
        sqlite3* build = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &build), SQLITE_OK);
        exec(build, "CREATE TABLE eqstrings (id INTEGER PRIMARY KEY, text TEXT NOT NULL);");
        exec(build, "CREATE TABLE dbstrings (category INTEGER, sub_id INTEGER, text TEXT NOT NULL);");
        // open() validates the schema version, so the temp db needs the
        // metadata table too (the same table the builder writes).
        exec(build, "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT); "
            "INSERT INTO metadata VALUES ('version', '" +
            std::to_string(EQT::kWilleqDbExpectedVersion) + "');");
        // Rows are the legacy eqstr file's own values, keyed by the ids the
        // tests below name.
        eqstr(build, 100, "Your target is out of range, get closer!");
        eqstr(build, 138, "You gain experience!!");
        eqstr(build, 339, "You have fashioned the items together to create something new!");
        eqstr(build, 436, "Your %1 spell has worn off of %2.");
        eqstr(build, 467, "--You have looted a %1.--");
        eqstr(build, 554, "%1 says '%T2'");
        eqstr(build, 1032, "%1 says '%2'");
        eqstr(build, 1034, "%1 shouts '%2'");
        eqstr(build, 1095, "I'll teach you to interfere with me %3.");
        eqstr(build, 1132, "Following you, Master.");
        eqstr(build, 12113, "You have slain %1!");
        // Non-empty stand-ins for the existence checks (Integration_SpellErrors).
        eqstr(build, 199, "You cannot afford the mana cost of that spell.");
        eqstr(build, 207, "You cannot cast while stunned.");
        eqstr(build, 214, "Target out of reach.");
        eqstr(build, 236, "Your spell was interrupted.");
        eqstr(build, 237, "You cannot cast while swimming.");
        dbstr(build, 1, 11, "Human");
        dbstr(build, 1, 12, "Humans");
        sqlite3_close(build);

        ASSERT_TRUE(raw.open(path));
        EQT::WilleqDatabase::setGlobalInstance(&raw);
        ASSERT_TRUE(db.initFromDatabase());
    }

    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        raw.close();
        std::remove(path.c_str());
    }

    void exec(sqlite3* s, const std::string& sql) {
        char* err = nullptr;
        ASSERT_EQ(sqlite3_exec(s, sql.c_str(), nullptr, nullptr, &err), SQLITE_OK)
            << (err ? err : "");
        if (err) sqlite3_free(err);
    }

    void eqstr(sqlite3* s, uint32_t id, const std::string& text) {
        // sqlite3_prepare_v2's tail out-param is `const char**`; the
        // sqlite3_exec helper above takes a `char**`, hence the two
        // different declarations.
        const char* err = nullptr;
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(s, "INSERT INTO eqstrings VALUES (?, ?)", -1, &stmt, &err), SQLITE_OK);
        sqlite3_bind_int(stmt, 1, static_cast<int>(id));
        sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    void dbstr(sqlite3* s, uint32_t cat, uint32_t sub, const std::string& text) {
        const char* err = nullptr;
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(s, "INSERT INTO dbstrings VALUES (?, ?, ?)", -1, &stmt, &err), SQLITE_OK);
        sqlite3_bind_int(stmt, 1, static_cast<int>(cat));
        sqlite3_bind_int(stmt, 2, static_cast<int>(sub));
        sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
};

}  // namespace

// Test string not found returns empty
TEST_F(StringDatabaseTest, GetString_NotFound) {
    EXPECT_EQ(db.getString(99999999), "");
}

// Test basic string lookup
TEST_F(StringDatabaseTest, GetString_KnownIds) {
    // Test known string IDs
    EXPECT_EQ(db.getString(100), "Your target is out of range, get closer!");
    EXPECT_EQ(db.getString(138), "You gain experience!!");
    EXPECT_EQ(db.getString(467), "--You have looted a %1.--");
    EXPECT_EQ(db.getString(554), "%1 says '%T2'");
    EXPECT_EQ(db.getString(1032), "%1 says '%2'");
    EXPECT_EQ(db.getString(1034), "%1 shouts '%2'");
    EXPECT_EQ(db.getString(1132), "Following you, Master.");
}

// Test DB string lookup
TEST_F(StringDatabaseTest, GetDbString_KnownIds) {
    // 1^11^Human
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
    // String 1032 = "%1 says '%2'"
    std::vector<std::string> args = {"Guard Hanns", "Halt! Who goes there?"};

    std::string result = db.formatString(1032, args);
    EXPECT_EQ(result, "Guard Hanns says 'Halt! Who goes there?'");
}

// Test formatString with %T lookup
TEST_F(StringDatabaseTest, FormatString_WithLookup) {
    // String 554 = "%1 says '%T2'"
    std::vector<std::string> args = {"Xararn", "1132"};

    std::string result = db.formatString(554, args);
    EXPECT_EQ(result, "Xararn says 'Following you, Master.'");
}

// Test loot message formatting
TEST_F(StringDatabaseTest, FormatString_LootMessage) {
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

// T22: %B (ability/buff) and %z (duration) placeholders had no coverage at
// all — substitutePlaceholder() has dedicated branches for both, but every
// existing test only drives %N/%T/# /@. These pin the branches' actual
// (if crude — "For now, just return the arg directly") documented behavior
// so a mutation to either is observable.
TEST_F(StringDatabaseTest, FormatTemplate_BPlaceholder_NoParen) {
    // No "(N)" suffix: %B2 behaves like a plain positional substitution.
    std::string tmpl = "You resist %B2!";
    std::vector<std::string> args = {"Fear", "the effect"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "You resist the effect!");
}

TEST_F(StringDatabaseTest, FormatTemplate_BPlaceholder_WithParen) {
    // "(N)" suffix (single-digit N only, per the hardcoded -3 offset):
    // %B1(5) — the parenthesized part is consumed but not itself used for
    // indexing; arg_num still comes from the digits before '('.
    std::string tmpl = "You cast %B1(5)!";
    std::vector<std::string> args = {"Fireball"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "You cast Fireball!");
}

TEST_F(StringDatabaseTest, FormatTemplate_BPlaceholder_OutOfRangeIndex) {
    // arg_num (2) is one past the last valid index for a 1-element args
    // vector — must resolve to empty, not read past the end.
    std::string tmpl = "You resist %B2!";
    std::vector<std::string> args = {"Fear"};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "You resist !");
}

TEST_F(StringDatabaseTest, FormatTemplate_ZPlaceholder) {
    // %z has no arg source (would come from spell data); it always emits
    // the literal "[duration]" placeholder text.
    std::string tmpl = "Effect lasts %z.";
    std::vector<std::string> args = {};

    std::string result = db.formatTemplate(tmpl, args);
    EXPECT_EQ(result, "Effect lasts [duration].");
}

// Test spell worn off message
TEST_F(StringDatabaseTest, FormatString_SpellWornOff) {
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
    // String 138 = "You gain experience!!"
    // SimpleMessage packets typically have no args for this message
    std::string result = db.getString(138);
    EXPECT_EQ(result, "You gain experience!!");
}

// Integration test: SimpleMessage with "Your target is out of range"
TEST_F(StringDatabaseTest, Integration_SimpleMessage_OutOfRange) {
    // String 100 = "Your target is out of range, get closer!"
    std::string result = db.getString(100);
    EXPECT_EQ(result, "Your target is out of range, get closer!");
}

// Integration test: FormattedMessage NPC dialogue with direct substitution
TEST_F(StringDatabaseTest, Integration_FormattedMessage_NpcSays) {
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
    // String 339 = "You have fashioned the items together to create something new!"
    // This is typically sent with no args
    std::string result = db.getString(339);
    EXPECT_EQ(result, "You have fashioned the items together to create something new!");
}

// Integration test: Combat "slain" message
TEST_F(StringDatabaseTest, Integration_SimpleMessage_Slain) {
    // String 12113 = "You have slain %1!"
    std::vector<std::string> args = {"Fippy Darkpaw"};
    std::string result = db.formatString(12113, args);
    EXPECT_EQ(result, "You have slain Fippy Darkpaw!");
}

// Integration test: Verify common spell error messages exist
TEST_F(StringDatabaseTest, Integration_SpellErrors) {
    // Verify common spell error string IDs are loaded
    EXPECT_FALSE(db.getString(199).empty());  // Insufficient mana
    EXPECT_FALSE(db.getString(207).empty());  // Cannot cast while stunned
    EXPECT_FALSE(db.getString(214).empty());  // Cannot reach target
    EXPECT_FALSE(db.getString(236).empty());  // Your spell is interrupted
    EXPECT_FALSE(db.getString(237).empty());  // You cannot cast spells while swimming
}

// Integration test: Verify "You have slain" message variants
TEST_F(StringDatabaseTest, Integration_SlainMessageVariants) {
    // String 12113 = "You have slain %1!"
    std::string slain12113 = db.getString(12113);
    EXPECT_EQ(slain12113, "You have slain %1!");

    // Test with formatting
    std::vector<std::string> args = {"a giant rat"};
    std::string formatted = db.formatString(12113, args);
    EXPECT_EQ(formatted, "You have slain a giant rat!");
}
