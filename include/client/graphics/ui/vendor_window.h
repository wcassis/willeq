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

// Callback types for vendor actions
using VendorBuyCallback = std::function<void(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)>;
using VendorSellCallback = std::function<void(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)>;
using VendorCloseCallback = std::function<void(uint16_t npcId)>;
using VendorIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;

// Vendor window mode (buy from vendor or sell to vendor)
enum class VendorMode {
    BUY,
    SELL
};

// Vendor item with slot info (for buy mode - items from vendor)
struct VendorItem {
    std::unique_ptr<inventory::ItemInstance> item;
    uint32_t vendorSlot = 0;  // Slot in vendor's inventory
};

// Sellable item from player inventory (for sell mode)
struct SellableItem {
    uint32_t inventorySlot = 0;       // Titanium inventory slot
    std::string name;
    uint32_t iconId = 0;
    uint32_t basePrice = 0;           // Item base price (before sell rate)
    uint32_t stackSize = 1;           // Current stack size
    bool isStackable = false;
    bool canSell = true;              // false if NO_TRADE
};

// Sort options for vendor list
enum class VendorSortMode {
    None,
    NameAsc,
    NameDesc,
    PriceAsc,
    PriceDesc
};

class VendorWindow : public WindowBase {
public:
    VendorWindow(inventory::InventoryManager* invManager, WindowManager* windowManager);

    // Window lifecycle
    void open(uint16_t npcId, const std::string& vendorName, float sellRate);
    void close();
    bool isOpen() const { return isOpen_; }
    uint16_t getNpcId() const { return npcId_; }
    float getSellRate() const { return sellRate_; }

    // Vendor data management
    void addVendorItem(uint32_t slot, std::unique_ptr<inventory::ItemInstance> item);
    void removeItem(uint32_t slot);
    void clearItems();
    bool isEmpty() const { return vendorItems_.empty(); }
    int getItemCount() const { return static_cast<int>(vendorItems_.size()); }
    const std::map<uint32_t, VendorItem>& getVendorItems() const { return vendorItems_; }
    const inventory::ItemInstance* getItem(uint32_t slot) const;

    // Mode switching
    VendorMode getMode() const { return currentMode_; }
    void setMode(VendorMode mode);

    // Selection (works for both buy and sell mode)
    int32_t getSelectedSlot() const { return selectedSlot_; }
    const VendorItem* getSelectedItem() const;        // For buy mode
    const SellableItem* getSelectedSellItem() const;  // For sell mode
    void setSelectedSlot(int32_t slot);
    void clearSelection();

    // Get currently highlighted slot (for tooltip integration)
    int32_t getHighlightedSlot() const { return highlightedSlot_; }
    const inventory::ItemInstance* getHighlightedItem() const;

    // Sell mode: player inventory items
    void setSellableItems(const std::vector<SellableItem>& items);
    void clearSellableItems();
    const std::vector<SellableItem>& getSellableItems() const { return sellableItems_; }
    uint32_t calculateSellPrice(uint32_t basePrice, uint32_t quantity) const;

    // Player money (for affordability checking)
    void setPlayerMoney(int32_t platinum, int32_t gold, int32_t silver, int32_t copper);
    int64_t getPlayerMoneyInCopper() const { return playerMoneyCopper_; }

    // Actions
    bool canBuyItem(uint32_t slot, uint32_t quantity) const;
    std::string getBuyError(uint32_t slot, uint32_t quantity) const;
    int32_t getItemPrice(uint32_t slot) const;
    uint32_t getMaxAffordableQuantity(uint32_t slot) const;
    std::string formatPrice(int32_t copperAmount) const;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Scrolling
    bool handleMouseWheel(float delta);
    void scrollUp();
    void scrollDown();
    int getScrollOffset() const { return scrollOffset_; }
    int getMaxScrollOffset() const;

    // Sorting
    VendorSortMode getSortMode() const { return sortMode_; }
    void setSortMode(VendorSortMode mode);
    void toggleSortByName();
    void toggleSortByPrice();

