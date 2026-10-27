/*
 * B1164 (#483): the attach scale s of a spellsnew.eff slot
 * (include/client/spell/spell_fx_attach_scale.h).
 *
 * Pinned on lane 5 by a read-only gdb probe of the official client
 * (refactor/B1164/README.md sec. 10, review/B1164/worker/dev5/gdb/): record 80
 * (spell 93) cast on a race-1 `#spawn` of server size 6 (OP_NewSpawn size=6.000)
 * draws its Target slots with s = 1.200000 (track row 0 = -0.459220, 1.108655,
 * 0; 0x1005c4f0 = 1.000000), and its Source slots on the race-6 caster whose
 * OP_ZoneEntry carries size 0 with s = 1.000000 (DEFAULT) / 0.999755 (a hand
 * bone). The client's rule behind both: eqgame 0x47a0e9 = 0.2 x size, the
 * size being the race default of 0x47a147 when the spawn carries none.
 * RED before B1164: fable passed kSpellFxAttachScale = 1.0 for every actor.
 */

#include <gtest/gtest.h>

#include "client/spell/spell_fx_attach_scale.h"

using EQ::SpellFxAttachScale;
using EQ::SpellFxRaceDefaultSize;

// The 93 target geometry: the lane's `#spawn TrollF 1 1 0 30000` is race 1
// at size 6 (every B1118/B1130/B1164 capture's OP_NewSpawn).
TEST(SpellFxAttachScale, Spell93TargetHumanSize6Is1_2) {
    EXPECT_FLOAT_EQ(SpellFxAttachScale(1, 6.0f), 1.2f);
}

// The gate's caster: race 6 (dark elf), OP_ZoneEntry size 0 -> the race
// default 5.0 -> s = 1.0, the probe's 1.000000.
TEST(SpellFxAttachScale, CasterDarkElfSize0IsRaceDefault1_0) {
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(6), 5.0f);
    EXPECT_FLOAT_EQ(SpellFxAttachScale(6, 0.0f), 1.0f);
    // The DarkF `#spawn` of the same leg (OP_NewSpawn size=5.000): also 1.0.
    EXPECT_FLOAT_EQ(SpellFxAttachScale(6, 5.0f), 1.0f);
}

// A spawn of server size 1 is drawn at a fifth of its model, and so are its
// emitters: s is 0.2 x size, not size / race default.
TEST(SpellFxAttachScale, SizeOneCasterIsOneFifth) {
    EXPECT_FLOAT_EQ(SpellFxAttachScale(1, 1.0f), 0.2f);
    EXPECT_FLOAT_EQ(SpellFxAttachScale(6, 1.0f), 0.2f);
}

// eqgame 0x47a147's table for the races this header covers.
TEST(SpellFxAttachScale, RaceDefaultSizes) {
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(1), 6.0f);   // human: the default case
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(2), 7.0f);   // barbarian
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(3), 6.0f);   // erudite
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(4), 5.0f);   // wood elf
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(7), 5.5f);   // half elf
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(8), 4.0f);   // dwarf
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(9), 8.0f);   // troll
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(10), 9.0f);  // ogre
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(11), 3.5f);  // halfling
    EXPECT_FLOAT_EQ(SpellFxRaceDefaultSize(12), 3.0f);  // gnome
    EXPECT_FLOAT_EQ(SpellFxAttachScale(1, 0.0f), 1.2f); // a human with no size: 0.2 x 6
}

// Races at or above 0x1a are not traced (0x47a239 may rewrite their scale):
// they keep the previous constant, never a guessed value.
TEST(SpellFxAttachScale, UntracedRaceKeepsOne) {
    EXPECT_FALSE(EQ::SpellFxAttachScaleTraced(0x1a));
    EXPECT_FLOAT_EQ(SpellFxAttachScale(0x48, 6.0f), 1.0f);
    EXPECT_FLOAT_EQ(SpellFxAttachScale(127, 0.0f), 1.0f);
    EXPECT_TRUE(EQ::SpellFxAttachScaleTraced(0x19));
}
