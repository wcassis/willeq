#pragma once

// B1148 (#468) -- the official client's corpse-fade list, ONE copy.
//
// Read from eqgame.exe's code (review/B1141/ghidra/out/eqgame.exe.c and
// eqgame.dis; refactor/B1148/README.md sec. 1):
//
//   OP_DeleteSpawn 0x4581b7 -> 0x4849b2: a spawn whose type byte (+0x244) is
//   above 1 (a corpse) and whose race is not 216 or 472 (0x47bfd0) gets
//   actor-gfx +0x19c = 1; the actor release 0x48f4a4 then puts the CActor on
//   the corpse-fade list (0x401351) instead of deleting it, stamping the
//   start (0x401100 -> 0x46d655, the ms clock). Every other spawn's actor is
//   deleted at the packet (0x439a46).
//
//   Per frame (0x445243 -> 0x4012bb -> 0x401127), t = now - start in ms:
//     t <= 1000          nothing                       (cmp 0x3e8, jbe)
//     t > 1300           alpha = clamp(1 - (t - 1300) * 0.0005, 0, 1) to the
//                        actor's vtbl +0x1b0 (EQGraphicsDX9 0x1003d880: the
//                        actor's +0x48 and its model instance)
//                        -- constants 0x63f4d0 = 1.0, 0x63f4d4 = 0.0005,
//                        0x63f4cc = 0.0: linear, transparent from t = 3300
//     first frame past 1000 / 1150 / 1300
//                        effect 0x5e at SPELLPOINT_RHAND + LHAND / CHEST
//                        (else DEFAULT) / RFOOT + LFOOT, ground-projected
//                        (vtbl +0x1a4 = DLL 0x100375d0 -> particle manager
//                        +0x1c = ActorEmittersNew.edd entry 94
//                        "Death_Effect01", which names NO texture; see
//                        kCorpseDeathEffectEmitter)
//     t > 3499           0x401110 -> 0x439a46: the actor is deleted (0xdab <
//                        last walk stamp - start).
//
// fable applies the same schedule to the corpse's model in EntityRenderer
// (beginCorpseFade / the per-frame walk) and in tools/model_viewer's FX
// harness (--target-despawn on a dead target).

#include <cstdint>

namespace EQT {
namespace Graphics {

class EQAnimatedMeshSceneNode;

constexpr uint32_t kCorpseFadeQuietMs = 1000;    // 0x3e8: nothing before
constexpr uint32_t kCorpseFadeAlphaStartMs = 1300;  // 0x514
constexpr float kCorpseFadeAlphaPerMs = 0.0005f;    // 0x63f4d4
constexpr uint32_t kCorpseFadeDeleteAfterMs = 3499;  // 0xdab: deleted when t > this
// ActorEmittersNew.edd entry 0x5e (94), "Death_Effect01": texture field
// (+0x40) empty, run fixed 1 s (+0x78 = -1), L 1.5 s, burst 100, 35/s.
constexpr int kCorpseDeathEffectEmitter = 0x5e;

// The races whose corpse actor is deleted at the packet (0x47bfd0).
inline bool CorpseRaceSkipsFadeList(uint16_t race) { return race == 216 || race == 472; }

// True when OP_DeleteSpawn queues this spawn's actor on the fade list.
inline bool CorpseUsesFadeList(bool corpse, uint16_t race) {
    return corpse && !CorpseRaceSkipsFadeList(race);
}

// The alpha 0x401127 gives the model at `elapsedMs` after OP_DeleteSpawn
// (1.0 before 1300 ms: the walk does not touch it).
inline float CorpseFadeAlpha(uint32_t elapsedMs) {
    if (elapsedMs <= kCorpseFadeAlphaStartMs) return 1.0f;
    const float a = 1.0f - static_cast<float>(elapsedMs - kCorpseFadeAlphaStartMs) * kCorpseFadeAlphaPerMs;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// True on the first walk at which the actor is deleted.
inline bool CorpseFadeExpired(uint32_t elapsedMs) { return elapsedMs > kCorpseFadeDeleteAfterMs; }

// The material types a fading model draws with: each built material type
// maps to its alpha-BLENDED twin (ZoneShaderManager's fade materials, base
// EMT_TRANSPARENT_VERTEX_ALPHA); -1 = not available. A material of any other
// type falls back to the fixed-function EMT_TRANSPARENT_VERTEX_ALPHA.
struct CorpseFadeMaterials {
    int solid = -1, alphaTest = -1, alphaTestRGBA = -1;          // the entity's own types
    int fadeSolid = -1, fadeAlphaTest = -1, fadeAlphaTestRGBA = -1;  // their blended twins
};

// Draws `node` at `alpha`: every vertex of its per-instance mesh carries the
// alpha byte (the zone shaders and the built-in Solid3D multiply it into
// gl_FragColor.a) and each material switches to its blended twin on the first
// call. Returns false when the node is null.
bool ApplyCorpseFadeAlpha(EQAnimatedMeshSceneNode* node, float alpha, const CorpseFadeMaterials& mats);

}  // namespace Graphics
}  // namespace EQT
