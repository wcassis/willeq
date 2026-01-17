/*
 * StringDatabase - EverQuest client string file loader
 *
 * Loads and parses eqstr_us.txt and dbstr_us.txt from the EQ client directory
 * to provide proper string lookups for FormattedMessage and SimpleMessage packets.
 */

#ifndef EQT_STRING_DATABASE_H
#define EQT_STRING_DATABASE_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>

namespace EQT {

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

    /*
     * Load eqstr_us.txt from EQ client directory
     * Format: Header lines followed by "<id> <text>" per line
     * Returns true on success
     */
    bool loadEqStrFile(const std::string& filepath);

    /*
     * Load dbstr_us.txt from EQ client directory
     * Format: "<category>^<sub_id>^<text>" per line
     * Returns true on success
     */
    bool loadDbStrFile(const std::string& filepath);

    /*
     * Get string by ID from eqstr_us.txt
     * Returns empty string if not found
     */
    std::string getString(uint32_t string_id) const;

    /*
     * Get string by category and sub_id from dbstr_us.txt
     * Returns empty string if not found
     */
    std::string getDbString(uint32_t category, uint32_t sub_id) const;

    /*
     * Format string with placeholder substitution
     *
     * Placeholder types:
     *   %1, %2, etc. - Direct substitution from args
     *   %T1, %T2, etc. - Parse arg as string ID, look up, substitute result
     *   %B1(N) - Ability/buff lookup (not yet implemented)
     *   #1, @1 - Numeric formatting variants
     *   %z - Duration placeholder
     *
     * Nested placeholders: If a %T lookup returns a string with its own
     * placeholders, those are filled from remaining args.
     *
     * @param string_id The template string ID
     * @param args Arguments for placeholder substitution
     * @return Formatted string, or empty if string_id not found
     */
    std::string formatString(uint32_t string_id, const std::vector<std::string>& args) const;

    /*
     * Format a template string directly with arguments
     * Use this when you already have the template text
     */
    std::string formatTemplate(const std::string& tmpl, const std::vector<std::string>& args) const;

    /*
     * Check if string files have been loaded
     */
    bool isEqStrLoaded() const { return eqstr_loaded_; }
    bool isDbStrLoaded() const { return dbstr_loaded_; }
    bool isLoaded() const { return eqstr_loaded_; }

    /*
     * Get count of loaded strings
     */
    size_t getEqStrCount() const { return eq_strings_.size(); }
    size_t getDbStrCount() const { return db_strings_.size(); }

private:
    // Parse a single placeholder and substitute
    // Returns the substituted text and advances the position past the placeholder
    std::string substitutePlaceholder(const std::string& tmpl, size_t& pos,
                                      const std::vector<std::string>& args,
                                      size_t& arg_index) const;

    // EQ string storage: string_id -> text
    std::unordered_map<uint32_t, std::string> eq_strings_;

    // DB string storage: (category, sub_id) -> text
    std::map<std::pair<uint32_t, uint32_t>, std::string> db_strings_;

    bool eqstr_loaded_ = false;
    bool dbstr_loaded_ = false;
};

} // namespace EQT

#endif // EQT_STRING_DATABASE_H
