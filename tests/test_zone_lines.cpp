// T19: zone_lines.cpp (EQT::ZoneLines JSON zone-line parser) unit tests.
//
// Two independent JSON schemas are parsed by this file (see
// src/client/zone_lines.cpp Study-first notes in refactor/T19_map_zone_lines.md):
//
//  1. Zone-specific format (data/zone_lines/<zone>.json, preferred):
//       { "zone": "<name>", "zone_lines": [
//           { "target_zone": "<name>", "bounds": {min_x,max_x,min_y,max_y,
//             min_z,max_z}, "landing": "fixed"|"preserve_x"|"preserve_y"|
//             "preserve_z", ... }, ... ] }
//     Entries WITHOUT a "bounds" member are silently skipped (not yet
//     mapped in the zone-line editor). target_zone is resolved to a
//     zone ID via a hardcoded ZONE_NAME_TO_ID table in zone_lines.cpp;
//     unrecognized names are skipped with a warning, not an error.
//
//  2. Legacy pre-extracted format (data/zone_lines.json, root-level,
//     fallback only): { "<zone>": { "zone_lines": [ { "zone_point_index",
//     "destination_zone", "destination_zone_id", "trigger_box": {min_x,
//     max_x,min_y,max_y,min_z,max_z}, "destination": {x,y,z,heading} } ] } }
//     No coordinate_transform member exists in any real zone_lines.json
//     entry as of this unit (grep confirmed) -- the BSP-to-world transform
//     branch in loadFromExtractedJson is currently dead code on real data.
//
// ZoneLines::loadFromZone() tries the zone-specific path first (3 candidate
// paths, CWD- and eqClientPath-relative), then falls back to the legacy
// format (also 3 candidate paths). This file drives the two format parsers
// directly with absolute paths (WILLEQ_REPO_ROOT, set at configure time) to
// stay independent of ctest's working directory, plus one test that
// exercises the eqClientPath-relative candidate loadFromZone() builds.
//
// Fixtures used: data/zone_lines/qeynos2.json (has 7 entries, 6 with
// "bounds" + 1 CLICK-type door entry without bounds -- a real-data example
// of the skip-if-no-bounds path) and data/zone_lines.json (legacy format,
// "akanon" entry). Both are checked-in repo files, not external fixtures.

#include <gtest/gtest.h>
#include "client/zone_lines.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

using namespace EQT;

namespace {

std::string RepoPath(const std::string& rel) {
    return std::string(WILLEQ_REPO_ROOT) + "/" + rel;
}

std::string TempJsonPath(const std::string& tag) {
    return "/tmp/t19_zone_lines_" + tag + "_" + std::to_string(getpid()) + ".json";
}

std::string WriteTempJson(const std::string& tag, const std::string& contents) {
    std::string path = TempJsonPath(tag);
    std::ofstream f(path, std::ios::trunc);
    f << contents;
    return path;
}

} // namespace

class ZoneLinesTempFileTest : public ::testing::Test {
protected:
    std::vector<std::string> filesToClean_;

    std::string WriteJson(const std::string& tag, const std::string& contents) {
        std::string path = WriteTempJson(tag, contents);
        filesToClean_.push_back(path);
        return path;
    }

    void TearDown() override {
        for (auto& p : filesToClean_) ::remove(p.c_str());
    }
};

// ============================================================================
// 1. Real fixtures: zone-specific format happy path (data/zone_lines/*.json).
// ============================================================================

TEST(ZoneLinesRealFixtures, Qeynos2_LoadsSixOfSevenEntries) {
    // qeynos2.json has 7 "zone_lines" entries; the 7th (a CLICK-type door
    // to poknowledge) has no "bounds" member and is skipped by
    // loadFromZoneSpecificJson's `if (!zl.isMember("bounds")) continue;`
    // guard -- pinned via discovery run, 2026-07-31.
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromZoneSpecificJson("qeynos2", RepoPath("data/zone_lines/qeynos2.json")));
    EXPECT_EQ(zl.getZoneLineCount(), 6u);
    EXPECT_TRUE(zl.hasZoneLines());
    EXPECT_TRUE(zl.hasExtractedZoneLines());
}

