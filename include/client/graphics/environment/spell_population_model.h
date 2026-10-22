// B1123 (#443): the expected LIVE population of one spell landing -- the one
// model behind the pool rule (SpellEffectsConfig::applyPool) and the pool tests
// (tests/spell_landing_population.h is a thin adapter over this).
//
// B1121 (#441) fed the landing to the official client's measured end: a PEAK
// phase (spawnRate over spawnWindow) and a SUSTAIN phase (sustainRate from
// sustainFrom to emitterLifetime), with lifetimes uniform in [lifetimeMin,
// lifetimeMax].  The live count at time t is the integral of rate(b) x
// P(life > t - b) over births b <= t; the halves of one landing (impact +
// spellComplete) start together, so their live counts add at each t.
// Expected values (no sampling noise).
#pragma once

#include <algorithm>
#include <vector>

namespace EQT {
namespace Graphics {
namespace Environment {
namespace spell_population {

// The emission schedule of one emitter (the keys the population depends on).
struct Schedule {
    float spawnRate = 0.0f;
    int burstCount = 0;
    float spawnWindow = 0.0f;      // 0 = spawnRate for the whole emitterLifetime
    float sustainRate = 0.0f;
    float sustainFrom = 0.0f;
    float emitterLifetime = 0.0f;  // 0 = unbounded
    float lifetimeMin = 0.0f;
    float lifetimeMax = 0.0f;
};

inline float RateAt(const Schedule& c, float age) {
    if (c.emitterLifetime > 0.0f && age >= c.emitterLifetime) return 0.0f;
    float r = 0.0f;
    if (c.spawnWindow <= 0.0f || age < c.spawnWindow) r += c.spawnRate;
    if (c.sustainRate > 0.0f && age >= c.sustainFrom) r += c.sustainRate;
    return r;
}

inline float Survives(const Schedule& c, float age) {
    if (age < c.lifetimeMin) return 1.0f;
    if (age >= c.lifetimeMax || c.lifetimeMax <= c.lifetimeMin) return 0.0f;
    return (c.lifetimeMax - age) / (c.lifetimeMax - c.lifetimeMin);
}

inline float LiveAt(const Schedule& c, float t) {
    constexpr int kSteps = 2000;  // midpoint rule over the births in [0, t]
    const float dt = t / static_cast<float>(kSteps);
    float n = static_cast<float>(c.burstCount) * Survives(c, t);
    for (int i = 0; i < kSteps; ++i) {
        const float b = (static_cast<float>(i) + 0.5f) * dt;
        n += RateAt(c, b) * Survives(c, t - b) * dt;
    }
    return n;
}

// The largest expected live count of one landing's emitters (its halves).
inline float PeakLive(const std::vector<Schedule>& halves) {
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

// B1122 held the Source past OP_Action (releaseDelay) and lets its sprites
// finish their life, so a cast's Source overlaps its OWN landing.  The Source
// emits steadily (rate x mean life live) until OP_Action + releaseDelay; its
// live count `tau` seconds after OP_Action:
inline float HeldSourceLiveAt(const Schedule& src, float releaseDelay, float tau) {
    const float hi = std::min(tau, releaseDelay);
    const float lo = tau - src.lifetimeMax;
    if (hi <= lo || src.spawnRate <= 0.0f) return 0.0f;
    constexpr int kSteps = 400;
    const float dt = (hi - lo) / static_cast<float>(kSteps);
    float n = 0.0f;
    for (int i = 0; i < kSteps; ++i) {
        const float b = lo + (static_cast<float>(i) + 0.5f) * dt;
        n += src.spawnRate * Survives(src, tau - b) * dt;
    }
    return n;
}

// A landing's live curve and its held Source's, on one 0.02 s grid of time
// after OP_Action (the landing starts `landingDelay` after it).
struct Overlap {
    std::vector<float> landing, source;
    float PeakAt(float k) const {
        float peak = 0.0f;
        for (size_t i = 0; i < landing.size(); ++i) peak = std::max(peak, k * landing[i] + source[i]);
        return peak;
    }
};

inline Overlap OverlapOf(const std::vector<Schedule>& halves, float landingDelay,
                         const Schedule& src, float releaseDelay) {
    float horizon = 0.0f;
    for (const auto& c : halves) horizon = std::max(horizon, c.emitterLifetime + c.lifetimeMax);
    Overlap o;
    for (float tau = 0.0f; tau <= landingDelay + horizon; tau += 0.02f) {
        float l = 0.0f;
        if (tau >= landingDelay)
            for (const auto& c : halves) l += LiveAt(c, tau - landingDelay);
        o.landing.push_back(l);
        o.source.push_back(HeldSourceLiveAt(src, releaseDelay, tau));
    }
    return o;
}

// The largest k for which the landing x k plus its held Source peaks at
// `target` (bisection; the peak grows with k).
inline float ScaleToFit(const Overlap& o, float target) {
    float lo = 0.0f, hi = 1.0f;
    while (o.PeakAt(hi) < target && hi < 1e6f) hi *= 2.0f;
    for (int i = 0; i < 50; ++i) {
        const float mid = 0.5f * (lo + hi);
        (o.PeakAt(mid) <= target ? lo : hi) = mid;
    }
    return lo;
}

}  // namespace spell_population
}  // namespace Environment
}  // namespace Graphics
}  // namespace EQT
