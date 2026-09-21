/*
 * DisplaySettings — environment/rendering quality settings.
 *
 * U07c2: Extracted from deleted options_window.h.
 * Pure data struct, no UI dependency.
 */

#pragma once

#include <cstdlib>
#include <cstdint>

namespace eqt {
namespace ui {

enum class EffectQuality {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

// T504 / Will 2026-09-21: "Baseline is feature parity with the original client."
// The detail system, boids, tumbleweeds, atmospheric particles and the other
// non-original features are measured SEPARATELY, on top of a baseline that does
// not contain them. WILLEQ_BASELINE=1 runs that baseline: the non-original
// switches below are forced off and the DetailManager is never created (it holds
// the zone's combined geometry, which pinned a baseline structure in memory).
inline bool BaselineOnly() {
    static const bool v = [] {
        const char* e = std::getenv("WILLEQ_BASELINE");
        return e && e[0] == '1';
    }();
    return v;
}

struct DisplaySettings {
    float renderDistance = 300.0f;

    EffectQuality environmentQuality = EffectQuality::Medium;
    bool atmosphericParticles = true;
    bool ambientCreatures = true;
    bool reactiveFoliage = true;
    bool rollingObjects = true;
    bool skyEnabled = true;
    bool animatedTrees = true;
    bool fireEffects = true;
    bool enableFireGlowLighting = true;
    bool enableFireGlowIcospheres = false;
    int maxFireGlowLights = 4;
    float environmentDensity = 0.5f;

    bool detailObjectsEnabled = true;
    float detailDensity = 1.0f;
    bool detailGrass = true;
    bool detailPlants = true;
    bool detailRocks = true;
    bool detailDebris = true;

    // Force every NON-ORIGINAL feature off (see BaselineOnly()).
    void applyBaselineOnly() {
        atmosphericParticles = false;
        ambientCreatures = false;
        reactiveFoliage = false;
        rollingObjects = false;
        animatedTrees = false;
        detailObjectsEnabled = false;
        detailGrass = detailPlants = detailRocks = detailDebris = false;
        detailDensity = 0.0f;
        environmentDensity = 0.0f;
    }
};

} // namespace ui
} // namespace eqt
