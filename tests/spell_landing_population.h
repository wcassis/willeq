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

#include "client/graphics/environment/spell_population_model.h"
#include "client/graphics/environment/unified_particle.h"
#include <vector>

namespace spell_landing_population {

using EQT::Graphics::Environment::EmitterConfig;

// B1123: the model itself is production code now (the pool rule uses it):
// include/client/graphics/environment/spell_population_model.h.
inline EQT::Graphics::Environment::spell_population::Schedule ScheduleOf(const EmitterConfig& c) {
    EQT::Graphics::Environment::spell_population::Schedule s;
    s.spawnRate = c.spawnRate;
    s.burstCount = c.burstCount;
    s.spawnWindow = c.spawnWindow;
    s.sustainRate = c.sustainRate;
    s.sustainFrom = c.sustainFrom;
    s.emitterLifetime = c.emitterLifetime;
    s.lifetimeMin = c.lifetimeMin;
    s.lifetimeMax = c.lifetimeMax;
    return s;
}

// The largest expected live count of the given emitters (one landing's halves).
inline float PeakLive(std::initializer_list<EmitterConfig> halves) {
    std::vector<EQT::Graphics::Environment::spell_population::Schedule> s;
    for (const auto& c : halves) s.push_back(ScheduleOf(c));
    return EQT::Graphics::Environment::spell_population::PeakLive(s);
}

// The PEAK phase's cohort: what B1120 / T422 called a landing's population.
inline float PeakCohort(const EmitterConfig& c) {
    const float w = c.spawnWindow > 0.0f ? std::min(c.spawnWindow, c.emitterLifetime) : c.emitterLifetime;
    return c.spawnRate * w + static_cast<float>(c.burstCount);
}

}  // namespace spell_landing_population
