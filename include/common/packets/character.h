// Character-sheet wire layouts: bind points, AAs, disciplines, tributes,
// bandolier and potion belt, target, and the spell cast/memorize path.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_CHARACTER_H
#define EQT_PACKETS_CHARACTER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)


/*
 * Bind structure
 * Size: 20 bytes
 */
struct BindStruct {
/*000*/ uint32_t zone_id;
/*004*/ float    x;
/*008*/ float    y;
/*012*/ float    z;
/*016*/ float    heading;
/*020*/
};
static_assert(sizeof(BindStruct) == 20, "BindStruct must be 20 bytes");  // eqemu titanium_structs.h:743

/*
 * AA Array entry
 * Size: 8 bytes
 */
struct AA_Array {
    uint32_t AA;
    uint32_t value;
};
static_assert(sizeof(AA_Array) == 8, "AA_Array must be 8 bytes");  // eqemu titanium_structs.h:620

/*
 * Disciplines structure
 * Size: 400 bytes
 */
struct Disciplines_Struct {
    uint32_t values[MAX_PP_DISCIPLINES];
};

/*
 * Tribute structure
 * Size: 8 bytes
 */
struct Tribute_Struct {
    uint32_t tribute;
    uint32_t tier;
};
static_assert(sizeof(Tribute_Struct) == 8, "Tribute_Struct must be 8 bytes");  // eqemu titanium_structs.h:635

/*
 * Bandolier item
 * Size: 72 bytes
 */
struct BandolierItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};
static_assert(sizeof(BandolierItem_Struct) == 72, "BandolierItem_Struct must be 72 bytes");  // eqemu titanium_structs.h:650

/*
 * Bandolier set
 * Size: 320 bytes
 */
struct Bandolier_Struct {
    char     Name[32];
    BandolierItem_Struct Items[BANDOLIER_ITEM_COUNT];
};
static_assert(sizeof(Bandolier_Struct) == 320, "Bandolier_Struct must be 320 bytes");  // eqemu titanium_structs.h:658

/*
 * Potion belt item
 * Size: 72 bytes
 */
struct PotionBeltItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};
static_assert(sizeof(PotionBeltItem_Struct) == 72, "PotionBeltItem_Struct must be 72 bytes");  // eqemu titanium_structs.h:665

/*
 * Potion belt
 * Size: 288 bytes
 */
struct PotionBelt_Struct {
    PotionBeltItem_Struct Items[POTION_BELT_SIZE];
};
static_assert(sizeof(PotionBelt_Struct) == 288, "PotionBelt_Struct must be 288 bytes");  // eqemu titanium_structs.h:673

/*
 * Target structure (OP_TargetMouse)
 * Size: 4 bytes
 */
struct Target_Struct {
/*00*/ uint32_t target_id;
/*04*/
};
static_assert(sizeof(Target_Struct) == 4, "Target_Struct must be 4 bytes");  // eqemu ClientTarget_Struct, titanium_structs.h:985

/*
 * Begin Cast structure
 * Size: 8 bytes
 */
struct BeginCast_Struct {
/*000*/ uint16_t caster_id;
/*002*/ uint16_t spell_id;
/*004*/ uint32_t cast_time;  // in milliseconds
/*008*/
};
static_assert(sizeof(BeginCast_Struct) == 8, "BeginCast_Struct must be 8 bytes");  // eqemu titanium_structs.h:460

/*
 * Cast Spell structure
 * Size: 20 bytes
 */
struct CastSpell_Struct {
/*00*/ uint32_t slot;
/*04*/ uint32_t spell_id;
/*08*/ uint32_t inventoryslot;  // 0xFFFF for normal cast
/*12*/ uint32_t target_id;
/*16*/ uint8_t  cs_unknown[4];
/*20*/
};

/*
 * Memorize Spell structure
 * Used for memorizing spells to gem slots and forgetting them
 * Size: 16 bytes
 */
struct MemorizeSpell_Struct {
/*00*/ uint32_t slot;       // Gem slot (0-7)
/*04*/ uint32_t spell_id;   // Spell ID
/*08*/ uint32_t scribing;   // 1=memorize, 2=forget, 3=spellbar
/*12*/ uint32_t unknown0;   // 742 for memorize, 0 for forget
/*16*/
};

/*
 * Mana Change structure
 * Size: 16 bytes
 */
struct ManaChange_Struct {
/*00*/ uint32_t new_mana;
/*04*/ uint32_t stamina;
/*08*/ uint32_t spell_id;
/*12*/ uint8_t  keepcasting;
/*13*/ uint8_t  padding[3];
/*16*/
};
static_assert(sizeof(ManaChange_Struct) == 16, "ManaChange_Struct must be 16 bytes");  // eqemu titanium_structs.h:443 (ENCODE(OP_ManaChange) exists, so Titanium wins over the 20-byte base struct)


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_CHARACTER_H
