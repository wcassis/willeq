#include "client/graphics/ui/bag_window.h"
#include <sstream>

namespace eqt {
namespace ui {

BagWindow::BagWindow(int16_t parentSlotId, int bagSize, inventory::InventoryManager* manager)
    : WindowBase(L"Container", 100, 100)  // Size will be calculated
    , parentSlotId_(parentSlotId)
    , bagSize_(bagSize)
    , manager_(manager)
{
    // Get bag name if available
    const inventory::ItemInstance* bag = manager_->getItem(parentSlotId_);
    if (bag) {
        std::wstring bagName(bag->name.begin(), bag->name.end());
        setTitle(bagName);
    }

    calculateLayout();
    initializeSlots();
}

void BagWindow::calculateLayout() {
    // Always use 2 columns, expand rows as needed
    // Supports up to 10 slots (5 rows)
    columns_ = 2;
    rows_ = (bagSize_ + 1) / 2;  // Ceiling division

    // Calculate window size
    int contentWidth = columns_ * SLOT_SIZE + (columns_ - 1) * SLOT_SPACING;
    int contentHeight = rows_ * SLOT_SIZE + (rows_ - 1) * SLOT_SPACING;

    int windowWidth = contentWidth + PADDING * 2 + BORDER_WIDTH * 2;
    int windowHeight = contentHeight + PADDING * 2 + BORDER_WIDTH * 2 + TITLE_BAR_HEIGHT;

    setSize(windowWidth, windowHeight);
}

void BagWindow::initializeSlots() {
    slots_.clear();
    slots_.reserve(bagSize_);

    int startX = BORDER_WIDTH + PADDING;
    int startY = BORDER_WIDTH + TITLE_BAR_HEIGHT + PADDING;

    for (int i = 0; i < bagSize_; i++) {
        int col = i % columns_;
        int row = i / columns_;

        int x = startX + col * (SLOT_SIZE + SLOT_SPACING);
        int y = startY + row * (SLOT_SIZE + SLOT_SPACING);

        // Calculate the actual slot ID for this bag slot
        // Uses calcContainerSlotId which handles general, bank, shared bank, cursor, and trade bags
        int16_t slotId = inventory::calcContainerSlotId(parentSlotId_, i);

        ItemSlot slot(slotId, SlotType::Bag, x, y, SLOT_SIZE);
        slots_.push_back(slot);
    }
}

bool BagWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
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

    // Check slots
    for (const auto& slot : slots_) {
        if (slot.containsPoint(relX, relY)) {
            int16_t slotId = slot.getSlotId();
            const inventory::ItemInstance* item = manager_->getItem(slotId);

            // Ctrl+click on readable item = read it
            if (ctrl && item && !item->bookText.empty() && readItemCallback_) {
                readItemCallback_(item->bookText, static_cast<uint8_t>(item->bookType));
                return true;
            }

            if (slotClickCallback_) {
                slotClickCallback_(slotId, shift, ctrl);
            }
            return true;
        }
    }

    return true;  // Consume click even if not on a slot
}

bool BagWindow::handleMouseUp(int x, int y, bool leftButton) {
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool BagWindow::handleMouseMove(int x, int y) {
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (!visible_ || !containsPoint(x, y)) {
        clearHighlights();
        if (slotHoverCallback_) {
            slotHoverCallback_(inventory::SLOT_INVALID, x, y);
        }
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Find slot under cursor
    int16_t hoveredSlot = inventory::SLOT_INVALID;
    for (const auto& slot : slots_) {
        if (slot.containsPoint(relX, relY)) {
            hoveredSlot = slot.getSlotId();
            break;
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

int16_t BagWindow::getSlotAtPosition(int x, int y) const {
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

void BagWindow::setHighlightedSlot(int16_t slotId) {
    highlightedSlot_ = slotId;

    for (auto& slot : slots_) {
        slot.setHighlighted(slot.getSlotId() == slotId);
    }
}

void BagWindow::setInvalidDropSlot(int16_t slotId) {
    invalidDropSlot_ = slotId;

    for (auto& slot : slots_) {
        slot.setInvalidDrop(slot.getSlotId() == slotId);
    }
}

void BagWindow::clearHighlights() {
    highlightedSlot_ = inventory::SLOT_INVALID;
    invalidDropSlot_ = inventory::SLOT_INVALID;

    for (auto& slot : slots_) {
        slot.setHighlighted(false);
        slot.setInvalidDrop(false);
    }
}

void BagWindow::renderContent(irr::video::IVideoDriver* driver,
                             irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    for (auto& slot : slots_) {
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

} // namespace ui
} // namespace eqt