TEST(ZoneLinesRealFixtures, Qeynos2_PinnedFirstBoxToQcat) {
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromZoneSpecificJson("qeynos2", RepoPath("data/zone_lines/qeynos2.json")));
    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_GE(boxes.size(), 1u);

    // First entry in qeynos2.json: target_zone "qcat" (zone ID 45 in the
    // ZONE_NAME_TO_ID table), landing "fixed". Values pinned from the
    // checked-in JSON's raw floats.
    const auto& b = boxes[0];
    EXPECT_EQ(b.targetZoneId, 45u);
    EXPECT_FALSE(b.isProximityBased);
    EXPECT_NEAR(b.minX, 180.7799072265625, 0.0001);
    EXPECT_NEAR(b.maxX, 196.87771606445312, 0.0001);
    EXPECT_NEAR(b.minY, 335.30398559570312, 0.0001);
    EXPECT_NEAR(b.maxY, 350.39566040039062, 0.0001);
    EXPECT_NEAR(b.minZ, -45.4375, 0.0001);
    EXPECT_NEAR(b.maxZ, -25.4375, 0.0001);
}

TEST(ZoneLinesRealFixtures, Qeynos2_PreserveXLandingResolvesToCurrentPosition) {
    // qeynos2.json's 6th bounded entry targets qeytoqrg (zone ID 4) with
    // landing "preserve_x" -- ezl.destX is set to the SAME_COORD_MARKER
    // sentinel (999999.0f) at parse time; checkExtractedZoneLines()
    // resolves that marker to the caller-supplied currentX at query time.
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromZoneSpecificJson("qeynos2", RepoPath("data/zone_lines/qeynos2.json")));

    // Box 6 (index 5): (1755.9130,-1119.2930,0.0000)-(1767.2142,1129.6768,30.0000).
    // Query a point inside it, with a distinguishable "current position".
    float qx = 1760.0f, qy = 0.0f, qz = 15.0f;
    float curX = 42.0f, curY = 77.0f, curZ = -3.0f;
    ZoneLineResult result = zl.checkPosition(qx, qy, qz, curX, curY, curZ);

    ASSERT_TRUE(result.isZoneLine);
    EXPECT_EQ(result.targetZoneId, 4u); // qeytoqrg
    EXPECT_FLOAT_EQ(result.targetX, curX); // preserve_x -> caller's currentX
    EXPECT_FLOAT_EQ(result.targetY, 0.0f); // fixed 0 per loadFromZoneSpecificJson
    EXPECT_FLOAT_EQ(result.targetZ, 0.0f);
}

TEST(ZoneLinesRealFixtures, Qeynos2_PositionOutsideAllBoxesReportsNoZoneLine) {
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromZoneSpecificJson("qeynos2", RepoPath("data/zone_lines/qeynos2.json")));

    ZoneLineResult result = zl.checkPosition(0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(result.isZoneLine);

    auto movement = zl.checkMovement(0, 0, 0, 1, 1, 1);
    EXPECT_FALSE(movement.has_value());
}

TEST(ZoneLinesRealFixtures, Befallen_AllEntriesLackBoundsSoLoadFails) {
    // befallen.json (checked in) has 2 zone_lines entries, neither with a
    // "bounds" member (only "trigger_coords" -- not yet mapped in the
    // editor). loadFromZoneSpecificJson skips both, leaving
    // extractedZoneLines_ empty, so the function returns false even though
    // the JSON itself parses fine and the zone name matches. This is
    // expected/documented behavior, not a bug -- flagged here so a future
    // change to befallen.json's bounds doesn't silently break this
    // assumption unnoticed.
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("befallen", RepoPath("data/zone_lines/befallen.json")));
    EXPECT_EQ(zl.getZoneLineCount(), 0u);
}

// ============================================================================
// 2. Real fixtures: legacy pre-extracted format happy path (data/zone_lines.json).
// ============================================================================

