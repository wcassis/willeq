// P49 (refactor/P49_drm_fail_loud.md, P43 follow-up): unit coverage for
// docker/irrlicht-drm/drm_mode_select.h's eqt_drm::selectDRMMode(), the
// pure decision logic extracted from CIrrDeviceFB::initDRM() so the P43
// root-caused silent-substitution bug (a requested resolution with no
// exact connector mode falling back to the preferred mode, without any
// signal loud enough for anyone to notice) can be verified WITHOUT a DRM
// device / real hardware -- x86 has no DRM/KMS path at all (see
// docker/CLAUDE.md), so this is the only way to test the decision logic
// itself in this repo; the real CIrrDeviceFB.cpp is build-verified only
// (ARM cross-build) and behaviorally verified by a ride-along Pi check
// (see refactor/deferred.md) by whichever unit next owns the Pi.
#include <gtest/gtest.h>

#include "docker/irrlicht-drm/drm_mode_select.h"

using eqt_drm::DRMModeCandidate;
using eqt_drm::DRMModeSelection;
using eqt_drm::selectDRMMode;

namespace {

// Mirrors the Pi/monitor's real mode list shape from P43's hardware
// confirmation (refactor/P43_res_visibility_root_cause.md Phase B): an
// exact 800x600 entry, a 2560x1440 preferred entry, and no 400x300 entry
// anywhere.
std::vector<DRMModeCandidate> piLikeModes() {
    return {
        {640, 480, 60, false},
        {800, 600, 56, false},
        {800, 600, 60, false},
        {800, 600, 72, false},
        {800, 600, 75, false},  // highest refresh exact match for 800x600
        {1024, 768, 60, false},
        {1280, 720, 60, false},
        {1920, 1080, 60, false},
        {2560, 1440, 60, true}, // preferred
    };
}

}  // namespace

TEST(DRMModeSelect, ExactMatchPicksHighestRefresh) {
    auto modes = piLikeModes();
    DRMModeSelection sel = selectDRMMode(modes, 800, 600);
    ASSERT_TRUE(sel.exactMatch);
    ASSERT_GE(sel.index, 0);
    EXPECT_EQ(modes[sel.index].width, 800u);
    EXPECT_EQ(modes[sel.index].height, 600u);
    EXPECT_EQ(modes[sel.index].refresh, 75u);  // highest of the four 800x600 entries
}

// The exact P43 root-cause scenario: 400x300 has no matching mode at all.
TEST(DRMModeSelect, NoMatchFallsBackToPreferred) {
    auto modes = piLikeModes();
    DRMModeSelection sel = selectDRMMode(modes, 400, 300);
    ASSERT_FALSE(sel.exactMatch);
    ASSERT_GE(sel.index, 0);
    EXPECT_EQ(modes[sel.index].width, 2560u);
    EXPECT_EQ(modes[sel.index].height, 1440u);
    EXPECT_TRUE(modes[sel.index].preferred);
}

TEST(DRMModeSelect, NoMatchNoPreferredFallsBackToFirst) {
    std::vector<DRMModeCandidate> modes = {
        {640, 480, 60, false},
        {1024, 768, 60, false},
    };
    DRMModeSelection sel = selectDRMMode(modes, 400, 300);
    ASSERT_FALSE(sel.exactMatch);
    ASSERT_EQ(sel.index, 0);
    EXPECT_EQ(modes[sel.index].width, 640u);
}

TEST(DRMModeSelect, ZeroRequestedResolutionSkipsMatchingGoesToPreferred) {
    // requestedW/H == 0 (no resolution requested) takes the same fallback
    // path as an unmatched request -- matches the pre-P49 behavior's `if
    // (requestedW > 0 && requestedH > 0)` guard.
    auto modes = piLikeModes();
    DRMModeSelection sel = selectDRMMode(modes, 0, 0);
    ASSERT_FALSE(sel.exactMatch);
    EXPECT_TRUE(modes[sel.index].preferred);
}

TEST(DRMModeSelect, EmptyModeListReturnsInvalidIndex) {
    std::vector<DRMModeCandidate> modes;
    DRMModeSelection sel = selectDRMMode(modes, 800, 600);
    EXPECT_EQ(sel.index, -1);
    EXPECT_FALSE(sel.exactMatch);
}

TEST(DRMModeSelect, MultiplePreferredFlagsPicksFirst) {
    // Malformed/unexpected connector data (more than one PREFERRED entry)
    // should not crash or infinite-loop -- picks the first one, same as
    // the original inline loop's `break` on first match.
    std::vector<DRMModeCandidate> modes = {
        {1920, 1080, 60, true},
        {2560, 1440, 60, true},
    };
    DRMModeSelection sel = selectDRMMode(modes, 400, 300);
    ASSERT_FALSE(sel.exactMatch);
    ASSERT_EQ(sel.index, 0);
    EXPECT_EQ(modes[sel.index].width, 1920u);
}

TEST(DRMModeSelect, ShippedResolutionAlwaysExactOnThisModeList) {
    // Sanity pin: 800x600 (the shipped resolution per P43's Review) must
    // never enter the fallback branch on the real Pi/monitor mode list.
    auto modes = piLikeModes();
    DRMModeSelection sel = selectDRMMode(modes, 800, 600);
    EXPECT_TRUE(sel.exactMatch);
}
