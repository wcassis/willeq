#include "client/graphics/ui/vendor_window.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/window_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "common/logging.h"

namespace eqt {
namespace ui {

VendorWindow::VendorWindow(inventory::InventoryManager* invManager, WindowManager* windowManager)
    : WindowBase(L"Merchant", 100, 100)  // Size will be calculated
    , inventoryManager_(invManager)
    , windowManager_(windowManager)
{
    calculateLayout();
}

void VendorWindow::calculateLayout() {
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    // Calculate content dimensions for list view
    int listWidth = NAME_COLUMN_WIDTH + PRICE_COLUMN_WIDTH + SCROLLBAR_WIDTH;
    int listHeight = HEADER_HEIGHT + VISIBLE_ROWS * ROW_HEIGHT;

    // Window dimensions (include tab bar)
    int windowWidth = listWidth + PADDING * 2 + borderWidth * 2;
    int windowHeight = borderWidth + titleBarHeight +
                       PADDING + TAB_HEIGHT +              // Tab bar (Buy/Sell)
                       listHeight +                        // Header + list rows
                       PADDING + BUTTON_ROW_HEIGHT +       // Buy/Close buttons
                       PADDING + borderWidth;

    setSize(windowWidth, windowHeight);
}

void VendorWindow::open(uint16_t npcId, const std::string& vendorName, float sellRate) {
    npcId_ = npcId;
    vendorName_ = vendorName;
    sellRate_ = sellRate;
    isOpen_ = true;
    currentMode_ = VendorMode::BUY;  // Always start in buy mode
    scrollOffset_ = 0;
    selectedSlot_ = -1;
    highlightedSlot_ = -1;
    highlightedRow_ = -1;

    // Set title to vendor name
    std::wstring title(vendorName.begin(), vendorName.end());
    setTitle(title);

    clearItems();
    clearSellableItems();
    show();

    LOG_DEBUG(MOD_UI, "Vendor window opened: npcId={} name='{}' sellRate={:.4f}",
              npcId, vendorName, sellRate);
}

void VendorWindow::close() {
    if (!isOpen_) {
        return;  // Already closed
    }

    // Set isOpen_ to false BEFORE calling callback to prevent recursion
    isOpen_ = false;
    uint16_t npcIdCopy = npcId_;

    npcId_ = 0;
    vendorName_.clear();
    sellRate_ = 1.0f;
    currentMode_ = VendorMode::BUY;
    clearItems();
    clearSellableItems();
    hide();

    LOG_DEBUG(MOD_UI, "Vendor window closed: npcId={}", npcIdCopy);

    // Call callback after state is cleared
    if (onClose_) {
        onClose_(npcIdCopy);
    }
}

void VendorWindow::addVendorItem(uint32_t slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (item) {
        LOG_DEBUG(MOD_UI, "Vendor UI addVendorItem: slot={} name='{}' icon={} price={}",
                  slot, item->name, item->icon, item->price);
        VendorItem vendorItem;
        vendorItem.item = std::move(item);
        vendorItem.vendorSlot = slot;
        vendorItems_[slot] = std::move(vendorItem);
        rebuildSortedList();
    }
}

void VendorWindow::removeItem(uint32_t slot) {
    vendorItems_.erase(slot);

    // Clear selection if removed item was selected
    if (selectedSlot_ == static_cast<int32_t>(slot)) {
        selectedSlot_ = -1;
    }

    rebuildSortedList();

    // Adjust scroll offset if needed
    int maxOffset = getMaxScrollOffset();
    if (scrollOffset_ > maxOffset) {
        scrollOffset_ = maxOffset;
    }
}

void VendorWindow::clearItems() {
    vendorItems_.clear();
    sortedSlots_.clear();
    selectedSlot_ = -1;
    highlightedSlot_ = -1;
    highlightedRow_ = -1;
    scrollOffset_ = 0;
}

const inventory::ItemInstance* VendorWindow::getItem(uint32_t slot) const {
    auto it = vendorItems_.find(slot);
    if (it != vendorItems_.end()) {
        return it->second.item.get();
    }
    return nullptr;
}

const VendorItem* VendorWindow::getSelectedItem() const {
    if (selectedSlot_ < 0) {
        return nullptr;
    }
    auto it = vendorItems_.find(static_cast<uint32_t>(selectedSlot_));
    if (it != vendorItems_.end()) {
        return &it->second;
    }
    return nullptr;
}

void VendorWindow::setSelectedSlot(int32_t slot) {
    selectedSlot_ = slot;
}

void VendorWindow::clearSelection() {
    selectedSlot_ = -1;
}

const inventory::ItemInstance* VendorWindow::getHighlightedItem() const {
    if (highlightedSlot_ < 0) {
        return nullptr;
    }
    auto it = vendorItems_.find(static_cast<uint32_t>(highlightedSlot_));
    if (it != vendorItems_.end()) {
        return it->second.item.get();
    }
    return nullptr;
}

void VendorWindow::setPlayerMoney(int32_t platinum, int32_t gold, int32_t silver, int32_t copper) {
    playerMoneyCopper_ = static_cast<int64_t>(platinum) * 1000 +
                         static_cast<int64_t>(gold) * 100 +
                         static_cast<int64_t>(silver) * 10 +
                         static_cast<int64_t>(copper);
}

int32_t VendorWindow::getItemPrice(uint32_t slot) const {
    auto it = vendorItems_.find(slot);
    if (it == vendorItems_.end() || !it->second.item) {
        return 0;
    }
    return it->second.item->price;
}

uint32_t VendorWindow::getMaxAffordableQuantity(uint32_t slot) const {
    int32_t price = getItemPrice(slot);
    if (price <= 0) {
        return 0;  // Free items or invalid
    }

    // Calculate how many we can afford
    uint32_t affordable = static_cast<uint32_t>(playerMoneyCopper_ / price);

    // Cap at 20 (typical stack size)
    return std::min(affordable, 20u);
}

bool VendorWindow::canBuyItem(uint32_t slot, uint32_t quantity) const {
    auto it = vendorItems_.find(slot);
    if (it == vendorItems_.end() || !it->second.item) {
        return false;
    }

    const auto& item = it->second.item;

    // Check price
    int64_t totalPrice = static_cast<int64_t>(item->price) * quantity;
    if (totalPrice > playerMoneyCopper_) {
        return false;
    }

    // Check lore conflict
    if (item->hasFlag(inventory::ITEM_FLAG_LORE)) {
        if (inventoryManager_ && inventoryManager_->hasLoreConflict(item.get())) {
            return false;
        }
    }

    // Check inventory space
    if (inventoryManager_) {
        // Simple check: do we have any empty slot?
        for (int16_t invSlot = inventory::GENERAL_BEGIN; invSlot <= inventory::GENERAL_END; invSlot++) {
            if (!inventoryManager_->hasItem(invSlot)) {
                return true;
            }
            // Check bags for empty slots
            if (inventoryManager_->isBag(invSlot)) {
                auto bagSlots = inventoryManager_->getBagContentsSlots(invSlot);
                for (int16_t bagSlot : bagSlots) {
                    if (!inventoryManager_->hasItem(bagSlot)) {
                        return true;
                    }
                }
            }
        }
        return false;  // No space
    }

    return true;
}

std::string VendorWindow::getBuyError(uint32_t slot, uint32_t quantity) const {
    auto it = vendorItems_.find(slot);
    if (it == vendorItems_.end() || !it->second.item) {
        return "Item not found";
    }

    const auto& item = it->second.item;

    // Check price
    int64_t totalPrice = static_cast<int64_t>(item->price) * quantity;
    if (totalPrice > playerMoneyCopper_) {
        return "You cannot afford this item";
    }

    // Check lore conflict
    if (item->hasFlag(inventory::ITEM_FLAG_LORE)) {
        if (inventoryManager_ && inventoryManager_->hasLoreConflict(item.get())) {
            return "You already have that lore item";
        }
    }

    // Check inventory space
    if (inventoryManager_) {
        bool hasSpace = false;
        for (int16_t invSlot = inventory::GENERAL_BEGIN; invSlot <= inventory::GENERAL_END; invSlot++) {
            if (!inventoryManager_->hasItem(invSlot)) {
                hasSpace = true;
                break;
            }
            if (inventoryManager_->isBag(invSlot)) {
                auto bagSlots = inventoryManager_->getBagContentsSlots(invSlot);
                for (int16_t bagSlot : bagSlots) {
                    if (!inventoryManager_->hasItem(bagSlot)) {
                        hasSpace = true;
                        break;
                    }
                }
                if (hasSpace) break;
            }
        }
        if (!hasSpace) {
            return "Inventory is full";
        }
    }

    return "";
}

std::string VendorWindow::formatPrice(int32_t copperAmount) const {
    if (copperAmount <= 0) {
        return "Free";
    }

    std::stringstream ss;
    bool hasValue = false;

    int32_t platinum = copperAmount / 1000;
    copperAmount %= 1000;
    int32_t gold = copperAmount / 100;
    copperAmount %= 100;
    int32_t silver = copperAmount / 10;
    int32_t copper = copperAmount % 10;

    if (platinum > 0) {
        ss << platinum << "p";
        hasValue = true;
    }
    if (gold > 0) {
        if (hasValue) ss << " ";
        ss << gold << "g";
        hasValue = true;
    }
    if (silver > 0) {
        if (hasValue) ss << " ";
        ss << silver << "s";
        hasValue = true;
    }
    if (copper > 0 || !hasValue) {
        if (hasValue) ss << " ";
        ss << copper << "c";
    }

    return ss.str();
}

// Mode switching
void VendorWindow::setMode(VendorMode mode) {
    if (currentMode_ != mode) {
        currentMode_ = mode;
        scrollOffset_ = 0;
        selectedSlot_ = -1;
        highlightedSlot_ = -1;
        highlightedRow_ = -1;

        LOG_DEBUG(MOD_UI, "Vendor mode changed to: {}",
                  mode == VendorMode::BUY ? "BUY" : "SELL");
    }
}

// Sell mode: get selected sellable item
const SellableItem* VendorWindow::getSelectedSellItem() const {
    if (selectedSlot_ < 0 || currentMode_ != VendorMode::SELL) {
        return nullptr;
    }

    // Find the item with matching inventory slot
    for (const auto& item : sellableItems_) {
        if (static_cast<int32_t>(item.inventorySlot) == selectedSlot_) {
            return &item;
        }
    }
    return nullptr;
}

// Sell mode: set sellable items from player inventory
void VendorWindow::setSellableItems(const std::vector<SellableItem>& items) {
    sellableItems_ = items;
    rebuildSellSortedList();

    // Clear selection if the selected item is no longer in the list
    if (selectedSlot_ >= 0 && currentMode_ == VendorMode::SELL) {
        bool found = false;
        for (const auto& item : sellableItems_) {
            if (static_cast<int32_t>(item.inventorySlot) == selectedSlot_) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_DEBUG(MOD_UI, "Clearing vendor sell selection - item at slot {} no longer available", selectedSlot_);
            selectedSlot_ = -1;
        }
    }

    LOG_DEBUG(MOD_UI, "Set {} sellable items", items.size());
}

void VendorWindow::clearSellableItems() {
    sellableItems_.clear();
    sellSortedIndices_.clear();
}

uint32_t VendorWindow::calculateSellPrice(uint32_t basePrice, uint32_t quantity) const {
    // Sell price is typically a fraction of the base price
    // The sellRate_ from the vendor affects the price
    // Standard EQ uses about 1/4 to 1/7 of base price for selling
    // We'll use sellRate_ as a multiplier (typically around 1.0)
    float sellMultiplier = 0.25f;  // Base sell rate (25% of item price)
    return static_cast<uint32_t>(basePrice * quantity * sellMultiplier * sellRate_);
}

void VendorWindow::rebuildSellSortedList() {
    sellSortedIndices_.clear();
    for (size_t i = 0; i < sellableItems_.size(); i++) {
        sellSortedIndices_.push_back(i);
    }

    // Sort based on current mode
    switch (sortMode_) {
        case VendorSortMode::NameAsc:
            std::sort(sellSortedIndices_.begin(), sellSortedIndices_.end(),
                [this](size_t a, size_t b) {
                    return sellableItems_[a].name < sellableItems_[b].name;
                });
            break;
        case VendorSortMode::NameDesc:
            std::sort(sellSortedIndices_.begin(), sellSortedIndices_.end(),
                [this](size_t a, size_t b) {
                    return sellableItems_[a].name > sellableItems_[b].name;
                });
            break;
        case VendorSortMode::PriceAsc:
            std::sort(sellSortedIndices_.begin(), sellSortedIndices_.end(),
                [this](size_t a, size_t b) {
                    return sellableItems_[a].basePrice < sellableItems_[b].basePrice;
                });
            break;
        case VendorSortMode::PriceDesc:
            std::sort(sellSortedIndices_.begin(), sellSortedIndices_.end(),
                [this](size_t a, size_t b) {
                    return sellableItems_[a].basePrice > sellableItems_[b].basePrice;
                });
            break;
        default:
            break;
    }
}

// Sorting implementation
void VendorWindow::rebuildSortedList() {
    sortedSlots_.clear();
    for (const auto& [slot, vendorItem] : vendorItems_) {
        sortedSlots_.push_back(slot);
    }

    // Sort based on current mode
    switch (sortMode_) {
        case VendorSortMode::NameAsc:
            std::sort(sortedSlots_.begin(), sortedSlots_.end(), [this](uint32_t a, uint32_t b) {
                const auto* itemA = getItem(a);
                const auto* itemB = getItem(b);
                if (!itemA || !itemB) return false;
                return itemA->name < itemB->name;
            });
            break;
        case VendorSortMode::NameDesc:
            std::sort(sortedSlots_.begin(), sortedSlots_.end(), [this](uint32_t a, uint32_t b) {
                const auto* itemA = getItem(a);
                const auto* itemB = getItem(b);
                if (!itemA || !itemB) return false;
                return itemA->name > itemB->name;
            });
            break;
        case VendorSortMode::PriceAsc:
            std::sort(sortedSlots_.begin(), sortedSlots_.end(), [this](uint32_t a, uint32_t b) {
                const auto* itemA = getItem(a);
                const auto* itemB = getItem(b);
                if (!itemA || !itemB) return false;
                return itemA->price < itemB->price;
            });
            break;
        case VendorSortMode::PriceDesc:
            std::sort(sortedSlots_.begin(), sortedSlots_.end(), [this](uint32_t a, uint32_t b) {
                const auto* itemA = getItem(a);
                const auto* itemB = getItem(b);
                if (!itemA || !itemB) return false;
                return itemA->price > itemB->price;
            });
            break;
        default:
            break;
    }
}

void VendorWindow::setSortMode(VendorSortMode mode) {
    if (sortMode_ != mode) {
        sortMode_ = mode;
        rebuildSortedList();
    }
}

void VendorWindow::toggleSortByName() {
    if (sortMode_ == VendorSortMode::NameAsc) {
        setSortMode(VendorSortMode::NameDesc);
    } else {
        setSortMode(VendorSortMode::NameAsc);
    }
}

void VendorWindow::toggleSortByPrice() {
    if (sortMode_ == VendorSortMode::PriceAsc) {
        setSortMode(VendorSortMode::PriceDesc);
    } else {
        setSortMode(VendorSortMode::PriceAsc);
    }
}

int VendorWindow::getMaxScrollOffset() const {
    int totalItems;
    if (currentMode_ == VendorMode::BUY) {
        totalItems = static_cast<int>(sortedSlots_.size());
    } else {
        totalItems = static_cast<int>(sellSortedIndices_.size());
    }
    int maxOffset = totalItems - VISIBLE_ROWS;
    return std::max(0, maxOffset);
}

void VendorWindow::scrollUp() {
    if (scrollOffset_ > 0) {
        scrollOffset_--;
    }
}

void VendorWindow::scrollDown() {
    int maxOffset = getMaxScrollOffset();
    if (scrollOffset_ < maxOffset) {
        scrollOffset_++;
    }
}

bool VendorWindow::handleMouseWheel(float delta) {
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

bool VendorWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    shiftHeld_ = shift;

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
            case ButtonId::TabBuy:
                setMode(VendorMode::BUY);
                break;
            case ButtonId::TabSell:
                setMode(VendorMode::SELL);
                break;
            case ButtonId::Buy: {
                // In BUY mode: buy from vendor
                // In SELL mode: sell to vendor
                if (currentMode_ == VendorMode::BUY) {
                    if (selectedSlot_ >= 0) {
                        uint32_t slot = static_cast<uint32_t>(selectedSlot_);
                        uint32_t quantity = shift ? std::max(1u, getMaxAffordableQuantity(slot)) : 1;

                        // Cap quantity at what the item allows (stackable items)
                        auto it = vendorItems_.find(slot);
                        if (it != vendorItems_.end() && it->second.item) {
                            if (!it->second.item->stackable) {
                                quantity = 1;
                            } else {
                                // Ensure stackSize is at least 1 (0 means uninitialized)
                                uint32_t maxStack = std::max(1, static_cast<int>(it->second.item->stackSize));
                                quantity = std::min(quantity, maxStack);
                            }
                        }
                        // Always ensure at least 1 quantity
                        quantity = std::max(1u, quantity);

                        LOG_DEBUG(MOD_UI, "Vendor Buy clicked: slot={} qty={} shift={}", slot, quantity, shift);
                        if (canBuyItem(slot, quantity) && onBuy_) {
                            onBuy_(npcId_, slot, quantity);
                        }
                    }
                } else {
                    // SELL mode
                    const SellableItem* sellItem = getSelectedSellItem();
                    if (sellItem && sellItem->canSell && onSell_) {
                        uint32_t quantity = shift ? sellItem->stackSize : 1;
                        quantity = std::max(1u, std::min(quantity, sellItem->stackSize));
                        LOG_DEBUG(MOD_UI, "Vendor Sell clicked: slot={} qty={} shift={}",
                                  sellItem->inventorySlot, quantity, shift);
                        onSell_(npcId_, sellItem->inventorySlot, quantity);
                    }
                }
                break;
            }
            case ButtonId::Close:
                LOG_DEBUG(MOD_UI, "Vendor Close button clicked");
                close();
                break;
            case ButtonId::ScrollUp:
                scrollUp();
                break;
            case ButtonId::ScrollDown:
                scrollDown();
                break;
            case ButtonId::SortName:
                toggleSortByName();
                if (currentMode_ == VendorMode::SELL) {
                    rebuildSellSortedList();
                }
                break;
            case ButtonId::SortPrice:
                toggleSortByPrice();
                if (currentMode_ == VendorMode::SELL) {
                    rebuildSellSortedList();
                }
                break;
            default:
                break;
        }
        return true;
    }

