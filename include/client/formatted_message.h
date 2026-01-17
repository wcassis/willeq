#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace eqt {

// Types of links that can appear in formatted messages
enum class LinkType {
    None,
    NPCName,    // Clickable NPC name - clicking says the text
    Item        // Item link - clicking shows tooltip
};

// A single link within a message
struct MessageLink {
    LinkType type = LinkType::None;
    std::string displayText;      // Text shown to user (e.g., "Renux")
    std::string metadata;         // Raw hex metadata (45 or 56 chars)
    uint32_t itemId = 0;          // Parsed item ID (for Item links)
    size_t startPos = 0;          // Character position in display string (start of '[')
    size_t endPos = 0;            // Character position in display string (after ']')
};

// A parsed message with links
struct ParsedFormattedMessage {
    std::string displayText;              // Full display string with [Name] formatting
    std::vector<MessageLink> links;       // All links in order of appearance
};

// A parsed message with arguments separated (null-byte delimited in raw packet)
struct ParsedFormattedMessageWithArgs {
    std::vector<std::string> args;        // Arguments separated by null bytes
    std::vector<MessageLink> links;       // All links in order of appearance
};

// Parse a FormattedMessage payload with full link preservation.
// Link format: [\x12<hex_metadata><item_name>\x12]
// Hex metadata is 45 or 56 characters of uppercase hex (0-9, A-F).
// Item name follows the metadata.
// Multiple links can appear anywhere in the message.
//
// @param data    Pointer to message data (after the 12-byte header)
// @param msg_len Length of the message data
// @return        ParsedFormattedMessage with display text and link information
ParsedFormattedMessage parseFormattedMessage(const uint8_t* data, size_t msg_len);

// Parse a FormattedMessage payload, extracting readable text and stripping link metadata.
// This is a simplified version that returns only the display string.
// For clickable link support, use parseFormattedMessage() instead.
//
// @param data    Pointer to message data (after the 12-byte header)
// @param msg_len Length of the message data
// @return        Cleaned display string with link metadata stripped
std::string parseFormattedMessageText(const uint8_t* data, size_t msg_len);

// Parse a FormattedMessage payload, preserving null-byte argument delimiters.
// The raw packet uses null bytes (0x00) to separate arguments for placeholder
// substitution (%1, %2, %T1, etc.). This function returns them as a vector.
//
// @param data    Pointer to message data (after the 12-byte header)
// @param msg_len Length of the message data
// @return        ParsedFormattedMessageWithArgs with arguments and link information
ParsedFormattedMessageWithArgs parseFormattedMessageArgs(const uint8_t* data, size_t msg_len);

} // namespace eqt
