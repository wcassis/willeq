/*
 * guild_packet_bodies.h -- the rank fields of the guild C->S opcodes, and
 * what eqemu actually does with them (T317).
 *
 * WHY THIS EXISTS
 *
 *   eq_guild.cpp used to write a literal `2` into both rank fields with the
 *   comment "TODO: Use actual rank":
 *
 *       invite.officer      = 2;   // OP_GuildInvite,  from /guildinvite
 *       remove.leaderstatus = 2;   // OP_GuildRemove,  from /guildremove
 *
 *   The board (deferred.md tier 2 item 6) recorded that as "the client claims
 *   officer/leader status the player may not have". READING THE SERVER SHOWS
 *   THAT IS NOT WHAT EITHER FIELD MEANS. Both handlers were read in full this
 *   unit (akk-stack/code/zone/client_packet.cpp):
 *
 *   OP_GuildInvite -- Handle_OP_GuildInvite, client_packet.cpp:8152.
 *       `gc->officer` is the rank being GRANTED to the person named in
 *       `othername`, not the rank of the sender. The handler does:
 *
 *           auto rank = gc->officer;
 *           if (ClientVersion() < RoF) switch (gc->officer) {
 *               case GUILD_MEMBER_TI (0):  rank = GUILD_MEMBER  (5); break;
 *               case GUILD_OFFICER_TI(1):  rank = GUILD_OFFICER (3); break;
 *               case GUILD_LEADER_TI (2):  rank = GUILD_LEADER  (1); break;
 *               default:                   rank = GUILD_RANK_NONE(0); break;
 *           }
 *
 *       and then compares `rank` against the *target's* current rank
 *       (`client->GuildRank()`) to decide promote vs demote, or passes it to
 *       `guild_mgr.RecordInvite(target_char_id, guild_id, rank)` for a fresh
 *       invite. The SENDER's authority is taken from the server's own
 *       `GuildRank()` via `guild_mgr.CheckPermission(...)` -- the packet field
 *       is never consulted for that. So the hardcoded 2 was not a lie about
 *       the sender at all; it was `/guildinvite Bob` asking the server to
 *       bring Bob in AS GUILD LEADER (or, if Bob was already a member, to
 *       promote him to leader). Only the sender's server-side permission
 *       check stood between that request and the guild changing hands.
 *
 *       A plain invite therefore sends GUILD_MEMBER_TI (0).
 *
 *   OP_GuildRemove -- Handle_OP_GuildRemove, client_packet.cpp:8672.
 *       The handler casts the body to GuildCommand_Struct and reads exactly
 *       one field, `othername` (offset 0, which is GuildRemove_Struct::target
 *       -- the two layouts agree there). `leaderstatus` IS NEVER READ. The
 *       remover's authority again comes from the server's own GuildRank()
 *       through CheckPermission(GUILD_ACTION_MEMBERS_REMOVE). Whatever we put
 *       in the field is discarded.
 *
 *       We send the player's REAL rank anyway: the field exists, we now know
 *       it (see below), and a truthful byte costs nothing. Nothing observable
 *       server-side changes.
 *
 * WHERE THE PLAYER'S REAL RANK LIVES
 *
 *   PlayerProfile_Struct, Titanium layout, offset 13055 -- one byte after
 *   `gm` at 13054, which this client already parses and T148 verified live.
 *   The server fills it in common/patches/titanium.cpp:1494, folding its
 *   internal 0..8 rank scale down to the three Titanium values:
 *
 *       GUILD_SENIOR_MEMBER/MEMBER/JUNIOR_MEMBER/INITIATE/RECRUIT -> 0
 *       GUILD_OFFICER / GUILD_SENIOR_OFFICER                      -> 1
 *       GUILD_LEADER                                              -> 2
 *       anything else (incl. not in a guild)                      -> 9
 *
 *   So the client CAN know its own rank; it simply was not parsing the byte.
 *   EverQuest::GetGuildRank() now returns it, in exactly that encoding.
 */

#ifndef EQT_CLIENT_GUILD_PACKET_BODIES_H
#define EQT_CLIENT_GUILD_PACKET_BODIES_H

#include <cstdint>
#include <cstring>
#include <string>

#include "common/packet_structs.h"

namespace EQ {
namespace guild {

// Titanium rank encoding, as the wire and PlayerProfile offset 13055 use it.
// Mirrors akk-stack/code/common/guilds.h GUILD_*_TI.
enum TitaniumRank : uint8_t {
    kRankMember  = 0,
    kRankOfficer = 1,
    kRankLeader  = 2,
    kRankNone    = 9,  // GUILD_RANK_NONE_TI == GUILD_MAX_RANK + 1
};

// The rank a plain `/guildinvite` asks the server to grant the invitee.
// NOT the sender's rank -- see the file header.
constexpr uint8_t kInviteGrantsRank = kRankMember;

inline void CopyName(char* dst, size_t cap, const std::string& src) {
    std::memset(dst, 0, cap);
    std::strncpy(dst, src.c_str(), cap - 1);
}

// OP_GuildInvite body. `officer` is the rank granted to `target_name`.
inline EQT::GuildCommand_Struct BuildGuildInvite(const std::string& target_name,
                                                 const std::string& my_name,
                                                 uint32_t guild_id,
                                                 uint8_t grant_rank = kInviteGrantsRank) {
    EQT::GuildCommand_Struct out;
    std::memset(&out, 0, sizeof(out));
    CopyName(out.othername, sizeof(out.othername), target_name);
    CopyName(out.myname, sizeof(out.myname), my_name);
    out.guildeqid = static_cast<uint16_t>(guild_id);
    out.officer = grant_rank;
    return out;
}

// OP_GuildRemove body. `leaderstatus` is the remover's own rank; eqemu's
// Handle_OP_GuildRemove never reads it.
inline EQT::GuildRemove_Struct BuildGuildRemove(const std::string& target_name,
                                                 const std::string& my_name,
                                                 uint8_t my_rank) {
    EQT::GuildRemove_Struct out;
    std::memset(&out, 0, sizeof(out));
    CopyName(out.target, sizeof(out.target), target_name);
    CopyName(out.name, sizeof(out.name), my_name);
    out.leaderstatus = my_rank;
    return out;
}

}  // namespace guild
}  // namespace EQ

#endif  // EQT_CLIENT_GUILD_PACKET_BODIES_H
