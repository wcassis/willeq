// T56: WilleqDatabase::open() schema-version validation.
//
// Verifies the fix for refactor/deferred.md's "willeq.db schema version is
// bumped but never validated" entry: open() now compares metadata.version
// against EQT::kWilleqDbExpectedVersion and fails loudly on mismatch,
// instead of silently degrading every spell lookup into "not found".
//
// These tests build small throwaway sqlite files under the gtest temp dir —
// they never touch the tracked data/willeq.db.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "client/willeq_database.h"

namespace {

std::string TempDbPath(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = tmp ? tmp : "/tmp";
    return dir + "/willeq_db_test_" + name + "_" + std::to_string(::getpid()) + ".sqlite";
}

// Creates a throwaway sqlite file with a `metadata` table. If
// includeVersionRow is false, the table exists but has no 'version' key
// (exercises the pre-existing "no version metadata" WARN path, unchanged
// by this fix). Returns the path; caller must remove() it.
std::string BuildDb(const char* name, bool includeMetadataTable, bool includeVersionRow,
                     int version) {
    std::string path = TempDbPath(name);
    std::remove(path.c_str());

    sqlite3* db = nullptr;
    EXPECT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);

    if (includeMetadataTable) {
        char* err = nullptr;
        sqlite3_exec(db, "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT)", nullptr,
                     nullptr, &err);
        if (err) sqlite3_free(err);

        if (includeVersionRow) {
            std::string sql = "INSERT INTO metadata (key, value) VALUES ('version', '" +
                               std::to_string(version) + "')";
            sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
            if (err) sqlite3_free(err);
        }
    }

    sqlite3_close(db);
    return path;
}

}  // namespace

// Correct version opens successfully.
TEST(WilleqDatabaseVersionTest, MatchingVersionOpens) {
    std::string path = BuildDb("matching", true, true, EQT::kWilleqDbExpectedVersion);

    EQT::WilleqDatabase db;
    EXPECT_TRUE(db.open(path));
    EXPECT_TRUE(db.isOpen());

    db.close();
    std::remove(path.c_str());
}

// Older-than-expected version fails.
TEST(WilleqDatabaseVersionTest, OlderVersionFailsToOpen) {
    std::string path = BuildDb("older", true, true, EQT::kWilleqDbExpectedVersion - 1);

    EQT::WilleqDatabase db;
    EXPECT_FALSE(db.open(path));
    EXPECT_FALSE(db.isOpen());

    std::remove(path.c_str());
}

// Newer-than-expected version also fails (both directions are fatal, T56
// decision: a newer db against an older client fails just as opaquely
// otherwise).
TEST(WilleqDatabaseVersionTest, NewerVersionFailsToOpen) {
    std::string path = BuildDb("newer", true, true, EQT::kWilleqDbExpectedVersion + 1);

    EQT::WilleqDatabase db;
    EXPECT_FALSE(db.open(path));
    EXPECT_FALSE(db.isOpen());

    std::remove(path.c_str());
}

// Missing `metadata` table entirely still fails as before this fix
// (pre-existing behaviour — must not regress).
TEST(WilleqDatabaseVersionTest, MissingMetadataTableFailsToOpen) {
    std::string path = BuildDb("no_meta_table", false, false, 0);

    EQT::WilleqDatabase db;
    EXPECT_FALSE(db.open(path));
    EXPECT_FALSE(db.isOpen());

    std::remove(path.c_str());
}

// `metadata` table present but no 'version' row: pre-existing WARN-and-open
// path, unchanged by this fix (there is nothing to compare against).
TEST(WilleqDatabaseVersionTest, MissingVersionRowStillOpens) {
    std::string path = BuildDb("no_version_row", true, false, 0);

    EQT::WilleqDatabase db;
    EXPECT_TRUE(db.open(path));
    EXPECT_TRUE(db.isOpen());

    db.close();
    std::remove(path.c_str());
}

