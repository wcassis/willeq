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

/*
 * Save structure -- OP_Save (C->S), 192 bytes.
 *
 * GROUND TRUTH, and how much of it there is.
 *
 * eqemu declares this payload as `struct Save_Struct { uint8 unknown00[192]; }`
 * (/mnt/projects/akk-stack/code/common/eq_packet_structs.h:5039, and the
 * identical Titanium copy at common/patches/titanium_structs.h:3203), and
 * `Client::Handle_OP_Save` (/mnt/projects/akk-stack/code/zone/client_packet.cpp
 * :13661) reads NOTHING out of it:
 *
 *     void Client::Handle_OP_Save(const EQApplicationPacket *app)
 *     {
 *         // The payload is 192 bytes - Not sure what is contained in payload
 *         Save();
 *     }
 *
 * There is no titanium.cpp DECODE for OP_Save either, so the bytes go on the
 * wire raw and no size check ever fires. The SERVER-VISIBLE contract is
 * therefore just "the opcode, with 192 bytes behind it".
 *
 * The field names below come from the T311 Leg A capture, not from eqemu:
 * four self-deaths of a real Titanium client, dumped server-side
 * (review/T311/legA/zonelogs/*_packetlog.log, "Size [194]" = 2 opcode + 192).
 * Diffing the four bodies byte by byte (T349) gives exactly one decodable
 * field group and a lot of noise:
 *
 *   [005..016]  three floats that are BYTE-IDENTICAL to the y/x/z of the last
 *               C->S OP_ClientUpdate before the death, in every one of the
 *               four -- including the z BIT PATTERN (0x40701060 = 3.750999 in
 *               the qeynos2 death, 0x41fe0622 = 31.752995 in the first qeynos
 *               death). Same y,x,z order as BecomeCorpse_Struct. So this is
 *               the client's current position in the WIRE frame; see
 *               include/client/position_frames.h.
 *   [110..174]  varies with no pattern and reads as uninitialized client
 *               memory: 32-bit heap pointers (0x1c832998, 0x1c556920, ...),
 *               and the fourth death does not even share the field SHAPE of
 *               the other three. Not reproducible, and nothing reads it.
 *   [000..003], [017..018]  vary per send, uncorrelated with anything in the
 *               capture.
 *   everything else  identical in all four bodies, across two zones. Emitted
 *               verbatim so our packet matches the oracle's bytes wherever the
 *               oracle is self-consistent. CAVEAT, recorded rather than
 *               hidden: all four samples come from ONE client process, so a
 *               "constant" here may be process-lifetime-stable stack content
 *               rather than a real field. The server reads none of it, so the
 *               distinction costs nothing either way.
 */
struct Save_Struct {
/*000*/ uint8_t  unknown000[4];   // varies every send; we zero it
/*004*/ uint8_t  unknown004;      // 0x15 in all four captures
/*005*/ float    y;               // server y  (== our internal PosX)
/*009*/ float    x;               // server x  (== our internal PosY)
/*013*/ float    z;               // WIRE-frame z, as in OP_ClientUpdate
/*017*/ uint8_t  unknown017[2];   // varies every send; we zero it
/*019*/ uint8_t  unknown019[30];  // zero in all four
/*049*/ uint32_t unknown049;      // 0x00002999 in all four
/*053*/ uint32_t unknown053;      // 0x00000030 in all four
/*057*/ uint8_t  unknown057[53];  // zero in all four
/*110*/ uint8_t  unknown110[65];  // uninitialized client memory; we zero it
/*175*/ uint8_t  unknown175[2];   // zero in all four
/*177*/ uint32_t unknown177;      // 0x00002999 in all four
/*181*/ uint32_t unknown181;      // 0x00000030 in all four
/*185*/ uint32_t unknown185;      // 0x00000037 in all four
/*189*/ uint8_t  unknown189[3];   // zero in all four
/*192*/
};
static_assert(sizeof(Save_Struct) == 192, "Save_Struct must be 192 bytes");  // eqemu eq_packet_structs.h:5039 declares it uint8[192] and Handle_OP_Save reads none of it

// The observed-constant bytes, so the sender and the layout test name them
// once. See the Save_Struct comment for where each number comes from.
inline constexpr uint8_t  kSaveUnknown004 = 0x15;
inline constexpr uint32_t kSaveUnknown049 = 0x00002999u;
inline constexpr uint32_t kSaveUnknown053 = 0x00000030u;
inline constexpr uint32_t kSaveUnknown177 = 0x00002999u;
inline constexpr uint32_t kSaveUnknown181 = 0x00000030u;
inline constexpr uint32_t kSaveUnknown185 = 0x00000037u;


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_CHARACTER_H
