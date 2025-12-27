#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "item_instance.h"
#include "ui_settings.h"
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace eqt {

namespace inventory {
class InventoryManager;
}

namespace ui {

class WindowManager;

// Callback types for loot actions
using LootItemCallback = std::function<void(uint16_t corpseId, int16_t slot)>;
using LootAllCallback = std::function<void(uint16_t corpseId)>;
using DestroyAllCallback = std::function<void(uint16_t corpseId)>;
using LootCloseCallback = std::function<void(uint16_t corpseId)>;
using LootIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;

class LootWindow : public WindowBase {
public:
    LootWindow(inventory::InventoryManager* invManager, WindowManager* windowManager);

    // Window lifecycle
    void open(uint16_t corpseId, const std::string& corpseName);
    void close();
    bool isOpen() const { return isOpen_; }
    uint16_t getCorpseId() const { return corpseId_; }

    // Loot data management
    void addLootItem(int16_t slot, std::unique_ptr<inventory::ItemInstance> item);
    void removeItem(int16_t slot);
    void clearLoot();
    bool isEmpty() const { return lootItems_.empty(); }
    int getItemCount() const { return static_cast<int>(lootItems_.size()); }
    const std::map<int16_t, std::unique_ptr<inventory::ItemInstance>>& getLootItems() const { return lootItems_; }
    const inventory::ItemInstance* getItem(int16_t slot) const;

    // Get currently highlighted slot (for tooltip integration)
    int16_t getHighlightedSlot() const { return highlightedSlot_; }

    // Actions
    bool canLootAll() const;
    bool canLootItem(int16_t slot) const;
    std::string getLootAllError() const;
    std::string getLootItemError(int16_t slot) const;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setOnLootItem(LootItemCallback callback) { onLootItem_ = callback; }
    void setOnLootAll(LootAllCallback callback) { onLootAll_ = callback; }
    void setOnDestroyAll(DestroyAllCallback callback) { onDestroyAll_ = callback; }
    void setOnClose(LootCloseCallback callback) { onClose_ = callback; }
    void setIconLookupCallback(LootIconLookupCallback callback) { iconLookupCallback_ = callback; }

    // Get slot at screen position
    int16_t getSlotAtPosition(int x, int y) const;

    // Highlighting
    void setHighlightedSlot(int16_t slotId);
    void clearHighlights();

    // Scrolling
    bool handleMouseWheel(float delta);
    void scrollUp();
    void scrollDown();
    int getScrollOffset() const { return scrollOffset_; }
    int getMaxScrollOffset() const;

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void calculateLayout();
    void renderSlots(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderButtons(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderScrollbar(irr::video::IVideoDriver* driver);

    // Button hit testing
    enum class ButtonId {
        None = -1,
        LootAll = 0,
        DestroyAll = 1,
        Close = 2,
        ScrollUp = 3,
        ScrollDown = 4
    };
    ButtonId getButtonAtPosition(int x, int y) const;
    irr::core::recti getButtonBounds(ButtonId button) const;
    irr::core::recti getScrollbarTrackBounds() const;
    irr::core::recti getScrollbarThumbBounds() const;
    bool isScrollbarAtPosition(int x, int y) const;

    // State
    bool isOpen_ = false;
    uint16_t corpseId_ = 0;
    std::string corpseName_;

    // Loot items (slot -> item)
    std::map<int16_t, std::unique_ptr<inventory::ItemInstance>> lootItems_;

    // Slots for rendering (only VISIBLE_SLOTS)
    std::vector<ItemSlot> slots_;

    // References
    inventory::InventoryManager* inventoryManager_;
    WindowManager* windowManager_;

    // UI state
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    ButtonId hoveredButton_ = ButtonId::None;
    int scrollOffset_ = 0;  // Index of first visible item
    bool draggingScrollbar_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;

    // Callbacks
    LootItemCallback onLootItem_;
    LootAllCallback onLootAll_;
    DestroyAllCallback onDestroyAll_;
    LootCloseCallback onClose_;
    LootIconLookupCallback iconLookupCallback_;

    // Layout constants - initialized from UISettings
    int COLUMNS;
    int ROWS;
    int VISIBLE_SLOTS;
    static constexpr int MAX_SLOTS = 30;  // Fixed maximum
    int SLOT_SIZE;
    int SLOT_SPACING;
    int PADDING;
    int BUTTON_WIDTH;
    int BUTTON_SPACING;
    int TOP_BUTTON_ROW_HEIGHT;
    int BOTTOM_BUTTON_ROW_HEIGHT;
    int SCROLLBAR_WIDTH;
    int SCROLL_BUTTON_HEIGHT;
};

} // namespace ui
} // namespace eqt
