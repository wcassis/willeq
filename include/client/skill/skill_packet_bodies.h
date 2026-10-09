/*
 * skill_packet_bodies.h -- wire bodies for the skill opcodes that eqemu
 * refuses to accept empty (T152).
 *
 * WHY THIS EXISTS
 *
 *   `SkillManager::sendSkillUsePacket()` historically sent `packet.Resize(0)`
 *   for all seventeen skills that have a dedicated opcode. T152 audited every
 *   one of them against `/mnt/projects/claude/EQEmu/zone/client_packet.cpp`
 *   and found twelve are genuinely empty-packet activations, while five are
 *   not:
 *
 *     OP_ApplyPoison  -- DECODE_LENGTH_EXACT(8), needs the poison's slot
 *     OP_Bind_Wound   -- needs the target id; the handler's size check does
 *                        NOT return, so an empty body reaches a null pBuffer
 *     OP_Disarm       -- needs source+target+skill, gated, silently dropped
 *     OP_PickPocket   -- needs the victim id; size check also does NOT return
 *     OP_Taunt        -- gated on ClientTarget_Struct (4 bytes)
 *
 *   Three of those five already have a correct sender elsewhere in this client
 *   (`EverQuest::SendApplyPoison`, `EverQuest::SendBindWound`,
 *   `CombatManager::Taunt`), so the skill path routes through them rather than
 *   growing a parallel copy -- the duplicated-then-drifted defect T147 spent a
 *   unit deleting. Only Disarm and PickPocket had no sender at all; their
 *   bodies are built here.
 *
 * AUTHORITY
 *
 *   Neither opcode has an ENCODE/DECODE in `common/patches/titanium.cpp`, so
 *   per the arbiter rule the base `common/eq_packet_structs.h` struct goes on
 *   the wire raw. Both headers are `#pragma pack(1)`, so the offsets below are
 *   byte-exact. THESE ARE SOURCE-DERIVED, NOT CAPTURED -- no live wire probe
 *   has confirmed them yet (T152 held no eqemu lock).
 */

#ifndef EQT_CLIENT_SKILL_PACKET_BODIES_H
#define EQT_CLIENT_SKILL_PACKET_BODIES_H

#include <array>
#include <cstdint>
#include <cstring>

namespace EQ {
namespace skill {

// ---------------------------------------------------------------------------
// Little-endian writers. memcpy-based on purpose: these bodies are packed and
// writing through a pointer cast at an unaligned offset is UB.
// ---------------------------------------------------------------------------
inline void PutU32LE(uint8_t* p, size_t off, uint32_t v) {
    const uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xff),
        static_cast<uint8_t>((v >> 8) & 0xff),
        static_cast<uint8_t>((v >> 16) & 0xff),
        static_cast<uint8_t>((v >> 24) & 0xff),
    };
    std::memcpy(p + off, b, 4);
}

inline void PutU16LE(uint8_t* p, size_t off, uint16_t v) {
    const uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xff),
        static_cast<uint8_t>((v >> 8) & 0xff),
    };
    std::memcpy(p + off, b, 2);
}

// ---------------------------------------------------------------------------
// OP_Disarm -- eqemu Disarm_Struct, common/eq_packet_structs.h:2001
//
//   /*000*/ uint32 source;   // MUST equal our own entity id, or the server
//                            // files a POSSIBLE_HACK player-event against the
//                            // account (client_packet.cpp:5775)
//   /*004*/ uint32 target;
//   /*008*/ uint32 skill;    // MUST equal the server's idea of our Disarm
//                            // skill or the handler falls through to
//                            // DISARM_NO_TARGET (client_packet.cpp:5804)
//   /*012*/ uint32 unknown;
// ---------------------------------------------------------------------------
inline constexpr size_t kDisarmBodyLen      = 16;
inline constexpr size_t kDisarmOffSource    = 0;
inline constexpr size_t kDisarmOffTarget    = 4;
inline constexpr size_t kDisarmOffSkill     = 8;
inline constexpr size_t kDisarmOffUnknown   = 12;

inline std::array<uint8_t, kDisarmBodyLen>
MakeDisarmBody(uint32_t source, uint32_t target, uint32_t skill) {
    std::array<uint8_t, kDisarmBodyLen> body{};
    PutU32LE(body.data(), kDisarmOffSource,  source);
    PutU32LE(body.data(), kDisarmOffTarget,  target);
    PutU32LE(body.data(), kDisarmOffSkill,   skill);
    PutU32LE(body.data(), kDisarmOffUnknown, 0);
    return body;
}

// ---------------------------------------------------------------------------
// OP_PickPocket -- eqemu PickPocket_Struct, common/eq_packet_structs.h:3556
// ("// Size 18")
//
//   /*000*/ uint32 to;        // victim entity id -- the ONLY field the
//                             // inbound handler reads (client_packet.cpp:11932)
//   /*004*/ uint32 from;
//   /*008*/ uint16 myskill;
//   /*010*/ uint8  type;
//   /*011*/ uint8  unknown1;
//   /*012*/ uint32 coin;
//   /*016*/ uint8  lastsix[2];
//
// The trailing fields are the server's RESPONSE payload; eqemu ignores them on
// input. They are zero-filled here rather than guessed.
// ---------------------------------------------------------------------------
inline constexpr size_t kPickPocketBodyLen   = 18;
inline constexpr size_t kPickPocketOffTo     = 0;
inline constexpr size_t kPickPocketOffFrom   = 4;
inline constexpr size_t kPickPocketOffSkill  = 8;
inline constexpr size_t kPickPocketOffType   = 10;
inline constexpr size_t kPickPocketOffCoin   = 12;
inline constexpr size_t kPickPocketOffLast6  = 16;

inline std::array<uint8_t, kPickPocketBodyLen>
MakePickPocketBody(uint32_t to, uint32_t from, uint16_t myskill) {
    std::array<uint8_t, kPickPocketBodyLen> body{};
    PutU32LE(body.data(), kPickPocketOffTo,   to);
    PutU32LE(body.data(), kPickPocketOffFrom, from);
    PutU16LE(body.data(), kPickPocketOffSkill, myskill);
    // type, unknown1, coin, lastsix stay zero.
    return body;
}

// ---------------------------------------------------------------------------
// eqemu EQ::item::ItemTypePoison, common/item_data.h:100.
//
// NOTE: this client parses that field into `ItemInstance::skillType`
// (inventory_item_parser.cpp reads serialized field 56, which titanium.cpp:3429
// writes as `item->ItemType`). The name is a weapon-centric misnomer -- for
// weapons the ItemType enum values ARE the weapon skill ids, which is why the
// misnaming has never shown up -- but the field carries the full ItemType.
// ---------------------------------------------------------------------------
inline constexpr uint8_t kItemTypePoison = 42;

// B900 (#216): the ranged-weapon ItemType values, eqemu common/item_data.h
// (same ItemType enum kItemTypePoison above; ItemInstance::skillType carries
// it). The autofire/manual split keys off these (zone/client_process.cpp:345-
// 380, zone/special_attacks.cpp:880-945): a bow -> Archery, a large or small
// throwing item -> Throwing, anything else is not a ranged weapon.
inline constexpr uint8_t kItemTypeBow           = 5;  // EQ::item::ItemTypeBow
inline constexpr uint8_t kItemTypeLargeThrowing = 7;  // EQ::item::ItemTypeLargeThrowing
inline constexpr uint8_t kItemTypeSmallThrowing = 19; // EQ::item::ItemTypeSmallThrowing

} // namespace skill
} // namespace EQ

#endif // EQT_CLIENT_SKILL_PACKET_BODIES_H
