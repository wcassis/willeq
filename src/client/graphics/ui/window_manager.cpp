#include "client/graphics/ui/window_manager.h"
#include "client/graphics/entity_renderer.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include <algorithm>
#include <iostream>
#include "common/logging.h"

namespace eqt {
namespace ui {

WindowManager::WindowManager() = default;
WindowManager::~WindowManager() = default;

irr::video::ITexture* WindowManager::getItemIcon(uint32_t iconId) {
    return iconLoader_.getIcon(iconId);
}

void WindowManager::init(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui,
                        inventory::InventoryManager* invManager,
                        int screenWidth, int screenHeight,
                        const std::string& eqClientPath) {
    driver_ = driver;
    gui_ = gui;
    invManager_ = invManager;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Initialize icon loader
    if (!eqClientPath.empty()) {
        if (iconLoader_.init(driver, eqClientPath)) {
            LOG_DEBUG(MOD_UI, "Item icon loader initialized");
        } else {
            LOG_ERROR(MOD_UI, "Failed to initialize item icon loader");
        }
    }

    // Create inventory window
    inventoryWindow_ = std::make_unique<InventoryWindow>(invManager_);
    positionInventoryWindow();

    // Set up callbacks
    inventoryWindow_->setBagClickCallback([this](int16_t generalSlot) {
        handleBagOpenClick(generalSlot);
    });

    inventoryWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });

    inventoryWindow_->setSlotClickCallback([this](int16_t slotId, bool shift, bool ctrl) {
        handleSlotClick(slotId, shift, ctrl);
    });

    inventoryWindow_->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
        handleSlotHover(slotId, mouseX, mouseY);
    });

    inventoryWindow_->setDestroyClickCallback([this]() {
        handleDestroyClick();
    });

    inventoryWindow_->setCloseCallback([this]() {
        closeAllBagWindows();
    });

    // Create loot window (initially hidden)
    lootWindow_ = std::make_unique<LootWindow>(invManager_, this);
    lootWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });
    positionLootWindow();

    // Create vendor window (initially hidden)
    vendorWindow_ = std::make_unique<VendorWindow>(invManager_, this);
    vendorWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });

    // Create chat window (visible by default)
    chatWindow_ = std::make_unique<ChatWindow>();
    chatWindow_->init(screenWidth, screenHeight);
}

void WindowManager::onResize(int screenWidth, int screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    positionInventoryWindow();
    positionLootWindow();
    tileBagWindows();
    if (chatWindow_) {
        chatWindow_->onResize(screenWidth, screenHeight);
    }
}

// ============================================================================
// UI Settings Management
// ============================================================================

void WindowManager::toggleUILock() {
    UISettings::instance().toggleUILock();
    bool locked = UISettings::instance().isUILocked();
    LOG_INFO(MOD_UI, "UI {} - windows {}", locked ? "locked" : "unlocked",
             locked ? "cannot be moved" : "can be moved");

    // Show status message in chat window
    if (chatWindow_) {
        std::string message = locked ?
            "UI Locked - Press Ctrl+L to unlock and move windows" :
            "UI Unlocked - Drag windows to reposition, Ctrl+S to save layout";
        chatWindow_->addSystemMessage(message, ChatChannel::System);
    }

    // Clear hover states when locking
    if (locked) {
        updateWindowHoverStates(mouseX_, mouseY_);
    }
}

bool WindowManager::isUILocked() const {
    return UISettings::instance().isUILocked();
}

bool WindowManager::saveUILayout(const std::string& path) {
    // Collect current window positions into UISettings
    collectWindowPositions();

    // Save to file
    std::string savePath = path.empty() ? "config/ui_settings.json" : path;
    bool success = UISettings::instance().saveToFile(savePath);

    if (success) {
        LOG_INFO(MOD_UI, "UI layout saved to {}", savePath);
        if (chatWindow_) {
            chatWindow_->addSystemMessage("UI layout saved to " + savePath, ChatChannel::System);
        }
    } else {
        LOG_ERROR(MOD_UI, "Failed to save UI layout to {}", savePath);
        if (chatWindow_) {
            chatWindow_->addSystemMessage("Failed to save UI layout", ChatChannel::System);
        }
    }

    return success;
}

bool WindowManager::loadUILayout(const std::string& path) {
    std::string loadPath = path.empty() ? "config/ui_settings.json" : path;
    bool success = UISettings::instance().loadFromFile(loadPath);

    if (success) {
        // Apply loaded settings to all windows
        applyWindowPositions();
        LOG_INFO(MOD_UI, "UI layout loaded from {}", loadPath);
        if (chatWindow_) {
            chatWindow_->addSystemMessage("UI layout loaded from " + loadPath, ChatChannel::System);
        }
    } else {
        LOG_ERROR(MOD_UI, "Failed to load UI layout from {}", loadPath);
        if (chatWindow_) {
            chatWindow_->addSystemMessage("Failed to load UI layout", ChatChannel::System);
        }
    }

    return success;
}

void WindowManager::resetUIToDefaults() {
    UISettings::instance().resetToDefaults();
    applyWindowPositions();
    LOG_INFO(MOD_UI, "UI layout reset to defaults");
    if (chatWindow_) {
        chatWindow_->addSystemMessage("UI layout reset to defaults", ChatChannel::System);
    }
}

void WindowManager::applyUISettings() {
    applyWindowPositions();
}

void WindowManager::collectWindowPositions() {
    auto& settings = UISettings::instance();

    // Chat window
    if (chatWindow_) {
        settings.chat().window.x = chatWindow_->getX();
        settings.chat().window.y = chatWindow_->getY();
        settings.chat().window.width = chatWindow_->getWidth();
        settings.chat().window.height = chatWindow_->getHeight();
        settings.chat().window.visible = chatWindow_->isVisible();
    }

    // Inventory window
    if (inventoryWindow_) {
        settings.inventory().window.x = inventoryWindow_->getX();
        settings.inventory().window.y = inventoryWindow_->getY();
        settings.inventory().window.visible = inventoryWindow_->isVisible();
    }

    // Loot window
    if (lootWindow_) {
        settings.loot().window.x = lootWindow_->getX();
        settings.loot().window.y = lootWindow_->getY();
        settings.loot().window.visible = lootWindow_->isOpen();
    }

    // Buff window
    if (buffWindow_) {
        settings.buff().window.x = buffWindow_->getX();
        settings.buff().window.y = buffWindow_->getY();
        settings.buff().window.visible = buffWindow_->isVisible();
    }

    // Group window
    if (groupWindow_) {
        settings.group().window.x = groupWindow_->getX();
        settings.group().window.y = groupWindow_->getY();
        settings.group().window.visible = groupWindow_->isVisible();
    }

    // Player status window
    if (playerStatusWindow_) {
        settings.playerStatus().window.x = playerStatusWindow_->getX();
        settings.playerStatus().window.y = playerStatusWindow_->getY();
        settings.playerStatus().window.visible = playerStatusWindow_->isVisible();
    }

    // Spell gem panel
    if (spellGemPanel_) {
        settings.spellGems().x = spellGemPanel_->getX();
        settings.spellGems().y = spellGemPanel_->getY();
        settings.spellGems().visible = spellGemPanel_->isVisible();
    }

    // Spellbook window
    if (spellBookWindow_) {
        settings.spellBook().window.x = spellBookWindow_->getX();
        settings.spellBook().window.y = spellBookWindow_->getY();
        settings.spellBook().window.visible = spellBookWindow_->isVisible();
    }

    LOG_DEBUG(MOD_UI, "Collected window positions for saving");
}

void WindowManager::applyWindowPositions() {
    const auto& settings = UISettings::instance();

    // Chat window
    if (chatWindow_) {
        int x = UISettings::resolvePosition(settings.chat().window.x, screenWidth_, 10);
        int y = UISettings::resolvePosition(settings.chat().window.y, screenHeight_, screenHeight_ - 182);
        chatWindow_->setPosition(x, y);
        if (settings.chat().window.width > 0 && settings.chat().window.height > 0) {
            chatWindow_->setSize(settings.chat().window.width, settings.chat().window.height);
        }
    }

    // Inventory window
    if (inventoryWindow_) {
        int x = UISettings::resolvePosition(settings.inventory().window.x, screenWidth_, INVENTORY_X);
        int y = UISettings::resolvePosition(settings.inventory().window.y, screenHeight_, INVENTORY_Y);
        inventoryWindow_->setPosition(x, y);
    }

    // Buff window
    if (buffWindow_) {
        int x = UISettings::resolvePosition(settings.buff().window.x, screenWidth_, 650);
        int y = UISettings::resolvePosition(settings.buff().window.y, screenHeight_, 53);
        buffWindow_->setPosition(x, y);
        if (settings.buff().window.visible) {
            buffWindow_->show();
        }
    }

    // Group window
    if (groupWindow_) {
        int defaultX = screenWidth_ - 150;
        int x = UISettings::resolvePosition(settings.group().window.x, screenWidth_, defaultX);
        int y = UISettings::resolvePosition(settings.group().window.y, screenHeight_, 210);
        groupWindow_->setPosition(x, y);
    }

    // Player status window
    if (playerStatusWindow_) {
        int x = UISettings::resolvePosition(settings.playerStatus().window.x, screenWidth_, 10);
        int y = UISettings::resolvePosition(settings.playerStatus().window.y, screenHeight_, 10);
        playerStatusWindow_->setPosition(x, y);
        if (settings.playerStatus().window.visible) {
            playerStatusWindow_->show();
        }
    }

    // Spell gem panel
    if (spellGemPanel_) {
        int x = settings.spellGems().x;
        int y = settings.spellGems().y;
        if (y == -1) {
            // Vertically centered
            int panelHeight = 8 * (settings.spellGems().gemHeight + settings.spellGems().gemSpacing) +
                              settings.spellGems().panelPadding * 2;
            y = (screenHeight_ - panelHeight) / 2;
        }
        spellGemPanel_->setPosition(x, y);
        if (settings.spellGems().visible) {
            spellGemPanel_->show();
        }
    }

    // Spellbook window
    if (spellBookWindow_) {
        int x = UISettings::resolvePosition(settings.spellBook().window.x, screenWidth_, 106);
        int y = UISettings::resolvePosition(settings.spellBook().window.y, screenHeight_, 131);
        spellBookWindow_->setPosition(x, y);
    }

    LOG_DEBUG(MOD_UI, "Applied window positions from UISettings");
}

void WindowManager::toggleInventory() {
    if (inventoryWindow_->isVisible()) {
        closeInventory();
    } else {
        openInventory();
    }
}

void WindowManager::openInventory() {
    inventoryWindow_->show();
}

void WindowManager::closeInventory() {
    // Return cursor item if holding one
    if (invManager_->hasCursorItem()) {
        invManager_->returnCursorItem();
    }

    inventoryWindow_->hide();
    closeAllBagWindows();
    tooltip_.clear();
    closeConfirmDialog();
}

void WindowManager::closeAllWindows() {
    closeInventory();
    closeLootWindow();
    closeVendorWindow();
}

void WindowManager::toggleBagWindow(int16_t generalSlot) {
    if (isBagWindowOpen(generalSlot)) {
        closeBagWindow(generalSlot);
    } else {
        openBagWindow(generalSlot);
    }
}

