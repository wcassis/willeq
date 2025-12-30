#include "client/graphics/ui/trade_window.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/window_manager.h"
#include "client/trade_manager.h"
#include "common/name_utils.h"
#include <algorithm>
#include "common/logging.h"

namespace eqt {
namespace ui {

TradeWindow::TradeWindow(inventory::InventoryManager* invManager, WindowManager* windowManager)
    : WindowBase(L"Trade", 100, 100)  // Size will be calculated
    , inventoryManager_(invManager)
    , windowManager_(windowManager)
{
    calculateLayout();
    initializeSlots();
}

void TradeWindow::calculateLayout() {
    // Layout for player trade:
    // +----------------------------------+
    // |          Trade Window            |
    // +----------------------------------+
    // |  Your Items    | Partner Items   |
    // | [slot][slot]   | [slot][slot]    |
    // | [slot][slot]   | [slot][slot]    |
    // +----------------------------------+
    // | Your Money:    | Partner Money:  |
    // | PP: 0  GP: 0   | PP: 0  GP: 0    |
    // | SP: 0  CP: 0   | SP: 0  CP: 0    |
    // +----------------------------------+
    // | [Accept]       [Cancel]          |
    // +----------------------------------+
    //
    // Layout for NPC trade (simpler - no partner section):
    // +------------------+
    // |  Give to <NPC>   |
    // +------------------+
    // |   Your Items     |
    // | [slot][slot]     |
    // | [slot][slot]     |
    // +------------------+
    // | Your Money:      |
    // | PP: 0  GP: 0     |
    // | SP: 0  CP: 0     |
    // +------------------+
    // | [Give]  [Cancel] |
    // +------------------+

    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int slotsHeight = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_SPACING;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int buttonHeight = getButtonHeight();

    int contentWidth;
    if (isNpcTrade_) {
        // NPC trade: just our items, no partner section
        contentWidth = PADDING + slotsWidth + PADDING;
    } else {
        // Player trade: own slots + divider + partner slots
        contentWidth = PADDING + slotsWidth + PADDING + DIVIDER_WIDTH + PADDING + slotsWidth + PADDING;
    }
    int windowWidth = borderWidth * 2 + contentWidth;

    // Total height: title + label + slots + money section + buttons
    // For NPC trades, buttons are stacked vertically
    int buttonsHeight = isNpcTrade_ ? (buttonHeight * 2 + BUTTON_SPACING) : buttonHeight;
    int windowHeight = borderWidth + titleBarHeight +
                       PADDING + LABEL_HEIGHT +               // "Your Items" label
                       SECTION_SPACING + slotsHeight +        // Item slots
                       SECTION_SPACING + MONEY_ROW_HEIGHT * 2 + // Money rows (PP/GP and SP/CP)
                       SECTION_SPACING + buttonsHeight +      // Accept/Cancel buttons
                       PADDING + borderWidth;

    setSize(windowWidth, windowHeight);
}

void TradeWindow::initializeSlots() {
    ownSlots_.clear();
    partnerSlots_.clear();
    ownSlots_.reserve(TRADE_SLOTS_PER_PLAYER);
    partnerSlots_.reserve(TRADE_SLOTS_PER_PLAYER);

    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    // Own slots start position (left side)
    int ownStartX = borderWidth + PADDING;
    int startY = borderWidth + titleBarHeight + PADDING + LABEL_HEIGHT + SECTION_SPACING;

    for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
        int col = i % COLUMNS;
        int row = i / COLUMNS;

        int x = ownStartX + col * (SLOT_SIZE + SLOT_SPACING);
        int y = startY + row * (SLOT_SIZE + SLOT_SPACING);

        ItemSlot slot(static_cast<int16_t>(i), SlotType::Trade, x, y, SLOT_SIZE);
        ownSlots_.push_back(slot);
    }

    // Partner slots start position (right side after divider)
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int partnerStartX = borderWidth + PADDING + slotsWidth + PADDING + DIVIDER_WIDTH + PADDING;

    for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
        int col = i % COLUMNS;
        int row = i / COLUMNS;

