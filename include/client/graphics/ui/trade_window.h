#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "item_instance.h"
#include "ui_settings.h"
#include <vector>
#include <functional>
#include <memory>
#include <array>

class TradeManager;

namespace eqt {

namespace inventory {
class InventoryManager;
}

namespace ui {

class WindowManager;

// Callback types for trade actions
using TradeAcceptCallback = std::function<void()>;
using TradeCancelCallback = std::function<void()>;
using TradeMoneyChangeCallback = std::function<void(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp)>;
using TradeIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;
using TradeSlotClickCallback = std::function<void(int16_t tradeSlot, bool shift, bool ctrl)>;
using TradeErrorCallback = std::function<void(const std::string& message)>;
using TradeMoneyAreaClickCallback = std::function<void()>;  // Called when own money area is clicked

class TradeWindow : public WindowBase {
public:
    static constexpr int TRADE_SLOTS_PER_PLAYER = 8;

    TradeWindow(inventory::InventoryManager* invManager, WindowManager* windowManager);

    // Window lifecycle
    void open(uint32_t partnerSpawnId, const std::string& partnerName, bool isNpcTrade = false);
    void close(bool sendCancel = true);  // Set false when closing due to trade completion
    bool isOpen() const { return isOpen_; }
    bool isNpcTrade() const { return isNpcTrade_; }
    uint32_t getPartnerSpawnId() const { return partnerSpawnId_; }
    const std::string& getPartnerName() const { return partnerName_; }

    // TradeManager reference (set by WindowManager during init)
    void setTradeManager(TradeManager* tradeMgr) { tradeManager_ = tradeMgr; }

    // Own item slots (display what we're trading)
    void updateOwnSlot(int slot, const inventory::ItemInstance* item);
    void clearOwnSlot(int slot);

    // Partner item slots (display what they're trading)
    void updatePartnerSlot(int slot, const inventory::ItemInstance* item);
    void clearPartnerSlot(int slot);
    void setPartnerItem(int slot, std::unique_ptr<inventory::ItemInstance> item);

    // Money display
    void setOwnMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp);
    void setPartnerMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp);

    // Accept state display
    void setOwnAccepted(bool accepted);
    void setPartnerAccepted(bool accepted);
    bool isOwnAccepted() const { return ownAccepted_; }
    bool isPartnerAccepted() const { return partnerAccepted_; }

    // Get currently highlighted slot (for tooltip integration)
    // Returns slot 0-3 for own slots, 4-7 for partner slots, or SLOT_INVALID
    int16_t getHighlightedSlot() const { return highlightedSlot_; }
    bool isHighlightedSlotOwn() const { return highlightedSlotIsOwn_; }

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setOnAccept(TradeAcceptCallback callback) { onAccept_ = callback; }
    void setOnCancel(TradeCancelCallback callback) { onCancel_ = callback; }
    void setOnMoneyChange(TradeMoneyChangeCallback callback) { onMoneyChange_ = callback; }
    void setIconLookupCallback(TradeIconLookupCallback callback) { iconLookupCallback_ = callback; }
    void setSlotClickCallback(TradeSlotClickCallback callback) { slotClickCallback_ = callback; }
    void setMoneyAreaClickCallback(TradeMoneyAreaClickCallback callback) { moneyAreaClickCallback_ = callback; }

    // Get slot at screen position (returns 0-3 for own, 4-7 for partner, or SLOT_INVALID)
    int16_t getSlotAtPosition(int x, int y, bool& isOwnSlot) const;

    // Get the item being displayed at a slot for tooltip
    const inventory::ItemInstance* getDisplayedItem(int slot, bool isOwn) const;

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void calculateLayout();
    void renderSlots(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderMoney(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderButtons(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderDivider(irr::video::IVideoDriver* driver);
    void renderAcceptHighlights(irr::video::IVideoDriver* driver);

    // Button hit testing
    enum class ButtonId {
        None = -1,
        Accept = 0,
        Cancel = 1
    };
    ButtonId getButtonAtPosition(int x, int y) const;
    irr::core::recti getButtonBounds(ButtonId button) const;

    // Money area hit testing
    bool isInOwnMoneyArea(int x, int y) const;
    irr::core::recti getOwnMoneyAreaBounds() const;

    // Highlighting
    void setHighlightedSlot(int16_t slotId, bool isOwn);
    void clearHighlights();

    // State
    bool isOpen_ = false;
    bool isNpcTrade_ = false;  // True if trading with NPC (simpler UI, no partner slots)
    uint32_t partnerSpawnId_ = 0;
    std::string partnerName_;

    // Slot data - own side references items in trade slots via TradeManager
    // Partner side stores unique_ptr copies received from server
    std::array<std::unique_ptr<inventory::ItemInstance>, TRADE_SLOTS_PER_PLAYER> partnerItems_;

    // Visual slots for rendering
    std::vector<ItemSlot> ownSlots_;      // 4 slots for our items
    std::vector<ItemSlot> partnerSlots_;  // 4 slots for partner items

    // Money amounts
    struct Money {
        uint32_t platinum = 0;
        uint32_t gold = 0;
        uint32_t silver = 0;
        uint32_t copper = 0;
    };
    Money ownMoney_;
    Money partnerMoney_;

    // Accept state
    bool ownAccepted_ = false;
    bool partnerAccepted_ = false;

    // References
    inventory::InventoryManager* inventoryManager_;
    WindowManager* windowManager_;
    TradeManager* tradeManager_ = nullptr;

    // UI state
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    bool highlightedSlotIsOwn_ = true;
    ButtonId hoveredButton_ = ButtonId::None;

    // Callbacks
    TradeAcceptCallback onAccept_;
    TradeCancelCallback onCancel_;
    TradeMoneyChangeCallback onMoneyChange_;
    TradeIconLookupCallback iconLookupCallback_;
    TradeSlotClickCallback slotClickCallback_;
    TradeMoneyAreaClickCallback moneyAreaClickCallback_;

    // Layout constants
    static constexpr int COLUMNS = 2;
    static constexpr int ROWS = 4;  // 8 slots per player in 2x4 grid
    static constexpr int SLOT_SIZE = 40;
    static constexpr int SLOT_SPACING = 4;
    static constexpr int PADDING = 8;
    static constexpr int DIVIDER_WIDTH = 4;
    static constexpr int MONEY_ROW_HEIGHT = 20;
    static constexpr int BUTTON_WIDTH = 70;
    static constexpr int BUTTON_SPACING = 10;
    static constexpr int SECTION_SPACING = 8;
    static constexpr int LABEL_HEIGHT = 16;
};

} // namespace ui
} // namespace eqt
