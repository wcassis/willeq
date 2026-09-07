// willeq_db_builder — Build SQLite database from EQ client data files
//
// Reads EQ client text files (spells, strings, etc.) and WillEQ JSON data files,
// writes a single SQLite database for runtime queries. This is the final stage
// in the data pipeline: source files remain editable, database is rebuilt on change.
//
// Usage:
//   ./willeq_db_builder --eq-path /path/to/EQ --data-path /path/to/willeq/data \
//       --config-path /path/to/willeq/config --output data/willeq.db
//
// Tables:
//   spells          - Spell data from spells_us.txt
//   eqstrings       - String lookups from eqstr_us.txt
//   dbstrings       - DB strings from dbstr_us.txt (category + sub_id)
//   item_models     - Item ID to model ID mapping
//   race_models     - Race ID to model code + S3D file
//   zone_points     - Zone line teleport destinations
//   s3d_assets      - S3D archive asset index (if s3d_index.json present)
//   sound_assets    - Sound file references from SoundAssets.txt
//
// T447 split this TU three ways; the loaders live beside it and the seam is
// tools/willeq_db/db_builder.h:
//   tools/willeq_db/db_loaders_text.cpp  the EQ client's *.txt inputs
//   tools/willeq_db/db_loaders_json.cpp  the WillEQ/config *.json inputs
// What stayed here is the four shared helpers, the metadata table and main().

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sqlite3.h"
#include "client/willeq_database.h"
#include "willeq_db/db_builder.h"

// ============================================================================
// Helpers
// ============================================================================

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// T392 (board B200): every Titanium *.txt data file is CRLF-terminated, and
// std::getline() splitting on '\n' leaves the '\r' attached to the end of the
// line -- so it lands inside the LAST field of every record. That is how
// sound_assets.filename came to hold "BuyItem.WAV\r": AudioManager lowercased
// it, missed in the snd*.pfs index, and returned the one failure exit that
// does not log, while the log line it did print ended in a CR and rendered as
// a clean "BuyItem.WAV" -- which is why T386 measured 31 silent sounds and
// could not see the cause. Four other tables carried the same taint
// (eqstrings, dbstrings, login_strings, and the race_data T395 later
// deleted). Read lines through this,
// never std::getline() directly, for any file that comes from the client.
bool readLine(std::istream& in, std::string& line) {
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// Splits on delim and KEEPS empty tokens, including a trailing one. The
// istringstream/getline form this replaced silently dropped the last field
// when it was empty, so dbstr_us.txt's 19 deliberately-blank strings
// ("142^12^") came out as 2 fields and were skipped by every `fields.size()
// < N` guard. That was invisible before T392 only because the CRLF bug left
// a "\r" sitting in the field and made it look non-empty.
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            tokens.push_back(s.substr(start));
            break;
        }
        tokens.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return tokens;
}

