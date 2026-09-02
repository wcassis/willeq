/*
 * WillEQ Packet Structures
 *
 * These structures define the Titanium client packet formats.
 * All structures are packed (1-byte aligned) to match network protocol.
 *
 * Based on EQEmu titanium_structs.h but standalone for the headless client.
 *
 * ---------------------------------------------------------------------------
 * RULES FOR THIS FILE (T147, 2026-08-19)
 *
 * 1. The `#pragma pack(push, 1)` at the top is popped at the very BOTTOM of
 *    the file. It used to be popped ~700 lines early, which left roughly 40
 *    structs compiling at natural alignment. That is the mechanism behind
 *    this file's whole wire-rot class: a struct written correctly against
 *    eqemu silently gains padding, someone "fixes" the static_assert to the
 *    padded value, and the result is a permanently-passing assert on a wrong
 *    wire layout (T145 hit exactly this with EnvDamage2_Struct: 36 instead of
 *    31). Do not add another pop.
 *
 * 2. Every struct here must be a WIRE layout. In-memory DTOs do not belong in
 *    this file -- see EQT::SpellActionInfo in include/client/spell/
 *    spell_manager.h, which used to live here as `Action_Struct` complete
 *    with authoritative-looking byte-offset comments while being 44 bytes
 *    against a 31-byte wire packet.
 *
 * 3. A struct that is wrong AND has no production consumer is a trap, not
 *    documentation: the next person to reinterpret_cast it gets the T134
 *    failure mode. Delete it rather than repairing it. T147 deleted 11 on
 *    that basis, plus FinishTrade_Struct -- which did have consumers, but
 *    described a payload eqemu sends with ZERO length.
 *
 * 4. `static_assert` values here are derived from eqemu's field lists, not
 *    from what the compiler happens to produce. If an assert fails, find out
 *    which side is wrong before touching the number.
 * ---------------------------------------------------------------------------
 */

#ifndef EQT_PACKET_STRUCTS_H
#define EQT_PACKET_STRUCTS_H

// J14: this file was 2,215 lines of wire layouts. It is now an umbrella over
// include/common/packets/. Every struct, enum, constant and compile-time size
// check moved verbatim; nothing was edited, and the files that include this
// header see exactly the same declarations they saw before.
//
// Each domain header opens and closes its OWN one-byte packing region. This
// file must NOT wrap them in one -- see rule 1 above, and
// refactor/J14_packet_structs_split.md.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "common/packets/common.h"
#include "common/packets/spawn.h"
#include "common/packets/combat.h"
#include "common/packets/character.h"
#include "common/packets/zone.h"
#include "common/packets/world_object.h"
#include "common/packets/group.h"
#include "common/packets/merchant.h"
#include "common/packets/guild.h"
#include "common/packets/travel_misc.h"
#include "common/packets/gm.h"

#endif // EQT_PACKET_STRUCTS_H
