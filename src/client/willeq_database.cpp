#include "client/willeq_database.h"
#include "common/logging.h"

#include <chrono>
#include <cstring>
#include <sqlite3.h>

namespace EQT {

namespace {

// T257: the open() path goes through a "file:" URI so ?immutable=1 can be
// passed. A URI filename must percent-encode the three characters that
// would otherwise be parsed as URI syntax; everything else (including
// '/', spaces and UTF-8) is passed through verbatim by SQLite.
std::string uriEscapePath(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        if (c == '%' || c == '?' || c == '#') {
            static const char hex[] = "0123456789ABCDEF";
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

constexpr int kOpenBusyTimeoutMs = 2000;

}  // namespace

WilleqDatabase::WilleqDatabase() = default;

WilleqDatabase::~WilleqDatabase() {
    close();
}

bool WilleqDatabase::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) close();

    // T111: SQLITE_OPEN_FULLMUTEX (was SQLITE_OPEN_NOMUTEX) -- NOMUTEX makes
    // this connection handle explicitly unsafe for concurrent multi-thread
    // use (SQLite's own documented meaning of the flag), which was latent
    // and unreachable in practice as long as EntityPrepWorker's background
    // thread rarely did meaningful concurrent work (T109's pickup path was
    // broken, so it mostly sat idle). T111's fix to the pickup path makes
    // EntityPrepWorker (background thread) and
    // EntityRenderer::updateEquipmentTransforms() (main/render thread, via
    // RaceModelLoader::getRaceCode() -> WilleqDatabase::getRaceModel()) both
    // query this SAME connection concurrently, for real, for the first
    // time -- live-caught as a reproducible SIGSEGV inside SQLite's
    // btreeCursor() (see refactor/T111_progressive_loading_rearm.md's
    // Review for the core-dump backtrace). FULLMUTEX serializes access to
    // the connection's internal structures; cheap here since the database
    // is opened SQLITE_OPEN_READONLY with a small (1MB) page cache and
    // small per-lookup queries, not a hot per-frame path.
    // T257: open as an IMMUTABLE database via a URI filename. willeq.db is
    // read-only shipped data (nothing outside tools/willeq_db_builder ever
    // writes it), but the builder leaves it in WAL journal mode, and a
    // plain SQLITE_OPEN_READONLY connection to a WAL database still
    // participates in the WAL protocol: it creates -wal/-shm, mmaps the
    // wal-index and takes the WAL_RECOVER lock to rebuild it on first use.
    // On the NFS mount the data lives on (local_lock=none, non-coherent
    // shm), two clients started in the same second raced on that recovery:
    // the loser's very first prepare failed with SQLITE_BUSY (extended
    // SQLITE_BUSY_RECOVERY, 261) and open() misreported it as "missing
    // metadata table" -- measured 97/200 losers with two processes,
    // review/T257/. immutable=1 tells SQLite the file cannot change while
    // open: no locks, no -wal/-shm, no recovery, so N concurrent readers
    // cannot fail. Measured 0/240 with eight processes on the same mount.
    // FULLMUTEX is kept (T111: two threads share this ONE connection).
    std::string uri = "file:" + uriEscapePath(dbPath) + "?immutable=1";
    int rc = sqlite3_open_v2(uri.c_str(), &db_,
                              SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_URI,
                              nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR(MOD_MAIN, "WilleqDatabase: failed to open {}: {} (sqlite rc={})", dbPath,
                  db_ ? sqlite3_errmsg(db_) : "unknown error", rc);
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    // Belt and braces: immutable=1 takes no locks, so this never fires for
    // the shipped file; it covers a caller handing in a non-immutable path.
    sqlite3_busy_timeout(db_, kOpenBusyTimeoutMs);

    // Performance pragmas for read-only access
    sqlite3_exec(db_, "PRAGMA mmap_size=1048576", nullptr, nullptr, nullptr);  // 1MB mmap
    sqlite3_exec(db_, "PRAGMA cache_size=-1024", nullptr, nullptr, nullptr);   // 1MB page cache
    sqlite3_exec(db_, "PRAGMA query_only=ON", nullptr, nullptr, nullptr);

    // Verify database has expected tables and matches the schema version
    // this client was built against (T56 fix — previously only logged,
    // never compared; a stale db silently returned empty spell lookups
    // instead of a clear error, see refactor/deferred.md).
    sqlite3_stmt* check = nullptr;
    rc = sqlite3_prepare_v2(db_, "SELECT value FROM metadata WHERE key='version'",
                            -1, &check, nullptr);
    if (rc == SQLITE_OK) {
        int step = sqlite3_step(check);
        if (step == SQLITE_ROW) {
            int version = sqlite3_column_int(check, 0);
            sqlite3_finalize(check);
            if (version != kWilleqDbExpectedVersion) {
                LOG_FATAL(MOD_MAIN,
                    "WilleqDatabase: {} has schema version {} ({}) but this client expects "
                    "version {} — rebuild it: run the willeq_db_builder tool "
                    "(tools/willeq_db_builder.cpp) to regenerate data/willeq.db",
                    dbPath, version, version < kWilleqDbExpectedVersion ? "too old" : "too new",
                    kWilleqDbExpectedVersion);
                sqlite3_close(db_);
                db_ = nullptr;
                return false;
            }
            LOG_INFO(MOD_MAIN, "WilleqDatabase: opened {} (version {})", dbPath, version);
        } else if (step == SQLITE_DONE) {
            sqlite3_finalize(check);
            LOG_WARN(MOD_MAIN, "WilleqDatabase: no version metadata in {}", dbPath);
        } else {
            // T257: a step failure is not a missing table either -- say what
            // SQLite said.
            LOG_ERROR(MOD_MAIN, "WilleqDatabase: {} version query failed: {} (sqlite rc={})",
                      dbPath, sqlite3_errmsg(db_), step);
            sqlite3_finalize(check);
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }
    } else {
        // T257: prepare can fail for reasons other than a missing table --
        // SQLITE_BUSY/SQLITE_LOCKED (another process holds the file) was
        // being reported as "missing metadata table" in T256. Only a
        // SQLITE_ERROR is a real schema problem; anything else names the
        // sqlite error code so the message is honest.
        if (rc == SQLITE_ERROR) {
            LOG_ERROR(MOD_MAIN, "WilleqDatabase: {} missing metadata table ({})", dbPath,
                      sqlite3_errmsg(db_));
        } else {
            LOG_ERROR(MOD_MAIN,
                      "WilleqDatabase: {} could not read the metadata table: {} (sqlite rc={}, "
                      "extended={}) -- the file is present but not readable right now, "
                      "not a schema problem",
                      dbPath, sqlite3_errmsg(db_), rc, sqlite3_extended_errcode(db_));
        }
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    return true;
}

void WilleqDatabase::close() {
    // T114: callers of close() (open()'s re-open path, dtor) may already hold
    // dbMutex_ (open()) or run before any other thread has a reference to
    // `this` (dtor) -- close() itself is not re-entered concurrently by
    // design (destructor runs single-threaded, open()'s self-call already
    // holds the lock), so it does not re-lock here.
    // Finalize all cached statements
    auto fin = [](sqlite3_stmt*& s) { if (s) { sqlite3_finalize(s); s = nullptr; } };
    fin(stmtGetSpell_);
    fin(stmtGetSpellByName_);
    fin(stmtFindSpellsByNameLike_);
    fin(stmtGetSpellCount_);
    fin(stmtGetString_);
    fin(stmtGetDbString_);
    fin(stmtGetItemModel_);
    fin(stmtGetAsset_);
    fin(stmtGetAssetsInArchive_);
    fin(stmtGetRaceModel_);
    fin(stmtGetRaceModelByCode_);
    fin(stmtGetSoundAsset_);
    fin(stmtGetAnimSound_);
    fin(stmtGetMusicFile_);
    fin(stmtGetLoginString_);

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

sqlite3_stmt* WilleqDatabase::prepare(const char* sql, sqlite3_stmt*& cached) {
    if (!cached && db_) {
        sqlite3_prepare_v2(db_, sql, -1, &cached, nullptr);
    }
    return cached;
}

std::string WilleqDatabase::colText(sqlite3_stmt* stmt, int col) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return text ? std::string(text) : "";
}

// T256: the string tables were built from CRLF text files and every row
// (5901 eqstrings, 8305 dbstrings, 470 login_strings at db version 4) ends
// in '\r'. The legacy text-file loader in StringDatabase strips it; this
// path did not, so every SQLite-served message rendered "...^M" in chat.
std::string WilleqDatabase::colTextNoCR(sqlite3_stmt* stmt, int col) {
    std::string s = colText(stmt, col);
    while (!s.empty() && s.back() == '\r') {
        s.pop_back();
    }
    return s;
}

size_t WilleqDatabase::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return 0;
    int current = 0, highwater = 0;
    sqlite3_db_status(db_, SQLITE_DBSTATUS_CACHE_USED, &current, &highwater, 0);
    return static_cast<size_t>(current);
}

// ============================================================================
// Timed query helper
// ============================================================================

class ScopedQueryTimer {
public:
    ScopedQueryTimer(DatabaseStats& stats) : stats_(stats),
        start_(std::chrono::steady_clock::now()) {}
    ~ScopedQueryTimer() {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_).count();
        stats_.recordQuery(static_cast<uint64_t>(elapsed));
    }
private:
    DatabaseStats& stats_;
    std::chrono::steady_clock::time_point start_;
};

// ============================================================================
// Spell queries
// ============================================================================

// Reads the 61 effect-slot columns (12 x effect_id/base/base2/max/formula +
// spell_affect_index) and the 12 reagent columns (T246), verbatim as they appear in the row — markers (254 for
// effect id, 100 for formula) are NOT interpreted here; that happens at the
// consumer boundary (SpellDatabase::queryAndCache), per SpellInfo's contract.
// `firstCol` is the sqlite3_column index of effect_id_1 (the first of the 61).
// The positional tail of the spells SELECT, shared by getSpell() and
// getSpellByName() so the two column runs cannot drift apart. Everything it
// reads is verbatim spells_us.txt -- markers are interpreted at the consumer
// boundary (SpellDatabase::queryAndCache), never here.
static void readSpellEffectColumns(sqlite3_stmt* stmt, int firstCol, SpellInfo& info) {
    int col = firstCol;
    for (int i = 0; i < 12; ++i) info.effectId[i]   = static_cast<int16_t>(sqlite3_column_int(stmt, col++));
    for (int i = 0; i < 12; ++i) info.baseValue[i]  = sqlite3_column_int(stmt, col++);
    for (int i = 0; i < 12; ++i) info.base2Value[i] = sqlite3_column_int(stmt, col++);
    for (int i = 0; i < 12; ++i) info.maxValue[i]   = sqlite3_column_int(stmt, col++);
    for (int i = 0; i < 12; ++i) info.formula[i]    = sqlite3_column_int(stmt, col++);
    info.spellAffectIndex = sqlite3_column_int(stmt, col++);
    // T246: 12 reagent columns follow spell_affect_index, verbatim (-1 = none).
    for (int i = 0; i < 4; ++i) info.reagentId[i]         = sqlite3_column_int(stmt, col++);
    for (int i = 0; i < 4; ++i) info.reagentCount[i]      = sqlite3_column_int(stmt, col++);
    for (int i = 0; i < 4; ++i) info.noexpendReagentId[i] = sqlite3_column_int(stmt, col++);
    // T355: endurance_cost is the LAST column of both spell SELECTs -- it was
    // appended rather than placed next to mana_cost so no existing index moved.
    info.enduranceCost = sqlite3_column_int(stmt, col++);
}

SpellInfo WilleqDatabase::getSpell(uint32_t spellId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    SpellInfo info;
    if (!db_) return info;

    auto* stmt = prepare(
        "SELECT id, name, player_tag, teleport_zone, cast_on_you, cast_on_other, "
        "spell_fades, cast_time_ms, recovery_time_ms, recast_time_ms, "
        "duration_formula, duration_cap, mana_cost, target_type, range_val, "
        "aoe_range, resist_type, resist_diff, casting_skill, is_beneficial, "
        "spell_icon, gem_icon, cast_anim, zone_type, class_levels, "
        "effect_id_1, effect_id_2, effect_id_3, effect_id_4, effect_id_5, effect_id_6, "
        "effect_id_7, effect_id_8, effect_id_9, effect_id_10, effect_id_11, effect_id_12, "
        "base_value_1, base_value_2, base_value_3, base_value_4, base_value_5, base_value_6, "
        "base_value_7, base_value_8, base_value_9, base_value_10, base_value_11, base_value_12, "
        "base2_value_1, base2_value_2, base2_value_3, base2_value_4, base2_value_5, base2_value_6, "
        "base2_value_7, base2_value_8, base2_value_9, base2_value_10, base2_value_11, base2_value_12, "
        "max_value_1, max_value_2, max_value_3, max_value_4, max_value_5, max_value_6, "
        "max_value_7, max_value_8, max_value_9, max_value_10, max_value_11, max_value_12, "
        "formula_1, formula_2, formula_3, formula_4, formula_5, formula_6, "
        "formula_7, formula_8, formula_9, formula_10, formula_11, formula_12, "
        "spell_affect_index, "
        "reagent_id_1, reagent_id_2, reagent_id_3, reagent_id_4, "
        "reagent_count_1, reagent_count_2, reagent_count_3, reagent_count_4, "
        "noexpend_reagent_1, noexpend_reagent_2, noexpend_reagent_3, noexpend_reagent_4, "
        "endurance_cost "
        "FROM spells WHERE id=?",
        stmtGetSpell_);
    if (!stmt) return info;

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, spellId);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.found = true;
        info.id = sqlite3_column_int(stmt, 0);
        info.name = colText(stmt, 1);
        info.playerTag = colText(stmt, 2);
        info.teleportZone = colText(stmt, 3);
        info.castOnYou = colText(stmt, 4);
        info.castOnOther = colText(stmt, 5);
        info.spellFades = colText(stmt, 6);
        info.castTimeMs = sqlite3_column_int(stmt, 7);
        info.recoveryTimeMs = sqlite3_column_int(stmt, 8);
        info.recastTimeMs = sqlite3_column_int(stmt, 9);
        info.durationFormula = sqlite3_column_int(stmt, 10);
        info.durationCap = sqlite3_column_int(stmt, 11);
        info.manaCost = sqlite3_column_int(stmt, 12);
        info.targetType = sqlite3_column_int(stmt, 13);
        info.range = static_cast<float>(sqlite3_column_double(stmt, 14));
        info.aoeRange = static_cast<float>(sqlite3_column_double(stmt, 15));
        info.resistType = sqlite3_column_int(stmt, 16);
        info.resistDiff = sqlite3_column_int(stmt, 17);
        info.castingSkill = sqlite3_column_int(stmt, 18);
        info.isBeneficial = sqlite3_column_int(stmt, 19) != 0;
        info.spellIcon = sqlite3_column_int(stmt, 20);
        info.gemIcon = sqlite3_column_int(stmt, 21);
        info.castAnim = sqlite3_column_int(stmt, 22);
        info.zoneType = sqlite3_column_int(stmt, 23);
        const void* blob = sqlite3_column_blob(stmt, 24);
        int blobLen = sqlite3_column_bytes(stmt, 24);
        info.classLevels.fill(255);
        if (blob && blobLen > 0) {
            size_t n = static_cast<size_t>(blobLen) < info.classLevels.size()
                           ? static_cast<size_t>(blobLen)
                           : info.classLevels.size();
            std::memcpy(info.classLevels.data(), blob, n);
        }
        readSpellEffectColumns(stmt, 25, info);
    }

    return info;
}

