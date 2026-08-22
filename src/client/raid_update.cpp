#include "client/raid_update.h"

#include "common/net/packet.h"

#include <algorithm>

namespace EQT {

namespace {

// Titanium RaidGeneral_Struct (titanium_structs.h:3053), 136 bytes:
//   action@0, player_name@4[64], leader_name@68[64], parameter@132
// NOTE the difference from eqemu's INTERNAL RaidGeneral_Struct
// (eq_packet_structs.h:4874, 140 bytes), which has an extra unknown1@68 and
// therefore leader_name@72 / parameter@136. Only action 10 arrives in that
// internal layout -- see the header's shape table.
constexpr size_t GEN_ACTION = 0;
constexpr size_t GEN_PLAYER_NAME = 4;
constexpr size_t GEN_LEADER_NAME = 68;
constexpr size_t GEN_PARAMETER = 132;
constexpr size_t GEN_SIZE = 136;
constexpr size_t NAME_LEN = 64;

// Titanium RaidAddMember_Struct: RaidGeneral_Struct + _class, level,
// isGroupLeader, unknown139.
constexpr size_t ADD_CLASS = 136;
constexpr size_t ADD_LEVEL = 137;
constexpr size_t ADD_IS_GROUP_LEADER = 138;
constexpr size_t ADD_SIZE = 140;

// emu RaidCreate_Struct (eq_packet_structs.h:4919), sent raw: action@0,
// leader_name@4[64], leader_id@68.
constexpr size_t CREATE_LEADER_NAME = 4;
constexpr size_t CREATE_LEADER_ID = 68;
constexpr size_t CREATE_SIZE = 72;

// emu RaidGeneral_Struct, the raw layout action 10 arrives in.
constexpr size_t EMU_GEN_SIZE = 140;

// Trailing text for actions 35/36 begins where the general body ends.
constexpr size_t TEXT_OFFSET = GEN_SIZE;

std::string readName(const EQ::Net::Packet& p, size_t offset) {
    // Fixed-width, NUL-padded. GetCString is bounded (T08) but would run past
    // the field into the next one if the server ever filled all 64 bytes, so
    // take the field and cut at its own terminator.
    std::string s = p.GetString(offset, NAME_LEN);
    const size_t nul = s.find('\0');
    if (nul != std::string::npos) {
        s.resize(nul);
    }
    return s;
}

bool isKnown(uint32_t a) {
    switch (a) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17:
    case 18: case 19: case 20: case 30: case 35: case 36:
        return true;
    default:
        return false;
    }
}

}  // namespace

std::string RaidActionName(uint32_t raw_action) {
    switch (raw_action) {
    case 0:  return "raidAdd";
    case 1:  return "raidRemove2";
    case 2:  return "raidMemberNameChange";
    case 3:  return "raidRemove1";
    case 4:  return "raidNoLongerLeader";
    case 5:  return "raidDisband";
    case 6:  return "raidMembers";
    case 7:  return "raidNoAssignLeadership";
    case 8:  return "raidCreate";
    case 10: return "raidNoRaid";
    case 11: return "raidChangeLootType";
    case 12: return "raidStringID";
    case 13: return "raidChangeGroupLeader";
    case 14: return "raidSetLeaderAbilities";
    case 15: return "raidSetLeaderData";
    case 16: return "raidChangeGroup";
    case 17: return "raidLock";
    case 18: return "raidUnlock";
    case 19: return "raidRedStringID";
    case 20: return "raidSetLeader";
    case 30: return "raidMakeLeader";
    case 35: return "raidSetMotd";
    case 36: return "raidSetNote";
    default: return "Unknown(" + std::to_string(raw_action) + ")";
    }
}

