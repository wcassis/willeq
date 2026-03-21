#ifndef EQT_WILLEQ_DATABASE_H
#define EQT_WILLEQ_DATABASE_H

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

struct sqlite3;
struct sqlite3_stmt;

namespace EQT {

// Runtime query stats for /pmem and /ft diagnostics
struct DatabaseStats {
    std::atomic<uint64_t> queryCount{0};
    std::atomic<uint64_t> totalQueryTimeUs{0};  // Microseconds
    std::atomic<uint64_t> peakQueryTimeUs{0};

    void recordQuery(uint64_t elapsedUs) {
        queryCount.fetch_add(1, std::memory_order_relaxed);
        totalQueryTimeUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        uint64_t prev = peakQueryTimeUs.load(std::memory_order_relaxed);
        while (elapsedUs > prev &&
               !peakQueryTimeUs.compare_exchange_weak(prev, elapsedUs,
                   std::memory_order_relaxed)) {}
    }

    double avgQueryTimeUs() const {
        uint64_t count = queryCount.load(std::memory_order_relaxed);
        return count > 0 ? static_cast<double>(totalQueryTimeUs.load()) / count : 0;
    }
};

// S3D asset location returned by getAssetLocation()
struct AssetLocation {
    std::string name;            // e.g., "clkshk01.bmp"
    std::string archive;         // e.g., "qeynos2.s3d"
    uint32_t blockOffset = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    bool found = false;
};

// Zone character model entry
struct ZoneCharacterEntry {
    std::string modelCode;
    std::string sourceFile;
};

// Spell data subset for runtime queries
struct SpellInfo {
    uint32_t id = 0;
    std::string name;
    std::string playerTag;
    std::string teleportZone;
    std::string castOnYou;
    std::string castOnOther;
    std::string spellFades;
    uint32_t castTimeMs = 0;
    uint32_t recoveryTimeMs = 0;
    uint32_t recastTimeMs = 0;
    uint8_t durationFormula = 0;
    uint32_t durationCap = 0;
    int32_t manaCost = 0;
    int32_t targetType = 0;
    float range = 0;
    float aoeRange = 0;
    int32_t resistType = 0;
    int32_t resistDiff = 0;
    int32_t castingSkill = 0;
    bool isBeneficial = false;
    uint16_t spellIcon = 0;
    uint16_t gemIcon = 0;
    uint16_t castAnim = 0;
    uint8_t zoneType = 0;
    bool found = false;
};

// Central database for all EQ client data lookups.
// Replaces in-memory parsed data structures with on-demand SQLite queries.
// Thread-safe: WAL mode allows concurrent readers from game and sim threads.
class WilleqDatabase {
public:
    WilleqDatabase();
    ~WilleqDatabase();

    // Non-copyable
    WilleqDatabase(const WilleqDatabase&) = delete;
    WilleqDatabase& operator=(const WilleqDatabase&) = delete;

    // Open the database file. Returns false on failure.
    // Call once at startup. FATAL if database is missing or invalid.
    bool open(const std::string& dbPath);

    // Close the database (called on shutdown).
    void close();

    bool isOpen() const { return db_ != nullptr; }

    // ── Spell queries ──
    SpellInfo getSpell(uint32_t spellId);
    SpellInfo getSpellByName(const std::string& name);
    uint32_t getSpellCount();

    // ── String queries ──
    std::string getString(uint32_t stringId);
    std::string getDbString(uint32_t category, uint32_t subId);

    // ── Item model queries ──
    int32_t getItemModel(uint32_t itemId);  // Returns -1 if not found

    // ── S3D asset queries ──
    AssetLocation getAssetLocation(const std::string& assetName);
    std::vector<AssetLocation> getAssetsInArchive(const std::string& archiveName);

    // ── Zone character queries ──
    std::vector<ZoneCharacterEntry> getZoneCharacters(const std::string& zoneName);

    // ── Sound queries ──
    std::string getSoundAsset(uint32_t soundId);
    int32_t getAnimationSound(const std::string& animationName);  // Returns -1 if not found

    // ── Music queries ──
    std::string getMusicFile(int index);

    // ── Login string queries ──
    std::string getLoginString(uint32_t stringId);

    // ── Stats ──
    const DatabaseStats& getStats() const { return stats_; }
    void resetStats() {
        stats_.queryCount.store(0, std::memory_order_relaxed);
        stats_.totalQueryTimeUs.store(0, std::memory_order_relaxed);
        stats_.peakQueryTimeUs.store(0, std::memory_order_relaxed);
    }
    size_t getMemoryUsage() const;  // SQLite page cache + overhead

private:
    // Prepare a statement and cache it. Returns the cached stmt.
    sqlite3_stmt* prepare(const char* sql, sqlite3_stmt*& cached);

    // Helper: extract a string column, returns "" if null
    static std::string colText(sqlite3_stmt* stmt, int col);

    sqlite3* db_ = nullptr;
    DatabaseStats stats_;

    // Cached prepared statements (one per query type)
    sqlite3_stmt* stmtGetSpell_ = nullptr;
    sqlite3_stmt* stmtGetSpellByName_ = nullptr;
    sqlite3_stmt* stmtGetSpellCount_ = nullptr;
    sqlite3_stmt* stmtGetString_ = nullptr;
    sqlite3_stmt* stmtGetDbString_ = nullptr;
    sqlite3_stmt* stmtGetItemModel_ = nullptr;
    sqlite3_stmt* stmtGetAsset_ = nullptr;
    sqlite3_stmt* stmtGetAssetsInArchive_ = nullptr;
    sqlite3_stmt* stmtGetZoneChars_ = nullptr;
    sqlite3_stmt* stmtGetSoundAsset_ = nullptr;
    sqlite3_stmt* stmtGetAnimSound_ = nullptr;
    sqlite3_stmt* stmtGetMusicFile_ = nullptr;
    sqlite3_stmt* stmtGetLoginString_ = nullptr;
};

} // namespace EQT

#endif // EQT_WILLEQ_DATABASE_H