SpellInfo WilleqDatabase::getSpellByName(const std::string& name) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    SpellInfo info;
    if (!db_) return info;

    auto* stmt = prepare(
        "SELECT id, name, player_tag, teleport_zone, cast_on_you, cast_on_other, "
        "spell_fades, cast_time_ms, recovery_time_ms, recast_time_ms, "
        "duration_formula, duration_cap, mana_cost, target_type, range_val, "
        "aoe_range, resist_type, resist_diff, casting_skill, is_beneficial, "
        "spell_icon, gem_icon, cast_anim, zone_type, class_levels, "
        "effect_id_1, effect_id_2, effect_id_3, effect_id_4, effect_id_5, effect_id_6, "
        "effect_id_7, effect_id_8, effect_id_9, effect_id_10, effect_id_11, effect_id_12, "
        "base_value_1, base_value_2, base_value_3, base_value_4, base_value_5, base_value_6, "
        "base_value_7, base_value_8, base_value_9, base_value_10, base_value_11, base_value_12, "
        "base2_value_1, base2_value_2, base2_value_3, base2_value_4, base2_value_5, base2_value_6, "
        "base2_value_7, base2_value_8, base2_value_9, base2_value_10, base2_value_11, base2_value_12, "
        "max_value_1, max_value_2, max_value_3, max_value_4, max_value_5, max_value_6, "
        "max_value_7, max_value_8, max_value_9, max_value_10, max_value_11, max_value_12, "
        "formula_1, formula_2, formula_3, formula_4, formula_5, formula_6, "
        "formula_7, formula_8, formula_9, formula_10, formula_11, formula_12, "
        "spell_affect_index, "
        "reagent_id_1, reagent_id_2, reagent_id_3, reagent_id_4, "
        "reagent_count_1, reagent_count_2, reagent_count_3, reagent_count_4, "
        "noexpend_reagent_1, noexpend_reagent_2, noexpend_reagent_3, noexpend_reagent_4, "
        "endurance_cost "
        "FROM spells WHERE name=?",
        stmtGetSpellByName_);
    if (!stmt) return info;

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.found = true;
        info.id = sqlite3_column_int(stmt, 0);
        info.name = colText(stmt, 1);
        info.playerTag = colText(stmt, 2);
        info.teleportZone = colText(stmt, 3);
        info.castOnYou = colText(stmt, 4);
        info.castOnOther = colText(stmt, 5);
        info.spellFades = colText(stmt, 6);
        info.castTimeMs = sqlite3_column_int(stmt, 7);
        info.recoveryTimeMs = sqlite3_column_int(stmt, 8);
        info.recastTimeMs = sqlite3_column_int(stmt, 9);
        info.durationFormula = sqlite3_column_int(stmt, 10);
        info.durationCap = sqlite3_column_int(stmt, 11);
        info.manaCost = sqlite3_column_int(stmt, 12);
        info.targetType = sqlite3_column_int(stmt, 13);
        info.range = static_cast<float>(sqlite3_column_double(stmt, 14));
        info.aoeRange = static_cast<float>(sqlite3_column_double(stmt, 15));
        info.resistType = sqlite3_column_int(stmt, 16);
        info.resistDiff = sqlite3_column_int(stmt, 17);
        info.castingSkill = sqlite3_column_int(stmt, 18);
        info.isBeneficial = sqlite3_column_int(stmt, 19) != 0;
        info.spellIcon = sqlite3_column_int(stmt, 20);
        info.gemIcon = sqlite3_column_int(stmt, 21);
        info.castAnim = sqlite3_column_int(stmt, 22);
        info.zoneType = sqlite3_column_int(stmt, 23);
        const void* blob = sqlite3_column_blob(stmt, 24);
        int blobLen = sqlite3_column_bytes(stmt, 24);
        info.classLevels.fill(255);
        if (blob && blobLen > 0) {
            size_t n = static_cast<size_t>(blobLen) < info.classLevels.size()
                           ? static_cast<size_t>(blobLen)
                           : info.classLevels.size();
            std::memcpy(info.classLevels.data(), blob, n);
        }
        readSpellEffectColumns(stmt, 25, info);
    }

    return info;
}