        int x = partnerStartX + col * (SLOT_SIZE + SLOT_SPACING);
        int y = startY + row * (SLOT_SIZE + SLOT_SPACING);

        ItemSlot slot(static_cast<int16_t>(i + TRADE_SLOTS_PER_PLAYER), SlotType::Trade, x, y, SLOT_SIZE);
        partnerSlots_.push_back(slot);
    }
}

void TradeWindow::open(uint32_t partnerSpawnId, const std::string& partnerName, bool isNpcTrade) {
    partnerSpawnId_ = partnerSpawnId;
    partnerName_ = partnerName;
    isNpcTrade_ = isNpcTrade;
    isOpen_ = true;

    // Reset state
    ownMoney_ = Money{};
    partnerMoney_ = Money{};
    ownAccepted_ = false;
    partnerAccepted_ = false;
    for (auto& item : partnerItems_) {
        item.reset();
    }

    // Recalculate layout for NPC vs player trade
    calculateLayout();
    initializeSlots();

    // Set title based on trade type (use display name for readability)
    std::wstring title;
    if (isNpcTrade_) {
        title = L"Give to ";
    } else {
        title = L"Trade with ";
    }
    title += EQT::toDisplayNameW(partnerName);
    setTitle(title);

    show();
    LOG_INFO(MOD_UI, "Trade window opened with {} (isNpc={})", partnerName, isNpcTrade);
}

void TradeWindow::close(bool sendCancel) {
    if (!isOpen_) {
        return;
    }

    isOpen_ = false;
    partnerSpawnId_ = 0;
    partnerName_.clear();

    // Clear partner items
    for (auto& item : partnerItems_) {
        item.reset();
    }

    // Clear our trade slots in inventory manager
    // (items are either returned to inventory on cancel, or moved to partner on complete)
    if (inventoryManager_) {
        inventoryManager_->clearTradeSlots();
    }

    // Reset money
    ownMoney_ = Money{};
    partnerMoney_ = Money{};

    // Reset accept state
    ownAccepted_ = false;
    partnerAccepted_ = false;

    hide();
    LOG_INFO(MOD_UI, "Trade window closed");

    // Only trigger cancel callback if requested (not on trade completion)
    if (sendCancel && onCancel_) {
        onCancel_();
    }
}

void TradeWindow::updateOwnSlot(int slot, const inventory::ItemInstance* item) {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return;
    }
    // Own items are displayed from trade slots in inventory manager
    // This just triggers a refresh
    LOG_DEBUG(MOD_UI, "Trade own slot {} updated", slot);
}

void TradeWindow::clearOwnSlot(int slot) {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return;
    }
    LOG_DEBUG(MOD_UI, "Trade own slot {} cleared", slot);
}

void TradeWindow::updatePartnerSlot(int slot, const inventory::ItemInstance* item) {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return;
    }
    // Partner items are stored locally (copies from server)
    // This method is for updating display only
    LOG_DEBUG(MOD_UI, "Trade partner slot {} updated", slot);
}

void TradeWindow::clearPartnerSlot(int slot) {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return;
    }
    partnerItems_[slot].reset();
    LOG_DEBUG(MOD_UI, "Trade partner slot {} cleared", slot);
}

void TradeWindow::setPartnerItem(int slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return;
    }
    partnerItems_[slot] = std::move(item);
    LOG_DEBUG(MOD_UI, "Trade partner slot {} set", slot);
}

void TradeWindow::setOwnMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp) {
    ownMoney_.platinum = pp;
    ownMoney_.gold = gp;
    ownMoney_.silver = sp;
    ownMoney_.copper = cp;
}

void TradeWindow::setPartnerMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp) {
    partnerMoney_.platinum = pp;
    partnerMoney_.gold = gp;
    partnerMoney_.silver = sp;
    partnerMoney_.copper = cp;
}

void TradeWindow::setOwnAccepted(bool accepted) {
    ownAccepted_ = accepted;
}

void TradeWindow::setPartnerAccepted(bool accepted) {
    partnerAccepted_ = accepted;
}

