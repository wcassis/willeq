#include "client/graphics/ui/bank_window.h"
#include "client/graphics/ui/money_input_dialog.h"  // For CurrencyType definition
#include <sstream>

namespace eqt {
namespace ui {

BankWindow::BankWindow(inventory::InventoryManager* manager)
    : WindowBase(L"Bank", 200, 340)  // Will be calculated in initializeSlots
    , manager_(manager)
{
    initializeSlots();
}

void BankWindow::initializeSlots() {
    // Calculate window dimensions based on slot layout
    // Main bank: 4x4 grid = 16 slots
    // Shared bank: 2 slots in a row below
    // Currency display with conversion buttons below that
    // Close button and Convert All button at bottom

    const int slotStep = SLOT_SIZE + SLOT_SPACING;

    // Calculate content dimensions
    int bankGridWidth = BANK_COLUMNS * SLOT_SIZE + (BANK_COLUMNS - 1) * SLOT_SPACING;
    int bankGridHeight = BANK_ROWS * SLOT_SIZE + (BANK_ROWS - 1) * SLOT_SPACING;

    int sharedBankWidth = SHARED_BANK_COLUMNS * SLOT_SIZE + (SHARED_BANK_COLUMNS - 1) * SLOT_SPACING;

    // Window needs extra width for conversion buttons next to currency
    // Currency display: "PP 99999999" (~70px) + "10" button (20px) + "All" button (24px) + gaps
    int currencyAreaWidth = 70 + 4 + 20 + 4 + 24;  // currency + gap + 10btn + gap + allbtn
    int contentWidth = std::max(bankGridWidth, currencyAreaWidth);

    // Content height: bank grid + gap + shared bank label + shared bank row + gap + currency + buttons
    int sharedBankLabelHeight = 14;
    int currencyHeight = 44;  // 4 lines of currency
    int buttonRowHeight = 30;

    int contentHeight = bankGridHeight + SECTION_GAP + sharedBankLabelHeight + SLOT_SIZE +
                        SECTION_GAP + currencyHeight + buttonRowHeight;

    // Calculate window size (add padding and borders)
    int windowWidth = contentWidth + PADDING * 2 + BORDER_WIDTH * 2;
    int windowHeight = contentHeight + PADDING * 2 + BORDER_WIDTH * 2 + TITLE_BAR_HEIGHT;

    setSize(windowWidth, windowHeight);

    // Initialize main bank slots (4x4 grid)
    int bankStartX = BORDER_WIDTH + PADDING;
    int bankStartY = BORDER_WIDTH + TITLE_BAR_HEIGHT + PADDING;

    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        int col = i % BANK_COLUMNS;
        int row = i / BANK_COLUMNS;

        int x = bankStartX + col * slotStep;
        int y = bankStartY + row * slotStep;

        int16_t slotId = inventory::BANK_BEGIN + i;
        bankSlots_[i] = ItemSlot(slotId, SlotType::Bank, x, y, SLOT_SIZE);
    }

    // Initialize shared bank slots (2 slots in a row, centered below main bank)
    int sharedBankStartY = bankStartY + bankGridHeight + SECTION_GAP + sharedBankLabelHeight;
    int sharedBankStartX = bankStartX + (bankGridWidth - sharedBankWidth) / 2;

    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        int x = sharedBankStartX + i * slotStep;
        int y = sharedBankStartY;

        int16_t slotId = inventory::SHARED_BANK_BEGIN + i;
        sharedBankSlots_[i] = ItemSlot(slotId, SlotType::Bank, x, y, SLOT_SIZE);
    }

    // Currency display bounds (left side, below shared bank)
    int currencyStartX = bankStartX;
    int currencyStartY = sharedBankStartY + SLOT_SIZE + SECTION_GAP;
    int currencyLabelWidth = 55;  // Reduced to fit buttons
    int lineHeight = 10;

    platinumBounds_ = irr::core::recti(currencyStartX, currencyStartY,
                                        currencyStartX + currencyLabelWidth, currencyStartY + lineHeight);
    goldBounds_ = irr::core::recti(currencyStartX, currencyStartY + lineHeight,
                                    currencyStartX + currencyLabelWidth, currencyStartY + lineHeight * 2);
    silverBounds_ = irr::core::recti(currencyStartX, currencyStartY + lineHeight * 2,
                                      currencyStartX + currencyLabelWidth, currencyStartY + lineHeight * 3);
    copperBounds_ = irr::core::recti(currencyStartX, currencyStartY + lineHeight * 3,
                                      currencyStartX + currencyLabelWidth, currencyStartY + lineHeight * 4);