// T316: /mem <partial name> used to resolve through an exact-match query
// only, so anything but the spell's full name failed. The pattern below is
// BOUND (never string-concatenated into the SQL) and the user's text is
// escaped first: without that, a player typing "%" would produce the pattern
// "%%%" and match the entire spell table. ESCAPE '\' declares the escape
// character to SQLite; backslash itself is escaped so it stays literal too.
static std::string escapeLikePattern(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (char c : raw) {
        if (c == '%' || c == '_' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::vector<SpellNameMatch> WilleqDatabase::findSpellsByNameLike(const std::string& partial,
                                                                 int limit) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<SpellNameMatch> out;
    if (!db_ || partial.empty() || limit <= 0) return out;

    auto* stmt = prepare(
        "SELECT id, name FROM spells WHERE name LIKE ?1 ESCAPE '\\' "
        "ORDER BY length(name), name LIMIT ?2",
        stmtFindSpellsByNameLike_);
    if (!stmt) return out;

    const std::string pattern = "%" + escapeLikePattern(partial) + "%";

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SpellNameMatch m;
        m.id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
        m.name = colText(stmt, 1);
        out.push_back(std::move(m));
    }
    sqlite3_reset(stmt);
    return out;
}

uint32_t WilleqDatabase::getSpellCount() {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return 0;

    auto* stmt = prepare("SELECT count(*) FROM spells", stmtGetSpellCount_);
    if (!stmt) return 0;

    sqlite3_reset(stmt);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        return sqlite3_column_int(stmt, 0);
    return 0;
}

// ============================================================================
// String queries
// ============================================================================

std::string WilleqDatabase::getString(uint32_t stringId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return "";

    auto* stmt = prepare("SELECT text FROM eqstrings WHERE id=?", stmtGetString_);
    if (!stmt) return "";

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, stringId);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return colTextNoCR(stmt, 0);
    return "";
}

