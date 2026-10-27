// B1165 (#484): the Options > Display > Particles > Spell page's rules the
// official client applies BEFORE a spell's stage draws, and the density it
// creates the stage's emitters with. Device-free (tests/test_spell_fx_options.cpp).
// Every rule is read from review/B1141/ghidra/out/eqgame.exe.c / eqgame.dis /
// EQGraphicsDX9.dis; refactor/B1165/README.md carries the line-by-line proof.
//
//   SpellParticleCastFilter ("On For:", [Defaults], default 0x18 = 24,
//   eqgame.exe.c:132638): the combobox stores the value table DAT_00793b4c =
//   {1, 2, 4, 8, 17, 18, 20, 24} (My spells .. All spells, eqgame.exe.c:
//   245181-245184) and FUN_00491cff (eqgame.exe.c:115163-115221, 0x491cff..
//   0x491e2f) decodes it: bit 0x10 = NPC casters, 0x08 = every PC caster,
//   0x04 = raid members (and group), 0x02 = group members; bit 0x01 is never
//   tested -- self is always shown.
//
//   The stage gate (FUN_0049b260, eqgame.exe.c:121989-122034): a stage the
//   filter hides draws nothing; a spell with a spellsnew.eff record (the EDD
//   path) draws only when SpellParticleDensity > 0, SpellParticleOpacity > 0
//   and ShowSpellEffects != 0 (0x49b30b..0x49b337); a spell without one (the
//   legacy path) draws when ShowSpellEffects != 0 or the stage is 1
//   (0x49b358..0x49b363; the third arm, spell +0x230 in 61..64 via
//   FUN_0044ced9, is not decoded -- README sec. 4).
//
//   Density at creation (CreateEmitter 0x1005a430, EQGraphicsDX9): the spell
//   family's emitters are created with SpellParticleDensity as the third
//   argument (eqgame 0x495f29 -> engine vtable +0x14 = 0x1005e1d0 ->
//   0x1005e0a0 -> 0x1005a430); see EQ::SpellFxSlotTimingAtDensity
//   (spell_fx_timing.h).

#ifndef EQT_SPELL_FX_OPTIONS_H
#define EQT_SPELL_FX_OPTIONS_H

#include <cstdint>

namespace EQ {

// What FUN_00491cff reads of a caster or target spawn. `present == false` is
// the client's null spawn pointer (no such spawn / no target).
struct SpellFxActor {
    bool present = false;
    uint8_t spawnType = 0;  // spawn +0x244: 0 PC, 1 NPC, 2/3 corpses (fable Entity::npc_type)
    bool inGroup = false;   // FUN_00485df1 on the group object 0x904248 (5 names)
    bool inRaid = false;    // FUN_004303a6 -> FUN_00430366 on the raid object 0x91d708 (72 names)
    bool isSelf = false;    // == DAT_00905ce4, the player's own spawn
};

// eqgame DAT_00793b4c, in the combobox's list order (EQUI_OptionsWindow.xml
// ODP_SpellParticlesFilterCombobox <Choices>).
inline constexpr int32_t kSpellParticleCastFilterValues[8] = {1, 2, 4, 8, 17, 18, 20, 24};
inline constexpr int32_t kSpellParticleCastFilterDefault = 0x18;

// FUN_00491cff, branch for branch (eqgame.dis 0x491cff..0x491e2f). The
// function returns TRUE to HIDE; this returns true to SHOW (its caller draws
// on a zero return, eqgame.exe.c:122018-122019).
inline bool SpellParticleCastFilterShows(int32_t filter, const SpellFxActor& caster,
                                         const SpellFxActor& target) {
    const bool npcCasters = (filter & 0x10) != 0;  // dl, 0x491d0f..0x491d21
    const bool allPcs = (filter & 0x08) != 0;      // [ebp-3], 0x491d23..0x491d27
    const bool raid = (filter & 0x0C) != 0;        // [ebp-4] / cl, 0x491d31..0x491d35
    const bool group = (filter & 0x0E) != 0;       // [ebp-2] / bl, 0x491d3d..0x491d41
    if (!caster.present && !target.present) return true;  // 0x491d43..0x491d4f
    if (allPcs && npcCasters) return true;                // 0x491d53..0x491d5d
    if (caster.present) {
        if (caster.spawnType == 1) {                      // 0x491d7b..0x491d83
            if (npcCasters) return true;
        } else if (caster.spawnType != 0) {               // 0x491d88..0x491d8a: a corpse
            return true;
        } else {                                          // a PC, 0x491d90..0x491dce
            if (allPcs) return true;
            if (raid && caster.inRaid) return true;
            if (group && caster.inGroup) return true;
            if (caster.isSelf) return true;
        }
    }
    // 0x491dd0..0x491e1d: the target decides.
    if (!target.present || target.spawnType != 0) return false;
    if (raid && target.inRaid) return true;
    if (group && target.inGroup) return true;
    return target.isSelf;
}

// The four Spell-page values plus the global switch, as the client holds them
// after its loader (eqgame.exe.c:132608-132638, 126007-126011).
struct SpellFxOptions {
    float density = 1.0f;    // SpellParticleDensity, [0, 1]
    float opacity = 1.0f;    // SpellParticleOpacity, [0, 1]
    float nearClip = 2.0f;   // SpellParticleNearClipPlane, [1, 8]
    int32_t castFilter = kSpellParticleCastFilterDefault;
    bool showSpellEffects = true;  // ShowSpellEffects (DAT_0092484c)
};

// The oracle capture's ini (review/B1159/worker/oracle/m_b1159_rig/
// eqclient-after.ini: SpellParticleDensity=1.000000, SpellParticleOpacity=
// 1.000000, SpellParticleNearClipPlane=2.000000, SpellParticleCastFilter=24,
// ShowSpellEffects=1) -- what every FX gate compares against.
inline bool IsOracleCaptureSpellFxOptions(const SpellFxOptions& o) {
    return o.density == 1.0f && o.opacity == 1.0f && o.nearClip == 2.0f && o.castFilter == 24 &&
           o.showSpellEffects;
}

// FUN_0049b260's gate for stage `stage` (0 Source, 1 Middle, 2 Target).
inline bool SpellFxStageDraws(const SpellFxOptions& o, bool hasRecord, int stage, const SpellFxActor& caster,
                              const SpellFxActor& target) {
    if (!SpellParticleCastFilterShows(o.castFilter, caster, target)) return false;  // 0x49b2f7..0x49b2f9
    if (hasRecord) return o.density > 0.0f && o.opacity > 0.0f && o.showSpellEffects;  // 0x49b30b..0x49b337
    return o.showSpellEffects || stage == 1;  // 0x49b358..0x49b363 (+0x230 arm: README sec. 4)
}

}  // namespace EQ

#endif  // EQT_SPELL_FX_OPTIONS_H
