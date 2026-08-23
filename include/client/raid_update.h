// Decoding for OP_RaidUpdate (S->C), the packet every raid membership event
// arrives on.
//
// T169. Before this, ZoneProcessRaidUpdate read a single uint32 action and
// switched on six hand-guessed values. Four of the six were WRONG against
// eqemu's own enum (common/raid.h), the body was never parsed at all, and the
// action a raid INVITE actually produces fell through to "Unknown" -- so an
// invited player was told nothing whatsoever.
//
// The action codes below are eqemu's `enum { raidAdd = 0, ... }` verbatim.
// The names are eqemu's names, deliberately: renaming them to something more
// readable is how a mapping like this drifts out of agreement with its
// authority. The comments record what the SERVER does at each send site
// (zone/raids.cpp, zone/client_packet.cpp) rather than what the name suggests.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {
namespace Net {
class Packet;
}
}

namespace EQT {

// eqemu common/raid.h, "raid packet types". Values are wire values; do not
// renumber. Codes eqemu declares but never sends on Titanium are included so
// an arriving one is named in the log instead of appearing as a bare integer.
enum class RaidAction : uint32_t {
    Add                = 0,   // a member joined. Body is RaidAddMember_Struct.
    Remove2            = 1,   // a member left. parameter = 0.
    MemberNameChange   = 2,
    Remove1            = 3,   // a member left. parameter = 0xFFFFFFFF.
    NoLongerLeader     = 4,
    Disband            = 5,   // the raid disbanded (this IS the disband code)
    Members            = 6,   // full details + member list (len 395+) on
                              // later clients -- NOT on Titanium, whose
                              // ENCODE has no case for it and ships a
                              // bare general with the list stripped.
    NoAssignLeadership = 7,
    Create             = 8,   // raid formed. Body is RaidCreate_Struct (72 B).
    NoRaid             = 10,  // "you are not in a raid". See the raw-body note.
    ChangeLootType     = 11,
    StringID           = 12,
    ChangeGroupLeader  = 13,
    SetLeaderAbilities = 14,
    SetLeaderData      = 15,
    ChangeGroup        = 16,
    Lock               = 17,
    Unlock             = 18,
    RedStringID        = 19,
    SetLeader          = 20,  // ALSO how an INVITE reaches the invited client
    MakeLeader         = 30,
    SetMotd            = 35,
    SetNote            = 36,
};

struct RaidUpdate {
    uint32_t raw_action = 0;         // as it arrived, even if unrecognised
    RaidAction action = RaidAction::Add;
    bool known_action = false;       // false => raw_action is not in the enum

    std::string player_name;         // the member this event is about
    std::string leader_name;         // the raid leader, or for an invite (20)
                                     // the player doing the inviting
    uint32_t parameter = 0;          // action-dependent; group number on Add
    bool has_parameter = false;      // false where the body has no such field

    // Action 0 (Add) only -- RaidAddMember_Struct's tail.
    bool has_member_info = false;
    uint8_t member_class = 0;
    uint8_t member_level = 0;
    bool is_group_leader = false;

    // Actions 35 (SetMotd) / 36 (SetNote) only.
    std::string text;

    // Action 8 (Create) only -- RaidCreate_Struct carries an entity id where
    // the general body has its second name.
    bool has_leader_id = false;
    uint32_t leader_id = 0;

    // Action 10 only. TRUE means this is not a raid message at all: it is
    // Client::CompleteConnect's zone-in name handshake (zone/client.cpp:859),
    // which reuses OP_RaidUpdate to carry a ZoneInSendName_Struct whose
    // `unknown0` happens to be 0x0A. See the shape table below.
    bool zone_in_name = false;

