// B815 (issue #83): pin the helper itself -- tests/asset_requirement.h.
//
// The three aggregate asset cases (ComprehensiveZoneTest.ClassicZones,
// ComprehensiveZoneTest.OverallSummary,
// DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries) used to FAIL in a
// fresh worktree (no data/eq_client symlink) while their per-zone siblings
// skipped cleanly. The ruling: an absent asset root is a SKIP locally, and a
// hard FAIL under WILLEQ_REQUIRE_ASSETS=1 (which .gitlab-ci.yml's `test` job
// sets, so CI can never skip these silently). This executable pins that
// decision in isolation.
//
// ctest registration (tests/cmake/core_utils.cmake) runs the binary TWICE:
//   - everything except the failing case -> must pass, with the skip case
//     actually SKIPping (if it ran OK instead, the helper stopped skipping
//     and this is a regression);
//   - AbsentPathWithEnvVarFails ALONE, under WILLEQ_REQUIRE_ASSETS=1, with
//     ctest WILL_FAIL -> the case's GTEST_FAIL is the expected, pinned
//     behaviour (if the helper stopped failing there, WILL_FAIL turns red).
// The failing case cannot assert on its own success: gtest records the
// failure and exits the test, so ctest is the only layer that can witness
// both outcomes from one binary.

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "asset_requirement.h"
#include "scoped_temp_dir.h"

namespace {

// gtest does not expose "this test is skipped" as an API; a skipped body
// stops at GTEST_SKIP(), so a flag set after the call is the observable
// difference between the skip path (reached) and the pass path (the call
// returned).
void markReached(std::string& flag) {
    flag = "reached";
}

} // namespace

class AssetRequirementTest : public ::testing::Test {
protected:
    void SetUp() override {
        flag_.clear();
        unsetenv("WILLEQ_REQUIRE_ASSETS");
    }

    void TearDown() override { unsetenv("WILLEQ_REQUIRE_ASSETS"); }

    std::string flag_;
};

// A path that opens must not skip or fail.
TEST_F(AssetRequirementTest, PresentPathRuns) {
    willeq_test::ScopedTempDir dir("b815_asset_requirement");
    const std::string file = (dir / "root").string();
    { std::ofstream out(file); out << "probe"; }
    eqt::test::requireEqClientAssets(file);
    markReached(flag_);
    EXPECT_EQ(flag_, "reached");
}

// Absent root, no env var: the test is SKIPPED, naming the missing path.
// If the helper ever stops skipping, this body runs to completion and the
// test passes as OK -- which ctest still counts as a regression signal,
// because a passing body where a skip was the contract means the decision
// flipped.
TEST_F(AssetRequirementTest, AbsentPathSkips) {
    eqt::test::requireEqClientAssets("/no/such/asset/root/b815");
    markReached(flag_);
}

// Absent root + WILLEQ_REQUIRE_ASSETS=1: a hard failure, not a skip.
// Deliberately last: GTEST_FAIL aborts this test body and marks the run
// failed; ctest runs it alone with WILL_FAIL (see the file header).
TEST_F(AssetRequirementTest, AbsentPathWithEnvVarFails) {
    setenv("WILLEQ_REQUIRE_ASSETS", "1", 1);
    eqt::test::requireEqClientAssets("/no/such/asset/root/b815");
    markReached(flag_);
}

// The env var only means anything when the path is actually missing.
TEST_F(AssetRequirementTest, PresentPathWithEnvVarRuns) {
    setenv("WILLEQ_REQUIRE_ASSETS", "1", 1);
    willeq_test::ScopedTempDir dir("b815_asset_requirement");
    const std::string file = (dir / "root").string();
    { std::ofstream out(file); out << "probe"; }
    eqt::test::requireEqClientAssets(file);
    markReached(flag_);
    EXPECT_EQ(flag_, "reached");
}

// A value other than "1" does not arm the requirement (the CI convention is
// exactly WILLEQ_REQUIRE_ASSETS=1).
TEST_F(AssetRequirementTest, AbsentPathWithOtherEnvValueSkips) {
    setenv("WILLEQ_REQUIRE_ASSETS", "true", 1);
    eqt::test::requireEqClientAssets("/no/such/asset/root/b815");
    markReached(flag_);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