    // Conversion button bounds (next to each currency except platinum)
    // Buttons are small: "10" (18px) and "All" (22px)
    int btn10Width = 18;
    int btnAllWidth = 22;
    int btnHeight = 10;
    int btnGap = 2;
    int btnStartX = currencyStartX + currencyLabelWidth + btnGap;

    // Gold -> Platinum conversion buttons (row 1, after PP label - PP has no buttons)
    // Silver -> Gold conversion buttons (row 2, after GP label)
    // Copper -> Silver conversion buttons (row 3, after SP label)

    // Note: Conversion buttons are placed next to the SOURCE currency
    // Gold line (row 1): convert GP->PP
    goldConvert10Bounds_ = irr::core::recti(
        btnStartX, currencyStartY + lineHeight,
        btnStartX + btn10Width, currencyStartY + lineHeight + btnHeight);
    goldConvertAllBounds_ = irr::core::recti(
        btnStartX + btn10Width + btnGap, currencyStartY + lineHeight,
        btnStartX + btn10Width + btnGap + btnAllWidth, currencyStartY + lineHeight + btnHeight);

    // Silver line (row 2): convert SP->GP
    silverConvert10Bounds_ = irr::core::recti(
        btnStartX, currencyStartY + lineHeight * 2,
        btnStartX + btn10Width, currencyStartY + lineHeight * 2 + btnHeight);
    silverConvertAllBounds_ = irr::core::recti(
        btnStartX + btn10Width + btnGap, currencyStartY + lineHeight * 2,
        btnStartX + btn10Width + btnGap + btnAllWidth, currencyStartY + lineHeight * 2 + btnHeight);

    // Copper line (row 3): convert CP->SP
    copperConvert10Bounds_ = irr::core::recti(
        btnStartX, currencyStartY + lineHeight * 3,
        btnStartX + btn10Width, currencyStartY + lineHeight * 3 + btnHeight);
    copperConvertAllBounds_ = irr::core::recti(
        btnStartX + btn10Width + btnGap, currencyStartY + lineHeight * 3,
        btnStartX + btn10Width + btnGap + btnAllWidth, currencyStartY + lineHeight * 3 + btnHeight);

    // Platinum line (row 0): convert all chain (cp->sp->gp->pp)
    // Aligned with "All" buttons on other rows
    convertAllChainBounds_ = irr::core::recti(
        btnStartX + btn10Width + btnGap, currencyStartY,
        btnStartX + btn10Width + btnGap + btnAllWidth, currencyStartY + btnHeight);

    // Bottom button row: Close button only
    int buttonWidth = 50;
    int buttonHeight = getButtonHeight();
    int buttonY = windowHeight - BORDER_WIDTH - PADDING - buttonHeight;

    // Close button (centered)
    closeButtonBounds_ = irr::core::recti(
        bankStartX + (bankGridWidth - buttonWidth) / 2, buttonY,
        bankStartX + (bankGridWidth - buttonWidth) / 2 + buttonWidth, buttonY + buttonHeight
    );
}

