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
