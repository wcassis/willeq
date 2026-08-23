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

private:
    std::vector<RaidMemberInfo> m_members;  // insertion order = arrival order
};

}  // namespace EQT
