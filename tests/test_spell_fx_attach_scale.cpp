/*
 * B1164 (#483): the attach scale s of a spellsnew.eff slot
 * (include/client/spell/spell_fx_attach_scale.h -> the one actor-scale rule,
 * include/client/graphics/actor_scale.h, shared with B1172 #491).
 *
 * Pinned on lane 5 by a read-only gdb probe of the official client
 * (refactor/B1164/README.md sec. 10, review/B1164/worker/dev5/gdb/): record 80
 * (spell 93) cast on a race-1 `#spawn` of server size 6 (OP_NewSpawn size=6.000)
 * draws its Target slots with s = 1.200000 (track row 0 = -0.459220, 1.108655,
 * 0; 0x1005c4f0 = 1.000000); the same cast on a race-6 `#spawn` of size 5
 * (DarkF) with s = 1.000000; and its Source slots on the race-6 caster whose
 * OP_ZoneEntry carries size 0 with s = 1.000000 (DEFAULT) / 0.999755 (a hand
 * bone). The client's rule behind them, every branch read from eqgame.exe
 * (README sec. 10.2): 0x47a147's default size when the spawn's is not > 0,
 * 0x47a0e9 (0.2 x size; size / 6 x 1.3 for races 0x193 and >= 0x195), then
 * 0x47a239's forced scales (1.0 / 2.0 / 0.05) for ten + two races.
 * RED before B1164: fable passed kSpellFxAttachScale = 1.0 for every actor;
 * RED before this fix leg: every race >= 0x1a read 1.0.
 */

#include <gtest/gtest.h>

#include "client/spell/spell_fx_attach_scale.h"

#include <cstdint>
#include <iterator>

using EQ::SpellFxAttachScale;

