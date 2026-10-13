// B1017 (#338), criteria (d)+(e): a test that REACHES THE STALE-PACK FATAL
// through the real miss path, and a mixed-case request that still gets the
// diagnosis.
//
// Why a new target: the B930 leg test_eqe1_stale_pack.cpp drives
// diagnoseStaleEQE1Pack() directly and then _exit(1)s unconditionally --
// its child never calls ConstrainedTextureCache::getOrLoad(), so deleting
// the diagnoseStaleEQE1Pack() CALL at
// constrained_texture_cache.cpp (the one the FATAL sits behind) leaves
// every existing test green. That was the audit's finding 7. This target
// links willeq_core (which carries constrained_texture_cache.cpp +
// constrained_texture_cache_compressed.cpp + eqe1_loader.cpp, the same TU
// set the client links) and drives the REAL miss: getOrLoad(fullName, {})
// on a cache that has one open v1 pack holding the 31-char truncation of
// fullName. The real site LOG_FATALs and exit(1)s, so the call runs in a
// fork child (TEST-NET-1 shape, like test_memory_ledger's net) and the
// parent greps the child's stdout for the FATAL text.
//
// RED on origin/fable, in two independent places:
//   (d) the child's log carries the generic "not found in any EQE1 flat
//       pack" text, not "is format v1 ... holds '...'" -- and (on fable,
//       before the B1017 normalisation) the normalised-name half of the
//       diagnosis does not exist in eqe1_loader.cpp at all;
//   (e) a mixed-case ".BMP" request gets the generic message, not the
//       stale diagnosis -- findCompressedEntry lowercases + strips the
//       extension but the fable diagnosis compared the RAW name.
// GREEN on this branch: both requests yield the stale-pack FATAL naming
// the pack, its version and the 31-char key it holds.

#include <gtest/gtest.h>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/eqe1_loader.h"
#include "common/logging.h"
#include "scoped_temp_dir.h"

#include <irrlicht.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

// The soldunga-class name (33 chars) and the v1 pack that holds its
// 31-char truncation -- the same synthetic-pack idiom as
// tests/test_eqe1_stale_pack.cpp (writeV1StalePack), byte-for-byte the
// pre-B910 builder's shape: 48-byte zeroed entry, strncpy(name, 31).
const char* const kFullName = "gnome_stronghold_floor_transition";  // 33

bool writeV1StalePack(const std::string& path) {
    const std::string fullName = kFullName;
    if (fullName.size() <= EQE1_INLINE_NAME_LIMIT) return false;

    uint32_t szEtc1 = etc1MipChainSize(4, 4, 3);
    uint32_t indexEnd = sizeof(EQE1Header) + 1 * EQE1_INDEX_ENTRY_SIZE;
    uint32_t off = alignTo16(indexEnd);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    EQE1Header hdr = {};
    hdr.magic = EQE1_MAGIC;
    hdr.version = EQE1_VERSION;
    hdr.numEntries = 1;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    EQE1IndexEntry e = {};
    std::strncpy(e.name, fullName.c_str(), EQE1_NAME_SIZE - 1);  // the truncation
    e.width = 4; e.height = 4; e.mipLevels = 3;
    e.entryType = EQE1_TYPE_ETC1_RGB;
    e.dataOffset = off; e.dataSize = szEtc1;
    out.write(reinterpret_cast<const char*>(&e), sizeof(e));
    std::vector<char> payload(off + szEtc1 - indexEnd, 0x5A);
    out.write(payload.data(), payload.size());
    return true;
}

