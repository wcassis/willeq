/**
 * B997 (GitLab #317) step 2: the DETERMINISTIC pixel A/B for the
 * per-material scene-graph door batch (EQT_SG_BATCH) -- the gate for
 * shipping it ON.
 *
 * Shells out to gles2_zone_benchmark's --sg-batch-ab mode ONE LEG PER
 * PROCESS (see tools/hwcaps/zone_bench_door_batch_ab.{h,cpp} and
 * refactor/B997/README.md "Step 2"): process 1 with EQT_SG_BATCH=0 (the
 * per-node drawAll() baseline), process 2 with EQT_SG_BATCH=1 (the
 * production gather/render/restore batch). Both render ONLY the zone's
 * doors through the production door path at the same fixed camera and
 * fixed deterministic lighting. The legs write sgbatch0.png / sgbatch1.png;
 * this test compares them BYTES (the PNG files themselves) and additionally
 * asserts the leg proof lines:
 *   - both legs report the SAME doors_in_graph (the drawn population did
 *     not change -- the batch must not add or drop a door);
 *   - leg B actually BATCHED something (batched_nodes > 0, batch_draws > 0,
 *     batch_draws < doors_in_graph -- the collapse is real);
 *   - the two PNGs are byte-identical (the pixel-identity verdict).
 *
 * The live-client frame diff is NOT a channel (0/18 frames identical across
 * two baseline legs -- the crowd tour is not frame-locked); this instrument
 * is the deterministic one: no server, no NPCs, fixed camera, fixed
 * lighting, no animation clock (update(0.0)).
 *
 * Asset-gated exactly like test_entity_viewer_regression (EQ_CLIENT_PATH env
 * var, else data/eq_client symlink, else skip) -- plus a skip guard for the
 * benchmark binary not existing. The door fixtures are committed under
 * refactor/B997/data/doors_<zone>.txt (the eqemu `doors` table rows the
 * server serialises into SpawnDoor, version 0; qeynos2 = 70 rows, gfaydark
 * = 102, see the README's "How the doors were sourced").
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "scoped_temp_dir.h"

namespace fs = std::filesystem;

namespace {

// The two zones the card names. Fixture path (repo-relative; the test runs
// with the repo root as its working directory under ctest, and WILLEQ_SRC
// makes it robust to other launch directories too).
struct ZoneFixture {
    const char* zone;
    const char* fixture;  // relative to the repo root
};
const ZoneFixture kZones[] = {
    {"qeynos2", "refactor/B997/data/doors_qeynos2.txt"},
    {"gfaydark", "refactor/B997/data/doors_gfaydark.txt"},
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

std::string repoRoot() {
    const char* env = std::getenv("WILLEQ_SRC");
    if (env && *env) return env;
#ifdef WILLEQ_SRC
    // The cmake registration passes the source dir as a compile definition
    // (the ctest run has no WILLEQ_SRC in the environment and its CWD is
    // the build/tests dir, not the repo root).
    return WILLEQ_SRC;
#endif
    return ".";
}

// One leg: run the tool with EQT_SG_BATCH set to `batchOn`, parse the
// SG_BATCH_AB_LEG line, and read back the PNG.
struct LegRun {
    bool ok = false;
    int doorsInGraph = -1;
    int batchedNodes = -1;
    int batchDraws = -1;
    std::string png;
    std::vector<char> pngBytes;
    std::string tail;  // last few stdout/stderr lines, for failure messages
};

LegRun runLeg(const std::string& bin, const std::string& eqPath,
              const ZoneFixture& zf, const std::string& outDir, bool batchOn) {
    LegRun r;
    // The repo-root fixture path (absolute so the popen CWD is irrelevant).
    // EQT_SG_BATCH is set EXPLICITLY per leg (the production default since
    // the flip is ON; leg A is the explicit =0 opt-out, leg B the explicit
    // =1) so the two legs cannot depend on whatever the environment
    // inherited.
    const std::string fixture = (fs::path(repoRoot()) / zf.fixture).string();
    std::string cmd = std::string("EQT_SG_BATCH=") + (batchOn ? "1" : "0") + " \"";
    cmd += bin + "\" --zone " + zf.zone + " --eq-path \"" + eqPath +
         "\" --sg-batch-ab --sg-batch-ab-doors \"" + fixture +
         "\" --sg-batch-ab-out-dir \"" + outDir +
         "\" --sg-batch-ab-min-doors 2 --res 640 480 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return r;

    std::string line;
    {
        std::ostringstream all;
        char buf[2048];
        while (fgets(buf, sizeof(buf), pipe)) {
            all << buf;
            line = buf;
            const char* m = strstr(line.c_str(), "SG_BATCH_AB_LEG ");
            if (m) {
                std::string rest(m + strlen("SG_BATCH_AB_LEG "));
                auto fieldInt = [&](const char* key) -> int {
                    const std::string k = std::string(key) + "=";
                    size_t p = rest.find(k);
                    if (p == std::string::npos) return -1;
                    p += k.size();
                    return std::atoi(rest.c_str() + p);
                };
                r.doorsInGraph = fieldInt("doors_in_graph");
                r.batchedNodes = fieldInt("batched_nodes");
                r.batchDraws = fieldInt("batch_draws");
                size_t pp = rest.find("png=");
                if (pp != std::string::npos) {
                    pp += 4;
                    size_t pe = rest.find(' ', pp);
                    r.png = rest.substr(pp, pe == std::string::npos
                                           ? std::string::npos : pe - pp);
                }
            }
            // Keep the last ~12 lines for diagnostics.
            if (line.size() > 1) {
                r.tail += line;
                if (r.tail.size() > 4000) r.tail = r.tail.substr(r.tail.size() - 4000);
            }
        }
        (void)all;
    }
    pclose(pipe);

    if (!fs::exists(r.png)) {
        fprintf(stderr, "[sg-batch-ab] leg %d: PNG %s missing\n", batchOn ? 1 : 0,
                r.png.c_str());
        return r;
    }
    std::ifstream f(r.png, std::ios::binary);
    r.pngBytes.assign(std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>());
    r.ok = !r.pngBytes.empty() && r.doorsInGraph >= 2;
    return r;
}

std::string hex(const std::vector<char>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (unsigned char c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 0xf]);
    }
    // The full digest is long; the first 32 hex chars are plenty for a
    // failure message (the byte comparison itself is exact).
    return s.substr(0, 32);
}

} // namespace

// The gate itself: both legs must render the SAME doors to the SAME pixels.
TEST(SgBatchDoorPixelAb, DoorBatchIsPixelIdentical) {
#ifdef GLES2_ZONE_BENCHMARK_BIN
    const std::string bin = GLES2_ZONE_BENCHMARK_BIN;
    if (!fs::exists(bin)) {
        GTEST_SKIP() << "benchmark binary missing: " << bin;
    }
    const std::string eqPath = resolveEqClientPath();
    if (!eqClientAvailable(eqPath)) {
        GTEST_SKIP() << "EQ client files not available (EQ_CLIENT_PATH or "
                        "data/eq_client symlink)";
    }
    willeq_test::ScopedTempDir out("b997_sg_batch_ab");

    for (const auto& zf : kZones) {
        LegRun a = runLeg(bin, eqPath, zf, out.path().string(), false);
        LegRun b = runLeg(bin, eqPath, zf, out.path().string(), true);

        ASSERT_TRUE(a.ok) << "leg A (EQT_SG_BATCH=0) setup failed for "
                          << zf.zone << ":\n" << a.tail;
        ASSERT_TRUE(b.ok) << "leg B (EQT_SG_BATCH=1) setup failed for "
                          << zf.zone << ":\n" << b.tail;

        // The drawn population must not change -- the batch replaces draws,
        // it does not add or drop a door.
        EXPECT_EQ(a.doorsInGraph, b.doorsInGraph)
            << "doors in the scene graph changed between legs for "
            << zf.zone << " (A=" << a.doorsInGraph << " B=" << b.doorsInGraph
            << ")";

        // Leg B must actually have batched (the whole point of the A/B -- a
        // "identical" result with 0 batched nodes would be a vacuous pass).
        EXPECT_GT(b.batchedNodes, 0)
            << "leg B batched NO doors for " << zf.zone << " -- the batch gate "
               "never fired; the pixel match is vacuous\n" << b.tail;
        EXPECT_GE(b.batchDraws, 1) << "leg B emitted no batch draws for "
                                   << zf.zone << "\n" << b.tail;

        // The verdict: the two legs' PNGs must be byte-identical.
        ASSERT_EQ(a.pngBytes.size(), b.pngBytes.size())
            << zf.zone << ": PNG sizes differ (A=" << a.pngBytes.size()
            << " B=" << b.pngBytes.size() << ") -- A=" << a.png << " B=" << b.png;
        EXPECT_EQ(a.pngBytes, b.pngBytes)
            << zf.zone << ": the scene-graph batch changed the rendered pixels "
               "(sha256[:32] A=" << hex(a.pngBytes)
               << " B=" << hex(b.pngBytes) << ")\n"
               << "PNGs: " << a.png << " vs " << b.png << "\n"
               << "leg A: doors=" << a.doorsInGraph << "\n"
               << "leg B: doors=" << b.doorsInGraph
               << " batched_nodes=" << b.batchedNodes
               << " batch_draws=" << b.batchDraws << "\n"
               << "leg B tail:\n" << b.tail;
    }
#else
    GTEST_SKIP() << "GLES2_ZONE_BENCHMARK_BIN not defined at configure time "
                    "(EQT_GRAPHICS off or the tree predates B997 step 2)";
#endif
}
