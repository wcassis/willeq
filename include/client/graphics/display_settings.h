/*
 * DisplaySettings — environment/rendering quality settings.
 *
 * U07c2: Extracted from deleted options_window.h.
 * Pure data struct, no UI dependency.
 */

#pragma once

#include <cstdint>

namespace eqt {
namespace ui {

enum class EffectQuality {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

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
    float detailViewDistance = 150.0f;
    bool detailGrass = true;
    bool detailPlants = true;
    bool detailRocks = true;
    bool detailDebris = true;
};

} // namespace ui
} // namespace eqt
