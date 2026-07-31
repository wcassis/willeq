/**
 * P47 entity-viewer regression pin.
 *
 * Shells out to gles2_zone_benchmark's --entity-viewer mode (see
 * tools/hwcaps/zone_bench_entity_viewer.h/.cpp,
 * refactor/P47_magenta_entity_viewer.md) and asserts, from its
 * ENTITY_VIEWER_RESULT stdout lines:
 *   - P37 ground-truth masked races (SKE/ZOM/BAT/ORC, real ETC1-companion
 *     alpha per refactor/P37_entity_texture_masked.md's ground-truth table)
 *     show a nonzero enclosed-magenta ("alpha gap") count at at least one of
 *     the four rendered angles.
 *   - The unmasked control (Human) shows EXACTLY zero at every angle.
 *
 * This makes P37/P46's masked-alpha rendering detectable-by-CI forever,
 * without needing a human to look at a picture (P44/P44b/P46/P46b/P46c/P46d
 * document five rejected rounds of trying to make that pixel-diff evidence
 * work; the magenta viewer is Will's own design replacing all of it).
 *
 * Requirements:
 * - EQ client files at EQ_CLIENT_PATH or data/eq_client symlink.
 * - gles2_zone_benchmark built (same tree -- GLES2_ZONE_BENCHMARK_BIN is
 *   set at configure time from $<TARGET_FILE:gles2_zone_benchmark>).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct RaceSpec {
    int id;
    const char* label;
    bool masked;
};

// P37 ground-truth masked races found in befallen (see
// refactor/P37_entity_texture_masked.md's ground-truth table:
// zomlg0001/batft0001/orclg0002/skelg0001 etc. all carry real ETC1
// companion alpha) plus race 1 (Human) as the unmasked control.
const RaceSpec kRaces[] = {
    {21, "SKE", true},
    {70, "ZOM", true},
    {34, "BAT", true},
    {17, "ORC", true},
    {1,  "HUM", false},
};

std::string resolveEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    if (env && *env) return env;
#ifdef EQ_CLIENT_DATA_PATH
    return EQ_CLIENT_DATA_PATH;
#else
    return "";
#endif
}

bool eqClientAvailable(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::exists(fs::path(path) / "gequip.s3d", ec);
}

// P48: result of one viewer invocation -- magenta counts plus whether the
// production composite eligibility gate actually ADMITTED this race (as
// opposed to falling back to the per-part path, which is ALSO alpha-correct
// since P46/P46d and would otherwise make the composite leg indistinguishable
// from a silent composite-path regression that still passes via fallback).
struct ViewerRunResult {
    std::vector<int> magentaValues;
    bool sawCompositeAdmitted = false;
    bool sawCompositeExcluded = false;
};

// Runs the viewer for one race and returns the magenta_in_bbox value parsed
// from each ENTITY_VIEWER_RESULT stdout line (one per rendered angle), plus
// (composite leg only, --log-level INFO) whether preloadModelData's
// composite eligibility gate admitted or excluded this race/sheet.
// P48: optional eqe1Path/composite params add the ETC1 and composite legs
// on top of the original RGBA-only signature (default args keep every
// pre-P48 call site source-identical).
ViewerRunResult runViewer(const std::string& binPath, const std::string& eqPath,
                           const std::string& outDir, const RaceSpec& race,
                           const std::string& eqe1Path = "", bool composite = false) {
    ViewerRunResult result;
    std::string cmd = "\"" + binPath + "\" --zone befallen --eq-path \"" + eqPath +
        "\" --entity-viewer --entity-viewer-race " + std::to_string(race.id) +
        " --entity-viewer-race-label " + race.label +
        " --entity-viewer-frame 26 --entity-viewer-out-dir \"" + outDir + "\"";
    if (!eqe1Path.empty()) {
        cmd += " --eqe1-path \"" + eqe1Path + "\"";
    }
    if (composite) {
        cmd += " --entity-viewer-composite --log-level=INFO";
    }
    cmd += " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;

    char buf[2048];
    const char* marker = "ENTITY_VIEWER_RESULT ";
    const char* magentaKey = "magenta_in_bbox=";
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        if (line.find("composite ADMITTED") != std::string::npos) {
            result.sawCompositeAdmitted = true;
        } else if (line.find("composite EXCLUDED") != std::string::npos) {
            result.sawCompositeExcluded = true;
        }
        if (line.find(marker) == std::string::npos) continue;
        size_t mpos = line.find(magentaKey);
        if (mpos == std::string::npos) continue;
        mpos += std::strlen(magentaKey);
        result.magentaValues.push_back(std::atoi(line.c_str() + mpos));
    }
    pclose(pipe);
    return result;
}

}  // namespace

// P48: NFS-hosted composite sheet directory -- same hardcoded-path convention
// test_eqe1_loader.cpp already uses for this NFS mount (no env var exists for
// it in this repo). Gated with fs::exists like every other NFS-dependent
// test; a missing mount SKIPs rather than fails.
const char* kEqe1Path = "/mnt/projects/EverQuest_ETC1";

class EntityViewerRegressionTest : public ::testing::Test {
protected:
    std::string binPath_;
    std::string eqPath_;
    std::string outDir_;

    void SetUp() override {
#ifndef GLES2_ZONE_BENCHMARK_BIN
        GTEST_SKIP() << "GLES2_ZONE_BENCHMARK_BIN not defined at configure time "
                        "(EQT_GRAPHICS off?)";
        return;
#else
        binPath_ = GLES2_ZONE_BENCHMARK_BIN;
        std::error_code ec;
        if (!fs::exists(binPath_, ec)) {
            GTEST_SKIP() << "gles2_zone_benchmark not built at " << binPath_;
            return;
        }
        eqPath_ = resolveEqClientPath();
        if (!eqClientAvailable(eqPath_)) {
            GTEST_SKIP() << "EQ client files not found at '" << eqPath_
                         << "' (set EQ_CLIENT_PATH or symlink data/eq_client)";
            return;
        }
        outDir_ = (fs::temp_directory_path() / "p47_entity_viewer_regression").string();
        fs::create_directories(outDir_, ec);
#endif
    }
};

// P48: composite leg -- same races, same reading rule, but routes the
// entity through the REAL composite eligibility gate
// (race_model_loader.cpp's sheet-capability check) via --eqe1-path +
// --entity-viewer-composite instead of the plain per-part legs
// MaskedRacesShowAlphaGapHumanDoesNot already covers. Masked races (SKE/
// ZOM/BAT/ORC) all got a real ETC1 alpha companion page in their composite
// sheet this unit (entity_composite_builder.cpp); Human's composite sheet
// has none (unmasked) and must stay exactly zero, matching the per-part
// control's semantics -- a regression here means either the eligibility
// gate stopped admitting masked-but-alpha-capable sheets, or
// mesh_building.cpp's buildCompositeMeshFromGeometry() stopped honoring
// CompositeTextureData::hasAlpha.
TEST_F(EntityViewerRegressionTest, CompositeLegMaskedRacesShowAlphaGapHumanDoesNot) {
    std::error_code ec;
    if (!fs::exists(kEqe1Path, ec)) {
        GTEST_SKIP() << "Composite sheet directory not found at '" << kEqe1Path
                     << "' (NFS mount unavailable)";
        return;
    }
    for (const auto& race : kRaces) {
        SCOPED_TRACE(race.label);
        ViewerRunResult run =
            runViewer(binPath_, eqPath_, outDir_, race, kEqe1Path, /*composite=*/true);
        const auto& values = run.magentaValues;
        ASSERT_FALSE(values.empty())
            << "no ENTITY_VIEWER_RESULT lines parsed for composite-leg race " << race.label
            << " (viewer crashed or produced no output -- rerun manually with "
            << binPath_ << " --entity-viewer --entity-viewer-composite --entity-viewer-race "
            << race.id << " --eq-path " << eqPath_ << " --eqe1-path " << kEqe1Path
            << " --entity-viewer-out-dir /tmp to see why)";

        if (race.masked) {
            // Discriminate composite-path alpha from the per-part FALLBACK path
            // (which is also alpha-correct per P46/P46d, and would otherwise make
            // this leg pass even if the composite eligibility gate silently
            // reverted to always-excluding masked races -- see
            // race_model_loader.cpp's "composite ADMITTED"/"composite EXCLUDED"
            // log lines, both added by this unit).
            EXPECT_TRUE(run.sawCompositeAdmitted)
                << race.label << " (composite leg): expected race_model_loader.cpp's "
                   "eligibility gate to log 'composite ADMITTED' (this race's sheet "
                   "was regenerated with a real alpha companion, P48) -- if it logged "
                   "'composite EXCLUDED' instead (sawCompositeExcluded="
                << run.sawCompositeExcluded << "), the deployed sheet lost its alpha "
                   "companion or the gate itself regressed, and the magenta signal "
                   "below (if nonzero) is coming from the FALLBACK path, not composite";
            int maxV = *std::max_element(values.begin(), values.end());
            EXPECT_GT(maxV, 0)
                << race.label << " (composite leg): expected at least one of the "
                << values.size() << " rendered angles to show a nonzero enclosed-magenta "
                   "alpha gap -- this race's composite sheet was regenerated with a real "
                   "ETC1 alpha companion (P48); got all zeros, which means the composite "
                   "eligibility gate or buildCompositeMeshFromGeometry()'s hasAlpha-gated "
                   "MaterialType regressed";
        } else {
            for (size_t i = 0; i < values.size(); ++i) {
                EXPECT_EQ(values[i], 0)
                    << race.label << " (composite leg): unmasked control must show "
                       "EXACTLY zero enclosed magenta at every angle (angle index " << i
                    << ") when composited -- a nonzero count here means an unmasked "
                       "composite sheet is incorrectly getting alpha-tested";
            }
        }
    }
}