std::string WilleqDatabase::getDbString(uint32_t category, uint32_t subId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return "";

    auto* stmt = prepare(
        "SELECT text FROM dbstrings WHERE category=? AND sub_id=?",
        stmtGetDbString_);
    if (!stmt) return "";

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, category);
    sqlite3_bind_int(stmt, 2, subId);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return colTextNoCR(stmt, 0);
    return "";
}

// ============================================================================
// Item model queries
// ============================================================================

int32_t WilleqDatabase::getItemModel(uint32_t itemId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return -1;

    auto* stmt = prepare("SELECT model_id FROM item_models WHERE item_id=?",
                          stmtGetItemModel_);
    if (!stmt) return -1;

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, itemId);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return sqlite3_column_int(stmt, 0);
    return -1;
}

// ============================================================================
// S3D asset queries
// ============================================================================

AssetLocation WilleqDatabase::getAssetLocation(const std::string& assetName) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    AssetLocation loc;
    if (!db_) return loc;

    auto* stmt = prepare(
        "SELECT archive, block_offset, compressed_size, uncompressed_size "
        "FROM s3d_assets WHERE name=?",
        stmtGetAsset_);
    if (!stmt) return loc;

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, assetName.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        loc.found = true;
        loc.name = assetName;
        loc.archive = colText(stmt, 0);
        loc.blockOffset = sqlite3_column_int(stmt, 1);
        loc.compressedSize = sqlite3_column_int(stmt, 2);
        loc.uncompressedSize = sqlite3_column_int(stmt, 3);
    }

    return loc;
}

