// B815 (issue #83): one helper that decides what an asset-dependent test does
// when its asset root is missing.
//
// `data/eq_client` is a gitignored symlink (tests/test_data_paths.cpp): it is
// present in the main checkout and in CI (the .gitlab-ci.yml before_script
// re-makes it after every `git clean -ffdx`), but absent in every fresh task
// worktree until someone makes it by hand. The per-zone tests already SKIP on
// a missing zone file; the three aggregate tests (ClassicZones,
// OverallSummary, MultipleZonesHaveObjectGeometries) instead counted "not
// found" zones and FAILED on `loadedCount > 0`, so a fresh worktree read as a
// regression.
//
// Decision (orchestrator ruling on B815): an absent asset root is a SKIP for
// a local run, but CI must never skip silently -- when WILLEQ_REQUIRE_ASSETS=1
// the same condition is a hard FAIL, and .gitlab-ci.yml sets it.
//
// Call it from a fixture SetUp (or at the top of a test body, before any
// asset use), wherever the suite resolves EQ_CLIENT_DATA_PATH:
//
//     void SetUp() override {
//         eqClientPath_ = ...;
//         eqt::test::requireEqClientAssets(eqClientPath_);
//     }
//
// The check re-reads WILLEQ_REQUIRE_ASSETS on every call, so a test can set
// it with setenv() and exercise both outcomes (tests/test_asset_requirement.cpp
// pins exactly that).

#pragma once

#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace eqt::test {

// GTEST_SKIP (default) or hard FAIL (WILLEQ_REQUIRE_ASSETS=1) when the path
// cannot be opened. Callers that guard a single zone file keep their own
// per-file GTEST_SKIP; this helper only settles the root-level question, so a
// suite is either skipped as one unit (root absent) or runs (root present).
inline void requireEqClientAssets(const std::string& path) {
    std::ifstream probe(path);
    if (probe.good()) {
        return;
    }
    const std::string message =
        "EQ client asset root not found at: " + path +
        " (gitignored symlink -- make it in this worktree or set "
        "EQ_CLIENT_PATH; CI runs with WILLEQ_REQUIRE_ASSETS=1 where this "
        "is a failure, not a skip)";
    const char* required = std::getenv("WILLEQ_REQUIRE_ASSETS");
    if (required && std::string(required) == "1") {
        GTEST_FAIL() << message;  // hard failure; aborts the test
    } else {
        GTEST_SKIP() << message;  // aborts the test as skipped
    }
}

} // namespace eqt::test
