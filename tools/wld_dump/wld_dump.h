// wld_dump.h — shared seam for the wld_dump tool (T447)
//
// The tool was one 1,436-line TU riding the 1,500-line ceiling, and 1,073 of
// those lines were a single dumpWld() function. It is now four files split by
// the PHASE of the dump:
//
//   tools/wld_dump.cpp                   printUsage() + main() (archive I/O)
//   tools/wld_dump/wld_dump_core.cpp     fragment-type names, hex dump, and
//                                        dumpWld(): header, string table,
//                                        fragment index + details table
//   tools/wld_dump/wld_dump_geometry.cpp the geometry/lighting fragments
//                                        (0x10, 0x36, 0x1B, 0x1C, 0x28,
//                                        0x2A, 0x35)
//   tools/wld_dump/wld_dump_animation.cpp the animation/actor fragments
//                                        (0x12, 0x13, 0x14, 0x15, 0x17,
//                                        0x18, 0x37, 0x2F, 0x2D) and the
//                                        hex fallback for every other type
//
// The two phase functions were contiguous statement ranges inside dumpWld(),
// so they read exactly the locals dumpWld() had built by then. WldDumpFragments
// carries those four values across the seam rather than re-deriving them; each
// phase re-binds them to the original local names at its top, which is why the
// moved bodies are verbatim.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The fragment index dumpWld() builds in its first phase.
struct WldDumpFragments {
    const char* fragPtr = nullptr;      // first byte of the fragment block
    size_t remaining = 0;               // bytes of fragment block
    uint32_t fragmentCount = 0;         // count from the file header
    // (string index, string) pairs, index negated as the nameRefs are
    const std::vector<std::pair<int, std::string>>* strings = nullptr;
    const std::map<uint32_t, int>* typeCounts = nullptr;
};

std::string getFragmentTypeName(uint32_t type);
void dumpHex(const char* data, size_t len, size_t maxBytes = 0);

void dumpWld(const std::vector<char>& data, const std::string& name);
void dumpGeometryFragments(const WldDumpFragments& frags);
void dumpAnimationFragments(const WldDumpFragments& frags);
