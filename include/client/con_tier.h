/*
 * B895 (GitLab #211) -- the client's level-based consider colour, in ONE place.
 *
 * WHY THIS HEADER EXISTS. The Tracking Window colours every row by con, and
 * the issue's rule is "reuse the existing consider colour logic, do not write
 * a second con table". The existing table was a private static in
 * src/client/graphics/entity_renderer.cpp (getGreenCutoff() plus the
 * thresholds in EntityRenderer::getConColor()), which a game-thread composer
 * cannot reach without including the renderer. Its body MOVED here verbatim;
 * EntityRenderer::getConColor() is now ConTierForLevels() + ConTierRgb() and
 * draws exactly the colours it drew before. There is still one table.
 *
 * WHAT THE TABLE YIELDS -- read this before trusting it as the oracle's. It
 * produces six tiers: gray, green, light blue, white, yellow, red. It has NO
 * dark-blue tier (the official client's "blue" con), and its white band is
 * diff -1..+2 where eqemu's old-con rule (zone/mob_ai.cpp Mob::GetLevelCon,
 * UseOldConSystem) makes only diff 0 white and +1..+2 yellow. Those are
 * defects of the pre-existing table, recorded in B895's assumptions list as
 * awaiting the oracle; this unit moved the table and did not re-tune it,
 * because re-tuning it changes the placeholder-cube colours too.
 */

#pragma once

#include <cstdint>

namespace eqt {

enum class ConTier : uint8_t {
    Gray = 0,
    Green,
    LightBlue,
    White,
    Yellow,
    Red,
};

/**
 * The con tier of an entity of `entityLevel` seen by a player of
 * `playerLevel`. White when either level is unknown (<= 0) -- the rule the
 * renderer's placeholder cubes always had.
 */
ConTier ConTierForLevels(int playerLevel, int entityLevel);

struct ConRgb {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

/** The colour each tier is drawn in (the renderer's existing values). */
ConRgb ConTierRgb(ConTier tier);

}  // namespace eqt
