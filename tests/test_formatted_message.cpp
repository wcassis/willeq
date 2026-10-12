#include <gtest/gtest.h>
#include "client/formatted_message.h"
#include <vector>
#include <string>

class FormattedMessageTest : public ::testing::Test {
protected:
    // Helper to build test data from a string with embedded binary
    std::vector<uint8_t> buildTestData(const std::string& text) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    // Helper to build link with hex metadata and name
    void appendLink(std::vector<uint8_t>& data, const std::string& hexMetadata, const std::string& name) {
        data.push_back('[');
        data.push_back(0x12);
        for (char c : hexMetadata) data.push_back(static_cast<uint8_t>(c));
        for (char c : name) data.push_back(static_cast<uint8_t>(c));
        data.push_back(0x12);
        data.push_back(']');
    }

    // B606 (#189): parseFormattedMessageText() is gone; the tests that
    // compared its display string join the parsed args themselves, with
    // single spaces between them -- the legacy parser's null->space
    // rendering.
    std::string joinArgs(const eqt::ParsedFormattedMessageWithArgs& p) const {
        std::string out;
        for (const auto& a : p.args) {
            if (!out.empty()) out += " ";
            out += a;
        }
        return out;
    }
};

// Test plain message with no links
TEST_F(FormattedMessageTest, PlainMessage_NoLinks) {
    std::string plain = "Hello, this is a plain message with no links.";
    auto data = buildTestData(plain);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, plain);
}

// Test message with null bytes replaced by spaces
TEST_F(FormattedMessageTest, NullBytesReplacedWithSpaces) {
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', 0x00, 'W', 'o', 'r', 'l', 'd'};

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Hello World");
}

// Test single link with 45-char hex metadata
TEST_F(FormattedMessageTest, SingleLink_45CharMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    // 45-char metadata as observed in real packets
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");
    for (char c : std::string(" now")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Talk to [Renux] now");
}

// Test single link with long hex metadata (56 chars like real EQ)
TEST_F(FormattedMessageTest, SingleLink_LongMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    // 56 char hex metadata (typical EQ format)
    appendLink(data, "0FFFFF0000F000000000000000000000000000000000000000000000", "Hanns");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "See [Hanns]");
}

// Test multiple links with both 45-char and 56-char metadata
TEST_F(FormattedMessageTest, MultipleLinks_MixedMetadataLengths) {
    std::vector<uint8_t> data;
    for (char c : std::string("Get ")) data.push_back(c);
    // 45-char metadata
    appendLink(data, "0FFFFF000050000000000000000000000000000000000", "Item1");
    for (char c : std::string(" and ")) data.push_back(c);
    // 56-char metadata
    appendLink(data, "0FFFFF00006000000000000000000000000000000000000000000000", "Item2");
    for (char c : std::string(" and ")) data.push_back(c);
    // Another 45-char metadata
    appendLink(data, "0FFFFF000070000000000000000000000000000000000", "Item3");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Get [Item1] and [Item2] and [Item3]");
}

// Test five links in a row with 45-char metadata (short format)
TEST_F(FormattedMessageTest, ManyLinks_FiveInRow_ShortMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);

    // Use 45-char metadata (short format observed in Titanium)
    const char* names[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};
    std::string metadata_45 = "0FFFFF0000D0000000000000000000000000000000000";  // 45 chars
    for (int n = 0; n < 5; n++) {
        appendLink(data, metadata_45, names[n]);
        if (n < 4) {
            for (char c : std::string(", ")) data.push_back(c);
        }
    }

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_NE(result.find("[Alpha]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Beta]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Gamma]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Delta]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Epsilon]"), std::string::npos) << "Result: " << result;
}

// Test five links in a row with 56-char metadata (long format)
TEST_F(FormattedMessageTest, ManyLinks_FiveInRow_LongMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);

    // Use 56-char metadata (long format observed in Titanium)
    const char* names[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};
    std::string metadata_56 = "0FFFFF0000D000000000000000000000000000000000000000000000";  // 56 chars
    for (int n = 0; n < 5; n++) {
        appendLink(data, metadata_56, names[n]);
        if (n < 4) {
            for (char c : std::string(", ")) data.push_back(c);
        }
    }

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_NE(result.find("[Alpha]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Beta]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Gamma]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Delta]"), std::string::npos) << "Result: " << result;
    EXPECT_NE(result.find("[Epsilon]"), std::string::npos) << "Result: " << result;
}