// T256 live leg: every eqstrings/dbstrings/login_strings row in the built
// data/willeq.db ends in '\r' (eqstr_us.txt is CRLF and the builder keeps
// the CR), so every SQLite-served string rendered "...^M" in chat -- the
// legacy text-file loader strips it, the SQLite path did not.
TEST(WilleqDatabaseStringTest, TrailingCarriageReturnIsStripped) {
    std::string path = BuildDb("cr", true, true, EQT::kWilleqDbExpectedVersion);
    {
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
        const char* sql =
            "CREATE TABLE eqstrings (id INTEGER PRIMARY KEY, text TEXT NOT NULL);"
            "INSERT INTO eqstrings VALUES (5006, 'ZONE: %1' || char(13));"
            "CREATE TABLE dbstrings (category INTEGER, sub_id INTEGER, text TEXT NOT NULL);"
            "INSERT INTO dbstrings VALUES (1, 2, 'Bind Wound' || char(13));"
            "CREATE TABLE login_strings (id INTEGER PRIMARY KEY, text TEXT NOT NULL);"
            "INSERT INTO login_strings VALUES (7, 'Welcome' || char(13));";
        char* err = nullptr;
        ASSERT_EQ(sqlite3_exec(raw, sql, nullptr, nullptr, &err), SQLITE_OK) << (err ? err : "");
        sqlite3_close(raw);
    }

    EQT::WilleqDatabase db;
    ASSERT_TRUE(db.open(path));
    EXPECT_EQ(db.getString(5006), "ZONE: %1");
    EXPECT_EQ(db.getDbString(1, 2), "Bind Wound");
    EXPECT_EQ(db.getLoginString(7), "Welcome");

    db.close();
    std::remove(path.c_str());
}

// ============================================================================
// T257: concurrent readers of the shipped data/willeq.db must never fail.
//
// The T256 live leg started two willeq processes in the same second and one
// of them aborted with "missing metadata table". The builder leaves
// willeq.db in WAL journal mode; a plain read-only connection to a WAL file
// still runs the WAL protocol (creates -wal/-shm, takes the WAL_RECOVER lock
// to rebuild the wal-index), and on the NFS mount the data lives on the
// second process lost that race with SQLITE_BUSY / SQLITE_BUSY_RECOVERY on
// its very first prepare. These tests use the REAL tracked file on its real
// filesystem, and fork() rather than threads because the race is between
// processes (threads in one process share SQLite's per-inode lock state and
// never see it).
// ============================================================================

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool FileExists(const std::string& p) {
    struct stat sb;
    return ::stat(p.c_str(), &sb) == 0;
}

// Opens the db in this (child) process and runs one real query. Returns 0
// on success so the parent can count failures via the exit status.
int OpenAndQueryOnce(const std::string& path) {
    EQT::WilleqDatabase db;
    if (!db.open(path)) return 1;
    if (db.getSpellCount() == 0) return 2;
    return 0;
}

// Forks `procs` children that all open `path` at the same instant, `rounds`
// times. Returns the number of children that failed.
int ConcurrentOpens(const std::string& path, int procs, int rounds) {
    int failures = 0;
    for (int r = 0; r < rounds; ++r) {
        for (int i = 0; i < procs; ++i) {
            pid_t pid = ::fork();
            if (pid == 0) ::_exit(OpenAndQueryOnce(path));
            EXPECT_GT(pid, 0) << "fork failed";
        }
        for (int i = 0; i < procs; ++i) {
            int status = 0;
            ::wait(&status);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ++failures;
        }
    }
    return failures;
}

}  // namespace

TEST(WilleqDatabaseConcurrentOpenTest, EightProcessesOpenTheShippedDbTogether) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;

    // 8 x 5 = 40 concurrent opens. Before T257 two processes lost ~50% of
    // the rounds on the project's NFS mount (97/200, review/T257/).
    EXPECT_EQ(ConcurrentOpens(path, 8, 5), 0);
}