const inventory::ItemInstance* TradeWindow::getDisplayedItem(int slot, bool isOwn) const {
    if (slot < 0 || slot >= TRADE_SLOTS_PER_PLAYER) {
        return nullptr;
    }

    if (isOwn) {
        // Own items come from trade manager via inventory
        if (tradeManager_ && inventoryManager_) {
            return inventoryManager_->getItem(inventory::TRADE_BEGIN + slot);
        }
        return nullptr;
    } else {
        // Partner items are stored locally
        return partnerItems_[slot].get();
    }
}

bool TradeWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check title bar for dragging (only if not locked)
    if (titleBarContainsPoint(x, y)) {
        if (canMove()) {
            dragging_ = true;
            dragOffset_.X = x - bounds_.UpperLeftCorner.X;
            dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        }
        return true;  // Consume click on title bar even if locked
    }

    // Check buttons
    ButtonId button = getButtonAtPosition(x, y);
    if (button != ButtonId::None) {
        switch (button) {
            case ButtonId::Accept:
                LOG_DEBUG(MOD_UI, "Trade Accept button clicked");
                if (onAccept_) {
                    onAccept_();
                }
                break;
            case ButtonId::Cancel:
                LOG_DEBUG(MOD_UI, "Trade Cancel button clicked");
                close();
                break;
            default:
                break;
        }
        return true;
    }

    // Check own money area - clicking here with cursor money adds it to trade
    if (isInOwnMoneyArea(x, y)) {
        LOG_DEBUG(MOD_UI, "Trade own money area clicked");
        if (moneyAreaClickCallback_) {
            moneyAreaClickCallback_();
        }
        return true;
    }

    // Check slots
    bool isOwnSlot = false;
    int16_t clickedSlot = getSlotAtPosition(x, y, isOwnSlot);
    if (clickedSlot != inventory::SLOT_INVALID) {
        if (isOwnSlot) {
            // Clicking own slot - handle item pickup/place
            // Convert to actual trade slot ID (3000-3003)
            int16_t tradeSlotId = inventory::TRADE_BEGIN + clickedSlot;
            LOG_DEBUG(MOD_UI, "Trade own slot {} clicked (inventory slot {})", clickedSlot, tradeSlotId);
            if (slotClickCallback_) {
                slotClickCallback_(tradeSlotId, shift, ctrl);
            }
        } else {
            // Clicking partner slot - view only (tooltip handled via hover)
            LOG_DEBUG(MOD_UI, "Trade partner slot {} clicked (view only)", clickedSlot);
        }
        return true;
    }

    return true;  // Consume click even if not on a slot
}

bool TradeWindow::handleMouseUp(int x, int y, bool leftButton) {
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool TradeWindow::handleMouseMove(int x, int y) {
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (!visible_ || !containsPoint(x, y)) {
        clearHighlights();
        hoveredButton_ = ButtonId::None;
        return false;
    }

    // Check buttons
    hoveredButton_ = getButtonAtPosition(x, y);

    // Check slots
    bool isOwnSlot = false;
    int16_t hoveredSlot = getSlotAtPosition(x, y, isOwnSlot);
    clearHighlights();
    if (hoveredSlot != inventory::SLOT_INVALID) {
        setHighlightedSlot(hoveredSlot, isOwnSlot);
    }

    return true;
}

int16_t TradeWindow::getSlotAtPosition(int x, int y, bool& isOwnSlot) const {
    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check own slots
    for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
        if (i < static_cast<int>(ownSlots_.size()) && ownSlots_[i].containsPoint(relX, relY)) {
            isOwnSlot = true;
            return static_cast<int16_t>(i);
        }
    }

    // Check partner slots
    for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
        if (i < static_cast<int>(partnerSlots_.size()) && partnerSlots_[i].containsPoint(relX, relY)) {
            isOwnSlot = false;
            return static_cast<int16_t>(i);
        }
    }

    isOwnSlot = true;
    return inventory::SLOT_INVALID;
}

