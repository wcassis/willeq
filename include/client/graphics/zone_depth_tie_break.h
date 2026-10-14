#pragma once
// B976 (#294, Will 2026-10-14 ruled option 1): when two zone regions tie in
// depth at a pixel, the TOP-FACING (+Y in Irrlicht space) region wins.
//
// The measured root cause (refactor/B976/README.md): at the timorous f2 gate
// pose the flat bottom plane is TWO real, overlapping, near-coplanar floor
// layers -- a +Y `sand3` region over a -Y `w1` underlayer. Both rasterize over
// the pixel; the depth test's winner depends on the surface depth format AND
// the driver's compare (x86 D24S8 -> +Y, x86 D16 -> per-row z-fight, Pi Lima
// depth=24 -> -Y). No normal is wrong. The fix is a deterministic tie-break:
// push the DOWN-FACING (-Y) layer's stored depth a little farther, so a top
// layer is strictly nearer in every format and the +Y region always wins.
//
// THE OFFSET. It lives in the VERTEX shader (FP32):
//   gl_Position.z += uDepthTieBias * step(0.5, -worldN.y) * gl_Position.w;
// i.e. add B*clip-w to the clip-z of every down-facing (worldN.y <= -0.5)
// vertex, so its stored NDC-z (clip-z / clip-w) moves by exactly +B. Two
// properties make one constant work on every surface and never move a triangle
// on screen:
//   * the offset is B in clip-z, which is B in NDC-z AT EVERY FRAGMENT: the
//     rasterizer stores the perspective-correct interpolation of (clip-z/clip-w),
//     and adding B to every vertex's (clip-z/clip-w) shifts the interpolated
//     depth by exactly B regardless of w. x/w and y/w are untouched, so the
//     triangle's screen-space footprint is bit-exact -- only its stored depth
//     moves. It is a clean NDC polygon offset, not a world-space nudge (which
//     would decay into sub-quantum at range on D16).
//   * LARGER stored depth = farther, and the test is GL_LEQUAL (nearer wins),
//     so pushing the -Y layer +B makes it strictly behind the +Y layer.
//     step(0.5, -worldN.y) is 1 only for the down-facing layer (worldN.y <= -0.5)
//     and 0 for up-facing / near-horizontal, so the +Y layer is untouched and a
//     -Y layer over a +Y layer separates by exactly B.
//
//   NOTE the earlier draft's two errors, corrected here:
//     (1) adding B to gl_Position.w makes z/w SMALLER -> the biased vertex
//         moves NEARER, the wrong direction. The offset is on gl_Position.z.
//     (2) the first draft selected the UP-facing layer (its selector had no
//         minus sign). We need the DOWN-facing selector, step(0.5, -worldN.y).
//
// THE VALUE. B = 2 D16 cells (a D16 cell is 2^-15 ~ 3.05e-5). It is the
// SMALLEST value that breaks the measured D16 tie: the README's per-row z-fight
// proves the two layers' rasterized depths sit within ~1 D16 cell, and per-row
// quantization ripples each by at most half a cell, so a 2-cell push leaves the
// +Y layer strictly nearer no matter how the ripple lands (2-cell bias beats a
// 1-cell-worst-case tie by a 1-cell margin). It cannot reorder genuinely
// distinct surfaces: a -Y surface and a +Y surface more than B apart in stored
// depth (a few cm at the tie plane) keep their order, and two down-facing
// surfaces both shift by the same B, so their relative order is preserved.
//
// THE GATE. Per-MATERIAL, via SMaterial::MaterialTypeParam2. That field is free
// on the region programs: MaterialTypeParam already carries the baked-light flag
// (T408), and Param2's only other use is the tree-bbox MaxEdge.Y written on
// WIND-material buffers -- trees never use the region programs. The builder
// stamps Param2 = 1.0 on zone-REGION buffers only (zone_geometry.cpp, plus the
// benchmark's region builder); ShaderCallback uploads uDepthTieBias from it.
// Entities, doors and trees carry Param2 = 0 on these programs, so they are
// never biased. The draw list, sorted order, state counts and poly counts of
// every zone are unchanged -- the acceptance gate (refactor/B976/README.md
// "Fix"). SMaterial::operator== walks MaterialTypeParam2, so the flag survives
// the pooled buffer merge.
//
// C++ side: the pure decision, unit-tested without a GPU
// (tests/test_zone_depth_tie_break.cpp). GLSL side: the one biased line
// (EQT_DEPTH_TIE_VS_LINES), spliced into the two region vertex programs
// (VERTEX_SHADER_SRC and VERTEX_SHADER_LIGHTWEIGHT_SRC) in
// src/client/graphics/zone_shader_sources_gles2.h, plus a hardcoded `uniform
// float uDepthTieBias;` in those same two programs.
#include <cmath>

