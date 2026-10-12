// B1013-1 (GitLab #334): the ONE parse rule for EQT_DRAW_CENSUS_WINDOW
// (include/client/graphics/draw_census.h drawCensusWindowFromEnv). The
// driver's [drawcensus] window and the client's [drawsite] window share it,
// so the rule lives in the client header and the driver receives the value
// through the gles2SetDrawCensusWindow bridge (drawCensusEndFrame calls it
// once, before its first totals read). This test includes the header WITHOUT
// EQT_HAS_GLES2: the parse rule is a plain client-side inline and must not
// depend on the driver bridge.
#include <gtest/gtest.h>

#include "client/graphics/draw_census.h"

using EQT::Graphics::drawCensusWindowFromEnv;

TEST(DrawCensusWindow, DefaultsAndRejection) {
    EXPECT_EQ(drawCensusWindowFromEnv(nullptr), 120u);
    EXPECT_EQ(drawCensusWindowFromEnv(""), 120u);
    EXPECT_EQ(drawCensusWindowFromEnv("0"), 120u);
    EXPECT_EQ(drawCensusWindowFromEnv("-5"), 120u);
    EXPECT_EQ(drawCensusWindowFromEnv("abc"), 120u);
    EXPECT_EQ(drawCensusWindowFromEnv("12x"), 120u);  // trailing junk
}

TEST(DrawCensusWindow, Values) {
    EXPECT_EQ(drawCensusWindowFromEnv("1"), 1u);
    EXPECT_EQ(drawCensusWindowFromEnv("600"), 600u);
    EXPECT_EQ(drawCensusWindowFromEnv("999999999"), 100000u);  // clamped
    EXPECT_EQ(drawCensusWindowFromEnv("100000"), 100000u);
    EXPECT_EQ(drawCensusWindowFromEnv("100001"), 100000u);
}
