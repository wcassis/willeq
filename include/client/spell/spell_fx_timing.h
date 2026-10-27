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
 * The cast-animation term (B1148, #468): 0x47ddd4(spell+0x186) indexes the
 * caster actor's animation table by the spell's CastingAnim (field 120) and
 * returns that track's length (ftol of the track object's first virtual).
 * The id names the track through eqgame's code table (0x8ffbc8 + id * 20,
 * filled by 0x44e7ec..: 1 = C01 .. 42 = T04, 43 = T05, 44 = T06;
 * SpellCastAnimationCode); fable reads the length from the caster's own
 * skeleton (frames x frame ms, s3d_loader_skinning.cpp), the model data the
 * client reads. A caster whose model fable cannot resolve (no entity, no
 * track) takes kSpellFxCastAnimationSeconds, the term B1145 MEASURED on the
 * official client: the caster box of the 20 unsaturated B1130 other-target
 * captures stops being fed a median 4.35 s after OP_Action (IQR 4.12..4.81;
 * review/B1145/cast_anim/, scripts/cast_anim_fit.py) -- that caster was
 * char 67, a Dark Elf female, whose model's T04/T05/T06 (the ELF donor's,
 * animation_mapping.cpp) are 4100 ms each.
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
// (measured; see the header comment). B1148: only the fallback for a caster
// whose model track fable cannot read.
constexpr float kSpellFxCastAnimationSeconds = 4.35f;

// B1148 (#468): eqgame's animation id -> track code table (0x8ffbc8 + id *
// 20, filled by 0x44e7ec..0x44ed..; read from the binary by
// review/B1148/scripts/anim_code_table.py). Lower case, the codes fable's
// skeletons key their animations by. nullptr for an id outside 1..78.
inline const char* SpellCastAnimationCode(int id) {
    static const char* const kCodes[] = {
        nullptr,
        "c01", "c02", "c03", "c04", "c05", "c06", "c07", "c08", "c09", "c10", "c11",  //  1..11
        "d01", "d02", "d03", "d04", "d05",                                             // 12..16
        "l01", "l02", "l03", "l04", "l05", "l06", "l07", "l08", "l09",                 // 17..25
        "o01",                                                                          // 26
        "s01", "s02", "s03", "s04", "s05",                                             // 27..31
        "p01", "p02", "p03", "p04", "p05", "p06", "p07",                               // 32..38
        "t01", "t02", "t03", "t04", "t05", "t06", "t07", "t08", "t09",                 // 39..47
        "s06", "s07", "s08", "s09", "s10", "s11", "s12", "s13", "s14", "s15", "s16",   // 48..58
        "s17", "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "s27",   // 59..69
        "s28", "p08", "o02", "o03", "p09", "l11", "l12", "s29", "l10",                 // 70..78
    };
    constexpr int kCount = static_cast<int>(sizeof(kCodes) / sizeof(kCodes[0]));
    return (id > 0 && id < kCount) ? kCodes[id] : nullptr;
}

enum class SpellFxStage : int { Source = 0, Middle = 1, Target = 2 };

// The caller duration eqgame passes to every slot of a stage, in ms (the
// unit CreateEmitter takes: 0 = none given).
// castAnimMs: the caster model's CastingAnim track length (0x47ddd4), < 0 =
// unknown -> kSpellFxCastAnimationSeconds.
inline int32_t SpellFxCallerDurationMs(SpellFxStage stage, int32_t castMs, int32_t field18Ms,
                                       int32_t castAnimMs = -1) {
    switch (stage) {
        case SpellFxStage::Source:
            return std::max<int32_t>(castMs, 0) +
                   (castAnimMs >= 0 ? castAnimMs
                                    : static_cast<int32_t>(kSpellFxCastAnimationSeconds * 1000.0f + 0.5f));
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

// B1165 (#484): the Spell page's density as CreateEmitter (EQGraphicsDX9
// 0x1005a430) applies it to the record's counts -- density is that function's
// third argument (eqgame 0x495f29 pushes SpellParticleDensity; engine vtable
// +0x14 = 0x1005e1d0 -> 0x1005e0a0 -> 0x1005a430), read ONCE, at creation:
//   density <= 0: no emitter at all (0x1005a463..0x1005a48e);
//   burst (+0x80) b -> ftol(b x density), raised to min(b, 16) when that is
//     below 16 (0x1005a495..0x1005a4bf);
//   per tick (+0x84) t >= 16: t -> ftol(t x density), the tick rate kept
//     (0x1005a4c1..0x1005a4e9); t < 16: t kept, the tick rate (+0x88) x
//     density instead (0x1005a4eb..0x1005a4f5).
// ftol (0x101a9b60) truncates toward zero. density 1 is the identity.
inline SpellFxSlotTiming SpellFxSlotTimingAtDensity(SpellFxSlotTiming t, float density) {
    if (!(density > 0.0f)) {
        t.burst = 0;
        t.perTick = 0;
        t.ticksPerSecond = 0.0f;
        return t;
    }
    const int32_t b = static_cast<int32_t>(static_cast<double>(t.burst) * static_cast<double>(density));
    t.burst = b < 16 ? std::min<int32_t>(t.burst, 16) : b;
    if (t.perTick >= 16)
        t.perTick = static_cast<int32_t>(static_cast<double>(t.perTick) * static_cast<double>(density));
    else
        t.ticksPerSecond = t.ticksPerSecond * density;
    return t;
}

}  // namespace EQ

#endif  // EQT_SPELL_FX_TIMING_H