bool BankWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Right-click not handled here
    if (!leftButton) {
        return false;
    }

    // Check title bar for dragging
    if (titleBarContainsPoint(x, y)) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    // Check close button
    if (closeButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        hide();
        if (closeCallback_) {
            closeCallback_();
        }
        return true;
    }

    // Check conversion buttons
    irr::core::vector2di point(relX, relY);

    // Convert All Chain button (converts cp->sp->gp->pp in sequence)
    if (convertAllChainBounds_.isPointInside(point)) {
        if (currencyConvertCallback_) {
            // Convert all copper first (as many sets of 10 as possible)
            uint32_t copperSets = copper_ / 10;
            if (copperSets > 0) {
                currencyConvertCallback_(0, copperSets * 10);  // COINTYPE_CP = 0
            }
            // Then convert all silver
            uint32_t silverSets = silver_ / 10;
            if (silverSets > 0) {
                currencyConvertCallback_(1, silverSets * 10);  // COINTYPE_SP = 1
            }
            // Then convert all gold
            uint32_t goldSets = gold_ / 10;
            if (goldSets > 0) {
                currencyConvertCallback_(2, goldSets * 10);  // COINTYPE_GP = 2
            }
        }
        return true;
    }

    // Copper conversion buttons (CP -> SP)
    if (copperConvert10Bounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && copper_ >= 10) {
            currencyConvertCallback_(0, 10);  // COINTYPE_CP = 0
        }
        return true;
    }
    if (copperConvertAllBounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && copper_ >= 10) {
            uint32_t amount = (copper_ / 10) * 10;  // Round down to nearest 10
            currencyConvertCallback_(0, amount);
        }
        return true;
    }

    // Silver conversion buttons (SP -> GP)
    if (silverConvert10Bounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && silver_ >= 10) {
            currencyConvertCallback_(1, 10);  // COINTYPE_SP = 1
        }
        return true;
    }
    if (silverConvertAllBounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && silver_ >= 10) {
            uint32_t amount = (silver_ / 10) * 10;
            currencyConvertCallback_(1, amount);
        }
        return true;
    }

    // Gold conversion buttons (GP -> PP)
    if (goldConvert10Bounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && gold_ >= 10) {
            currencyConvertCallback_(2, 10);  // COINTYPE_GP = 2
        }
        return true;
    }
    if (goldConvertAllBounds_.isPointInside(point)) {
        if (currencyConvertCallback_ && gold_ >= 10) {
            uint32_t amount = (gold_ / 10) * 10;
            currencyConvertCallback_(2, amount);
        }
        return true;
    }

    // Check main bank slots
    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        if (bankSlots_[i].containsPoint(relX, relY)) {
            int16_t slotId = bankSlots_[i].getSlotId();
            const inventory::ItemInstance* item = manager_->getItem(slotId);

            // Ctrl+click on readable item = read it
            if (ctrl && item && !item->bookText.empty() && readItemCallback_) {
                readItemCallback_(item->bookText, static_cast<uint8_t>(item->bookType));
                return true;
            }

            // Check if this is a bag and handle accordingly
            if (item && item->isContainer() && !shift && !ctrl) {
                // Single click on bag = open bag window
                if (bagClickCallback_) {
                    bagClickCallback_(slotId);
                }
            } else {
                // Shift+click, Ctrl+click, or non-bag = normal slot click
                if (slotClickCallback_) {
                    slotClickCallback_(slotId, shift, ctrl);
                }
            }
            return true;
        }
    }

    // Check shared bank slots
    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        if (sharedBankSlots_[i].containsPoint(relX, relY)) {
            int16_t slotId = sharedBankSlots_[i].getSlotId();
            const inventory::ItemInstance* item = manager_->getItem(slotId);

            // Ctrl+click on readable item = read it
            if (ctrl && item && !item->bookText.empty() && readItemCallback_) {
                readItemCallback_(item->bookText, static_cast<uint8_t>(item->bookType));
                return true;
            }

            // Check if this is a bag and handle accordingly
            if (item && item->isContainer() && !shift && !ctrl) {
                // Single click on bag = open bag window
                if (bagClickCallback_) {
                    bagClickCallback_(slotId);
                }
            } else {
                // Shift+click, Ctrl+click, or non-bag = normal slot click
                if (slotClickCallback_) {
                    slotClickCallback_(slotId, shift, ctrl);
                }
            }
            return true;
        }
    }

    // Check currency click
    bool currencyFound = false;
    CurrencyType currencyType = getCurrencyAtPosition(relX, relY, currencyFound);
    if (currencyFound) {
        uint32_t maxAmount = 0;
        switch (currencyType) {
            case CurrencyType::Platinum: maxAmount = platinum_; break;
            case CurrencyType::Gold: maxAmount = gold_; break;
            case CurrencyType::Silver: maxAmount = silver_; break;
            case CurrencyType::Copper: maxAmount = copper_; break;
        }
        if (currencyClickCallback_) {
            currencyClickCallback_(currencyType, maxAmount);
        }
        return true;
    }

    return true;  // Consume click even if not on a slot
}

