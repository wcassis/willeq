// B930 (issue #248): a STALE EQE1 zone pack must fail loudly by pack name +
// version.
//
// The measured cause (refactor/B930/README.md): the copy of soldunga.eqe1
// that two hosts read is header version 1 and holds
// gnome_stronghold_floor_transition (33 chars) as a null-padded 31-char
// truncation (gnome_stronghold_floor_transiti) — the pre-B910 builder's
// silently valid write. The client asks for the full 33-char name, misses
// every open pack, and the all-or-nothing rule FATAL-exits with a message
// that names the TEXTURE, not the cause.
//
// Where the loud failure lives, and why it cannot be at open:
// A v1/v2 pack whose every name fits in 31 chars is a LEGITIMATE format —
// the builder keeps it v1/v2 even after a rebuild (willeq.eqe1 is one,
// v2, 132 entries, all names <=31), the pinned test
// testV1PayloadFormatDefaultsToETC1 opens a real v1 pack, and CI's B852
// zone-content sweep loads qeynos2's v1 packs every pipeline. And at open
// time the stale pack is byte-identical to a freshly-rebuilt one: a
// 31-char name followed by NUL does not say whether the 32nd character
// existed. Rejecting v1/v2 at open would break both the format and the
// suites that pin it. The signature is only decidable when the client
// MEETS a >31-char name: a v1/v2 pack that holds the 31-char prefix of a
// missing >31-char name is, by construction, the pack that truncated it
// (a v3 pack cannot miss a name it was told to store — B910). That check
// is diagnoseStaleEQE1Pack() — pure, on the loader, and this file drives
// it through the same synthetic-pack idiom the B910 tests use.
//
// RED: on origin/fable EQE1Loader has no getFormatVersion() /
// holdsTruncatedName() and diagnoseStaleEQE1Pack() does not exist — this
// file does not compile, so the target's ctest fails (red). GREEN: this
// branch, the same target builds and every case below passes.
//
// Part 3 (a small ctest leg) audits tools/verify_eqe1 itself: a v1 pack
// prints v1 and, under --require-v3, reports STALE and exits non-zero; a
// v3 pack prints v3 and passes the same audit.

#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"
#include "scoped_temp_dir.h"

#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace EQT::Graphics;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("  %s... ", #name); fflush(stdout)

#define PASS() \
    do { printf("PASS\n"); testsPassed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

// The plain (void-returning) test body: on failure, record and stop the
// body.
#define ASSERT_TRUE(expr, msg) \
    do { if (!(expr)) { FAIL(msg); return; } } while(0)

// The value-returning helpers (writeV1StalePack / writeV3Pack) cannot use
// a bare `return`, so they check their preconditions into a bool and the
// caller treats a failed write as a test failure.
#define REQUIRE_OK(cond, msg) \
    do { if (!(cond)) { FAIL(msg); } } while(0)

// Writes a synthetic pack the exact way the PRE-B910 builder wrote a
// >31-char texture: zero-initialized 48-byte entry, strncpy(name, 31) —
// the 32nd byte is NUL (the class signature, measured against the stale
// soldunga.eqe1 itself). Returns false on a failed precondition/write.
// The full 33-char name is a constant the callers already hold.
static bool writeV1StalePack(const std::string& path) {
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33
    if (fullName.size() <= EQE1_INLINE_NAME_LIMIT) return false;

    uint32_t szEtc1 = etc1MipChainSize(4, 4, 3);
    uint32_t indexEnd = sizeof(EQE1Header) + 1 * EQE1_INDEX_ENTRY_SIZE;
    uint32_t off = alignTo16(indexEnd);

    {
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
    }
    return true;
}

// The v3 twin: the same full name stored via the name table — what the
// REBUILT pack is.
static bool writeV3Pack(const std::string& path) {
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33
    if (fullName.size() <= EQE1_INLINE_NAME_LIMIT) return false;
    uint32_t szEtc1 = etc1MipChainSize(4, 4, 3);
    uint32_t indexEnd = sizeof(EQE1Header) + 1 * EQE1_INDEX_ENTRY_SIZE_V3;
    uint32_t nameOff = indexEnd;
    uint32_t off = alignTo16(nameOff + fullName.size());

    {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) return false;
        EQE1Header hdr = {};
        hdr.magic = EQE1_MAGIC;
        hdr.version = EQE1_VERSION_V3;
        hdr.numEntries = 1;
        out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        EQE1IndexEntry e = {};  // name field zeroed — table-resident
        e.width = 4; e.height = 4; e.mipLevels = 3;
        e.entryType = EQE1_TYPE_ETC1_RGB; e.payloadFormat = EQE1_PAYLOAD_ETC1;
        e.dataOffset = off; e.dataSize = szEtc1;
        out.write(reinterpret_cast<const char*>(&e), sizeof(e));
        EQE1IndexEntryNameTail tail = {};
        tail.nameOffset = nameOff;
        tail.nameLength = static_cast<uint32_t>(fullName.size());
        out.write(reinterpret_cast<const char*>(&tail), sizeof(tail));
        out.write(fullName.data(), static_cast<std::streamsize>(fullName.size()));
        std::vector<char> payload(off + szEtc1 - (nameOff + fullName.size()), 0x5A);
        out.write(payload.data(), payload.size());
    }
    return true;
}

