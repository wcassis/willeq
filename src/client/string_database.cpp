#include "client/string_database.h"
#include "client/willeq_database.h"
#include "common/logging.h"

#include <cctype>
#include <algorithm>

namespace EQT {

// ============================================================================
// SQLite initialization
// ============================================================================

bool StringDatabase::initFromDatabase() {
    auto* db = WilleqDatabase::globalInstance();
    if (!db || !db->isOpen()) {
        return false;
    }

    db_ = db;
    using_db_ = true;

    LOG_INFO(MOD_MAIN, "StringDatabase: initialized from SQLite");
    return true;
}

std::string StringDatabase::getString(uint32_t string_id) const {
    query_count_.fetch_add(1, std::memory_order_relaxed);

    if (using_db_ && db_) {
        return db_->getString(string_id);
    }
    return "";
}

std::string StringDatabase::getDbString(uint32_t category, uint32_t sub_id) const {
    query_count_.fetch_add(1, std::memory_order_relaxed);

    if (using_db_ && db_) {
        return db_->getDbString(category, sub_id);
    }
    return "";
}

std::string StringDatabase::formatString(uint32_t string_id, const std::vector<std::string>& args) const {
    std::string tmpl = getString(string_id);
    if (tmpl.empty()) {
        return "";
    }
    return formatTemplate(tmpl, args);
}

std::string StringDatabase::formatTemplate(const std::string& tmpl, const std::vector<std::string>& args) const {
    std::string result;
    result.reserve(tmpl.size() * 2);  // Estimate output size

    size_t arg_index = 0;  // Track which arg we're on for sequential access
    size_t pos = 0;

    while (pos < tmpl.size()) {
        char c = tmpl[pos];

        // Check for placeholder markers: %, #, @
        if ((c == '%' || c == '#' || c == '@') && pos + 1 < tmpl.size()) {
            size_t start_pos = pos;
            std::string substituted = substitutePlaceholder(tmpl, pos, args, arg_index);

            if (pos != start_pos) {
                // Placeholder was processed
                result += substituted;
                continue;
            }
        }

        // Regular character
        result += c;
        pos++;
    }

    return result;
}

std::string StringDatabase::substitutePlaceholder(const std::string& tmpl, size_t& pos,
                                                   const std::vector<std::string>& args,
                                                   size_t& arg_index) const {
    char marker = tmpl[pos];
    size_t start_pos = pos;

    // Check for %T (string ID lookup)
    if (marker == '%' && pos + 1 < tmpl.size() && (tmpl[pos + 1] == 'T' || tmpl[pos + 1] == 't')) {
        pos += 2;  // Skip %T

        // Parse the number
        size_t num_start = pos;
        while (pos < tmpl.size() && std::isdigit(tmpl[pos])) {
            pos++;
        }

        if (pos > num_start) {
            int arg_num = std::stoi(tmpl.substr(num_start, pos - num_start));
            size_t idx = static_cast<size_t>(arg_num - 1);  // %T1 = args[0]

            if (idx < args.size()) {
                // Parse the arg as a string ID
                uint32_t lookup_id;
                try {
                    lookup_id = static_cast<uint32_t>(std::stoul(args[idx]));
                } catch (...) {
                    // Not a valid number, return the arg directly
                    return args[idx];
                }

                // Look up the string
                std::string looked_up = getString(lookup_id);
                if (looked_up.empty()) {
                    // String not found, return the ID as-is
                    return args[idx];
                }

                // The looked-up string may have its own placeholders
                // These use ABSOLUTE arg indices from the original args
                // (e.g., %3 in the looked-up string refers to args[2])
                return formatTemplate(looked_up, args);
            }
            // Arg index out of range
            return "";
        }

        // No number after %T, reset
        pos = start_pos;
        return "";
    }

    // Check for %B (ability/buff lookup) - format: %B1(N)
    if (marker == '%' && pos + 1 < tmpl.size() && (tmpl[pos + 1] == 'B' || tmpl[pos + 1] == 'b')) {
        pos += 2;  // Skip %B

        // Parse the number
        size_t num_start = pos;
        while (pos < tmpl.size() && std::isdigit(tmpl[pos])) {
            pos++;
        }

        // Check for (N) suffix
        if (pos < tmpl.size() && tmpl[pos] == '(') {
            size_t paren_end = tmpl.find(')', pos);
            if (paren_end != std::string::npos) {
                pos = paren_end + 1;
            }
        }

        if (pos > num_start) {
            int arg_num = std::stoi(tmpl.substr(num_start, pos - num_start - (tmpl[pos-1] == ')' ? 3 : 0)));
            size_t idx = static_cast<size_t>(arg_num - 1);

            if (idx < args.size()) {
                // For now, just return the arg directly
                // Full %B support would require ability name lookups
                return args[idx];
            }
            return "";
        }

        pos = start_pos;
        return "";
    }

    // Check for %z (duration placeholder)
    if (marker == '%' && pos + 1 < tmpl.size() && (tmpl[pos + 1] == 'z' || tmpl[pos + 1] == 'Z')) {
        pos += 2;  // Skip %z
        // Duration would come from spell data, not args
        // For now, return a placeholder
        return "[duration]";
    }

    // Check for regular placeholder: %N, #N, @N
    if (pos + 1 < tmpl.size() && std::isdigit(tmpl[pos + 1])) {
        pos++;  // Skip marker

        // Parse the number
        size_t num_start = pos;
        while (pos < tmpl.size() && std::isdigit(tmpl[pos])) {
            pos++;
        }

        if (pos > num_start) {
            int arg_num = std::stoi(tmpl.substr(num_start, pos - num_start));
            size_t idx = static_cast<size_t>(arg_num - 1);  // %1 = args[0]

            if (idx < args.size()) {
                return args[idx];
            }
            // Arg index out of range
            return "";
        }
    }

    // Not a recognized placeholder, don't advance
    return "";
}

} // namespace EQT