bool BankWindow::handleMouseUp(int x, int y, bool leftButton) {
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool BankWindow::handleMouseMove(int x, int y) {
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (!visible_ || !containsPoint(x, y)) {
        clearHighlights();
        closeButtonHighlighted_ = false;
        // Clear conversion button highlights
        copperConvert10Highlighted_ = false;
        copperConvertAllHighlighted_ = false;
        silverConvert10Highlighted_ = false;
        silverConvertAllHighlighted_ = false;
        goldConvert10Highlighted_ = false;
        goldConvertAllHighlighted_ = false;
        convertAllChainHighlighted_ = false;
        // Don't call slotHoverCallback_ when mouse is outside - let other windows handle it
        // The tooltip will be cleared at the end of WindowManager::handleMouseMove if no window handles it
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;
    irr::core::vector2di point(relX, relY);

    // Check close button
    closeButtonHighlighted_ = closeButtonBounds_.isPointInside(point);

    // Check conversion button highlights
    copperConvert10Highlighted_ = copperConvert10Bounds_.isPointInside(point);
    copperConvertAllHighlighted_ = copperConvertAllBounds_.isPointInside(point);
    silverConvert10Highlighted_ = silverConvert10Bounds_.isPointInside(point);
    silverConvertAllHighlighted_ = silverConvertAllBounds_.isPointInside(point);
    goldConvert10Highlighted_ = goldConvert10Bounds_.isPointInside(point);
    goldConvertAllHighlighted_ = goldConvertAllBounds_.isPointInside(point);
    convertAllChainHighlighted_ = convertAllChainBounds_.isPointInside(point);

    // Find slot under cursor
    int16_t hoveredSlot = inventory::SLOT_INVALID;

    // Check main bank slots
    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        if (bankSlots_[i].containsPoint(relX, relY)) {
            hoveredSlot = bankSlots_[i].getSlotId();
            break;
        }
    }

    // Check shared bank slots if not found
    if (hoveredSlot == inventory::SLOT_INVALID) {
        for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
            if (sharedBankSlots_[i].containsPoint(relX, relY)) {
                hoveredSlot = sharedBankSlots_[i].getSlotId();
                break;
            }
        }
    }

    // Update highlighting
    clearHighlights();
    if (hoveredSlot != inventory::SLOT_INVALID) {
        setHighlightedSlot(hoveredSlot);
    }

    // Notify about hover for tooltip
    if (slotHoverCallback_) {
        slotHoverCallback_(hoveredSlot, x, y);
    }

    return true;
}

int16_t BankWindow::getSlotAtPosition(int x, int y) const {
    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check main bank slots
    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        if (bankSlots_[i].containsPoint(relX, relY)) {
            return bankSlots_[i].getSlotId();
        }
    }

    // Check shared bank slots
    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        if (sharedBankSlots_[i].containsPoint(relX, relY)) {
            return sharedBankSlots_[i].getSlotId();
        }
    }

    return inventory::SLOT_INVALID;
}

void BankWindow::setHighlightedSlot(int16_t slotId) {
    highlightedSlot_ = slotId;

    // Update bank slot highlighting
    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        bankSlots_[i].setHighlighted(bankSlots_[i].getSlotId() == slotId);
    }

    // Update shared bank slot highlighting
    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        sharedBankSlots_[i].setHighlighted(sharedBankSlots_[i].getSlotId() == slotId);
    }
}

void BankWindow::setInvalidDropSlot(int16_t slotId) {
    invalidDropSlot_ = slotId;

    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        bankSlots_[i].setInvalidDrop(bankSlots_[i].getSlotId() == slotId);
    }

    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        sharedBankSlots_[i].setInvalidDrop(sharedBankSlots_[i].getSlotId() == slotId);
    }
}

void BankWindow::clearHighlights() {
    highlightedSlot_ = inventory::SLOT_INVALID;
    invalidDropSlot_ = inventory::SLOT_INVALID;

    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        bankSlots_[i].setHighlighted(false);
        bankSlots_[i].setInvalidDrop(false);
    }

    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        sharedBankSlots_[i].setHighlighted(false);
        sharedBankSlots_[i].setInvalidDrop(false);
    }
}

void BankWindow::renderContent(irr::video::IVideoDriver* driver,
                                irr::gui::IGUIEnvironment* gui) {
    renderBankSlots(driver, gui);
    renderSharedBankSlots(driver, gui);
    renderCurrency(driver, gui);
    renderConversionButtons(driver, gui);
    renderCloseButton(driver, gui);
}

