#include "client/graphics/ui/loot_window.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/window_manager.h"
#include <algorithm>
#include <iostream>
#include "common/logging.h"

namespace eqt {
namespace ui {

LootWindow::LootWindow(inventory::InventoryManager* invManager, WindowManager* windowManager)
    : WindowBase(L"Corpse", 100, 100)  // Size will be calculated
    , inventoryManager_(invManager)
    , windowManager_(windowManager)
{
    // Initialize layout constants from UISettings
    const auto& lootSettings = UISettings::instance().loot();
    COLUMNS = lootSettings.columns;
    ROWS = lootSettings.rows;
    VISIBLE_SLOTS = COLUMNS * ROWS;
    SLOT_SIZE = lootSettings.slotSize;
    SLOT_SPACING = lootSettings.slotSpacing;
    PADDING = lootSettings.padding;
    BUTTON_WIDTH = lootSettings.buttonWidth;
    BUTTON_SPACING = lootSettings.buttonSpacing;
    SCROLLBAR_WIDTH = lootSettings.scrollbarWidth;
    SCROLL_BUTTON_HEIGHT = lootSettings.scrollButtonHeight;
    TOP_BUTTON_ROW_HEIGHT = lootSettings.topButtonRowHeight;
    BOTTOM_BUTTON_ROW_HEIGHT = lootSettings.bottomButtonRowHeight;

    calculateLayout();
    initializeSlots();
}

void LootWindow::calculateLayout() {
    // Calculate content dimensions for VISIBLE_SLOTS (8 items in 2 rows of 4)
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int slotsHeight = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_SPACING;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    // Window dimensions (include scrollbar width)
    int windowWidth = slotsWidth + PADDING * 2 + borderWidth * 2 + SCROLLBAR_WIDTH + PADDING;
    int windowHeight = borderWidth + titleBarHeight +
                       PADDING + TOP_BUTTON_ROW_HEIGHT +    // Top buttons (Loot All, Destroy All)
                       PADDING + slotsHeight +               // Item slots (8 visible)
                       PADDING + BOTTOM_BUTTON_ROW_HEIGHT +  // Close button
                       PADDING + borderWidth;

    setSize(windowWidth, windowHeight);
}

void LootWindow::initializeSlots() {
    slots_.clear();
    slots_.reserve(VISIBLE_SLOTS);
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    int startX = borderWidth + PADDING;
    int startY = borderWidth + titleBarHeight + PADDING + TOP_BUTTON_ROW_HEIGHT + PADDING;

    // Only create slots for visible items (8 slots)
    for (int i = 0; i < VISIBLE_SLOTS; i++) {
        int col = i % COLUMNS;
        int row = i / COLUMNS;

        int x = startX + col * (SLOT_SIZE + SLOT_SPACING);
        int y = startY + row * (SLOT_SIZE + SLOT_SPACING);

        ItemSlot slot(static_cast<int16_t>(i), SlotType::General, x, y, SLOT_SIZE);
        slots_.push_back(slot);
    }
}

void LootWindow::open(uint16_t corpseId, const std::string& corpseName) {
    corpseId_ = corpseId;
    corpseName_ = corpseName;
    isOpen_ = true;
    scrollOffset_ = 0;

    // Set title to corpse name
    std::wstring title(corpseName.begin(), corpseName.end());
    setTitle(title);

    clearLoot();
    show();
}

void LootWindow::close() {
    if (!isOpen_) {
        return;  // Already closed or closing
    }

    // Set isOpen_ to false BEFORE calling callback to prevent recursion
    // (callback may call closeLootWindow which calls close() again)
    isOpen_ = false;
    uint16_t corpseIdCopy = corpseId_;

    corpseId_ = 0;
    corpseName_.clear();
    clearLoot();
    hide();

    // Call callback after state is cleared
    if (onClose_) {
        onClose_(corpseIdCopy);
    }
}

void LootWindow::addLootItem(int16_t slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (item) {
        LOG_DEBUG(MOD_UI, "Loot UI addLootItem: slot={} name='{}' icon={}", slot, item->name, item->icon);
        lootItems_[slot] = std::move(item);
    } else {
        LOG_DEBUG(MOD_UI, "Loot UI addLootItem: slot={} item=nullptr", slot);
    }
}

void LootWindow::removeItem(int16_t slot) {
    lootItems_.erase(slot);

    // Adjust scroll offset if needed
    int maxOffset = getMaxScrollOffset();
    if (scrollOffset_ > maxOffset) {
        scrollOffset_ = maxOffset;
    }

    // Auto-close if empty
    if (lootItems_.empty() && isOpen_) {
        close();
    }
}

void LootWindow::clearLoot() {
    lootItems_.clear();
    highlightedSlot_ = inventory::SLOT_INVALID;
    scrollOffset_ = 0;
}

const inventory::ItemInstance* LootWindow::getItem(int16_t slot) const {
    auto it = lootItems_.find(slot);
    if (it != lootItems_.end()) {
        return it->second.get();
    }
    return nullptr;
}

int LootWindow::getMaxScrollOffset() const {
    int totalItems = static_cast<int>(lootItems_.size());
    int maxOffset = totalItems - VISIBLE_SLOTS;
    return std::max(0, maxOffset);
}

void LootWindow::scrollUp() {
    if (scrollOffset_ > 0) {
        scrollOffset_--;
    }
}

void LootWindow::scrollDown() {
    int maxOffset = getMaxScrollOffset();
    if (scrollOffset_ < maxOffset) {
        scrollOffset_++;
    }
}

bool LootWindow::handleMouseWheel(float delta) {
    if (!visible_ || !isOpen_) {
        return false;
    }

    if (delta > 0) {
        scrollUp();
    } else if (delta < 0) {
        scrollDown();
    }

    return true;
}

bool LootWindow::canLootAll() const {
    if (lootItems_.empty()) {
        return true;  // Nothing to loot
    }

    if (!inventoryManager_) {
        return false;
    }

    // Count available slots in player inventory
    int availableSlots = 0;

    // Check general inventory slots (22-29)
    for (int16_t slot = inventory::GENERAL_BEGIN; slot <= inventory::GENERAL_END; slot++) {
        if (!inventoryManager_->hasItem(slot)) {
            availableSlots++;
        } else if (inventoryManager_->isBag(slot)) {
            // Also count empty slots in bags
            auto bagSlots = inventoryManager_->getBagContentsSlots(slot);
            for (int16_t bagSlot : bagSlots) {
                if (!inventoryManager_->hasItem(bagSlot)) {
                    availableSlots++;
                }
            }
        }
    }

    int itemsToLoot = static_cast<int>(lootItems_.size());

    if (itemsToLoot > availableSlots) {
        return false;
    }

    // Check for lore conflicts
    for (const auto& [slot, item] : lootItems_) {
        if (item && item->hasFlag(inventory::ITEM_FLAG_LORE)) {
            if (inventoryManager_->hasLoreConflict(item.get())) {
                return false;
            }
        }
    }

    return true;
}

bool LootWindow::canLootItem(int16_t slot) const {
    auto it = lootItems_.find(slot);
    if (it == lootItems_.end() || !it->second) {
        return false;
    }

    if (!inventoryManager_) {
        return false;
    }

    const auto& item = it->second;

    // Check lore conflict
    if (item->hasFlag(inventory::ITEM_FLAG_LORE)) {
        if (inventoryManager_->hasLoreConflict(item.get())) {
            return false;
        }
    }

    // Check if there's an available slot
    // First check general inventory slots (22-29)
    for (int16_t invSlot = inventory::GENERAL_BEGIN; invSlot <= inventory::GENERAL_END; invSlot++) {
        if (!inventoryManager_->hasItem(invSlot)) {
            return true;  // Found an empty slot
        }
        // Check if this slot is a bag with empty slots
        if (inventoryManager_->isBag(invSlot)) {
            // Check if item can fit in the bag (size check)
            const auto* bag = inventoryManager_->getItem(invSlot);
            if (bag && item->size <= bag->bagSize) {
                auto bagSlots = inventoryManager_->getBagContentsSlots(invSlot);
                for (int16_t bagSlot : bagSlots) {
                    if (!inventoryManager_->hasItem(bagSlot)) {
                        return true;  // Found an empty slot in a bag
                    }
                }
            }
        }
    }

    return false;  // No empty slots
}

std::string LootWindow::getLootAllError() const {
    if (lootItems_.empty()) {
        return "";
    }

    if (!inventoryManager_) {
        return "Inventory not available";
    }

    // Check for lore conflicts first
    for (const auto& [slot, item] : lootItems_) {
        if (item && item->hasFlag(inventory::ITEM_FLAG_LORE)) {
            if (inventoryManager_->hasLoreConflict(item.get())) {
                return "You already have that lore item";
            }
        }
    }

    // Count available slots in player inventory
    int availableSlots = 0;

    // Check general inventory slots (22-29) and bags
    for (int16_t slot = inventory::GENERAL_BEGIN; slot <= inventory::GENERAL_END; slot++) {
        if (!inventoryManager_->hasItem(slot)) {
            availableSlots++;
        } else if (inventoryManager_->isBag(slot)) {
            auto bagSlots = inventoryManager_->getBagContentsSlots(slot);
            for (int16_t bagSlot : bagSlots) {
                if (!inventoryManager_->hasItem(bagSlot)) {
                    availableSlots++;
                }
            }
        }
    }

    int itemsToLoot = static_cast<int>(lootItems_.size());

    if (itemsToLoot > availableSlots) {
        return "Insufficient inventory space";
    }

    return "";
}

std::string LootWindow::getLootItemError(int16_t slot) const {
    auto it = lootItems_.find(slot);
    if (it == lootItems_.end() || !it->second) {
        return "Item not found";
    }

    if (!inventoryManager_) {
        return "Inventory not available";
    }

    const auto& item = it->second;

    if (item->hasFlag(inventory::ITEM_FLAG_LORE)) {
        if (inventoryManager_->hasLoreConflict(item.get())) {
            return "You already have that lore item";
        }
    }

    // Check if there's an available slot (general slots and bags)
    for (int16_t invSlot = inventory::GENERAL_BEGIN; invSlot <= inventory::GENERAL_END; invSlot++) {
        if (!inventoryManager_->hasItem(invSlot)) {
            return "";  // Found an empty general slot
        }
        // Check bags for empty slots
        if (inventoryManager_->isBag(invSlot)) {
            const auto* bag = inventoryManager_->getItem(invSlot);
            if (bag && item->size <= bag->bagSize) {
                auto bagSlots = inventoryManager_->getBagContentsSlots(invSlot);
                for (int16_t bagSlot : bagSlots) {
                    if (!inventoryManager_->hasItem(bagSlot)) {
                        return "";  // Found an empty bag slot
                    }
                }
            }
        }
    }

    return "Inventory is full";
}

bool LootWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check title bar for dragging
    if (titleBarContainsPoint(x, y)) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    // Check scrollbar thumb for dragging
    irr::core::recti thumbBounds = getScrollbarThumbBounds();
    if (thumbBounds.isPointInside(irr::core::vector2di(x, y))) {
        draggingScrollbar_ = true;
        scrollbarDragStartY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
        return true;
    }

