/*
 * Static UI panels — simple render functions, not a class hierarchy.
 *
 * U03d: Each panel is a free function that takes a UIRenderer + data.
 * Data comes from cached bridge events, not from EverQuest pointers.
 */

#pragma once

#include <irrlicht.h>
#include <string>
#include <cstdint>

namespace EQT {
namespace Graphics {

class UIRenderer;
struct UILayout;

// Cached player stats (from PlayerStatsChanged bridge event)
struct PlayerStatsData {
    uint32_t curHP = 0, maxHP = 1;
    uint32_t curMana = 0, maxMana = 1;
    uint32_t curEndurance = 0, maxEndurance = 1;
    uint8_t level = 1;
    std::string name;
};

// Cached target info (from TargetChanged bridge event)
struct TargetInfoData {
    uint16_t spawnId = 0;  // 0 = no target
    std::string name;
    uint8_t level = 0;
    uint8_t hpPercent = 100;
};

/** Render player status panel (HP/mana/stamina bars + name). */
void renderPlayerStatus(UIRenderer& ui, const UILayout& layout,
                        const PlayerStatsData& stats);

/** Render target info panel (HP bar + name). Only renders if target exists. */
void renderTargetInfo(UIRenderer& ui, const UILayout& layout,
                      const TargetInfoData& target);

} // namespace Graphics
} // namespace EQT
