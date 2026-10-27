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
//   * The actor's scale (eqgame actor +0xC4) is the one rule of
//     include/client/graphics/actor_scale.h, EQT::Graphics::clientActorScale,
//     shared with B1172 (#491, the model's drawn scale). Every branch of it is
//     cited there and re-read in refactor/B1164/README.md sec. 10.2:
//       - size: the spawn's size (+0x23c) when > 0, else 0x47a147's race /
//         gender default, stored back (0x47fb07..0x47fb30);
//       - 0x47a0e9 (called at 0x47fb82, stored at +0xC4 by 0x47fb87):
//         0.2 x size, x 0.7692308 while the NewZone flag at 0x923c30 is set;
//         races 0x193 and >= 0x195 (0x47937c): size / 6 x 1.3, or size / 6
//         while 0x923c30 or the byte 0x923798 is set;
//       - 0x47a239 (called at 0x47fbff with the scale slot preset to -1.0 at
//         0x47fbbe): races 0x31, 0x49, 0x72, 0x8d..0x8f, 0x9e, 0xc4, 0xfc,
//         0x194 -> 1.0, 0x48 -> 2.0, 0x6c -> 0.05; written over +0xC4 at
//         0x47fc41 when it is not -1.0. Every other race keeps 0x47a0e9's.
// The lane-5 read (gdb on 0x10060b27, refactor/B1164/scripts/b1164_s_gdb.py)
// gave s = 1.200000 on the Target slots of record 80 cast on a race-1
// `#spawn` of size 6 (row 0 = -0.459220, 1.108655, 0; 0x1005c4f0 = 1.000000),
// 1.000000 on the Target slots of the same cast on a race-6 `#spawn` of size 5
// (DarkF), and 1.000000 / 0.999755 on the Source slots of a race-6 caster of
// size 0 (race default 5.0) -- 0.2 x 6, 0.2 x 5, 0.2 x 5.
//
// The root track's |row 0| factor is read for HUM and DAF (1.000000 both,
// refactor/B1164/scripts/b1164_root_track.py); SpellFxAttachScale() is the
// actor scale, i.e. it takes that factor as 1 for every model.

#include "client/graphics/actor_scale.h"

#include <cstdint>

namespace EQ {

// The attach scale s of a spell emitter on an actor of `race` / `gender`
// whose spawn carries `serverSize` (Spawn_Struct size; <= 0 = none): the
// actor's scale, EQT::Graphics::clientActorScale(). The two flags are the
// client's globals 0x923c30 (NewZone_Struct offset 592) and 0x923798; both
// read 0 on lane 5 (s = 0.2 x 6 exactly), which is the default here.
inline float SpellFxAttachScale(uint16_t race, uint8_t gender, float serverSize,
                                bool zoneScaleFlag = false, bool flag923798 = false) {
    return EQT::Graphics::clientActorScale(race, gender, serverSize, zoneScaleFlag, flag923798);
}

}  // namespace EQ