void BankWindow::renderBankSlots(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    for (int i = 0; i < inventory::BANK_COUNT; i++) {
        ItemSlot& slot = bankSlots_[i];
        int16_t slotId = slot.getSlotId();
        const inventory::ItemInstance* item = manager_->getItem(slotId);

        // Get icon texture if available
        irr::video::ITexture* iconTexture = nullptr;
        if (item && iconLookupCallback_) {
            iconTexture = iconLookupCallback_(item->icon);
        }

        // Temporarily offset the slot for rendering
        irr::core::recti originalBounds = slot.getBounds();
        irr::core::recti offsetBounds(
            originalBounds.UpperLeftCorner.X + offsetX,
            originalBounds.UpperLeftCorner.Y + offsetY,
            originalBounds.LowerRightCorner.X + offsetX,
            originalBounds.LowerRightCorner.Y + offsetY
        );
        slot.setBounds(offsetBounds);

        slot.render(driver, gui, item, iconTexture);

        // Restore original bounds
        slot.setBounds(originalBounds);
    }
}

void BankWindow::renderSharedBankSlots(irr::video::IVideoDriver* driver,
                                        irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Draw "Shared Bank" label above the shared bank slots
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (font && inventory::SHARED_BANK_COUNT > 0) {
        // Get position from first shared bank slot
        irr::core::recti firstSlotBounds = sharedBankSlots_[0].getBounds();
        int labelX = firstSlotBounds.UpperLeftCorner.X + offsetX;
        int labelY = firstSlotBounds.UpperLeftCorner.Y + offsetY - 14;

        irr::video::SColor labelColor(255, 200, 200, 200);
        font->draw(L"Shared Bank",
                   irr::core::recti(labelX, labelY, labelX + 100, labelY + 12),
                   labelColor);
    }

    // Render shared bank slots
    for (int i = 0; i < inventory::SHARED_BANK_COUNT; i++) {
        ItemSlot& slot = sharedBankSlots_[i];
        int16_t slotId = slot.getSlotId();
        const inventory::ItemInstance* item = manager_->getItem(slotId);

        // Get icon texture if available
        irr::video::ITexture* iconTexture = nullptr;
        if (item && iconLookupCallback_) {
            iconTexture = iconLookupCallback_(item->icon);
        }

        // Temporarily offset the slot for rendering
        irr::core::recti originalBounds = slot.getBounds();
        irr::core::recti offsetBounds(
            originalBounds.UpperLeftCorner.X + offsetX,
            originalBounds.UpperLeftCorner.Y + offsetY,
            originalBounds.LowerRightCorner.X + offsetX,
            originalBounds.LowerRightCorner.Y + offsetY
        );
        slot.setBounds(offsetBounds);

        slot.render(driver, gui, item, iconTexture);

        // Restore original bounds
        slot.setBounds(originalBounds);
    }
}

void BankWindow::renderCurrency(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    int lineHeight = 10;

    // Currency colors (same as inventory window)
    irr::video::SColor platColor(255, 180, 180, 220);   // Light blue/silver
    irr::video::SColor goldColor(255, 255, 215, 0);     // Gold
    irr::video::SColor silverColor(255, 192, 192, 192); // Silver
    irr::video::SColor copperColor(255, 184, 115, 51);  // Copper

    int x = platinumBounds_.UpperLeftCorner.X + offsetX;
    int y = platinumBounds_.UpperLeftCorner.Y + offsetY;
    int currencyWidth = 70;

    // Platinum
    {
        std::wstringstream ss;
        ss << L"PP " << platinum_;
        font->draw(ss.str().c_str(),
                   irr::core::recti(x, y, x + currencyWidth, y + lineHeight),
                   platColor);
        y += lineHeight;
    }

    // Gold
    {
        std::wstringstream ss;
        ss << L"GP " << gold_;
        font->draw(ss.str().c_str(),
                   irr::core::recti(x, y, x + currencyWidth, y + lineHeight),
                   goldColor);
        y += lineHeight;
    }

    // Silver
    {
        std::wstringstream ss;
        ss << L"SP " << silver_;
        font->draw(ss.str().c_str(),
                   irr::core::recti(x, y, x + currencyWidth, y + lineHeight),
                   silverColor);
        y += lineHeight;
    }

    // Copper
    {
        std::wstringstream ss;
        ss << L"CP " << copper_;
        font->draw(ss.str().c_str(),
                   irr::core::recti(x, y, x + currencyWidth, y + lineHeight),
                   copperColor);
    }
}