    // Check scrollbar track (click to jump)
    irr::core::recti trackBounds = getScrollbarTrackBounds();
    if (trackBounds.isPointInside(irr::core::vector2di(x, y))) {
        // Click above thumb = scroll up, below = scroll down
        if (y < thumbBounds.UpperLeftCorner.Y) {
            scrollUp();
        } else if (y > thumbBounds.LowerRightCorner.Y) {
            scrollDown();
        }
        return true;
    }

    // Check buttons
    ButtonId button = getButtonAtPosition(x, y);
    if (button != ButtonId::None) {
        switch (button) {
            case ButtonId::LootAll:
                LOG_DEBUG(MOD_UI, "Loot UI LootAll button clicked, corpseId={} canLootAll={} hasCallback={}", corpseId_, canLootAll(), (onLootAll_ ? "yes" : "no"));
                if (canLootAll() && onLootAll_) {
                    onLootAll_(corpseId_);
                }
                break;
            case ButtonId::DestroyAll:
                LOG_DEBUG(MOD_UI, "Loot UI DestroyAll button clicked, corpseId={}", corpseId_);
                if (onDestroyAll_) {
                    onDestroyAll_(corpseId_);
                }
                break;
            case ButtonId::Close:
                LOG_DEBUG(MOD_UI, "Loot UI Close button clicked, corpseId={}", corpseId_);
                close();
                break;
            case ButtonId::ScrollUp:
                scrollUp();
                break;
            case ButtonId::ScrollDown:
                scrollDown();
                break;
            default:
                break;
        }
        return true;
    }

