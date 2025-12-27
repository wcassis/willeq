#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "inventory_manager.h"
#include <vector>
#include <functional>

namespace eqt {
namespace ui {

// Callback types
using BagSlotClickCallback = std::function<void(int16_t slotId, bool shift, bool ctrl)>;
using BagSlotHoverCallback = std::function<void(int16_t slotId, int mouseX, int mouseY)>;
using BagIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;

class BagWindow : public WindowBase {
public:
    BagWindow(int16_t parentSlotId, int bagSize, inventory::InventoryManager* manager);

    // Get the general slot this bag is in
    int16_t getParentSlotId() const { return parentSlotId_; }

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setSlotClickCallback(BagSlotClickCallback callback) { slotClickCallback_ = callback; }
    void setSlotHoverCallback(BagSlotHoverCallback callback) { slotHoverCallback_ = callback; }
    void setIconLookupCallback(BagIconLookupCallback callback) { iconLookupCallback_ = callback; }

    // Get slot at position
    int16_t getSlotAtPosition(int x, int y) const;

    // Update highlighting
    void setHighlightedSlot(int16_t slotId);
    void setInvalidDropSlot(int16_t slotId);
    void clearHighlights();

    // Get bag dimensions for tiling
    int getBagSlotCount() const { return bagSize_; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void calculateLayout();

    // The general slot this bag occupies
    int16_t parentSlotId_;

    // Number of slots in this bag
    int bagSize_;

    // Inventory manager reference
    inventory::InventoryManager* manager_;

    // Bag slots
    std::vector<ItemSlot> slots_;

    // Layout
    int columns_ = 2;
    int rows_ = 1;

    // Callbacks
    BagSlotClickCallback slotClickCallback_;
    BagSlotHoverCallback slotHoverCallback_;
    BagIconLookupCallback iconLookupCallback_;

    // Highlighted slot
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    int16_t invalidDropSlot_ = inventory::SLOT_INVALID;

    // Layout constants
    static constexpr int SLOT_SIZE = 36;
    static constexpr int SLOT_SPACING = 4;
    static constexpr int PADDING = 8;
};

} // namespace ui
} // namespace eqt