    // Check list rows - clicking selects the item
    irr::core::recti listArea = getListAreaBounds();
    if (listArea.isPointInside(irr::core::vector2di(x, y))) {
        int relY = y - listArea.UpperLeftCorner.Y - HEADER_HEIGHT;
        if (relY >= 0) {
            int rowIndex = relY / ROW_HEIGHT;
            int itemIndex = scrollOffset_ + rowIndex;

            if (currentMode_ == VendorMode::BUY) {
                // Buy mode: select from vendor items
                if (itemIndex >= 0 && itemIndex < static_cast<int>(sortedSlots_.size())) {
                    selectedSlot_ = static_cast<int32_t>(sortedSlots_[itemIndex]);
                    const auto* item = getItem(selectedSlot_);
                    LOG_DEBUG(MOD_UI, "Vendor item selected: row={} vendorSlot={} name='{}'",
                              rowIndex, selectedSlot_, item ? item->name : "null");
                }
            } else {
                // Sell mode: select from player inventory items
                if (itemIndex >= 0 && itemIndex < static_cast<int>(sellSortedIndices_.size())) {
                    size_t sellIndex = sellSortedIndices_[itemIndex];
                    if (sellIndex < sellableItems_.size()) {
                        selectedSlot_ = static_cast<int32_t>(sellableItems_[sellIndex].inventorySlot);
                        LOG_DEBUG(MOD_UI, "Sell item selected: row={} invSlot={} name='{}'",
                                  rowIndex, selectedSlot_, sellableItems_[sellIndex].name);
                    }
                }
            }
        }
        return true;
    }