void WindowManager::openBagWindow(int16_t generalSlot) {
    // Check if slot contains a bag
    const inventory::ItemInstance* item = invManager_->getItem(generalSlot);
    if (!item || !item->isContainer()) {
        return;
    }

    // Don't open if already open
    if (isBagWindowOpen(generalSlot)) {
        return;
    }

    // Create bag window
    auto bagWindow = std::make_unique<BagWindow>(generalSlot, item->bagSlots, invManager_);

    // Set up callbacks
    bagWindow->setSlotClickCallback([this](int16_t slotId, bool shift, bool ctrl) {
        handleBagSlotClick(slotId, shift, ctrl);
    });

    bagWindow->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
        handleSlotHover(slotId, mouseX, mouseY);
    });

    bagWindow->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });

    bagWindow->show();
    bagWindows_[generalSlot] = std::move(bagWindow);

    tileBagWindows();
}

void WindowManager::closeBagWindow(int16_t generalSlot) {
    auto it = bagWindows_.find(generalSlot);
    if (it != bagWindows_.end()) {
        bagWindows_.erase(it);
        tileBagWindows();
    }
}

void WindowManager::closeAllBagWindows() {
    bagWindows_.clear();
}

bool WindowManager::isBagWindowOpen(int16_t generalSlot) const {
    return bagWindows_.find(generalSlot) != bagWindows_.end();
}

// Loot window management
void WindowManager::openLootWindow(uint16_t corpseId, const std::string& corpseName) {
    if (!lootWindow_) {
        return;
    }

    // Set up callbacks before opening
    lootWindow_->setOnLootItem(onLootItemCallback_);
    lootWindow_->setOnLootAll(onLootAllCallback_);
    lootWindow_->setOnDestroyAll(onDestroyAllCallback_);
    lootWindow_->setOnClose(onLootCloseCallback_);

    lootWindow_->open(corpseId, corpseName);
    positionLootWindow();
}

void WindowManager::closeLootWindow() {
    // Clear loot cursor if we have one
    cancelLootCursor();

    if (lootWindow_ && lootWindow_->isOpen()) {
        lootWindow_->close();
    }
}

bool WindowManager::isLootWindowOpen() const {
    return lootWindow_ && lootWindow_->isOpen();
}

void WindowManager::addLootItem(int16_t slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (lootWindow_) {
        lootWindow_->addLootItem(slot, std::move(item));
    }
}

void WindowManager::removeLootItem(int16_t slot) {
    // Clear loot cursor if it matches the removed item
    if (lootCursorSlot_ == slot) {
        cancelLootCursor();
    }

    if (lootWindow_) {
        lootWindow_->removeItem(slot);
    }
}

void WindowManager::clearLootItems() {
    if (lootWindow_) {
        lootWindow_->clearLoot();
    }
}

void WindowManager::setOnLootItem(LootItemCallback callback) {
    onLootItemCallback_ = callback;
    if (lootWindow_) {
        lootWindow_->setOnLootItem(callback);
    }
}

void WindowManager::setOnLootAll(LootAllCallback callback) {
    onLootAllCallback_ = callback;
    if (lootWindow_) {
        lootWindow_->setOnLootAll(callback);
    }
}

void WindowManager::setOnDestroyAll(DestroyAllCallback callback) {
    onDestroyAllCallback_ = callback;
    if (lootWindow_) {
        lootWindow_->setOnDestroyAll(callback);
    }
}

void WindowManager::setOnLootClose(LootCloseCallback callback) {
    onLootCloseCallback_ = callback;
    if (lootWindow_) {
        lootWindow_->setOnClose(callback);
    }
}

// Vendor window management
void WindowManager::openVendorWindow(uint16_t npcId, const std::string& vendorName, float sellRate) {
    if (!vendorWindow_) {
        return;
    }

    // Set up callbacks before opening
    vendorWindow_->setOnBuy(onVendorBuyCallback_);
    vendorWindow_->setOnSell(onVendorSellCallback_);
    vendorWindow_->setOnClose(onVendorCloseCallback_);

    vendorWindow_->open(npcId, vendorName, sellRate);
    positionVendorWindow();

    // Populate sellable items for sell mode
    refreshVendorSellableItems();
}

void WindowManager::closeVendorWindow() {
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        vendorWindow_->close();
    }
}

bool WindowManager::isVendorWindowOpen() const {
    return vendorWindow_ && vendorWindow_->isOpen();
}

void WindowManager::addVendorItem(uint32_t slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (vendorWindow_) {
        vendorWindow_->addVendorItem(slot, std::move(item));
    }
}

void WindowManager::removeVendorItem(uint32_t slot) {
    if (vendorWindow_) {
        vendorWindow_->removeItem(slot);
    }
}

void WindowManager::clearVendorItems() {
    if (vendorWindow_) {
        vendorWindow_->clearItems();
    }
}

void WindowManager::refreshVendorSellableItems() {
    if (!vendorWindow_ || !invManager_) {
        return;
    }

    // Get sellable items from inventory
    auto inventoryItems = invManager_->getSellableItems();

    // Convert to SellableItem structs
    std::vector<SellableItem> sellableItems;
    sellableItems.reserve(inventoryItems.size());

    for (const auto& [slot, item] : inventoryItems) {
        SellableItem sellItem;
        sellItem.inventorySlot = static_cast<uint32_t>(slot);
        sellItem.name = item->name;
        sellItem.iconId = item->icon;
        sellItem.basePrice = item->price;
        sellItem.stackSize = item->stackable ? static_cast<uint32_t>(item->quantity) : 1;
        sellItem.isStackable = item->stackable;
        sellItem.canSell = !item->noDrop;  // Already filtered, but double-check
        sellableItems.push_back(sellItem);
    }

    vendorWindow_->setSellableItems(sellableItems);
}

void WindowManager::setOnVendorBuy(VendorBuyCallback callback) {
    onVendorBuyCallback_ = callback;
    if (vendorWindow_) {
        vendorWindow_->setOnBuy(callback);
    }
}

void WindowManager::setOnVendorSell(VendorSellCallback callback) {
    onVendorSellCallback_ = callback;
    if (vendorWindow_) {
        vendorWindow_->setOnSell(callback);
    }
}

void WindowManager::setOnVendorClose(VendorCloseCallback callback) {
    onVendorCloseCallback_ = callback;
    if (vendorWindow_) {
        vendorWindow_->setOnClose(callback);
    }
}

bool WindowManager::handleKeyPress(irr::EKEY_CODE key, bool shift, bool ctrl) {
    // Handle UI management key bindings (Ctrl+key)
    if (ctrl) {
        switch (key) {
            case irr::KEY_KEY_L:
                // Ctrl+L: Toggle UI lock
                toggleUILock();
                return true;

            case irr::KEY_KEY_S:
                // Ctrl+S: Save UI layout
                saveUILayout();
                return true;

            case irr::KEY_KEY_R:
                if (shift) {
                    // Ctrl+Shift+R: Reset UI to defaults
                    resetUIToDefaults();
                    return true;
                }
                break;

            default:
                break;
        }
    }

    // Handle confirmation dialog first
    if (isConfirmDialogOpen()) {
        if (key == irr::KEY_ESCAPE) {
            closeConfirmDialog();
            return true;
        }
        if (key == irr::KEY_RETURN || key == irr::KEY_KEY_Y) {
            // Confirm
            if (confirmDialogType_ == ConfirmDialogType::DestroyItem) {
                invManager_->destroyCursorItem();
            }
            closeConfirmDialog();
            return true;
        }
        if (key == irr::KEY_KEY_N) {
            closeConfirmDialog();
            return true;
        }
        return true;  // Consume all keys when dialog is open
    }

    // ESC clears spell cursor if active
    if (key == irr::KEY_ESCAPE && spellCursor_.active) {
        clearSpellCursor();
        return true;
    }

    // Route to chat window if chat input is focused
    if (chatWindow_ && chatWindow_->isInputFocused()) {
        // ESC unfocuses chat input
        if (key == irr::KEY_ESCAPE) {
            chatWindow_->unfocusInput();
            return true;
        }
        // Route character to chat window (shift provides the character)
        return chatWindow_->handleKeyPress(key, 0, shift, ctrl);
    }

    // Enter key focuses chat input
    if (key == irr::KEY_RETURN && chatWindow_) {
        chatWindow_->focusInput();
        return true;
    }

    // I key toggles inventory
    if (key == irr::KEY_KEY_I) {
        toggleInventory();
        return true;
    }

    // ESC closes inventory if open
    if (key == irr::KEY_ESCAPE && isInventoryOpen()) {
        closeInventory();
        return true;
    }

    // 1-8 keys cast from spell gems
    if (spellGemPanel_ && spellGemPanel_->isVisible()) {
        if (spellGemPanel_->handleKeyPress(static_cast<int>(key))) {
            return true;
        }
    }

    return false;
}