// Test real EQ packet format (from qeynos2 zone log)
TEST_F(FormattedMessageTest, RealPacket_Renux_Hanns) {
    std::vector<uint8_t> data;
    for (char c : std::string("mess around with ")) data.push_back(c);
    appendLink(data, "0FFFFF0000F000000000000000000000000000000000000000000000", "Renux");
    for (char c : std::string(". She's in tight with ")) data.push_back(c);
    appendLink(data, "0FFFFF00010000000000000000000000000000000000000000000000", "Hanns");
    for (char c : std::string(", and you know how he is.")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_NE(result.find("[Renux]"), std::string::npos);
    EXPECT_NE(result.find("[Hanns]"), std::string::npos);
    EXPECT_NE(result.find("mess around with"), std::string::npos);
    EXPECT_NE(result.find("and you know how he is"), std::string::npos);
}

// Test name starting with uppercase letter that could be hex (A-F)
// With fixed 45/56 char metadata sizes, this now works correctly
TEST_F(FormattedMessageTest, NameStartsWithHexLetter) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    // 45-char metadata followed by name starting with 'A'
    appendLink(data, "0FFFFF0000D0000000000000000000000000000000000", "Arnold");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_NE(result.find("[Arnold]"), std::string::npos) << "Result: " << result;
}

// Test empty link (just the markers with no content)
TEST_F(FormattedMessageTest, EmptyLink) {
    std::vector<uint8_t> data;
    for (char c : std::string("Before ")) data.push_back(c);
    data.push_back('[');
    data.push_back(0x12);
    data.push_back(0x12);
    data.push_back(']');
    for (char c : std::string(" after")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Before [] after");
}

// Test link with only hex metadata (no readable name)
TEST_F(FormattedMessageTest, LinkWithOnlyHexMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    data.push_back('[');
    data.push_back(0x12);
    for (char c : std::string("ABCD1234")) data.push_back(c);  // All hex, no name
    data.push_back(0x12);
    data.push_back(']');

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "See []");
}

// Test unpaired link marker (just opening, no closing)
TEST_F(FormattedMessageTest, UnpairedLinkMarker) {
    std::vector<uint8_t> data;
    for (char c : std::string("Text with ")) data.push_back(c);
    data.push_back('[');
    data.push_back(0x12);
    for (char c : std::string("ABCDName")) data.push_back(c);
    // No closing 0x12
    for (char c : std::string(" more text")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    // Content inside unpaired link is skipped
    EXPECT_EQ(result, "Text with [");
}

// Test message with NPC name and dialogue (typical FormattedMessage)
TEST_F(FormattedMessageTest, NPCDialogue_WithNullSeparator) {
    std::vector<uint8_t> data;
    // NPC name
    for (char c : std::string("Zannsin Resdinet")) data.push_back(c);
    data.push_back(0x00);  // Null separator
    // Dialogue
    for (char c : std::string("Yeah, whatever, Knarg.")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Zannsin Resdinet Yeah, whatever, Knarg.");
}

// Test trimming of leading and trailing spaces
TEST_F(FormattedMessageTest, TrimsLeadingAndTrailingSpaces) {
    std::vector<uint8_t> data;
    for (char c : std::string("   Hello World   ")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "Hello World");
}

// Test non-printable characters outside links are skipped
TEST_F(FormattedMessageTest, NonPrintableCharsSkipped) {
    std::vector<uint8_t> data = {'H', 'i', 0x01, 0x02, 0x03, 'B', 'y', 'e'};

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "HiBye");
}

// ============================================================================
// Structured Parsing Tests (parseFormattedMessageArgs)
// ============================================================================

// Test structured parsing returns same display text as simple parser
TEST_F(FormattedMessageTest, StructuredParsing_SameDisplayText) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");
    for (char c : std::string(" now")) data.push_back(c);

    // B606 (#189): the second, structured parser (parseFormattedMessage,
    // per-link startPos/endPos) had no production caller and is deleted;
    // the test keeps its point -- parsing twice is stable.
    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string first = joinArgs(_p);
    eqt::ParsedFormattedMessageWithArgs _p2 = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string second = joinArgs(_p2);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, "Talk to [Renux] now");
}

