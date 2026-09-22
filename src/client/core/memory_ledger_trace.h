// T504: the memory ledger's stack-trace instrument -- internal interface
// between memory_ledger.cpp (the allocator hooks and recordPoint) and
// memory_ledger_trace.cpp (site table, backtraces, report). Not a public
// header; include/client/core/memory_ledger.h declares the public switch
// (traceConfigure/traceReport).
#pragma once
#include <cstdint>
#include <string>

namespace eqt {
namespace memory {
namespace trace_detail {

inline uint8_t traceIdOf(uint32_t catWord) { return static_cast<uint8_t>(catWord >> 24); }
inline void packTraceId(uint32_t& catWord, uint8_t id) {
    catWord = (catWord & 0x00FFFFFFu) | (static_cast<uint32_t>(id) << 24);
}

// Called from the alloc hook. Returns the site id (0 = not traced).
uint8_t traceSiteFor(uint8_t cat, uint64_t bytes);
void traceCredit(uint32_t catWord, uint64_t bytes);
void traceRecharge(uint32_t catWord, uint64_t bytes);
// recordPoint(): report first, then arm if this is the arm point.
void traceArmAt(const std::string& point);
std::string traceToLogLines(const std::string& point);

}  // namespace trace_detail
}  // namespace memory
}  // namespace eqt
