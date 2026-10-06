/*
 * The user chat-colour table -- the Options window's Colors page (B993, #313).
 *
 * THE TABLE IS THE ORACLE'S. The official client persists it in eqclient.ini
 * `[TextColors]` as `User_<N>_Red / _Green / _Blue` on a 0..255 scale, N =
 * 1..86. /mnt/projects/EverQuestP1999/eqclient.ini carries all 86 rows, and
 * those values are the defaults here (kDefaults in chat_text_colors.cpp,
 * quoted row by row).
 *
 * WHICH ROW IS WHICH CATEGORY. `User_N` is the colour of the server's chat
 * type 255 + N -- eqemu's `Chat::Say` = 256 is User_1, `Chat::Tell` = 257 is
 * User_2, ... `Chat::Stun` = 340 is User_85 -- and the row's name is the
 * client's own eqstr_us.txt 6199 + N ("Say", "Tell", "Group", ... "Pet
 * Responses" at 6281 = User_82). The defaults agree with that reading row by
 * row where the era's look is known (Tell 190,40,190, Group 0,255,255, Guild
 * 40,240,40, Shout 255,0,0, Other hits you 255,0,0). Rows 83..86 have no
 * eqstr name in that run (6282 "Basilisk", 6283/6284 are sentences); their
 * names are the later list strings 6288-6290 and eqemu's ItemSpeech.
 * ASSUMPTION AWAITING ORACLE (C1): the Options list shows User_1..User_86 in
 * index order, unfiltered, with these names.
 *
 * WHAT fable REACHES. fable's chat lines carry a coarse ChatChannel, plus --
 * since B993 -- the server's own chat type when the line came from a
 * FormattedMessage / SimpleMessage (ChatMessageData::chatType). A line is
 * coloured from this table when either names a row (userColorFor()); a line
 * that names none (a local error, a GM line) keeps its compiled colour.
 *
 * Device-free.
 */

#pragma once

#include <array>
#include <cstdint>

#include "client/chat/chat_message_buffer.h"

namespace eqt {
namespace ui {

/** eqclient.ini [TextColors] has User_1 .. User_86. */
constexpr int kUserColorCount = 86;

/** The server chat type User_1 colours (eqemu Chat::Say). */
constexpr uint32_t kFirstUserColorChatType = 256;

struct UserColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    bool operator==(const UserColor& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const UserColor& o) const { return !(*this == o); }
};

/** Index 0 is User_1. */
using ChatTextColors = std::array<UserColor, kUserColorCount>;

/** The 86 rows of /mnt/projects/EverQuestP1999/eqclient.ini [TextColors]. */
const ChatTextColors& defaultChatTextColors();

/** The row's Options-list text (1-based N); "" out of range. */
const char* userColorName(int n);

/** The User_N a server chat type names (256..341 -> 1..86), else 0. */
int userColorForChatType(uint32_t chatType);

/**
 * The User_N fable's coarse channel maps to, else 0 (keep the compiled
 * colour). See the table in chat_text_colors.cpp for each mapping's reason.
 */
int userColorForChannel(ChatChannel channel);

/** The server chat type wins over the channel when it names a row. */
int userColorFor(ChatChannel channel, uint32_t chatType);

/** ARGB (alpha 255) of row N of `table`; 0 if N is out of range. */
uint32_t userColorArgb(const ChatTextColors& table, int n);

/** A line's draw colour: row `userColor` of `table` when it names one, else `fallbackArgb`. */
uint32_t resolveChatLineColor(const ChatTextColors* table, int userColor, uint32_t fallbackArgb);

}  // namespace ui
}  // namespace eqt
