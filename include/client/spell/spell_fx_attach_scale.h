#pragma once

// B1164 (#483): the attach scale s the official client multiplies an attached
// spellsnew.edd emitter by (+0x6C: sizes and velocities; +0x19C: shape and
// offsets), pinned at runtime on lane 5 and derived from the client's code.
//
// What the client does (refactor/B1164/README.md sec. 10):
//   * EQGraphicsDX9 CParticleSystem::Render (0x100605b0) takes s from whatever
//     the emitter instance is attached to. A spellsnew.eff slot on attach
//     point 1 (SPELLPOINT_DEFAULT) of an old-world model is TRACK-attached
//     (+0x70): s = 0x1005c4f0(track) x |row 0| of the track's world matrix
//     (0x10060ae8..0x10060b23). The DEFAULT point is the model's root track
//     (0x10046830 binds SPELLPOINT_DEFAULT to the model tag with no suffix,
//     e.g. HUM -> HUM_TRACKDEF, whose one frame is the identity rotation,
//     |q| = 16384/16384), so the row's length is the ACTOR's scale.
//   * The actor's scale is eqgame 0x47a0e9's return (called by 0x47fa9e at
//     0x47fb82, stored at actor +0xC4 by 0x47fb87): 0.2 x the spawn's size,
//     the size being the race default of 0x47a147 when the spawn carries
//     none (size <= 0).
// The lane-5 read (gdb on 0x10060b27, refactor/B1164/scripts/b1164_s_gdb.py)
// gave s = 1.200000 on the Target slots of record 80 cast on a race-1
// `#spawn` of size 6 (row 0 = -0.459220, 1.108655, 0; 0x1005c4f0 = 1.000000)
// and s = 1.000000 / 0.999755 on the Source slots of a race-6 caster of size 0
// (race default 5.0) -- 0.2 x 6 and 0.2 x 5.
//
// Scope: races 0..0x19. For those 0x47a239 (the per-race override that may
// rewrite the actor scale) returns without writing, and 0x47937c is false, so
// 0.2 x size is the whole rule. Other races keep the previous constant 1.0
// (kSpellFxAttachScale): their override is not traced here.

#include <cstdint>

namespace EQ {

// eqgame 0x47a147: the size an actor of `race` takes when the server sends 0,
// for the races this header covers (< 0x1a; `gender` matters only above it).
inline float SpellFxRaceDefaultSize(uint16_t race) {
    switch (race) {
        case 2:  return 7.0f;   // _DAT_00642658
        case 4:
        case 6:  return 5.0f;   // _DAT_00640e58
        case 7:  return 5.5f;   // _DAT_0064263c
        case 8:  return 4.0f;   // _DAT_00642650
        case 9:  return 8.0f;   // _DAT_00641fa0
        case 10: return 9.0f;   // _DAT_006410cc
        case 11: return 3.5f;   // _DAT_00642d68
        case 12: return 3.0f;   // _DAT_00640e24
        default: return 6.0f;   // _DAT_0064264c
    }
}

// True when SpellFxAttachScale() applies the client's rule to `race`.
inline bool SpellFxAttachScaleTraced(uint16_t race) { return race < 0x1a; }

// The attach scale s of a spell emitter on an actor of `race` whose spawn
// carries `serverSize` (Spawn_Struct size; <= 0 = none): the actor's scale,
// eqgame 0x47a0e9 = 0.2 (_DAT_00640fec) x the size. 1.0 for an untraced race.
inline float SpellFxAttachScale(uint16_t race, float serverSize) {
    if (!SpellFxAttachScaleTraced(race)) return 1.0f;
    const float size = serverSize > 0.0f ? serverSize : SpellFxRaceDefaultSize(race);
    return 0.2f * size;
}

}  // namespace EQ