    return true;  // Consume click even if not on a row
}

bool VendorWindow::handleMouseUp(int x, int y, bool leftButton) {
    if (draggingScrollbar_ && leftButton) {
        draggingScrollbar_ = false;
        return true;
    }
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool VendorWindow::handleMouseMove(int x, int y) {
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
        highlightedRow_ = -1;
        return false;
    }

    // Check buttons
    hoveredButton_ = getButtonAtPosition(x, y);

    // Check list rows for highlighting
    highlightedRow_ = -1;
    highlightedSlot_ = -1;
    irr::core::recti listArea = getListAreaBounds();
    if (listArea.isPointInside(irr::core::vector2di(x, y))) {
        int relY = y - listArea.UpperLeftCorner.Y - HEADER_HEIGHT;
        if (relY >= 0) {
            int rowIndex = relY / ROW_HEIGHT;
            int itemIndex = scrollOffset_ + rowIndex;

            if (currentMode_ == VendorMode::BUY) {
                // Buy mode: highlight vendor items
                if (itemIndex >= 0 && itemIndex < static_cast<int>(sortedSlots_.size())) {
                    highlightedRow_ = rowIndex;
                    highlightedSlot_ = static_cast<int32_t>(sortedSlots_[itemIndex]);
                }
            } else {
                // Sell mode: highlight player inventory items
                if (itemIndex >= 0 && itemIndex < static_cast<int>(sellSortedIndices_.size())) {
                    size_t sellIndex = sellSortedIndices_[itemIndex];
                    if (sellIndex < sellableItems_.size()) {
                        highlightedRow_ = rowIndex;
                        highlightedSlot_ = static_cast<int32_t>(sellableItems_[sellIndex].inventorySlot);
                    }
                }
            }
        }
    }

    return true;
}

