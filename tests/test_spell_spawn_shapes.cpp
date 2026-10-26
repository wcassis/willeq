// B1150 (#470): the ten spawn shapes the official client decodes from
// spellsnew.edd +0xA8.
//
// Census (the card's distribution, re-derived by this unit's own one-liner
// over the real file -- refactor/B1150/README.md section 2):
//   0:297 1:202 2:13 3:125 4:275 5:808 6:5 7:9 8:18 9:366  (sum 2118)
//
// Geometry: every case of computeEddShapePoint() (the client's per-shape
// switch at EQGraphicsDX9.dll 0x10061753, jump table 0x1006408c, cases
// 0x10061769..0x10061fa5) is
// asserted as a surface invariant -- the point of each shape sits on its
// surface -- plus the offsets (+0xB8..+0xC0, pre-tilt basis, x s19c), the
// two tilts (+0xC4 rotates (b1,b2), +0xC8 then rotates (b1,b3)), the +0x19C
// scale and the +0xB0 == 0 -> "+0xAC" rule.
//
// RNG equivalence (stated once, as the card rules): the client's 512/1000/
// 2000-entry tables are uniform draws (the 512-table a permutation of the
// 512ths angle indices 0..511 feeding the 512-entry SIN/COS lookups at
// 0x10184500/0x10184520; the 2000-table a permutation of [-1, 1)), so the
// sim draws from particleRandomFloat(); the tables are not emulated.

#include <gtest/gtest.h>

#include "client/graphics/environment/spell_spawn_shape.h"
#include "client/spell/spells_new_edd_reader.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using EQT::Graphics::Environment::EddShapeParams;
using EQT::Graphics::Environment::computeEddShapePoint;
using EQT::Graphics::Environment::eddStackedRingCounts;

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

void putI(std::vector<uint8_t>& e, size_t off, int32_t v) { std::memcpy(&e[off], &v, 4); }
void putF(std::vector<uint8_t>& e, size_t off, float v) { std::memcpy(&e[off], &v, 4); }

bool near1(float x) { return std::fabs(x - 1.0f) < 1e-4f; }

// A deterministic draw sequence in [0,1) standing in for the client's
// uniform tables.
struct SeqDraw {
    int i = 0;
    float operator()() { return static_cast<float>(0.13f + 0.17f * (i++ % 17)); }
};

// A fresh draw per call site is what the client has (one table counter,
// consecutive values), so each case below draws through its own instance.
template <typename F>
void withDraw(F f) {
    SeqDraw d;
    f([&d] { return d(); });
}

}  // namespace

// ---------------------------------------------------------------------------
// The reader: the shape words at their code offsets.
// ---------------------------------------------------------------------------

TEST(SpellSpawnShapesReader, DecodesTheShapeWordsAtTheirCodeOffsets) {
    std::vector<uint8_t> e(416, 0);
    putI(e, 0xA8, 7);
    putF(e, 0xAC, 1.5f);
    putF(e, 0xB0, 0.0f);   // the "same as +0xAC" marker stays 0 here
    putF(e, 0xB4, 2.5f);
    putF(e, 0xB8, 0.5f);
    putF(e, 0xBC, -0.5f);
    putF(e, 0xC0, 2.0f);
    putF(e, 0xC4, 128.0f);
    putF(e, 0xC8, 64.0f);
    putF(e, 0xD0, -0.25f);
    putI(e, 0x19C, 1);

    const EQ::SpellsNewEddShape s = EQ::decodeSpellsNewEddShape(e.data());
    EXPECT_EQ(s.shape, 7);
    EXPECT_FLOAT_EQ(s.dimX, 1.5f);
    EXPECT_FLOAT_EQ(s.dimY, 0.0f);
    EXPECT_FLOAT_EQ(s.dimZ, 2.5f);
    EXPECT_FLOAT_EQ(s.offX, 0.5f);
    EXPECT_FLOAT_EQ(s.offY, -0.5f);
    EXPECT_FLOAT_EQ(s.offZ, 2.0f);
    EXPECT_FLOAT_EQ(s.tiltA, 128.0f);
    EXPECT_FLOAT_EQ(s.tiltB, 64.0f);
    EXPECT_FLOAT_EQ(s.depth, -0.25f);
    EXPECT_TRUE(s.scaleShapes);
}