void TradeWindow::setHighlightedSlot(int16_t slotId, bool isOwn) {
    highlightedSlot_ = slotId;
    highlightedSlotIsOwn_ = isOwn;

    // Clear all highlights first
    for (auto& slot : ownSlots_) {
        slot.setHighlighted(false);
    }
    for (auto& slot : partnerSlots_) {
        slot.setHighlighted(false);
    }

    // Set the highlighted slot
    if (slotId >= 0 && slotId < TRADE_SLOTS_PER_PLAYER) {
        if (isOwn && slotId < static_cast<int16_t>(ownSlots_.size())) {
            ownSlots_[slotId].setHighlighted(true);
        } else if (!isOwn && slotId < static_cast<int16_t>(partnerSlots_.size())) {
            partnerSlots_[slotId].setHighlighted(true);
        }
    }
}

void TradeWindow::clearHighlights() {
    highlightedSlot_ = inventory::SLOT_INVALID;

    for (auto& slot : ownSlots_) {
        slot.setHighlighted(false);
    }
    for (auto& slot : partnerSlots_) {
        slot.setHighlighted(false);
    }
}

TradeWindow::ButtonId TradeWindow::getButtonAtPosition(int x, int y) const {
    for (int i = 0; i <= static_cast<int>(ButtonId::Cancel); i++) {
        ButtonId btn = static_cast<ButtonId>(i);
        irr::core::recti bounds = getButtonBounds(btn);
        if (bounds.isPointInside(irr::core::vector2di(x, y))) {
            return btn;
        }
    }
    return ButtonId::None;
}

irr::core::recti TradeWindow::getButtonBounds(ButtonId button) const {
    int windowX = bounds_.UpperLeftCorner.X;
    int borderWidth = getBorderWidth();
    int buttonHeight = getButtonHeight();

    if (isNpcTrade_) {
        // NPC trades: buttons stacked vertically, centered horizontally
        int buttonX = windowX + (bounds_.getWidth() - BUTTON_WIDTH) / 2;
        int bottomY = bounds_.LowerRightCorner.Y - borderWidth - PADDING;

        switch (button) {
            case ButtonId::Accept: {
                // Give button on top
                int y = bottomY - buttonHeight * 2 - BUTTON_SPACING;
                return irr::core::recti(buttonX, y, buttonX + BUTTON_WIDTH, y + buttonHeight);
            }
            case ButtonId::Cancel: {
                // Cancel button below
                int y = bottomY - buttonHeight;
                return irr::core::recti(buttonX, y, buttonX + BUTTON_WIDTH, y + buttonHeight);
            }
            default:
                return irr::core::recti();
        }
    } else {
        // Player trades: buttons side by side
        int buttonY = bounds_.LowerRightCorner.Y - borderWidth - PADDING - buttonHeight;
        int totalButtonWidth = BUTTON_WIDTH * 2 + BUTTON_SPACING;
        int startX = windowX + (bounds_.getWidth() - totalButtonWidth) / 2;

        switch (button) {
            case ButtonId::Accept: {
                int x = startX;
                return irr::core::recti(x, buttonY, x + BUTTON_WIDTH, buttonY + buttonHeight);
            }
            case ButtonId::Cancel: {
                int x = startX + BUTTON_WIDTH + BUTTON_SPACING;
                return irr::core::recti(x, buttonY, x + BUTTON_WIDTH, buttonY + buttonHeight);
            }
            default:
                return irr::core::recti();
        }
    }
}

irr::core::recti TradeWindow::getOwnMoneyAreaBounds() const {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int slotsHeight = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_SPACING;

    // Money section starts after slots
    int moneyY = offsetY + borderWidth + titleBarHeight + PADDING + LABEL_HEIGHT +
                 SECTION_SPACING + slotsHeight + SECTION_SPACING;
    int ownMoneyX = offsetX + borderWidth + PADDING;

    return irr::core::recti(
        ownMoneyX,
        moneyY,
        ownMoneyX + slotsWidth,
        moneyY + MONEY_ROW_HEIGHT * 2
    );
}

bool TradeWindow::isInOwnMoneyArea(int x, int y) const {
    irr::core::recti bounds = getOwnMoneyAreaBounds();
    return bounds.isPointInside(irr::core::vector2di(x, y));
}

void TradeWindow::renderContent(irr::video::IVideoDriver* driver,
                                irr::gui::IGUIEnvironment* gui) {
    renderAcceptHighlights(driver);
    if (!isNpcTrade_) {
        renderDivider(driver);
    }
    renderSlots(driver, gui);
    renderMoney(driver, gui);
    renderButtons(driver, gui);
}