bool execSQL(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\nStatement: %s\n", err, sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ============================================================================
// Metadata table
// ============================================================================

static void writeMetadata(sqlite3* db, int totalEntries) {
    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS metadata (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    auto setMeta = [&](const char* key, const std::string& value) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO metadata (key, value) VALUES (?,?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };

    // T56: version lives in include/client/willeq_database.h
    // (kWilleqDbExpectedVersion) so builder and client cannot drift — bump
    // both together in the same commit when the schema changes.
    setMeta("version", std::to_string(EQT::kWilleqDbExpectedVersion));
    setMeta("build_date", __DATE__ " " __TIME__);
    setMeta("total_entries", std::to_string(totalEntries));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string eqPath;
    std::string dataPath = "data";
    std::string configPath = "config";
    std::string outputPath = "data/willeq.db";
    std::string s3dIndexPath;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) eqPath = argv[++i];
        else if (strcmp(argv[i], "--data-path") == 0 && i + 1 < argc) dataPath = argv[++i];
        else if (strcmp(argv[i], "--config-path") == 0 && i + 1 < argc) configPath = argv[++i];
        else if (strcmp(argv[i], "--s3d-index") == 0 && i + 1 < argc) s3dIndexPath = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s --eq-path /path/to/EQ [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --eq-path PATH      EQ client directory (required)\n");
            printf("  --data-path PATH    WillEQ data directory (default: data)\n");
            printf("  --config-path PATH  WillEQ config directory (default: config)\n");
            printf("  --s3d-index PATH    s3d_index.json path (optional)\n");
            printf("  -o, --output PATH   Output database path (default: data/willeq.db)\n");
            return 0;
        }
    }

    if (eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path is required\n");
        return 1;
    }

    printf("=== WillEQ Database Builder ===\n");
    printf("EQ path: %s\n", eqPath.c_str());
    printf("Output: %s\n\n", outputPath.c_str());

    auto totalStart = std::chrono::steady_clock::now();

    // Remove existing db
    remove(outputPath.c_str());

    // Open database
    sqlite3* db;
    if (sqlite3_open(outputPath.c_str(), &db) != SQLITE_OK) {
        fprintf(stderr, "ERROR: cannot create database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Performance pragmas. WAL is a BUILD-TIME setting only: the shipped
    // file is switched back to a rollback journal before the final close
    // (below), so nothing that opens it later runs the WAL protocol.
    execSQL(db, "PRAGMA journal_mode=WAL");
    execSQL(db, "PRAGMA synchronous=NORMAL");
    execSQL(db, "PRAGMA page_size=4096");

    int totalEntries = 0;

    // ── Spells ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadSpells(db, eqPath + "/spells_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  spells:       %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── EQ Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadEqStrings(db, eqPath + "/eqstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  eqstrings:    %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── DB Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadDbStrings(db, eqPath + "/dbstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  dbstrings:    %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Item Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadItemModels(db, dataPath + "/item_models.json");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  item_models:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Equipment Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadEquipmentModels(db, dataPath + "/equipment_models.json");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  equip_models: %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Race Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadRaceModels(db, configPath + "/race_models.json");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  race_models:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Sound Assets ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadSoundAssets(db, eqPath + "/SoundAssets.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  sound_assets: %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Animation Sounds ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadAnimationSounds(db, eqPath + "/AnimationSounds.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  anim_sounds:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Music Index ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadMusicIndex(db, eqPath + "/mp3index.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  music_files:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Login Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadLoginStrings(db, eqPath + "/eqlsstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  login_strings:%6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── S3D Asset Index ──
    if (!s3dIndexPath.empty()) {
        auto start = std::chrono::steady_clock::now();
        int n = loadS3dIndex(db, s3dIndexPath);
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  s3d_assets:   %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Metadata ──
    writeMetadata(db, totalEntries);

    // Final optimization
    execSQL(db, "ANALYZE");

    // T258: ship a plain rollback-journal database. A WAL-mode file makes
    // every ordinary reader (sqlite3 CLI, scripts, pre-T257 clients) run WAL
    // recovery on open and create -wal/-shm sidecars; on the project's NFS
    // mount two readers opening together raced on that recovery and one
    // lost with SQLITE_BUSY_RECOVERY (T257). Switching the journal mode
    // checkpoints the WAL into the main file and rewrites header bytes
    // 18/19 from 2/2 (WAL) to 1/1 (legacy). VACUUM then compacts the file;
    // it is cheap at this size (~4 MB).
    if (!execSQL(db, "PRAGMA journal_mode=DELETE")) {
        fprintf(stderr, "ERROR: could not leave WAL mode\n");
        sqlite3_close(db);
        return 1;
    }
    execSQL(db, "VACUUM");

    sqlite3_close(db);

    auto totalElapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - totalStart).count();

    // Report file size
    std::ifstream check(outputPath, std::ios::binary | std::ios::ate);
    float sizeMB = check ? check.tellg() / (1024.0f * 1024.0f) : 0;

    printf("\nDatabase built: %s (%.2f MB, %d entries, %.0f ms)\n",
           outputPath.c_str(), sizeMB, totalEntries, totalElapsed);

    return 0;
}
