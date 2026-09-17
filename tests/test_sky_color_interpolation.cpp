// T495 (board B789/B790, ONE defect): the sky/fog colour table's channel lerp
// underflowed `irr::u32`.
//
// `SkyRenderer::interpolateSkyColors()` lerped each channel as
//     a.getRed() + (b.getRed() - a.getRed()) * t
// and `SColor::getRed()` returns `irr::u32`. On every ramp where a channel
// DECREASES the subtraction wrapped to ~4.29e9; `* t` in float zeroed the low
// bits (a 24-bit mantissa cannot hold a 32-bit integer); and the `& 0xff`
// inside SColor's constructor left 0, 64, 128 or 192. Almost every segment of
// the table has a decreasing channel, so most of the day was garbage.
//
// The garbage reached three consumers: the dome's vertex tint, the scene clear
// colour, and `getRecommendedFogColor()` -> `IrrlichtRenderer::updateTimeOfDay()`
// -> `driver_->setFog()` -> `uFogColor`, which every fogged pixel is mixed
// toward. That is why T492's corpus showed a red sky (B789) and a red horizon
// band over the trees (B790): they are the same arithmetic reaching two
// consumers, not two defects.
//
// These cases are PURE: no device, no assets, no scene manager. The pixel
// consumer is pinned separately in test_sky_dome_tint_channel_order.cpp.
//
// The bounds here come from the TABLE's own anchors, never from the official
// client: whether the anchors are the right colours at all is board B781, a
// separate live question. All this file asserts is that a colour between two
// anchors is BETWEEN them.

#include <gtest/gtest.h>

#include "client/graphics/sky_color_table.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace EQT::Graphics;

namespace {

float hourOf(int h, int m) {
    return static_cast<float>(h) + static_cast<float>(m) / 60.0f;
}

std::string triple(const irr::video::SColor& c) {
    return "(" + std::to_string(c.getRed()) + "," + std::to_string(c.getGreen()) +
           "," + std::to_string(c.getBlue()) + ")";
}

std::string clock(int h, int m) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}

// The production tint, at a given hour with no weather darkening
// (weatherBrightness_ = 1.0, its default) -- what
// SkyRenderer::updateSkyLayerColors() writes into the dome's vertices.
irr::video::SColor tintAt(int h, int m) {
    const SkyColorSet s = SkyColorsAt(hourOf(h, m));
    return SkyLayerTint(s, s.cloudBrightness * 1.0f);
}

void expectInHull(const char* what, const irr::video::SColor& got,
                  const irr::video::SColor& a, const irr::video::SColor& b,
                  const std::string& when) {
    const irr::u32 ga[3] = {got.getRed(), got.getGreen(), got.getBlue()};
    const irr::u32 aa[3] = {a.getRed(), a.getGreen(), a.getBlue()};
    const irr::u32 bb[3] = {b.getRed(), b.getGreen(), b.getBlue()};
    static const char* kChan[3] = {"red", "green", "blue"};
    for (int i = 0; i < 3; ++i) {
        const irr::u32 lo = std::min(aa[i], bb[i]);
        const irr::u32 hi = std::max(aa[i], bb[i]);
        EXPECT_GE(ga[i], lo) << when << " " << what << " " << kChan[i] << ": got "
                             << triple(got) << ", anchors " << triple(a) << " -> "
                             << triple(b);
        EXPECT_LE(ga[i], hi) << when << " " << what << " " << kChan[i] << ": got "
                             << triple(got) << ", anchors " << triple(a) << " -> "
                             << triple(b);
    }
}

}  // namespace

// Every colour the table produces between 05:00 and 21:00 must lie inside the
// hull of the two anchors it is being interpolated between, per channel.
// Before the fix this failed all over the day; the first failure of the sweep
// was 05:00.
TEST(SkyColorTableTest, EveryChannelStaysInsideItsAnchorHull) {
    for (int h = 5; h < 21; ++h) {
        for (int m = 0; m < 60; m += 5) {
            const float t = hourOf(h, m);
            const SkyColorSegment seg = SkyColorSegmentAt(t);
            ASSERT_NE(seg.a, nullptr);
            ASSERT_NE(seg.b, nullptr);
            const SkyColorSet got = SkyColorsAt(t);
            const std::string when = clock(h, m);
            expectInHull("zenith", got.zenith, seg.a->zenith, seg.b->zenith, when);
            expectInHull("horizon", got.horizon, seg.a->horizon, seg.b->horizon, when);
            expectInHull("fog", got.fog, seg.a->fog, seg.b->fog, when);
        }
    }
}