void TradeWindow::renderAcceptHighlights(irr::video::IVideoDriver* driver) {
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int buttonHeight = getButtonHeight();

    // Calculate section dimensions - start below the labels
    int sectionTop = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + PADDING + LABEL_HEIGHT + SECTION_SPACING - 4;
    int sectionBottom = bounds_.LowerRightCorner.Y - borderWidth - PADDING - buttonHeight - SECTION_SPACING;

    // Green color for accept highlight (semi-transparent for the fill, solid for border)
    irr::video::SColor greenBorder(255, 0, 200, 0);
    irr::video::SColor greenFill(30, 0, 255, 0);  // Very transparent green tint
    const int highlightBorderWidth = 2;

    // Own section (left side) - highlight when we accept
    if (ownAccepted_) {
        int ownLeft = bounds_.UpperLeftCorner.X + borderWidth;
        int ownRight = bounds_.UpperLeftCorner.X + borderWidth + PADDING + slotsWidth + PADDING;

        // Draw subtle green tint over our section
        driver->draw2DRectangle(greenFill,
            irr::core::recti(ownLeft, sectionTop, ownRight, sectionBottom));

        // Draw green border around our section
        // Top
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(ownLeft, sectionTop, ownRight, sectionTop + highlightBorderWidth));
        // Bottom
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(ownLeft, sectionBottom - highlightBorderWidth, ownRight, sectionBottom));
        // Left
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(ownLeft, sectionTop, ownLeft + highlightBorderWidth, sectionBottom));
        // Right
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(ownRight - highlightBorderWidth, sectionTop, ownRight, sectionBottom));
    }

    // Partner section (right side) - highlight when partner accepts (player trade only)
    if (!isNpcTrade_ && partnerAccepted_) {
        int partnerLeft = bounds_.UpperLeftCorner.X + borderWidth + PADDING + slotsWidth + PADDING + DIVIDER_WIDTH;
        int partnerRight = bounds_.LowerRightCorner.X - borderWidth;

        // Draw subtle green tint over partner's section
        driver->draw2DRectangle(greenFill,
            irr::core::recti(partnerLeft, sectionTop, partnerRight, sectionBottom));

        // Draw green border around partner's section
        // Top
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(partnerLeft, sectionTop, partnerRight, sectionTop + highlightBorderWidth));
        // Bottom
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(partnerLeft, sectionBottom - highlightBorderWidth, partnerRight, sectionBottom));
        // Left
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(partnerLeft, sectionTop, partnerLeft + highlightBorderWidth, sectionBottom));
        // Right
        driver->draw2DRectangle(greenBorder,
            irr::core::recti(partnerRight - highlightBorderWidth, sectionTop, partnerRight, sectionBottom));
    }
}

void TradeWindow::renderDivider(irr::video::IVideoDriver* driver) {
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    // Divider position (between own and partner sections)
    int dividerX = bounds_.UpperLeftCorner.X + borderWidth + PADDING + slotsWidth + PADDING;
    int dividerY1 = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + PADDING;
    int dividerY2 = bounds_.LowerRightCorner.Y - borderWidth - PADDING;

    // Draw divider line
    irr::video::SColor dividerColor = getBorderDarkColor();
    driver->draw2DRectangle(dividerColor,
        irr::core::recti(dividerX, dividerY1, dividerX + DIVIDER_WIDTH, dividerY2));
}