// ---------------------------------------------------------------------------
// The shipped census.
// ---------------------------------------------------------------------------

TEST(SpellSpawnShapesShipped, CensusOfTheShapeWord) {
    EQ::SpellsNewEddReader reader;
    if (!reader.loadFile(clientDir() + "/spellsnew.edd"))
        GTEST_SKIP() << "spellsnew.edd not under " << clientDir();

    std::vector<int> counts(10, 0);
    for (size_t k = 0; k < reader.emitterCount(); ++k) {
        const EQ::SpellsNewEddShape& s = reader.shapeAt(k);
        ASSERT_GE(s.shape, 0) << "entry " << k;
        ASSERT_LT(s.shape, 10) << "entry " << k << " shape " << s.shape;
        ++counts[s.shape];
    }
    // 0:297 1:202 2:13 3:125 4:275 5:808 6:5 7:9 8:18 9:366 (sum 2118)
    const int want[10] = {297, 202, 13, 125, 275, 808, 5, 9, 18, 366};
    for (int s = 0; s < 10; ++s)
        EXPECT_EQ(counts[s], want[s]) << "shape " << s;
    int sum = 0;
    for (int c : counts) sum += c;
    EXPECT_EQ(sum, 2118);
}

// ---------------------------------------------------------------------------
// The ten cases: surface invariants.
// ---------------------------------------------------------------------------

TEST(SpellSpawnShapes, PointIsTheOrigin) {
    EddShapeParams p;  // shape 0
    const auto out = computeEddShapePoint(p, 0, 1, [] { return 0.5f; });
    EXPECT_EQ(out.point, glm::vec3(0.0f));
    EXPECT_FALSE(out.hasNormal);
}

TEST(SpellSpawnShapes, RingIsEvenlySpacedOnItsCircle) {
    EddShapeParams p;
    p.shape = 1;
    p.dimX = 2.0f;
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        const auto out = computeEddShapePoint(p, i, n, [] { return 0.5f; });
        // The in-plane circle is in the (b2,b3) = (world-Y, world-Z) plane; the
        // height axis (b1, world-X) is 0 on the ring.
        EXPECT_FLOAT_EQ(std::hypot(out.point.y, out.point.z), 2.0f) << i;
        EXPECT_FLOAT_EQ(out.point.x, 0.0f) << i;
    }
    // Evenly spaced: the neighbour's angle differs by 2*pi/n.
    const auto a = computeEddShapePoint(p, 0, n, [] { return 0.5f; });
    const auto b = computeEddShapePoint(p, 1, n, [] { return 0.5f; });
    const float dot = glm::dot(glm::vec3(0.0f, a.point.y, a.point.z),
                               glm::vec3(0.0f, b.point.y, b.point.z));
    EXPECT_NEAR(dot, 2.0f * 2.0f * std::cos(2.0f * 3.14159265358979323846f / n), 1e-4f);
}

TEST(SpellSpawnShapes, StackedRingsRideTheirCountTableLevels) {
    EddShapeParams p;
    p.shape = 2;
    p.dimX = 2.0f;
    p.dimZ = 4.0f;
    int rings = 0, perRing = 0;
    eddStackedRingCounts(18, rings, perRing);
    EXPECT_EQ(rings, 3);
    EXPECT_EQ(perRing, 6);
    // Every particle of a batch of 18 sits on one of the three levels
    // z = (B4/R)*k - 0.5*B4, k = (i/perRing) % R, at radius AC.
    for (int i = 0; i < 18; ++i) {
        const auto out = computeEddShapePoint(p, i, 18, [] { return 0.5f; });
        EXPECT_FLOAT_EQ(std::hypot(out.point.y, out.point.z), 2.0f) << i;
        const int k = (i / perRing) % rings;
        EXPECT_FLOAT_EQ(out.point.x, (4.0f / 3.0f) * static_cast<float>(k) - 2.0f) << i;
    }
    // The tier table's own shape (decompile lines 68595-68642).
    int r = 0, pr = 0;
    eddStackedRingCounts(8, r, pr);  EXPECT_EQ(r, 8);  EXPECT_EQ(pr, 16);
    eddStackedRingCounts(9, r, pr);  EXPECT_EQ(r, 3);  EXPECT_EQ(pr, 3);
    eddStackedRingCounts(512, r, pr); EXPECT_EQ(r, 16); EXPECT_EQ(pr, 32);
    eddStackedRingCounts(513, r, pr); EXPECT_EQ(r, 24); EXPECT_EQ(pr, 21);
    eddStackedRingCounts(3200, r, pr); EXPECT_EQ(r, 40); EXPECT_EQ(pr, 80);
    eddStackedRingCounts(7200, r, pr); EXPECT_EQ(r, 100); EXPECT_EQ(pr, 72);
}