// A reader must leave no -wal/-shm sidecars behind: immutable=1 never
// creates them. Guarded on the pre-state so a sidecar left by some other
// tool (the sqlite3 CLI opens read-write) cannot fail this test.
TEST(WilleqDatabaseConcurrentOpenTest, ReaderCreatesNoWalOrShmFiles) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
    const std::string wal = path + "-wal";
    const std::string shm = path + "-shm";
    const bool walBefore = FileExists(wal);
    const bool shmBefore = FileExists(shm);

    ASSERT_EQ(ConcurrentOpens(path, 4, 2), 0);

    if (!walBefore) EXPECT_FALSE(FileExists(wal)) << wal << " was created by a reader";
    if (!shmBefore) EXPECT_FALSE(FileExists(shm)) << shm << " was created by a reader";
}

// The immutable open goes through a "file:" URI; the three URI-syntax
// characters in a path must survive the round trip.
TEST(WilleqDatabaseConcurrentOpenTest, PathWithUriSyntaxCharactersOpens) {
    std::string path = BuildDb("uri%chars?and#more", true, true, EQT::kWilleqDbExpectedVersion);
    ASSERT_NE(path.find('%'), std::string::npos);
    ASSERT_NE(path.find('?'), std::string::npos);
    ASSERT_NE(path.find('#'), std::string::npos);

    EQT::WilleqDatabase db;
    EXPECT_TRUE(db.open(path));
    EXPECT_TRUE(db.isOpen());

    db.close();
    std::remove(path.c_str());
}

// A missing file still fails open() (EverQuest::OpenDatabase falls back to
// legacy file loading on false) -- immutable=1 must not change that.
TEST(WilleqDatabaseConcurrentOpenTest, MissingFileStillFailsToOpen) {
    std::string path = TempDbPath("does_not_exist");
    std::remove(path.c_str());

    EQT::WilleqDatabase db;
    EXPECT_FALSE(db.open(path));
    EXPECT_FALSE(db.isOpen());
    EXPECT_FALSE(FileExists(path)) << "open() must not create the file";
}

// ============================================================================
// T258: the shipped file must not be a WAL-mode database. The builder
// switches back to a rollback journal before its final close, so header
// bytes 18/19 (file format read/write versions) are 1/1, not 2/2, and a
// PLAIN read-only open -- not the client's immutable URI -- reports
// journal_mode=delete and creates no -wal/-shm sidecars. This is what any
// other tool (sqlite3 CLI, scripts) sees.
// ============================================================================

TEST(WilleqDatabaseJournalModeTest, ShippedDbHeaderIsNotWal) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;

    FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    unsigned char header[100] = {0};
    ASSERT_EQ(std::fread(header, 1, sizeof(header), f), sizeof(header));
    std::fclose(f);

    EXPECT_EQ(std::string(reinterpret_cast<char*>(header), 15), "SQLite format 3");
    EXPECT_EQ(header[18], 1) << "write version 2 = WAL; the shipped file must be legacy (1)";
    EXPECT_EQ(header[19], 1) << "read version 2 = WAL; the shipped file must be legacy (1)";
}

TEST(WilleqDatabaseJournalModeTest, PlainReadOnlyOpenReportsDeleteAndLeavesNoSidecars) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
    const std::string wal = path + "-wal";
    const std::string shm = path + "-shm";
    const bool walBefore = FileExists(wal);
    const bool shmBefore = FileExists(shm);

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    std::string mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    EXPECT_EQ(mode, "delete");
    if (!walBefore) EXPECT_FALSE(FileExists(wal)) << wal << " was created by a plain reader";
    if (!shmBefore) EXPECT_FALSE(FileExists(shm)) << shm << " was created by a plain reader";
}

// T262: the shipped db's content. `zone_characters` (2188 (zone, model_code,
// source_file) hints from the client's *_chr.txt files) had exactly one
// reader, getZoneCharacters(), with zero callers. The comparison in
// refactor/T262_zone_characters_table.md showed the runtime resolves every
// spawnable model without it (race_models.s3d_file -> zone _chr -> globals ->
// directory scan for the mesh; the T207 index for textures) -- EXCEPT the
// three dragon races whose race_models entry had no s3d_file at all, which
// the hint would have papered over in the two zones that list them. The
// table is gone and the three races carry their archive, so the fix holds
// in every zone, not two.
namespace {
bool TableExists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                           -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}
}  // namespace

