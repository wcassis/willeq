/**
 * B1172 (#491): the official client's actor scale -- race / gender / spawn
 * size -> the uniform scale a model is drawn at (actor_scale.h; the trace is
 * refactor/B1172/README.md sec. 1).
 *
 * THE DEFECT. fable drew a spawn at a per-race art scale from
 * config/race_models.json times serverSize / 6, so the oracle's own
 * `TrollF000 race=1 gender=0 size=6.000` human target was drawn at 1.0 where
 * the client draws 6 x 0.2 = 1.2: 238 px against the oracle's 288 at the same
 * anchor (B1166). The dark-elf caster, whose spawn size is 0.000 on the wire,
 * is drawn at the default 5.0 x 0.2 = 1.0 by both.
 */

#include <gtest/gtest.h>

#include "client/graphics/actor_scale.h"

using EQT::Graphics::clientActorScale;
using EQT::Graphics::clientDefaultActorSize;
using EQT::Graphics::clientRaceForcedActorScale;
using EQT::Graphics::clientRaceUsesSixthScale;
using EQT::Graphics::clientSizeToActorScale;

// The two figures of the oracle captures (r51, B1118): the human target
// spawned at size 6.000 and the dark-elf caster whose size word is 0.000.
TEST(ClientActorScale, TheOracleTargetAndCaster) {
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 6.0f), 1.2f);  // TrollF000: Human male, size 6
    EXPECT_FLOAT_EQ(clientActorScale(6, 1, 0.0f), 1.0f);  // Testcaster: Dark Elf female, size 0
    // Their ratio is what the frames compare: 1.2 (fable drew 1.0 / 1.0).
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 6.0f) / clientActorScale(6, 1, 0.0f), 1.2f);
}

// 0x47a147's table for the twelve classic races (jump table 0x47a209); gender
// does not enter for them.
TEST(ClientActorScale, DefaultSizeTableForTheClassicRaces) {
    const float expected[13] = {6.0f, 6.0f, 7.0f, 6.0f, 5.0f, 6.0f, 5.0f, 5.5f, 4.0f, 8.0f, 9.0f, 3.5f, 3.0f};
    for (uint16_t race = 1; race <= 12; ++race) {
        for (uint8_t gender = 0; gender <= 2; ++gender) {
            EXPECT_FLOAT_EQ(clientDefaultActorSize(race, gender), expected[race]) << "race " << race;
            // size 0 -> the default -> x 0.2
            EXPECT_FLOAT_EQ(clientActorScale(race, gender, 0.0f), expected[race] * 0.2f) << "race " << race;
        }
    }
}

TEST(ClientActorScale, TheGenderedDefaultsAndTheRest) {
    EXPECT_FLOAT_EQ(clientDefaultActorSize(42, 2), 3.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(42, 0), 6.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(120, 2), 3.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(120, 1), 6.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(43, 0), 4.7f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(128, 0), 6.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(130, 0), 7.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(330, 0), 5.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(24, 0), 6.0f);
    EXPECT_FLOAT_EQ(clientDefaultActorSize(999, 0), 6.0f);
}

// A size the server does send wins over the default, for every race: a
// size-6 dark elf is drawn at 1.2, not at her default 1.0; a shrink to 3
// halves a human; negative / zero mean "not said".
TEST(ClientActorScale, TheSpawnSizeWordIsLinear) {
    EXPECT_FLOAT_EQ(clientActorScale(6, 1, 6.0f), 1.2f);
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 3.0f), 0.6f);
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 15.0f), 3.0f);
    EXPECT_FLOAT_EQ(clientActorScale(9, 0, -1.0f), 1.6f);  // troll default 8
    EXPECT_FLOAT_EQ(clientActorScale(36, 2, 2.0f), 0.4f);   // a_rodent (r51) size 2
}

// 0x47937c / 0x47a0e9: races 403 and >= 405 use size / 6 x 1.3; the zone
// flag (NewZone +592) multiplies the classic races by 0.769 and drops the
// 1.3 of the others.
TEST(ClientActorScale, TheSixthScaleRacesAndTheFlags) {
    EXPECT_FALSE(clientRaceUsesSixthScale(1));
    EXPECT_FALSE(clientRaceUsesSixthScale(402));
    EXPECT_TRUE(clientRaceUsesSixthScale(403));
    EXPECT_FALSE(clientRaceUsesSixthScale(404));
    EXPECT_TRUE(clientRaceUsesSixthScale(405));
    EXPECT_FLOAT_EQ(clientActorScale(405, 0, 6.0f), 1.3f);
    EXPECT_FLOAT_EQ(clientSizeToActorScale(405, 6.0f, true), 1.0f);
    EXPECT_FLOAT_EQ(clientSizeToActorScale(405, 6.0f, false, true), 1.0f);
    EXPECT_NEAR(clientSizeToActorScale(1, 6.0f, true), 1.2f * 0.7692308f, 1e-6f);
}

// 0x47a239: the races whose scale is forced whatever their size.
TEST(ClientActorScale, ForcedScales) {
    float s = -1.0f;
    EXPECT_FALSE(clientRaceForcedActorScale(1, s));
    EXPECT_FALSE(clientRaceForcedActorScale(6, s));
    EXPECT_FLOAT_EQ(clientActorScale(72, 0, 6.0f), 2.0f);
    EXPECT_FLOAT_EQ(clientActorScale(108, 0, 20.0f), 0.05f);
    for (uint16_t race : {49, 73, 114, 141, 142, 143, 158, 196, 252, 404}) {
        EXPECT_FLOAT_EQ(clientActorScale(race, 0, 12.0f), 1.0f) << "race " << race;
    }
}