TEST(SpellSpawnShapes, CylinderSurfacesAtRadiusAndHalfHeight) {
    EddShapeParams p;
    p.shape = 3;
    p.dimX = 2.0f;
    p.dimZ = 4.0f;
    for (int i = 0; i < 32; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 32, [&d] { return d(); });
        EXPECT_NEAR(std::hypot(out.point.y, out.point.z), 2.0f, 1e-4f) << i;
        EXPECT_GE(out.point.x, -2.0f - 1e-4f) << i;
        EXPECT_LT(out.point.x, 2.0f) << i;  // (u - 0.5) * B4, u in [0, 1)
    }
}

TEST(SpellSpawnShapes, DiscFillsItsEllipse) {
    EddShapeParams p;
    p.shape = 4;
    p.dimX = 2.0f;
    p.dimY = 1.0f;
    for (int i = 0; i < 64; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 64, [&d] { return d(); });
        // Inside or on the unit ellipse (x0^2 + y0^2 <= 1 of the client's
        // rejection), with x0 = (world-Y)/AC, y0 = (world-Z)/B0.
        const float q = (out.point.y / 2.0f) * (out.point.y / 2.0f) +
                        (out.point.z / 1.0f) * (out.point.z / 1.0f);
        EXPECT_LE(q, 1.0f + 1e-4f) << i;
        EXPECT_FLOAT_EQ(out.point.x, 0.0f) << i;
    }
    // The draws are wide enough to actually fill it: some point past half
    // radius exists among 64 rejections.
    bool far = false;
    for (int i = 0; i < 64; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 64, [&d] { return d(); });
        if (std::hypot(out.point.y / 2.0f, out.point.z / 1.0f) > 0.7f) far = true;
    }
    EXPECT_TRUE(far);
}

TEST(SpellSpawnShapes, SphereSitsOnItsEllipsoidWithAnOutwardNormal) {
    EddShapeParams p;
    p.shape = 5;
    p.dimX = 2.0f;
    p.dimY = 1.0f;
    p.dimZ = 3.0f;
    for (int i = 0; i < 32; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 32, [&d] { return d(); });
        // (X/B4)^2 + (Y/AC)^2 + (Z/B0)^2 == 1, where X=world-X (the height,
        // semi-axis B4=3), Y=world-Y (semi-axis AC=2), Z=world-Z (semi-axis
        // B0=1); z0 uniform, r0 = sqrt(1-z0^2).
        const float q = (out.point.x / 3.0f) * (out.point.x / 3.0f) +
                        (out.point.y / 2.0f) * (out.point.y / 2.0f) +
                        (out.point.z / 1.0f) * (out.point.z / 1.0f);
        EXPECT_NEAR(q, 1.0f, 1e-4f) << i;
        EXPECT_TRUE(out.hasNormal) << i;
        // Outward: the normal points away from the centre of the ellipsoid.
        EXPECT_GT(glm::dot(out.normal, out.point / glm::vec3(3.0f, 2.0f, 1.0f)), 0.0f) << i;
        EXPECT_NEAR(glm::length(out.normal), 1.0f, 1e-4f) << i;
    }
}