int32_t VendorWindow::getSlotAtPosition(int x, int y) const {
    irr::core::recti listArea = getListAreaBounds();
    if (!listArea.isPointInside(irr::core::vector2di(x, y))) {
        return -1;
    }

    int relY = y - listArea.UpperLeftCorner.Y - HEADER_HEIGHT;
    if (relY < 0) {
        return -1;
    }

    int rowIndex = relY / ROW_HEIGHT;
    int itemIndex = scrollOffset_ + rowIndex;
    if (itemIndex >= 0 && itemIndex < static_cast<int>(sortedSlots_.size())) {
        return static_cast<int32_t>(sortedSlots_[itemIndex]);
    }

    return -1;
}

void VendorWindow::setHighlightedSlot(int32_t slot) {
    highlightedSlot_ = slot;
}

void VendorWindow::clearHighlights() {
    highlightedSlot_ = -1;
    highlightedRow_ = -1;
}

VendorWindow::ButtonId VendorWindow::getButtonAtPosition(int x, int y) const {
    // Check all buttons including tabs
    for (int i = 0; i <= static_cast<int>(ButtonId::TabSell); i++) {
        ButtonId btn = static_cast<ButtonId>(i);
        irr::core::recti bounds = getButtonBounds(btn);
        if (bounds.isPointInside(irr::core::vector2di(x, y))) {
            return btn;
        }
    }
    return ButtonId::None;
}

