// B976 (#294, Will 2026-10-14 ruled option 1): when two zone regions tie in
// depth at a pixel, the TOP-FACING (+Y in Irrlicht space) region wins.
//
// The fix is a per-vertex clip-space depth offset in the zone region vertex
// shaders, gated by a per-material flag (SMaterial::MaterialTypeParam2),
// so the screen-space mapping of every triangle is bit-exact and only the
// stored depth moves. The pure decision (selector + shift + flag) lives in
// include/client/graphics/zone_depth_tie_break.h and is unit-tested here
// without a GPU. The GLSL splice and the material plumbing live in
// src/client/graphics/zone_shader_sources_gles2.h, zone_shader_callbacks.h
// and zone_geometry.cpp; the hygiene half of this test re-scans the shader
// source to pin the splice's presence and absence (the same discipline as
// test_baked_vertex_lighting.cpp's "six programs carry the baked uniforms").

#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

#include "client/graphics/zone_depth_tie_break.h"

namespace DT = EQT::Graphics::DepthTie;

namespace {

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
    return n;
}

std::string readShaderSources() {
    std::ifstream f(B976_ZONE_SHADER_SOURCES);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// The pure decision.
// ---------------------------------------------------------------------------

// The selector picks the DOWN-FACING layer (worldN.y <= -0.5) and nothing
// else. The GLSL is step(0.5, -worldN.y): -worldN.y >= 0.5 <=> worldN.y <= -0.5.
// A top-facing floor (worldN.y = +1) must NOT be selected; a bottom-facing
// floor (worldN.y = -1) MUST be; a near-horizontal surface (worldN.y = 0)
// must not (it is neither the top nor the bottom of a tie).
TEST(ZoneDepthTieBreak, DownFacingSelectorPicksTheMinusYLayerOnly) {
    // Down-facing: selected.
    EXPECT_FLOAT_EQ(DT::downFacingSelector(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(DT::downFacingSelector(-0.5f), 1.0f);   // exactly the threshold
    // Up-facing / horizontal: not selected.
    EXPECT_FLOAT_EQ(DT::downFacingSelector(+1.0f), 0.0f);
    EXPECT_FLOAT_EQ(DT::downFacingSelector(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(DT::downFacingSelector(+0.5f), 0.0f);
    // The mirror-image GLSL expression, evaluated in C++, must agree.
    auto glslStep = [](float x) { return x >= 0.5f ? 1.0f : 0.0f; };
    for (float ny : {-1.0f, -0.7f, -0.5f, -0.3f, 0.0f, 0.3f, 0.5f, 0.7f, 1.0f}) {
        EXPECT_FLOAT_EQ(DT::downFacingSelector(ny), glslStep(-ny))
            << "C++ selector and GLSL step(0.5,-worldN.y) disagree at worldN.y=" << ny;
    }
}

// THE TIE-BREAK PROPER. The measured defect: a +Y (top) region over a -Y
// (bottom) region, their stored depths within ~1 D16 cell of each other. The
// bias pushes ONLY the down-facing layer's stored depth by exactly B (the
// up-facing layer is untouched), so the top layer is strictly nearer and wins
// the GL_LEQUAL test -- in every depth format.
TEST(ZoneDepthTieBreak, TopFacingRegionWinsTheDepthTie) {
    const float B = DT::kNdcDepthTieBias;
    // Two near-coplanar floor layers: top (+Y) over bottom (-Y).
    // Their un-biased stored depths differ by less than one D16 cell.
    const float baseTop = 0.5f;                       // top layer (+Y)
    const float baseBottom = 0.5f - DT::kD16Cell * 0.6f;  // bottom (-Y), slightly nearer
    const float biasTop = DT::uDepthTieBiasForFlag(1.0f);   // flagged region
    EXPECT_FLOAT_EQ(biasTop, B);

    const float dTop = DT::biasedStoredDepth(baseTop, +1.0f, biasTop);
    const float dBottom = DT::biasedStoredDepth(baseBottom, -1.0f, biasTop);

    // Top is untouched; bottom is pushed +B (farther).
    EXPECT_FLOAT_EQ(dTop, baseTop);
    EXPECT_FLOAT_EQ(dBottom, baseBottom + B);
    // Stored depth: LARGER = farther; GL_LEQUAL (nearer wins) => smaller wins.
    // Before the bias the bottom (nearer) would win; after it the top wins.
    EXPECT_GT(dBottom, dTop);
    EXPECT_LT(dTop, dBottom) << "the top-facing region must strictly win the tie";
    // And it wins by the bias margin, not by a hair's breadth.
    EXPECT_GE(dBottom - dTop, B - (baseTop - baseBottom));
}

// The bias must NOT reorder genuinely distinct surfaces. Two down-facing
// surfaces more than B apart keep their relative order (both shift by +B).
// And a -Y surface that is genuinely behind a +Y surface stays behind.
TEST(ZoneDepthTieBreak, DoesNotReorderDistinctSurfaces) {
    const float B = DT::kNdcDepthTieBias;
    const float bias = DT::uDepthTieBiasForFlag(1.0f);
    // Two down-facing surfaces, one 10 D16 cells behind the other.
    const float a = 0.4f, b = 0.4f + DT::kD16Cell * 10.0f;
    EXPECT_LT(a, b);
    EXPECT_LT(DT::biasedStoredDepth(a, -1.0f, bias), DT::biasedStoredDepth(b, -1.0f, bias));
    // Their separation is preserved exactly (both +B).
    EXPECT_FLOAT_EQ(b - a,
        DT::biasedStoredDepth(b, -1.0f, bias) - DT::biasedStoredDepth(a, -1.0f, bias));
    // A genuinely-farther +Y surface is not yanked behind a -Y surface.
    const float nearBottom = 0.5f;                       // -Y, +B
    const float farTop = 0.5f + B + DT::kD16Cell * 5.0f; // +Y, untouched, clearly farther
    EXPECT_GT(DT::biasedStoredDepth(farTop, +1.0f, bias),
              DT::biasedStoredDepth(nearBottom, -1.0f, bias));
}

// The material gate: only flagged zone-region buffers take the bias;
// entities/doors/trees (flag 0) and non-zone shared programs get 0.
TEST(ZoneDepthTieBreak, MaterialGateGatesTheBias) {
    // The builder stamps exactly 1.0 on region buffers -> full bias.
    EXPECT_FLOAT_EQ(DT::uDepthTieBiasForFlag(1.0f), DT::kNdcDepthTieBias);
    // Non-zone geometry (Param2 = 0) gets 0, on every normal.
    EXPECT_FLOAT_EQ(DT::uDepthTieBiasForFlag(0.0f), 0.0f);
    // A down-facing vertex on an unflagged buffer is not shifted at all.
    EXPECT_FLOAT_EQ(DT::ndcDepthTieShift(-1.0f, DT::uDepthTieBiasForFlag(0.0f)), 0.0f);
    // The flag is a > 0.5 threshold, so a half-stamped value still gates it off
    // on one side and on on the other.
    EXPECT_FLOAT_EQ(DT::uDepthTieBiasForFlag(0.5f), 0.0f);
    EXPECT_FLOAT_EQ(DT::uDepthTieBiasForFlag(0.5f + 1e-6f), DT::kNdcDepthTieBias);
}

// The bias value is exactly 2 D16 cells: the smallest that breaks the
// measured within-~1-cell tie with a 1-cell margin over the worst-case
// per-row ripple, and too small to reorder surfaces a few cm apart.
TEST(ZoneDepthTieBreak, BiasIsTwoD16Cells) {
    EXPECT_FLOAT_EQ(DT::kNdcDepthTieBias, 2.0f * DT::kD16Cell);
    EXPECT_FLOAT_EQ(DT::kD16Cell, 1.0f / 32768.0f);
}

// ---------------------------------------------------------------------------
// The GLSL splice hygiene (the source-scan half).
// ---------------------------------------------------------------------------

// The one biased line must be present EXACTLY ONCE in each of the two region
// vertex programs and NOWHERE else. Wind and atlas programs share the lighting
// equation but must NOT carry the tie-break: trees and placeable objects use
// those programs and are never flagged, and the tree-bbox MaxEdge.Y (Param2)
// must not be misread as the zone flag.
TEST(ZoneDepthTieBreakGlsl, RegionVertexProgramsCarryTheTieBreakAndNothingElseDoes) {
    const std::string src = readShaderSources();
    ASSERT_FALSE(src.empty()) << "could not read " << B976_ZONE_SHADER_SOURCES;

    // The uniform is declared in exactly the two region programs.
    EXPECT_EQ(countOf(src, "uniform float uDepthTieBias;"), 2u)
        << "uDepthTieBias must be declared in VERTEX_SHADER_SRC and "
           "VERTEX_SHADER_LIGHTWEIGHT_SRC only";
    // The splice macro is expanded exactly once per region program. The bias
    // statement itself is a C++ macro (EQT_DEPTH_TIE_VS_LINES) defined in
    // include/client/graphics/zone_depth_tie_break.h, so it is not spelled
    // out in this file -- that exact text is pinned by the header test below.
    EXPECT_EQ(countOf(src, "EQT_DEPTH_TIE_VS_LINES"), 2u);
    // The wrong direction (adding to gl_Position.w moves the vertex NEARER,
    // z/w shrinks) and the wrong selector (step(0.5, worldN.y) is UP-facing)
    // must be ABSENT from every program.
    EXPECT_EQ(countOf(src, "gl_Position.w += uDepthTieBias"), 0u);
    EXPECT_EQ(countOf(src, "step(0.5, worldN.y)"), 0u);

    // Per-program: exactly VERTEX_SHADER_SRC and VERTEX_SHADER_LIGHTWEIGHT_SRC
    // carry the uniform; atlas and wind programs do not.
    auto programCount = [&](const std::string& name) -> size_t {
        const std::string marker = "static const char* " + name + " = R\"(";
        const size_t start = src.find(marker);
        if (start == std::string::npos) return 0;
        size_t end = src.find("static const char*", start + marker.size());
        if (end == std::string::npos) end = src.size();
        return countOf(src.substr(start, end - start), "uniform float uDepthTieBias;");
    };
    EXPECT_EQ(programCount("VERTEX_SHADER_SRC"), 1u);
    EXPECT_EQ(programCount("VERTEX_SHADER_LIGHTWEIGHT_SRC"), 1u);
    EXPECT_EQ(programCount("ATLAS_VERTEX_SHADER_SRC"), 0u);
    EXPECT_EQ(programCount("ATLAS_VERTEX_SHADER_LIGHTWEIGHT_SRC"), 0u);
    EXPECT_EQ(programCount("WIND_VERTEX_SHADER_SRC"), 0u);
    EXPECT_EQ(programCount("WIND_VERTEX_SHADER_LIGHTWEIGHT_SRC"), 0u);
}

// The GLSL macro body (in the header, not the shader file) must be the
// +z / -Y form. This is the regression that pins the two design errors
// corrected from the first draft: the offset is on gl_Position.z (farther),
// and the selector is step(0.5, -worldN.y) (down-facing), not
// step(0.5, worldN.y) (up-facing).
TEST(ZoneDepthTieBreakGlsl, TheMacroIsPlusZFartherOnTheDownFacingLayer) {
    std::ifstream f(B976_DEPTH_TIE_HEADER);
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string h = ss.str();
    ASSERT_FALSE(h.empty()) << "could not read " << B976_DEPTH_TIE_HEADER;
    EXPECT_NE(h.find("gl_Position.z += uDepthTieBias * step(0.5, -worldN.y) * gl_Position.w;"),
              std::string::npos) << "the bias must add to gl_Position.z";
    EXPECT_EQ(h.find("gl_Position.w += uDepthTieBias"), std::string::npos)
        << "adding to gl_Position.w moves the vertex nearer (z/w shrinks)";
    // The selector must be the down-facing form everywhere it appears.
    EXPECT_EQ(countOf(h, "step(0.5, worldN.y)"), 0u);
    EXPECT_GE(countOf(h, "step(0.5, -worldN.y)"), 1u);
}
