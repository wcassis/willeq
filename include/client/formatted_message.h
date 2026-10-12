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
    std::string displayText;      // Text shown to user (e.g. "Renux")
    std::string metadata;         // Raw hex metadata
    uint32_t itemId = 0;
    size_t startPos = 0;          // Character position in display string (start of '[')
    size_t endPos = 0;            // Character position in display string (after ']')
};

// A parsed message with arguments separated (null-byte delimited in raw packet)
struct ParsedFormattedMessageWithArgs {
    std::vector<std::string> args;        // Arguments separated by null bytes
    std::vector<MessageLink> links;       // All links in order of appearance
};

// B606 (#189): the structured parseFormattedMessage() (ParsedFormattedMessage
// with per-link startPos/endPos) is deleted. Its only production caller was
// parseFormattedMessageText() -- itself a zero-caller wrapper B606 removed --
// so the structured half of this TU had no live consumer: the chat path calls
// parseFormattedMessageArgs() below (chat_component.cpp), which carries the
// T34 metadata-size fix. A function whose only net was its own tests is
// deleted together with them (T458's rule), not kept under a new exemption.

// Parse a FormattedMessage payload, preserving null-byte argument delimiters.
// The raw packet uses null bytes (0x00) to separate arguments for placeholder
// substitution (%1, %2, %T1, etc.). This function returns them as a vector.
//
// @param data    Pointer to message data (after the 12-byte header)
// @param msg_len Length of the message data
// @return        ParsedFormattedMessageWithArgs with arguments and link information
ParsedFormattedMessageWithArgs parseFormattedMessageArgs(const uint8_t* data, size_t msg_len);

} // namespace eqt