TEST(ZoneLinesRealFixtures, LegacyFormat_AkanonPinnedFields) {
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromExtractedJson("akanon", RepoPath("data/zone_lines.json")));
    EXPECT_EQ(zl.getZoneLineCount(), 1u);

    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_EQ(boxes.size(), 1u);
    const auto& b = boxes[0];
    EXPECT_EQ(b.targetZoneId, 56u); // steamfont
    EXPECT_EQ(b.zonePointIndex, 1000u);
    EXPECT_FLOAT_EQ(b.minX, 51.5f);
    EXPECT_FLOAT_EQ(b.maxX, 56.5f);
    EXPECT_FLOAT_EQ(b.minY, -79.5f);
    EXPECT_FLOAT_EQ(b.maxY, -74.5f);
    EXPECT_FLOAT_EQ(b.minZ, -26.0f);
    EXPECT_FLOAT_EQ(b.maxZ, 34.0f);

    // No coordinate_transform in this real entry -- raw trigger_box values
    // pass through unmodified (hasTransform=false branch in
    // loadFromExtractedJson).
    ZoneLineResult result = zl.checkPosition(54.0f, -77.0f, 0.0f);
    ASSERT_TRUE(result.isZoneLine);
    EXPECT_EQ(result.targetZoneId, 56u);
    EXPECT_FLOAT_EQ(result.targetX, 528.0f);
    EXPECT_FLOAT_EQ(result.targetY, -2062.0f);
    EXPECT_FLOAT_EQ(result.targetZ, -110.0f);
    EXPECT_FLOAT_EQ(result.heading, 66.0f);
}

TEST(ZoneLinesRealFixtures, LegacyFormat_UnknownZoneReturnsFalse) {
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromExtractedJson("totally_not_a_real_zone_xyz", RepoPath("data/zone_lines.json")));
    EXPECT_EQ(zl.getZoneLineCount(), 0u);
}

// ============================================================================
// 3. loadFromZone(): path-probing / fallback-chain behavior.
// ============================================================================

TEST(ZoneLinesRealFixtures, LoadFromZone_EqClientPathRelativeCandidate) {
    // loadFromZone() tries `eqClientPath + "/../data/zone_lines/<zone>.json"`
    // as its 3rd zone-specific candidate. fopen()/the OS resolve ".."
    // lexically THROUGH the real filesystem -- the intermediate directory
    // component must actually exist, or the whole open fails with ENOENT
    // even though the final target path is "obviously" reachable on paper
    // (confirmed the hard way: an earlier version of this test used a
    // nonexistent "fake_eq_client_subdir" component and failed everywhere
    // except when ctest's CWD happened to be the repo root, where the
    // CWD-relative 1st candidate masked the bug). Use "tests" (always
    // exists) as the real intermediate directory so this test is
    // independent of ctest's working directory and actually exercises the
    // 3rd/eqClientPath-relative candidate rather than accidentally passing
    // via the 1st.
    std::string fakeEqClientPath = RepoPath("tests");
    ZoneLines zl;
    EXPECT_TRUE(zl.loadFromZone("qeynos2", fakeEqClientPath));
    EXPECT_EQ(zl.getZoneLineCount(), 6u);
}

TEST(ZoneLinesRealFixtures, LoadFromZone_UnknownZoneAndBadPath_ReturnsFalse) {
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZone("zone_that_does_not_exist_anywhere", "/nonexistent/eq/client/path"));
    EXPECT_EQ(zl.getZoneLineCount(), 0u);
}

// ============================================================================
// 4. Synthetic malformed input.
// ============================================================================

TEST_F(ZoneLinesTempFileTest, ExtractedFormat_RootIsArrayNotObject_FailsGracefully) {
    std::string path = WriteJson("root_array", "[1,2,3]");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromExtractedJson("anyzone", path));
}

TEST_F(ZoneLinesTempFileTest, ExtractedFormat_ZoneLinesNotArray_FailsGracefully) {
    std::string path = WriteJson("zl_not_array", R"({"anyzone": {"zone_lines": "notanarray"}})");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromExtractedJson("anyzone", path));
}

TEST_F(ZoneLinesTempFileTest, ExtractedFormat_MissingTriggerBoxAndDestination_DefaultsToZero) {
    // No crash, but silently produces a degenerate all-zero box -- the
    // parser doesn't validate that trigger_box/destination are present at
    // all (both are optional per `if (zl.isMember(...))` guards).
    std::string path = WriteJson("minimal_entry",
        R"({"anyzone": {"zone_lines": [{"zone_point_index": 5, "destination_zone": "foo", "destination_zone_id": 9}]}})");
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromExtractedJson("anyzone", path));
    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_FLOAT_EQ(boxes[0].minX, 0.0f);
    EXPECT_FLOAT_EQ(boxes[0].maxX, 0.0f);
    EXPECT_EQ(boxes[0].targetZoneId, 9u);
}