irr::core::recti VendorWindow::getButtonBounds(ButtonId button) const {
    int windowX = bounds_.UpperLeftCorner.X;
    int windowY = bounds_.UpperLeftCorner.Y;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int buttonHeight = getButtonHeight();

    switch (button) {
        case ButtonId::Buy: {
            int x = windowX + borderWidth + PADDING;
            int y = bounds_.LowerRightCorner.Y - borderWidth - PADDING - buttonHeight;
            return irr::core::recti(x, y, x + BUTTON_WIDTH, y + buttonHeight);
        }
        case ButtonId::Close: {
            int listWidth = NAME_COLUMN_WIDTH + PRICE_COLUMN_WIDTH;
            int x = windowX + borderWidth + PADDING + listWidth - BUTTON_WIDTH;
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
        case ButtonId::SortName: {
            // Name column header is clickable for sorting
            irr::core::recti listArea = getListAreaBounds();
            return irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                                   listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH, listArea.UpperLeftCorner.Y + HEADER_HEIGHT);
        }
        case ButtonId::SortPrice: {
            // Price column header is clickable for sorting
            irr::core::recti listArea = getListAreaBounds();
            return irr::core::recti(listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH, listArea.UpperLeftCorner.Y,
                                   listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH + PRICE_COLUMN_WIDTH, listArea.UpperLeftCorner.Y + HEADER_HEIGHT);
        }
        case ButtonId::TabBuy: {
            // Buy tab is in the tab bar above the list area
            int x = windowX + borderWidth + PADDING;
            int y = windowY + borderWidth + titleBarHeight + PADDING;
            return irr::core::recti(x, y, x + TAB_WIDTH, y + TAB_HEIGHT);
        }
        case ButtonId::TabSell: {
            // Sell tab is next to Buy tab
            int x = windowX + borderWidth + PADDING + TAB_WIDTH + 2;  // 2px gap between tabs
            int y = windowY + borderWidth + titleBarHeight + PADDING;
            return irr::core::recti(x, y, x + TAB_WIDTH, y + TAB_HEIGHT);
        }
        default:
            return irr::core::recti();
    }
}

irr::core::recti VendorWindow::getScrollbarTrackBounds() const {
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int listHeight = HEADER_HEIGHT + VISIBLE_ROWS * ROW_HEIGHT;

    int x = bounds_.UpperLeftCorner.X + borderWidth + PADDING + NAME_COLUMN_WIDTH + PRICE_COLUMN_WIDTH;
    int y = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + PADDING + TAB_HEIGHT;  // Account for tab bar

    return irr::core::recti(x, y, x + SCROLLBAR_WIDTH, y + listHeight);
}

irr::core::recti VendorWindow::getScrollbarThumbBounds() const {
    irr::core::recti track = getScrollbarTrackBounds();

    // Account for up/down buttons
    int trackTop = track.UpperLeftCorner.Y + SCROLL_BUTTON_HEIGHT;
    int trackBottom = track.LowerRightCorner.Y - SCROLL_BUTTON_HEIGHT;
    int trackHeight = trackBottom - trackTop;

    // Get total items based on current mode
    int totalItems = (currentMode_ == VendorMode::BUY)
        ? static_cast<int>(sortedSlots_.size())
        : static_cast<int>(sellSortedIndices_.size());

    if (totalItems <= VISIBLE_ROWS) {
        // No scrolling needed - thumb fills track
        return irr::core::recti(track.UpperLeftCorner.X, trackTop,
                               track.LowerRightCorner.X, trackBottom);
    }

    // Calculate thumb size based on visible ratio
    float visibleRatio = static_cast<float>(VISIBLE_ROWS) / totalItems;
    int thumbHeight = std::max(20, static_cast<int>(trackHeight * visibleRatio));

    // Calculate thumb position based on scroll offset
    int maxOffset = getMaxScrollOffset();
    float scrollRatio = (maxOffset > 0) ? static_cast<float>(scrollOffset_) / maxOffset : 0.0f;
    int thumbY = trackTop + static_cast<int>((trackHeight - thumbHeight) * scrollRatio);

    return irr::core::recti(track.UpperLeftCorner.X, thumbY,
                           track.LowerRightCorner.X, thumbY + thumbHeight);
}

irr::core::recti VendorWindow::getListAreaBounds() const {
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    int x = bounds_.UpperLeftCorner.X + borderWidth + PADDING;
    int y = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + PADDING + TAB_HEIGHT;  // Account for tab bar
    int width = NAME_COLUMN_WIDTH + PRICE_COLUMN_WIDTH;
    int height = HEADER_HEIGHT + VISIBLE_ROWS * ROW_HEIGHT;

    return irr::core::recti(x, y, x + width, y + height);
}

irr::core::recti VendorWindow::getRowBounds(int rowIndex) const {
    irr::core::recti listArea = getListAreaBounds();
    int y = listArea.UpperLeftCorner.Y + HEADER_HEIGHT + rowIndex * ROW_HEIGHT;
    return irr::core::recti(listArea.UpperLeftCorner.X, y,
                           listArea.LowerRightCorner.X, y + ROW_HEIGHT);
}