bool WindowManager::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    mouseX_ = x;
    mouseY_ = y;

    // Right-click clears spell cursor if active
    if (!leftButton && spellCursor_.active) {
        clearSpellCursor();
        return true;
    }

    if (!leftButton) {
        return false;
    }

    // Handle quantity slider first (modal)
    if (quantitySliderActive_) {
        // Check OK button
        if (quantitySliderOkBounds_.isPointInside(irr::core::vector2di(x, y))) {
            confirmQuantitySlider();
            return true;
        }
        // Check Cancel button
        if (quantitySliderCancelBounds_.isPointInside(irr::core::vector2di(x, y))) {
            closeQuantitySlider();
            return true;
        }
        // Check slider track for dragging
        if (quantitySliderTrackBounds_.isPointInside(irr::core::vector2di(x, y))) {
            quantitySliderDragging_ = true;
            // Calculate value from position
            int trackWidth = quantitySliderTrackBounds_.getWidth() - 10;  // Handle width
            int relX = x - quantitySliderTrackBounds_.UpperLeftCorner.X - 5;
            float ratio = std::clamp(static_cast<float>(relX) / trackWidth, 0.0f, 1.0f);
            quantitySliderValue_ = 1 + static_cast<int32_t>(ratio * (quantitySliderMax_ - 1));
            return true;
        }
        // Consume all clicks when slider is open
        return true;
    }

    // Handle confirmation dialog
    if (isConfirmDialogOpen()) {
        if (confirmYesButtonBounds_.isPointInside(irr::core::vector2di(x, y))) {
            if (confirmDialogType_ == ConfirmDialogType::DestroyItem) {
                invManager_->destroyCursorItem();
            }
            closeConfirmDialog();
            return true;
        }
        if (confirmNoButtonBounds_.isPointInside(irr::core::vector2di(x, y))) {
            closeConfirmDialog();
            return true;
        }
        return true;  // Consume all clicks when dialog is open
    }

    // Handle loot cursor item placement/cancellation
    if (hasLootCursorItem()) {
        if (!leftButton) {
            // Right-click cancels loot cursor
            cancelLootCursor();
            return true;
        }

        // Left-click with loot cursor - check if clicking on inventory or bag
        // to place the item (which loots it from the corpse)
        bool clickedOnInventory = inventoryWindow_ && inventoryWindow_->isVisible() &&
                                  inventoryWindow_->containsPoint(x, y);
        bool clickedOnBag = false;
        for (auto& [slotId, bagWindow] : bagWindows_) {
            if (bagWindow->containsPoint(x, y)) {
                clickedOnBag = true;
                break;
            }
        }

        if (clickedOnInventory || clickedOnBag) {
            // Place the loot item (triggers loot request to server)
            placeLootItem();
            return true;
        }

        // Clicking elsewhere (including loot window) cancels loot cursor
        // unless clicking on a different loot item (handled below)
        bool clickedOnLoot = lootWindow_ && lootWindow_->isOpen() &&
                            lootWindow_->containsPoint(x, y);
        if (!clickedOnLoot) {
            cancelLootCursor();
            return true;
        }

        // If clicked on loot window, cancel current and let loot window handle new pickup
        cancelLootCursor();
    }

    // Check spell gem panel (high priority for casting)
    if (spellGemPanel_ && spellGemPanel_->isVisible()) {
        if (spellGemPanel_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check spellbook window
    if (spellBookWindow_ && spellBookWindow_->isVisible()) {
        if (spellBookWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check buff window
    if (buffWindow_ && buffWindow_->isVisible()) {
        if (buffWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check group window
    if (groupWindow_ && groupWindow_->isVisible()) {
        if (groupWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check loot window first (if open)
    if (lootWindow_ && lootWindow_->isOpen()) {
        if (lootWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check vendor window (if open)
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        if (vendorWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check bag windows (they're on top of inventory)
    for (auto& [slotId, bagWindow] : bagWindows_) {
        if (bagWindow->handleMouseDown(x, y, leftButton, shift, ctrl)) {
            return true;
        }
    }

    // Check inventory window
    if (inventoryWindow_ && inventoryWindow_->isVisible()) {
        if (inventoryWindow_->handleMouseDown(x, y, leftButton, shift, ctrl)) {
            return true;
        }

        // Click outside inventory with cursor item - return item
        if (invManager_->hasCursorItem()) {
            invManager_->returnCursorItem();
            return true;
        }
    }

    // Check chat window (behind other windows)
    if (chatWindow_ && chatWindow_->isVisible()) {
        if (chatWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    return false;
}

bool WindowManager::handleMouseUp(int x, int y, bool leftButton) {
    mouseX_ = x;
    mouseY_ = y;

    // Handle quantity slider drag release
    if (quantitySliderDragging_) {
        quantitySliderDragging_ = false;
        return true;
    }

    // Check spellbook window
    if (spellBookWindow_ && spellBookWindow_->isVisible()) {
        if (spellBookWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check buff window
    if (buffWindow_ && buffWindow_->isVisible()) {
        if (buffWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check loot window
    if (lootWindow_ && lootWindow_->isOpen()) {
        if (lootWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check vendor window
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        if (vendorWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check bag windows
    for (auto& [slotId, bagWindow] : bagWindows_) {
        if (bagWindow->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check inventory window
    if (inventoryWindow_) {
        if (inventoryWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check chat window
    if (chatWindow_ && chatWindow_->isVisible()) {
        if (chatWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    return false;
}

bool WindowManager::handleMouseMove(int x, int y) {
    mouseX_ = x;
    mouseY_ = y;

    // Handle quantity slider dragging
    if (quantitySliderDragging_) {
        int trackWidth = quantitySliderTrackBounds_.getWidth() - 10;  // Handle width
        int relX = x - quantitySliderTrackBounds_.UpperLeftCorner.X - 5;
        float ratio = std::clamp(static_cast<float>(relX) / trackWidth, 0.0f, 1.0f);
        quantitySliderValue_ = 1 + static_cast<int32_t>(ratio * (quantitySliderMax_ - 1));
        return true;
    }

    // Update quantity slider button highlights
    if (quantitySliderActive_) {
        quantitySliderOkHighlighted_ = quantitySliderOkBounds_.isPointInside(irr::core::vector2di(x, y));
        quantitySliderCancelHighlighted_ = quantitySliderCancelBounds_.isPointInside(irr::core::vector2di(x, y));
        return true;
    }

    // Update confirmation dialog button highlights
    if (isConfirmDialogOpen()) {
        confirmYesHighlighted_ = confirmYesButtonBounds_.isPointInside(irr::core::vector2di(x, y));
        confirmNoHighlighted_ = confirmNoButtonBounds_.isPointInside(irr::core::vector2di(x, y));
        return true;
    }

    // Check spell gem panel
    if (spellGemPanel_ && spellGemPanel_->isVisible()) {
        spellGemPanel_->handleMouseMove(x, y);
        // Don't return true - allow other windows to update their hover state
    }

    // Check spellbook window
    if (spellBookWindow_ && spellBookWindow_->isVisible()) {
        if (spellBookWindow_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check buff window
    if (buffWindow_ && buffWindow_->isVisible()) {
        if (buffWindow_->handleMouseMove(x, y)) {
            // Update buff tooltip based on hovered buff
            const EQ::ActiveBuff* hoveredBuff = buffWindow_->getHoveredBuff();
            if (hoveredBuff) {
                buffTooltip_.setBuff(hoveredBuff, x, y);
            } else {
                buffTooltip_.clear();
            }
            return true;
        }
    }

    // Check group window
    if (groupWindow_ && groupWindow_->isVisible()) {
        groupWindow_->handleMouseMove(x, y);
        // Don't return true - allow other windows to update their hover state
    }

    // Check loot window
    if (lootWindow_ && lootWindow_->isOpen()) {
        if (lootWindow_->handleMouseMove(x, y)) {
            // Update tooltip for loot window items
            int16_t highlightedSlot = lootWindow_->getHighlightedSlot();
            if (highlightedSlot != inventory::SLOT_INVALID) {
                const inventory::ItemInstance* item = lootWindow_->getItem(highlightedSlot);
                if (item && !invManager_->hasCursorItem()) {
                    tooltip_.setItem(item, x, y);
                } else {
                    tooltip_.clear();
                }
            } else {
                tooltip_.clear();
            }
            return true;
        }
    }

    // Check vendor window
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        if (vendorWindow_->handleMouseMove(x, y)) {
            // Update tooltip for vendor window items
            int32_t highlightedSlot = vendorWindow_->getHighlightedSlot();
            if (highlightedSlot >= 0) {
                const inventory::ItemInstance* item = vendorWindow_->getHighlightedItem();
                if (item) {
                    tooltip_.setItem(item, x, y);
                } else {
                    tooltip_.clear();
                }
            } else {
                tooltip_.clear();
            }
            return true;
        }
    }

    // Check bag windows
    for (auto& [slotId, bagWindow] : bagWindows_) {
        if (bagWindow->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check inventory window
    if (inventoryWindow_ && inventoryWindow_->isVisible()) {
        if (inventoryWindow_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check chat window
    if (chatWindow_ && chatWindow_->isVisible()) {
        if (chatWindow_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Clear tooltips if not over any slot
    tooltip_.clear();
    buffTooltip_.clear();

    // Update window hover states for unlock highlighting
    updateWindowHoverStates(x, y);

    return false;
}

void WindowManager::updateWindowHoverStates(int x, int y) {
    // Only update hover states if UI is unlocked
    if (isUILocked()) {
        // Clear all hover states when locked
        if (chatWindow_) chatWindow_->setHovered(false);
        if (inventoryWindow_) inventoryWindow_->setHovered(false);
        if (lootWindow_) lootWindow_->setHovered(false);
        if (vendorWindow_) vendorWindow_->setHovered(false);
        if (buffWindow_) buffWindow_->setHovered(false);
        if (groupWindow_) groupWindow_->setHovered(false);
        if (spellBookWindow_) spellBookWindow_->setHovered(false);
        for (auto& [slotId, bagWindow] : bagWindows_) {
            bagWindow->setHovered(false);
        }
        return;
    }

    // Update hover state for each window
    if (chatWindow_) {
        chatWindow_->setHovered(chatWindow_->containsPoint(x, y));
    }
    if (inventoryWindow_) {
        inventoryWindow_->setHovered(inventoryWindow_->isVisible() && inventoryWindow_->containsPoint(x, y));
    }
    if (lootWindow_) {
        lootWindow_->setHovered(lootWindow_->isOpen() && lootWindow_->containsPoint(x, y));
    }
    if (vendorWindow_) {
        vendorWindow_->setHovered(vendorWindow_->isOpen() && vendorWindow_->containsPoint(x, y));
    }
    if (buffWindow_) {
        buffWindow_->setHovered(buffWindow_->isVisible() && buffWindow_->containsPoint(x, y));
    }
    if (groupWindow_) {
        groupWindow_->setHovered(groupWindow_->isVisible() && groupWindow_->containsPoint(x, y));
    }
    if (spellBookWindow_) {
        spellBookWindow_->setHovered(spellBookWindow_->isVisible() && spellBookWindow_->containsPoint(x, y));
    }
    for (auto& [slotId, bagWindow] : bagWindows_) {
        bagWindow->setHovered(bagWindow->containsPoint(x, y));
    }
}

bool WindowManager::handleMouseWheel(float delta) {
    // Route mouse wheel to chat window if mouse is over it
    if (chatWindow_ && chatWindow_->isVisible() && chatWindow_->containsPoint(mouseX_, mouseY_)) {
        return chatWindow_->handleMouseWheel(delta);
    }

    // Route mouse wheel to loot window if open and mouse is over it
    if (lootWindow_ && lootWindow_->isOpen()) {
        if (lootWindow_->containsPoint(mouseX_, mouseY_)) {
            return lootWindow_->handleMouseWheel(delta);
        }
    }

    // Route mouse wheel to vendor window if open and mouse is over it
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        if (vendorWindow_->containsPoint(mouseX_, mouseY_)) {
            return vendorWindow_->handleMouseWheel(delta);
        }
    }

    return false;
}

void WindowManager::render() {
    if (!driver_ || !gui_) {
        return;
    }

    // Render spell gem panel (always visible when initialized)
    if (spellGemPanel_ && spellGemPanel_->isVisible()) {
        spellGemPanel_->render(driver_, gui_);
    }

    // Render spellbook window
    if (spellBookWindow_ && spellBookWindow_->isVisible()) {
        spellBookWindow_->render(driver_, gui_);
    }

    // Render casting bar (above spell gems)
    if (castingBar_) {
        castingBar_->render(driver_, gui_);
    }

    // Render target casting bar (near top of screen)
    if (targetCastingBar_) {
        targetCastingBar_->render(driver_, gui_);
    }

    // Render memorizing bar (above player casting bar)
    if (memorizingBar_) {
        memorizingBar_->render(driver_, gui_);
    }

    // Render buff window
    if (buffWindow_ && buffWindow_->isVisible()) {
        buffWindow_->render(driver_, gui_);
    }

    // Render group window
    if (groupWindow_ && groupWindow_->isVisible()) {
        groupWindow_->render(driver_, gui_);
    }

    // Render player status window (upper left)
    if (playerStatusWindow_ && playerStatusWindow_->isVisible()) {
        playerStatusWindow_->render(driver_, gui_);
    }

    // Render chat window first (behind other windows)
    if (chatWindow_ && chatWindow_->isVisible()) {
        chatWindow_->render(driver_, gui_);
    }

    // Render inventory window
    if (inventoryWindow_ && inventoryWindow_->isVisible()) {
        inventoryWindow_->render(driver_, gui_);
    }

    // Render loot window
    if (lootWindow_ && lootWindow_->isOpen()) {
        lootWindow_->render(driver_, gui_);
    }

    // Render vendor window
    if (vendorWindow_ && vendorWindow_->isOpen()) {
        vendorWindow_->render(driver_, gui_);
    }

    // Render bag windows
    for (auto& [slotId, bagWindow] : bagWindows_) {
        bagWindow->render(driver_, gui_);
    }

    // Render tooltip
    tooltip_.render(driver_, gui_, screenWidth_, screenHeight_);

    // Render buff tooltip (from buff window hover)
    buffTooltip_.render(driver_, gui_, screenWidth_, screenHeight_);

    // Render spell tooltip (from spell gem panel hover)
    renderSpellTooltip();

    // Render cursor item
    renderCursorItem();

    // Render spell cursor (for spellbook-to-spellbar memorization)
    renderSpellCursor();

    // Render confirmation dialog
    if (isConfirmDialogOpen()) {
        renderConfirmDialog();
    }

    // Render quantity slider
    if (quantitySliderActive_) {
        renderQuantitySlider();
    }

    // Render lock indicator (shows lock state in corner of screen)
    renderLockIndicator();
}

void WindowManager::update(uint32_t currentTimeMs) {
    tooltip_.update(currentTimeMs, mouseX_, mouseY_);
    buffTooltip_.update(currentTimeMs, mouseX_, mouseY_);
    if (chatWindow_) {
        chatWindow_->update(currentTimeMs);
    }
    // Update group window (refreshes member data from EverQuest)
    if (groupWindow_) {
        groupWindow_->update();
    }
    // Update player status window (refreshes HP/mana/stamina from EverQuest)
    if (playerStatusWindow_) {
        playerStatusWindow_->update();
    }
    // Convert ms to seconds for casting bar updates
    static uint32_t lastUpdateMs = 0;
    float deltaTime = (currentTimeMs - lastUpdateMs) / 1000.0f;
    lastUpdateMs = currentTimeMs;

    if (deltaTime > 0 && deltaTime < 1.0f) {  // Sanity check
        if (castingBar_) {
            castingBar_->update(deltaTime);
        }
        if (targetCastingBar_) {
            targetCastingBar_->update(deltaTime);
        }
        if (memorizingBar_) {
            memorizingBar_->update(deltaTime);
        }
    }
}

bool WindowManager::isInventoryOpen() const {
    return inventoryWindow_ && inventoryWindow_->isVisible();
}

bool WindowManager::hasOpenWindows() const {
    return isInventoryOpen() || isLootWindowOpen() || isVendorWindowOpen() || !bagWindows_.empty() ||
           isConfirmDialogOpen() || (chatWindow_ && chatWindow_->isVisible());
}

bool WindowManager::hasCursorItem() const {
    return invManager_ && invManager_->hasCursorItem();
}

bool WindowManager::hasLootCursorItem() const {
    return lootCursorSlot_ != inventory::SLOT_INVALID && lootCursorItem_ != nullptr;
}

bool WindowManager::hasAnyCursorItem() const {
    return hasCursorItem() || hasLootCursorItem();
}

void WindowManager::pickupLootItem(uint16_t corpseId, int16_t lootSlot) {
    LOG_DEBUG(MOD_UI, "Loot UI pickupLootItem: corpseId={} lootSlot={}", corpseId, lootSlot);

    // Can't pick up if we already have something on cursor
    if (hasAnyCursorItem()) {
        LOG_DEBUG(MOD_UI, "pickupLootItem: Already have cursor item, ignoring");
        return;
    }

    // Get the item from loot window
    if (lootWindow_ && lootWindow_->isOpen()) {
        const inventory::ItemInstance* item = lootWindow_->getItem(lootSlot);
        if (item) {
            lootCursorCorpseId_ = corpseId;
            lootCursorSlot_ = lootSlot;
            lootCursorItem_ = item;
            tooltip_.clear();  // Hide tooltip while dragging
            LOG_DEBUG(MOD_UI, "Loot UI pickupLootItem: Picked up '{}'", item->name);
        } else {
            LOG_DEBUG(MOD_UI, "Loot UI pickupLootItem: No item at slot {}", lootSlot);
        }
    } else {
        LOG_DEBUG(MOD_UI, "pickupLootItem: Loot window not open");
    }
}

void WindowManager::placeLootItem() {
    LOG_DEBUG(MOD_UI, "Loot UI placeLootItem: corpseId={} slot={} hasCallback={}",
              lootCursorCorpseId_, lootCursorSlot_, (onLootItemCallback_ ? "yes" : "no"));

    // Trigger the loot callback to actually loot the item
    if (hasLootCursorItem() && onLootItemCallback_) {
        LOG_DEBUG(MOD_UI, "placeLootItem: Triggering loot callback");
        onLootItemCallback_(lootCursorCorpseId_, lootCursorSlot_);
    } else {
        LOG_DEBUG(MOD_UI, "placeLootItem: No loot cursor item or no callback");
    }

    // Clear loot cursor state
    cancelLootCursor();
}

void WindowManager::cancelLootCursor() {
    LOG_DEBUG(MOD_UI, "cancelLootCursor");
    lootCursorCorpseId_ = 0;
    lootCursorSlot_ = inventory::SLOT_INVALID;
    lootCursorItem_ = nullptr;
}

void WindowManager::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    if (inventoryWindow_) {
        inventoryWindow_->setCharacterName(name);
        inventoryWindow_->setCharacterLevel(level);
        inventoryWindow_->setCharacterClass(className);
    }
}

void WindowManager::setCharacterDeity(const std::wstring& deity) {
    if (inventoryWindow_) {
        inventoryWindow_->setCharacterDeity(deity);
    }
}

void WindowManager::updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                          uint32_t curMana, uint32_t maxMana,
                                          uint32_t curEnd, uint32_t maxEnd,
                                          int ac, int atk,
                                          int str, int sta, int agi, int dex, int wis, int intel, int cha,
                                          int pr, int mr, int dr, int fr, int cr,
                                          float weight, float maxWeight,
                                          uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    if (inventoryWindow_) {
        inventoryWindow_->setHP(static_cast<int>(curHp), static_cast<int>(maxHp));
        inventoryWindow_->setMana(static_cast<int>(curMana), static_cast<int>(maxMana));
        inventoryWindow_->setStamina(static_cast<int>(curEnd), static_cast<int>(maxEnd));
        inventoryWindow_->setAC(ac);
        inventoryWindow_->setATK(atk);
        inventoryWindow_->setStats(str, sta, agi, dex, wis, intel, cha);
        inventoryWindow_->setResists(pr, mr, dr, fr, cr);
        inventoryWindow_->setWeight(weight, maxWeight);
        inventoryWindow_->setCurrency(platinum, gold, silver, copper);
    }
}

void WindowManager::initModelView(irr::scene::ISceneManager* smgr,
                                   EQT::Graphics::RaceModelLoader* raceLoader,
                                   EQT::Graphics::EquipmentModelLoader* equipLoader) {
    if (!inventoryWindow_) {
        LOG_WARN(MOD_UI, "WindowManager::initModelView: inventory window not created yet");
        return;
    }

    if (!smgr || !driver_) {
        LOG_ERROR(MOD_UI, "WindowManager::initModelView: null scene manager or driver");
        return;
    }

    inventoryWindow_->initModelView(smgr, driver_, raceLoader, equipLoader);
    LOG_INFO(MOD_UI, "WindowManager: Model view initialized");
}

void WindowManager::setPlayerAppearance(uint16_t raceId, uint8_t gender,
                                         const EQT::Graphics::EntityAppearance& appearance) {
    LOG_DEBUG(MOD_UI, "WindowManager::setPlayerAppearance race={} gender={}", raceId, gender);
    if (inventoryWindow_) {
        inventoryWindow_->setPlayerAppearance(raceId, gender, appearance);
    }
}

void WindowManager::showConfirmDialog(ConfirmDialogType type, const std::wstring& message) {
    confirmDialogType_ = type;
    confirmDialogMessage_ = message;

    // Position dialog in center of screen
    int dialogWidth = 300;
    int dialogHeight = 100;
    int dialogX = (screenWidth_ - dialogWidth) / 2;
    int dialogY = (screenHeight_ - dialogHeight) / 2;

    confirmDialogBounds_ = irr::core::recti(
        dialogX, dialogY,
        dialogX + dialogWidth, dialogY + dialogHeight
    );

    // Button positions
    int buttonWidth = 60;
    int buttonHeight = 24;
    int buttonY = dialogY + dialogHeight - buttonHeight - 10;

    confirmYesButtonBounds_ = irr::core::recti(
        dialogX + dialogWidth / 2 - buttonWidth - 10, buttonY,
        dialogX + dialogWidth / 2 - 10, buttonY + buttonHeight
    );

    confirmNoButtonBounds_ = irr::core::recti(
        dialogX + dialogWidth / 2 + 10, buttonY,
        dialogX + dialogWidth / 2 + buttonWidth + 10, buttonY + buttonHeight
    );
}

void WindowManager::closeConfirmDialog() {
    confirmDialogType_ = ConfirmDialogType::None;
    confirmDialogMessage_.clear();
    confirmYesHighlighted_ = false;
    confirmNoHighlighted_ = false;
}

void WindowManager::showItemTooltip(const inventory::ItemInstance* item, int mouseX, int mouseY) {
    tooltip_.setItem(item, mouseX, mouseY);
}

void WindowManager::showBuffTooltip(const EQ::ActiveBuff* buff, int mouseX, int mouseY) {
    buffTooltip_.setBuff(buff, mouseX, mouseY);
}

void WindowManager::hideBuffTooltip() {
    buffTooltip_.clear();
}

void WindowManager::handleSlotClick(int16_t slotId, bool shift, bool ctrl) {
    LOG_DEBUG(MOD_UI, "WindowManager handleSlotClick: slotId={} shift={} ctrl={}", slotId, shift, ctrl);
    if (slotId == inventory::SLOT_INVALID) {
        return;
    }

    if (invManager_->hasCursorItem()) {
        // Try to place item - check for auto-stacking
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = invManager_->getItem(slotId);

        // Check if we can stack onto an existing item of the same type
        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId && targetItem->stackSize > 0 &&
            targetItem->quantity < targetItem->stackSize) {
            // Auto-stack: move items from cursor to target stack
            LOG_DEBUG(MOD_UI, "WindowManager Auto-stacking cursor item onto slot {}", slotId);
            invManager_->placeOnMatchingStack(slotId);
        } else {
            LOG_DEBUG(MOD_UI, "WindowManager Placing cursor item at slot {}", slotId);
            placeItem(slotId);
        }
    } else {
        // Try to pick up item
        if (invManager_->hasItem(slotId)) {
            const inventory::ItemInstance* item = invManager_->getItem(slotId);

            // Check if it's a bag - require shift to pick up
            if (item && item->isContainer() && !shift && !ctrl) {
                // Don't pick up bags without shift
                LOG_DEBUG(MOD_UI, "Slot contains bag, need shift to pick up");
                return;
            }

            // Handle stackable items with modifiers
            if (item && item->stackable && item->quantity > 1) {
                if (ctrl) {
                    // Ctrl+click: pick up just 1 item from the stack
                    LOG_DEBUG(MOD_UI, "WindowManager Ctrl+click: picking up 1 from stack of {} at slot {}", item->quantity, slotId);
                    invManager_->pickupPartialStack(slotId, 1);
                    tooltip_.clear();
                    return;
                } else if (shift) {
                    // Shift+click: show quantity slider
                    LOG_DEBUG(MOD_UI, "WindowManager Shift+click: showing quantity slider for stack of {} at slot {}", item->quantity, slotId);
                    showQuantitySlider(slotId, item->quantity);
                    return;
                }
            }

            // If picking up a bag, close its window
            if (item && item->isContainer()) {
                closeBagWindow(slotId);
            }

            LOG_DEBUG(MOD_UI, "WindowManager Picking up item from slot {}", slotId);
            pickupItem(slotId);
        } else {
            LOG_DEBUG(MOD_UI, "WindowManager No item in slot {}", slotId);
        }
    }
}

void WindowManager::handleBagSlotClick(int16_t slotId, bool shift, bool ctrl) {
    LOG_DEBUG(MOD_UI, "WindowManager handleBagSlotClick: slotId={} shift={} ctrl={}", slotId, shift, ctrl);
    if (slotId == inventory::SLOT_INVALID) {
        return;
    }

    if (invManager_->hasCursorItem()) {
        // Try to place item - check for auto-stacking
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = invManager_->getItem(slotId);

        // Check if we can stack onto an existing item of the same type
        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId && targetItem->stackSize > 0 &&
            targetItem->quantity < targetItem->stackSize) {
            // Auto-stack: move items from cursor to target stack
            LOG_DEBUG(MOD_UI, "WindowManager Auto-stacking cursor item onto bag slot {}", slotId);
            invManager_->placeOnMatchingStack(slotId);
        } else {
            LOG_DEBUG(MOD_UI, "WindowManager Placing cursor item in bag slot {}", slotId);
            placeItem(slotId);
        }
    } else {
        if (invManager_->hasItem(slotId)) {
            const inventory::ItemInstance* item = invManager_->getItem(slotId);

            // Handle stackable items with modifiers
            if (item && item->stackable && item->quantity > 1) {
                if (ctrl) {
                    // Ctrl+click: pick up just 1 item from the stack
                    LOG_DEBUG(MOD_UI, "WindowManager Ctrl+click: picking up 1 from bag stack of {} at slot {}", item->quantity, slotId);
                    invManager_->pickupPartialStack(slotId, 1);
                    tooltip_.clear();
                    return;
                } else if (shift) {
                    // Shift+click: show quantity slider
                    LOG_DEBUG(MOD_UI, "WindowManager Shift+click: showing quantity slider for bag stack of {} at slot {}", item->quantity, slotId);
                    showQuantitySlider(slotId, item->quantity);
                    return;
                }
            }

            LOG_DEBUG(MOD_UI, "WindowManager Picking up from bag slot {}", slotId);
            pickupItem(slotId);
        }
    }
}

void WindowManager::handleSlotHover(int16_t slotId, int mouseX, int mouseY) {
    if (slotId == inventory::SLOT_INVALID) {
        tooltip_.clear();
        return;
    }

    const inventory::ItemInstance* item = invManager_->getItem(slotId);
    if (item && !invManager_->hasCursorItem()) {
        tooltip_.setItem(item, mouseX, mouseY);
    } else {
        tooltip_.clear();
    }

    // Update slot highlighting for cursor item
    if (invManager_->hasCursorItem() && inventoryWindow_) {
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        if (!invManager_->canPlaceItemInSlot(cursorItem, slotId)) {
            inventoryWindow_->setInvalidDropSlot(slotId);
        }
    }
}

void WindowManager::handleDestroyClick() {
    if (!invManager_->hasCursorItem()) {
        return;
    }

    const inventory::ItemInstance* item = invManager_->getCursorItem();
    if (item) {
        std::wstring message = L"Destroy ";
        message += std::wstring(item->name.begin(), item->name.end());
        message += L"?";
        showConfirmDialog(ConfirmDialogType::DestroyItem, message);
    }
}

void WindowManager::handleBagOpenClick(int16_t generalSlot) {
    // Can't open bags while holding an item
    if (invManager_->hasCursorItem()) {
        return;
    }

    toggleBagWindow(generalSlot);
}

void WindowManager::pickupItem(int16_t slotId) {
    if (invManager_->pickupItem(slotId)) {
        tooltip_.clear();
    }
}

void WindowManager::placeItem(int16_t targetSlot) {
    invManager_->placeItem(targetSlot);
}

void WindowManager::returnCursorItem() {
    invManager_->returnCursorItem();
}

void WindowManager::positionInventoryWindow() {
    if (inventoryWindow_) {
        inventoryWindow_->setPosition(INVENTORY_X, INVENTORY_Y);
    }
}

void WindowManager::positionLootWindow() {
    if (!lootWindow_ || !inventoryWindow_) {
        return;
    }

    // Position loot window to the right of the inventory window
    int x = inventoryWindow_->getRight() + WINDOW_MARGIN;
    int y = INVENTORY_Y;

    // Ensure visible on screen - if not enough room on right, put on left
    if (x + lootWindow_->getWidth() > screenWidth_ - WINDOW_MARGIN) {
        x = INVENTORY_X - lootWindow_->getWidth() - WINDOW_MARGIN;
        if (x < WINDOW_MARGIN) {
            x = WINDOW_MARGIN;  // Fallback to left edge
        }
    }

    lootWindow_->setPosition(x, y);
}

void WindowManager::positionVendorWindow() {
    if (!vendorWindow_) {
        return;
    }

    // Position vendor window in center-right of screen
    int windowWidth = vendorWindow_->getWidth();
    int windowHeight = vendorWindow_->getHeight();
    int x = (screenWidth_ - windowWidth) / 2 + 100;  // Slightly right of center
    int y = (screenHeight_ - windowHeight) / 2;

    // Ensure visible on screen
    if (x + windowWidth > screenWidth_ - WINDOW_MARGIN) {
        x = screenWidth_ - windowWidth - WINDOW_MARGIN;
    }
    if (y + windowHeight > screenHeight_ - WINDOW_MARGIN) {
        y = screenHeight_ - windowHeight - WINDOW_MARGIN;
    }
    if (x < WINDOW_MARGIN) x = WINDOW_MARGIN;
    if (y < WINDOW_MARGIN) y = WINDOW_MARGIN;

    vendorWindow_->setPosition(x, y);
}

void WindowManager::tileBagWindows() {
    if (bagWindows_.empty()) {
        return;
    }

    irr::core::recti tilingArea = getBagTilingArea();
    int currentX = tilingArea.UpperLeftCorner.X;
    int currentY = tilingArea.UpperLeftCorner.Y;
    int maxHeightInRow = 0;

    for (auto& [slotId, bagWindow] : bagWindows_) {
        int bagWidth = bagWindow->getWidth();
        int bagHeight = bagWindow->getHeight();

        // Check if bag fits in current row
        if (currentX + bagWidth > tilingArea.LowerRightCorner.X) {
            // Move to next row
            currentX = tilingArea.UpperLeftCorner.X;
            currentY += maxHeightInRow + WINDOW_MARGIN;
            maxHeightInRow = 0;

            // If we've run out of vertical space, wrap to top
            if (currentY + bagHeight > tilingArea.LowerRightCorner.Y) {
                currentY = tilingArea.UpperLeftCorner.Y;
            }
        }

        bagWindow->setPosition(currentX, currentY);
        currentX += bagWidth + WINDOW_MARGIN;
        maxHeightInRow = std::max(maxHeightInRow, bagHeight);
    }
}

irr::core::recti WindowManager::getBagTilingArea() const {
    int startX = INVENTORY_X;
    int startY = INVENTORY_Y;

    if (inventoryWindow_ && inventoryWindow_->isVisible()) {
        startX = inventoryWindow_->getRight() + WINDOW_MARGIN;
        // Reserve space for future loot window
        startX += RESERVED_LOOT_WIDTH + WINDOW_MARGIN;
    }

    return irr::core::recti(
        startX, startY,
        screenWidth_ - WINDOW_MARGIN,
        screenHeight_ - WINDOW_MARGIN
    );
}

void WindowManager::renderCursorItem() {
    if (!driver_ || !gui_) {
        return;
    }

    // Check for either inventory cursor item or loot cursor item
    const inventory::ItemInstance* item = nullptr;
    if (invManager_->hasCursorItem()) {
        item = invManager_->getCursorItem();
    } else if (hasLootCursorItem()) {
        item = lootCursorItem_;
    }

    if (!item) {
        return;
    }

    // Draw item at cursor position
    const int CURSOR_ITEM_SIZE = 32;
    const int OFFSET = 8;  // Offset from cursor

    int x = mouseX_ + OFFSET;
    int y = mouseY_ + OFFSET;

    // Item rect
    irr::core::recti itemRect(x, y, x + CURSOR_ITEM_SIZE, y + CURSOR_ITEM_SIZE);

    // Try to get item icon
    irr::video::ITexture* iconTexture = iconLoader_.getIcon(item->icon);

    if (iconTexture) {
        // Draw item icon
        driver_->draw2DImage(iconTexture, itemRect,
            irr::core::recti(0, 0, iconTexture->getSize().Width, iconTexture->getSize().Height),
            nullptr, nullptr, true);
    } else {
        // Fallback: Color based on item type
        irr::video::SColor itemColor;
        if (item->magic) {
            itemColor = irr::video::SColor(200, 100, 100, 200);
        } else if (item->isContainer()) {
            itemColor = irr::video::SColor(200, 139, 90, 43);
        } else {
            itemColor = irr::video::SColor(200, 150, 150, 150);
        }

        // Add golden tint for loot cursor items to indicate they're from a corpse
        if (hasLootCursorItem()) {
            itemColor = irr::video::SColor(200, 180, 160, 80);  // Golden/tan tint
        }

        driver_->draw2DRectangle(itemColor, itemRect);
    }

    // Border (golden for loot cursor, white for inventory cursor)
    irr::video::SColor borderColor = hasLootCursorItem() ?
        irr::video::SColor(255, 218, 165, 32) :  // Gold border for loot
        irr::video::SColor(255, 200, 200, 200);   // White border for inventory
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y, x + CURSOR_ITEM_SIZE, y + 1));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y, x + 1, y + CURSOR_ITEM_SIZE));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y + CURSOR_ITEM_SIZE - 1, x + CURSOR_ITEM_SIZE, y + CURSOR_ITEM_SIZE));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x + CURSOR_ITEM_SIZE - 1, y, x + CURSOR_ITEM_SIZE, y + CURSOR_ITEM_SIZE));
}

void WindowManager::renderSpellCursor() {
    if (!driver_ || !spellCursor_.active || !spellCursor_.icon) {
        return;
    }

    // Draw spell icon centered on cursor with slight offset
    const int CURSOR_SPELL_SIZE = 24;
    const int OFFSET = 4;

    int x = mouseX_ + OFFSET;
    int y = mouseY_ + OFFSET;

    irr::core::recti spellRect(x, y, x + CURSOR_SPELL_SIZE, y + CURSOR_SPELL_SIZE);

    // Draw with slight transparency
    irr::video::SColor colors[4] = {
        irr::video::SColor(220, 255, 255, 255),
        irr::video::SColor(220, 255, 255, 255),
        irr::video::SColor(220, 255, 255, 255),
        irr::video::SColor(220, 255, 255, 255)
    };

    irr::core::dimension2du iconSize = spellCursor_.icon->getOriginalSize();
    driver_->draw2DImage(spellCursor_.icon, spellRect,
        irr::core::recti(0, 0, iconSize.Width, iconSize.Height),
        nullptr, colors, true);

    // Draw purple/magical border to indicate it's a spell
    irr::video::SColor borderColor(255, 180, 100, 255);
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y, x + CURSOR_SPELL_SIZE, y + 1));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y, x + 1, y + CURSOR_SPELL_SIZE));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x, y + CURSOR_SPELL_SIZE - 1, x + CURSOR_SPELL_SIZE, y + CURSOR_SPELL_SIZE));
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(x + CURSOR_SPELL_SIZE - 1, y, x + CURSOR_SPELL_SIZE, y + CURSOR_SPELL_SIZE));
}

void WindowManager::renderConfirmDialog() {
    if (!isConfirmDialogOpen() || !driver_ || !gui_) {
        return;
    }

    // Draw dialog background
    irr::video::SColor bgColor(240, 48, 48, 48);
    driver_->draw2DRectangle(bgColor, confirmDialogBounds_);

    // Draw border
    irr::video::SColor borderColor(255, 100, 100, 100);
    // Top
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(confirmDialogBounds_.UpperLeftCorner.X,
                        confirmDialogBounds_.UpperLeftCorner.Y,
                        confirmDialogBounds_.LowerRightCorner.X,
                        confirmDialogBounds_.UpperLeftCorner.Y + 2));
    // Left
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(confirmDialogBounds_.UpperLeftCorner.X,
                        confirmDialogBounds_.UpperLeftCorner.Y,
                        confirmDialogBounds_.UpperLeftCorner.X + 2,
                        confirmDialogBounds_.LowerRightCorner.Y));
    // Bottom
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(confirmDialogBounds_.UpperLeftCorner.X,
                        confirmDialogBounds_.LowerRightCorner.Y - 2,
                        confirmDialogBounds_.LowerRightCorner.X,
                        confirmDialogBounds_.LowerRightCorner.Y));
    // Right
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(confirmDialogBounds_.LowerRightCorner.X - 2,
                        confirmDialogBounds_.UpperLeftCorner.Y,
                        confirmDialogBounds_.LowerRightCorner.X,
                        confirmDialogBounds_.LowerRightCorner.Y));

    // Draw message
    irr::gui::IGUIFont* font = gui_->getBuiltInFont();
    if (font) {
        irr::core::recti textRect(
            confirmDialogBounds_.UpperLeftCorner.X + 10,
            confirmDialogBounds_.UpperLeftCorner.Y + 20,
            confirmDialogBounds_.LowerRightCorner.X - 10,
            confirmDialogBounds_.UpperLeftCorner.Y + 50
        );
        font->draw(confirmDialogMessage_.c_str(), textRect,
                  irr::video::SColor(255, 255, 255, 255));
    }

    // Draw Yes button
    irr::video::SColor yesColor = confirmYesHighlighted_ ?
        irr::video::SColor(255, 96, 96, 96) : irr::video::SColor(255, 72, 72, 72);
    driver_->draw2DRectangle(yesColor, confirmYesButtonBounds_);
    if (font) {
        font->draw(L"Yes", confirmYesButtonBounds_,
                  irr::video::SColor(255, 200, 200, 200), true, true);
    }

    // Draw No button
    irr::video::SColor noColor = confirmNoHighlighted_ ?
        irr::video::SColor(255, 96, 96, 96) : irr::video::SColor(255, 72, 72, 72);
    driver_->draw2DRectangle(noColor, confirmNoButtonBounds_);
    if (font) {
        font->draw(L"No", confirmNoButtonBounds_,
                  irr::video::SColor(255, 200, 200, 200), true, true);
    }
}

WindowBase* WindowManager::getWindowAtPosition(int x, int y) {
    // Check bag windows first (on top)
    for (auto& [slotId, bagWindow] : bagWindows_) {
        if (bagWindow->isVisible() && bagWindow->containsPoint(x, y)) {
            return bagWindow.get();
        }
    }

    // Check inventory window
    if (inventoryWindow_ && inventoryWindow_->isVisible() &&
        inventoryWindow_->containsPoint(x, y)) {
        return inventoryWindow_.get();
    }

    return nullptr;
}

bool WindowManager::isChatInputFocused() const {
    return chatWindow_ && chatWindow_->isInputFocused();
}

void WindowManager::focusChatInput() {
    if (chatWindow_) {
        chatWindow_->focusInput();
    }
}

void WindowManager::unfocusChatInput() {
    if (chatWindow_) {
        chatWindow_->unfocusInput();
    }
}

void WindowManager::setChatSubmitCallback(ChatSubmitCallback callback) {
    if (chatWindow_) {
        chatWindow_->setSubmitCallback(callback);
    }
}

void WindowManager::initSpellGemPanel(EQ::SpellManager* spellMgr) {
    if (!spellMgr) {
        LOG_WARN(MOD_UI, "Cannot initialize spell gem panel - SpellManager is null");
        return;
    }

    spellMgr_ = spellMgr;  // Store for tooltip rendering

    // Set SpellDatabase on buff tooltip
    buffTooltip_.setSpellDatabase(&spellMgr->getDatabase());
    spellGemPanel_ = std::make_unique<SpellGemPanel>(spellMgr, &iconLoader_);

    // Position on left side of screen, vertically centered
    // New smaller gems: 20x20 with 2px spacing and 2px padding
    int panelWidth = 24;  // GEM_WIDTH(20) + padding(2*2)
    int panelHeight = 8 * 22 + 4;  // 8 gems * (20 + 2 spacing) + padding(2*2)
    int panelX = 10;  // Left side with small margin
    int panelY = (screenHeight_ - panelHeight) / 2;
    spellGemPanel_->setPosition(panelX, panelY);

    LOG_DEBUG(MOD_UI, "Spell gem panel initialized at ({}, {})", panelX, panelY);

    // Also create the spellbook window
    spellBookWindow_ = std::make_unique<SpellBookWindow>(spellMgr, &iconLoader_);
    spellBookWindow_->hide();  // Start hidden
    LOG_DEBUG(MOD_UI, "Spellbook window created");

    // Connect spellbook spell click to set cursor for memorization
    spellBookWindow_->setSetSpellCursorCallback([this](uint32_t spell_id, irr::video::ITexture* icon) {
        setSpellOnCursor(spell_id, icon);
    });

    // Connect spellbook button in gem panel to toggle spellbook window
    spellGemPanel_->setSpellbookCallback([this]() {
        toggleSpellbook();
    });

    // Connect hover callbacks for spell gem tooltips
    spellGemPanel_->setGemHoverCallback([this, spellMgr](uint8_t gem_slot, uint32_t spell_id, int mouseX, int mouseY) {
        if (spell_id != EQ::SPELL_UNKNOWN && spell_id != 0xFFFFFFFF) {
            hoveredSpellId_ = spell_id;
            hoveredSpellX_ = mouseX;
            hoveredSpellY_ = mouseY;
        }
    });

    spellGemPanel_->setGemHoverEndCallback([this]() {
        hoveredSpellId_ = EQ::SPELL_UNKNOWN;
    });

    // Connect cursor spell callbacks for spellbook-to-gem memorization
    spellGemPanel_->setGetSpellCursorCallback([this]() -> uint32_t {
        return getSpellOnCursor();
    });

    spellGemPanel_->setClearSpellCursorCallback([this]() {
        clearSpellCursor();
    });

    spellGemPanel_->setMemorizeCallback([spellMgr](uint32_t spell_id, uint8_t gem_slot) {
        if (spellMgr) {
            spellMgr->memorizeSpell(spell_id, gem_slot);
        }
    });
}

void WindowManager::setGemCastCallback(GemCastCallback callback) {
    if (spellGemPanel_) {
        spellGemPanel_->setGemCastCallback(callback);
    }
}

void WindowManager::setGemForgetCallback(GemForgetCallback callback) {
    if (spellGemPanel_) {
        spellGemPanel_->setGemForgetCallback(callback);
    }
}

// ============================================================================
// Spellbook Window Management
// ============================================================================

void WindowManager::toggleSpellbook() {
    if (!spellBookWindow_) {
        LOG_WARN(MOD_UI, "Cannot toggle spellbook - not initialized");
        return;
    }

    if (spellBookWindow_->isVisible()) {
        closeSpellbook();
    } else {
        openSpellbook();
    }
}

void WindowManager::openSpellbook() {
    if (spellBookWindow_) {
        // Position to the right of spell gem panel
        int x = 106;
        int y = 131;
        spellBookWindow_->setPosition(x, y);
        spellBookWindow_->show();
        spellBookWindow_->refresh();
        LOG_DEBUG(MOD_UI, "Spellbook opened at ({}, {})", x, y);
    }
}

void WindowManager::closeSpellbook() {
    if (spellBookWindow_) {
        spellBookWindow_->hide();
        LOG_DEBUG(MOD_UI, "Spellbook closed");
    }
}

bool WindowManager::isSpellbookOpen() const {
    return spellBookWindow_ && spellBookWindow_->isVisible();
}

void WindowManager::setSpellMemorizeCallback(SpellClickCallback callback) {
    spellMemorizeCallback_ = callback;
    if (spellBookWindow_) {
        spellBookWindow_->setSpellClickCallback(callback);
    }
}

// ============================================================================
// Buff Window Management
// ============================================================================

void WindowManager::initBuffWindow(EQ::BuffManager* buffMgr) {
    if (!buffMgr) {
        LOG_WARN(MOD_UI, "Cannot initialize buff window - BuffManager is null");
        return;
    }

    buffWindow_ = std::make_unique<BuffWindow>(buffMgr, &iconLoader_);
    buffWindow_->positionDefault(screenWidth_, screenHeight_);
    buffWindow_->show();  // Buff window visible by default

    // Set up cancel callback
    if (buffCancelCallback_) {
        buffWindow_->setCancelCallback(buffCancelCallback_);
    }

    LOG_DEBUG(MOD_UI, "Buff window initialized");
}

void WindowManager::toggleBuffWindow() {
    if (!buffWindow_) return;
    if (buffWindow_->isVisible()) {
        closeBuffWindow();
    } else {
        openBuffWindow();
    }
}

void WindowManager::openBuffWindow() {
    if (buffWindow_) {
        buffWindow_->show();
    }
}

void WindowManager::closeBuffWindow() {
    if (buffWindow_) {
        buffWindow_->hide();
    }
}

bool WindowManager::isBuffWindowOpen() const {
    return buffWindow_ && buffWindow_->isVisible();
}

void WindowManager::setBuffCancelCallback(BuffCancelCallback callback) {
    buffCancelCallback_ = callback;
    if (buffWindow_) {
        buffWindow_->setCancelCallback(callback);
    }
}

// ============================================================================
// Group Window Management
// ============================================================================

void WindowManager::initGroupWindow(EverQuest* eq) {
    if (!eq) {
        LOG_WARN(MOD_UI, "Cannot initialize group window - EverQuest is null");
        return;
    }

    groupWindow_ = std::make_unique<GroupWindow>();
    groupWindow_->setEQ(eq);
    groupWindow_->positionDefault(screenWidth_, screenHeight_);
    groupWindow_->show();  // Group window visible by default

    // Set up callbacks
    if (groupInviteCallback_) {
        groupWindow_->setInviteCallback(groupInviteCallback_);
    }
    if (groupDisbandCallback_) {
        groupWindow_->setDisbandCallback(groupDisbandCallback_);
    }
    if (groupAcceptCallback_) {
        groupWindow_->setAcceptCallback(groupAcceptCallback_);
    }
    if (groupDeclineCallback_) {
        groupWindow_->setDeclineCallback(groupDeclineCallback_);
    }

    LOG_DEBUG(MOD_UI, "Group window initialized");
}

void WindowManager::toggleGroupWindow() {
    if (!groupWindow_) return;
    if (groupWindow_->isVisible()) {
        closeGroupWindow();
    } else {
        openGroupWindow();
    }
}

void WindowManager::openGroupWindow() {
    if (groupWindow_) {
        groupWindow_->show();
    }
}

void WindowManager::closeGroupWindow() {
    if (groupWindow_) {
        groupWindow_->hide();
    }
}

bool WindowManager::isGroupWindowOpen() const {
    return groupWindow_ && groupWindow_->isVisible();
}

void WindowManager::setGroupInviteCallback(GroupInviteCallback callback) {
    groupInviteCallback_ = callback;
    if (groupWindow_) {
        groupWindow_->setInviteCallback(callback);
    }
}

void WindowManager::setGroupDisbandCallback(GroupDisbandCallback callback) {
    groupDisbandCallback_ = callback;
    if (groupWindow_) {
        groupWindow_->setDisbandCallback(callback);
    }
}

void WindowManager::setGroupAcceptCallback(GroupAcceptCallback callback) {
    groupAcceptCallback_ = callback;
    if (groupWindow_) {
        groupWindow_->setAcceptCallback(callback);
    }
}

void WindowManager::setGroupDeclineCallback(GroupDeclineCallback callback) {
    groupDeclineCallback_ = callback;
    if (groupWindow_) {
        groupWindow_->setDeclineCallback(callback);
    }
}

// ============================================================================
// Player Status Window Management
// ============================================================================

void WindowManager::initPlayerStatusWindow(EverQuest* eq) {
    if (!eq) {
        LOG_WARN(MOD_UI, "Cannot initialize player status window - EverQuest is null");
        return;
    }

    playerStatusWindow_ = std::make_unique<PlayerStatusWindow>();
    playerStatusWindow_->setEQ(eq);

    // Apply position from settings
    const auto& settings = UISettings::instance().playerStatus();
    int x = UISettings::resolvePosition(settings.window.x, screenWidth_, 10);
    int y = UISettings::resolvePosition(settings.window.y, screenHeight_, 10);
    playerStatusWindow_->setPosition(x, y);

    if (settings.window.visible) {
        playerStatusWindow_->show();
    }

    LOG_DEBUG(MOD_UI, "Player status window initialized");
}

// ============================================================================
// Casting Bar Management
// ============================================================================

void WindowManager::initCastingBar() {
    castingBar_ = std::make_unique<CastingBar>();
    castingBar_->setScreenSize(screenWidth_, screenHeight_);
    positionCastingBarAboveChat();
    LOG_DEBUG(MOD_UI, "Casting bar initialized");
}

void WindowManager::positionCastingBarAboveChat() {
    if (!castingBar_) return;

    // Position horizontally centered on screen
    int x = screenWidth_ / 2;

    // Position just above the chat window with some padding
    // Casting bar has: bar (16px) + text below (~14px) = ~30px total below y
    int y;
    if (chatWindow_) {
        // 35 pixels above chat window top (includes bar height + text + padding)
        y = chatWindow_->getY() - 35;
    } else {
        // Fallback if chat window doesn't exist yet
        y = screenHeight_ - 150;
    }

    castingBar_->setPosition(x, y);
}

void WindowManager::startCast(const std::string& spellName, uint32_t castTimeMs) {
    if (!castingBar_) {
        initCastingBar();
    }
    castingBar_->startCast(spellName, castTimeMs);
}

void WindowManager::cancelCast() {
    if (castingBar_) {
        castingBar_->cancel();
    }
}

void WindowManager::completeCast() {
    if (castingBar_) {
        castingBar_->complete();
    }
}

bool WindowManager::isCastingBarActive() const {
    return castingBar_ && castingBar_->isActive();
}

// ============================================================================
// Target Casting Bar Management
// ============================================================================

void WindowManager::startTargetCast(const std::string& casterName, const std::string& spellName, uint32_t castTimeMs) {
    if (!targetCastingBar_) {
        targetCastingBar_ = std::make_unique<CastingBar>();
        targetCastingBar_->setScreenSize(screenWidth_, screenHeight_);
        // Position target casting bar near top of screen
        targetCastingBar_->setPosition(screenWidth_ / 2, 80);
        // Use different color for target's casting bar (red/orange tint)
        targetCastingBar_->setBarColor(irr::video::SColor(255, 255, 140, 80));
        LOG_DEBUG(MOD_UI, "Target casting bar initialized");
    }
    // Format as "CasterName: SpellName"
    std::string displayName = casterName + ": " + spellName;
    targetCastingBar_->startCast(displayName, castTimeMs);

    // Update player status window with target's spell
    if (playerStatusWindow_) {
        playerStatusWindow_->setTargetCastingSpell(spellName);
    }
}

void WindowManager::cancelTargetCast() {
    if (targetCastingBar_) {
        targetCastingBar_->cancel();
    }
    // Clear target spell from player status window
    if (playerStatusWindow_) {
        playerStatusWindow_->clearTargetCastingSpell();
    }
}

void WindowManager::completeTargetCast() {
    if (targetCastingBar_) {
        targetCastingBar_->complete();
    }
    // Clear target spell from player status window
    if (playerStatusWindow_) {
        playerStatusWindow_->clearTargetCastingSpell();
    }
}

bool WindowManager::isTargetCastingBarActive() const {
    return targetCastingBar_ && targetCastingBar_->isActive();
}

// ============================================================================
// Memorizing Bar Management
// ============================================================================

void WindowManager::startMemorize(const std::string& spellName, uint32_t durationMs) {
    if (!memorizingBar_) {
        memorizingBar_ = std::make_unique<CastingBar>();
        memorizingBar_->setScreenSize(screenWidth_, screenHeight_);
        // Position memorizing bar above the player casting bar
        int y;
        if (chatWindow_) {
            y = chatWindow_->getY() - 70;  // Higher than casting bar
        } else {
            y = screenHeight_ - 185;
        }
        memorizingBar_->setPosition(screenWidth_ / 2, y);
        // Use yellow/gold tint for memorization
        memorizingBar_->setBarColor(irr::video::SColor(255, 220, 200, 80));
        memorizingBar_->setBackgroundColor(irr::video::SColor(200, 60, 60, 30));
        LOG_DEBUG(MOD_UI, "Memorizing bar initialized");
    }
    std::string displayName = "Memorizing " + spellName;
    memorizingBar_->startCast(displayName, durationMs);
}

void WindowManager::cancelMemorize() {
    if (memorizingBar_) {
        memorizingBar_->cancel();
    }
}

void WindowManager::completeMemorize() {
    if (memorizingBar_) {
        memorizingBar_->complete();
    }
}

bool WindowManager::isMemorizingBarActive() const {
    return memorizingBar_ && memorizingBar_->isActive();
}

// ============================================================================
// Spell Cursor Management
// ============================================================================

void WindowManager::setSpellOnCursor(uint32_t spellId, irr::video::ITexture* icon) {
    spellCursor_.active = true;
    spellCursor_.spellId = spellId;
    spellCursor_.icon = icon;
    LOG_DEBUG(MOD_UI, "Spell {} placed on cursor", spellId);
}

void WindowManager::clearSpellCursor() {
    if (spellCursor_.active) {
        LOG_DEBUG(MOD_UI, "Spell cursor cleared");
    }
    spellCursor_.active = false;
    spellCursor_.spellId = 0xFFFFFFFF;
    spellCursor_.icon = nullptr;
}

bool WindowManager::hasSpellOnCursor() const {
    return spellCursor_.active;
}

uint32_t WindowManager::getSpellOnCursor() const {
    return spellCursor_.spellId;
}

void WindowManager::renderSpellTooltip() {
    if (!driver_ || !gui_ || !spellMgr_) return;
    if (hoveredSpellId_ == EQ::SPELL_UNKNOWN || hoveredSpellId_ == 0xFFFFFFFF) return;

    const EQ::SpellData* spell = spellMgr_->getSpell(hoveredSpellId_);
    if (!spell) return;

    irr::gui::IGUIFont* font = gui_->getBuiltInFont();
    if (!font) return;

    // Build tooltip text lines
    std::vector<std::wstring> lines;

    // Spell name (header)
    std::wstring nameW(spell->name.begin(), spell->name.end());
    lines.push_back(nameW);

    // Mana cost
    if (spell->mana_cost > 0) {
        lines.push_back(L"Mana: " + std::to_wstring(spell->mana_cost));
    }

    // Cast time
    if (spell->cast_time_ms > 0) {
        float castSec = spell->cast_time_ms / 1000.0f;
        std::wstring castStr = L"Cast: " + std::to_wstring(static_cast<int>(castSec * 10) / 10.0f).substr(0, 4) + L"s";
        lines.push_back(castStr);
    } else {
        lines.push_back(L"Cast: Instant");
    }

    // Recast time
    if (spell->recast_time_ms > 0) {
        float recastSec = spell->recast_time_ms / 1000.0f;
        std::wstring recastStr = L"Recast: " + std::to_wstring(static_cast<int>(recastSec)) + L"s";
        lines.push_back(recastStr);
    }

    // Duration
    if (spell->duration_formula > 0 || spell->duration_cap > 0) {
        int durationTicks = spell->duration_cap;
        if (durationTicks > 0) {
            int durationSec = durationTicks * 6;  // 6 seconds per tick
            if (durationSec >= 60) {
                lines.push_back(L"Duration: " + std::to_wstring(durationSec / 60) + L"m " + std::to_wstring(durationSec % 60) + L"s");
            } else {
                lines.push_back(L"Duration: " + std::to_wstring(durationSec) + L"s");
            }
        }
    }

    // Range
    if (spell->range > 0) {
        lines.push_back(L"Range: " + std::to_wstring(static_cast<int>(spell->range)));
    }

    // Target type
    std::wstring targetStr = L"Target: ";
    switch (spell->target_type) {
        case EQ::SpellTargetType::Self: targetStr += L"Self"; break;
        case EQ::SpellTargetType::Single: targetStr += L"Single"; break;
        case EQ::SpellTargetType::GroupV1:
        case EQ::SpellTargetType::GroupV2: targetStr += L"Group"; break;
        case EQ::SpellTargetType::AECaster: targetStr += L"AE (Caster)"; break;
        case EQ::SpellTargetType::AETarget: targetStr += L"AE (Target)"; break;
        case EQ::SpellTargetType::TargetRing: targetStr += L"Target Ring"; break;
        case EQ::SpellTargetType::Pet: targetStr += L"Pet"; break;
        case EQ::SpellTargetType::Corpse: targetStr += L"Corpse"; break;
        default: targetStr += L"Other"; break;
    }
    lines.push_back(targetStr);

    // Resist type
    if (spell->resist_type != EQ::ResistType::None) {
        std::wstring resistStr = L"Resist: ";
        switch (spell->resist_type) {
            case EQ::ResistType::Magic: resistStr += L"Magic"; break;
            case EQ::ResistType::Fire: resistStr += L"Fire"; break;
            case EQ::ResistType::Cold: resistStr += L"Cold"; break;
            case EQ::ResistType::Poison: resistStr += L"Poison"; break;
            case EQ::ResistType::Disease: resistStr += L"Disease"; break;
            case EQ::ResistType::Chromatic: resistStr += L"Chromatic"; break;
            case EQ::ResistType::Prismatic: resistStr += L"Prismatic"; break;
            default: resistStr += L"Unknown"; break;
        }
        lines.push_back(resistStr);
    }

    // Calculate tooltip dimensions
    int lineHeight = 12;
    int padding = 4;
    int maxWidth = 0;
    for (const auto& line : lines) {
        irr::core::dimension2du sz = font->getDimension(line.c_str());
        if (static_cast<int>(sz.Width) > maxWidth) {
            maxWidth = static_cast<int>(sz.Width);
        }
    }

    int tooltipWidth = maxWidth + padding * 2;
    int tooltipHeight = static_cast<int>(lines.size()) * lineHeight + padding * 2;

    // Position tooltip to the right of mouse, or left if it would go off screen
    int tooltipX = hoveredSpellX_ + 15;
    int tooltipY = hoveredSpellY_;

    // Ensure tooltip stays on screen
    if (tooltipX + tooltipWidth > screenWidth_) {
        tooltipX = hoveredSpellX_ - tooltipWidth - 5;
    }
    if (tooltipY + tooltipHeight > screenHeight_) {
        tooltipY = screenHeight_ - tooltipHeight;
    }

    irr::core::recti tooltipRect(tooltipX, tooltipY, tooltipX + tooltipWidth, tooltipY + tooltipHeight);

    // Draw tooltip background
    driver_->draw2DRectangle(irr::video::SColor(240, 20, 20, 30), tooltipRect);

    // Draw border
    driver_->draw2DRectangleOutline(tooltipRect, irr::video::SColor(255, 100, 100, 140));

    // Draw text lines
    int y = tooltipY + padding;
    for (size_t i = 0; i < lines.size(); i++) {
        irr::video::SColor textColor = (i == 0) ?
            irr::video::SColor(255, 255, 220, 128) :  // Gold for spell name
            irr::video::SColor(255, 200, 200, 200);   // Gray for details

        irr::core::dimension2du sz = font->getDimension(lines[i].c_str());
        font->draw(lines[i].c_str(),
            irr::core::recti(tooltipX + padding, y, tooltipX + padding + sz.Width, y + sz.Height),
            textColor);
        y += lineHeight;
    }
}

void WindowManager::renderLockIndicator() {
    if (!driver_ || !gui_) return;

    irr::gui::IGUIFont* font = gui_->getBuiltInFont();
    if (!font) return;

    bool locked = isUILocked();

    // Create indicator text with lock symbol
    std::wstring indicatorText;
    irr::video::SColor bgColor;
    irr::video::SColor textColor;

    if (locked) {
        indicatorText = L"[=] UI Locked";
        bgColor = irr::video::SColor(180, 40, 40, 40);      // Dark gray background
        textColor = irr::video::SColor(255, 150, 150, 150); // Gray text
    } else {
        indicatorText = L"[O] UI Unlocked - Drag windows to move";
        bgColor = irr::video::SColor(200, 60, 60, 20);      // Dark yellow/brown background
        textColor = irr::video::SColor(255, 255, 220, 100); // Yellow text
    }

    // Calculate text dimensions
    irr::core::dimension2du textSize = font->getDimension(indicatorText.c_str());

    // Position in top-right corner with padding
    const int padding = 6;
    const int margin = 10;
    int width = textSize.Width + padding * 2;
    int height = textSize.Height + padding * 2;
    int x = screenWidth_ - width - margin;
    int y = margin;

    irr::core::recti bgRect(x, y, x + width, y + height);

    // Draw background
    driver_->draw2DRectangle(bgColor, bgRect);

    // Draw border
    irr::video::SColor borderColor = locked ?
        irr::video::SColor(255, 80, 80, 80) :    // Gray border when locked
        irr::video::SColor(255, 200, 180, 60);   // Yellow border when unlocked
    driver_->draw2DRectangleOutline(bgRect, borderColor);

    // Draw text
    irr::core::recti textRect(x + padding, y + padding, x + width - padding, y + height - padding);
    font->draw(indicatorText.c_str(), textRect, textColor);
}

// ============================================================================
// Quantity Slider (for shift+click on stacks)
// ============================================================================

void WindowManager::showQuantitySlider(int16_t slotId, int32_t maxQuantity) {
    if (maxQuantity <= 1) {
        return;  // No point in showing slider for 1 item
    }

    quantitySliderActive_ = true;
    quantitySliderSlot_ = slotId;
    quantitySliderMax_ = maxQuantity;
    quantitySliderValue_ = maxQuantity;  // Default to picking up entire stack
    quantitySliderDragging_ = false;

    // Calculate dialog bounds (centered on screen)
    const int dialogWidth = 200;
    const int dialogHeight = 100;
    int dialogX = (screenWidth_ - dialogWidth) / 2;
    int dialogY = (screenHeight_ - dialogHeight) / 2;

    quantitySliderBounds_ = irr::core::recti(
        dialogX, dialogY,
        dialogX + dialogWidth, dialogY + dialogHeight
    );

    // Slider track (horizontal bar in middle of dialog)
    const int trackHeight = 16;
    const int trackMargin = 20;
    int trackY = dialogY + 40;
    quantitySliderTrackBounds_ = irr::core::recti(
        dialogX + trackMargin, trackY,
        dialogX + dialogWidth - trackMargin, trackY + trackHeight
    );

    // OK and Cancel buttons
    const int buttonWidth = 60;
    const int buttonHeight = 22;
    const int buttonY = dialogY + dialogHeight - buttonHeight - 10;

    quantitySliderOkBounds_ = irr::core::recti(
        dialogX + dialogWidth / 2 - buttonWidth - 10, buttonY,
        dialogX + dialogWidth / 2 - 10, buttonY + buttonHeight
    );

    quantitySliderCancelBounds_ = irr::core::recti(
        dialogX + dialogWidth / 2 + 10, buttonY,
        dialogX + dialogWidth / 2 + buttonWidth + 10, buttonY + buttonHeight
    );

    LOG_DEBUG(MOD_UI, "Quantity slider shown for slot {} (max={})", slotId, maxQuantity);
}

void WindowManager::closeQuantitySlider() {
    quantitySliderActive_ = false;
    quantitySliderSlot_ = inventory::SLOT_INVALID;
    quantitySliderMax_ = 1;
    quantitySliderValue_ = 1;
    quantitySliderDragging_ = false;
    quantitySliderOkHighlighted_ = false;
    quantitySliderCancelHighlighted_ = false;
    LOG_DEBUG(MOD_UI, "Quantity slider closed");
}

void WindowManager::confirmQuantitySlider() {
    if (!quantitySliderActive_ || quantitySliderSlot_ == inventory::SLOT_INVALID) {
        closeQuantitySlider();
        return;
    }

    int16_t slotId = quantitySliderSlot_;
    int32_t quantity = quantitySliderValue_;
    closeQuantitySlider();

    // Pick up the selected quantity
    if (quantity > 0 && invManager_->hasItem(slotId)) {
        LOG_DEBUG(MOD_UI, "Quantity slider confirmed: picking up {} from slot {}", quantity, slotId);
        invManager_->pickupPartialStack(slotId, quantity);
        tooltip_.clear();
    }
}

void WindowManager::renderQuantitySlider() {
    if (!quantitySliderActive_ || !driver_ || !gui_) {
        return;
    }

    // Draw semi-transparent overlay
    driver_->draw2DRectangle(
        irr::video::SColor(128, 0, 0, 0),
        irr::core::recti(0, 0, screenWidth_, screenHeight_)
    );

    // Draw dialog background
    irr::video::SColor bgColor(240, 48, 48, 48);
    driver_->draw2DRectangle(bgColor, quantitySliderBounds_);

    // Draw border
    irr::video::SColor borderColor(255, 100, 100, 100);
    driver_->draw2DRectangleOutline(quantitySliderBounds_, borderColor);

    irr::gui::IGUIFont* font = gui_->getBuiltInFont();

    // Draw title
    if (font) {
        irr::core::recti titleRect(
            quantitySliderBounds_.UpperLeftCorner.X + 10,
            quantitySliderBounds_.UpperLeftCorner.Y + 8,
            quantitySliderBounds_.LowerRightCorner.X - 10,
            quantitySliderBounds_.UpperLeftCorner.Y + 24
        );
        font->draw(L"Select Quantity", titleRect,
                  irr::video::SColor(255, 255, 255, 255), true, false);
    }

    // Draw slider track background
    driver_->draw2DRectangle(
        irr::video::SColor(255, 32, 32, 32),
        quantitySliderTrackBounds_
    );

    // Draw slider track border
    driver_->draw2DRectangleOutline(quantitySliderTrackBounds_,
        irr::video::SColor(255, 80, 80, 80));

    // Draw slider handle
    int trackWidth = quantitySliderTrackBounds_.getWidth() - 10;
    float ratio = static_cast<float>(quantitySliderValue_ - 1) / std::max(1, quantitySliderMax_ - 1);
    int handleX = quantitySliderTrackBounds_.UpperLeftCorner.X + 5 +
                  static_cast<int>(ratio * trackWidth);
    int handleY = quantitySliderTrackBounds_.UpperLeftCorner.Y + 2;
    int handleHeight = quantitySliderTrackBounds_.getHeight() - 4;

    irr::core::recti handleRect(
        handleX - 4, handleY,
        handleX + 4, handleY + handleHeight
    );
    driver_->draw2DRectangle(
        irr::video::SColor(255, 180, 180, 180),
        handleRect
    );

    // Draw quantity value
    if (font) {
        std::wstring valueStr = std::to_wstring(quantitySliderValue_) + L" / " +
                               std::to_wstring(quantitySliderMax_);
        irr::core::recti valueRect(
            quantitySliderTrackBounds_.UpperLeftCorner.X,
            quantitySliderTrackBounds_.LowerRightCorner.Y + 2,
            quantitySliderTrackBounds_.LowerRightCorner.X,
            quantitySliderTrackBounds_.LowerRightCorner.Y + 16
        );
        font->draw(valueStr.c_str(), valueRect,
                  irr::video::SColor(255, 200, 200, 200), true, false);
    }

    // Draw OK button
    irr::video::SColor okColor = quantitySliderOkHighlighted_ ?
        irr::video::SColor(255, 96, 96, 96) : irr::video::SColor(255, 72, 72, 72);
    driver_->draw2DRectangle(okColor, quantitySliderOkBounds_);
    if (font) {
        font->draw(L"OK", quantitySliderOkBounds_,
                  irr::video::SColor(255, 200, 200, 200), true, true);
    }

    // Draw Cancel button
    irr::video::SColor cancelColor = quantitySliderCancelHighlighted_ ?
        irr::video::SColor(255, 96, 96, 96) : irr::video::SColor(255, 72, 72, 72);
    driver_->draw2DRectangle(cancelColor, quantitySliderCancelBounds_);
    if (font) {
        font->draw(L"Cancel", quantitySliderCancelBounds_,
                  irr::video::SColor(255, 200, 200, 200), true, true);
    }
}

} // namespace ui
} // namespace eqt
