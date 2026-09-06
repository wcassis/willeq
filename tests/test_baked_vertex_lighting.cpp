// T408 — the zone's baked per-vertex lighting (board B110).
//
// Until T408 `WldLoader::parseFragment36` read `header.colorCount` and then
// stepped over the array under a `// Skip colors` comment, so every zone
// vertex reached the GPU as opaque white and EQ's static lamp pools did not
// exist in fable. T367 measured what was being thrown away by temporarily
// instrumenting that function against one qeynos2 load; this file re-takes
// that census through the PRODUCTION loader on every ctest run, so the array
// cannot be silently skipped again.
//
// The reference numbers are T367's (review/T367/README.md §5 and
// review/T367/logs/qeynos2_baked_vertex_colours.txt).

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cctype>

#include "wld_test_fixtures.h"

namespace {

struct BakedCensus {
    int regionMeshes = 0;            // R<n>_DMSPRITEDEF fragments
    int regionMeshesWithColors = 0;  // ... carrying colorCount == vertexCount
    long long vertices = 0;          // vertices in those meshes
    long long colors = 0;            // colours loaded for them
    double sumB = 0, sumG = 0, sumR = 0, sumA = 0;
    int minByte = 255, maxByte = 0;
};

bool isRegionMesh(const std::string& name) {
    if (name.size() < 3 || name[0] != 'R') return false;
    size_t i = 1;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i == 1) return false;
    return name.compare(i, std::string::npos, "_DMSPRITEDEF") == 0;
}

}  // namespace

class BakedVertexLightingTest : public WldIntegrationTest {
protected:
    // qeynos2 is the zone every night measurement in this project has used
    // (T358, T361, T367, T377, T386, T401, T405).
    std::string zonePath() const { return eqClientPath_ + "/qeynos2.s3d"; }

    BakedCensus censusQeynos2(WldLoader& loader) {
        BakedCensus c;
        for (const auto& geom : loader.getGeometries()) {
            if (!geom || !isRegionMesh(geom->name)) continue;
            c.regionMeshes++;
            if (geom->vertexColors.empty()) continue;
            c.regionMeshesWithColors++;
            c.vertices += static_cast<long long>(geom->vertices.size());
            c.colors += static_cast<long long>(geom->vertexColors.size());
            for (uint32_t packed : geom->vertexColors) {
                const int b = static_cast<int>(packed & 0xFF);
                const int g = static_cast<int>((packed >> 8) & 0xFF);
                const int r = static_cast<int>((packed >> 16) & 0xFF);
                const int a = static_cast<int>((packed >> 24) & 0xFF);
                c.sumB += b; c.sumG += g; c.sumR += r; c.sumA += a;
                for (int v : {b, g, r}) {
                    if (v < c.minByte) c.minByte = v;
                    if (v > c.maxByte) c.maxByte = v;
                }
            }
        }
        return c;
    }
};

// THE LOAD PROOF. Pre-T408 every one of these counts is zero, because the
// loader skipped the array; the mean-byte assertions reproduce T367's census.
TEST_F(BakedVertexLightingTest, Qeynos2RegionMeshesCarryTheirBakedColours) {
    if (!fileExists(zonePath())) {
        GTEST_SKIP() << "no EQ data at " << zonePath();
    }
    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(zonePath(), "qeynos2.wld"))
        << "could not parse qeynos2.wld out of " << zonePath();

    const BakedCensus c = censusQeynos2(loader);

    std::printf("[T408 census] regionMeshes=%d withColours=%d vertices=%lld colours=%lld\n",
                c.regionMeshes, c.regionMeshesWithColors, c.vertices, c.colors);
    if (c.colors > 0) {
        std::printf("[T408 census] mean bytes  B=%.2f G=%.2f R=%.2f A=%.2f  min=%d max=%d\n",
                    c.sumB / static_cast<double>(c.colors),
                    c.sumG / static_cast<double>(c.colors),
                    c.sumR / static_cast<double>(c.colors),
                    c.sumA / static_cast<double>(c.colors),
                    c.minByte, c.maxByte);
    }

    // T367's census said 3,830 region meshes and 72,436 coloured vertices.
    // Taken through the production loader it is EXACTLY HALF of each: 1,915
    // and 36,218. 1,915 is also the count the ResolvedMaterialList comment in
    // wld_loader.h has carried since J-batch ("in qeynos2, all 1915 mesh
    // fragments share one material list"), so the loader and this census
    // agree and T367's temporary instrumentation double-counted every mesh.
    // Nothing about B110's conclusion changes -- the distribution is the same
    // one -- but the numbers on the board were 2x.
    EXPECT_EQ(c.regionMeshes, 1915);
    EXPECT_EQ(c.regionMeshesWithColors, c.regionMeshes)
        << "every region mesh carries colorCount == vertexCount";
    EXPECT_EQ(c.colors, c.vertices) << "one colour per vertex";
    EXPECT_EQ(c.vertices, 36218);

    // It is a real baked light distribution, not a constant: the darkest
    // vertex is 0 and the brightest 255 on all three colour channels.
    EXPECT_EQ(c.minByte, 0);
    EXPECT_EQ(c.maxByte, 255);

    // The packing is BGRA. The three colour means are the static light EQ
    // baked into the zone; T367 quoted their average as 44.2/255 and this
    // reproduces it to 0.24/255. The fourth byte is NOT part of that average
    // and is twice as large -- it is a separate per-vertex channel, pinned
    // here so a future change to the unpack order shows up as a test failure
    // and not as a colour shift nobody can attribute.
    const double meanB = c.sumB / static_cast<double>(c.colors);
    const double meanG = c.sumG / static_cast<double>(c.colors);
    const double meanR = c.sumR / static_cast<double>(c.colors);
    const double meanA = c.sumA / static_cast<double>(c.colors);
    EXPECT_NEAR(meanB, 26.76, 0.05);
    EXPECT_NEAR(meanG, 53.48, 0.05);
    EXPECT_NEAR(meanR, 53.08, 0.05);
    EXPECT_NEAR(meanA, 107.24, 0.05);
    EXPECT_NEAR((meanB + meanG + meanR) / 3.0, 44.2, 0.3)
        << "T367's vertex-weighted mean baked byte";
}