void TradeWindow::renderSlots(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Render section labels
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int labelY = offsetY + borderWidth + titleBarHeight + PADDING;

    // "Your Items" label
    int ownLabelX = offsetX + borderWidth + PADDING;
    irr::core::recti ownLabelRect(ownLabelX, labelY,
                                   ownLabelX + COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING,
                                   labelY + LABEL_HEIGHT);
    gui->getBuiltInFont()->draw(L"Your Items", ownLabelRect,
                                irr::video::SColor(255, 200, 200, 200),
                                false, false, &ownLabelRect);

    // "Partner Items" label (player trade only)
    if (!isNpcTrade_) {
        int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
        int partnerLabelX = offsetX + borderWidth + PADDING + slotsWidth + PADDING + DIVIDER_WIDTH + PADDING;
        irr::core::recti partnerLabelRect(partnerLabelX, labelY,
                                           partnerLabelX + slotsWidth,
                                           labelY + LABEL_HEIGHT);
        // Use partner name if available
        std::wstring partnerLabel = partnerName_.empty() ? L"Partner Items" :
            std::wstring(partnerName_.begin(), partnerName_.end()) + L"'s Items";
        gui->getBuiltInFont()->draw(partnerLabel.c_str(), partnerLabelRect,
                                    irr::video::SColor(255, 200, 200, 200),
                                    false, false, &partnerLabelRect);
    }

    // Render own slots
    for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
        if (i >= static_cast<int>(ownSlots_.size())) break;

        auto& slot = ownSlots_[i];
        const inventory::ItemInstance* item = getDisplayedItem(i, true);

        irr::video::ITexture* iconTexture = nullptr;
        if (item && iconLookupCallback_) {
            iconTexture = iconLookupCallback_(item->icon);
        }

        // Offset slot for rendering
        irr::core::recti originalBounds = slot.getBounds();
        irr::core::recti offsetBounds(
            originalBounds.UpperLeftCorner.X + offsetX,
            originalBounds.UpperLeftCorner.Y + offsetY,
            originalBounds.LowerRightCorner.X + offsetX,
            originalBounds.LowerRightCorner.Y + offsetY
        );
        slot.setBounds(offsetBounds);
        slot.render(driver, gui, item, iconTexture);
        slot.setBounds(originalBounds);
    }

    // Render partner slots (player trade only)
    if (!isNpcTrade_) {
        for (int i = 0; i < TRADE_SLOTS_PER_PLAYER; i++) {
            if (i >= static_cast<int>(partnerSlots_.size())) break;

            auto& slot = partnerSlots_[i];
            const inventory::ItemInstance* item = getDisplayedItem(i, false);

            irr::video::ITexture* iconTexture = nullptr;
            if (item && iconLookupCallback_) {
                iconTexture = iconLookupCallback_(item->icon);
            }

            // Offset slot for rendering
            irr::core::recti originalBounds = slot.getBounds();
            irr::core::recti offsetBounds(
                originalBounds.UpperLeftCorner.X + offsetX,
                originalBounds.UpperLeftCorner.Y + offsetY,
                originalBounds.LowerRightCorner.X + offsetX,
                originalBounds.LowerRightCorner.Y + offsetY
            );
            slot.setBounds(offsetBounds);
            slot.render(driver, gui, item, iconTexture);
            slot.setBounds(originalBounds);
        }
    }
}

