#pragma once

// T504 / Will 2026-09-21: "Baseline is feature parity with the original client."
// The non-original features -- detail objects, reactive foliage, boids,
// tumbleweeds, atmospheric particles, animated trees, storm clouds, fire glow,
// and navmesh pathfinding (/moveto, follow) -- are measured SEPARATELY, on top
// of a baseline that does not contain them. WILLEQ_BASELINE=1 runs that
// baseline. Header-only and dependency-free so game code and renderer code can
// both ask.

#include <cstdlib>

namespace eqt {

inline bool BaselineOnly() {
    static const bool v = [] {
        const char* e = std::getenv("WILLEQ_BASELINE");
        return e && e[0] == '1';
    }();
    return v;
}

}  // namespace eqt