TEST_F(EntityViewerRegressionTest, MaskedRacesShowAlphaGapHumanDoesNot) {
    for (const auto& race : kRaces) {
        SCOPED_TRACE(race.label);
        ViewerRunResult run = runViewer(binPath_, eqPath_, outDir_, race);
        const auto& values = run.magentaValues;
        ASSERT_FALSE(values.empty())
            << "no ENTITY_VIEWER_RESULT lines parsed for race " << race.label
            << " (viewer crashed or produced no output -- rerun manually with "
            << binPath_ << " --entity-viewer --entity-viewer-race " << race.id
            << " --eq-path " << eqPath_ << " --entity-viewer-out-dir /tmp to see why)";

        if (race.masked) {
            int maxV = *std::max_element(values.begin(), values.end());
            EXPECT_GT(maxV, 0)
                << race.label << ": expected at least one of the "
                << values.size() << " rendered angles to show a nonzero "
                   "enclosed-magenta alpha gap (P37 ground truth: masked, "
                   "real ETC1 companion alpha) -- got all zeros, which means "
                   "P37/P46's masked-alpha rendering regressed";
        } else {
            for (size_t i = 0; i < values.size(); ++i) {
                EXPECT_EQ(values[i], 0)
                    << race.label << ": unmasked control must show EXACTLY "
                       "zero enclosed magenta at every angle (angle index "
                    << i << ") -- a nonzero count here means either a real "
                       "regression (something is now discarding pixels on an "
                       "unmasked race) or the viewer's connected-component "
                       "noise floor (zone_bench_entity_viewer.cpp's "
                       "kMinHoleAreaPx) needs revisiting";
            }
        }
    }
}
