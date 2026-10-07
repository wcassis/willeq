/*
 * ChatMessageBuffer implementation.
 * U07a: Relocated from src/client/graphics/ui/ — no Irrlicht dependency.
 */

#include "client/chat/chat_message_buffer.h"
#include "client/chat/chat_text_colors.h"
#include <ctime>

namespace eqt {
namespace ui {

// Helper: pack ARGB color as uint32_t (matches Irrlicht SColor layout)
static constexpr uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t getChannelColor(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::Say:         return argb(255, 255, 255, 255);  // White
        case ChatChannel::Tell:        return argb(255, 255, 0, 255);    // Magenta
        case ChatChannel::Group:       return argb(255, 0, 255, 255);    // Cyan
        case ChatChannel::Guild:       return argb(255, 0, 255, 0);      // Green
        case ChatChannel::Shout:       return argb(255, 255, 0, 0);      // Red
        case ChatChannel::Auction:     return argb(255, 0, 255, 0);      // Green
        case ChatChannel::OOC:         return argb(255, 0, 255, 0);      // Green
        case ChatChannel::Broadcast:   return argb(255, 255, 255, 0);    // Yellow
        case ChatChannel::Emote:       return argb(255, 255, 255, 0);    // Yellow
        case ChatChannel::GMSay:       return argb(255, 255, 255, 0);    // Yellow
        case ChatChannel::Raid:        return argb(255, 255, 128, 0);    // Orange
        case ChatChannel::Combat:      return argb(255, 255, 255, 255);  // White
        case ChatChannel::CombatSelf:  return argb(255, 255, 100, 100);  // Light red
        case ChatChannel::Experience:  return argb(255, 255, 255, 0);    // Yellow
        case ChatChannel::Loot:        return argb(255, 150, 255, 150);  // Light green
        case ChatChannel::Spell:       return argb(255, 150, 200, 255);  // Light blue
        case ChatChannel::System:      return argb(255, 255, 255, 0);    // Yellow
        case ChatChannel::Error:       return argb(255, 255, 0, 0);      // Red
        case ChatChannel::NPCDialogue: return argb(255, 200, 200, 255);  // Lavender
        case ChatChannel::CombatMiss:  return argb(255, 160, 160, 160);  // Gray
        default:                       return argb(255, 200, 200, 200);  // Light gray
    }
}

// B502 (GitLab #177) DELETED getChannelName(): a third channel-name table
// (the one spelled for the ChatChannel enum here) with zero callers -- the
// name in a chat line comes from ChatChannelName() (the ONE such table,
// pinned by tests/test_chat_channel_names.cpp), and the per-channel wording
// lives in formatMessageForDisplay().

std::string formatTimestamp(uint32_t timestamp) {
    if (timestamp == 0) return "";
    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm* tm = std::localtime(&time);
    if (!tm) return "";
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "[%02d:%02d]", tm->tm_hour, tm->tm_min);
    return std::string(buffer);
}

std::string formatMessageForDisplay(const ChatMessage& msg, bool showTimestamp) {
    std::string result;
    if (showTimestamp && msg.timestamp != 0) {
        result = formatTimestamp(msg.timestamp) + " ";
    }
    switch (msg.channel) {
        case ChatChannel::Say:
            result += msg.sender.empty() ? "You say, '" + msg.text + "'" : msg.sender + " says, '" + msg.text + "'";
            break;
        case ChatChannel::Tell:
            result += msg.sender.empty() ? "You tell " + msg.text : msg.sender + " tells you, '" + msg.text + "'";
            break;
        case ChatChannel::Group:
            result += msg.sender.empty() ? "You tell the group, '" + msg.text + "'" : msg.sender + " tells the group, '" + msg.text + "'";
            break;
        case ChatChannel::Guild:
            result += msg.sender.empty() ? "You tell the guild, '" + msg.text + "'" : msg.sender + " tells the guild, '" + msg.text + "'";
            break;
        case ChatChannel::Shout:
            result += msg.sender.empty() ? "You shout, '" + msg.text + "'" : msg.sender + " shouts, '" + msg.text + "'";
            break;
        case ChatChannel::Auction:
            result += msg.sender.empty() ? "You auction, '" + msg.text + "'" : msg.sender + " auctions, '" + msg.text + "'";
            break;
        case ChatChannel::OOC:
            result += msg.sender.empty() ? "You say out of character, '" + msg.text + "'" : msg.sender + " says out of character, '" + msg.text + "'";
            break;
        case ChatChannel::Broadcast:
            result += "BROADCAST: " + msg.text;
            break;
        case ChatChannel::Emote:
            result += msg.sender.empty() ? msg.text : msg.sender + " " + msg.text;
            break;
        case ChatChannel::Raid:
            result += msg.sender.empty() ? "You tell the raid, '" + msg.text + "'" : msg.sender + " tells the raid, '" + msg.text + "'";
            break;
        case ChatChannel::GMSay:
            result += "[GM] " + msg.sender + ": " + msg.text;
            break;
        case ChatChannel::NPCDialogue:
            result += msg.text;
            break;
        default:
            result += msg.text;
            break;
    }
    return result;
}

std::string formatMessageForDisplay(const ChatMessage& msg) {
    return formatMessageForDisplay(msg, false);
}

ChatMessageBuffer::ChatMessageBuffer(size_t maxMessages)
    : maxMessages_(maxMessages) {
}

void ChatMessageBuffer::addMessage(ChatMessage msg) {
    if (msg.timestamp == 0) {
        msg.timestamp = static_cast<uint32_t>(std::time(nullptr));
    }
    if (msg.color == 0xFFFFFFFF) {
        msg.color = getChannelColor(msg.channel);
    }
    if (msg.userColor == 0) {
        msg.userColor = static_cast<uint8_t>(userColorForChannel(msg.channel));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pendingMessages_.push_back(std::move(msg));
}

void ChatMessageBuffer::addSystemMessage(const std::string& text, ChatChannel channel) {
    ChatMessage msg;
    msg.text = text;
    msg.channel = channel;
    msg.isSystemMessage = true;
    msg.color = getChannelColor(channel);
    addMessage(std::move(msg));
}

void ChatMessageBuffer::processPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pendingMessages_.empty()) {
        for (auto& msg : pendingMessages_) {
            messages_.push_back(std::move(msg));
            ++appendedCount_;  // T462: see appendedCount()
            if (messages_.size() > maxMessages_) {
                messages_.pop_front();
            }
        }
        pendingMessages_.clear();
    }
}

// B502 (GitLab #177) DELETED getRecentMessages(): the copy-based "last N"
// reader with zero callers -- the chat panel renders getMessages() and
// tracks what is new through appendedCount() (T462).

void ChatMessageBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    pendingMessages_.clear();
    ++clearGeneration_;  // T462: a wrap cache over this buffer starts over
}

} // namespace ui
} // namespace eqt