// The real miss path, once, in a child that exits (the FATAL site is
// std::exit(1) -- all-or-nothing, docs/decisions.md:92; it cannot run in
// the test process itself). The child's FATAL is written through the
// logger's ERROR path (LOG_FATAL <= LOG_ERROR -> appendError -> the stderr
// buffer), flushed synchronously by LOG_FATAL's FlushThreadLog() before the
// exit -- so the parent reads it off stderr (the INFO lines go to stdout,
// which is why the B930 leg that read only stdout never saw the FATAL).
// Returns the child's stderr plus its exit-code tag.
std::string runRealMiss(const std::string& packPath, const std::string& request) {
    pid_t pid = fork();
    if (pid < 0) return "fork failed";
    if (pid == 0) {
        // Child.
        SetLogLevel(LOG_WARN);  // FATAL reaches the drain regardless of level
        EQE1Loader loader;
        if (!loader.open(packPath)) { _exit(98); }

        // A cache with no driver at all is enough: the miss path
        // (findCompressedEntry -> stale FATAL) never touches one.
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        ConstrainedTextureCache cache(config);
        cache.addEQE1Loader(&loader);

        cache.getOrLoad(request, {}, "B1017Test", "stale-miss", false, nullptr);
        _exit(99);  // unreachable: getOrLoad exit(1)s on the miss
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 1) return "EXIT1";
    return "child-exit-" + std::to_string(status);
}

}  // namespace

// (d) The REAL miss path: getOrLoad(fullName, {}) on a cache holding the
// stale v1 pack must LOG_FATAL with the pack's path + version + the
// 31-char key -- not the generic asset-gap text -- and exit 1.
TEST(Eqe1RealMiss, StalePackFatalOnRealGetOrLoad) {
    const willeq_test::ScopedTempDir tmp("test_eqe1_real_miss_d");
    const std::string packPath = (tmp / "soldunga.eqe1").string();
    ASSERT_TRUE(writeV1StalePack(packPath)) << "cannot write synthetic v1 pack";

    testing::internal::CaptureStderr();
    const std::string result = runRealMiss(packPath, kFullName);
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_EQ(result, "EXIT1")
        << "the all-or-nothing miss must exit 1; got " << result
        << "\n--- captured log ---\n" << output;
    EXPECT_NE(output.find("is format v1"), std::string::npos)
        << "the stale-pack FATAL (not the generic asset gap) must fire.\n"
        << "--- captured log ---\n" << output;
    EXPECT_NE(output.find(packPath), std::string::npos)
        << "the FATAL must name the pack.\n--- captured log ---\n" << output;
    EXPECT_NE(output.find("gnome_stronghold_floor_transiti"), std::string::npos)
        << "the FATAL must name the 31-char key the pack holds.\n"
        << "--- captured log ---\n" << output;
    EXPECT_EQ(output.find("not found in any EQE1 flat pack"), std::string::npos)
        << "a stale pack must not get the generic asset-gap message.\n"
        << "--- captured log ---\n" << output;
}

// (e) A mixed-case request WITH a .BMP suffix -- the exact shape findCompressedEntry
// normalises (lowercase + strip .bmp/.dds). Pre-B1017 the diagnosis
// compared the RAW name against the pack's lowercase key and fell back to
// the generic message.
TEST(Eqe1RealMiss, MixedCaseBmpRequestStillGetsTheStaleDiagnosis) {
    const willeq_test::ScopedTempDir tmp("test_eqe1_real_miss_e");
    const std::string packPath = (tmp / "soldunga.eqe1").string();
    ASSERT_TRUE(writeV1StalePack(packPath)) << "cannot write synthetic v1 pack";

    const std::string request = "Gnome_Stronghold_Floor_Transition.BMP";
    // The request's normalised form (lowercase, no .BMP) is the full
    // 33-char name -- so the pack's 31-char key IS its prefix.
    ASSERT_EQ(request.size() - 4, 33u);

    testing::internal::CaptureStderr();
    const std::string result = runRealMiss(packPath, request);
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_EQ(result, "EXIT1")
        << "the all-or-nothing miss must exit 1; got " << result
        << "\n--- captured log ---\n" << output;
    EXPECT_NE(output.find("is format v1"), std::string::npos)
        << "the stale-pack FATAL must fire for the mixed-case request "
           "(the diagnosis must normalise like the lookup did).\n"
        << "--- captured log ---\n" << output;
    EXPECT_NE(output.find("gnome_stronghold_floor_transiti"), std::string::npos)
        << "the FATAL must name the normalised 31-char key.\n"
        << "--- captured log ---\n" << output;
    EXPECT_EQ(output.find("not found in any EQE1 flat pack"), std::string::npos)
        << "a mixed-case request must not get the generic asset-gap message.\n"
        << "--- captured log ---\n" << output;
}
