// T284: a composite sheet opens ONE pack, not the whole sheet directory.
//
// The finding (T281 Review): loadZoneStep0_EQE1 opened every .eqe1 in the
// --composite-entities directory. The shipped layout puts the sheet JSONs
// beside the packs in the ETC1 tree, so that scan mapped all 1506 packs /
// 4.44 GiB to reach the handful of sheets a zone's races use. On the 32-bit
// ARM Pi that scan was T195's address-space exhaustion (mmap, not RAM).
//
// That exhaustion is no longer the reason composite entities are off on the
// orangepi preset: T284 removed the scan and T289 measured VmPeak 3,128 MB ->
// 575 MB on the Pi. They stay off for their resident cost -- see
// docs/decisions.md 2026-08-27, "Composite entities stay OFF on the
// orangepi". Do not cite the T195/T198 rationale again.
//
// Two guards, at the two levels the fix lives at:
//
//   1. Index level (fast, no assets): every composite sheet pack in the
//      shipped index must be the SOLE owner of its own name, so
//      EQE1TextureIndex::resolve() can only ever name one pack for a sheet.
//      A name in no pack still resolves to nothing -- decision #2's fatal
//      path, pinned by test_eqe1_texture_index.cpp's
//      NameInNoArchiveResolvesNothing.
//
//   2. Integration level (shells out to gles2_zone_benchmark, the only x86
//      harness that drives the composite path without a server): a
//      single-race composite run must open a HANDFUL of sheet packs on
//      demand, not the directory. Against the pre-T284 scan this fails with
//      "Composite EQE1: 498 archives loaded"; after it, the run reports
//      "EQE1-COMPOSITE: 1 pack(s) opened on demand".
//
// Both skip cleanly without the NFS-hosted assets, same precedent as
// test_entity_viewer_regression.cpp and test_eqe1_texture_index.cpp.

#include <gtest/gtest.h>

#include "client/graphics/eqe1_texture_index.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using EQT::Graphics::EQE1TextureIndex;

