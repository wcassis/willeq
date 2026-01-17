#include "client/string_database.h"
#include "common/logging.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace EQT {

bool StringDatabase::loadEqStrFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_MAIN, "StringDatabase: Failed to open eqstr file: {}", filepath);
        return false;
    }

    eq_strings_.clear();
    eqstr_loaded_ = false;

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;

        // Skip first two header lines (e.g., "EQST0002" and "0 5901")
        if (line_num <= 2) {
            continue;
        }

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Format: "<id> <text>"
        // Find first space to separate ID from text
        size_t space_pos = line.find(' ');
        if (space_pos == std::string::npos) {
            // Line with only ID, no text - store empty string
            try {
                uint32_t id = static_cast<uint32_t>(std::stoul(line));
                eq_strings_[id] = "";
            } catch (...) {
                // Skip malformed lines
                continue;
            }
            continue;
        }

        // Parse ID
        std::string id_str = line.substr(0, space_pos);
        uint32_t id;
        try {
            id = static_cast<uint32_t>(std::stoul(id_str));
        } catch (...) {
            // Skip malformed lines
            continue;
        }

        // Rest is the text (strip trailing \r from Windows line endings)
        std::string text = line.substr(space_pos + 1);
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
        }

        eq_strings_[id] = text;
    }

    eqstr_loaded_ = true;
    LOG_INFO(MOD_MAIN, "StringDatabase: Loaded {} strings from eqstr file", eq_strings_.size());
    return true;
}

bool StringDatabase::loadDbStrFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_MAIN, "StringDatabase: Failed to open dbstr file: {}", filepath);
        return false;
    }

    db_strings_.clear();
    dbstr_loaded_ = false;

    std::string line;

    while (std::getline(file, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Format: "<category>^<sub_id>^<text>"
        size_t first_caret = line.find('^');
        if (first_caret == std::string::npos) {
            continue;
        }

        size_t second_caret = line.find('^', first_caret + 1);
        if (second_caret == std::string::npos) {
            continue;
        }

        // Parse category and sub_id
        uint32_t category, sub_id;
        try {
            category = static_cast<uint32_t>(std::stoul(line.substr(0, first_caret)));
            sub_id = static_cast<uint32_t>(std::stoul(line.substr(first_caret + 1, second_caret - first_caret - 1)));
        } catch (...) {
            // Skip malformed lines
            continue;
        }

        // Rest is the text (strip trailing \r from Windows line endings)
        std::string text = line.substr(second_caret + 1);
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
        }

        db_strings_[{category, sub_id}] = text;
    }

    dbstr_loaded_ = true;
    LOG_INFO(MOD_MAIN, "StringDatabase: Loaded {} strings from dbstr file", db_strings_.size());
    return true;
}

std::string StringDatabase::getString(uint32_t string_id) const {
    auto it = eq_strings_.find(string_id);
    if (it != eq_strings_.end()) {
        return it->second;
    }
    return "";
}

std::string StringDatabase::getDbString(uint32_t category, uint32_t sub_id) const {
    auto it = db_strings_.find({category, sub_id});
    if (it != db_strings_.end()) {
        return it->second;
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
