/*
 * B1165 (#484): the Options > Particles > Spell page's rules, device-free
 * (include/client/spell/spell_fx_options.h, spell_fx_timing.h).
 *
 *   - "On For:" (SpellParticleCastFilter): every value of eqgame's table
 *     DAT_00793b4c = {1, 2, 4, 8, 17, 18, 20, 24} against a self / group /
 *     raid / NPC / other-PC / corpse caster, and the landing cases, through
 *     the decode of FUN_00491cff (eqgame.dis 0x491cff..0x491e2f). The
 *     expectations are the combobox's own labels (EQUI_OptionsWindow.xml):
 *     My spells, Group spells, Raid spells, All PCs, Me + NPC, Group + NPC,
 *     Raid + NPC, All spells.
 *   - The stage gate (FUN_0049b260, 0x49b2f7..0x49b363).
 *   - The density CreateEmitter applies (0x1005a463..0x1005a4f5).
 */

#include <gtest/gtest.h>

#include <string>

#include "client/spell/spell_fx_options.h"
#include "client/spell/spell_fx_timing.h"

using namespace EQ;

namespace {

SpellFxActor Pc(bool self, bool group, bool raid) {
    SpellFxActor a;
    a.present = true;
    a.spawnType = 0;
    a.isSelf = self;
    a.inGroup = group;
    a.inRaid = raid;
    return a;
}
SpellFxActor Npc() {
    SpellFxActor a;
    a.present = true;
    a.spawnType = 1;
    return a;
}
SpellFxActor Corpse(uint8_t type) {
    SpellFxActor a;
    a.present = true;
    a.spawnType = type;
    return a;
}

const char* const kLabel[8] = {"My spells",   "Group spells", "Raid spells", "All PCs",
                               "Me + NPC",    "Group + NPC",  "Raid + NPC",  "All spells"};

}  // namespace

TEST(SpellParticleCastFilter, TheTableIsTheBinarysAndTheDefaultIs24) {
    // DAT_00793b4c read from eqgame.exe (review/B1165/worker/scripts/peread.py).
    const int32_t expect[8] = {1, 2, 4, 8, 17, 18, 20, 24};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(kSpellParticleCastFilterValues[i], expect[i]) << kLabel[i];
    EXPECT_EQ(kSpellParticleCastFilterDefault, 24);
    EXPECT_TRUE(IsOracleCaptureSpellFxOptions(SpellFxOptions{}));
}

// The Source stage: eqgame's BeginCast passes the caster as both the caster
// and the target (eqgame.exe.c:122270), so each row is one caster kind.
TEST(SpellParticleCastFilter, EveryTableEntryOnEveryCasterKind) {
    struct Row {
        const char* who;
        SpellFxActor a;
        bool show[8];  // My, Group, Raid, AllPCs, Me+NPC, Group+NPC, Raid+NPC, All
    };
    const Row rows[] = {
        {"self", Pc(true, false, false), {true, true, true, true, true, true, true, true}},
        {"group member", Pc(false, true, false), {false, true, true, true, false, true, true, true}},
        {"raid member", Pc(false, false, true), {false, false, true, true, false, false, true, true}},
        {"group + raid member", Pc(false, true, true), {false, true, true, true, false, true, true, true}},
        {"other PC", Pc(false, false, false), {false, false, false, true, false, false, false, true}},
        {"NPC", Npc(), {false, false, false, false, true, true, true, true}},
        {"PC corpse", Corpse(2), {true, true, true, true, true, true, true, true}},
        {"NPC corpse", Corpse(3), {true, true, true, true, true, true, true, true}},
    };
    for (const auto& r : rows)
        for (int i = 0; i < 8; ++i)
            EXPECT_EQ(SpellParticleCastFilterShows(kSpellParticleCastFilterValues[i], r.a, r.a), r.show[i])
                << r.who << " under '" << kLabel[i] << "'";
}

