// Travel, group split, LFG, combat abilities, disciplines, banking and the
// phase 3 miscellaneous wire layouts.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_TRAVEL_MISC_H
#define EQT_PACKETS_TRAVEL_MISC_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// Phase 3: Travel System Structures
// ============================================================================

/**
 * Control Boat Struct
 * Used for TAKING or RELEASING control of a boat -- not for steering it.
 * Size: 8 bytes (Titanium format)
 * Used in: OP_ControlBoat (C->S)
 *
 * Ground truth: eqemu ControlBoat_Struct (common/eq_packet_structs.h:5327);
 * titanium.cpp has no ENCODE/DECODE for OP_ControlBoat, so this goes on the
 * wire raw and `Handle_OP_ControlBoat` (zone/client_packet.cpp:5421) rejects
 * a mismatch with a visible "Wrong size: OP_ControlBoat" log line.
 *
 * T145: our previous 20-byte version had a `heading` float and a `type`
 * control byte. NEITHER FIELD EXISTS ON THE WIRE -- the packet carries only
 * a boat entity id and a single take/release boolean. Boat heading is not
 * sent through this opcode.
 */
struct ControlBoat_Struct {
/*0000*/ uint32_t boatid;             // Boat entity ID
/*0004*/ uint8_t  take_control;       // 1 = taking control, 0 = releasing it
/*0005*/ uint8_t  unknown[3];         // Unknown
/*0008*/
};
static_assert(sizeof(ControlBoat_Struct) == 8, "ControlBoat_Struct must be 8 bytes");

// ============================================================================
// Phase 3: Group Split Structures
// ============================================================================

/**
 * Split Money Struct
 * Used for splitting money with group
 * Size: 16 bytes (Titanium format)
 * Used in: OP_Split (C->S)
 */
struct Split_Struct {
/*0000*/ uint32_t platinum;           // Platinum to split
/*0004*/ uint32_t gold;               // Gold to split
/*0008*/ uint32_t silver;             // Silver to split
/*0012*/ uint32_t copper;             // Copper to split
/*0016*/
};
static_assert(sizeof(Split_Struct) == 16, "Split_Struct must be 16 bytes");

// ============================================================================
// Phase 3: LFG System Structures
// ============================================================================

/**
 * LFG Appearance Struct
 * Used for LFG flag updates from server
 * Size: 8 bytes (Titanium format)
 * Used in: OP_LFGAppearance (S->C)
 */
struct LFG_Appearance_Struct {
/*0000*/ uint32_t spawn_id;           // Player spawn ID
/*0004*/ uint32_t lfg;                // 0=off, 1=lfg
/*0008*/
};
static_assert(sizeof(LFG_Appearance_Struct) == 8, "LFG_Appearance_Struct must be 8 bytes");

// ============================================================================
// Phase 3: Combat Abilities Structures
// ============================================================================

/**
 * Shielding Struct
 * Used for warrior/paladin shielding ability
 * Size: 4 bytes (Titanium format)
 * Used in: OP_Shielding (C->S)
 */
struct Shielding_Struct {
/*0000*/ uint32_t target_id;          // Target to shield
/*0004*/
};
static_assert(sizeof(Shielding_Struct) == 4, "Shielding_Struct must be 4 bytes");

/**
 * Environmental Damage Struct
 * Used for damage from environment (falling, lava, drowning, traps)
 * Size: 31 bytes (Titanium format)
 * Used in: OP_EnvDamage
 *
 * Ground truth: eqemu EnvDamage2_Struct -- base
 * common/eq_packet_structs.h:3020 and Titanium
 * common/patches/titanium_structs.h:2251 are byte-identical, and titanium.cpp
 * has no ENCODE/DECODE for OP_EnvDamage, so this struct goes on the wire raw
 * and `Client::Handle_OP_EnvDamage`'s exact-size check
 * (zone/client_packet.cpp:6301) is the gate.
 *
 * DIRECTION (T145): despite the old comment above, OP_EnvDamage is C->S in
 * eqemu -- it is registered in ConnectedOpcodes (zone/client_packet.cpp:198)
 * and the server NEVER sends it. `ZoneProcessEnvDamage` is therefore an
 * unreachable receive path regardless of the struct size; there is no
 * corresponding sender in this client at all. See refactor/T145 Review.
 *
 * dmgtype values: eqemu EQ::constants::EnvironmentalDamage
 * (common/emu_constants.h:308) -- 250=Lava, 251=Drowning, 252=Falling,
 * 253=Trap.
 */
