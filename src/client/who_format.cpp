#include "client/who_format.h"

namespace EQT {

std::string substituteWhoCount(const std::string& tmpl, uint32_t count,
                               bool& substituted) {
    const std::string placeholder = "%1";
    const std::string value = std::to_string(count);

    std::string out = tmpl;
    substituted = false;
    size_t pos = 0;
    while ((pos = out.find(placeholder, pos)) != std::string::npos) {
        out.replace(pos, placeholder.size(), value);
        pos += value.size();  // don't rescan the inserted digits
        substituted = true;
    }
    return out;
}

}  // namespace EQT