TEST(SpellParticleCastFilter, NoSpawnAtAllIsAlwaysShown) {
    // 0x491d43..0x491d4f: caster == 0 && target == 0 -> shown.
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(SpellParticleCastFilterShows(kSpellParticleCastFilterValues[i], SpellFxActor{}, SpellFxActor{}))
            << kLabel[i];
}

// The Target stage (FUN_0047f4af -> FUN_0049b260(2, spell, caster, target),
// eqgame.exe.c:101960): a hidden caster falls through to the target.
TEST(SpellParticleCastFilter, TheLandingFallsThroughToTheTarget) {
    const SpellFxActor me = Pc(true, false, false);
    const SpellFxActor other = Pc(false, false, false);
    const SpellFxActor groupie = Pc(false, true, false);
    const SpellFxActor raider = Pc(false, false, true);
    for (int i = 0; i < 8; ++i) {
        const int32_t f = kSpellParticleCastFilterValues[i];
        EXPECT_TRUE(SpellParticleCastFilterShows(f, other, me)) << "another PC's spell on me, " << kLabel[i];
        EXPECT_TRUE(SpellParticleCastFilterShows(f, Npc(), me)) << "an NPC's spell on me, " << kLabel[i];
        EXPECT_EQ(SpellParticleCastFilterShows(f, other, groupie), (f & 0x0E) != 0)
            << "another PC's spell on my group member, " << kLabel[i];
        EXPECT_EQ(SpellParticleCastFilterShows(f, other, raider), (f & 0x0C) != 0)
            << "another PC's spell on my raid member, " << kLabel[i];
        EXPECT_EQ(SpellParticleCastFilterShows(f, Npc(), Npc()), (f & 0x10) != 0)
            << "an NPC's spell on an NPC, " << kLabel[i];
        EXPECT_EQ(SpellParticleCastFilterShows(f, other, Npc()), (f & 0x08) != 0)
            << "another PC's spell on an NPC, " << kLabel[i];
        // The Middle (missile) stage: the caster and no target (eqgame.exe.c:95153).
        EXPECT_EQ(SpellParticleCastFilterShows(f, other, SpellFxActor{}), (f & 0x08) != 0)
            << "another PC's missile, " << kLabel[i];
        EXPECT_TRUE(SpellParticleCastFilterShows(f, me, SpellFxActor{})) << "my missile, " << kLabel[i];
    }
    // An off-table value decodes by its bits all the same (the global keeps
    // the raw ini int, eqgame.exe.c:132638; fable keeps it too since the B1165
    // fix: OptionsParticles.AnOffTableCastFilterIntIsKeptRawAndDecodedByItsBits).
    EXPECT_FALSE(SpellParticleCastFilterShows(0, other, other));
    EXPECT_TRUE(SpellParticleCastFilterShows(0, me, me));
}

TEST(SpellFxStageGate, TheRecordPathNeedsDensityOpacityAndShowSpellEffects) {
    const SpellFxActor me = Pc(true, false, false);
    SpellFxOptions o;
    for (int stage = 0; stage < 3; ++stage) {
        EXPECT_TRUE(SpellFxStageDraws(o, true, stage, me, me)) << stage;
        EXPECT_TRUE(SpellFxStageDraws(o, false, stage, me, me)) << stage;
    }
    o.density = 0.0f;
    EXPECT_FALSE(SpellFxStageDraws(o, true, 0, me, me)) << "0x49b30b: density <= 0";
    EXPECT_TRUE(SpellFxStageDraws(o, false, 0, me, me)) << "the legacy path does not read density";
    o = SpellFxOptions{};
    o.opacity = 0.0f;
    EXPECT_FALSE(SpellFxStageDraws(o, true, 2, me, me)) << "0x49b31e: opacity <= 0";
    o = SpellFxOptions{};
    o.showSpellEffects = false;
    for (int stage = 0; stage < 3; ++stage)
        EXPECT_FALSE(SpellFxStageDraws(o, true, stage, me, me)) << "0x49b331: ShowSpellEffects 0, stage " << stage;
    EXPECT_FALSE(SpellFxStageDraws(o, false, 0, me, me)) << "0x49b358: legacy, ShowSpellEffects 0";
    EXPECT_TRUE(SpellFxStageDraws(o, false, 1, me, me)) << "0x49b360: legacy stage 1 draws anyway";
    EXPECT_FALSE(SpellFxStageDraws(o, false, 2, me, me));
    o = SpellFxOptions{};
    o.castFilter = 1;
    const SpellFxActor other = Pc(false, false, false);
    EXPECT_FALSE(SpellFxStageDraws(o, true, 0, other, other)) << "0x49b2f9: the filter first";
    EXPECT_FALSE(SpellFxStageDraws(o, false, 1, other, SpellFxActor{}));
}