    // Check slots
    int16_t clickedSlot = getSlotAtPosition(x, y);
    if (clickedSlot != inventory::SLOT_INVALID) {
        LOG_DEBUG(MOD_UI, "Loot UI Slot clicked: displaySlot={} scrollOffset={}", clickedSlot, scrollOffset_);
        // Find the actual corpse slot ID for this display index
        int targetIndex = scrollOffset_ + clickedSlot;
        int currentIndex = 0;
        for (const auto& [corpseSlot, item] : lootItems_) {
            if (currentIndex == targetIndex) {
                LOG_DEBUG(MOD_UI, "Loot UI Found item at corpseSlot={} name='{}' canLoot={}", corpseSlot, item->name, canLootItem(corpseSlot));
                // Validate before attempting to pick up
                if (canLootItem(corpseSlot)) {
                    // Use click-to-move: pick up item to cursor
                    if (windowManager_) {
                        windowManager_->pickupLootItem(corpseId_, corpseSlot);
                    }
                } else {
                    LOG_DEBUG(MOD_UI, "Loot UI Cannot loot item: {}", getLootItemError(corpseSlot));
                }
                // If validation fails, the error can be retrieved via getLootItemError()
                break;
            }
            currentIndex++;
        }
        return true;
    }

    return true;  // Consume click even if not on a slot
}

bool LootWindow::handleMouseUp(int x, int y, bool leftButton) {
    if (draggingScrollbar_ && leftButton) {
        draggingScrollbar_ = false;
        return true;
    }
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool LootWindow::handleMouseMove(int x, int y) {
    // Handle scrollbar dragging
    if (draggingScrollbar_) {
        irr::core::recti trackBounds = getScrollbarTrackBounds();
        int trackHeight = trackBounds.getHeight() - SCROLL_BUTTON_HEIGHT * 2;

        if (trackHeight > 0) {
            int maxOffset = getMaxScrollOffset();
            if (maxOffset > 0) {
                int deltaY = y - scrollbarDragStartY_;
                float scrollPerPixel = static_cast<float>(maxOffset) / trackHeight;
                int newOffset = scrollbarDragStartOffset_ + static_cast<int>(deltaY * scrollPerPixel);
                scrollOffset_ = std::max(0, std::min(newOffset, maxOffset));
            }
        }
        return true;
    }

    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (!visible_ || !containsPoint(x, y)) {
        clearHighlights();
        hoveredButton_ = ButtonId::None;
        return false;
    }

    // Check buttons (including scroll buttons)
    hoveredButton_ = getButtonAtPosition(x, y);

    // Check slots
    int16_t hoveredSlot = getSlotAtPosition(x, y);
    clearHighlights();
    if (hoveredSlot != inventory::SLOT_INVALID) {
        setHighlightedSlot(hoveredSlot);
    }

    return true;
}

int16_t LootWindow::getSlotAtPosition(int x, int y) const {
    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check each visible slot
    for (int i = 0; i < VISIBLE_SLOTS; i++) {
        if (i < static_cast<int>(slots_.size()) && slots_[i].containsPoint(relX, relY)) {
            // Make sure this slot index maps to an actual item
            int itemIndex = scrollOffset_ + i;
            if (itemIndex < static_cast<int>(lootItems_.size())) {
                return static_cast<int16_t>(i);
            }
        }
    }

    return inventory::SLOT_INVALID;
}

void LootWindow::setHighlightedSlot(int16_t slotId) {
    highlightedSlot_ = slotId;

    for (size_t i = 0; i < slots_.size(); i++) {
        slots_[i].setHighlighted(static_cast<int16_t>(i) == slotId);
    }
}

void LootWindow::clearHighlights() {
    highlightedSlot_ = inventory::SLOT_INVALID;

    for (auto& slot : slots_) {
        slot.setHighlighted(false);
    }
}

LootWindow::ButtonId LootWindow::getButtonAtPosition(int x, int y) const {
    // Check all buttons including scroll buttons
    for (int i = 0; i <= static_cast<int>(ButtonId::ScrollDown); i++) {
        ButtonId btn = static_cast<ButtonId>(i);
        irr::core::recti bounds = getButtonBounds(btn);
        if (bounds.isPointInside(irr::core::vector2di(x, y))) {
            return btn;
        }
    }
    return ButtonId::None;
}

irr::core::recti LootWindow::getButtonBounds(ButtonId button) const {
    int windowX = bounds_.UpperLeftCorner.X;
    int windowY = bounds_.UpperLeftCorner.Y;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int buttonHeight = getButtonHeight();

    switch (button) {
        case ButtonId::LootAll: {
            int x = windowX + borderWidth + PADDING;
            int y = windowY + borderWidth + titleBarHeight + PADDING;
            return irr::core::recti(x, y, x + BUTTON_WIDTH, y + buttonHeight);
        }
        case ButtonId::DestroyAll: {
            int x = windowX + borderWidth + PADDING + BUTTON_WIDTH + BUTTON_SPACING;
            int y = windowY + borderWidth + titleBarHeight + PADDING;
            return irr::core::recti(x, y, x + BUTTON_WIDTH, y + buttonHeight);
        }
        case ButtonId::Close: {
            // Center the close button at the bottom (excluding scrollbar area)
            int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
            int contentWidth = slotsWidth + PADDING * 2;
            int x = windowX + borderWidth + (contentWidth - BUTTON_WIDTH) / 2;
            int y = bounds_.LowerRightCorner.Y - borderWidth - PADDING - buttonHeight;
            return irr::core::recti(x, y, x + BUTTON_WIDTH, y + buttonHeight);
        }
        case ButtonId::ScrollUp: {
            irr::core::recti track = getScrollbarTrackBounds();
            return irr::core::recti(track.UpperLeftCorner.X, track.UpperLeftCorner.Y,
                                   track.LowerRightCorner.X, track.UpperLeftCorner.Y + SCROLL_BUTTON_HEIGHT);
        }
        case ButtonId::ScrollDown: {
            irr::core::recti track = getScrollbarTrackBounds();
            return irr::core::recti(track.UpperLeftCorner.X, track.LowerRightCorner.Y - SCROLL_BUTTON_HEIGHT,
                                   track.LowerRightCorner.X, track.LowerRightCorner.Y);
        }
        default:
            return irr::core::recti();
    }
}

irr::core::recti LootWindow::getScrollbarTrackBounds() const {
    int slotsWidth = COLUMNS * SLOT_SIZE + (COLUMNS - 1) * SLOT_SPACING;
    int slotsHeight = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_SPACING;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    int x = bounds_.UpperLeftCorner.X + borderWidth + PADDING + slotsWidth + PADDING;
    int y = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + PADDING + TOP_BUTTON_ROW_HEIGHT + PADDING;

    return irr::core::recti(x, y, x + SCROLLBAR_WIDTH, y + slotsHeight);
}

irr::core::recti LootWindow::getScrollbarThumbBounds() const {
    irr::core::recti track = getScrollbarTrackBounds();

    // Account for up/down buttons
    int trackTop = track.UpperLeftCorner.Y + SCROLL_BUTTON_HEIGHT;
    int trackBottom = track.LowerRightCorner.Y - SCROLL_BUTTON_HEIGHT;
    int trackHeight = trackBottom - trackTop;

    int totalItems = static_cast<int>(lootItems_.size());
    if (totalItems <= VISIBLE_SLOTS) {
        // No scrolling needed - thumb fills track
        return irr::core::recti(track.UpperLeftCorner.X, trackTop,
                               track.LowerRightCorner.X, trackBottom);
    }

    // Calculate thumb size based on visible ratio
    float visibleRatio = static_cast<float>(VISIBLE_SLOTS) / totalItems;
    int thumbHeight = std::max(20, static_cast<int>(trackHeight * visibleRatio));

    // Calculate thumb position based on scroll offset
    int maxOffset = getMaxScrollOffset();
    float scrollRatio = (maxOffset > 0) ? static_cast<float>(scrollOffset_) / maxOffset : 0.0f;
    int thumbY = trackTop + static_cast<int>((trackHeight - thumbHeight) * scrollRatio);

    return irr::core::recti(track.UpperLeftCorner.X, thumbY,
                           track.LowerRightCorner.X, thumbY + thumbHeight);
}

bool LootWindow::isScrollbarAtPosition(int x, int y) const {
    irr::core::recti track = getScrollbarTrackBounds();
    return track.isPointInside(irr::core::vector2di(x, y));
}

void LootWindow::renderContent(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui) {
    renderButtons(driver, gui);
    renderSlots(driver, gui);
    renderScrollbar(driver);
}

void LootWindow::renderButtons(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui) {
    // Loot All button - disabled if can't loot all items
    bool lootAllHighlighted = (hoveredButton_ == ButtonId::LootAll);
    bool lootAllDisabled = !canLootAll();
    irr::core::recti lootAllBounds = getButtonBounds(ButtonId::LootAll);
    drawButton(driver, gui, lootAllBounds, L"Loot All", lootAllHighlighted && !lootAllDisabled, lootAllDisabled);

    // Destroy All button
    bool destroyAllHighlighted = (hoveredButton_ == ButtonId::DestroyAll);
    irr::core::recti destroyAllBounds = getButtonBounds(ButtonId::DestroyAll);
    drawButton(driver, gui, destroyAllBounds, L"Destroy All", destroyAllHighlighted);

    // Close button
    bool closeHighlighted = (hoveredButton_ == ButtonId::Close);
    irr::core::recti closeBounds = getButtonBounds(ButtonId::Close);
    drawButton(driver, gui, closeBounds, L"Close", closeHighlighted);
}

void LootWindow::renderScrollbar(irr::video::IVideoDriver* driver) {
    irr::core::recti track = getScrollbarTrackBounds();

    // Get colors from UISettings
    const auto& scrollbarSettings = UISettings::instance().scrollbar();
    irr::video::SColor trackColor = scrollbarSettings.trackColor;
    irr::video::SColor thumbColor = scrollbarSettings.thumbColor;
    irr::video::SColor thumbHoverColor = scrollbarSettings.thumbHoverColor;
    irr::video::SColor buttonColor = scrollbarSettings.buttonColor;
    irr::video::SColor buttonHoverColor = scrollbarSettings.buttonHoverColor;
    irr::video::SColor arrowColor = scrollbarSettings.arrowColor;

    // Draw track background
    driver->draw2DRectangle(trackColor, track);

    // Draw track border
    driver->draw2DRectangle(getBorderDarkColor(),
        irr::core::recti(track.UpperLeftCorner.X, track.UpperLeftCorner.Y,
                        track.LowerRightCorner.X, track.UpperLeftCorner.Y + 1));
    driver->draw2DRectangle(getBorderDarkColor(),
        irr::core::recti(track.UpperLeftCorner.X, track.UpperLeftCorner.Y,
                        track.UpperLeftCorner.X + 1, track.LowerRightCorner.Y));

    // Draw up button
    irr::core::recti upBtn = getButtonBounds(ButtonId::ScrollUp);
    irr::video::SColor upColor = (hoveredButton_ == ButtonId::ScrollUp) ? buttonHoverColor : buttonColor;
    driver->draw2DRectangle(upColor, upBtn);
    // Draw up arrow (simple triangle using lines)
    int arrowCenterX = (upBtn.UpperLeftCorner.X + upBtn.LowerRightCorner.X) / 2;
    int arrowCenterY = (upBtn.UpperLeftCorner.Y + upBtn.LowerRightCorner.Y) / 2;
    driver->draw2DLine(irr::core::vector2di(arrowCenterX, arrowCenterY - 3),
                       irr::core::vector2di(arrowCenterX - 4, arrowCenterY + 2),
                       arrowColor);
    driver->draw2DLine(irr::core::vector2di(arrowCenterX, arrowCenterY - 3),
                       irr::core::vector2di(arrowCenterX + 4, arrowCenterY + 2),
                       arrowColor);

    // Draw down button
    irr::core::recti downBtn = getButtonBounds(ButtonId::ScrollDown);
    irr::video::SColor downColor = (hoveredButton_ == ButtonId::ScrollDown) ? buttonHoverColor : buttonColor;
    driver->draw2DRectangle(downColor, downBtn);
    // Draw down arrow
    arrowCenterX = (downBtn.UpperLeftCorner.X + downBtn.LowerRightCorner.X) / 2;
    arrowCenterY = (downBtn.UpperLeftCorner.Y + downBtn.LowerRightCorner.Y) / 2;
    driver->draw2DLine(irr::core::vector2di(arrowCenterX, arrowCenterY + 3),
                       irr::core::vector2di(arrowCenterX - 4, arrowCenterY - 2),
                       arrowColor);
    driver->draw2DLine(irr::core::vector2di(arrowCenterX, arrowCenterY + 3),
                       irr::core::vector2di(arrowCenterX + 4, arrowCenterY - 2),
                       arrowColor);

    // Draw thumb (only if there are items to scroll)
    if (lootItems_.size() > static_cast<size_t>(VISIBLE_SLOTS)) {
        irr::core::recti thumb = getScrollbarThumbBounds();
        irr::video::SColor currentThumbColor = draggingScrollbar_ ? thumbHoverColor : thumbColor;
        driver->draw2DRectangle(currentThumbColor, thumb);

        // Draw thumb highlight (3D effect)
        irr::video::SColor thumbHighlight(255, 130, 130, 130);
        irr::video::SColor thumbShadow(255, 70, 70, 70);
        driver->draw2DRectangle(thumbHighlight,
            irr::core::recti(thumb.UpperLeftCorner.X, thumb.UpperLeftCorner.Y,
                            thumb.LowerRightCorner.X, thumb.UpperLeftCorner.Y + 1));
        driver->draw2DRectangle(thumbHighlight,
            irr::core::recti(thumb.UpperLeftCorner.X, thumb.UpperLeftCorner.Y,
                            thumb.UpperLeftCorner.X + 1, thumb.LowerRightCorner.Y));
        driver->draw2DRectangle(thumbShadow,
            irr::core::recti(thumb.UpperLeftCorner.X, thumb.LowerRightCorner.Y - 1,
                            thumb.LowerRightCorner.X, thumb.LowerRightCorner.Y));
        driver->draw2DRectangle(thumbShadow,
            irr::core::recti(thumb.LowerRightCorner.X - 1, thumb.UpperLeftCorner.Y,
                            thumb.LowerRightCorner.X, thumb.LowerRightCorner.Y));
    }
}

void LootWindow::renderSlots(irr::video::IVideoDriver* driver,
                             irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Get items as a vector for indexed access
    std::vector<std::pair<int16_t, const inventory::ItemInstance*>> itemList;
    for (const auto& [corpseSlot, item] : lootItems_) {
        itemList.push_back({corpseSlot, item.get()});
    }

    // Render only visible items based on scroll offset
    for (int i = 0; i < VISIBLE_SLOTS; i++) {
        int itemIndex = scrollOffset_ + i;
        if (itemIndex >= static_cast<int>(itemList.size())) {
            break;
        }

        if (i >= static_cast<int>(slots_.size())) {
            break;
        }

        auto& slot = slots_[i];
        const inventory::ItemInstance* item = itemList[itemIndex].second;

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
