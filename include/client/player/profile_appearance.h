// The player's own appearance, as carried by OP_PlayerProfile.
//
// T167. ZoneProcessPlayerProfile (EverQuest, eq_zone_entry.cpp since J02)
// parsed all seven of these fields into locals and then read none of them
// -- the implemented-but-
// unreached pattern, and the FIRST instance of it found on a RECEIVE path
// rather than a send path. The consequence was user-visible: /appearance
// (T153) had no source for eyecolor1/eyecolor2 at all, so every appearance
// change silently reset both eyes to 0, because eqemu's Handle_OP_FaceChange
// copies all seven fields into the PlayerProfile and Save()s unconditionally
// (zone/client_packet.cpp:6387) -- a field you do not re-send is a field you
// have set to zero.
//
// Parsing lives here as a free function, not as more of
// ZoneProcessPlayerProfile, for two reasons: its TU was at its accepted size
// ceiling at the time, and a private method cannot be tested. The offsets are the part
// that can silently rot, so they are what the tests pin.

#pragma once

#include <cstddef>
#include <cstdint>

namespace EQ {
namespace Net {
class Packet;
}
}

namespace EQT {

// The seven fields of Titanium's FaceChange_Struct, which are also seven
// fields of its PlayerProfile_Struct. `valid` is false on a
// default-constructed instance and true only after a profile has actually
// been parsed -- callers MUST check it before treating the zeros as the
// player's real appearance. Zero is a legitimate value for every field, so
// there is no in-band sentinel.
struct ProfileAppearance {
    uint8_t haircolor = 0;
    uint8_t beardcolor = 0;
    uint8_t eyecolor1 = 0;
    uint8_t eyecolor2 = 0;
    uint8_t hairstyle = 0;
    uint8_t beard = 0;
    uint8_t face = 0;
    bool valid = false;
};

// Offsets into PlayerProfile_Struct, verified field-by-field against eqemu's
// common/patches/titanium_structs.h (the annotated `/*00172*/` column, struct
// at :814) -- NOT inferred from our own older comments, which is how a wrong
// offset would survive a rewrite. haircolor..beard are six consecutive bytes
// after abilitySlotRefresh; face sits far later, immediately after WIS.
inline constexpr size_t PP_OFFSET_HAIRCOLOR  = 172;
inline constexpr size_t PP_OFFSET_BEARDCOLOR = 173;
inline constexpr size_t PP_OFFSET_EYECOLOR1  = 174;
inline constexpr size_t PP_OFFSET_EYECOLOR2  = 175;
inline constexpr size_t PP_OFFSET_HAIRSTYLE  = 176;
inline constexpr size_t PP_OFFSET_BEARD      = 177;
inline constexpr size_t PP_OFFSET_FACE       = 2264;

// Reads the seven appearance fields out of a PlayerProfile packet body.
// `base` is the offset of the struct within the packet (2 in the live path --
// the opcode precedes it). Returns a ProfileAppearance with valid=true; the
// caller is responsible for having length-checked the packet first, exactly
// as ZoneProcessPlayerProfile does before any other field read.
ProfileAppearance ParseProfileAppearance(const EQ::Net::Packet& p, size_t base);

}  // namespace EQT