// T145 needed a LOCAL pack(1) here because this struct used to sit below the
// file's `#pragma pack(pop)`; at natural alignment its correct field list
// measured 36 bytes instead of 31. T147 moved that pop to EOF, so the local
// pragmas are gone and this struct is covered by the file-level pack(1) like
// every other struct in the header.
struct EnvDamage2_Struct {
/*0000*/ uint32_t id;                 // Entity ID
/*0004*/ uint16_t unknown4;           // Unknown
/*0006*/ uint32_t damage;             // Amount of damage
/*0010*/ uint8_t  unknown10[12];      // Unknown
/*0022*/ uint8_t  dmgtype;            // 250=lava, 251=drowning, 252=falling, 253=trap
/*0023*/ uint8_t  unknown2[4];        // Unknown
/*0027*/ uint16_t constant;           // Always 0xFFFF
/*0029*/ uint16_t unknown29;          // Unknown
/*0031*/
};
static_assert(sizeof(EnvDamage2_Struct) == 31, "EnvDamage2_Struct must be 31 bytes");

// ============================================================================
// Phase 3: Discipline System Structures
// (Disciplines_Struct is already defined with MAX_PP_DISCIPLINES)
// ============================================================================

/**
 * Discipline Timer Struct
 * Used for discipline reuse timers
 * Size: 12 bytes (Titanium format)
 * Used in: OP_DisciplineTimer (S->C)
 *
 * Ground truth: eqemu DisciplineTimer_Struct
 * (common/eq_packet_structs.h:5418) -- TimerID, Duration, Unknown08.
 * T145: we were missing the trailing `unknown08`. Nothing was broken by this
 * today (the only consumer is a read whose guard is `<`, and both consumed
 * fields are at correct offsets), but it would have been a live bug the
 * moment this struct was used to SEND.
 */
struct DisciplineTimer_Struct {
/*0000*/ uint32_t timer_id;           // Timer slot ID (eqemu: TimerID)
/*0004*/ uint32_t timer_value;        // Remaining time in seconds (eqemu: Duration)
/*0008*/ uint32_t unknown08;          // Unknown
/*0012*/
};
static_assert(sizeof(DisciplineTimer_Struct) == 12, "DisciplineTimer_Struct must be 12 bytes");

// ============================================================================
// Phase 3: Banking Structures
// ============================================================================

/**
 * Banker Change Struct
 * Used for banker/currency updates
 * Size: 32 bytes (Titanium format)
 * Used in: OP_BankerChange (S->C)
 */
struct BankerChange_Struct {
/*0000*/ uint32_t platinum;           // Bank platinum
/*0004*/ uint32_t gold;               // Bank gold
/*0008*/ uint32_t silver;             // Bank silver
/*0012*/ uint32_t copper;             // Bank copper
/*0016*/ uint32_t platinum_bank;      // On-hand platinum (duplicate?)
/*0020*/ uint32_t gold_bank;          // On-hand gold (duplicate?)
/*0024*/ uint32_t silver_bank;        // On-hand silver (duplicate?)
/*0028*/ uint32_t copper_bank;        // On-hand copper (duplicate?)
/*0032*/
};
static_assert(sizeof(BankerChange_Struct) == 32, "BankerChange_Struct must be 32 bytes");

// ============================================================================
// Phase 3: Misc Structures
// ============================================================================

/**
 * Popup Response Struct
 * Used for responding to popup dialogs
 * Size: 8 bytes (Titanium format)
 * Used in: OP_PopupResponse (C->S)
 *
 * Ground truth: eqemu PopupResponse_Struct (common/eq_packet_structs.h:1903);
 * titanium.cpp has no ENCODE/DECODE for OP_PopupResponse, so this goes on the
 * wire raw and `Handle_OP_PopupResponse` (zone/client_packet.cpp:11976)
 * rejects a mismatch.
 *
 * T145: our previous 12-byte version had a third `response` field carrying
 * "which button was clicked". THAT FIELD IS FICTIONAL -- the wire format
 * carries only the popup id, and eqemu's handler dispatches on the id alone.
 */
struct PopupResponse_Struct {
/*0000*/ uint32_t sender;             // eqemu: unknown0000
/*0004*/ uint32_t popup_id;           // Popup ID from PopupText
/*0008*/
};
static_assert(sizeof(PopupResponse_Struct) == 8, "PopupResponse_Struct must be 8 bytes");


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_TRAVEL_MISC_H
