#pragma once
//
// zone_bench_draw_util.h — T387: the one file-static helper that survived the
// zone_bench_loader.cpp split with callers on BOTH sides of a seam.
//
// textureGroupId() was `static` in the single 3,280-line TU; the split put
// its callers in zone_bench_draw.cpp (zone/object/entity draw collection) and
// zone_bench_entities.cpp (renderBatchedEntities). Rather than duplicate it
// (the comment below is explicit that a second copy already has to be kept in
// sync with production), it moves here verbatim as `inline`. Internal to
// tools/hwcaps — not part of zone_bench_loader.h's public surface.

#include <cstdint>
#include <irrlicht.h>

namespace ZoneBench {

// H02b: address-independent 16-bit texture grouping ID for materialKey.
// The old key was `(uintptr_t)texture & 0xFFFF` -- the low bits of a heap
// POINTER. That made the material-sorted draw order depend on allocation
// addresses, so ASLR (or merely a different environment-block size shifting
// the layout) reordered equal-material draws between otherwise identical
// runs -> z-fighting pixel flips on coplanar geometry and a wobbling
// stateChanges count. This silently broke the golden-frame CI gate's
// exact-match bar (1-33 differing pixels between identical invocations,
// only reproducible across processes). An FNV-1a hash of the texture NAME
// is just as good a grouping key and is stable across runs.
// Q02 applied this same fix to production (zone_lifecycle/renderer_zone_draw.cpp has a
// byte-identical copy of this helper) -- keep the two copies in sync.
inline uint32_t textureGroupId(const irr::video::SMaterial& mat) {
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}


} // namespace ZoneBench