namespace {

struct Row {
    uint16_t race;
    uint8_t gender;
    float size;      // Spawn_Struct size; 0 = none
    float expected;  // the client's actor scale
    const char* why;
};

// The live points (lane 5, gdb run4/s_probe.log).
const Row kLive[] = {
    {1, 0, 6.0f, 1.2f, "93 target TrollF, s_probe.log:704/:708"},
    {6, 1, 5.0f, 1.0f, "93 target DarkF (#spawn size 5), s_probe.log t 92.8..99.2"},
    {6, 1, 0.0f, 1.0f, "caster Testcaster (OP_ZoneEntry size 0), s_probe.log:626"},
};

// 0x47a147 (jump table 0x47a209 for races 1..12, compares above 0x18): the
// size a spawn takes when its own is not > 0; s = 0.2 x that default.
const Row kDefaults[] = {
    {0, 0, 0.0f, 0.2f * 6.0f, "race 0: below the table, 0x64264c 6.0"},
    {1, 0, 0.0f, 0.2f * 6.0f, "human, table -> 0x47a1e1 6.0"},
    {2, 0, 0.0f, 0.2f * 7.0f, "barbarian 0x642658 7.0"},
    {3, 0, 0.0f, 0.2f * 6.0f, "erudite 6.0"},
    {4, 0, 0.0f, 0.2f * 5.0f, "wood elf 0x640e58 5.0"},
    {5, 0, 0.0f, 0.2f * 6.0f, "high elf 6.0"},
    {6, 0, 0.0f, 0.2f * 5.0f, "dark elf 5.0"},
    {7, 0, 0.0f, 0.2f * 5.5f, "half elf 0x64263c 5.5"},
    {8, 0, 0.0f, 0.2f * 4.0f, "dwarf 0x642650 4.0"},
    {9, 0, 0.0f, 0.2f * 8.0f, "troll 0x641fa0 8.0"},
    {10, 0, 0.0f, 0.2f * 9.0f, "ogre 0x6410cc 9.0"},
    {11, 0, 0.0f, 0.2f * 3.5f, "halfling 0x642d68 3.5"},
    {12, 0, 0.0f, 0.2f * 3.0f, "gnome 0x640e24 3.0"},
    {0x18, 0, 0.0f, 0.2f * 6.0f, "0x18: je 0x47a1e1, 6.0"},
    {0x2a, 0, 0.0f, 0.2f * 6.0f, "0x2a gender 0: 6.0"},
    {0x2a, 2, 0.0f, 0.2f * 3.0f, "0x2a gender 2: 0x47a1e9 -> 3.0"},
    {0x2b, 0, 0.0f, 0.2f * 4.7f, "0x2b 0x647cd8 4.7"},
    {0x4d, 0, 0.0f, 0.2f * 5.0f, "0x4d 5.0"},
    {0x4e, 0, 0.0f, 0.2f * 6.0f, "0x4e 6.0"},
    {0x51, 0, 0.0f, 0.2f * 3.5f, "0x51 3.5"},
    {0x58, 0, 0.0f, 0.2f * 3.0f, "0x58 3.0"},
    {0x5a, 0, 0.0f, 0.2f * 7.0f, "0x5a 7.0"},
    {0x5c, 0, 0.0f, 0.2f * 8.0f, "0x5c 8.0"},
    {0x5d, 0, 0.0f, 0.2f * 9.0f, "0x5d 9.0"},
    {0x5e, 0, 0.0f, 0.2f * 4.0f, "0x5e 4.0"},
    {0x70, 0, 0.0f, 0.2f * 5.0f, "0x70 5.0"},
    {0x78, 2, 0.0f, 0.2f * 3.0f, "0x78 gender 2: 3.0"},
    {0x78, 1, 0.0f, 0.2f * 6.0f, "0x78 gender 1: 6.0"},
    {0x80, 0, 0.0f, 0.2f * 6.0f, "0x80 6.0"},
    {0x82, 0, 0.0f, 0.2f * 7.0f, "0x82 7.0"},
    {0x14a, 0, 0.0f, 0.2f * 5.0f, "0x14a 5.0"},
    {0x7f, 0, 0.0f, 0.2f * 6.0f, "race 127 (not in 0x47a147): 6.0"},
};

// 0x47a0e9 with a size the spawn carries: 0.2 x size, whatever the race --
// including every race 0x47a239 reaches without touching the scale slot
// (0x1a/0x1b, 0x24, 0x32, 0x5b, 0x78, 0xab, 0x14a, 0x164 write only its
// other outputs; every other race returns at once).
const Row kFifth[] = {
    {1, 0, 1.0f, 0.2f, "size 1 human"},
    {6, 0, 1.0f, 0.2f, "size 1 dark elf"},
    {0x1a, 0, 6.0f, 1.2f, "0x1a: 0x47a37a writes [ebp+0x10] only"},
    {0x1b, 0, 6.0f, 1.2f, "0x1b: same"},
    {0x24, 0, 6.0f, 1.2f, "0x24: 0x47a2e2 writes [ebp+0x18], [ebp+0x10]"},
    {0x32, 0, 6.0f, 1.2f, "0x32: 0x47a27e -> [ebp+0x10]"},
    {0x5b, 0, 6.0f, 1.2f, "0x5b: 0x47a2e2"},
    {0x78, 0, 6.0f, 1.2f, "0x78: 0x47a323 -> [ebp+0x10]"},
    {0xab, 0, 6.0f, 1.2f, "0xab: 0x47a323"},
    {0x14a, 0, 6.0f, 1.2f, "0x14a: 0x47a37a"},
    {0x164, 0, 6.0f, 1.2f, "0x164: 0x47a323"},
    {0x7f, 0, 10.0f, 2.0f, "race 127: 0x47a239 returns"},
    {0x192, 0, 6.0f, 1.2f, "0x192: below 0x193"},
};

// 0x47a0e9's other branch (0x47937c true: race 0x193 or >= 0x195), both
// globals 0 (lane 5): size x 1/6 (0x647cd4) x 1.3 (0x642d50).
const Row kSixth[] = {
    {0x193, 0, 6.0f, 1.3f, "0x193 size 6"},
    {0x195, 0, 12.0f, 2.6f, "0x195 size 12"},
    {0x1f4, 0, 3.0f, 0.65f, "500 size 3"},
    {0x195, 0, 0.0f, 1.3f, "0x195 size 0: 0x47a147 default 6.0"},
};

// 0x47a239's scale slot ([ebp+0xc], preset to -1.0 at 0x47fbbe), written
// over actor +0xC4 at 0x47fc41: the size does not matter.
const Row kForced[] = {
    {0x31, 0, 6.0f, 1.0f, "0x31 -> 0x47a336 .. 0x47a371 fld1"},
    {0x48, 0, 6.0f, 2.0f, "0x48 -> 0x47a29b 0x6410b4 2.0"},
    {0x49, 0, 6.0f, 1.0f, "0x49 -> 0x47a35f"},
    {0x6c, 0, 6.0f, 0.05f, "0x6c -> 0x47a2d7 0x642ae8 0.05"},
    {0x72, 0, 6.0f, 1.0f, "0x72 -> 0x47a35d"},
    {0x8d, 0, 6.0f, 1.0f, "0x8d -> 0x47a35f"},
    {0x8e, 0, 6.0f, 1.0f, "0x8e -> 0x47a32e"},
    {0x8f, 0, 3.0f, 1.0f, "0x8f -> 0x47a32e"},
    {0x9e, 0, 6.0f, 1.0f, "0x9e -> 0x47a336"},
    {0xc4, 0, 9.0f, 1.0f, "0xc4 -> 0x47a336"},
    {0xfc, 0, 6.0f, 1.0f, "0xfc -> 0x47a35f"},
    {0x194, 0, 6.0f, 1.0f, "0x194 -> 0x47a35d"},
    {0x48, 0, 0.0f, 2.0f, "0x48 size 0: still 2.0"},
    {0x6c, 0, 12.0f, 0.05f, "0x6c size 12: still 0.05"},
};

void ExpectRows(const Row* rows, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const Row& r = rows[i];
        EXPECT_NEAR(SpellFxAttachScale(r.race, r.gender, r.size), r.expected, 1e-6f)
            << "race 0x" << std::hex << r.race << std::dec << " gender " << int(r.gender)
            << " size " << r.size << ": " << r.why;
    }
}

}  // namespace

