#pragma once

// B964: named owner rows a subsystem publishes beside the ledger's
// categories -- bytes the allocator cannot see (GPU textures on Lima live in
// GEM objects, on llvmpipe inside gpu_driver), reported by their owner so
// every byte is on a named row. recordPoint() (memory_ledger.cpp) prints one
//
//   [memledger-owner] point=<p> category=<c> owner=<name> live=<bytes> <detail>
//
// line per published row at every lifecycle point -- the same line shape
// gles2_zone_benchmark printed for tex.impostor in B952, so one parser reads
// both. A row is a REPORT, never an allocation: it adds nothing to any
// category's live bytes (no double counting), and its bytes are whatever the
// owner says it holds (the logical size of its textures).
//
// Header-only (inline variables) so that every target linking the ledger gets
// it without a CMake source-list change. Thread-safe: rows are published from
// the render thread and read by whichever thread records a point.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace eqt {
namespace memory {

struct OwnerRow {
    std::string category;   // the ledger category the bytes belong under
    uint64_t liveBytes = 0;
    std::string detail;     // extra `key=value` tokens, space separated
};

namespace owner_rows_detail {
inline std::mutex& mutex() {
    static std::mutex m;
    return m;
}
inline std::map<std::string, OwnerRow>& rows() {
    static std::map<std::string, OwnerRow> r;
    return r;
}
}  // namespace owner_rows_detail

// Publish (or replace) the row `owner`.
inline void setOwnerRow(const std::string& owner, const std::string& category, uint64_t liveBytes,
                        const std::string& detail = std::string()) {
    std::lock_guard<std::mutex> lock(owner_rows_detail::mutex());
    OwnerRow& r = owner_rows_detail::rows()[owner];
    r.category = category;
    r.liveBytes = liveBytes;
    r.detail = detail;
}

// Withdraw the row (the owner no longer exists).
inline void clearOwnerRow(const std::string& owner) {
    std::lock_guard<std::mutex> lock(owner_rows_detail::mutex());
    owner_rows_detail::rows().erase(owner);
}

// The `[memledger-owner]` lines for `point`, newline separated, no trailing
// newline; empty when nothing is published.
inline std::string ownerRowsToLogLines(const std::string& point) {
    std::lock_guard<std::mutex> lock(owner_rows_detail::mutex());
    std::string out;
    for (const auto& [name, r] : owner_rows_detail::rows()) {
        if (!out.empty()) out += '\n';
        out += "[memledger-owner] point=" + point + " category=" + r.category + " owner=" + name +
               " live=" + std::to_string(r.liveBytes);
        if (!r.detail.empty()) out += " " + r.detail;
    }
    return out;
}

}  // namespace memory
}  // namespace eqt