void BankWindow::renderConversionButtons(irr::video::IVideoDriver* driver,
                                          irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Button colors
    irr::video::SColor btnBg(255, 60, 60, 80);
    irr::video::SColor btnBgHighlight(255, 80, 80, 120);
    irr::video::SColor btnBgDisabled(255, 40, 40, 50);
    irr::video::SColor btnText(255, 200, 200, 200);
    irr::video::SColor btnTextDisabled(255, 100, 100, 100);

    // Helper lambda to draw a small conversion button
    auto drawSmallButton = [&](const irr::core::recti& bounds, const wchar_t* text,
                               bool highlighted, bool enabled) {
        irr::core::recti screenBounds(
            bounds.UpperLeftCorner.X + offsetX,
            bounds.UpperLeftCorner.Y + offsetY,
            bounds.LowerRightCorner.X + offsetX,
            bounds.LowerRightCorner.Y + offsetY
        );

        irr::video::SColor bgColor = enabled ? (highlighted ? btnBgHighlight : btnBg) : btnBgDisabled;
        irr::video::SColor textColor = enabled ? btnText : btnTextDisabled;

        driver->draw2DRectangle(bgColor, screenBounds);
        font->draw(text, screenBounds, textColor, true, true);
    };

    // Gold conversion buttons (GP -> PP) - only enabled if gold >= 10
    bool goldEnabled = gold_ >= 10;
    drawSmallButton(goldConvert10Bounds_, L"10", goldConvert10Highlighted_, goldEnabled);
    drawSmallButton(goldConvertAllBounds_, L"All", goldConvertAllHighlighted_, goldEnabled);

    // Silver conversion buttons (SP -> GP) - only enabled if silver >= 10
    bool silverEnabled = silver_ >= 10;
    drawSmallButton(silverConvert10Bounds_, L"10", silverConvert10Highlighted_, silverEnabled);
    drawSmallButton(silverConvertAllBounds_, L"All", silverConvertAllHighlighted_, silverEnabled);

    // Copper conversion buttons (CP -> SP) - only enabled if copper >= 10
    bool copperEnabled = copper_ >= 10;
    drawSmallButton(copperConvert10Bounds_, L"10", copperConvert10Highlighted_, copperEnabled);
    drawSmallButton(copperConvertAllBounds_, L"All", copperConvertAllHighlighted_, copperEnabled);

    // Convert All Chain button (on platinum row) - enabled if any currency can be converted
    bool chainEnabled = (copper_ >= 10 || silver_ >= 10 || gold_ >= 10);
    drawSmallButton(convertAllChainBounds_, L"All", convertAllChainHighlighted_, chainEnabled);
}

void BankWindow::renderCloseButton(irr::video::IVideoDriver* driver,
                                    irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    irr::core::recti closeBounds(
        closeButtonBounds_.UpperLeftCorner.X + offsetX,
        closeButtonBounds_.UpperLeftCorner.Y + offsetY,
        closeButtonBounds_.LowerRightCorner.X + offsetX,
        closeButtonBounds_.LowerRightCorner.Y + offsetY
    );

    drawButton(driver, gui, closeBounds, L"Close", closeButtonHighlighted_);
}

CurrencyType BankWindow::getCurrencyAtPosition(int relX, int relY, bool& found) const {
    found = false;
    irr::core::vector2di point(relX, relY);

    if (platinumBounds_.isPointInside(point)) {
        found = true;
        return CurrencyType::Platinum;
    }
    if (goldBounds_.isPointInside(point)) {
        found = true;
        return CurrencyType::Gold;
    }
    if (silverBounds_.isPointInside(point)) {
        found = true;
        return CurrencyType::Silver;
    }
    if (copperBounds_.isPointInside(point)) {
        found = true;
        return CurrencyType::Copper;
    }

    return CurrencyType::Platinum;  // Default, but found will be false
}

} // namespace ui
} // namespace eqt