// Test single link has correct position
TEST_F(FormattedMessageTest, StructuredParsing_SingleLinkPosition) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    appendLink(data, "0FFFFF0000D0000000000000000000000000000000000", "Arnold");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string flat = joinArgs(result);

    EXPECT_EQ(flat, "See [Arnold]");
    ASSERT_EQ(result.links.size(), 1u);

    const auto& link = result.links[0];
    EXPECT_EQ(link.displayText, "Arnold");
    EXPECT_EQ(flat.substr(4, 8), "[Arnold]");
}

// Test multiple links have correct non-overlapping positions
TEST_F(FormattedMessageTest, StructuredParsing_MultipleLinkPositions) {
    std::vector<uint8_t> data;
    for (char c : std::string("Get ")) data.push_back(c);
    appendLink(data, "0FFFFF000050000000000000000000000000000000000", "Item1");
    for (char c : std::string(" and ")) data.push_back(c);
    appendLink(data, "0FFFFF00006000000000000000000000000000000000000000000000", "Item2");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string flat = joinArgs(result);

    EXPECT_EQ(flat, "Get [Item1] and [Item2]");
    ASSERT_EQ(result.links.size(), 2u);

    // Links live in order, at their flat positions.
    EXPECT_EQ(result.links[0].displayText, "Item1");
    EXPECT_EQ(flat.find("[Item1]"), 4u);
    EXPECT_EQ(result.links[1].displayText, "Item2");
    EXPECT_EQ(flat.find("[Item2]"), 16u);
    EXPECT_LT(flat.find("[Item1]"), flat.find("[Item2]"));
}

// Test metadata is preserved in link
TEST_F(FormattedMessageTest, StructuredParsing_MetadataPreserved) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    std::string metadata = "0FFFFF0000F0000000000000000000000000000000000";  // 45 chars
    appendLink(data, metadata, "Renux");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].metadata, metadata);
}

// Test 56-char metadata is preserved
TEST_F(FormattedMessageTest, StructuredParsing_LongMetadataPreserved) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    std::string metadata = "0FFFFF0000F000000000000000000000000000000000000000000000";  // 56 chars
    appendLink(data, metadata, "Hanns");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].metadata, metadata);
}

// Test NPC link type detection
TEST_F(FormattedMessageTest, StructuredParsing_NPCLinkType) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    // NPC-style metadata with small ID value
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].type, eqt::LinkType::NPCName);
}

// Test that all 0FFFFF links are treated as NPC links (including multi-link messages)
// The value at positions 6-9 is a link index, not an item ID
TEST_F(FormattedMessageTest, StructuredParsing_MultipleNPCLinks) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    // First link with link index 0 at positions 6-9 (45 char metadata)
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");
    for (char c : std::string(" or ")) data.push_back(c);
    // Second link with link index 1 at positions 6-9 (45 char metadata)
    appendLink(data, "0FFFFF0001F0000000000000000000000000000000000", "Hanns");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 2u);
    // Both links should be NPC type, not Item type
    EXPECT_EQ(result.links[0].type, eqt::LinkType::NPCName);
    EXPECT_EQ(result.links[0].displayText, "Renux");
    EXPECT_EQ(result.links[1].type, eqt::LinkType::NPCName);
    EXPECT_EQ(result.links[1].displayText, "Hanns");
}

// Test message with no links returns empty links vector
TEST_F(FormattedMessageTest, StructuredParsing_NoLinks) {
    std::string plain = "This is a plain message.";
    std::vector<uint8_t> data(plain.begin(), plain.end());

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string flat = joinArgs(result);

    EXPECT_EQ(flat, plain);
    EXPECT_TRUE(result.links.empty());
}

