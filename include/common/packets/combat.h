// Buff, consider, combat-ability, corpse-money, chat, death and despawn
// wire layouts.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_COMBAT_H
#define EQT_PACKETS_COMBAT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

/*
 * Spell Buff structure (in profile)
 * Size: 20 bytes
 */
struct SpellBuff_Struct {
/*000*/ uint8_t  effect_type;    // 0=no buff, 2=buff, 4=inverse
/*001*/ uint8_t  level;
/*002*/ uint8_t  bard_modifier;
/*003*/ uint8_t  unknown003;
/*004*/ uint32_t spellid;
/*008*/ int32_t  duration;
/*012*/ uint32_t counters;
/*016*/ uint32_t player_id;      // Caster ID
/*020*/
};

/*
 * Pet Buff Window structure
 * Sent when pet buffs change or on zone-in
 * Size: varies (30 spell slots)
 */
static constexpr int PET_BUFF_COUNT = 30;

struct PetBuff_Struct {
/*000*/ uint32_t petid;                        // Spawn ID of the pet
/*004*/ uint32_t spellid[PET_BUFF_COUNT];      // Array of spell IDs (0xFFFFFFFF = empty)
/*124*/ int32_t  ticsremaining[PET_BUFF_COUNT]; // Remaining ticks for each buff
/*244*/ uint32_t buffcount;                    // Number of active buffs
/*248*/
};

/*
 * Consider structure (OP_Consider)
 * Size: 28 bytes
 *
 * Verified against eqemu 2026-08-19 (T147): titanium.cpp has no
 * ENCODE/DECODE for OP_Consider, so the base struct is the wire format --
 * common/eq_packet_structs.h:1351, which is byte-identical to
 * common/patches/titanium_structs.h:1160. Both carry `pvpcon` + a 3-byte
 * tail that this struct was missing (24 instead of 28).
 *
 * NOT wire-observed. eq_combat_actions.cpp:40 parses OP_Consider with a
 * private local copy of this layout and a `p.Length() < 30` guard (2 opcode
 * + 28), which is the correct size -- so the hand-rolled parser was right and
 * this shared struct was the one that had rotted.
 */
struct Consider_Struct {
/*000*/ uint32_t playerid;
/*004*/ uint32_t targetid;
/*008*/ uint32_t faction;
/*012*/ uint32_t level;
/*016*/ int32_t  cur_hp;
/*020*/ int32_t  max_hp;
/*024*/ uint8_t  pvpcon;        // Pvp con flag 0/1
/*025*/ uint8_t  unknown3[3];
/*028*/
};
static_assert(sizeof(Consider_Struct) == 28, "Consider_Struct must be 28 bytes");

/*
 * Combat Ability structure (kick, bash, taunt, etc.)
 * Size: 12 bytes
 */
struct CombatAbility_Struct {
/*00*/ uint32_t target;      // Target entity ID
/*04*/ uint32_t attack;      // Attack value (usually 0)
/*08*/ uint32_t skill;       // Skill ID (e.g., SKILL_KICK, SKILL_BASH)
/*12*/
};
static_assert(sizeof(CombatAbility_Struct) == 12, "CombatAbility_Struct must be 12 bytes");  // eqemu titanium_structs.h:1572

/*
 * Money on Corpse structure
 * Size: 20 bytes
 */
struct MoneyOnCorpse_Struct {
/*00*/ uint8_t  response;
/*01*/ uint8_t  unknown01[3];
/*04*/ uint32_t platinum;
/*08*/ uint32_t gold;
/*12*/ uint32_t silver;
/*16*/ uint32_t copper;
/*20*/
};
static_assert(sizeof(MoneyOnCorpse_Struct) == 20, "MoneyOnCorpse_Struct must be 20 bytes");  // eqemu moneyOnCorpseStruct, titanium_structs.h:1451

/*
 * Channel Message structure (OP_ChannelMessage)
 * Variable size; fixed header is 148 bytes.
 *
 * Verified against eqemu 2026-08-19 (T147):
 * common/patches/titanium_structs.h:1020 (titanium.cpp has ENCODE and DECODE
 * for OP_ChannelMessage, so the Titanium struct is the arbiter; the base
 * struct at common/eq_packet_structs.h:1189 is byte-identical anyway).
 * `cm_unknown4` is uint32[2], not uint32 -- the message body starts at 148,
 * not 144.
 *
 * NOT wire-observed. src/client/eq_chat.cpp:31-32 already documents the
 * correct `unknown(8)` layout and parses at the right offsets by hand; this
 * shared struct was the copy that drifted, and it contradicted the correct
 * code sitting next to it.
 */
struct ChannelMessage_Struct {
/*000*/ char     targetname[64];
/*064*/ char     sender[64];
/*128*/ uint32_t language;
/*132*/ uint32_t chan_num;
/*136*/ uint32_t cm_unknown4[2];
/*144*/ uint32_t skill_in_language;
/*148*/ char     message[0];
};
static_assert(sizeof(ChannelMessage_Struct) == 148,
              "ChannelMessage_Struct fixed header must be 148 bytes");
static_assert(offsetof(ChannelMessage_Struct, message) == 148,
              "ChannelMessage_Struct message must start at offset 148");

/*
 * Death structure (OP_Death)
 * Size: 32 bytes
 *
 * !! FIELD ORDER UNSETTLED -- DO NOT "HARMONISE" THIS WITHOUT A CAPTURE !!
 * (T147, 2026-08-19)
 *
 * The size is certain (8 x uint32 in every eqemu variant). The order of
 * bindzoneid / spell_id / attack_skill is NOT:
 *   - common/eq_packet_structs.h:1367 has bindzoneid@12, spell_id@16,
 *     attack_skill@20 -- which is what this struct has;
 *   - common/patches/titanium_structs.h:1176 has attack_skill@12,
 *     spell_id@16, bindzoneid@20 -- exactly reversed.
 * titanium.cpp has no ENCODE(OP_Death), so the arbiter rule says the base
 * struct wins and our order is right. That is reasoning, not observation.
 * Only a live capture settles it; recorded in refactor/deferred.md.
 */
struct Death_Struct {
/*000*/ uint32_t spawn_id;
/*004*/ uint32_t killer_id;
/*008*/ uint32_t corpseid;
/*012*/ uint32_t bindzoneid;
/*016*/ uint32_t spell_id;
/*020*/ uint32_t attack_skill;
/*024*/ int32_t  damage;
/*028*/ uint32_t is_PC;
/*032*/
};
static_assert(sizeof(Death_Struct) == 32, "Death_Struct must be 32 bytes");  // eqemu eq_packet_structs.h:1367 -- FIELD ORDER UNSETTLED, see comment

/*
 * Delete Spawn structure
 * Size: 4 bytes
 */
struct DeleteSpawn_Struct {
/*00*/ uint32_t spawn_id;
/*04*/
};
static_assert(sizeof(DeleteSpawn_Struct) == 4, "DeleteSpawn_Struct must be 4 bytes");  // eqemu titanium_structs.h:1008 (DECODE(OP_DeleteSpawn) exists, so Titanium wins over the 5-byte base struct)

#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_COMBAT_H