std::vector<AssetLocation> WilleqDatabase::getAssetsInArchive(const std::string& archiveName) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<AssetLocation> results;
    if (!db_) return results;

    auto* stmt = prepare(
        "SELECT name, block_offset, compressed_size, uncompressed_size "
        "FROM s3d_assets WHERE archive=?",
        stmtGetAssetsInArchive_);
    if (!stmt) return results;

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, archiveName.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AssetLocation loc;
        loc.found = true;
        loc.name = colText(stmt, 0);
        loc.archive = archiveName;
        loc.blockOffset = sqlite3_column_int(stmt, 1);
        loc.compressedSize = sqlite3_column_int(stmt, 2);
        loc.uncompressedSize = sqlite3_column_int(stmt, 3);
        results.push_back(std::move(loc));
    }

    return results;
}

// ============================================================================
// Race model queries
// ============================================================================

RaceModelInfo WilleqDatabase::getRaceModel(uint16_t raceId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    RaceModelInfo info;
    if (!db_) return info;

    auto* stmt = prepare(
        "SELECT race_id, code, name, female_code, male_code, neuter_code, "
        "s3d_file, fallback_code, animation_source, animation_source_s3d, "
        "scale, gender_variants, invisible, color FROM race_models WHERE race_id=?",
        stmtGetRaceModel_);
    if (!stmt) return info;

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, raceId);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.found = true;
        info.raceId = sqlite3_column_int(stmt, 0);
        info.code = colText(stmt, 1);
        info.name = colText(stmt, 2);
        info.femaleCode = colText(stmt, 3);
        info.maleCode = colText(stmt, 4);
        info.neuterCode = colText(stmt, 5);
        info.s3dFile = colText(stmt, 6);
        info.fallbackCode = colText(stmt, 7);
        info.animationSource = colText(stmt, 8);
        info.animationSourceS3d = colText(stmt, 9);
        info.scale = static_cast<float>(sqlite3_column_double(stmt, 10));
        info.genderVariants = sqlite3_column_int(stmt, 11) != 0;
        info.invisible = sqlite3_column_int(stmt, 12) != 0;
        info.colorARGB = static_cast<uint32_t>(sqlite3_column_int64(stmt, 13));
    }

    return info;
}

