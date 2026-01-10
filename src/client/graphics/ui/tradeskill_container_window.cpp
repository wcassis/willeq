#include "client/graphics/ui/tradeskill_container_window.h"
#include "client/graphics/ui/inventory_manager.h"
#include "common/logging.h"
#include <sstream>

namespace eqt {
namespace ui {

TradeskillContainerWindow::TradeskillContainerWindow(inventory::InventoryManager* manager)
    : WindowBase(L"Tradeskill Container", 100, 100)  // Size will be calculated
    , manager_(manager)
{
    // Initial layout with default 10 slots
    slotCount_ = 10;
    calculateLayout();
    initializeSlots();
}

void TradeskillContainerWindow::openForWorldObject(uint32_t dropId, const std::string& name,
                                                   uint8_t objectType, int slotCount)
{
    LOG_DEBUG(MOD_INVENTORY, "Opening tradeskill window for world object: dropId={} name='{}' type={} slots={}",
        dropId, name, objectType, slotCount);

    isOpen_ = true;
    isWorldContainer_ = true;
    worldObjectId_ = dropId;
    containerSlot_ = inventory::SLOT_INVALID;
    containerType_ = objectType;
    containerName_ = name;
    slotCount_ = slotCount;

    // Clear any cached items from previous session
    worldContainerItems_.clear();

    // Build title with tradeskill name
    std::wstring title(name.begin(), name.end());
    const char* tradeskillName = inventory::getTradeskillContainerName(objectType);
    if (tradeskillName && std::string(tradeskillName) != "Unknown Container") {
        title = std::wstring(tradeskillName, tradeskillName + strlen(tradeskillName));
    }
    setTitle(title);

    calculateLayout();
    initializeSlots();
    show();
}

void TradeskillContainerWindow::openForInventoryItem(int16_t containerSlot, const std::string& name,
                                                     uint8_t bagType, int slotCount)
{
    LOG_DEBUG(MOD_INVENTORY, "Opening tradeskill window for inventory item: slot={} name='{}' type={} slots={}",
        containerSlot, name, bagType, slotCount);

    isOpen_ = true;
    isWorldContainer_ = false;
    worldObjectId_ = 0;
    containerSlot_ = containerSlot;
    containerType_ = bagType;
    containerName_ = name;
    slotCount_ = slotCount;

    // For inventory items, use the item name as the title
    std::wstring title(name.begin(), name.end());
    setTitle(title);

    calculateLayout();
    initializeSlots();
    show();
}

void TradeskillContainerWindow::close()
{
    if (!isOpen_) return;

    LOG_DEBUG(MOD_INVENTORY, "Closing tradeskill container window");

    isOpen_ = false;
    worldContainerItems_.clear();
    clearHighlights();
    hide();

    if (closeCallback_) {
        closeCallback_();
    }
}

void TradeskillContainerWindow::calculateLayout()
{
    // Always use 2 columns, expand rows as needed
    columns_ = 2;
    rows_ = (slotCount_ + 1) / 2;  // Ceiling division

    // Calculate content dimensions
    int slotsWidth = columns_ * SLOT_SIZE + (columns_ - 1) * SLOT_SPACING;
    int slotsHeight = rows_ * SLOT_SIZE + (rows_ - 1) * SLOT_SPACING;

    // Window dimensions
    int contentWidth = PADDING + slotsWidth + PADDING;
    int contentHeight = PADDING + slotsHeight + COMBINE_BUTTON_MARGIN + COMBINE_BUTTON_HEIGHT + PADDING;

    int windowWidth = contentWidth + BORDER_WIDTH * 2;
    int windowHeight = contentHeight + BORDER_WIDTH * 2 + TITLE_BAR_HEIGHT;

    setSize(windowWidth, windowHeight);
}

void TradeskillContainerWindow::initializeSlots()
{
    slots_.clear();
    slots_.reserve(slotCount_);

    int startX = BORDER_WIDTH + PADDING;
    int startY = BORDER_WIDTH + TITLE_BAR_HEIGHT + PADDING;

    for (int i = 0; i < slotCount_; i++) {
        int col = i % columns_;
        int row = i / columns_;

        int x = startX + col * (SLOT_SIZE + SLOT_SPACING);
        int y = startY + row * (SLOT_SIZE + SLOT_SPACING);

        // Calculate the actual slot ID for this container slot
        int16_t slotId = calculateSlotId(i);

        ItemSlot slot(slotId, SlotType::Bag, x, y, SLOT_SIZE);
        slots_.push_back(slot);
    }
}

int16_t TradeskillContainerWindow::calculateSlotId(int index) const
{
    if (isWorldContainer_) {
        // World container slots are in the WORLD range
        return inventory::WORLD_BEGIN + static_cast<int16_t>(index);
    } else {
        // Inventory container slots use the bag slot calculation
        return inventory::calcBagSlotId(containerSlot_, static_cast<int16_t>(index));
    }
}

const inventory::ItemInstance* TradeskillContainerWindow::getContainerItem(int16_t slotId) const
{
    if (isWorldContainer_) {
        // For world containers, check our cached items first, then inventory manager
        auto it = worldContainerItems_.find(slotId);
        if (it != worldContainerItems_.end()) {
            return it->second.get();
        }
        // Also check inventory manager for items placed there
        return manager_->getItem(slotId);
    } else {
        // For inventory containers, items are in the inventory manager
        return manager_->getItem(slotId);
    }
}

int16_t TradeskillContainerWindow::getSlotAtPosition(int x, int y) const
{
    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    for (const auto& slot : slots_) {
        if (slot.containsPoint(relX, relY)) {
            return slot.getSlotId();
        }
    }

    return inventory::SLOT_INVALID;
}

void TradeskillContainerWindow::setHighlightedSlot(int16_t slotId)
{
    highlightedSlot_ = slotId;

    for (auto& slot : slots_) {
        slot.setHighlighted(slot.getSlotId() == slotId);
    }
}

void TradeskillContainerWindow::setInvalidDropSlot(int16_t slotId)
{
    invalidDropSlot_ = slotId;

    for (auto& slot : slots_) {
        slot.setInvalidDrop(slot.getSlotId() == slotId);
    }
}

void TradeskillContainerWindow::clearHighlights()
{
    highlightedSlot_ = inventory::SLOT_INVALID;
    invalidDropSlot_ = inventory::SLOT_INVALID;
    combineButtonHovered_ = false;

    for (auto& slot : slots_) {
        slot.setHighlighted(false);
        slot.setInvalidDrop(false);
    }
}

bool TradeskillContainerWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_ || !isOpen_) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Right-click closes the window
    if (!leftButton) {
        close();
        return true;
    }

