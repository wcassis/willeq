#include "client/graphics/ui/chat_message_buffer.h"
#include <ctime>

namespace eqt {
namespace ui {

irr::video::SColor getChannelColor(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::Say:
            return irr::video::SColor(255, 255, 255, 255);  // White
        case ChatChannel::Tell:
            return irr::video::SColor(255, 255, 0, 255);    // Purple/Magenta
        case ChatChannel::Group:
            return irr::video::SColor(255, 0, 255, 255);    // Cyan
        case ChatChannel::Guild:
            return irr::video::SColor(255, 0, 255, 0);      // Green
        case ChatChannel::Shout:
            return irr::video::SColor(255, 255, 0, 0);      // Red
        case ChatChannel::Auction:
            return irr::video::SColor(255, 0, 255, 0);      // Green
        case ChatChannel::OOC:
            return irr::video::SColor(255, 0, 255, 0);      // Green
        case ChatChannel::Broadcast:
            return irr::video::SColor(255, 255, 255, 0);    // Yellow
        case ChatChannel::Emote:
            return irr::video::SColor(255, 255, 255, 0);    // Yellow
        case ChatChannel::GMSay:
            return irr::video::SColor(255, 255, 255, 0);    // Yellow
        case ChatChannel::Raid:
            return irr::video::SColor(255, 255, 128, 0);    // Orange
        case ChatChannel::Combat:
            return irr::video::SColor(255, 255, 255, 255);  // White
        case ChatChannel::CombatSelf:
            return irr::video::SColor(255, 255, 100, 100);  // Light red
        case ChatChannel::Experience:
            return irr::video::SColor(255, 255, 255, 0);    // Yellow
        case ChatChannel::Loot:
            return irr::video::SColor(255, 150, 255, 150);  // Light green
        case ChatChannel::Spell:
            return irr::video::SColor(255, 150, 200, 255);  // Light blue
        case ChatChannel::System:
            return irr::video::SColor(255, 255, 255, 0);    // Yellow
        case ChatChannel::Error:
            return irr::video::SColor(255, 255, 0, 0);      // Red
        case ChatChannel::NPCDialogue:
            return irr::video::SColor(255, 200, 200, 255);  // Light blue/lavender for NPC dialogue
        default:
            return irr::video::SColor(255, 200, 200, 200);  // Light gray
    }
}

const char* getChannelName(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::Say:       return "say";
        case ChatChannel::Tell:      return "tell";
        case ChatChannel::Group:     return "group";
        case ChatChannel::Guild:     return "guild";
        case ChatChannel::Shout:     return "shout";
        case ChatChannel::Auction:   return "auction";
        case ChatChannel::OOC:       return "ooc";
        case ChatChannel::Broadcast: return "broadcast";
        case ChatChannel::Emote:     return "emote";
        case ChatChannel::GMSay:     return "gm";
        case ChatChannel::Raid:      return "raid";
        case ChatChannel::Combat:    return "combat";
        case ChatChannel::CombatSelf:return "combat";
        case ChatChannel::Experience:return "exp";
        case ChatChannel::Loot:      return "loot";
        case ChatChannel::Spell:     return "spell";
        case ChatChannel::System:    return "system";
        case ChatChannel::Error:     return "error";
        case ChatChannel::NPCDialogue: return "npc";
        default:                     return "unknown";
    }
}

std::string formatTimestamp(uint32_t timestamp) {
    if (timestamp == 0) {
        return "";
    }

    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm* tm = std::localtime(&time);
    if (!tm) {
        return "";
    }

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
            if (!msg.sender.empty()) {
                result += msg.sender + " says, '" + msg.text + "'";
            } else {
                result += "You say, '" + msg.text + "'";
            }
            break;

        case ChatChannel::Tell:
            if (!msg.sender.empty()) {
                result += msg.sender + " tells you, '" + msg.text + "'";
            } else {
                result += "You tell " + msg.text;
            }
            break;

        case ChatChannel::Group:
            if (!msg.sender.empty()) {
                result += msg.sender + " tells the group, '" + msg.text + "'";
            } else {
                result += "You tell the group, '" + msg.text + "'";
            }
            break;

        case ChatChannel::Guild:
            if (!msg.sender.empty()) {
                result += msg.sender + " tells the guild, '" + msg.text + "'";
            } else {
                result += "You tell the guild, '" + msg.text + "'";
            }
            break;

        case ChatChannel::Shout:
            if (!msg.sender.empty()) {
                result += msg.sender + " shouts, '" + msg.text + "'";
            } else {
                result += "You shout, '" + msg.text + "'";
            }
            break;

        case ChatChannel::Auction:
            if (!msg.sender.empty()) {
                result += msg.sender + " auctions, '" + msg.text + "'";
            } else {
                result += "You auction, '" + msg.text + "'";
            }
            break;

        case ChatChannel::OOC:
            if (!msg.sender.empty()) {
                result += msg.sender + " says out of character, '" + msg.text + "'";
            } else {
                result += "You say out of character, '" + msg.text + "'";
            }
            break;

        case ChatChannel::Broadcast:
            result += "BROADCAST: " + msg.text;
            break;

        case ChatChannel::Emote:
            if (!msg.sender.empty()) {
                result += msg.sender + " " + msg.text;
            } else {
                result += msg.text;
            }
            break;

        case ChatChannel::Raid:
            if (!msg.sender.empty()) {
                result += msg.sender + " tells the raid, '" + msg.text + "'";
            } else {
                result += "You tell the raid, '" + msg.text + "'";
            }
            break;

        case ChatChannel::GMSay:
            result += "[GM] " + msg.sender + ": " + msg.text;
            break;

        case ChatChannel::NPCDialogue:
            // NPC dialogue - text already includes NPC name, display as-is
            result += msg.text;
            break;

        case ChatChannel::Combat:
        case ChatChannel::CombatSelf:
        case ChatChannel::Experience:
        case ChatChannel::Loot:
        case ChatChannel::Spell:
        case ChatChannel::System:
        case ChatChannel::Error:
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
    // Set timestamp if not already set
    if (msg.timestamp == 0) {
        msg.timestamp = static_cast<uint32_t>(std::time(nullptr));
    }

    // Set color based on channel if not already set (default white check)
    if (msg.color == irr::video::SColor(255, 255, 255, 255)) {
        msg.color = getChannelColor(msg.channel);
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
        hasNewMessages_ = true;

        for (auto& msg : pendingMessages_) {
            messages_.push_back(std::move(msg));
            if (messages_.size() > maxMessages_) {
                messages_.pop_front();
            }
        }
        pendingMessages_.clear();
    }
}

std::vector<ChatMessage> ChatMessageBuffer::getRecentMessages(size_t count) const {
    std::vector<ChatMessage> result;
    size_t startIdx = (messages_.size() > count) ? messages_.size() - count : 0;
    for (size_t i = startIdx; i < messages_.size(); ++i) {
        result.push_back(messages_[i]);
    }
    return result;
}

void ChatMessageBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    pendingMessages_.clear();
    hasNewMessages_ = false;
}

} // namespace ui
} // namespace eqt