TEST_F(ZoneLinesTempFileTest, SpecificFormat_UnknownTargetZone_SkippedGracefully) {
    std::string path = WriteJson("unknown_zone",
        R"({"zone": "unk", "zone_lines": [{"target_zone": "totallymadeupzonexyz",
            "bounds": {"min_x":0,"max_x":1,"min_y":0,"max_y":1,"min_z":0,"max_z":1},
            "landing":"fixed"}]})");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("unk", path));
    EXPECT_EQ(zl.getZoneLineCount(), 0u);
}

TEST_F(ZoneLinesTempFileTest, SpecificFormat_ZoneNameMismatch_FailsGracefully) {
    std::string path = WriteJson("zone_mismatch",
        R"({"zone": "qeynos2", "zone_lines": []})");
    ZoneLines zl;
    // Asking for a different zone than the file declares -> mismatch guard
    // rejects it even though the file parses fine.
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("befallen", path));
}

TEST_F(ZoneLinesTempFileTest, SpecificFormat_MissingBoundsFields_DefaultToZero) {
    // Only min_x/max_x present; min_y/max_y/min_z/max_z absent. jsoncpp's
    // operator[] returns a null Json::Value for a missing key, and
    // Json::Value::asFloat() on null returns 0.0 rather than throwing --
    // so this loads "successfully" with a degenerate (partially zeroed)
    // box instead of erroring. Documented behavior, not a crash.
    std::string path = WriteJson("missing_bounds_fields",
        R"({"zone": "badbounds", "zone_lines": [{"target_zone": "commons",
            "bounds": {"min_x": 0.0, "max_x": 10.0}, "landing": "fixed"}]})");
    ZoneLines zl;
    ASSERT_TRUE(zl.loadFromZoneSpecificJson("badbounds", path));
    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_FLOAT_EQ(boxes[0].minX, 0.0f);
    EXPECT_FLOAT_EQ(boxes[0].maxX, 10.0f);
    EXPECT_FLOAT_EQ(boxes[0].minY, 0.0f); // defaulted, not an error
    EXPECT_FLOAT_EQ(boxes[0].maxY, 0.0f); // defaulted, not an error
}

TEST_F(ZoneLinesTempFileTest, EmptyFile_FailsGracefully) {
    std::string path = WriteJson("empty", "");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("anyzone", path));
    EXPECT_FALSE(zl.loadFromExtractedJson("anyzone", path));
}

TEST_F(ZoneLinesTempFileTest, NonJsonGarbage_FailsGracefully) {
    std::string path = WriteJson("garbage", "this is not json {{{ at all");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("anyzone", path));
    EXPECT_FALSE(zl.loadFromExtractedJson("anyzone", path));
}

TEST_F(ZoneLinesTempFileTest, NonexistentFile_FailsGracefully) {
    std::string path = "/tmp/t19_zone_lines_does_not_exist_" + std::to_string(getpid()) + ".json";
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("anyzone", path));
    EXPECT_FALSE(zl.loadFromExtractedJson("anyzone", path));
}

TEST_F(ZoneLinesTempFileTest, EmptyZoneLinesArray_FailsGracefully) {
    std::string path = WriteJson("empty_array", R"({"zone": "empty", "zone_lines": []})");
    ZoneLines zl;
    EXPECT_FALSE(zl.loadFromZoneSpecificJson("empty", path));
    EXPECT_EQ(zl.getZoneLineCount(), 0u);
}

// ============================================================================
// 5. Wrong-typed JSON fields -- was an uncaught throw (T19 finding), fixed
//    in T20.
// ============================================================================