RaidUpdate DecodeRaidUpdate(const EQ::Net::Packet& p, size_t base) {
    RaidUpdate u;

    if (p.Length() < base + 4) {
        return u;  // not even an action code; valid stays false
    }

    u.raw_action = p.GetUInt32(base + GEN_ACTION);
    u.known_action = isKnown(u.raw_action);
    if (u.known_action) {
        u.action = static_cast<RaidAction>(u.raw_action);
    }

    const size_t body = p.Length() - base;

    // Action 8 is the odd one out: a different struct entirely, and it arrives
    // on the same wire opcode because Titanium maps OP_RaidJoin and
    // OP_RaidUpdate to the same value.
    if (u.raw_action == static_cast<uint32_t>(RaidAction::Create)) {
        if (body < CREATE_SIZE) {
            return u;
        }
        u.leader_name = readName(p, base + CREATE_LEADER_NAME);
        u.leader_id = p.GetUInt32(base + CREATE_LEADER_ID);
        u.has_leader_id = true;
        u.valid = true;
        return u;
    }

    // Action 10 arrives in eqemu's INTERNAL layout, unencoded. Its names and
    // parameter are shifted 4 bytes; nothing but the action is where the
    // Titanium layout puts it. The action alone is the whole message, so read
    // no further rather than reading a shifted field as if it were aligned.
    if (u.raw_action == static_cast<uint32_t>(RaidAction::NoRaid)) {
        u.valid = (body >= EMU_GEN_SIZE);
        return u;
    }

    if (body < GEN_SIZE) {
        return u;
    }

    u.player_name = readName(p, base + GEN_PLAYER_NAME);
    u.leader_name = readName(p, base + GEN_LEADER_NAME);
    u.valid = true;

    // Offset 132 is `parameter` for the general and add-member shapes, and for
    // the motd/note bodies that embed a general. For 14/30 it is the first
    // bytes of the leadership AA block -- reading it as a parameter would
    // report AA data as a group number.
    const bool leadership = (u.raw_action == static_cast<uint32_t>(RaidAction::SetLeaderAbilities) ||
                             u.raw_action == static_cast<uint32_t>(RaidAction::MakeLeader));
    if (!leadership && body >= GEN_PARAMETER + 4) {  // i.e. the full 136
        u.parameter = p.GetUInt32(base + GEN_PARAMETER);
        u.has_parameter = true;
    }

    if (u.raw_action == static_cast<uint32_t>(RaidAction::Add) && body >= ADD_SIZE) {
        u.member_class = p.GetUInt8(base + ADD_CLASS);
        u.member_level = p.GetUInt8(base + ADD_LEVEL);
        u.is_group_leader = p.GetUInt8(base + ADD_IS_GROUP_LEADER) != 0;
        u.has_member_info = true;
    }

    if ((u.raw_action == static_cast<uint32_t>(RaidAction::SetMotd) ||
         u.raw_action == static_cast<uint32_t>(RaidAction::SetNote)) &&
        body > TEXT_OFFSET) {
        // Variable length: eqemu's reply is "max 1024, but reply is variable",
        // so take what actually arrived and stop at the terminator.
        u.text = p.GetCString(base + TEXT_OFFSET);
    }

    return u;
}


std::vector<uint8_t> encodeRaidCommand(uint32_t action,
                                       const std::string& player_name,
                                       const std::string& leader_name,
                                       uint32_t parameter) {
    // 140-byte internal RaidGeneral_Struct: the handler length-checks against
    // this, not Titanium's 136-byte layout. See the header for field roles.
    constexpr size_t kSize = 140;
    constexpr size_t kPlayerName = 4;
    constexpr size_t kLeaderName = 72;
    constexpr size_t kParameter = 136;
    constexpr size_t kNameLen = 64;  // 63 chars + NUL

    std::vector<uint8_t> body(kSize, 0);

    auto putU32 = [&](size_t off, uint32_t v) {
        body[off + 0] = static_cast<uint8_t>(v & 0xff);
        body[off + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
        body[off + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
        body[off + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
    };
    auto putName = [&](size_t off, const std::string& name) {
        // NUL-padded, truncated to 63 chars so byte 63 is always the terminator.
        const size_t n = std::min(name.size(), kNameLen - 1);
        for (size_t i = 0; i < n; ++i) {
            body[off + i] = static_cast<uint8_t>(name[i]);
        }
        // remaining bytes already 0
    };

    putU32(0, action);
    putName(kPlayerName, player_name);
    // unknown1@68 stays 0
    putName(kLeaderName, leader_name);
    putU32(kParameter, parameter);
    return body;
}

}  // namespace EQT