TEST(SpellSpawnShapes, BoxSitsOnItsFaceWithAnOutwardNormal) {
    EddShapeParams p;
    p.shape = 6;
    p.dimX = 2.0f;
    p.dimY = 1.0f;
    p.dimZ = 3.0f;
    for (int i = 0; i < 96; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 96, [&d] { return d(); });
        const glm::vec3& q = out.point;
        // Half-extents: world-X (B4=3), world-Y (AC=2), world-Z (B0=1).
        EXPECT_LE(std::fabs(q.x), 3.0f + 1e-4f) << i;
        EXPECT_LE(std::fabs(q.y), 2.0f + 1e-4f) << i;
        EXPECT_LE(std::fabs(q.z), 1.0f + 1e-4f) << i;
        // On the surface: at least one coordinate is at its half-extent.
        EXPECT_TRUE(near1(std::fabs(q.x) / 3.0f) || near1(std::fabs(q.y) / 2.0f) ||
                    near1(std::fabs(q.z) / 1.0f))
            << i << " (" << q.x << ", " << q.y << ", " << q.z << ")";
        EXPECT_TRUE(out.hasNormal) << i;
        // The normal is an axis pointing outward from the face it is on.
        EXPECT_NEAR(std::fabs(glm::dot(out.normal, q / glm::vec3(3.0f, 2.0f, 1.0f))), 1.0f, 1e-4f)
            << i;
    }
}

TEST(SpellSpawnShapes, ConeCouplesRadiusAndHeightOnOneDraw) {
    EddShapeParams p;
    p.shape = 7;
    p.dimX = 2.0f;
    p.dimZ = 4.0f;
    for (int i = 0; i < 32; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 32, [&d] { return d(); });
        const float r = std::hypot(out.point.y, out.point.z);
        // ONE u: r = u*AC and the height (world-X) = u*B4, so X = (r/AC)*B4
        // exactly.
        EXPECT_NEAR(out.point.x, (r / 2.0f) * 4.0f, 1e-4f) << i;
        EXPECT_LE(r, 2.0f + 1e-4f) << i;
        EXPECT_LE(out.point.x, 4.0f) << i;
    }
}

TEST(SpellSpawnShapes, TorusSitsOnItsTorus) {
    EddShapeParams p;
    p.shape = 8;
    p.dimX = 2.0f;
    p.dimZ = 0.5f;
    for (int i = 0; i < 64; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 64, [&d] { return d(); });
        // ((in-plane radius - major)^2 + height^2) == minor^2, where the
        // in-plane circle is in (world-Y, world-Z) and the tube height is
        // world-X.
        const float ring = std::hypot(out.point.y, out.point.z) - 2.0f;
        EXPECT_NEAR(ring * ring + out.point.x * out.point.x, 0.25f, 1e-4f) << i;
    }
}

TEST(SpellSpawnShapes, RingRandomAngleIsOnTheCircle) {
    EddShapeParams p;
    p.shape = 9;
    p.dimX = 2.0f;
    for (int i = 0; i < 32; ++i) {
        SeqDraw d;
        const auto out = computeEddShapePoint(p, i, 32, [&d] { return d(); });
        EXPECT_NEAR(std::hypot(out.point.y, out.point.z), 2.0f, 1e-4f) << i;
        EXPECT_FLOAT_EQ(out.point.x, 0.0f) << i;
    }
}

// ---------------------------------------------------------------------------
// The shared words: B0 == 0 -> AC, offsets, tilts, the +0x19C scale.
// ---------------------------------------------------------------------------

TEST(SpellSpawnShapes, ZeroDimYFallsBackToDimX) {
    EddShapeParams p;
    p.shape = 4;
    p.dimX = 2.0f;
    p.dimY = 0.0f;  // circle, not ellipse
    SeqDraw d;
    const auto out = computeEddShapePoint(p, 0, 1, [&d] { return d(); });
    // dimY == 0: a circle of radius AC in the (world-Y, world-Z) plane.
    const float q = (out.point.y / 2.0f) * (out.point.y / 2.0f) +
                    (out.point.z / 2.0f) * (out.point.z / 2.0f);
    EXPECT_LE(q, 1.0f + 1e-4f);
}