namespace EQT {
namespace Graphics {
namespace DepthTie {

// EXACTLY 2 D16 cells: the smallest integer-cell value that breaks the
// measured tie (the two layers' rasterized depths sit within ~1 D16 cell)
// with a 1-cell margin over the worst-case per-row ripple, and too small to
// reorder surfaces a few cm apart. Defined as 2 * kD16Cell so the test can
// pin the relationship exactly (6.0e-5f is only 1.966 cells in float32).
constexpr float kNdcDepthTieBias = 2.0f * (1.0f / 32768.0f);
// A D16 cell in NDC-z: the buffer quantizes [0,1] to 2^15 cells.
constexpr float kD16Cell = 1.0f / 32768.0f;
// worldN.y at or below this is "down-facing" (the -Y layer that gets pushed).
// Matches the shader's step(0.5, -worldN.y).
constexpr float kTieSelectorY = -0.5f;

// The GLSL line that does the work, spliced into the region vertex programs
// right after each computes `vec3 worldN`. Needs `gl_Position` and `worldN` in
// scope. (The uniform `uDepthTieBias` is declared separately, in the same
// programs' uniform block.)
#define EQT_DEPTH_TIE_VS_LINES \
    "    // B976 (#294): zone depth tie-break -- the top-facing region wins a\n" \
    "    // depth tie. Down-facing (worldN.y <= -0.5) zone geometry is pushed\n" \
    "    // +bias in stored NDC-z (farther); see include/client/graphics/zone_depth_tie_break.h.\n" \
    "    gl_Position.z += uDepthTieBias * step(0.5, -worldN.y) * gl_Position.w;\n"

// ---------------------------------------------------------------------------
// The pure decision (unit-tested, no GPU).
// ---------------------------------------------------------------------------

// The GLSL step(0.5, -worldN.y): 1.0 when the source normal faces down
// (worldN.y <= kTieSelectorY), 0.0 otherwise. Region matrices are pure
// translations, so a vertex's worldN IS its source normal and a floor's -1.0
// never wavers close to 0 by interpolation.
inline float downFacingSelector(float worldNy) {
    return worldNy <= kTieSelectorY ? 1.0f : 0.0f;
}

// The per-fragment NDC-z shift (constant over a triangle, see the header):
// kNdcDepthTieBias for a down-facing vertex on a flagged buffer, 0 otherwise.
inline float ndcDepthTieShift(float worldNy, float uDepthTieBias) {
    return uDepthTieBias * downFacingSelector(worldNy);
}

// The stored NDC-z (the depth-buffer value; LARGER = farther, and the test is
// GL_LEQUAL so the smaller value wins). baseNdcZ is the un-biased vertex depth.
// Unit-tested as the tie-break's pure decision (tests/test_zone_depth_tie_break.cpp);
// the production counterpart is the GLSL line EQT_DEPTH_TIE_VS_LINES.
inline float biasedStoredDepth(float baseNdcZ, float worldNy, float uDepthTieBias) {
    return baseNdcZ + ndcDepthTieShift(worldNy, uDepthTieBias);
}

// The uDepthTieBias a shader callback uploads from the material flag the
// builder stamps on zone-region buffers (SMaterial::MaterialTypeParam2).
// 1.0 = zone geometry, take the tie-break; 0.0 = shared-program mesh
// (entities, doors, trees), never biased.
inline float uDepthTieBiasForFlag(float zoneGeometryFlag) {
    return zoneGeometryFlag > 0.5f ? kNdcDepthTieBias : 0.0f;
}

}  // namespace DepthTie
}  // namespace Graphics
}  // namespace EQT