TEST(WilleqDatabaseShippedContentTest, ZoneCharactersTableIsGone) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    EXPECT_FALSE(TableExists(db, "zone_characters"))
        << "zone_characters has no reader; the builder must not write it (T262)";
    EXPECT_TRUE(TableExists(db, "race_models"));
    sqlite3_close(db);
}

TEST(WilleqDatabaseShippedContentTest, DragonRacesNameTheirChrArchive) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
    EQT::WilleqDatabase db;
    ASSERT_TRUE(db.open(path));
    // Race id -> the dedicated archive that holds <CODE>_HS_DEF and all of its
    // textures (bwd_chr/gdr_chr/pri_chr in both the S3D and EQE1 trees).
    const struct { uint16_t race; const char* code; const char* s3d; } expected[] = {
        {195, "BWD", "bwd_chr.s3d"},
        {196, "GDR", "gdr_chr.s3d"},
        {198, "PRI", "pri_chr.s3d"},
    };
    for (const auto& e : expected) {
        auto info = db.getRaceModel(e.race);
        ASSERT_TRUE(info.found) << "race " << e.race;
        EXPECT_EQ(info.code, e.code) << "race " << e.race;
        EXPECT_EQ(info.s3dFile, e.s3d)
            << "race " << e.race << " has a mesh but race_models names no archive, so the "
               "T199 candidate set never includes it and its textures resolve to nothing";
    }
}

// T265: every race_models row names an archive that exists AND holds its
// model code. The runtime (RaceModelLoader) reads races from THIS db, tries
// the row's s3d_file first for the mesh, and T199 adds the same archive to
// the EQE1 texture candidate set -- so a row whose s3d_file is empty, missing,
// or does not contain <CODE>_HS_DEF / <CODE>_ACTORDEF is a placeholder cube
// (T211/T213/T262 all found instances). The check reads the archive's WLD
// string hash directly (PfsArchive + the WLD XOR key), so it needs only
// pfs.cpp, not the graphics stack. Skips when the EQ client tree is absent.
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include "client/graphics/eq/pfs.h"