RaceModelInfo WilleqDatabase::getRaceModelByCode(const std::string& code) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    RaceModelInfo info;
    if (!db_) return info;

    auto* stmt = prepare(
        "SELECT race_id, code, name, female_code, male_code, neuter_code, "
        "s3d_file, fallback_code, animation_source, animation_source_s3d, "
        "scale, gender_variants, invisible, color FROM race_models WHERE code=? COLLATE NOCASE",
        stmtGetRaceModelByCode_);
    if (!stmt) return info;

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.found = true;
        info.raceId = sqlite3_column_int(stmt, 0);
        info.code = colText(stmt, 1);
        info.name = colText(stmt, 2);
        info.femaleCode = colText(stmt, 3);
        info.maleCode = colText(stmt, 4);
        info.neuterCode = colText(stmt, 5);
        info.s3dFile = colText(stmt, 6);
        info.fallbackCode = colText(stmt, 7);
        info.animationSource = colText(stmt, 8);
        info.animationSourceS3d = colText(stmt, 9);
        info.scale = static_cast<float>(sqlite3_column_double(stmt, 10));
        info.genderVariants = sqlite3_column_int(stmt, 11) != 0;
        info.invisible = sqlite3_column_int(stmt, 12) != 0;
        info.colorARGB = static_cast<uint32_t>(sqlite3_column_int64(stmt, 13));
    }

    return info;
}

