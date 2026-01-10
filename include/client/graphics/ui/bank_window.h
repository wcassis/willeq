#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "inventory_manager.h"
#include <array>
#include <functional>

namespace eqt {
namespace ui {

// Forward declare CurrencyType (defined in money_input_dialog.h)
enum class CurrencyType;

// Callback types (reuse from inventory_window.h patterns)
using BankBagClickCallback = std::function<void(int16_t bankSlot)>;
using BankSlotClickCallback = std::function<void(int16_t slotId, bool shift, bool ctrl)>;
using BankSlotHoverCallback = std::function<void(int16_t slotId, int mouseX, int mouseY)>;
using BankIconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;
using BankCurrencyClickCallback = std::function<void(CurrencyType type, uint32_t maxAmount)>;
using BankReadItemCallback = std::function<void(const std::string& bookText, uint8_t bookType)>;
using BankCloseCallback = std::function<void()>;

// Callback for moving currency between bank and inventory
// Parameters: coinType (0=cp,1=sp,2=gp,3=pp), amount, fromBank (true=bank->inv, false=inv->bank)
using BankCurrencyMoveCallback = std::function<void(int32_t coinType, int32_t amount, bool fromBank)>;

// Callback for currency conversion in bank
// Parameters: fromCoinType (0=cp,1=sp,2=gp), amount (number of source coins to convert)
// Note: conversion is always "up" (cp->sp->gp->pp), 10:1 ratio
using BankCurrencyConvertCallback = std::function<void(int32_t fromCoinType, int32_t amount)>;

class BankWindow : public WindowBase {
public:
    BankWindow(inventory::InventoryManager* manager);

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setBagClickCallback(BankBagClickCallback callback) { bagClickCallback_ = callback; }
    void setSlotClickCallback(BankSlotClickCallback callback) { slotClickCallback_ = callback; }
    void setSlotHoverCallback(BankSlotHoverCallback callback) { slotHoverCallback_ = callback; }
    void setIconLookupCallback(BankIconLookupCallback callback) { iconLookupCallback_ = callback; }
    void setCurrencyClickCallback(BankCurrencyClickCallback callback) { currencyClickCallback_ = callback; }
    void setReadItemCallback(BankReadItemCallback callback) { readItemCallback_ = callback; }
    void setCurrencyConvertCallback(BankCurrencyConvertCallback callback) { currencyConvertCallback_ = callback; }

    // Get slot at position (returns slot ID or SLOT_INVALID)
    int16_t getSlotAtPosition(int x, int y) const;

    // Update highlighting
    void setHighlightedSlot(int16_t slotId);
    void setInvalidDropSlot(int16_t slotId);
    void clearHighlights();

    // Currency display (shows player's carried money)
    void setCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
        platinum_ = platinum; gold_ = gold; silver_ = silver; copper_ = copper;
    }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void renderBankSlots(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui);
    void renderSharedBankSlots(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui);
    void renderCurrency(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui);
    void renderConversionButtons(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui);
    void renderCloseButton(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);

    // Helper to check if point is on currency
    CurrencyType getCurrencyAtPosition(int relX, int relY, bool& found) const;

    // Inventory manager reference
    inventory::InventoryManager* manager_;

    // Bank slots (16 main bank slots: 2000-2015)
    std::array<ItemSlot, inventory::BANK_COUNT> bankSlots_;

    // Shared bank slots (2 slots: 2500-2501)
    std::array<ItemSlot, inventory::SHARED_BANK_COUNT> sharedBankSlots_;

    // Close button
    irr::core::recti closeButtonBounds_;
    bool closeButtonHighlighted_ = false;

    // Callbacks
    BankBagClickCallback bagClickCallback_;
    BankSlotClickCallback slotClickCallback_;
    BankSlotHoverCallback slotHoverCallback_;
    BankIconLookupCallback iconLookupCallback_;
    BankCurrencyClickCallback currencyClickCallback_;
    BankReadItemCallback readItemCallback_;
    BankCurrencyConvertCallback currencyConvertCallback_;

    // Highlighted slot
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    int16_t invalidDropSlot_ = inventory::SLOT_INVALID;

    // Currency (player's carried money, displayed for reference)
    uint32_t platinum_ = 0;
    uint32_t gold_ = 0;
    uint32_t silver_ = 0;
    uint32_t copper_ = 0;

    // Currency click bounds (relative to window)
    irr::core::recti platinumBounds_;
    irr::core::recti goldBounds_;
    irr::core::recti silverBounds_;
    irr::core::recti copperBounds_;

    // Conversion button bounds (relative to window)
    // Each currency (except platinum) has "10" and "All" buttons to convert to next currency
    irr::core::recti copperConvert10Bounds_;
    irr::core::recti copperConvertAllBounds_;
    irr::core::recti silverConvert10Bounds_;
    irr::core::recti silverConvertAllBounds_;
    irr::core::recti goldConvert10Bounds_;
    irr::core::recti goldConvertAllBounds_;
    irr::core::recti convertAllChainBounds_;  // Convert all currencies in chain (cp->sp->gp->pp)

    // Conversion button highlight states
    bool copperConvert10Highlighted_ = false;
    bool copperConvertAllHighlighted_ = false;
    bool silverConvert10Highlighted_ = false;
    bool silverConvertAllHighlighted_ = false;
    bool goldConvert10Highlighted_ = false;
    bool goldConvertAllHighlighted_ = false;
    bool convertAllChainHighlighted_ = false;

    // Layout constants
    static constexpr int SLOT_SIZE = 36;
    static constexpr int SLOT_SPACING = 4;
    static constexpr int PADDING = 8;
    static constexpr int BANK_COLUMNS = 4;
    static constexpr int BANK_ROWS = 4;  // 16 slots in 4x4 grid
    static constexpr int SHARED_BANK_COLUMNS = 2;
    static constexpr int SECTION_GAP = 12;  // Gap between bank and shared bank sections
};

} // namespace ui
} // namespace eqt
