#pragma once
// P09 Phase B: cross-region same-material zone draw merge prototype.
// Flag-gated behind --merge-zone-draws (default OFF everywhere).
//
// Chosen design (see refactor/P09_cross_region_merge.md Review, A-gate):
// candidate architecture #3, client-side-index variant. A persistent
// per-texture CPU vertex array is built once at zone load (world-space
// baked, so no per-region transform is needed at draw time); at draw time,
// runs of consecutive entries in the ALREADY-SORTED (materialKey,
// distanceSq) draw-entry list that share the same materialKey (which
// already encodes texture identity + effective MaterialType, i.e. the
// per-region adaptive-lighting tier decision -- see zone_bench_loader.cpp's
// collectDrawEntries()) are coalesced: their precomputed index ranges are
// gathered into a small per-frame scratch index array and submitted as ONE
// glDrawElements via Irrlicht's existing client-side-vertex-array fallback
// path (COpenGLES2Driver::drawMeshBuffer -- the same code path already used
// for entities/doors/UI, see docker/irrlicht-drm/COpenGLES2Driver.cpp). No
// raw GL calls; reuses the production driver's attribute/shader/material
// binding unchanged, so pixel output is expected to be identical to the
// unmerged path (same triangles, same vertex bytes, same shader/material).
//
// New TU (zone_bench_loader.cpp is at/near the 3000-line FAIL cap; see
// zone_bench_histogram.cpp for the precedent of relocating new machinery).

#include <cstdint>
#include <vector>

namespace irr { namespace video { class IVideoDriver; } }

namespace ZoneBench {
struct ZoneBenchData;
struct DrawEntry;
}

namespace ZoneBench {

// Builds the persistent per-texture merged vertex buffers + template index
// ranges from data.regions. Must be called once after zone geometry is
// loaded/built (mesh buffers populated) and before any merged draw. World
// transform is baked into vertex positions at this point (regions are
// static, translation-only world matrices -- see RegionData::position),
// so merged draws render with an identity world transform.
//
// Zone-wide scope (no per-cell/eviction grouping): the benchmark harness
// keeps all regions resident for the process lifetime (no streaming), so
// there is no eviction unit to respect here -- see the Review's Phase D
// landing sketch for how production's per-region ConstrainedMeshCache
// eviction reconciles with this (cell-scoped buckets, not whole-zone).
//
// Regions whose mesh buffers are not EVT_STANDARD (S3DVertex) are left out
// of the merge buckets and always draw via the normal per-region path
// (matches the vertex format buildTexturedMesh/buildColoredMesh actually
// produce for zone geometry; a defensive fallback, not expected to trigger
// on real zone data).
void buildMergedZoneBuffers(ZoneBenchData& data);

// Releases the merged buffers (call once at shutdown, after the last
// merged draw of the run -- mirrors ZoneBenchData's own cleanup pattern).
void destroyMergedZoneBuffers();

// Draws an already-collected, already-material-sorted list of DrawEntry
// (as produced by collectDrawEntries(materialSort=true, ...)) using the
// merge-aware path: consecutive runs sharing the same materialKey are
// coalesced into one glDrawElements each (via the client-side-index
// gather); singleton runs draw exactly as the unmerged path does. Returns
// the number of actual glDrawElements-equivalent draw calls issued (for
// the CSV draws column / per-pose reduction reporting).
int renderMergedDrawEntries(ZoneBenchData& data,
                             irr::video::IVideoDriver* driver,
                             const std::vector<DrawEntry>& sortedEntries);

// P09c Phase C: the "fair baked-vs-baked" comparison mode. Draws the SAME
// baked world-space vertex data buildMergedZoneBuffers() already built (one
// persistent per-texture buffer per bucket/generation), but submitted as
// INDIVIDUAL per-region-buffer draws (one drawMeshBuffer call per DrawEntry,
// identity world transform, same per-entry order as the unmerged path) --
// i.e. everything renderMergedDrawEntries() does EXCEPT the coalescing.
// Isolates whether a merged-vs-unmerged pixel diff is caused by the merge
// mechanism itself (coalescing/run-splitting) or by the baked-position
// architecture generally (which this mode shares with the merged path):
//   - merged vs baked-unmerged diff => a real merge-only bug (loop back to
//     the merge fix).
//   - baked-unmerged vs production-path (per-region-matrix) unmerged diff =>
//     the pure floating-point transform-order delta, architecturally
//     inherent to baking world-space positions once at load time instead of
//     applying a per-region world matrix at draw time (mathematically
//     equivalent, not bit-identical in float32).
// Requires buildMergedZoneBuffers() to have already been called (same
// precondition as renderMergedDrawEntries). Returns the draw call count
// (always sortedEntries.size(), since nothing is coalesced here -- kept as
// a return value for call-site symmetry with renderMergedDrawEntries).
int renderBakedUnmergedDrawEntries(ZoneBenchData& data,
                                    irr::video::IVideoDriver* driver,
                                    const std::vector<DrawEntry>& sortedEntries);

} // namespace ZoneBench