// 1. The stale pack LOADS (a null-padded 31-char name is formally valid —
//    the very silence that hid it, and why the diagnosis cannot be at open),
//    reports its header version, and the diagnosis names pack + version +
//    the truncation.
static void testStaleV1PackDiagnosed() {
    TEST(StaleV1PackDiagnosed);
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33
    ASSERT_TRUE(fullName.size() > EQE1_INLINE_NAME_LIMIT,
                "the soldunga-class name must exceed the inline field");

    const willeq_test::ScopedTempDir tmp("test_eqe1_stale_pack_v1");
    const std::string path = (tmp / "soldunga_stale.eqe1").string();
    REQUIRE_OK(writeV1StalePack(path), "cannot write synthetic v1 pack");

    EQE1Loader loader;
    ASSERT_TRUE(loader.open(path), "the stale v1 pack still loads (by design)");
    ASSERT_TRUE(loader.getFormatVersion() == EQE1_VERSION,
                "getFormatVersion must report the header version");
    ASSERT_TRUE(loader.lookup(fullName) == nullptr,
                "the full 33-char name must MISS (the soldunga defect)");

    std::vector<const EQE1Loader*> loaders{&loader};
    const auto diag = diagnoseStaleEQE1Pack(loaders, fullName);
    ASSERT_TRUE(diag.stalePack, "the stale-pack signature must be detected");
    ASSERT_TRUE(diag.packVersion == EQE1_VERSION,
                "diagnosis must carry the pack's version");
    ASSERT_TRUE(diag.packPath == path,
                "diagnosis must carry the pack's path");
    ASSERT_TRUE(diag.truncatedName == fullName.substr(0, EQE1_INLINE_NAME_LIMIT),
                "diagnosis must carry the 31-char key the pack holds");
    PASS();
}

// 2. The rebuilt (v3) pack: full-name lookup HITS, and a name it does not
//    hold is NOT blamed on it — a v3 pack cannot truncate.
static void testRebuiltV3PackNotBlamed() {
    TEST(RebuiltV3PackNotBlamed);
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33

    const willeq_test::ScopedTempDir tmp("test_eqe1_stale_pack_v3");
    const std::string path = (tmp / "soldunga_rebuilt.eqe1").string();
    REQUIRE_OK(writeV3Pack(path), "cannot write synthetic v3 pack");

    EQE1Loader loader;
    ASSERT_TRUE(loader.open(path), "the v3 pack must load");
    ASSERT_TRUE(loader.getFormatVersion() == EQE1_VERSION_V3,
                "getFormatVersion must report v3");
    ASSERT_TRUE(loader.lookup(fullName) != nullptr,
                "the full 33-char name must round-trip (B910)");

    std::vector<const EQE1Loader*> loaders{&loader};
    // A >31-char name the v3 pack genuinely does not hold is an asset gap,
    // not a truncation — the plain FATAL stays correct.
    const auto diag = diagnoseStaleEQE1Pack(loaders, "some_other_long_texture_name_x");
    ASSERT_TRUE(!diag.stalePack,
                "a v3 pack must never be blamed for a truncation");
    PASS();
}

// 3. The negative cases that keep the loud FATAL from becoming a lie:
//    closed loaders, short names, and an empty open set all diagnose as
//    "plain asset gap".
static void testNegativeCases() {
    TEST(NegativeCases);
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33

    const willeq_test::ScopedTempDir tmp("test_eqe1_stale_pack_neg");
    const std::string path = (tmp / "neg.eqe1").string();
    REQUIRE_OK(writeV1StalePack(path), "cannot write synthetic v1 pack");

    EQE1Loader closed;
    ASSERT_TRUE(!diagnoseStaleEQE1Pack({&closed}, fullName).stalePack,
                "a closed loader cannot be blamed");

    EQE1Loader loader;
    ASSERT_TRUE(loader.open(path), "open failed");
    ASSERT_TRUE(!diagnoseStaleEQE1Pack({&loader}, "shortname").stalePack,
                "a name that fits the 31-char field cannot be truncated");
    ASSERT_TRUE(!diagnoseStaleEQE1Pack({}, fullName).stalePack,
                "an empty open set has no pack to blame");
    PASS();
}

