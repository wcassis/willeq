// B895 (GitLab #211): the level-based con table, moved verbatim out of
// src/client/graphics/entity_renderer.cpp so the game-thread tracking list can
// colour its rows with the SAME table the placeholder cubes use. See the
// banner in include/client/con_tier.h.

#include "client/con_tier.h"

namespace eqt {

namespace {

// EQ Titanium con color rules
// Green cutoff thresholds vary by player level
int GreenCutoff(int playerLevel) {
    if (playerLevel <= 7) return -4;
    if (playerLevel <= 12) return -6;
    if (playerLevel <= 20) return -8;
    if (playerLevel <= 25) return -10;
    if (playerLevel <= 30) return -12;
    if (playerLevel <= 35) return -14;
    if (playerLevel <= 40) return -16;
    if (playerLevel <= 45) return -18;
    if (playerLevel <= 50) return -20;
    if (playerLevel <= 55) return -21;
    return -22;  // 56-65
}

}  // namespace

ConTier ConTierForLevels(int playerLevel, int entityLevel) {
    if (playerLevel <= 0 || entityLevel <= 0) {
        return ConTier::White;  // White for unknown
    }

    const int diff = entityLevel - playerLevel;
    const int greenCutoff = GreenCutoff(playerLevel);

    if (diff <= greenCutoff) {
        return ConTier::Gray;
    } else if (diff < -5) {
        return ConTier::Green;
    } else if (diff < -1) {
        return ConTier::LightBlue;
    } else if (diff < 3) {
        return ConTier::White;
    } else if (diff < 6) {
        return ConTier::Yellow;
    }
    return ConTier::Red;
}

ConRgb ConTierRgb(ConTier tier) {
    switch (tier) {
        case ConTier::Gray:      return {128, 128, 128};
        case ConTier::Green:     return {0, 200, 0};
        case ConTier::LightBlue: return {0, 160, 255};
        case ConTier::White:     return {255, 255, 255};
        case ConTier::Yellow:    return {255, 255, 0};
        case ConTier::Red:       return {255, 0, 0};
    }
    return {255, 255, 255};
}

}  // namespace eqt
