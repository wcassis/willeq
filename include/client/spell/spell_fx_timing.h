/*
 * WillEQ spell FX timing from spellsnew.edd (B1145, #465)
 *
 * When one spellsnew.eff slot's emitter starts, how long it emits, how long
 * its particles live and when it ends -- the official client's rule, read
 * from its code by B1141 (#461; docs/spellsnew_format.md,
 * review/B1141/edd_fields_from_code.tsv, eff_fields_from_code.tsv):
 *
 *   run T     +0x78 < 0: T = |+0x78| (a FIXED run, whatever the caller says)
 *             +0x78 >= 0: T = the caller's duration when it gives one
 *             (> 0 ms, x 0.001), else +0x78   (CreateEmitter 0x1005a4f9..)
 *   start     +0x8C: no emission tick before the emitter is that old;
 *             the emission window is T - start (< 0: nothing is emitted)
 *   lifetime  +0x7C: each particle lives L s (< 0: it lives T)
 *   end       the last particle dies at T + L (CreateEmitter 0x1005a619:
 *             (window + L) + delay), all times from the stage's start
 *   envelope  +0x80 particles on the first frame (the burst), then +0x84
 *             per tick at +0x88 ticks/s over the window (0x10060ea4..)
 *
 * The duration eqgame passes per stage (eff_fields_from_code.tsv "call
 * args"): Source = the cast time + the cast animation's length (0x49b977:
 * cast_ms + 0x47ddd4()), Middle = spell field 18 (0x476b12), Target = 0.
 *
 * MEASURED, not in the file:
 *   - kSpellFxCastAnimationSeconds: 0x47ddd4 returns the length of the
 *     caster model's cast animation track (model data, not traced). B1145
 *     measured the term on the official client: the caster box of the 20
 *     unsaturated B1130 other-target captures stops being fed a median 4.35 s
 *     after OP_Action (IQR 4.12..4.81, CastingAnim 42/43/44 alike;
 *     review/B1145/cast_anim/, scripts/cast_anim_fit.py).
 *   - the stage's start itself (the client's delay from the packet to the
 *     first emission) is the measured stage latency the caller passes; no
 *     slot of the 40 measured records has a +0x8C on its first emitter, yet
 *     every Source shows BC+0.37..0.50 (SpellEffectsConfig keeps it).
 */

#ifndef EQT_SPELL_FX_TIMING_H
#define EQT_SPELL_FX_TIMING_H

#include <algorithm>
#include <cstdint>

#include "client/spell/spells_new_edd_reader.h"

namespace EQ {

// B1145: the cast-animation term eqgame adds to the Source duration, seconds
// (measured; see the header comment).
constexpr float kSpellFxCastAnimationSeconds = 4.35f;

enum class SpellFxStage : int { Source = 0, Middle = 1, Target = 2 };

// The caller duration eqgame passes to every slot of a stage, in ms (the
// unit CreateEmitter takes: 0 = none given).
inline int32_t SpellFxCallerDurationMs(SpellFxStage stage, int32_t castMs, int32_t field18Ms) {
    switch (stage) {
        case SpellFxStage::Source:
            return std::max<int32_t>(castMs, 0) +
                   static_cast<int32_t>(kSpellFxCastAnimationSeconds * 1000.0f + 0.5f);
        case SpellFxStage::Middle: return std::max<int32_t>(field18Ms, 0);
        case SpellFxStage::Target: return 0;
    }
    return 0;
}

// One slot's timing, every time in seconds from its stage's start.
struct SpellFxSlotTiming {
    float start = 0.0f;     // first emission (+0x8C)
    float run = 0.0f;       // T: the emitter stops emitting at this age
    float lifetime = 0.0f;  // L: each particle's life (>= 0 here)
    bool fixed = false;     // +0x78 < 0
    int32_t burst = 0;      // +0x80
    int32_t perTick = 0;    // +0x84
    float ticksPerSecond = 0.0f;  // +0x88

    // The emission window T - start; < 0 = the emitter emits nothing.
    float window() const { return run - start; }
    bool emits() const { return window() >= 0.0f && (burst > 0 || streamRate() > 0.0f); }
    // Particles per second over the window after the burst.
    float streamRate() const { return static_cast<float>(perTick) * ticksPerSecond; }
    // When the last emission happens and when its particle dies.
    float lastEmission() const { return std::max(run, start); }
    float end() const { return emits() ? lastEmission() + lifetime : start; }
    // The most particles alive at once (the burst plus one lifetime of
    // stream, or the whole window when it is shorter than a life).
    float peakLive() const {
        if (!emits()) return 0.0f;
        return static_cast<float>(std::max(burst, 0)) +
               streamRate() * std::min(window(), lifetime);
    }
};

inline SpellFxSlotTiming SpellFxSlotTimingFor(const SpellsNewEddTiming& w, int32_t callerMs) {
    SpellFxSlotTiming t;
    t.fixed = w.fixedDuration();
    if (t.fixed)
        t.run = -w.duration;
    else
        t.run = callerMs > 0 ? static_cast<float>(callerMs) * 0.001f : w.duration;
    t.start = std::max(w.delay, 0.0f);
    t.lifetime = w.lifetime >= 0.0f ? w.lifetime : t.run;
    t.burst = w.burst;
    t.perTick = w.perTick;
    t.ticksPerSecond = w.ticksPerSecond;
    return t;
}

}  // namespace EQ

#endif  // EQT_SPELL_FX_TIMING_H