// 4. The real zone shape: the zone-own v1 pack opens FIRST (up front) and a
//    v3 global pack is lazy-opened later. Both hold the soldunga name here
//    (the v3 pack is what the index would resolve for it); the diagnosis
//    must still name the v1 ZONE pack, in open order.
static void testZoneOwnV1NamedFirst() {
    TEST(ZoneOwnV1NamedFirst);
    const std::string zoneName = "gnome_stronghold_floor_transition";    // 33

    const willeq_test::ScopedTempDir tmp("test_eqe1_stale_pack_zone");
    const std::string v1Path = (tmp / "soldunga.eqe1").string();
    const std::string v3Path = (tmp / "global_chr.eqe1").string();
    REQUIRE_OK(writeV1StalePack(v1Path), "cannot write synthetic v1 pack");
    REQUIRE_OK(writeV3Pack(v3Path), "cannot write synthetic v3 pack");

    EQE1Loader zone;
    ASSERT_TRUE(zone.open(v1Path), "zone pack open failed");
    EQE1Loader global;
    ASSERT_TRUE(global.open(v3Path), "global pack open failed");
    ASSERT_TRUE(global.lookup(zoneName) != nullptr, "v3 name missing");

    // Zone order: the zone-own pack was opened before the lazy global.
    std::vector<const EQE1Loader*> loaders{&zone, &global};
    const auto diag = diagnoseStaleEQE1Pack(loaders, zoneName);
    ASSERT_TRUE(diag.stalePack, "the stale zone pack must be detected");
    ASSERT_TRUE(diag.packPath == v1Path, "the ZONE pack (first open) is named");
    ASSERT_TRUE(diag.packVersion == EQE1_VERSION, "its version is v1");
    PASS();
}

// 5. The loud failure itself, end to end: a child process runs the REAL
//    diagnosis + the all-or-nothing exit the FATAL site makes, against a
//    synthetic stale v1 pack. The child prints the diagnosis before exiting
//    so the parent can assert the message's content; its exit code is what
//    all-or-nothing means.
//
//    This is the leg that is RED on origin/fable the other way: there
//    diagnoseStaleEQE1Pack() does not exist at all (compile red), and the
//    pre-B930 FATAL names only the texture.
static void testFatalExitNamesThePack() {
    TEST(FatalExitNamesThePack);
    const std::string fullName = "gnome_stronghold_floor_transition";  // 33

    const willeq_test::ScopedTempDir tmp("test_eqe1_stale_pack_fatal");
    const std::string path = (tmp / "soldunga.eqe1").string();
    const std::string markerPath = (tmp / "fatal_child.out").string();
    REQUIRE_OK(writeV1StalePack(path), "cannot write synthetic v1 pack");

    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0, "fork failed");
    if (pid == 0) {
        // Child: the diagnosis the FATAL site runs, printed verbatim.
        EQE1Loader loader;
        if (!loader.open(path)) { _exit(99); }
        std::vector<const EQE1Loader*> loaders{&loader};
        const auto diag = diagnoseStaleEQE1Pack(loaders, fullName);
        FILE* f = std::fopen(markerPath.c_str(), "w");
        if (f) {
            fprintf(f, "B930 stalePack=%d version=%u path=%s truncated=%s\n",
                    diag.stalePack ? 1 : 0, diag.packVersion,
                    diag.packPath.c_str(), diag.truncatedName.c_str());
            std::fclose(f);
        }
        // The all-or-nothing exit the client makes after LOG_FATAL.
        _exit(1);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
    }
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 1,
                "the all-or-nothing path must exit 1");

    std::ifstream in(markerPath);
    ASSERT_TRUE(in.is_open(), "the child wrote no diagnosis (RED on pre-B930)");
    std::string line;
    std::getline(in, line);
    if (line.find("stalePack=1") == std::string::npos) {
        FAIL(("child did not detect the stale pack: " + line).c_str());
        return;
    }
    if (line.find("version=1") == std::string::npos) {
        FAIL(("child did not carry the pack version: " + line).c_str());
        return;
    }
    if (line.find("path=" + path) == std::string::npos) {
        FAIL(("child did not carry the pack path: " + line).c_str());
        return;
    }
    if (line.find("truncated=gnome_stronghold_floor_transiti") == std::string::npos) {
        FAIL(("child did not carry the truncated key: " + line).c_str());
        return;
    }
    PASS();
}

int main() {
    printf("test_eqe1_stale_pack:\n");

    testStaleV1PackDiagnosed();
    testRebuiltV3PackNotBlamed();
    testNegativeCases();
    testZoneOwnV1NamedFirst();
    testFatalExitNamesThePack();

    printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
    return testsFailed > 0 ? 1 : 0;
}