void TradeWindow::renderMoney(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int slotsHeight = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_SPACING;

    // Money section starts after slots
    int moneyY = offsetY + borderWidth + titleBarHeight + PADDING + LABEL_HEIGHT +
                 SECTION_SPACING + slotsHeight + SECTION_SPACING;

    irr::video::SColor textColor(255, 200, 200, 200);
    irr::video::SColor coinPP(255, 220, 220, 255);  // Platinum - light blue
    irr::video::SColor coinGP(255, 255, 215, 0);    // Gold - gold
    irr::video::SColor coinSP(255, 192, 192, 192);  // Silver - silver
    irr::video::SColor coinCP(255, 184, 115, 51);   // Copper - copper

    // Own money (left side)
    int ownMoneyX = offsetX + borderWidth + PADDING;

    // Row 1: PP and GP
    wchar_t moneyBuf[64];
    swprintf(moneyBuf, 64, L"PP: %u  GP: %u", ownMoney_.platinum, ownMoney_.gold);
    irr::core::recti ownRow1(ownMoneyX, moneyY, ownMoneyX + slotsWidth, moneyY + MONEY_ROW_HEIGHT);
    gui->getBuiltInFont()->draw(moneyBuf, ownRow1, textColor, false, false, &ownRow1);

    // Row 2: SP and CP
    swprintf(moneyBuf, 64, L"SP: %u  CP: %u", ownMoney_.silver, ownMoney_.copper);
    irr::core::recti ownRow2(ownMoneyX, moneyY + MONEY_ROW_HEIGHT,
                              ownMoneyX + slotsWidth, moneyY + MONEY_ROW_HEIGHT * 2);
    gui->getBuiltInFont()->draw(moneyBuf, ownRow2, textColor, false, false, &ownRow2);

    // Partner money (right side, player trade only)
    if (!isNpcTrade_) {
        int partnerMoneyX = offsetX + borderWidth + PADDING + slotsWidth + PADDING + DIVIDER_WIDTH + PADDING;

        // Row 1: PP and GP
        swprintf(moneyBuf, 64, L"PP: %u  GP: %u", partnerMoney_.platinum, partnerMoney_.gold);
        irr::core::recti partnerRow1(partnerMoneyX, moneyY, partnerMoneyX + slotsWidth, moneyY + MONEY_ROW_HEIGHT);
        gui->getBuiltInFont()->draw(moneyBuf, partnerRow1, textColor, false, false, &partnerRow1);

        // Row 2: SP and CP
        swprintf(moneyBuf, 64, L"SP: %u  CP: %u", partnerMoney_.silver, partnerMoney_.copper);
        irr::core::recti partnerRow2(partnerMoneyX, moneyY + MONEY_ROW_HEIGHT,
                                      partnerMoneyX + slotsWidth, moneyY + MONEY_ROW_HEIGHT * 2);
        gui->getBuiltInFont()->draw(moneyBuf, partnerRow2, textColor, false, false, &partnerRow2);
    }
}

void TradeWindow::renderButtons(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
    // Accept/Give button - show different color if accepted
    bool acceptHighlighted = (hoveredButton_ == ButtonId::Accept);
    irr::core::recti acceptBounds = getButtonBounds(ButtonId::Accept);

    // Use "Give" for NPC trades, "Accept" for player trades
    const wchar_t* buttonLabel = isNpcTrade_ ? L"Give" : L"Accept";
    const wchar_t* acceptedLabel = isNpcTrade_ ? L"Given" : L"Accepted";

    if (ownAccepted_) {
        // Green tint when accepted
        irr::video::SColor acceptedColor(255, 50, 150, 50);
        driver->draw2DRectangle(acceptedColor, acceptBounds);
        // Border
        driver->draw2DRectangle(getBorderLightColor(),
            irr::core::recti(acceptBounds.UpperLeftCorner.X, acceptBounds.UpperLeftCorner.Y,
                            acceptBounds.LowerRightCorner.X, acceptBounds.UpperLeftCorner.Y + 1));
        driver->draw2DRectangle(getBorderLightColor(),
            irr::core::recti(acceptBounds.UpperLeftCorner.X, acceptBounds.UpperLeftCorner.Y,
                            acceptBounds.UpperLeftCorner.X + 1, acceptBounds.LowerRightCorner.Y));
        driver->draw2DRectangle(getBorderDarkColor(),
            irr::core::recti(acceptBounds.UpperLeftCorner.X, acceptBounds.LowerRightCorner.Y - 1,
                            acceptBounds.LowerRightCorner.X, acceptBounds.LowerRightCorner.Y));
        driver->draw2DRectangle(getBorderDarkColor(),
            irr::core::recti(acceptBounds.LowerRightCorner.X - 1, acceptBounds.UpperLeftCorner.Y,
                            acceptBounds.LowerRightCorner.X, acceptBounds.LowerRightCorner.Y));
        // Text
        gui->getBuiltInFont()->draw(acceptedLabel, acceptBounds,
                                    irr::video::SColor(255, 255, 255, 255), true, true, &acceptBounds);
    } else {
        drawButton(driver, gui, acceptBounds, buttonLabel, acceptHighlighted);
    }

    // Cancel button
    bool cancelHighlighted = (hoveredButton_ == ButtonId::Cancel);
    irr::core::recti cancelBounds = getButtonBounds(ButtonId::Cancel);
    drawButton(driver, gui, cancelBounds, L"Cancel", cancelHighlighted);
}

} // namespace ui
} // namespace eqt