TEST(SpellSpawnShapes, OffsetsMoveTheOriginAlongThePreTiltBasis) {
    EddShapeParams p;  // shape 0: the point is exactly the origin
    p.offX = 1.0f;
    p.offY = 2.0f;
    p.offZ = 3.0f;
    const auto out = computeEddShapePoint(p, 0, 1, [] { return 0.5f; });
    EXPECT_FLOAT_EQ(out.point.x, 1.0f);
    EXPECT_FLOAT_EQ(out.point.y, 2.0f);
    EXPECT_FLOAT_EQ(out.point.z, 3.0f);
}

// Axis mapping (the client's point combination, decompile lines 69352-69370):
// world = height*axis1 + cos(th)*r*axis2 + sin(th)*r*axis3, so the shape's
// z (height) rides b1 (world X here), its x (cos*r) rides b2 (world Y) and
// its y (sin*r) rides b3 (world Z). A ring's angle-0 point (x=1) is therefore
// along b2, its angle-90 point (y=1) along b3 -- not along world X.
TEST(SpellSpawnShapes, TiltARotatesTheFirstTwoAxes) {
    EddShapeParams p;
    p.shape = 1;
    p.dimX = 1.0f;
    p.tiltA = 128.0f;  // a quarter turn of (b1, b2) in EQ 512ths
    // The ring's angle-0 point lies along b2 pre-tilt; b2' = cos*b2 - sin*b1
    // sends it to -b1 (world -X).
    const auto out0 = computeEddShapePoint(p, 0, 4, [] { return 0.5f; });
    EXPECT_NEAR(out0.point.x, -1.0f, 1e-4f);
    EXPECT_NEAR(out0.point.y, 0.0f, 1e-4f);
    EXPECT_NEAR(out0.point.z, 0.0f, 1e-4f);
    // The angle-90 point lies along b3, which tilt A does not touch.
    const auto out = computeEddShapePoint(p, 1, 4, [] { return 0.5f; });
    EXPECT_NEAR(out.point.x, 0.0f, 1e-4f);
    EXPECT_NEAR(out.point.y, 0.0f, 1e-4f);
    EXPECT_NEAR(out.point.z, 1.0f, 1e-4f);
}

TEST(SpellSpawnShapes, TiltBRotatesTheFirstAndThirdAxes) {
    EddShapeParams p;
    p.shape = 1;
    p.dimX = 1.0f;
    p.tiltB = 128.0f;  // a quarter turn of (b1, b3)
    // The angle-90 point lies along b3 pre-tilt; b3' = cos*b3 - sin*b1 sends
    // it to -b1 (world -X).
    const auto out = computeEddShapePoint(p, 1, 4, [] { return 0.5f; });
    EXPECT_NEAR(out.point.x, -1.0f, 1e-4f);
    EXPECT_NEAR(out.point.y, 0.0f, 1e-4f);
    EXPECT_NEAR(out.point.z, 0.0f, 1e-4f);
    // The angle-0 point lies along b2, which tilt B does not touch.
    const auto out0 = computeEddShapePoint(p, 0, 4, [] { return 0.5f; });
    EXPECT_NEAR(out0.point.x, 0.0f, 1e-4f);
    EXPECT_NEAR(out0.point.y, 1.0f, 1e-4f);
    EXPECT_NEAR(out0.point.z, 0.0f, 1e-4f);
}

TEST(SpellSpawnShapes, ScaleMultipliesRadiiHeightsAndOffsets) {
    EddShapeParams p;
    p.shape = 0;
    p.offX = 1.0f;
    p.scale = 2.0f;
    const auto out = computeEddShapePoint(p, 0, 1, [] { return 0.5f; });
    EXPECT_FLOAT_EQ(out.point.x, 2.0f);

    EddShapeParams ring;
    ring.shape = 1;
    ring.dimX = 1.0f;
    ring.scale = 3.0f;
    // The angle-0 ring point is along b2 (world Y), scaled by 3.
    const auto o = computeEddShapePoint(ring, 0, 4, [] { return 0.5f; });
    EXPECT_NEAR(o.point.x, 0.0f, 1e-4f);
    EXPECT_NEAR(o.point.y, 3.0f, 1e-4f);
    EXPECT_NEAR(o.point.z, 0.0f, 1e-4f);
}