// Inside one segment each channel must move monotonically from its `a` value
// to its `b` value. The underflow made decreasing channels jump around.
TEST(SkyColorTableTest, EveryChannelIsMonotoneAcrossASegment) {
    for (int h = 5; h < 21; ++h) {
        const SkyColorSegment seg = SkyColorSegmentAt(hourOf(h, 0));
        ASSERT_NE(seg.a, nullptr);
        if (seg.a == seg.b) continue;  // constant arm

        const irr::video::SColor* pairs[3][2] = {
            {&seg.a->zenith, &seg.b->zenith},
            {&seg.a->horizon, &seg.b->horizon},
            {&seg.a->fog, &seg.b->fog},
        };
        static const char* kWhat[3] = {"zenith", "horizon", "fog"};

        for (int p = 0; p < 3; ++p) {
            for (int c = 0; c < 3; ++c) {
                const auto chan = [c](const irr::video::SColor& col) -> int {
                    return c == 0 ? static_cast<int>(col.getRed())
                         : c == 1 ? static_cast<int>(col.getGreen())
                                  : static_cast<int>(col.getBlue());
                };
                const int from = chan(*pairs[p][0]);
                const int to = chan(*pairs[p][1]);
                const int dir = (to > from) ? 1 : (to < from) ? -1 : 0;
                int prev = from;
                for (int step = 0; step <= 100; ++step) {
                    const float t = static_cast<float>(step) / 100.0f;
                    const SkyColorSet s = LerpSkyColors(*seg.a, *seg.b, t);
                    const irr::video::SColor got =
                        p == 0 ? s.zenith : p == 1 ? s.horizon : s.fog;
                    const int v = chan(got);
                    if (dir >= 0) {
                        EXPECT_GE(v, prev) << "hour " << h << " " << kWhat[p]
                                           << " channel " << c << " at t=" << t
                                           << ": " << from << " -> " << to
                                           << ", went " << prev << " -> " << v;
                    }
                    if (dir <= 0) {
                        EXPECT_LE(v, prev) << "hour " << h << " " << kWhat[p]
                                           << " channel " << c << " at t=" << t
                                           << ": " << from << " -> " << to
                                           << ", went " << prev << " -> " << v;
                    }
                    prev = v;
                }
                EXPECT_EQ(prev, to) << "hour " << h << " " << kWhat[p]
                                    << " channel " << c << " must land on its anchor";
            }
        }
    }
}

// The corpus triples, pinned by name. Each expected value is the anchors'
// arithmetic, computed by hand below; each "was" is what T492's frames
// measured out of the broken code.
TEST(SkyColorTableTest, TheQeytoqrgCorpusHourIsNotRed) {
    // review/T492/llmbench/players/p2/run.log: zone-in game time 17:25.
    // Segment AFTERNOON -> SUNSET, t = 25/60 = 0.41667.
    //   zenith  (120,160,210) -> (130,130,180) = (124,148,198)
    //   horizon (210,200,190) -> (255,160, 80) = (229,183,144)
    //   zenith/horizon average (176,165,171), * cloudBrightness 0.9167
    //   = tint (161,151,156), clear = tint/2 = (80,75,78)
    // was (161,58,58): red 2.8x both other channels, the flat colour filling
    // 35,748 of the 36,000 px of the sky crop in
    // frames/15:38:13.227_F_sky_p2_overhead_window.png AND 33,893 px of the
    // tree band in 15:46:54.538_H_cam_p2_qeytoqrg_chase_window.png.
    const irr::video::SColor tint = tintAt(17, 25);
    const irr::video::SColor clear = SkyClearColorFromTint(tint);
    EXPECT_NEAR(static_cast<double>(tint.getRed()), 161.0, 1.5) << triple(tint);
    EXPECT_NEAR(static_cast<double>(tint.getGreen()), 151.0, 1.5) << triple(tint);
    EXPECT_NEAR(static_cast<double>(tint.getBlue()), 156.0, 1.5) << triple(tint);
    // The shape that made B789/B790 look like a missing texture: red hugely
    // dominant. Whatever the anchors end up being (B781), 17:25 is a sunset
    // blend of two near-grey colours and cannot be 2.8x red.
    EXPECT_LT(static_cast<double>(tint.getRed()),
              2.0 * static_cast<double>(tint.getBlue()))
        << "tint " << triple(tint) << " -- (161,58,58) is the underflow";
    EXPECT_NEAR(static_cast<double>(clear.getRed()), 80.0, 1.5) << triple(clear);
    EXPECT_NEAR(static_cast<double>(clear.getGreen()), 75.0, 1.5) << triple(clear);
    EXPECT_NEAR(static_cast<double>(clear.getBlue()), 78.0, 1.5) << triple(clear);
    // was (80,29,29).
    EXPECT_GT(static_cast<double>(clear.getBlue()), 40.0) << triple(clear);
}

