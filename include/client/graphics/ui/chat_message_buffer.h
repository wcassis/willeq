#pragma once

#include <irrlicht.h>
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <cstdint>
#include "client/formatted_message.h"

namespace eqt {
namespace ui {

// Chat channel types (matches EQ protocol)
enum class ChatChannel : uint8_t {
    Guild = 0,
    Group = 2,
    Shout = 3,
    Auction = 4,
    OOC = 5,
    Broadcast = 6,
    Tell = 7,
    Say = 8,
    Petition = 10,
    GMSay = 11,
    Raid = 15,
    Emote = 22,
    // System message types (local only, not from server)
    Combat = 100,
    CombatSelf = 101,
    Experience = 102,
    Loot = 103,
    Spell = 104,
    System = 105,
    Error = 106,
    NPCDialogue = 107,  // NPC dialogue from FormattedMessage
    CombatMiss = 108    // Combat misses (filterable separately)
};

// A single chat message
struct ChatMessage {
    std::string sender;           // Who sent (empty for system messages)
    std::string text;             // Message content
    ChatChannel channel;          // Channel type
    uint32_t timestamp;           // Unix timestamp (seconds)
    irr::video::SColor color;     // Text color for rendering
    bool isSystemMessage = false; // True for combat, exp, loot, etc.
    std::vector<MessageLink> links; // Clickable links in the message (for FormattedMessage)

    ChatMessage() : channel(ChatChannel::Say), timestamp(0),
                    color(255, 255, 255, 255) {}
};

// Get the display color for a channel
irr::video::SColor getChannelColor(ChatChannel channel);

// Get the channel name for display
const char* getChannelName(ChatChannel channel);

// Format a message for display (e.g., "[Name] says, 'text'")
std::string formatMessageForDisplay(const ChatMessage& msg);

// Format a message with optional timestamp prefix (e.g., "[12:34] [Name] says, 'text'")
std::string formatMessageForDisplay(const ChatMessage& msg, bool showTimestamp);

// Format timestamp as string (e.g., "[12:34]")
std::string formatTimestamp(uint32_t timestamp);

// Thread-safe message buffer for chat window
class ChatMessageBuffer {
public:
    ChatMessageBuffer(size_t maxMessages = 1000);
    ~ChatMessageBuffer() = default;

    // Add a message (thread-safe, called from network thread)
    void addMessage(ChatMessage msg);

    // Add a simple system message
    void addSystemMessage(const std::string& text, ChatChannel channel = ChatChannel::System);

    // Process pending messages (call from UI thread at start of frame)
    void processPending();

    // Get messages for rendering (call from UI thread only, after processPending)
    const std::deque<ChatMessage>& getMessages() const { return messages_; }

    // Get recent N messages
    std::vector<ChatMessage> getRecentMessages(size_t count) const;

    // Clear all messages
    void clear();

    // Get message count
    size_t size() const { return messages_.size(); }

    // Check if there are new messages since last check
    bool hasNewMessages() const { return hasNewMessages_; }
    void clearNewMessageFlag() { hasNewMessages_ = false; }

private:
    std::deque<ChatMessage> messages_;
    std::deque<ChatMessage> pendingMessages_;
    mutable std::mutex mutex_;
    size_t maxMessages_;
    bool hasNewMessages_ = false;
};

} // namespace ui
} // namespace eqt