// ---------------------------------------------------------------------------
// The composition pins. T408's live A/B reached the plan's STOP: no member of
// the family reproduces the official client's two day/night ratios, and the
// best fit in it is "baked light off". These pin that result so the next unit
// has to re-measure rather than re-argue.
// ---------------------------------------------------------------------------

#include "client/graphics/zone_shader.h"
#include <fstream>
#include <sstream>

namespace {

std::string readShaderSources() {
    std::ifstream f(T408_ZONE_SHADER_SOURCES);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
    return n;
}

}  // namespace

TEST(BakedLightCompositionTest, TheCommittedCompositionIsTheOneTheLiveAbSolved) {
    // Measured 2026-09-06, qeynos2, weather and heading pinned, geometry-only
    // crop 560x110+200+350, linear light. Oracle = official Titanium client,
    // two sessions agreeing to 0.5%; fable = two sessions per spot.
    //
    //   L = (day/night)_lampfree / (day/night)_lamp
    //   oracle 2.281 | fable off 2.476 | add .25/.50/1.0 2.826/3.096/3.622
    //                | multiply 0.655  | max .50/1.0 3.461/4.683
    //
    // Off is the closest member of the family and every other one is worse,
    // so the composition is Off until the UNIFORM x2.2 night deficit that
    // actually separates the two clients (board B280) is solved.
    EXPECT_EQ(EQT::Graphics::kBakedLightMode, EQT::Graphics::kBakedLightOff)
        << "moving the committed composition needs a NEW live A/B against the "
           "official client -- see review/T408/README.md, not an argument";
    EXPECT_FLOAT_EQ(EQT::Graphics::kBakedLightGain, 0.0f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kBakedLightSkyWeight, 0.0f);
}

TEST(BakedLightCompositionTest, EveryZoneVertexShaderCarriesTheBakedUniformsAndIsNeutralWhenOff) {
    const std::string src = readShaderSources();
    ASSERT_FALSE(src.empty()) << "could not read " << T408_ZONE_SHADER_SOURCES;

    // Six vertex programs share the zone lighting equation (plain, atlas, wind
    // and the three lightweight variants). A copy that missed the edit would
    // light one class of geometry differently with no other symptom.
    EXPECT_EQ(countOf(src, "uniform vec3 uBakedLight;"), 6u);
    EXPECT_EQ(countOf(src, "uniform float uBakedFlag;"), 6u);

    // The neutrality that keeps every pre-T408 pixel golden exact: on a mesh
    // that carries baked light the old "aColor is a tint" reading is replaced
    // by WHITE (and its alpha by 1.0), so mode 0 is the pre-T408 equation and
    // not the multiplicative composition. Getting this wrong is not
    // hypothetical -- T408's first probe pass had it wrong and its mode-0
    // baseline silently measured the multiply.
    EXPECT_EQ(countOf(src, "vec3 tintSrc = mix(aColor.rgb, vec3(1.0), uBakedFlag);"), 6u);
    EXPECT_EQ(countOf(src, "float alphaSrc = mix(aColor.a, 1.0, uBakedFlag);"), 6u);
    EXPECT_EQ(countOf(src, "float bakedOn = uBakedFlag * step(0.5, uBakedLight.z);"), 6u);
    EXPECT_EQ(countOf(src, "vec3 litRGB = mix(tintOnly * tintSrc, lit, bakedOn)"), 6u);

    // And the array the loader now reads is never skipped again.
    EXPECT_EQ(countOf(src, "// Skip colors"), 0u);
}
