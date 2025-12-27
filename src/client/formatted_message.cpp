#include "client/formatted_message.h"

namespace eqt {

// Link metadata sizes observed in Titanium client
// The server sends either 45 or 56 character hex metadata
static constexpr size_t LINK_METADATA_SIZE_SHORT = 45;
static constexpr size_t LINK_METADATA_SIZE_LONG = 56;

// Helper to convert hex character to value (0-15, or -1 if invalid)
static int hexCharToValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse item ID from link metadata
// The metadata format appears to be: TTTTTT + item_id (hex) + padding
// Based on observed patterns, item ID starts at position 6 and is 4-5 hex chars
static uint32_t parseItemIdFromMetadata(const std::string& metadata) {
    // Metadata format analysis from server logs:
    // Position 0-5: Type/flags (e.g., "0FFFFF")
    // Position 6+: Item data including ID
    //
    // For NPC names, the pattern is typically "0FFFFF0000X..." where X encodes entity info
    // For items, there would be an item ID encoded
    //
    // Since we don't have clear item link examples yet, we'll attempt to parse
    // what looks like an ID from positions 6-13 (up to 8 hex chars = 32 bits)

    if (metadata.length() < 14) {
        return 0;
    }

    // Try to extract a value from positions 6-13
    uint32_t value = 0;
    for (size_t i = 6; i < 14 && i < metadata.length(); ++i) {
        int digit = hexCharToValue(metadata[i]);
        if (digit < 0) break;
        value = (value << 4) | digit;
    }

    return value;
}

// Determine link type from metadata
// NPC dialogue links typically have specific patterns
// Item links have item IDs encoded with different prefixes
static LinkType determineLinkType(const std::string& metadata, uint32_t parsedId) {
    // Based on observed server logs, NPC name links use the "0FFFFF" prefix:
    // - "0FFFFF0000F000..." - first link in a message (link index 0)
    // - "0FFFFF00010000..." - second link in a message (link index 1)
    // - "0FFFFF0002..." etc.
    //
    // The values at positions 6-9 are link indices, not item IDs.
    // All links with the "0FFFFF" prefix are NPC dialogue keywords.
    //
    // Real item links in EQ use a different metadata format with item-specific
    // data (item ID, augments, etc.) and different prefix patterns.

    if (metadata.length() < 6) {
        return LinkType::None;
    }

    // Check for NPC link pattern: starts with "0FFFFF"
    // This prefix indicates an NPC dialogue keyword link
    if (metadata.substr(0, 6) == "0FFFFF") {
        return LinkType::NPCName;
    }

    // Other patterns would be item links (when we encounter them)
    // For now, default to NPC name for unknown patterns since we're primarily
    // seeing these in NPC dialogue contexts
    return LinkType::NPCName;
}

ParsedFormattedMessage parseFormattedMessage(const uint8_t* data, size_t msg_len) {
    ParsedFormattedMessage result;
    bool in_link = false;
    size_t link_content_start = 0;
    size_t link_bracket_pos = 0;  // Position of '[' in output

    for (size_t i = 0; i < msg_len; ++i) {
        if (data[i] == 0x12) {
            if (!in_link) {
                // Starting a link - we should have seen '[' just before
                in_link = true;
                link_content_start = i + 1;
                // Record where the '[' is in the output
                // (it was added in the previous iteration)
                link_bracket_pos = result.displayText.length() > 0 ?
                                   result.displayText.length() - 1 : 0;
            } else {
                // Ending a link - extract metadata and name
                size_t link_len = i - link_content_start;
                size_t name_start_offset = 0;
                size_t metadata_size = 0;

                if (link_len > LINK_METADATA_SIZE_LONG) {
                    metadata_size = LINK_METADATA_SIZE_LONG;
                    name_start_offset = LINK_METADATA_SIZE_LONG;
                } else if (link_len > LINK_METADATA_SIZE_SHORT) {
                    metadata_size = LINK_METADATA_SIZE_SHORT;
                    name_start_offset = LINK_METADATA_SIZE_SHORT;
                } else {
                    // All metadata, no name
                    metadata_size = link_len;
                    name_start_offset = link_len;
                }

                // Extract metadata
                std::string metadata;
                for (size_t j = 0; j < metadata_size && (link_content_start + j) < i; ++j) {
                    char c = static_cast<char>(data[link_content_start + j]);
                    if (c >= 32 && c <= 126) {
                        metadata += c;
                    }
                }

                // Extract and add name to display
                std::string name;
                for (size_t j = link_content_start + name_start_offset; j < i; ++j) {
                    if (data[j] >= 32 && data[j] <= 126) {
                        char c = static_cast<char>(data[j]);
                        name += c;
                        result.displayText += c;
                    }
                }

                // Create link record
                MessageLink link;
                link.displayText = name;
                link.metadata = metadata;
                link.itemId = parseItemIdFromMetadata(metadata);
                link.type = determineLinkType(metadata, link.itemId);
                link.startPos = link_bracket_pos;
                // endPos will be set after we add the closing ']'

                // The ']' will be added when we see it in the next iteration
                // Store the link temporarily - we'll set endPos when we see ']'
                result.links.push_back(link);

                in_link = false;
            }
        } else if (!in_link) {
            if (data[i] == 0) {
                // Replace null bytes with space
                result.displayText += ' ';
            } else if (data[i] >= 32 && data[i] <= 126) {
                char c = static_cast<char>(data[i]);
                result.displayText += c;

                // If this is ']' and we just finished a link, update endPos
                if (c == ']' && !result.links.empty()) {
                    MessageLink& lastLink = result.links.back();
                    if (lastLink.endPos == 0) {
                        lastLink.endPos = result.displayText.length();
                    }
                }
            }
            // Skip other non-printable chars outside links
        }
    }

    // Trim trailing spaces
    while (!result.displayText.empty() && result.displayText.back() == ' ') {
        result.displayText.pop_back();
    }

    // Trim leading spaces (need to adjust link positions)
    size_t leading_spaces = 0;
    for (char c : result.displayText) {
        if (c == ' ') {
            leading_spaces++;
        } else {
            break;
        }
    }

    if (leading_spaces > 0) {
        result.displayText = result.displayText.substr(leading_spaces);
        // Adjust all link positions
        for (auto& link : result.links) {
            if (link.startPos >= leading_spaces) {
                link.startPos -= leading_spaces;
            } else {
                link.startPos = 0;
            }
            if (link.endPos >= leading_spaces) {
                link.endPos -= leading_spaces;
            } else {
                link.endPos = 0;
            }
        }
    }

    return result;
}

std::string parseFormattedMessageText(const uint8_t* data, size_t msg_len) {
    // Use the structured parser and just return the display text
    ParsedFormattedMessage parsed = parseFormattedMessage(data, msg_len);
    return parsed.displayText;
}

} // namespace eqt