    // Check close button first (before title bar drag)
    if (isCloseButtonHit(x, y)) {
        close();
        return true;
    }

    // Check title bar for dragging
    if (titleBarContainsPoint(x, y)) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    // Check Combine button
    if (isCombineButtonHit(relX, relY)) {
        LOG_DEBUG(MOD_INVENTORY, "Combine button clicked");
        if (combineCallback_) {
            combineCallback_();
        }
        return true;
    }

    // Check slots
    for (const auto& slot : slots_) {
        if (slot.containsPoint(relX, relY)) {
            int16_t slotId = slot.getSlotId();
            LOG_TRACE(MOD_INVENTORY, "Tradeskill slot clicked: {}", slotId);
            if (slotClickCallback_) {
                slotClickCallback_(slotId, shift, ctrl);
            }
            return true;
        }
    }

    return true;  // Consume click even if not on a specific element
}

bool TradeskillContainerWindow::handleMouseUp(int x, int y, bool leftButton)
{
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool TradeskillContainerWindow::handleMouseMove(int x, int y)
{
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (!visible_ || !isOpen_ || !containsPoint(x, y)) {
        clearHighlights();
        if (slotHoverCallback_) {
            slotHoverCallback_(inventory::SLOT_INVALID, x, y);
        }
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check button hover states
    closeButtonHovered_ = isCloseButtonHit(x, y);
    combineButtonHovered_ = isCombineButtonHit(relX, relY);

    // Find slot under cursor
    int16_t hoveredSlot = inventory::SLOT_INVALID;
    for (const auto& slot : slots_) {
        if (slot.containsPoint(relX, relY)) {
            hoveredSlot = slot.getSlotId();
            break;
        }
    }

    // Update slot highlighting
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

bool TradeskillContainerWindow::isCombineButtonHit(int relX, int relY) const
{
    irr::core::recti buttonBounds = getCombineButtonBounds();
    return buttonBounds.isPointInside(irr::core::vector2di(relX, relY));
}

irr::core::recti TradeskillContainerWindow::getCombineButtonBounds() const
{
    // Combine button is centered at the bottom
    int slotsWidth = columns_ * SLOT_SIZE + (columns_ - 1) * SLOT_SPACING;
    int slotsHeight = rows_ * SLOT_SIZE + (rows_ - 1) * SLOT_SPACING;

    int buttonWidth = slotsWidth;  // Full width of slots area
    int buttonX = BORDER_WIDTH + PADDING;
    int buttonY = BORDER_WIDTH + TITLE_BAR_HEIGHT + PADDING + slotsHeight + COMBINE_BUTTON_MARGIN;

    return irr::core::recti(buttonX, buttonY,
                            buttonX + buttonWidth, buttonY + COMBINE_BUTTON_HEIGHT);
}

void TradeskillContainerWindow::renderContent(irr::video::IVideoDriver* driver,
                                               irr::gui::IGUIEnvironment* gui)
{
    renderCloseButton(driver, gui);
    renderSlots(driver, gui);
    renderCombineButton(driver, gui);
}

void TradeskillContainerWindow::renderSlots(irr::video::IVideoDriver* driver,
                                            irr::gui::IGUIEnvironment* gui)
{
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    for (auto& slot : slots_) {
        int16_t slotId = slot.getSlotId();
        const inventory::ItemInstance* item = getContainerItem(slotId);

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

void TradeskillContainerWindow::renderCombineButton(irr::video::IVideoDriver* driver,
                                                     irr::gui::IGUIEnvironment* gui)
{
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    irr::core::recti buttonBounds = getCombineButtonBounds();

    // Offset for screen position
    irr::core::recti screenBounds(
        buttonBounds.UpperLeftCorner.X + offsetX,
        buttonBounds.UpperLeftCorner.Y + offsetY,
        buttonBounds.LowerRightCorner.X + offsetX,
        buttonBounds.LowerRightCorner.Y + offsetY
    );

    // Use the base class drawButton helper
    drawButton(driver, gui, screenBounds, L"Combine", combineButtonHovered_);
}

irr::core::recti TradeskillContainerWindow::getCloseButtonBounds() const
{
    // Close button is in the right side of the title bar
    // Square button the same height as title bar minus some padding
    int buttonSize = TITLE_BAR_HEIGHT - 4;
    int buttonX = bounds_.getWidth() - BORDER_WIDTH - buttonSize - 2;
    int buttonY = BORDER_WIDTH + 2;

    return irr::core::recti(buttonX, buttonY, buttonX + buttonSize, buttonY + buttonSize);
}

bool TradeskillContainerWindow::isCloseButtonHit(int x, int y) const
{
    // x, y are screen coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    irr::core::recti closeBtn = getCloseButtonBounds();
    return relX >= closeBtn.UpperLeftCorner.X && relX < closeBtn.LowerRightCorner.X &&
           relY >= closeBtn.UpperLeftCorner.Y && relY < closeBtn.LowerRightCorner.Y;
}

void TradeskillContainerWindow::renderCloseButton(irr::video::IVideoDriver* driver,
                                                   irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti buttonBounds = getCloseButtonBounds();

    // Offset for screen position
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    irr::core::recti screenBounds(
        buttonBounds.UpperLeftCorner.X + offsetX,
        buttonBounds.UpperLeftCorner.Y + offsetY,
        buttonBounds.LowerRightCorner.X + offsetX,
        buttonBounds.LowerRightCorner.Y + offsetY
    );

    // Draw button background
    irr::video::SColor bgColor = closeButtonHovered_ ?
        irr::video::SColor(255, 200, 80, 80) :   // Red-ish when hovered
        irr::video::SColor(255, 100, 100, 100);  // Gray normally
    driver->draw2DRectangle(bgColor, screenBounds);

    // Draw X
    irr::video::SColor xColor(255, 255, 255, 255);
    int padding = 3;
    int x1 = screenBounds.UpperLeftCorner.X + padding;
    int y1 = screenBounds.UpperLeftCorner.Y + padding;
    int x2 = screenBounds.LowerRightCorner.X - padding;
    int y2 = screenBounds.LowerRightCorner.Y - padding;

    // Draw two lines for the X
    driver->draw2DLine(irr::core::position2di(x1, y1), irr::core::position2di(x2, y2), xColor);
    driver->draw2DLine(irr::core::position2di(x2, y1), irr::core::position2di(x1, y2), xColor);
}

} // namespace ui
} // namespace eqt
