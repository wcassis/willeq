/*
 * The user chat-colour table (B993, #313) -- see chat_text_colors.h for the
 * provenance of the rows and the category mapping.
 */

#include "client/chat/chat_text_colors.h"

namespace eqt {
namespace ui {

namespace {

struct Row {
    UserColor color;
    const char* name;
};

// /mnt/projects/EverQuestP1999/eqclient.ini [TextColors] User_N_Red/Green/
// Blue, N = 1..86, in order; names eqstr_us.txt 6199 + N (rows 83..86: see
// the header). Generated from the two files, not typed.
// clang-format off
const Row kRows[kUserColorCount] = {
    {{255, 255, 255}, "Say"},  // User_1, eqstr 6200
    {{190,  40, 190}, "Tell"},  // User_2, eqstr 6201
    {{  0, 255, 255}, "Group"},  // User_3, eqstr 6202
    {{ 40, 240,  40}, "Guild"},  // User_4, eqstr 6203
    {{  0, 128,   0}, "OOC"},  // User_5, eqstr 6204
    {{  0, 128,   0}, "Auction"},  // User_6, eqstr 6205
    {{255,   0,   0}, "Shout"},  // User_7, eqstr 6206
    {{ 90,  90, 255}, "Emote"},  // User_8, eqstr 6207
    {{ 90,  90, 255}, "Spells"},  // User_9, eqstr 6208
    {{255, 255, 255}, "You hit other"},  // User_10, eqstr 6209
    {{255,   0,   0}, "Other hits you"},  // User_11, eqstr 6210
    {{255, 255, 255}, "You miss other"},  // User_12, eqstr 6211
    {{255, 255, 255}, "Other misses you"},  // User_13, eqstr 6212
    {{255, 255,   0}, "Some broadcasts"},  // User_14, eqstr 6213
    {{ 90,  90, 255}, "Skills"},  // User_15, eqstr 6214
    {{255, 255, 255}, "Special Abilities"},  // User_16, eqstr 6215
    {{255, 255, 255}, "Unused at this time"},  // User_17, eqstr 6216
    {{255, 255, 255}, "Default Text"},  // User_18, eqstr 6217
    {{255, 255, 255}, "Unused at this time"},  // User_19, eqstr 6218
    {{240, 240,   0}, "Merchant Offer Price"},  // User_20, eqstr 6219
    {{240, 240,   0}, "Merchant Buy/Sell"},  // User_21, eqstr 6220
    {{255, 255, 255}, "Your death message"},  // User_22, eqstr 6221
    {{255, 255, 255}, "Other death message"},  // User_23, eqstr 6222
    {{255, 255, 255}, "Other damage other"},  // User_24, eqstr 6223
    {{255, 255, 255}, "Other miss other"},  // User_25, eqstr 6224
    {{128,   0, 128}, "/who command"},  // User_26, eqstr 6225
    {{255, 255, 255}, "yell for help"},  // User_27, eqstr 6226
    {{ 90,  90, 255}, "Hit for non-melee"},  // User_28, eqstr 6227
    {{240, 240,   0}, "Spell worn off"},  // User_29, eqstr 6228
    {{  0, 140,   0}, "Money splits"},  // User_30, eqstr 6229
    {{ 90,  90, 255}, "Loot message"},  // User_31, eqstr 6230
    {{255,   0,   0}, "Dice Roll (/random)"},  // User_32, eqstr 6231
    {{ 90,  90, 255}, "Others spells"},  // User_33, eqstr 6232
    {{255,   0,   0}, "Spell Failures"},  // User_34, eqstr 6233
    {{215, 154,  66}, "Chat Channel"},  // User_35, eqstr 6234
    {{110, 143, 176}, "Chat Channel 1"},  // User_36, eqstr 6235
    {{110, 143, 176}, "Chat Channel 2"},  // User_37, eqstr 6236
    {{110, 143, 176}, "Chat Channel 3"},  // User_38, eqstr 6237
    {{110, 143, 176}, "Chat Channel 4"},  // User_39, eqstr 6238
    {{110, 143, 176}, "Chat Channel 5"},  // User_40, eqstr 6239
    {{110, 143, 176}, "Chat Channel 6"},  // User_41, eqstr 6240
    {{110, 143, 176}, "Chat Channel 7"},  // User_42, eqstr 6241
    {{110, 143, 176}, "Chat Channel 8"},  // User_43, eqstr 6242
    {{110, 143, 176}, "Chat Channel 9"},  // User_44, eqstr 6243
    {{110, 143, 176}, "Chat Channel 10"},  // User_45, eqstr 6244
    {{255, 255, 255}, "Melee Crits"},  // User_46, eqstr 6245
    {{255, 255, 255}, "Spell Crits"},  // User_47, eqstr 6246
    {{255,   0,   0}, "Too far away (melee)"},  // User_48, eqstr 6247
    {{255,   0,   0}, "NPC Rampage"},  // User_49, eqstr 6248
    {{255,   0,   0}, "NPC Flurry"},  // User_50, eqstr 6249
    {{255,   0,   0}, "NPC Enrage"},  // User_51, eqstr 6250
    {{255, 255, 255}, "Say echo"},  // User_52, eqstr 6251
    {{255, 255, 255}, "Tell echo"},  // User_53, eqstr 6252
    {{255, 255, 255}, "Group echo"},  // User_54, eqstr 6253
    {{255, 255, 255}, "Guild echo"},  // User_55, eqstr 6254
    {{255, 255, 255}, "OOC echo"},  // User_56, eqstr 6255
    {{255, 255, 255}, "Auction echo"},  // User_57, eqstr 6256
    {{255, 255, 255}, "Shout echo"},  // User_58, eqstr 6257
    {{255, 255, 255}, "Emote echo"},  // User_59, eqstr 6258
    {{215, 154,  66}, "Chat Channel 1 echo"},  // User_60, eqstr 6259
    {{215, 154,  66}, "Chat Channel 2 echo"},  // User_61, eqstr 6260
    {{215, 154,  66}, "Chat Channel 3 echo"},  // User_62, eqstr 6261
    {{215, 154,  66}, "Chat Channel 4 echo"},  // User_63, eqstr 6262
    {{215, 154,  66}, "Chat Channel 5 echo"},  // User_64, eqstr 6263
    {{215, 154,  66}, "Chat Channel 6 echo"},  // User_65, eqstr 6264
    {{215, 154,  66}, "Chat Channel 7 echo"},  // User_66, eqstr 6265
    {{215, 154,  66}, "Chat Channel 8 echo"},  // User_67, eqstr 6266
    {{215, 154,  66}, "Chat Channel 9 echo"},  // User_68, eqstr 6267
    {{215, 154,  66}, "Chat Channel 10 echo"},  // User_69, eqstr 6268
    {{255, 255,   0}, "Unused at this time"},  // User_70, eqstr 6269
    {{255,   0, 255}, "Item Tags"},  // User_71, eqstr 6270
    {{  0, 200, 200}, "Raid Say"},  // User_72, eqstr 6271
    {{255, 255, 255}, "My Pet"},  // User_73, eqstr 6272
    {{255, 255, 255}, "Damage Shield"},  // User_74, eqstr 6273
    {{  0, 255, 255}, "Leadership Messages"},  // User_75, eqstr 6274
    {{255,   0,   0}, "Pet Rampage/Flurry"},  // User_76, eqstr 6275
    {{255, 255, 255}, "Pet Crits"},  // User_77, eqstr 6276
    {{ 90,  90, 255}, "Focus Effects"},  // User_78, eqstr 6277
    {{255, 255,   0}, "Experience Messages"},  // User_79, eqstr 6278
    {{255, 255,   0}, "System Messages"},  // User_80, eqstr 6279
    {{255, 255, 255}, "Pet Spells"},  // User_81, eqstr 6280
    {{255, 255, 255}, "Pet Responses"},  // User_82, eqstr 6281
    {{255, 255, 255}, "Item Speech"},  // User_83, eqemu Chat::ItemSpeech
    {{255, 255, 255}, "Strikethrough Messages"},  // User_84, eqstr 6288
    {{255, 255, 255}, "Stun Messages"},  // User_85, eqstr 6289
    {{255, 155, 155}, "Swarm Pet Death Messages"},  // User_86, eqstr 6290
};
// clang-format on

ChatTextColors makeDefaults() {
    ChatTextColors t{};
    for (int i = 0; i < kUserColorCount; ++i) t[i] = kRows[i].color;
    return t;
}

}  // namespace

const ChatTextColors& defaultChatTextColors() {
    static const ChatTextColors kDefaults = makeDefaults();
    return kDefaults;
}

const char* userColorName(int n) {
    if (n < 1 || n > kUserColorCount) return "";
    return kRows[n - 1].name;
}

int userColorForChatType(uint32_t chatType) {
    if (chatType < kFirstUserColorChatType) return 0;
    const uint32_t n = chatType - kFirstUserColorChatType + 1;
    return n <= static_cast<uint32_t>(kUserColorCount) ? static_cast<int>(n) : 0;
}

// fable's coarse channels -> the row the official client would colour that
// line with. 0 = no row: the line keeps its compiled colour.
int userColorForChannel(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::Say:         return 1;   // Say
        case ChatChannel::Tell:        return 2;   // Tell
        case ChatChannel::Group:       return 3;   // Group
        case ChatChannel::Guild:       return 4;   // Guild
        case ChatChannel::OOC:         return 5;   // OOC
        case ChatChannel::Auction:     return 6;   // Auction
        case ChatChannel::Shout:       return 7;   // Shout
        case ChatChannel::Emote:       return 8;   // Emote
        case ChatChannel::Spell:       return 9;   // Spells
        case ChatChannel::Broadcast:   return 14;  // Some broadcasts
        // ChatOutput::Combat(text, false): "X hits Y", "Y has been slain by X".
        case ChatChannel::Combat:      return 24;  // Other damage other
        case ChatChannel::Loot:        return 31;  // Loot message
        case ChatChannel::Raid:        return 72;  // Raid Say
        case ChatChannel::Experience:  return 79;  // Experience Messages
        case ChatChannel::System:      return 80;  // System Messages
        // An NPC's dialogue reaches the official client as a say.
        case ChatChannel::NPCDialogue: return 1;   // Say
        // CombatSelf carries BOTH "You hit X" (row 10) and "X hit you" (row
        // 11), and CombatMiss both directions of a miss (rows 12/13), so
        // neither names one row; Error, GMSay and Petition have none.
        default:                       return 0;
    }
}

int userColorFor(ChatChannel channel, uint32_t chatType) {
    const int byType = userColorForChatType(chatType);
    return byType != 0 ? byType : userColorForChannel(channel);
}

uint32_t userColorArgb(const ChatTextColors& table, int n) {
    if (n < 1 || n > kUserColorCount) return 0;
    const UserColor& c = table[n - 1];
    return (0xFFu << 24) | (static_cast<uint32_t>(c.r) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

uint32_t resolveChatLineColor(const ChatTextColors* table, int userColor, uint32_t fallbackArgb) {
    if (!table || userColor < 1 || userColor > kUserColorCount) return fallbackArgb;
    return userColorArgb(*table, userColor);
}

}  // namespace ui
}  // namespace eqt
