#pragma once

// J04 (Batch J): private internal header for src/client/action/command_processor*.cpp.
// Holds the two anonymous-namespace helpers that were used across a cluster
// boundary when command_processor.cpp was split -- parseU32Strict/splitWhitespace
// are called from cmdRecipeSearch et al (command_processor_world.cpp) AND from
// cmdAppearance (command_processor_spells_gm.cpp). Marked inline because they
// lose internal linkage here. Sanctioned Batch-F exception, T166 wld_internal.h
// precedent. Not for inclusion outside src/client/action/.

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace eqt {
namespace action {
namespace detail {

// Parse a decimal uint32 with no trailing junk. Returns false on any
// malformed input (empty, negative, non-numeric, overflow, trailing chars).
inline bool parseU32Strict(const std::string& text, uint32_t& out) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(text, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    if (consumed != text.size()) {
        return false;
    }
    if (parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

inline std::vector<std::string> splitWhitespace(const std::string& text) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = text.find_first_not_of(" \t", i);
        if (start == std::string::npos) break;
        size_t end = text.find_first_of(" \t", start);
        if (end == std::string::npos) end = text.size();
        parts.push_back(text.substr(start, end - start));
        i = end;
    }
    return parts;
}

} // namespace detail
} // namespace action
} // namespace eqt