// ============================================================================
// Sound queries
// ============================================================================

std::string WilleqDatabase::getSoundAsset(uint32_t soundId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return "";

    auto* stmt = prepare("SELECT filename FROM sound_assets WHERE id=?",
                          stmtGetSoundAsset_);
    if (!stmt) return "";

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, soundId);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return colText(stmt, 0);
    return "";
}

int32_t WilleqDatabase::getAnimationSound(const std::string& animationName) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return -1;

    auto* stmt = prepare(
        "SELECT sound_id FROM animation_sounds WHERE animation_name=?",
        stmtGetAnimSound_);
    if (!stmt) return -1;

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, animationName.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return sqlite3_column_int(stmt, 0);
    return -1;
}

// ============================================================================
// Music queries
// ============================================================================

std::string WilleqDatabase::getMusicFile(int index) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return "";

    auto* stmt = prepare("SELECT filename FROM music_files WHERE id=?",
                          stmtGetMusicFile_);
    if (!stmt) return "";

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, index);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return colText(stmt, 0);
    return "";
}

// ============================================================================
// Login string queries
// ============================================================================

std::string WilleqDatabase::getLoginString(uint32_t stringId) {
    ScopedQueryTimer timer(stats_);
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return "";

    auto* stmt = prepare("SELECT text FROM login_strings WHERE id=?",
                          stmtGetLoginString_);
    if (!stmt) return "";

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, stringId);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        return colTextNoCR(stmt, 0);
    return "";
}

} // namespace EQT