void VendorWindow::renderTabs(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // Tab colors
    irr::video::SColor activeTabBg(255, 60, 60, 60);
    irr::video::SColor inactiveTabBg(255, 40, 40, 40);
    irr::video::SColor hoverTabBg(255, 70, 70, 70);
    irr::video::SColor tabText(255, 220, 220, 220);
    irr::video::SColor activeTabText(255, 255, 255, 180);

    // Buy tab
    irr::core::recti buyTabRect = getButtonBounds(ButtonId::TabBuy);
    bool buyHovered = (hoveredButton_ == ButtonId::TabBuy);
    bool buyActive = (currentMode_ == VendorMode::BUY);

    irr::video::SColor buyBg = buyActive ? activeTabBg : (buyHovered ? hoverTabBg : inactiveTabBg);
    driver->draw2DRectangle(buyBg, buyTabRect);

    // Buy tab border (highlight top/left for active)
    if (buyActive) {
        irr::video::SColor highlight(255, 100, 100, 100);
        driver->draw2DRectangle(highlight,
            irr::core::recti(buyTabRect.UpperLeftCorner.X, buyTabRect.UpperLeftCorner.Y,
                            buyTabRect.LowerRightCorner.X, buyTabRect.UpperLeftCorner.Y + 2));
    }

    // Buy tab text
    irr::video::SColor buyTextColor = buyActive ? activeTabText : tabText;
    font->draw(L"Buy",
              irr::core::recti(buyTabRect.UpperLeftCorner.X + (TAB_WIDTH - 24) / 2,
                              buyTabRect.UpperLeftCorner.Y + (TAB_HEIGHT - 12) / 2,
                              buyTabRect.LowerRightCorner.X,
                              buyTabRect.LowerRightCorner.Y),
              buyTextColor);

    // Sell tab
    irr::core::recti sellTabRect = getButtonBounds(ButtonId::TabSell);
    bool sellHovered = (hoveredButton_ == ButtonId::TabSell);
    bool sellActive = (currentMode_ == VendorMode::SELL);

    irr::video::SColor sellBg = sellActive ? activeTabBg : (sellHovered ? hoverTabBg : inactiveTabBg);
    driver->draw2DRectangle(sellBg, sellTabRect);

    // Sell tab border (highlight top/left for active)
    if (sellActive) {
        irr::video::SColor highlight(255, 100, 100, 100);
        driver->draw2DRectangle(highlight,
            irr::core::recti(sellTabRect.UpperLeftCorner.X, sellTabRect.UpperLeftCorner.Y,
                            sellTabRect.LowerRightCorner.X, sellTabRect.UpperLeftCorner.Y + 2));
    }

    // Sell tab text
    irr::video::SColor sellTextColor = sellActive ? activeTabText : tabText;
    font->draw(L"Sell",
              irr::core::recti(sellTabRect.UpperLeftCorner.X + (TAB_WIDTH - 28) / 2,
                              sellTabRect.UpperLeftCorner.Y + (TAB_HEIGHT - 12) / 2,
                              sellTabRect.LowerRightCorner.X,
                              sellTabRect.LowerRightCorner.Y),
              sellTextColor);
}

void VendorWindow::renderSellListView(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui) {
    irr::core::recti listArea = getListAreaBounds();
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // List background
    irr::video::SColor listBg(255, 40, 40, 40);
    irr::core::recti listRect(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y + HEADER_HEIGHT,
                              listArea.LowerRightCorner.X, listArea.LowerRightCorner.Y);
    driver->draw2DRectangle(listBg, listRect);

    // Render visible rows
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int itemIndex = scrollOffset_ + i;
        if (itemIndex >= static_cast<int>(sellSortedIndices_.size())) {
            break;
        }

        size_t sellIndex = sellSortedIndices_[itemIndex];
        if (sellIndex >= sellableItems_.size()) {
            continue;
        }

        const SellableItem& item = sellableItems_[sellIndex];
        irr::core::recti rowBounds = getRowBounds(i);

        // Alternating row colors
        irr::video::SColor rowBg = (i % 2 == 0) ?
            irr::video::SColor(255, 45, 45, 45) :
            irr::video::SColor(255, 50, 50, 50);

        // Selection highlight
        bool isSelected = (static_cast<int32_t>(item.inventorySlot) == selectedSlot_);
        if (isSelected) {
            rowBg = irr::video::SColor(255, 80, 80, 50);
        }

        // Hover highlight
        bool isHovered = (i == highlightedRow_);
        if (isHovered && !isSelected) {
            rowBg = irr::video::SColor(255, 60, 60, 60);
        }

        // Gray out non-sellable items
        if (!item.canSell) {
            rowBg = irr::video::SColor(255, 50, 40, 40);
        }

        driver->draw2DRectangle(rowBg, rowBounds);

        // Draw icon
        int iconX = rowBounds.UpperLeftCorner.X + 2;
        int iconY = rowBounds.UpperLeftCorner.Y + (ROW_HEIGHT - ICON_SIZE) / 2;
        irr::core::recti iconRect(iconX, iconY, iconX + ICON_SIZE, iconY + ICON_SIZE);

        irr::video::ITexture* iconTexture = nullptr;
        if (iconLookupCallback_) {
            iconTexture = iconLookupCallback_(item.iconId);
        }

        if (iconTexture) {
            driver->draw2DImage(iconTexture, iconRect,
                               irr::core::recti(0, 0, iconTexture->getSize().Width, iconTexture->getSize().Height),
                               nullptr, nullptr, true);
        } else {
            // Placeholder icon
            irr::video::SColor placeholderColor(255, 100, 100, 100);
            driver->draw2DRectangle(placeholderColor, iconRect);
        }

        // Draw item name with stack size
        std::wstring itemName(item.name.begin(), item.name.end());
        if (item.stackSize > 1) {
            itemName += L" (" + std::to_wstring(item.stackSize) + L")";
        }
        // Truncate if too long
        if (itemName.length() > 25) {
            itemName = itemName.substr(0, 22) + L"...";
        }

        // Color: gray for non-sellable, normal for sellable
        irr::video::SColor nameColor = item.canSell ?
            irr::video::SColor(255, 220, 220, 180) :
            irr::video::SColor(255, 120, 120, 120);

        int textY = rowBounds.UpperLeftCorner.Y + (ROW_HEIGHT - 12) / 2;
        font->draw(itemName.c_str(),
                  irr::core::recti(iconX + ICON_SIZE + 4, textY,
                                  rowBounds.UpperLeftCorner.X + NAME_COLUMN_WIDTH - 4, rowBounds.LowerRightCorner.Y),
                  nameColor);

        // Draw sell price
        uint32_t sellPrice = calculateSellPrice(item.basePrice, 1);
        std::string priceStr = formatPrice(static_cast<int32_t>(sellPrice));
        std::wstring priceWStr(priceStr.begin(), priceStr.end());

        irr::video::SColor priceColor = item.canSell ?
            irr::video::SColor(255, 200, 200, 200) :
            irr::video::SColor(255, 120, 120, 120);

        font->draw(priceWStr.c_str(),
                  irr::core::recti(rowBounds.UpperLeftCorner.X + NAME_COLUMN_WIDTH + 4, textY,
                                  rowBounds.LowerRightCorner.X - 4, rowBounds.LowerRightCorner.Y),
                  priceColor);
    }

    // Draw column separator in list area
    irr::video::SColor separatorColor(255, 60, 60, 60);
    int separatorX = listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH;
    driver->draw2DRectangle(separatorColor,
        irr::core::recti(separatorX, listArea.UpperLeftCorner.Y + HEADER_HEIGHT,
                        separatorX + 1, listArea.LowerRightCorner.Y));

    // Draw list border
    irr::video::SColor borderColor = getBorderDarkColor();
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                        listArea.LowerRightCorner.X, listArea.UpperLeftCorner.Y + 1));
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                        listArea.UpperLeftCorner.X + 1, listArea.LowerRightCorner.Y));
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.LowerRightCorner.Y - 1,
                        listArea.LowerRightCorner.X, listArea.LowerRightCorner.Y));
}