    // Callbacks
    void setOnBuy(VendorBuyCallback callback) { onBuy_ = callback; }
    void setOnSell(VendorSellCallback callback) { onSell_ = callback; }
    void setOnClose(VendorCloseCallback callback) { onClose_ = callback; }
    void setIconLookupCallback(VendorIconLookupCallback callback) { iconLookupCallback_ = callback; }

    // Get slot at screen position (returns vendor slot or -1)
    int32_t getSlotAtPosition(int x, int y) const;

    // Highlighting
    void setHighlightedSlot(int32_t slot);
    void clearHighlights();

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void calculateLayout();
    void renderListView(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderColumnHeaders(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderButtons(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderScrollbar(irr::video::IVideoDriver* driver);

    // Sorting helpers
    void rebuildSortedList();

    // Sell mode rendering
    void renderSellListView(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderTabs(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void rebuildSellSortedList();

    // Button hit testing
    enum class ButtonId {
        None = -1,
        Buy = 0,      // Also used as "Sell" in sell mode
        Close = 1,
        ScrollUp = 2,
        ScrollDown = 3,
        SortName = 4,
        SortPrice = 5,
        TabBuy = 6,
        TabSell = 7
    };
    ButtonId getButtonAtPosition(int x, int y) const;
    irr::core::recti getButtonBounds(ButtonId button) const;
    irr::core::recti getScrollbarTrackBounds() const;
    irr::core::recti getScrollbarThumbBounds() const;
    irr::core::recti getListAreaBounds() const;
    irr::core::recti getRowBounds(int rowIndex) const;

    // State
    bool isOpen_ = false;
    uint16_t npcId_ = 0;
    std::string vendorName_;
    float sellRate_ = 1.0f;
    VendorMode currentMode_ = VendorMode::BUY;

    // Player money (in copper)
    int64_t playerMoneyCopper_ = 0;

    // Buy mode: Vendor items (vendor slot -> item)
    std::map<uint32_t, VendorItem> vendorItems_;

    // Sell mode: Player inventory items that can be sold
    std::vector<SellableItem> sellableItems_;
    std::vector<size_t> sellSortedIndices_;  // Sorted indices into sellableItems_

    // Sorted list of vendor slot IDs (for display order in buy mode)
    std::vector<uint32_t> sortedSlots_;
    VendorSortMode sortMode_ = VendorSortMode::NameAsc;

    // References
    inventory::InventoryManager* inventoryManager_;
    WindowManager* windowManager_;

    // UI state
    int32_t selectedSlot_ = -1;      // Currently selected item (-1 = none)
    int32_t highlightedSlot_ = -1;   // Currently hovered item (-1 = none)
    int highlightedRow_ = -1;        // Currently hovered row (-1 = none)
    ButtonId hoveredButton_ = ButtonId::None;
    int scrollOffset_ = 0;           // Index of first visible item
    bool draggingScrollbar_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;
    bool shiftHeld_ = false;         // Track shift key state for stack buying

    // Callbacks
    VendorBuyCallback onBuy_;
    VendorSellCallback onSell_;
    VendorCloseCallback onClose_;
    VendorIconLookupCallback iconLookupCallback_;

    // Layout constants for list view
    static constexpr int VISIBLE_ROWS = 12;               // Number of visible rows
    static constexpr int ROW_HEIGHT = 22;                 // Height of each row
    static constexpr int ICON_SIZE = 20;                  // Small icon in list
    static constexpr int NAME_COLUMN_WIDTH = 200;         // Width of name column
    static constexpr int PRICE_COLUMN_WIDTH = 80;         // Width of price column
    static constexpr int HEADER_HEIGHT = 20;              // Column header height
    static constexpr int TAB_HEIGHT = 24;                 // Tab bar height
    static constexpr int TAB_WIDTH = 60;                  // Width of each tab
    static constexpr int PADDING = 8;
    static constexpr int BUTTON_WIDTH = 70;
    static constexpr int BUTTON_ROW_HEIGHT = 26;
    static constexpr int SCROLLBAR_WIDTH = 16;
    static constexpr int SCROLL_BUTTON_HEIGHT = 16;
};

} // namespace ui
} // namespace eqt
