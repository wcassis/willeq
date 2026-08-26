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