    bool valid = false;              // false => the packet was too short for
                                     // the shape its action implies
};

// Decodes one OP_RaidUpdate body. `base` is the offset of the struct within
// the packet (2 in the live path -- the opcode precedes it).
//
// THE BODY SHAPE DEPENDS ON THE ACTION. There is no single raid struct:
//   * action 0  -> Titanium RaidAddMember_Struct (140 B)
//   * action 8  -> emu RaidCreate_Struct (72 B), sent RAW: titanium.cpp has no
//                  ENCODE for OP_RaidJoin, which is the SAME wire opcode
//   * action 10 -> TWO DIFFERENT PACKETS, told apart by LENGTH (T180):
//                  136 B = ZoneInSendName_Struct, the zone-in name handshake.
//                    Client::CompleteConnect (zone/client.cpp:859) sends
//                    OP_RaidUpdate carrying {unknown0=0x0A, name[64],
//                    name2[64], unknown132} -- both names our own. This is
//                    what every zone entry produces, raid or no raid, and it
//                    is NOT a raid event: `zone_in_name` is set and the
//                    caller must not touch raid state on it.
//                  140 B = emu RaidGeneral_Struct, sent RAW by ENCODE's
//                    `case raidNoRaid: dest->QueuePacket(inapp)`, so its
//                    leader_name/parameter sit 4 bytes later than every other
//                    action's. Only `action` is at a common offset. NOTE:
//                    nothing in eqemu's zone/ actually sends this -- only the
//                    six patch encoders name raidNoRaid -- so the 140-byte
//                    shape is decoded for completeness, not because it has
//                    ever been observed.
//   * 14 / 30   -> RaidLeadershipUpdate_Struct: general-compatible for the two
//                  names, but offset 132 is leadership AA data, NOT parameter
//   * 35 / 36   -> RaidGeneral_Struct + a trailing motd/note string
//   * all else  -> Titanium RaidGeneral_Struct (136 B)
// Reading `parameter` at a fixed offset for every action -- the obvious
// implementation -- is wrong for 8, 10, 14 and 30.
RaidUpdate DecodeRaidUpdate(const EQ::Net::Packet& p, size_t base);

// eqemu's own name for an action code, or "Unknown(N)". Log-facing.
std::string RaidActionName(uint32_t raw_action);

// Encode an outbound OP_RaidCommand body (Titanium wire opcode OP_RaidInvite
// 0x5891, routed by eqemu to Handle_OP_RaidCommand). eqemu's
// DECODE(OP_RaidInvite) (titanium.cpp) reads the TITANIUM 136-byte wire layout
// and converts it to the 140-byte internal struct, so this is the 136-byte one:
//   action@0, player_name@4[64], leader_name@68[64], parameter@132
// Sending the internal 140-byte layout instead puts leader_name 4 bytes too
// late; the DECODE then reads it as empty and the handler silently drops the
// command (found live, T179). Names are NUL-padded and truncated to 63 chars +
// terminator. See eqemu
// common/raid.h for the RaidCommand* action codes; the C->S ones we use are
// 1 (AcceptInvite), 3 (Invite), 5 (Disband). For AcceptInvite the field roles
// are: player_name = the INVITER, leader_name = US (the invitee) --
// Handle_OP_RaidCommand's RaidCommandAcceptInvite arm resolves them that way
// (zone/client_packet.cpp).
std::vector<uint8_t> encodeRaidCommand(uint32_t action,
                                       const std::string& player_name,
                                       const std::string& leader_name,
                                       uint32_t parameter = 0);

// ---------------------------------------------------------------------------
// Roster (T180)
//
// TITANIUM NEVER DELIVERS A ROSTER PACKET. eqemu's action 6 (raidMembers) --
// the one that carries the whole member list -- has no `case` in
// common/patches/titanium.cpp's ENCODE(OP_RaidUpdate), so it falls through the
// `default` arm and arrives as a bare 136-byte general with the list gone.
//
// The roster a Titanium client can have is therefore one it ASSEMBLES from the
// action-0 (raidAdd) stream. eqemu emits exactly that: Raid::SendBulkRaid
// (zone/raids.cpp:1317) walks the members and sends one raidAdd per member to
// a client that joins or zones in, and SendRaidAddAll broadcasts one to the
// whole raid when somebody new joins. A group change is a Remove followed by
// an Add (Raid::SendRaidMove), so it needs no separate handling here.

// eqemu zone/raids.h:72 -- a member who is in the raid but in no group.
constexpr uint32_t kRaidGroupless = 0xFFFFFFFFu;

// eqemu zone/raids.h:70-71. A raid is 12 groups of 6, plus an unlimited
// "ungrouped bank" that members sit in until somebody assigns them.
constexpr uint32_t kMaxRaidGroups = 12;
constexpr size_t kMaxRaidGroupSize = 6;

struct RaidMemberInfo {
    std::string name;
    uint8_t class_id = 0;
    uint8_t level = 0;
    uint32_t group = kRaidGroupless;  // raidAdd's `parameter`
    bool is_group_leader = false;
};

// Membership assembled from the decoded update stream. Deliberately separate
// from EverQuest so the policy below is testable without a live client; the
// same object is the client's only source of truth for who is in the raid.
class RaidRoster {
public:
    // Applies one decoded update. `my_name` is this character's name: a Remove
    // that names US, a Disband, or a real NoRaid means we are out of the raid
    // entirely and the whole roster goes, not one row.
    //
    // A `zone_in_name` update is NOT a raid event and is ignored here.
    void Apply(const RaidUpdate& u, const std::string& my_name);