// Test empty link (just markers, no content)
TEST_F(FormattedMessageTest, StructuredParsing_EmptyLink) {
    std::vector<uint8_t> data;
    for (char c : std::string("Before ")) data.push_back(c);
    data.push_back('[');
    data.push_back(0x12);
    data.push_back(0x12);
    data.push_back(']');
    for (char c : std::string(" after")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string flat = joinArgs(result);

    EXPECT_EQ(flat, "Before [] after");
    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].displayText, "");
    EXPECT_EQ(result.links[0].metadata, "");
}

// Test link positions are adjusted when leading spaces are trimmed
TEST_F(FormattedMessageTest, StructuredParsing_LeadingSpacesTrimmed) {
    std::vector<uint8_t> data;
    for (char c : std::string("   Talk to ")) data.push_back(c);  // 3 leading spaces
    appendLink(data, "0FFFFF0000D0000000000000000000000000000000000", "Guard");

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    // Leading spaces should be trimmed
    std::string flat = joinArgs(result);
    EXPECT_EQ(flat, "Talk to [Guard]");
    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(flat.find("[Guard]"), 8u);
}

// Test five links all have correct positions
TEST_F(FormattedMessageTest, StructuredParsing_FiveLinks) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);

    const char* names[] = {"A", "BB", "CCC", "DDDD", "EEEEE"};
    std::string metadata_45 = "0FFFFF0000D0000000000000000000000000000000000";

    for (int i = 0; i < 5; i++) {
        appendLink(data, metadata_45, names[i]);
        if (i < 4) {
            for (char c : std::string(" ")) data.push_back(c);
        }
    }

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    // Expected: "See [A] [BB] [CCC] [DDDD] [EEEEE]"
    ASSERT_EQ(result.links.size(), 5u);

    // Verify each link's display text and its flat position
    const std::string expected = "See [A] [BB] [CCC] [DDDD] [EEEEE]";
    std::string flat = joinArgs(result);
    EXPECT_EQ(flat, expected);
    size_t at = 0;
    for (size_t i = 0; i < result.links.size(); i++) {
        EXPECT_EQ(result.links[i].displayText, names[i])
            << "Link " << i << " wrong display text";
        at = flat.find("[" + std::string(names[i]) + "]", at);
        ASSERT_NE(at, std::string::npos) << "Link " << i << " missing in flat text";
        at += 1;
    }
}

// ============================================================================
// Tests for parseFormattedMessageArgs (null-byte delimited argument parsing)
// ============================================================================

// Test basic null-byte separated arguments
TEST_F(FormattedMessageTest, Args_NullByteSeparatedArgs) {
    // "Fippy Darkpaw" <null> "1095" <null> "Xararn"
    std::vector<uint8_t> data;
    for (char c : std::string("Fippy Darkpaw")) data.push_back(c);
    data.push_back(0x00);
    for (char c : std::string("1095")) data.push_back(c);
    data.push_back(0x00);
    for (char c : std::string("Xararn")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 3u);
    EXPECT_EQ(result.args[0], "Fippy Darkpaw");
    EXPECT_EQ(result.args[1], "1095");
    EXPECT_EQ(result.args[2], "Xararn");
}

// Test single argument (no null bytes)
TEST_F(FormattedMessageTest, Args_SingleArg) {
    std::string text = "Just one argument";
    std::vector<uint8_t> data(text.begin(), text.end());

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 1u);
    EXPECT_EQ(result.args[0], "Just one argument");
}

// Test with trailing null bytes (should be ignored)
TEST_F(FormattedMessageTest, Args_TrailingNulls) {
    std::vector<uint8_t> data;
    for (char c : std::string("Arg1")) data.push_back(c);
    data.push_back(0x00);
    for (char c : std::string("Arg2")) data.push_back(c);
    data.push_back(0x00);
    data.push_back(0x00);  // Trailing null should be ignored

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 2u);
    EXPECT_EQ(result.args[0], "Arg1");
    EXPECT_EQ(result.args[1], "Arg2");
}

// Test with leading/trailing spaces in arguments
TEST_F(FormattedMessageTest, Args_TrimSpaces) {
    std::vector<uint8_t> data;
    for (char c : std::string("  Arg1  ")) data.push_back(c);
    data.push_back(0x00);
    for (char c : std::string("  Arg2  ")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 2u);
    EXPECT_EQ(result.args[0], "Arg1");
    EXPECT_EQ(result.args[1], "Arg2");
}

