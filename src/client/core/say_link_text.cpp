// T395 Leg B (board B205). See include/client/say_link_text.h for the wire
// format, the eqemu producer and the two measured strings.

#include "client/say_link_text.h"

namespace eqt {

std::string StripSayLinks(const std::string& text) {
    if (text.find(kSayLinkDelimiter) == std::string::npos) {
        return text;
    }

    std::string out;
    out.reserve(text.size());

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t open = text.find(kSayLinkDelimiter, pos);
        if (open == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, open - pos);

        const size_t close = text.find(kSayLinkDelimiter, open + 1);
        if (close == std::string::npos) {
            // Unpaired delimiter: not a link. Emit the rest verbatim.
            out.append(text, open, std::string::npos);
            break;
        }

        const size_t payload = close - open - 1;
        if (payload > kTitaniumSayLinkBodyChars) {
            // <body><display text>
            out.append(text, open + 1 + kTitaniumSayLinkBodyChars,
                       payload - kTitaniumSayLinkBodyChars);
        } else {
            // Too short to be a Titanium link body -- keep whatever is there,
            // minus the delimiters, rather than dropping a name.
            out.append(text, open + 1, payload);
        }
        pos = close + 1;
    }

    return out;
}

}  // namespace eqt
