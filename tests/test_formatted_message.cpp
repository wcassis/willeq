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
};

// Test plain message with no links
TEST_F(FormattedMessageTest, PlainMessage_NoLinks) {
    std::string plain = "Hello, this is a plain message with no links.";
    auto data = buildTestData(plain);

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, plain);
}

// Test message with null bytes replaced by spaces
TEST_F(FormattedMessageTest, NullBytesReplacedWithSpaces) {
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', 0x00, 'W', 'o', 'r', 'l', 'd'};

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, "Hello World");
}

// Test single link with 45-char hex metadata
TEST_F(FormattedMessageTest, SingleLink_45CharMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    // 45-char metadata as observed in real packets
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");
    for (char c : std::string(" now")) data.push_back(c);

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, "Talk to [Renux] now");
}

// Test single link with long hex metadata (56 chars like real EQ)
TEST_F(FormattedMessageTest, SingleLink_LongMetadata) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    // 56 char hex metadata (typical EQ format)
    appendLink(data, "0FFFFF0000F000000000000000000000000000000000000000000000", "Hanns");

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
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

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, "Zannsin Resdinet Yeah, whatever, Knarg.");
}

// Test trimming of leading and trailing spaces
TEST_F(FormattedMessageTest, TrimsLeadingAndTrailingSpaces) {
    std::vector<uint8_t> data;
    for (char c : std::string("   Hello World   ")) data.push_back(c);

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, "Hello World");
}

// Test non-printable characters outside links are skipped
TEST_F(FormattedMessageTest, NonPrintableCharsSkipped) {
    std::vector<uint8_t> data = {'H', 'i', 0x01, 0x02, 0x03, 'B', 'y', 'e'};

    std::string result = eqt::parseFormattedMessageText(data.data(), data.size());
    EXPECT_EQ(result, "HiBye");
}

// ============================================================================
// Structured Parsing Tests (parseFormattedMessage)
// ============================================================================

// Test structured parsing returns same display text as simple parser
TEST_F(FormattedMessageTest, StructuredParsing_SameDisplayText) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");
    for (char c : std::string(" now")) data.push_back(c);

    std::string simpleResult = eqt::parseFormattedMessageText(data.data(), data.size());
    eqt::ParsedFormattedMessage structured = eqt::parseFormattedMessage(data.data(), data.size());

    EXPECT_EQ(structured.displayText, simpleResult);
    EXPECT_EQ(structured.displayText, "Talk to [Renux] now");
}

// Test single link has correct position
TEST_F(FormattedMessageTest, StructuredParsing_SingleLinkPosition) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    appendLink(data, "0FFFFF0000D0000000000000000000000000000000000", "Arnold");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    EXPECT_EQ(result.displayText, "See [Arnold]");
    ASSERT_EQ(result.links.size(), 1u);

    const auto& link = result.links[0];
    EXPECT_EQ(link.displayText, "Arnold");
    EXPECT_EQ(link.startPos, 4u);  // Position of '['
    EXPECT_EQ(link.endPos, 12u);   // Position after ']'

    // Verify the substring matches
    std::string linkText = result.displayText.substr(link.startPos, link.endPos - link.startPos);
    EXPECT_EQ(linkText, "[Arnold]");
}

// Test multiple links have correct non-overlapping positions
TEST_F(FormattedMessageTest, StructuredParsing_MultipleLinkPositions) {
    std::vector<uint8_t> data;
    for (char c : std::string("Get ")) data.push_back(c);
    appendLink(data, "0FFFFF000050000000000000000000000000000000000", "Item1");
    for (char c : std::string(" and ")) data.push_back(c);
    appendLink(data, "0FFFFF00006000000000000000000000000000000000000000000000", "Item2");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    EXPECT_EQ(result.displayText, "Get [Item1] and [Item2]");
    ASSERT_EQ(result.links.size(), 2u);

    // First link: [Item1] at positions 4-11
    EXPECT_EQ(result.links[0].displayText, "Item1");
    EXPECT_EQ(result.links[0].startPos, 4u);
    EXPECT_EQ(result.links[0].endPos, 11u);

    // Second link: [Item2] at positions 16-23
    EXPECT_EQ(result.links[1].displayText, "Item2");
    EXPECT_EQ(result.links[1].startPos, 16u);
    EXPECT_EQ(result.links[1].endPos, 23u);

    // Verify links don't overlap
    EXPECT_LT(result.links[0].endPos, result.links[1].startPos);

    // Verify substrings
    EXPECT_EQ(result.displayText.substr(result.links[0].startPos,
              result.links[0].endPos - result.links[0].startPos), "[Item1]");
    EXPECT_EQ(result.displayText.substr(result.links[1].startPos,
              result.links[1].endPos - result.links[1].startPos), "[Item2]");
}

