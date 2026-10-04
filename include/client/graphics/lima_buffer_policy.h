#pragma once

// B986: Lima's BO cache retains freed stream uploads and draw-time buffers
// outside RssAnon. The real-Pi crowd census identifies it as the excess over
// the unchanged unnamed-GEM guard (refactor/B986_pi_gpu_guard.md). Prefer
// releasing those buffers to retaining them for reuse: memory first.
// This must run BEFORE createDeviceEx: GBM/EGL initializes Lima before the
// renderer can query GL_RENDERER. Other drivers ignore LIMA_DEBUG.

#include <cstdlib>
#include <string>

namespace EQT {
namespace Graphics {

inline bool configureLimaBufferPolicy(bool useDRM) {
    if (!useDRM) return true;
    const char* current = std::getenv("LIMA_DEBUG");
    std::string flags = current ? current : "";
    // Mesa's special "all" value already includes NO_BO_CACHE; appending
    // would stop it being recognized as the special value.
    if (flags == "all") return true;
    // Mesa accepts comma/space-separated debug options. Match whole tokens,
    // retaining the caller's other options and avoiding duplicates.
    const std::string delimiters = ", \t\n\r";
    for (size_t begin = flags.find_first_not_of(delimiters); begin != std::string::npos;) {
        const size_t end = flags.find_first_of(delimiters, begin);
        if (flags.substr(begin, end == std::string::npos ? end : end - begin) == "nobocache")
            return true;
        begin = flags.find_first_not_of(delimiters, end);
    }
    if (!flags.empty()) flags += ',';
    flags += "nobocache";
    return ::setenv("LIMA_DEBUG", flags.c_str(), 1) == 0;
}

} // namespace Graphics
} // namespace EQT