TEST(SpellFxDensity, CreateEmitterScalesTheRecordsCounts) {
    SpellFxSlotTiming t;
    t.start = 0.25f;
    t.run = 5.0f;
    t.lifetime = 1.5f;
    t.burst = 50;
    t.perTick = 50;
    t.ticksPerSecond = 30.0f;
    // density 1: the identity.
    const SpellFxSlotTiming one = SpellFxSlotTimingAtDensity(t, 1.0f);
    EXPECT_EQ(one.burst, 50);
    EXPECT_EQ(one.perTick, 50);
    EXPECT_FLOAT_EQ(one.ticksPerSecond, 30.0f);
    EXPECT_FLOAT_EQ(one.start, 0.25f);
    EXPECT_FLOAT_EQ(one.run, 5.0f);
    EXPECT_FLOAT_EQ(one.lifetime, 1.5f);
    // 0.5: 50 -> ftol(25) = 25 (>= 16); per tick 50 >= 16 -> 25, the rate kept.
    const SpellFxSlotTiming half = SpellFxSlotTimingAtDensity(t, 0.5f);
    EXPECT_EQ(half.burst, 25);
    EXPECT_EQ(half.perTick, 25);
    EXPECT_FLOAT_EQ(half.ticksPerSecond, 30.0f);
    // 0.25: ftol(12.5) = 12 < 16 -> min(50, 16) = 16; per tick 50 -> 12.
    const SpellFxSlotTiming low = SpellFxSlotTimingAtDensity(t, 0.25f);
    EXPECT_EQ(low.burst, 16);
    EXPECT_EQ(low.perTick, 12);
    // Below 16: the burst is kept; the per-tick count is kept and the RATE scales.
    t.burst = 5;
    t.perTick = 5;
    t.ticksPerSecond = 10.0f;
    const SpellFxSlotTiming small = SpellFxSlotTimingAtDensity(t, 0.5f);
    EXPECT_EQ(small.burst, 5) << "min(5, 16)";
    EXPECT_EQ(small.perTick, 5);
    EXPECT_FLOAT_EQ(small.ticksPerSecond, 5.0f);
    // 33 at 0.5: ftol(16.5) = 16 -- truncation, not rounding (0x101a9b60).
    t.burst = 33;
    t.perTick = 33;
    t.ticksPerSecond = 18.0f;
    const SpellFxSlotTiming trunc = SpellFxSlotTimingAtDensity(t, 0.5f);
    EXPECT_EQ(trunc.burst, 16);
    EXPECT_EQ(trunc.perTick, 16);
    EXPECT_FLOAT_EQ(trunc.ticksPerSecond, 18.0f);
    // 0: no emitter (0x1005a463..0x1005a48e) -- nothing emits.
    const SpellFxSlotTiming off = SpellFxSlotTimingAtDensity(t, 0.0f);
    EXPECT_FALSE(off.emits());
    EXPECT_EQ(off.burst, 0);
    EXPECT_EQ(off.perTick, 0);
}
