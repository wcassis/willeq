#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "item_instance.h"
#include <vector>
#include <functional>
#include <memory>
#include <map>

namespace eqt {

namespace inventory {
class InventoryManager;
}

namespace ui {

// Callback types for tradeskill container
using TradeskillSlotClickCallback = std::function<void(int16_t slotId, bool shift, bool ctrl)>;
using TradeskillSlotHoverCallback = std::function<void(int16_t slotId, int mouseX, int mouseY)>;
using TradeskillCombineCallback = std::function<void()>;
using TradeskillCloseCallback = std::function<void()>;
using TradeskillIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;

/**
 * TradeskillContainerWindow - UI for tradeskill containers (forges, looms, etc.)
 *
 * Supports two modes:
 * 1. World object containers (forges, looms, ovens, etc.) - items stored in WORLD slots
 * 2. Inventory containers (portable tradeskill kits) - items stored in bag slots
 *
 * Features:
 * - Grid of item slots (2 columns, up to 5 rows for 10 slots)
 * - Combine button at the bottom
 * - Title shows container name and tradeskill type
 */
class TradeskillContainerWindow : public WindowBase {
public:
    TradeskillContainerWindow(inventory::InventoryManager* manager);

    // Open for world object (forge, loom, etc.)
    void openForWorldObject(uint32_t dropId, const std::string& name,
                           uint8_t objectType, int slotCount);

    // Open for inventory tradeskill container
    void openForInventoryItem(int16_t containerSlot, const std::string& name,
                             uint8_t bagType, int slotCount);

    // Close the container
    void close();

    // Check if open
    bool isOpen() const { return isOpen_; }

    // Check if this is a world container (vs inventory container)
    bool isWorldContainer() const { return isWorldContainer_; }

    // Get container identifiers
    uint32_t getWorldObjectId() const { return worldObjectId_; }
    int16_t getContainerSlot() const { return containerSlot_; }
    uint8_t getContainerType() const { return containerType_; }

    // Get currently highlighted slot (for tooltip)
    int16_t getHighlightedSlot() const { return highlightedSlot_; }

    // Get slot at screen position
    int16_t getSlotAtPosition(int x, int y) const;

    // Highlighting
    void setHighlightedSlot(int16_t slotId);
    void setInvalidDropSlot(int16_t slotId);
    void clearHighlights();

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setSlotClickCallback(TradeskillSlotClickCallback cb) { slotClickCallback_ = cb; }
    void setSlotHoverCallback(TradeskillSlotHoverCallback cb) { slotHoverCallback_ = cb; }
    void setCombineCallback(TradeskillCombineCallback cb) { combineCallback_ = cb; }
    void setCloseCallback(TradeskillCloseCallback cb) { closeCallback_ = cb; }
    void setIconLookupCallback(TradeskillIconLookupCallback cb) { iconLookupCallback_ = cb; }

    // Get item in a container slot (for tooltip)
    const inventory::ItemInstance* getContainerItem(int16_t slotId) const;

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void calculateLayout();
    int16_t calculateSlotId(int index) const;

    // Rendering helpers
    void renderSlots(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderCombineButton(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Hit testing
    bool isCombineButtonHit(int relX, int relY) const;
    irr::core::recti getCombineButtonBounds() const;
    bool isCloseButtonHit(int x, int y) const;
    irr::core::recti getCloseButtonBounds() const;

    // Rendering helpers
    void renderCloseButton(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Inventory manager reference
    inventory::InventoryManager* manager_;

    // Container state
    bool isOpen_ = false;
    bool isWorldContainer_ = false;
    uint32_t worldObjectId_ = 0;           // For world containers
    int16_t containerSlot_ = inventory::SLOT_INVALID;  // For inventory containers
    uint8_t containerType_ = 0;            // Tradeskill type
    std::string containerName_;
    int slotCount_ = 10;

    // Item storage for world containers (we cache items here since they're not in inventory)
    // For inventory containers, items are accessed directly from InventoryManager
    std::map<int16_t, std::unique_ptr<inventory::ItemInstance>> worldContainerItems_;

    // Slots
    std::vector<ItemSlot> slots_;

    // Layout
    int columns_ = 2;
    int rows_ = 5;

    // Callbacks
    TradeskillSlotClickCallback slotClickCallback_;
    TradeskillSlotHoverCallback slotHoverCallback_;
    TradeskillCombineCallback combineCallback_;
    TradeskillCloseCallback closeCallback_;
    TradeskillIconLookupCallback iconLookupCallback_;

    // UI state
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    int16_t invalidDropSlot_ = inventory::SLOT_INVALID;
    bool combineButtonHovered_ = false;
    bool closeButtonHovered_ = false;

    // Layout constants
    static constexpr int SLOT_SIZE = 40;
    static constexpr int SLOT_SPACING = 4;
    static constexpr int PADDING = 10;
    static constexpr int COMBINE_BUTTON_HEIGHT = 24;
    static constexpr int COMBINE_BUTTON_MARGIN = 8;
};

} // namespace ui
} // namespace eqt
