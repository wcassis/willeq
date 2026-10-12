/*
 * StringDatabase - string lookup over the shipped string tables.
 *
 * Queries go through WilleqDatabase (data/willeq.db's eqstrings/dbstrings
 * tables). The legacy eqstr_us.txt / dbstr_us.txt text-file loaders were
 * B606 (#189) dead: the client initializes exclusively via
 * initFromDatabase() (eq_strings.cpp's StringDb::LoadStringFiles), so the
 * file mode was reachable only from tests.
 */

#ifndef EQT_STRING_DATABASE_H
#define EQT_STRING_DATABASE_H

#include <atomic>
#include <cstdint>
#include <string>
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

    // Initialize from WilleqDatabase global instance (SQLite-backed)
    bool initFromDatabase();

    // ========================================================================
    // Lookups
    // ========================================================================

    // Get string by ID from the eqstrings table
    std::string getString(uint32_t string_id) const;

    // Get string by category and sub_id from the dbstrings table
    std::string getDbString(uint32_t category, uint32_t sub_id) const;

    // Format string with placeholder substitution
    std::string formatString(uint32_t string_id, const std::vector<std::string>& args) const;

    // Format a template string directly with arguments
    std::string formatTemplate(const std::string& tmpl, const std::vector<std::string>& args) const;

    // ========================================================================
    // State
    // ========================================================================

    bool isLoaded() const { return db_ != nullptr; }

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

    // Stats
    mutable std::atomic<uint64_t> query_count_{0};
};

} // namespace EQT

#endif // EQT_STRING_DATABASE_H