void VendorWindow::renderContent(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui) {
    renderTabs(driver, gui);
    renderColumnHeaders(driver, gui);

    if (currentMode_ == VendorMode::BUY) {
        renderListView(driver, gui);
    } else {
        renderSellListView(driver, gui);
    }

    renderScrollbar(driver);
    renderButtons(driver, gui);
}

void VendorWindow::renderColumnHeaders(irr::video::IVideoDriver* driver,
                                        irr::gui::IGUIEnvironment* gui) {
    irr::core::recti listArea = getListAreaBounds();
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // Header background
    irr::video::SColor headerBg(255, 60, 60, 60);
    irr::core::recti headerRect(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                                listArea.LowerRightCorner.X, listArea.UpperLeftCorner.Y + HEADER_HEIGHT);
    driver->draw2DRectangle(headerBg, headerRect);

    // Name column header
    irr::core::recti nameHeaderRect = getButtonBounds(ButtonId::SortName);
    bool nameHovered = (hoveredButton_ == ButtonId::SortName);
    if (nameHovered) {
        irr::video::SColor hoverColor(255, 80, 80, 80);
        driver->draw2DRectangle(hoverColor, nameHeaderRect);
    }

    // Name header text with sort indicator
    std::wstring nameText = L"Item";
    if (sortMode_ == VendorSortMode::NameAsc) {
        nameText += L" ^";
    } else if (sortMode_ == VendorSortMode::NameDesc) {
        nameText += L" v";
    }
    irr::video::SColor headerTextColor(255, 220, 220, 220);
    font->draw(nameText.c_str(),
              irr::core::recti(nameHeaderRect.UpperLeftCorner.X + 4,
                              nameHeaderRect.UpperLeftCorner.Y + 3,
                              nameHeaderRect.LowerRightCorner.X,
                              nameHeaderRect.LowerRightCorner.Y),
              headerTextColor);

    // Price column header
    irr::core::recti priceHeaderRect = getButtonBounds(ButtonId::SortPrice);
    bool priceHovered = (hoveredButton_ == ButtonId::SortPrice);
    if (priceHovered) {
        irr::video::SColor hoverColor(255, 80, 80, 80);
        driver->draw2DRectangle(hoverColor, priceHeaderRect);
    }

    // Price header text with sort indicator
    std::wstring priceText = L"Price";
    if (sortMode_ == VendorSortMode::PriceAsc) {
        priceText += L" ^";
    } else if (sortMode_ == VendorSortMode::PriceDesc) {
        priceText += L" v";
    }
    font->draw(priceText.c_str(),
              irr::core::recti(priceHeaderRect.UpperLeftCorner.X + 4,
                              priceHeaderRect.UpperLeftCorner.Y + 3,
                              priceHeaderRect.LowerRightCorner.X,
                              priceHeaderRect.LowerRightCorner.Y),
              headerTextColor);

    // Header border
    irr::video::SColor borderColor = getBorderDarkColor();
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y + HEADER_HEIGHT - 1,
                        listArea.LowerRightCorner.X, listArea.UpperLeftCorner.Y + HEADER_HEIGHT));

    // Column separator
    int separatorX = listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH;
    driver->draw2DRectangle(borderColor,
        irr::core::recti(separatorX, listArea.UpperLeftCorner.Y,
                        separatorX + 1, listArea.UpperLeftCorner.Y + HEADER_HEIGHT));
}

