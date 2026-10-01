#pragma once

// B962 (GitLab #280): ONE switch that restores the client's pre-B962 GPU
// buffer allocation pattern, so the Orange Pi A/B (refactor/B962/README.md)
// runs both sides from the same binary, the way WILLEQ_ZONE_POOLS=0 does for
// the T504 pools:
//
//   WILLEQ_GPU_BUFFERS_LEGACY=1
//     - the zone draw pools draw from client-side vertex arrays (no shared
//       static VBO: Mesa streams every run's vertex range each frame);
//     - the GPU upload thread's shared EGL context is created even with the
//       pools on, and setupMinimalZoneCollision() rebuilds the player's
//       region as a second, undrawn node whose VBO that thread uploads;
//     - an EQE1 texture uploads every mip level the pack holds with the
//       default minification filter in place, so Mesa allocates level 0
//       alone and parks each further level in a buffer of its own.
//
// Unset (the default) is the fixed behaviour. Read once.

#include <cstdlib>

namespace EQT {
namespace Graphics {

inline bool gpuBuffersLegacy() {
    static const bool legacy = [] {
        const char* v = std::getenv("WILLEQ_GPU_BUFFERS_LEGACY");
        return v && v[0] == '1';
    }();
    return legacy;
}

} // namespace Graphics
} // namespace EQT
