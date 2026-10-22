// B1121 (#441): the expected LIVE population of a spell landing emitter, for the
// pool tests.  Before B1121 every landing emitted once over a 0.6 s window whose
// particles all outlived it, so spawnRate x emitterLifetime was the peak.  B1121
// feeds the landing to the official client's measured end (B1118 M3): a PEAK
// phase (spawnRate over spawnWindow) and a SUSTAIN phase (sustainRate from
// sustainFrom to emitterLifetime), with lifetimes uniform in [lifetimeMin,
// lifetimeMax].  The live count at time t is the integral of rate(b) x
// P(life > t - b) over births b <= t; the halves of one landing start together,
// so their live counts add at each t.  Expected values (no sampling noise).
#pragma once

#include <algorithm>
#include <initializer_list>

#include "client/graphics/environment/unified_particle.h"

namespace spell_landing_population {

using EQT::Graphics::Environment::EmitterConfig;

inline float RateAt(const EmitterConfig& c, float age) {
    if (c.emitterLifetime > 0.0f && age >= c.emitterLifetime) return 0.0f;
    float r = 0.0f;
    if (c.spawnWindow <= 0.0f || age < c.spawnWindow) r += c.spawnRate;
    if (c.sustainRate > 0.0f && age >= c.sustainFrom) r += c.sustainRate;
    return r;
}

inline float Survives(const EmitterConfig& c, float age) {
    if (age < c.lifetimeMin) return 1.0f;
    if (age >= c.lifetimeMax || c.lifetimeMax <= c.lifetimeMin) return 0.0f;
    return (c.lifetimeMax - age) / (c.lifetimeMax - c.lifetimeMin);
}

inline float LiveAt(const EmitterConfig& c, float t) {
    constexpr int kSteps = 2000;  // midpoint rule over the births in [0, t]
    const float dt = t / static_cast<float>(kSteps);
    float n = static_cast<float>(c.burstCount) * Survives(c, t);
    for (int i = 0; i < kSteps; ++i) {
        const float b = (static_cast<float>(i) + 0.5f) * dt;
        n += RateAt(c, b) * Survives(c, t - b) * dt;
    }
    return n;
}

// The largest expected live count of the given emitters (one landing's halves).
inline float PeakLive(std::initializer_list<EmitterConfig> halves) {
    float horizon = 0.0f;
    for (const auto& c : halves) horizon = std::max(horizon, c.emitterLifetime + c.lifetimeMax);
    float peak = 0.0f;
    for (float t = 0.0f; t <= horizon; t += 0.02f) {
        float n = 0.0f;
        for (const auto& c : halves) n += LiveAt(c, t);
        peak = std::max(peak, n);
    }
    return peak;
}

// The PEAK phase's cohort: what B1120 / T422 called a landing's population.
inline float PeakCohort(const EmitterConfig& c) {
    const float w = c.spawnWindow > 0.0f ? std::min(c.spawnWindow, c.emitterLifetime) : c.emitterLifetime;
    return c.spawnRate * w + static_cast<float>(c.burstCount);
}

}  // namespace spell_landing_population