// Both loadFromZoneSpecificJson() and loadFromExtractedJson() call
// Json::Value::asFloat() directly on bounds/trigger_box/destination
// sub-fields with no isNumeric()/isConvertibleTo() guard. jsoncpp throws
// Json::LogicError ("Value is not convertible to float.") when asFloat()
// is called on a non-numeric JSON value (e.g. a string). Neither function
// wraps these calls in a try/catch, so a single malformed field in an
// otherwise well-formed zone-lines JSON file (wrong type, not just a
// missing key -- contrast with the MissingBoundsFields test above, which
// is safe) throws an exception straight out of the parser. Nothing further
// up the real call chain (ZoneLines::loadFromZone(), or its callers) wraps
// this in a try/catch either, so in production this crashes the whole
// process via std::terminate the first time a malformed float sneaks into
// a zone-lines JSON file.
//
// Reproduced standalone outside gtest first (uncaught Json::LogicError ->
// std::terminate -> SIGABRT, discovery run 2026-07-31). Pinned here with
// ASSERT_THROW rather than ASSERT_DEATH: googletest's death-test harness
// installs its own top-level exception barrier around the forked child's
// statement (to produce a clean "threw an exception" diagnostic instead of
// a raw core dump), so the exception never reaches a real signal inside
// the child and ASSERT_DEATH's regex never gets to match -- confirmed by
// running it first and observing exactly that "Result: threw an
// exception" failure mode instead of a signal death. ASSERT_THROW pins the
// same defect (uncaught exception escapes the parser) without depending on
// that harness's signal-vs-exception distinction.
TEST_F(ZoneLinesTempFileTest, SpecificFormat_NonNumericBoundsFieldFallsBackWithoutThrowing) {
    // T20: the bad field takes the 0.0f fallback (with a warning) and the
    // rest of the entry still loads -- one typo no longer aborts the client.
    std::string path = WriteJson("bad_type_specific",
        R"({"zone": "badtypes", "zone_lines": [{"target_zone": "commons",
            "bounds": {"min_x": "not_a_number", "max_x": 10.0, "min_y": 0.0,
                       "max_y": 10.0, "min_z": 0.0, "max_z": 10.0},
            "landing": "fixed"}]})");

    ZoneLines zl;
    EXPECT_NO_THROW({
        EXPECT_TRUE(zl.loadFromZoneSpecificJson("badtypes", path));
    });
    ASSERT_EQ(zl.getZoneLineCount(), 1u);
    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_FLOAT_EQ(boxes[0].minX, 0.0f);   // the malformed field
    EXPECT_FLOAT_EQ(boxes[0].maxX, 10.0f);  // well-formed neighbours survive
    EXPECT_FLOAT_EQ(boxes[0].maxY, 10.0f);
}

TEST_F(ZoneLinesTempFileTest, ExtractedFormat_NonNumericTriggerBoxFieldFallsBackWithoutThrowing) {
    // Same fix verified independently on loadFromExtractedJson's
    // trigger_box path.
    std::string path = WriteJson("bad_type_extracted",
        R"({"anyzone": {"zone_lines": [{"zone_point_index": 5, "destination_zone": "foo",
            "destination_zone_id": 9, "trigger_box": {"min_x":"bad","max_x":1,
            "min_y":0,"max_y":1,"min_z":0,"max_z":1}}]}})");

    ZoneLines zl;
    EXPECT_NO_THROW({
        EXPECT_TRUE(zl.loadFromExtractedJson("anyzone", path));
    });
    ASSERT_EQ(zl.getZoneLineCount(), 1u);
    auto boxes = zl.getZoneLineBoundingBoxes();
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_FLOAT_EQ(boxes[0].minX, 0.0f);
    EXPECT_FLOAT_EQ(boxes[0].maxX, 1.0f);
}

TEST_F(ZoneLinesTempFileTest, WrongTypedContainerStillFailsCleanly) {
    // A structurally hostile shape (trigger_box is a string, not an object)
    // must also fail without escaping an exception -- this is the case the
    // per-field helpers can't cover, so it exercises the loader's catch.
    std::string path = WriteJson("bad_container",
        R"({"anyzone": {"zone_lines": [{"zone_point_index": 1, "destination_zone": "foo",
            "destination_zone_id": 9, "trigger_box": "not_an_object"}]}})");

    ZoneLines zl;
    EXPECT_NO_THROW(zl.loadFromExtractedJson("anyzone", path));
}
