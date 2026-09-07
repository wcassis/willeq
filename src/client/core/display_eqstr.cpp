/*
 * T429 Leg A (board B390) -- see include/client/display_eqstr.h for why this
 * exists and why it is NOT a second formatter.
 */

#include "client/display_eqstr.h"

#include "client/string_database.h"

#include <cctype>
#include <cstdio>

namespace eqt {

std::string EqStrLine(const EQT::StringDatabase& strings, uint32_t id,
                      const std::vector<std::string>& args) {
    // formatString() already returns "" for a missing row, and runs the one
    // shared substitution engine for a present one. Nothing else to do.
    return strings.formatString(id, args);
}

std::string EqStrPrintfFloat(const EQT::StringDatabase& strings, uint32_t id,
                             double value) {
    const std::string tmpl = strings.getString(id);
    if (tmpl.empty()) {
        return "";
    }

    // Find one printf float conversion: '%', optional digits (width),
    // optional '.'+digits (precision), then 'f'. Deliberately narrow -- it
    // must NOT match the positional "%1" rows, nor the trailing literal '%'
    // of "Shielding: %1%".
    for (size_t i = 0; i + 1 < tmpl.size(); ++i) {
        if (tmpl[i] != '%') {
            continue;
        }
        size_t j = i + 1;
        while (j < tmpl.size() && std::isdigit(static_cast<unsigned char>(tmpl[j]))) {
            ++j;
        }
        int precision = 6;  // C's default for %f
        if (j < tmpl.size() && tmpl[j] == '.') {
            size_t p = j + 1;
            int prec = 0;
            bool any = false;
            while (p < tmpl.size() && std::isdigit(static_cast<unsigned char>(tmpl[p]))) {
                prec = prec * 10 + (tmpl[p] - '0');
                ++p;
                any = true;
            }
            if (!any) {
                continue;
            }
            precision = prec;
            j = p;
        }
        if (j >= tmpl.size() || tmpl[j] != 'f') {
            continue;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
        return tmpl.substr(0, i) + buf + tmpl.substr(j + 1);
    }

    // No float conversion in the shipped row -- hand it back untouched.
    return tmpl;
}

}  // namespace eqt