    void Clear() { m_members.clear(); }

    const std::vector<RaidMemberInfo>& Members() const { return m_members; }
    size_t Size() const { return m_members.size(); }
    bool Empty() const { return m_members.empty(); }

    // nullptr if `name` is not in the roster. Case-sensitive: eqemu's own
    // member lookups (GetClientByName, the strcmp walks in raids.cpp) are too.
    const RaidMemberInfo* Find(const std::string& name) const;

    // T184: how many members are currently in `group` (a WIRE group number,
    // 0-based, or kRaidGroupless). eqemu refuses a move into a group that
    // already holds kMaxRaidGroupSize -- silently, by falling out of the
    // `if (group_count < 6)` in Handle_OP_RaidCommand -- so the client counts
    // for itself in order to say something instead.
    size_t CountInGroup(uint32_t group) const;

private:
    std::vector<RaidMemberInfo> m_members;  // insertion order = arrival order
};

// ---------------------------------------------------------------------------
// Loot rules (T181)
//
// eqemu zone/zonedb.h:381, `namespace RaidLootTypes`. The raid's loot type is
// consulted in zone/attack.cpp:2887 when a corpse is looted; 4 (All) is the
// default a new raid is created with (zone/raids.cpp:49).
enum class RaidLootType : uint32_t {
    RaidLeader  = 1,  // only the raid leader may loot
    GroupLeader = 2,  // raid leader + group leaders
    Selected    = 3,  // raid leader + the named looters (AddLooter/RemoveLooter)
    All         = 4,  // anyone (the default)
};

// Accepts either a digit ("1".."4") or a word ("leader", "groupleader",
// "selected", "all"). Returns false and leaves `out` untouched on anything
// else -- the server has no validation of its own here (Handle_OP_RaidCommand's
// LootType arm passes `parameter` straight to Raid::ChangeLootType), so a
// typo'd loot type would otherwise be stored verbatim and silently disable
// looting for everyone.
bool ParseRaidLootType(const std::string& text, uint32_t& out);

// Human-readable name for a loot type, or "Unknown(N)".
std::string RaidLootTypeName(uint32_t loot_type);

// Parses the group argument of `/raid group <name> <group>`.
//
// NUMBERING: the user-facing number is **1-based**, matching what `/raid list`
// prints ("Group 1".."Group 12"); the wire is **0-based**. A user who reads
// "Group 2" off the roster and types `2` must land in that same group, so the
// conversion happens here, once. `none` / `out` / `ungrouped` select the
// ungrouped bank (any wire value >= kMaxRaidGroups takes eqemu's else branch;
// we send kRaidGroupless because it says what it means).
//
// Returns false on anything else -- including `0`, which is the near-miss a
// user thinking in wire numbers would type. eqemu does not validate either:
// `parameter` outside 0..11 is simply treated as "ungrouped", so an
// unchecked `0` would silently move somebody OUT of their group.
bool ParseRaidGroup(const std::string& text, uint32_t& out_wire_group);

// "Group 3", or "no group" for kRaidGroupless. Takes a WIRE group number and
// prints it 1-based, the same convention /raid list uses.
std::string RaidGroupName(uint32_t wire_group);

}  // namespace EQT
