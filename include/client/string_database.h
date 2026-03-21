/*
 * StringDatabase - EverQuest client string file loader
 *
 * Primary mode: SQLite-backed queries via WilleqDatabase.
 * Legacy mode: Loads and parses eqstr_us.txt and dbstr_us.txt from the EQ client directory.
 */

#ifndef EQT_STRING_DATABASE_H
#define EQT_STRING_DATABASE_H

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>

namespace EQT {

class WilleqDatabase;

class StringDatabase {
public:
    StringDatabase() = default;
    ~StringDatabase() = default;

    // Non-copyable
    StringDatabase(const StringDatabase&) = delete;
    StringDatabase& operator=(const StringDatabase&) = delete;

    // Moveable
    StringDatabase(StringDatabase&&) = default;
    StringDatabase& operator=(StringDatabase&&) = default;

    // ========================================================================
    // Initialization
    // ========================================================================

    // Initialize from WilleqDatabase (SQLite-backed, preferred)
    bool initFromDatabase(WilleqDatabase* db);

    // Legacy: Load eqstr_us.txt from EQ client directory
    bool loadEqStrFile(const std::string& filepath);

    // Legacy: Load dbstr_us.txt from EQ client directory
    bool loadDbStrFile(const std::string& filepath);

    // ========================================================================
    // Lookups
    // ========================================================================

    // Get string by ID from eqstr_us.txt / eqstrings table
    std::string getString(uint32_t string_id) const;

    // Get string by category and sub_id from dbstr_us.txt / dbstrings table
    std::string getDbString(uint32_t category, uint32_t sub_id) const;

    // Format string with placeholder substitution
    std::string formatString(uint32_t string_id, const std::vector<std::string>& args) const;

    // Format a template string directly with arguments
    std::string formatTemplate(const std::string& tmpl, const std::vector<std::string>& args) const;

    // ========================================================================
    // State
    // ========================================================================

    bool isEqStrLoaded() const { return eqstr_loaded_; }
    bool isDbStrLoaded() const { return dbstr_loaded_; }
    bool isLoaded() const { return eqstr_loaded_; }

    size_t getEqStrCount() const;
    size_t getDbStrCount() const;

    // Cache/query stats for /pmem
    uint64_t getQueryCount() const { return query_count_.load(std::memory_order_relaxed); }

private:
    // Parse a single placeholder and substitute
    std::string substitutePlaceholder(const std::string& tmpl, size_t& pos,
                                      const std::vector<std::string>& args,
                                      size_t& arg_index) const;

    // SQLite database (non-owning pointer)
    WilleqDatabase* db_ = nullptr;
    bool using_db_ = false;

    // Legacy storage
    std::unordered_map<uint32_t, std::string> eq_strings_;
    std::map<std::pair<uint32_t, uint32_t>, std::string> db_strings_;

    bool eqstr_loaded_ = false;
    bool dbstr_loaded_ = false;

    // Stats
    mutable std::atomic<uint64_t> query_count_{0};
};

} // namespace EQT

#endif // EQT_STRING_DATABASE_H
