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

namespace eqt { namespace ui { class ChatMessageBuffer; } }

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

// Cached chat panel state
struct ChatPanelState {
    eqt::ui::ChatMessageBuffer* messageBuffer = nullptr;
    int scrollOffset = 0;      // Lines scrolled up from bottom (0 = at bottom)
    bool showTimestamps = false;
    std::string inputText;     // Current input field text
    int cursorPos = 0;
};

/** Render chat panel (message history + input field). */
void renderChatPanel(UIRenderer& ui, const UILayout& layout,
                     ChatPanelState& state);

// Popup types for center screen area
enum class PopupType : uint8_t {
    None = 0,
    Inventory,
    Spellbook,
    Vendor,
    Bank,
    Loot,
    Trade,
    Skills,
    Options
};

// A single inventory slot's visible state
struct SlotDisplayInfo {
    bool hasItem = false;
    std::string itemName;       // Short name for display
    uint32_t iconId = 0;        // Item icon ID (for future icon atlas)
    int32_t quantity = 1;       // Stack count
};

// Cached inventory state for rendering
struct InventoryPanelState {
    PopupType activePopup = PopupType::None;
    // Equipment slots (22 total: 0-21)
    static constexpr int EQUIP_SLOTS = 22;
    SlotDisplayInfo equipSlots[22];
    // General inventory (8 slots)
    static constexpr int GENERAL_SLOTS = 8;
    SlotDisplayInfo generalSlots[8];
    // Hovered slot (-1 = none)
    int hoveredSlot = -1;
};

/** Render inventory popup (equipment + general slots). Only when activePopup == Inventory. */
void renderInventoryPopup(UIRenderer& ui, const UILayout& layout,
                          const InventoryPanelState& inv);

} // namespace Graphics
} // namespace EQT