namespace {

bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Names of every model definition (<CODE>_HS_DEF / <CODE>_ACTORDEF) in the
// archive's .wld files, read from the encoded string hash of each WLD header.
bool ArchiveModelNames(const std::string& s3dPath, std::set<std::string>& names, std::string& err) {
    EQT::Graphics::PfsArchive archive;
    if (!archive.open(s3dPath)) { err = "cannot open " + s3dPath; return false; }
    std::vector<std::string> wlds;
    archive.getFilenames("wld", wlds);
    if (wlds.empty()) { err = "no .wld in " + s3dPath; return false; }
    static const uint8_t kKey[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};
    for (const auto& wld : wlds) {
        std::vector<char> buf;
        if (!archive.get(wld, buf) || buf.size() < 28) continue;
        uint32_t magic = 0, hashLen = 0;
        std::memcpy(&magic, buf.data(), 4);
        std::memcpy(&hashLen, buf.data() + 20, 4);
        if (magic != 0x54503D02u || 28u + hashLen > buf.size()) continue;
        std::string hash(buf.data() + 28, hashLen);
        for (size_t i = 0; i < hash.size(); ++i) hash[i] ^= static_cast<char>(kKey[i % 8]);
        size_t start = 0;
        while (start < hash.size()) {
            size_t end = hash.find('\0', start);
            if (end == std::string::npos) end = hash.size();
            std::string s = hash.substr(start, end - start);
            if (EndsWith(s, "_HS_DEF") || EndsWith(s, "_ACTORDEF")) names.insert(s);
            start = end + 1;
        }
    }
    return true;
}

std::string Upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

TEST(WilleqDatabaseShippedContentTest, EveryRaceRowNamesAnArchiveHoldingItsCode) {
    const std::string dbPath = WILLEQ_DB_REAL_PATH;
    if (!FileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
    const std::string eqPath = EQ_CLIENT_DATA_PATH;
    if (!FileExists(eqPath + "/global_chr.s3d")) GTEST_SKIP() << "no EQ client tree at " << eqPath;

    // Race 32 (Ghost, GHM/GHF) is the one documented exception: no archive in
    // the client holds a GHM mesh (T265 scanned all 419 *_chr*.s3d), so there
    // is nothing correct to name. Any other empty s3d_file is a regression.
    const std::set<int> kNoModelAnywhere = {32};

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT race_id, code, s3d_file FROM race_models ORDER BY race_id",
                                 -1, &stmt, nullptr), SQLITE_OK);
    std::map<std::string, std::set<std::string>> cache;  // archive -> model names
    int rows = 0, checked = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ++rows;
        const int race = sqlite3_column_int(stmt, 0);
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* f = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const std::string code = Upper(c ? c : "");
        const std::string s3d = f ? f : "";
        EXPECT_FALSE(code.empty()) << "race " << race << " has no code";
        if (kNoModelAnywhere.count(race)) {
            EXPECT_TRUE(s3d.empty()) << "race " << race << " gained an s3d_file; drop it from the exception list";
            continue;
        }
        EXPECT_FALSE(s3d.empty()) << "race " << race << " (" << code << ") names no s3d_file";
        if (s3d.empty()) continue;
        const std::string path = eqPath + "/" + s3d;
        EXPECT_TRUE(FileExists(path)) << "race " << race << " (" << code << ") s3d_file " << s3d << " is not in the client tree";
        if (!FileExists(path)) continue;
        auto it = cache.find(s3d);
        if (it == cache.end()) {
            std::set<std::string> names; std::string err;
            ASSERT_TRUE(ArchiveModelNames(path, names, err)) << err;
            it = cache.emplace(s3d, std::move(names)).first;
        }
        const bool holds = it->second.count(code + "_HS_DEF") || it->second.count(code + "_ACTORDEF");
        EXPECT_TRUE(holds) << "race " << race << ": " << s3d << " holds no " << code
                           << "_HS_DEF / _ACTORDEF (the row points at the wrong archive)";
        ++checked;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    EXPECT_GE(rows, 187) << "race_models shrank";
    EXPECT_EQ(checked, rows - static_cast<int>(kNoModelAnywhere.size()));
}

// T265's individual fixes, pinned so a stale rebuild of the db from an older
// race_models.json is caught by name (the generic test above would also fail,
// but this says which row and why).
TEST(WilleqDatabaseShippedContentTest, T265RaceRowFixesAreInTheDb) {
    const std::string path = WILLEQ_DB_REAL_PATH;
    if (!FileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
    EQT::WilleqDatabase db;
    ASSERT_TRUE(db.open(path));
    const struct { uint16_t race; const char* code; const char* s3d; const char* why; } expected[] = {
        {17,  "GOL",  "befallen_chr.s3d",        "GOM is the Velious golem; befallen_chr holds GOL"},
        {72,  "SHIP", "oot_chr.s3d",             "timorous_chr holds LAUNCH/LAUNCHM, not SHIP"},
        {330, "FRM",  "globalpcfroglok_chr.s3d", "FRM/FRF exist only in globalpcfroglok_chr"},
        {240, "IVM",  "global_chr.s3d",          "no TPM mesh anywhere; aliased to 127 Invisible Man"},
        {35,  "EEL",  "eel_chr.s3d",             "eqemu Race::GiantEel had no row"},
        {114, "GSP",  "oot_chr.s3d",             "eqemu Race::GhostShip had no row"},
        {141, "BOAT", "global_chr.s3d",          "eqemu Race::Boat had no row"},
    };
    for (const auto& e : expected) {
        auto info = db.getRaceModel(e.race);
        ASSERT_TRUE(info.found) << "race " << e.race << ": " << e.why;
        EXPECT_EQ(info.code, e.code) << "race " << e.race << ": " << e.why;
        EXPECT_EQ(info.s3dFile, e.s3d) << "race " << e.race << ": " << e.why;
    }
}