void VendorWindow::renderListView(irr::video::IVideoDriver* driver,
                                   irr::gui::IGUIEnvironment* gui) {
    irr::core::recti listArea = getListAreaBounds();
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // List background
    irr::video::SColor listBg(255, 40, 40, 40);
    irr::core::recti listRect(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y + HEADER_HEIGHT,
                              listArea.LowerRightCorner.X, listArea.LowerRightCorner.Y);
    driver->draw2DRectangle(listBg, listRect);

    // Render visible rows
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int itemIndex = scrollOffset_ + i;
        if (itemIndex >= static_cast<int>(sortedSlots_.size())) {
            break;
        }

        uint32_t vendorSlot = sortedSlots_[itemIndex];
        auto it = vendorItems_.find(vendorSlot);
        if (it == vendorItems_.end() || !it->second.item) {
            continue;
        }

        const auto& item = it->second.item;
        irr::core::recti rowBounds = getRowBounds(i);

        // Alternating row colors
        irr::video::SColor rowBg = (i % 2 == 0) ?
            irr::video::SColor(255, 45, 45, 45) :
            irr::video::SColor(255, 50, 50, 50);

        // Selection highlight
        bool isSelected = (static_cast<int32_t>(vendorSlot) == selectedSlot_);
        if (isSelected) {
            rowBg = irr::video::SColor(255, 80, 80, 50);
        }

        // Hover highlight
        bool isHovered = (i == highlightedRow_);
        if (isHovered && !isSelected) {
            rowBg = irr::video::SColor(255, 60, 60, 60);
        }

        driver->draw2DRectangle(rowBg, rowBounds);

        // Draw icon
        int iconX = rowBounds.UpperLeftCorner.X + 2;
        int iconY = rowBounds.UpperLeftCorner.Y + (ROW_HEIGHT - ICON_SIZE) / 2;
        irr::core::recti iconRect(iconX, iconY, iconX + ICON_SIZE, iconY + ICON_SIZE);

        irr::video::ITexture* iconTexture = nullptr;
        if (iconLookupCallback_) {
            iconTexture = iconLookupCallback_(item->icon);
        }

        if (iconTexture) {
            driver->draw2DImage(iconTexture, iconRect,
                               irr::core::recti(0, 0, iconTexture->getSize().Width, iconTexture->getSize().Height),
                               nullptr, nullptr, true);
        } else {
            // Placeholder icon
            irr::video::SColor placeholderColor(255, 100, 100, 100);
            driver->draw2DRectangle(placeholderColor, iconRect);
        }

        // Draw item name
        std::wstring itemName(item->name.begin(), item->name.end());
        // Truncate if too long
        if (itemName.length() > 25) {
            itemName = itemName.substr(0, 22) + L"...";
        }

        // Color based on affordability
        bool canAfford = (item->price <= playerMoneyCopper_);
        irr::video::SColor nameColor = canAfford ?
            irr::video::SColor(255, 220, 220, 180) :
            irr::video::SColor(255, 180, 100, 100);

        int textY = rowBounds.UpperLeftCorner.Y + (ROW_HEIGHT - 12) / 2;
        font->draw(itemName.c_str(),
                  irr::core::recti(iconX + ICON_SIZE + 4, textY,
                                  rowBounds.UpperLeftCorner.X + NAME_COLUMN_WIDTH - 4, rowBounds.LowerRightCorner.Y),
                  nameColor);

        // Draw price
        std::string priceStr = formatPrice(item->price);
        std::wstring priceWStr(priceStr.begin(), priceStr.end());

        irr::video::SColor priceColor = canAfford ?
            irr::video::SColor(255, 200, 200, 200) :
            irr::video::SColor(255, 200, 80, 80);

        font->draw(priceWStr.c_str(),
                  irr::core::recti(rowBounds.UpperLeftCorner.X + NAME_COLUMN_WIDTH + 4, textY,
                                  rowBounds.LowerRightCorner.X - 4, rowBounds.LowerRightCorner.Y),
                  priceColor);
    }

    // Draw column separator in list area
    irr::video::SColor separatorColor(255, 60, 60, 60);
    int separatorX = listArea.UpperLeftCorner.X + NAME_COLUMN_WIDTH;
    driver->draw2DRectangle(separatorColor,
        irr::core::recti(separatorX, listArea.UpperLeftCorner.Y + HEADER_HEIGHT,
                        separatorX + 1, listArea.LowerRightCorner.Y));

    // Draw list border
    irr::video::SColor borderColor = getBorderDarkColor();
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                        listArea.LowerRightCorner.X, listArea.UpperLeftCorner.Y + 1));
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.UpperLeftCorner.Y,
                        listArea.UpperLeftCorner.X + 1, listArea.LowerRightCorner.Y));
    driver->draw2DRectangle(borderColor,
        irr::core::recti(listArea.UpperLeftCorner.X, listArea.LowerRightCorner.Y - 1,
                        listArea.LowerRightCorner.X, listArea.LowerRightCorner.Y));
}

void VendorWindow::renderButtons(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui) {
    // Buy/Sell button - changes label based on mode
    bool buttonHighlighted = (hoveredButton_ == ButtonId::Buy);
    bool buttonDisabled;
    const wchar_t* buttonLabel;

    if (currentMode_ == VendorMode::BUY) {
        // Buy mode: disabled if no selection or can't afford
        buttonDisabled = (selectedSlot_ < 0) || !canBuyItem(static_cast<uint32_t>(selectedSlot_), 1);
        buttonLabel = L"Buy";
    } else {
        // Sell mode: disabled if no selection or item not sellable
        const SellableItem* sellItem = getSelectedSellItem();
        buttonDisabled = (selectedSlot_ < 0) || !sellItem || !sellItem->canSell;
        buttonLabel = L"Sell";
    }

    irr::core::recti buyBounds = getButtonBounds(ButtonId::Buy);
    drawButton(driver, gui, buyBounds, buttonLabel, buttonHighlighted && !buttonDisabled, buttonDisabled);

    // Close button
    bool closeHighlighted = (hoveredButton_ == ButtonId::Close);
    irr::core::recti closeBounds = getButtonBounds(ButtonId::Close);
    drawButton(driver, gui, closeBounds, L"Close", closeHighlighted);
}

void VendorWindow::renderScrollbar(irr::video::IVideoDriver* driver) {
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
    // Draw up arrow
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
    size_t totalItems = (currentMode_ == VendorMode::BUY)
        ? sortedSlots_.size()
        : sellSortedIndices_.size();
    if (totalItems > static_cast<size_t>(VISIBLE_ROWS)) {
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

} // namespace ui
} // namespace eqt