TEST(SkyColorTableTest, TheLegEHourHasFogWithBlueInIt) {
    // review/T492 Leg E game time 16:49; the far field in
    // frames/16:43:43.983_H_cam_p2_qeytoqrg_chase_window.png measured
    // (170.0,178.0,0.75) -- blue EXACTLY zero over the sky and the distant
    // trees, which is uFogColor, not a texture.
    // Segment DAY -> AFTERNOON: fog (160,180,200) -> (175,180,190).
    //   16:45 t=0.75  -> (171,180,193)   was (171,180,0)
    //   16:49 t=0.8167-> (172,180,192)   was (172,180,0)
    for (const auto& c : {std::make_pair(45, 193), std::make_pair(49, 192)}) {
        const SkyColorSet s = SkyColorsAt(hourOf(16, c.first));
        EXPECT_NEAR(static_cast<double>(s.fog.getBlue()),
                    static_cast<double>(c.second), 1.5)
            << clock(16, c.first) << " fog " << triple(s.fog)
            << " -- blue 0 is the underflow (B790's 'pink' far field)";
        EXPECT_GE(s.fog.getBlue(), 190u) << triple(s.fog);
        EXPECT_LE(s.fog.getBlue(), 200u) << triple(s.fog);
    }
}

TEST(SkyColorTableTest, DuskDoesNotCollapseToBlack) {
    // 18:30, segment SUNSET -> DUSK at t=0.5: every anchor channel was
    // decreasing, so all three colours underflowed to (0,0,0) together and the
    // tint with them. Expected: zenith (105,95,150), horizon (228,130,70),
    // fog (150,110,100), tint (107,72,71).
    const SkyColorSet s = SkyColorsAt(hourOf(18, 30));
    EXPECT_GT(s.zenith.getRed() + s.zenith.getGreen() + s.zenith.getBlue(), 0u)
        << "zenith " << triple(s.zenith);
    EXPECT_NEAR(static_cast<double>(s.zenith.getRed()), 105.0, 1.5) << triple(s.zenith);
    EXPECT_NEAR(static_cast<double>(s.zenith.getGreen()), 95.0, 1.5) << triple(s.zenith);
    EXPECT_NEAR(static_cast<double>(s.zenith.getBlue()), 150.0, 1.5) << triple(s.zenith);
    EXPECT_NEAR(static_cast<double>(s.horizon.getRed()), 228.0, 1.5) << triple(s.horizon);
    EXPECT_NEAR(static_cast<double>(s.horizon.getGreen()), 130.0, 1.5) << triple(s.horizon);
    EXPECT_NEAR(static_cast<double>(s.horizon.getBlue()), 70.0, 1.5) << triple(s.horizon);
    EXPECT_NEAR(static_cast<double>(s.fog.getRed()), 150.0, 1.5) << triple(s.fog);
    EXPECT_NEAR(static_cast<double>(s.fog.getGreen()), 110.0, 1.5) << triple(s.fog);
    EXPECT_NEAR(static_cast<double>(s.fog.getBlue()), 100.0, 1.5) << triple(s.fog);
    const irr::video::SColor tint = tintAt(18, 30);
    EXPECT_GT(tint.getRed() + tint.getGreen() + tint.getBlue(), 0u) << triple(tint);
}

TEST(SkyColorTableTest, TheAfternoonZenithKeepsItsBlue) {
    // 16:15, segment DAY -> AFTERNOON: zenith blue 220 -> 210, a DECREASING
    // channel, so it came out 0 -- a blue sky with no blue in it. Expected
    // (105,153,218).
    const SkyColorSet s = SkyColorsAt(hourOf(16, 15));
    EXPECT_NEAR(static_cast<double>(s.zenith.getBlue()), 218.0, 1.5) << triple(s.zenith);
    EXPECT_GE(s.zenith.getBlue(), 210u) << triple(s.zenith);
    EXPECT_LE(s.zenith.getBlue(), 220u) << triple(s.zenith);
}

// The constant arms and the exact anchors are unchanged by the fix -- 11:00 is
// what T484's pixel test renders, and it must not move.
TEST(SkyColorTableTest, TheDaylightArmIsUnchanged) {
    const SkyColorSet s = SkyColorsAt(11.0f);
    EXPECT_EQ(s.zenith.getRed(), 100u);
    EXPECT_EQ(s.zenith.getGreen(), 150u);
    EXPECT_EQ(s.zenith.getBlue(), 220u);
    EXPECT_EQ(s.horizon.getRed(), 180u);
    EXPECT_EQ(s.horizon.getGreen(), 200u);
    EXPECT_EQ(s.horizon.getBlue(), 220u);
    const irr::video::SColor tint = tintAt(11, 0);
    EXPECT_EQ(tint.getRed(), 140u) << triple(tint);
    EXPECT_EQ(tint.getGreen(), 175u) << triple(tint);
    EXPECT_EQ(tint.getBlue(), 220u) << triple(tint);
}

// t outside 0-1 must clamp to an anchor, not extrapolate out of the hull.
TEST(SkyColorTableTest, BlendFactorIsClamped) {
    const SkyColorSegment seg = SkyColorSegmentAt(17.5f);
    const SkyColorSet below = LerpSkyColors(*seg.a, *seg.b, -3.0f);
    const SkyColorSet above = LerpSkyColors(*seg.a, *seg.b, 4.0f);
    EXPECT_EQ(below.zenith.color, seg.a->zenith.color) << triple(below.zenith);
    EXPECT_EQ(above.zenith.color, seg.b->zenith.color) << triple(above.zenith);
}