TEST(SpellFxAttachScale, LaneFiveLivePoints) { ExpectRows(kLive, std::size(kLive)); }

TEST(SpellFxAttachScale, SizeZeroTakesTheRaceDefault) { ExpectRows(kDefaults, std::size(kDefaults)); }

TEST(SpellFxAttachScale, FifthOfTheSizeForEveryUnforcedRace) { ExpectRows(kFifth, std::size(kFifth)); }

TEST(SpellFxAttachScale, SixthScaleRaces) { ExpectRows(kSixth, std::size(kSixth)); }

TEST(SpellFxAttachScale, ForcedScaleRaces) { ExpectRows(kForced, std::size(kForced)); }

// 0x47a0e9's two globals: 0x923c30 (NewZone_Struct offset 592) multiplies the
// fifth by 0x647cd0 (0.7692308) and drops the sixth-scale x 1.3; 0x923798
// drops the x 1.3 only. Both are 0 on lane 5 (the defaults).
TEST(SpellFxAttachScale, ZoneScaleFlags) {
    EXPECT_NEAR(SpellFxAttachScale(1, 0, 6.0f, true, false), 1.2f * 0.7692308f, 1e-6f);
    EXPECT_NEAR(SpellFxAttachScale(1, 0, 6.0f, false, true), 1.2f, 1e-6f);
    EXPECT_NEAR(SpellFxAttachScale(0x195, 0, 6.0f, true, false), 1.0f, 1e-6f);
    EXPECT_NEAR(SpellFxAttachScale(0x195, 0, 6.0f, false, true), 1.0f, 1e-6f);
    EXPECT_NEAR(SpellFxAttachScale(0x48, 0, 6.0f, true, true), 2.0f, 1e-6f);  // forced wins
}
