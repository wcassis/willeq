#pragma once

// B1172 (#491): the official client's actor scale -- the one uniform scale a
// spawn's model is drawn at -- from race, gender and the spawn's size word.
// Traced in refactor/B1172/README.md sec. 1 (eqgame.exe, the P1999 Titanium
// build decompiled in review/B1141/ghidra/out/). There is no per-race art
// scale in the client: the model's own mesh times this number is what the
// renderer draws.
//
//   size  = Spawn_Struct.size (offset 75, copied to EQPlayer+0x23c at
//           0x4816fd) or the AT_Size appearance value (0x4524e9 ->
//           0x481d66); when it is not > 0 the client substitutes the
//           race/gender default below (0x47fb07..0x47fb30).
//   scale = size * 0.2 for every race except 403 and >= 405, which use
//           size / 6 * 1.3 (0x47a0e9; the two flags below select the
//           variants); then a short list of races forces its own scale
//           (0x47a239 -> 0x47fc41).
//   The result is EQPlayer->actor+0xC4, handed to the graphics actor's
//   vtable slot 0x50 (0x43d3e8 at build, 0x481d8b on a size change), which
//   stores it as the scene node's uniform scale (EQGraphicsDX9 0x10036bbc ->
//   0x101901f7).

#include <cstdint>

namespace EQT::Graphics {

// FUN_0047a147 (0x47a147): the size a spawn is drawn at when its size word
// is not > 0. Jump table 0x47a209 for races 1..12, compares above 24.
// Gender 2 is the neuter gender (races 42 and 120 only).
constexpr float clientDefaultActorSize(uint16_t race, uint8_t gender) {
    switch (race) {
        case 2:  return 7.0f;   // Barbarian
        case 4:  return 5.0f;   // Wood Elf
        case 6:  return 5.0f;   // Dark Elf
        case 7:  return 5.5f;   // Half Elf
        case 8:  return 4.0f;   // Dwarf
        case 9:  return 8.0f;   // Troll
        case 10: return 9.0f;   // Ogre
        case 11: return 3.5f;   // Halfling
        case 12: return 3.0f;   // Gnome
        case 42: return gender == 2 ? 3.0f : 6.0f;
        case 43: return 4.7f;
        case 77: return 5.0f;
        case 81: return 3.5f;
        case 88: return 3.0f;
        case 90: return 7.0f;
        case 92: return 8.0f;
        case 93: return 9.0f;
        case 94: return 4.0f;
        case 112: return 5.0f;
        case 120: return gender == 2 ? 3.0f : 6.0f;
        case 130: return 7.0f;
        case 330: return 5.0f;
        default: return 6.0f;  // 1 Human, 3 Erudite, 5 High Elf and every other race
    }
}

// FUN_0047937c (0x47937c): the races drawn on the size/6 scale.
constexpr bool clientRaceUsesSixthScale(uint16_t race) {
    return race == 403 || race >= 405;
}

// FUN_0047a0e9 (0x47a0e9): size -> scale. `zoneScaleFlag` is the dword the
// client copies from NewZone_Struct offset 592 (the OP_NewZone memcpy of 700
// bytes to 0x9239e0 at 0x45c006; 0x923c30 = +0x250); it reads 0 in every
// oracle capture decoded for B1172 (r51, B1118 oracle_druid). `flag923798`
// is the byte at 0x923798 (direct stores: 0x4937a6 writes 0, 0x4a4d3b writes
// bl in a reset run); it matters for the size/6 races only.
constexpr float clientSizeToActorScale(uint16_t race, float size, bool zoneScaleFlag = false,
                                       bool flag923798 = false) {
    if (!clientRaceUsesSixthScale(race)) {
        return zoneScaleFlag ? size * 0.2f * 0.7692308f : size * 0.2f;  // 0x640fec, 0x647cd0
    }
    return (!zoneScaleFlag && !flag923798) ? size * (1.0f / 6.0f) * 1.3f  // 0x647cd4, 0x642d50
                                           : size * (1.0f / 6.0f);
}

// FUN_0047a239 (0x47a239), its scale output only: the races whose scale is
// forced whatever their size (written back over actor+0xC4 at 0x47fc41 when
// it is not -1). Returns false for every other race.
constexpr bool clientRaceForcedActorScale(uint16_t race, float& scale) {
    switch (race) {
        case 72: scale = 2.0f; return true;    // 0x47a29b
        case 108: scale = 0.05f; return true;  // 0x47a2d7
        case 49: case 73: case 114: case 141: case 142: case 143:
        case 158: case 196: case 252: case 404:
            scale = 1.0f; return true;         // 0x47a371
        default: return false;
    }
}

// FUN_0047fa9e (0x47fa9e): the scale the client draws a spawn at.
constexpr float clientActorScale(uint16_t race, uint8_t gender, float size, bool zoneScaleFlag = false,
                                 bool flag923798 = false) {
    const float s = (size > 0.0f) ? size : clientDefaultActorSize(race, gender);
    float forced = 0.0f;
    if (clientRaceForcedActorScale(race, forced)) return forced;
    return clientSizeToActorScale(race, s, zoneScaleFlag, flag923798);
}

}  // namespace EQT::Graphics