// Test metadata is preserved in link
TEST_F(FormattedMessageTest, StructuredParsing_MetadataPreserved) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    std::string metadata = "0FFFFF0000F0000000000000000000000000000000000";  // 45 chars
    appendLink(data, metadata, "Renux");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].metadata, metadata);
}

// Test 56-char metadata is preserved
TEST_F(FormattedMessageTest, StructuredParsing_LongMetadataPreserved) {
    std::vector<uint8_t> data;
    for (char c : std::string("See ")) data.push_back(c);
    std::string metadata = "0FFFFF0000F000000000000000000000000000000000000000000000";  // 56 chars
    appendLink(data, metadata, "Hanns");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].metadata, metadata);
}

// Test NPC link type detection
TEST_F(FormattedMessageTest, StructuredParsing_NPCLinkType) {
    std::vector<uint8_t> data;
    for (char c : std::string("Talk to ")) data.push_back(c);
    // NPC-style metadata with small ID value
    appendLink(data, "0FFFFF0000F0000000000000000000000000000000000", "Renux");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

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

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

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

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    EXPECT_EQ(result.displayText, plain);
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

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    EXPECT_EQ(result.displayText, "Before [] after");
    ASSERT_EQ(result.links.size(), 1u);
    EXPECT_EQ(result.links[0].displayText, "");
    EXPECT_EQ(result.links[0].metadata, "");
}

// Test link positions are adjusted when leading spaces are trimmed
TEST_F(FormattedMessageTest, StructuredParsing_LeadingSpacesTrimmed) {
    std::vector<uint8_t> data;
    for (char c : std::string("   Talk to ")) data.push_back(c);  // 3 leading spaces
    appendLink(data, "0FFFFF0000D0000000000000000000000000000000000", "Guard");

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    // Leading spaces should be trimmed
    EXPECT_EQ(result.displayText, "Talk to [Guard]");
    ASSERT_EQ(result.links.size(), 1u);

    // Link position should be adjusted for trimmed spaces
    EXPECT_EQ(result.links[0].startPos, 8u);  // "Talk to " = 8 chars
    EXPECT_EQ(result.links[0].endPos, 15u);   // "Talk to [Guard]" = 15 chars
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

    eqt::ParsedFormattedMessage result = eqt::parseFormattedMessage(data.data(), data.size());

    // Expected: "See [A] [BB] [CCC] [DDDD] [EEEEE]"
    ASSERT_EQ(result.links.size(), 5u);

    // Verify each link's display text
    EXPECT_EQ(result.links[0].displayText, "A");
    EXPECT_EQ(result.links[1].displayText, "BB");
    EXPECT_EQ(result.links[2].displayText, "CCC");
    EXPECT_EQ(result.links[3].displayText, "DDDD");
    EXPECT_EQ(result.links[4].displayText, "EEEEE");

    // Verify all positions are sequential and non-overlapping
    for (size_t i = 0; i < result.links.size(); i++) {
        EXPECT_LT(result.links[i].startPos, result.links[i].endPos)
            << "Link " << i << " has invalid positions";

        if (i > 0) {
            EXPECT_LT(result.links[i-1].endPos, result.links[i].startPos)
                << "Links " << (i-1) << " and " << i << " overlap";
        }

        // Verify substring extraction works
        std::string extracted = result.displayText.substr(
            result.links[i].startPos,
            result.links[i].endPos - result.links[i].startPos);
        EXPECT_EQ(extracted, "[" + std::string(names[i]) + "]")
            << "Link " << i << " extraction failed";
    }
}