// Test NPC dialogue format: "NPC Name" <null> "String ID" <null> "Target Name"
TEST_F(FormattedMessageTest, Args_NPCDialogueFormat) {
    // Based on plan example: Fippy Darkpaw\x00 1095\x00 Xararn\x00
    std::vector<uint8_t> data;
    for (char c : std::string("Fippy Darkpaw")) data.push_back(c);
    data.push_back(0x00);
    for (char c : std::string(" 1095")) data.push_back(c);  // Note: space before number
    data.push_back(0x00);
    for (char c : std::string(" Xararn")) data.push_back(c);
    data.push_back(0x00);

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 3u);
    EXPECT_EQ(result.args[0], "Fippy Darkpaw");
    EXPECT_EQ(result.args[1], "1095");  // Space should be trimmed
    EXPECT_EQ(result.args[2], "Xararn");
}

// Test with links embedded in arguments
TEST_F(FormattedMessageTest, Args_WithLinks) {
    std::vector<uint8_t> data;
    for (char c : std::string("NPC Name")) data.push_back(c);
    data.push_back(0x00);
    // Second arg contains a link
    for (char c : std::string("Talk to ")) data.push_back(c);
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Guard");
    for (char c : std::string(" about quest")) data.push_back(c);

    eqt::ParsedFormattedMessageWithArgs result = eqt::parseFormattedMessageArgs(data.data(), data.size());

    ASSERT_EQ(result.args.size(), 2u);
    EXPECT_EQ(result.args[0], "NPC Name");
    // Note: brackets are preserved as they're part of the link display format
    EXPECT_EQ(result.args[1], "Talk to [Guard] about quest");
    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].displayText, "Guard");
}

// ---------------------------------------------------------------------------
// T34 regression: long item names with SHORT (45-char) metadata
//
// Will spotted the symptom in T34's live loot evidence: the client rendered
// "You have looted a kin." for the item "Scaled Curskin". Root cause was a
// purely length-based metadata-size guess -- link_len > 56 was taken to mean
// 56-char metadata, but 45-char metadata plus a 12+ character name also
// exceeds 56, so the name started 11 characters too late.
//
// Every pre-existing test here used a short name ("Renux", "Guard",
// "Epsilon" -- 7 chars at most), which stays under the 56 threshold and so
// never exercised the broken branch. These cases sit either side of it.
// ---------------------------------------------------------------------------

TEST_F(FormattedMessageTest, ShortMetadata_LongItemName_NameNotTruncated) {
    // 45-char metadata + a 14-char name => link_len 59, the exact shape that
    // used to yield "kin".
    std::vector<uint8_t> data;
    for (char c : std::string("You have looted a ")) data.push_back(c);
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Scaled Curskin");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "You have looted a [Scaled Curskin]");
}

TEST_F(FormattedMessageTest, ShortMetadata_TwelveCharName_IsTheBoundaryCase) {
    // 45 + 12 = 57, the first length that tripped the old rule.
    std::vector<uint8_t> data;
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Rusty Dagger");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "[Rusty Dagger]");
}

TEST_F(FormattedMessageTest, LongMetadata_WithLongName_StillParsesAsLongMetadata) {
    // The other side of the fix: genuine 56-char metadata must still be
    // recognised, and its name must not lose its first 11 characters.
    std::vector<uint8_t> data;
    appendLink(data, "0FFFFF0000D000000000000000000000000000000000000000000000", "Scaled Curskin");

    eqt::ParsedFormattedMessageWithArgs _p = eqt::parseFormattedMessageArgs(data.data(), data.size());
    std::string result = joinArgs(_p);
    EXPECT_EQ(result, "[Scaled Curskin]");
}

TEST_F(FormattedMessageTest, ShortMetadata_LongName_LinkDisplayTextIsWholeName) {
    // Same fix seen through the structured parser, not just the flattened text.
    std::vector<uint8_t> data;
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Scaled Curskin");

    auto result = eqt::parseFormattedMessageArgs(data.data(), data.size());
    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].displayText, "Scaled Curskin");
}