namespace {

// P48's convention: hardcoded NFS path, fs::exists-gated.
const char* kEqe1Path = "/mnt/projects/EverQuest_ETC1";

bool isCompositeSheetPack(const std::string& stem) {
    return stem.find("_hs_def_") != std::string::npos ||
           stem.find("_actordef_") != std::string::npos;
}

// Same resolution order as test_entity_viewer_regression.cpp: env var, else
// the configure-time data/eq_client path, else nothing (-> skip).
std::string resolveEqClientPath() {
    if (const char* env = std::getenv("EQ_CLIENT_PATH")) {
        if (*env) return env;
    }
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

}  // namespace

// ---------------------------------------------------------------------------
// 1. Index level
// ---------------------------------------------------------------------------

// A composite sheet pack holds exactly one entry, named after the pack
// (entity_composite_builder --eqe1 emits one sheet per pack). That is what
// makes "resolve the name, open its pack" a ONE-pack answer with no directory
// scan and no ambiguity. If a future builder change starts sharing sheet names
// across packs, resolve() would start picking among owners and this pins that
// it has not happened silently.
TEST(Eqe1CompositeOpen, EverySheetNameHasExactlyOneOwner) {
    EQE1TextureIndex index;
    if (!index.load(EQE1_TEXTURE_INDEX_JSON)) {
        GTEST_SKIP() << "no shipped index at " << EQE1_TEXTURE_INDEX_JSON;
    }

    std::vector<std::string> sheets;
    for (size_t i = 0; i < index.archiveCount(); ++i) {
        const std::string& name = index.archiveName(static_cast<uint16_t>(i));
        if (isCompositeSheetPack(name)) sheets.push_back(name);
    }
    ASSERT_FALSE(sheets.empty())
        << "the shipped index names no *_hs_def_* / *_actordef_* pack -- T281 "
           "indexed 498 of them; rebuild data/eqe1_texture_index.json";

    size_t checked = 0, notSelfOwned = 0, multiOwner = 0;
    std::string firstBad;
    for (const auto& sheet : sheets) {
        EQE1TextureIndex::Resolution r;
        if (!index.resolve(sheet, {}, r)) {
            ++notSelfOwned;
            if (firstBad.empty()) firstBad = sheet + " (resolves to nothing)";
            continue;
        }
        if (r.archive != sheet) {
            ++notSelfOwned;
            if (firstBad.empty()) firstBad = sheet + " -> " + r.archive;
        }
        if (r.ownerCount != 1) {
            ++multiOwner;
            if (firstBad.empty())
                firstBad = sheet + " has " + std::to_string(r.ownerCount) + " owners";
        }
        ++checked;
    }

    EXPECT_EQ(notSelfOwned, 0u)
        << "a composite sheet name must name its own pack (first: " << firstBad << ")";
    EXPECT_EQ(multiOwner, 0u)
        << "a composite sheet name must have exactly one owner, so resolving it "
           "can open only one pack (first: " << firstBad << ")";
    EXPECT_GT(checked, 400u) << "expected ~498 sheet packs, checked " << checked;
}

// ---------------------------------------------------------------------------
// 2. Integration level
// ---------------------------------------------------------------------------

// One race, one composite sheet: the run must open a HANDFUL of sheet packs on
// demand, never the directory. The bound is deliberately loose (the point is
// 1-vs-498, not an exact count) but far below the 498 the scan opened.
TEST(Eqe1CompositeOpen, SingleRaceCompositeRunOpensOnePack) {
#ifndef GLES2_ZONE_BENCHMARK_BIN
    GTEST_SKIP() << "GLES2_ZONE_BENCHMARK_BIN not defined at configure time";
#else
    std::error_code ec;
    const std::string bin = GLES2_ZONE_BENCHMARK_BIN;
    if (!fs::exists(bin, ec)) GTEST_SKIP() << "gles2_zone_benchmark not built at " << bin;
    if (!fs::exists(kEqe1Path, ec)) GTEST_SKIP() << "no EQE1/composite tree at " << kEqe1Path;
    const std::string eqPath = resolveEqClientPath();
    if (!eqClientAvailable(eqPath))
        GTEST_SKIP() << "EQ client files not found at '" << eqPath
                     << "' (set EQ_CLIENT_PATH or symlink data/eq_client)";

    const std::string outDir = (fs::temp_directory_path() / "t284_composite_open").string();
    fs::create_directories(outDir, ec);

    std::string cmd = "\"" + bin + "\" --zone befallen --eq-path \"" + eqPath +
        "\" --entity-viewer --entity-viewer-race 1 --entity-viewer-race-label HUM"
        " --entity-viewer-frame 26 --entity-viewer-out-dir \"" + outDir + "\""
        " --eqe1-path \"" + std::string(kEqe1Path) + "\" --entity-viewer-composite"
        " --log-level=INFO 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "popen failed for: " << cmd;

    long opened = -1;          // T284 line
    long scanned = -1;         // pre-T284 line
    bool sawCompositeMesh = false;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        size_t p = line.find("EQE1-COMPOSITE: ");
        if (p != std::string::npos) opened = std::atol(line.c_str() + p + 16);
        p = line.find("Composite EQE1: ");
        if (p != std::string::npos) scanned = std::atol(line.c_str() + p + 16);
        if (line.find("buildCompositeMesh: built mesh") != std::string::npos)
            sawCompositeMesh = true;
    }
    pclose(pipe);

    ASSERT_GE(opened, 0)
        << "no 'EQE1-COMPOSITE: N pack(s) opened on demand' line -- the composite "
           "path still scans the sheet directory"
        << (scanned >= 0 ? " (it reported 'Composite EQE1: " + std::to_string(scanned) +
                           " archives loaded')" : "")
        << "\ncommand: " << cmd;

    EXPECT_LE(opened, 8)
        << "a single-race composite run opened " << opened << " pack(s); the "
           "index-driven open must name one pack per sheet, not scan the directory";
    EXPECT_GE(opened, 1) << "the composite sheet was never opened at all";

    // Guard against passing by simply not doing composites any more: the run
    // must still have built a composite mesh.
    EXPECT_TRUE(sawCompositeMesh)
        << "no buildCompositeMesh line -- the run did not take the composite path, "
           "so the open count above proves nothing";
#endif
}
