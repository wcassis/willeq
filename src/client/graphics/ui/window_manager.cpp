#include "client/graphics/ui/window_manager.h"
#include "client/graphics/entity_renderer.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/trade_manager.h"
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

    inventoryWindow_->setCurrencyClickCallback([this](CurrencyType type, uint32_t maxAmount) {
        currencyClickSource_ = CurrencyClickSource::Inventory;
        handleCurrencyClick(type, maxAmount);
    });

    inventoryWindow_->setReadItemCallback([this](const std::string& bookText, uint8_t bookType) {
        if (readItemCallback_) {
            readItemCallback_(bookText, bookType);
        }
    });

    // Create money input dialog
    moneyInputDialog_ = std::make_unique<MoneyInputDialog>();
    moneyInputDialog_->setOnConfirm([this](CurrencyType type, uint32_t amount) {
        handleMoneyInputConfirm(type, amount);
    });

    // Create loot window (initially hidden)
    lootWindow_ = std::make_unique<LootWindow>(invManager_, this);
    lootWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });
    lootWindow_->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
        handleLootSlotHover(slotId, mouseX, mouseY);
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

    // Create tradeskill container window (initially hidden)
    tradeskillWindow_ = std::make_unique<TradeskillContainerWindow>(invManager_);
    tradeskillWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });
    tradeskillWindow_->setSlotClickCallback([this](int16_t slotId, bool shift, bool ctrl) {
        handleTradeskillSlotClick(slotId, shift, ctrl);
    });
    tradeskillWindow_->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
        handleTradeskillSlotHover(slotId, mouseX, mouseY);
    });

    // Create hotbar window (visible by default)
    initHotbarWindow();
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

    // Auto-save layout when locking
    if (locked) {
        saveUILayout();
        updateWindowHoverStates(mouseX_, mouseY_);
    }

    // Show status message in chat window
    if (chatWindow_) {
        std::string message = locked ?
            "UI Locked - Layout saved" :
            "UI Unlocked - Drag windows to reposition";
        chatWindow_->addSystemMessage(message, ChatChannel::System);
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

    // Hotbar window
    if (hotbarWindow_) {
        settings.hotbar().window.x = hotbarWindow_->getX();
        settings.hotbar().window.y = hotbarWindow_->getY();
        settings.hotbar().window.visible = hotbarWindow_->isVisible();
    }

    // Skills window
    if (skillsWindow_) {
        settings.skills().window.x = skillsWindow_->getX();
        settings.skills().window.y = skillsWindow_->getY();
        settings.skills().window.visible = skillsWindow_->isVisible();
    }

    // Trade window
    if (tradeWindow_) {
        settings.trade().window.x = tradeWindow_->getX();
        settings.trade().window.y = tradeWindow_->getY();
        settings.trade().window.visible = tradeWindow_->isOpen();
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

    // Hotbar window
    if (hotbarWindow_) {
        const auto& hotbarSettings = settings.hotbar();
        if (hotbarSettings.window.x >= 0 && hotbarSettings.window.y >= 0) {
            hotbarWindow_->setPosition(hotbarSettings.window.x, hotbarSettings.window.y);
        }
    }

    // Skills window
    if (skillsWindow_) {
        const auto& skillsSettings = settings.skills();
        if (skillsSettings.window.x >= 0 && skillsSettings.window.y >= 0) {
            skillsWindow_->setPosition(skillsSettings.window.x, skillsSettings.window.y);
        }
    }

    // Trade window
    if (tradeWindow_) {
        // Default position is centered
        int defaultX = (screenWidth_ - tradeWindow_->getWidth()) / 2;
        int defaultY = (screenHeight_ - tradeWindow_->getHeight()) / 2;
        int x = UISettings::resolvePosition(settings.trade().window.x, screenWidth_, defaultX);
        int y = UISettings::resolvePosition(settings.trade().window.y, screenHeight_, defaultY);
        tradeWindow_->setPosition(x, y);
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
    closeTradeskillContainer();
}

void WindowManager::toggleBagWindow(int16_t generalSlot) {
    // Check if this is a tradeskill container
    const inventory::ItemInstance* item = invManager_->getItem(generalSlot);
    if (item && item->isContainer() && inventory::isTradeskillContainerType(item->bagType)) {
        // For tradeskill containers, check if this specific one is already open
        if (isTradeskillContainerOpen() && tradeskillWindow_ &&
            !tradeskillWindow_->isWorldContainer() &&
            tradeskillWindow_->getContainerSlot() == generalSlot) {
            // Close it
            LOG_DEBUG(MOD_UI, "Toggling tradeskill container closed for slot {}", generalSlot);
            closeTradeskillContainer();
        } else {
            // Open it (openBagWindow handles tradeskill containers)
            openBagWindow(generalSlot);
        }
        return;
    }

    // Regular bag toggle
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

    // Check if this is a tradeskill container
    if (inventory::isTradeskillContainerType(item->bagType)) {
        // Open tradeskill container window instead of regular bag window
        LOG_DEBUG(MOD_UI, "Opening tradeskill container for inventory item at slot {} (bagType={})",
                  generalSlot, item->bagType);

        // Close any existing tradeskill window first
        closeTradeskillContainer();

        // Open the tradeskill container window for this inventory item
        openTradeskillContainerForItem(generalSlot, item->name,
                                       item->bagType, item->bagSlots);
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

    bagWindow->setReadItemCallback([this](const std::string& bookText, uint8_t bookType) {
        if (readItemCallback_) {
            readItemCallback_(bookText, bookType);
        }
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

// ============================================================================
// Bank window management
// ============================================================================

void WindowManager::openBankWindow() {
    if (!invManager_) {
        return;
    }

    // Create bank window if it doesn't exist
    if (!bankWindow_) {
        bankWindow_ = std::make_unique<BankWindow>(invManager_);

        // Set up callbacks
        bankWindow_->setBagClickCallback([this](int16_t bankSlot) {
            handleBankBagOpenClick(bankSlot);
        });

        bankWindow_->setSlotClickCallback([this](int16_t slotId, bool shift, bool ctrl) {
            handleBankSlotClick(slotId, shift, ctrl);
        });

        bankWindow_->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
            handleSlotHover(slotId, mouseX, mouseY);
        });

        bankWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
            return iconLoader_.getIcon(iconId);
        });

        bankWindow_->setCurrencyClickCallback([this](CurrencyType type, uint32_t maxAmount) {
            currencyClickSource_ = CurrencyClickSource::Bank;
            handleCurrencyClick(type, maxAmount);
        });

        bankWindow_->setReadItemCallback([this](const std::string& bookText, uint8_t bookType) {
            if (readItemCallback_) {
                readItemCallback_(bookText, bookType);
            }
        });

        bankWindow_->setCloseCallback([this]() {
            closeAllBankBagWindows();
            if (onBankCloseCallback_) {
                onBankCloseCallback_();
            }
        });

        bankWindow_->setCurrencyConvertCallback([this](int32_t fromCoinType, int32_t amount) {
            if (onBankCurrencyConvertCallback_) {
                onBankCurrencyConvertCallback_(fromCoinType, amount);
            }
        });
    }

    // Update currency display with bank currency (not inventory currency)
    bankWindow_->setCurrency(bankPlatinum_, bankGold_, bankSilver_, bankCopper_);

    positionBankWindow();
    bankWindow_->show();

    LOG_DEBUG(MOD_UI, "Bank window opened");
}

void WindowManager::closeBankWindow() {
    closeAllBankBagWindows();

    if (bankWindow_) {
        bankWindow_->hide();
    }

    tooltip_.clear();
    LOG_DEBUG(MOD_UI, "Bank window closed");
}

void WindowManager::toggleBankWindow() {
    if (isBankWindowOpen()) {
        closeBankWindow();
    } else {
        openBankWindow();
    }
}

bool WindowManager::isBankWindowOpen() const {
    return bankWindow_ && bankWindow_->isVisible();
}

void WindowManager::openBankBagWindow(int16_t bankSlot) {
    // Validate this is a bank or shared bank slot
    if (!inventory::isBankSlot(bankSlot) && !inventory::isSharedBankSlot(bankSlot)) {
        return;
    }

    // Check if slot contains a bag
    const inventory::ItemInstance* item = invManager_->getItem(bankSlot);
    if (!item || !item->isContainer()) {
        return;
    }

    // Don't open if already open
    if (isBankBagWindowOpen(bankSlot)) {
        return;
    }

    // Create bag window for bank container
    auto bagWindow = std::make_unique<BagWindow>(bankSlot, item->bagSlots, invManager_);

    // Set up callbacks
    bagWindow->setSlotClickCallback([this](int16_t slotId, bool shift, bool ctrl) {
        handleBankSlotClick(slotId, shift, ctrl);
    });

    bagWindow->setSlotHoverCallback([this](int16_t slotId, int mouseX, int mouseY) {
        handleSlotHover(slotId, mouseX, mouseY);
    });

    bagWindow->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });

    bagWindow->setReadItemCallback([this](const std::string& bookText, uint8_t bookType) {
        if (readItemCallback_) {
            readItemCallback_(bookText, bookType);
        }
    });

    bagWindow->show();
    bankBagWindows_[bankSlot] = std::move(bagWindow);

    tileBankBagWindows();
}

void WindowManager::closeBankBagWindow(int16_t bankSlot) {
    auto it = bankBagWindows_.find(bankSlot);
    if (it != bankBagWindows_.end()) {
        bankBagWindows_.erase(it);
        tileBankBagWindows();
    }
}

void WindowManager::closeAllBankBagWindows() {
    bankBagWindows_.clear();
}

bool WindowManager::isBankBagWindowOpen(int16_t bankSlot) const {
    return bankBagWindows_.find(bankSlot) != bankBagWindows_.end();
}

void WindowManager::setOnBankClose(BankCloseCallback callback) {
    onBankCloseCallback_ = callback;
    if (bankWindow_) {
        bankWindow_->setCloseCallback([this]() {
            closeAllBankBagWindows();
            if (onBankCloseCallback_) {
                onBankCloseCallback_();
            }
        });
    }
}

void WindowManager::setOnBankCurrencyMove(BankCurrencyMoveCallback callback) {
    onBankCurrencyMoveCallback_ = callback;
}

void WindowManager::setOnBankCurrencyConvert(BankCurrencyConvertCallback callback) {
    onBankCurrencyConvertCallback_ = callback;
    if (bankWindow_) {
        bankWindow_->setCurrencyConvertCallback([this](int32_t fromCoinType, int32_t amount) {
            if (onBankCurrencyConvertCallback_) {
                onBankCurrencyConvertCallback_(fromCoinType, amount);
            }
        });
    }
}

void WindowManager::positionBankWindow() {
    if (!bankWindow_) {
        return;
    }

    // Position bank window on the left side of the screen
    // (opposite side from inventory which is typically on the right)
    int x = 50;
    int y = 50;

    bankWindow_->setPosition(x, y);
}

void WindowManager::tileBankBagWindows() {
    if (bankBagWindows_.empty() || !bankWindow_) {
        return;
    }

    // Position bank bag windows below the bank window
    int startX = bankWindow_->getX();
    int startY = bankWindow_->getY() + bankWindow_->getHeight() + 10;
    int curX = startX;
    int curY = startY;
    int maxRowHeight = 0;

    for (auto& [slot, window] : bankBagWindows_) {
        // Check if we need to wrap to next row
        if (curX + window->getWidth() > screenWidth_ - 50) {
            curX = startX;
            curY += maxRowHeight + 5;
            maxRowHeight = 0;
        }

        window->setPosition(curX, curY);
        curX += window->getWidth() + 5;
        maxRowHeight = std::max(maxRowHeight, window->getHeight());
    }
}

void WindowManager::handleBankSlotClick(int16_t slotId, bool shift, bool ctrl) {
    LOG_DEBUG(MOD_UI, "WindowManager handleBankSlotClick: slotId={} shift={} ctrl={}", slotId, shift, ctrl);

    if (slotId == inventory::SLOT_INVALID) {
        return;
    }

    // Check if we're holding a cursor item
    if (invManager_->hasCursorItem()) {
        // Try to place item in bank slot
        if (invManager_->canPlaceItemInSlot(invManager_->getCursorItem(), slotId)) {
            LOG_DEBUG(MOD_UI, "WindowManager Placing cursor item in bank slot {}", slotId);
            invManager_->placeItem(slotId);
            tooltip_.clear();
        } else {
            LOG_DEBUG(MOD_UI, "WindowManager Cannot place item in bank slot {}", slotId);
        }
        return;
    }

    // No cursor item - try to pick up from bank
    const inventory::ItemInstance* item = invManager_->getItem(slotId);
    if (item) {
        // Handle stackable items
        if (item->stackable && item->quantity > 1) {
            if (ctrl) {
                // Ctrl+click: pick up just 1 item from the stack
                LOG_DEBUG(MOD_UI, "WindowManager Ctrl+click: picking up 1 from bank stack of {} at slot {}", item->quantity, slotId);
                invManager_->pickupPartialStack(slotId, 1);
                tooltip_.clear();
                return;
            } else if (shift) {
                // Shift+click: show quantity slider
                LOG_DEBUG(MOD_UI, "WindowManager Shift+click: showing quantity slider for bank stack of {} at slot {}", item->quantity, slotId);
                showQuantitySlider(slotId, item->quantity);
                return;
            }
        }

        // If picking up a bag from bank, close its window
        if (item->isContainer()) {
            closeBankBagWindow(slotId);
        }

        LOG_DEBUG(MOD_UI, "WindowManager Picking up item from bank slot {}", slotId);
        pickupItem(slotId);
    } else {
        LOG_DEBUG(MOD_UI, "WindowManager No item in bank slot {}", slotId);
    }
}

void WindowManager::handleBankBagOpenClick(int16_t bankSlot) {
    // Can't open bags while holding an item
    if (invManager_->hasCursorItem()) {
        return;
    }

    // Toggle bank bag window
    if (isBankBagWindowOpen(bankSlot)) {
        closeBankBagWindow(bankSlot);
    } else {
        openBankBagWindow(bankSlot);
    }
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

// ============================================================================
// Trade Window Management
// ============================================================================

void WindowManager::initTradeWindow(TradeManager* tradeMgr) {
    tradeManager_ = tradeMgr;
    tradeWindow_ = std::make_unique<TradeWindow>(invManager_, this);
    tradeWindow_->setTradeManager(tradeMgr);
    tradeWindow_->setIconLookupCallback([this](uint32_t iconId) -> irr::video::ITexture* {
        return iconLoader_.getIcon(iconId);
    });
    tradeWindow_->setSlotClickCallback([this](int16_t tradeSlot, bool shift, bool ctrl) {
        handleTradeSlotClick(tradeSlot, shift, ctrl);
    });
    tradeWindow_->setMoneyAreaClickCallback([this]() {
        handleTradeMoneyAreaClick();
    });

    // Create trade request dialog
    tradeRequestDialog_ = std::make_unique<TradeRequestDialog>();

    LOG_DEBUG(MOD_UI, "Trade window and request dialog initialized");
}

void WindowManager::openTradeWindow(uint32_t partnerSpawnId, const std::string& partnerName, bool isNpcTrade) {
    if (!tradeWindow_) {
        LOG_WARN(MOD_UI, "Trade window not initialized");
        return;
    }

    // Set up callbacks before opening
    tradeWindow_->setOnAccept(onTradeAcceptCallback_);
    tradeWindow_->setOnCancel(onTradeCancelCallback_);

    tradeWindow_->open(partnerSpawnId, partnerName, isNpcTrade);

    // Use saved position if available, otherwise center on screen
    const auto& settings = UISettings::instance();
    int defaultX = (screenWidth_ - tradeWindow_->getWidth()) / 2;
    int defaultY = (screenHeight_ - tradeWindow_->getHeight()) / 2;
    int x = UISettings::resolvePosition(settings.trade().window.x, screenWidth_, defaultX);
    int y = UISettings::resolvePosition(settings.trade().window.y, screenHeight_, defaultY);
    tradeWindow_->setPosition(x, y);

    // If player has an item on cursor, automatically add it to the first trade slot
    if (invManager_ && invManager_->hasCursorItem()) {
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        if (cursorItem && !cursorItem->noDrop) {
            // Find first empty trade slot
            int16_t firstEmptySlot = inventory::SLOT_INVALID;
            for (int i = 0; i < 8; i++) {
                int16_t tradeSlot = inventory::TRADE_BEGIN + i;
                if (!invManager_->hasItem(tradeSlot)) {
                    firstEmptySlot = tradeSlot;
                    break;
                }
            }

            if (firstEmptySlot != inventory::SLOT_INVALID) {
                int tradeSlotIndex = firstEmptySlot - inventory::TRADE_BEGIN;
                int16_t sourceSlot = invManager_->getCursorSourceSlot();
                LOG_DEBUG(MOD_UI, "Auto-placing cursor item (from slot {}) in trade slot {} on window open",
                          sourceSlot, firstEmptySlot);

                // Notify TradeManager about the item being added to trade
                if (tradeManager_) {
                    tradeManager_->addItemToTrade(sourceSlot, tradeSlotIndex);
                }

                placeItem(firstEmptySlot);
            }
        } else if (cursorItem && cursorItem->noDrop) {
            LOG_DEBUG(MOD_UI, "Cursor item '{}' is NO_DROP, not auto-adding to trade", cursorItem->name);
        }
    }
    // If player has money on cursor, automatically add it to the trade
    else if (invManager_ && invManager_->hasCursorMoney() && tradeManager_) {
        uint32_t amount = invManager_->getCursorMoneyAmount();
        auto cursorType = invManager_->getCursorMoneyType();

        uint32_t pp = 0, gp = 0, sp = 0, cp = 0;
        switch (cursorType) {
            case inventory::InventoryManager::CursorMoneyType::Platinum:
                pp = amount;
                LOG_DEBUG(MOD_UI, "Auto-placing {} platinum in trade on window open", amount);
                break;
            case inventory::InventoryManager::CursorMoneyType::Gold:
                gp = amount;
                LOG_DEBUG(MOD_UI, "Auto-placing {} gold in trade on window open", amount);
                break;
            case inventory::InventoryManager::CursorMoneyType::Silver:
                sp = amount;
                LOG_DEBUG(MOD_UI, "Auto-placing {} silver in trade on window open", amount);
                break;
            case inventory::InventoryManager::CursorMoneyType::Copper:
                cp = amount;
                LOG_DEBUG(MOD_UI, "Auto-placing {} copper in trade on window open", amount);
                break;
            default:
                break;
        }

        if (pp > 0 || gp > 0 || sp > 0 || cp > 0) {
            tradeManager_->setOwnMoney(pp, gp, sp, cp);
            invManager_->clearCursorMoney();
            refreshCurrencyDisplay();
        }
    }

    // Dismiss trade request dialog if open
    dismissTradeRequest();
}

void WindowManager::closeTradeWindow(bool sendCancel) {
    if (tradeWindow_ && tradeWindow_->isOpen()) {
        tradeWindow_->close(sendCancel);
    }
}

bool WindowManager::isTradeWindowOpen() const {
    return tradeWindow_ && tradeWindow_->isOpen();
}

void WindowManager::showTradeRequest(uint32_t spawnId, const std::string& playerName) {
    if (!tradeRequestDialog_) {
        LOG_WARN(MOD_UI, "Trade request dialog not initialized");
        return;
    }

    // Set up callbacks
    tradeRequestDialog_->setOnAccept(onTradeRequestAcceptCallback_);
    tradeRequestDialog_->setOnDecline(onTradeRequestDeclineCallback_);

    tradeRequestDialog_->show(spawnId, playerName);

    // Position in center of screen
    int dialogWidth = tradeRequestDialog_->getWidth();
    int dialogHeight = tradeRequestDialog_->getHeight();
    int x = (screenWidth_ - dialogWidth) / 2;
    int y = (screenHeight_ - dialogHeight) / 2;
    tradeRequestDialog_->setPosition(x, y);
}

void WindowManager::dismissTradeRequest() {
    if (tradeRequestDialog_ && tradeRequestDialog_->isShown()) {
        tradeRequestDialog_->dismiss();
    }
}

bool WindowManager::isTradeRequestShown() const {
    return tradeRequestDialog_ && tradeRequestDialog_->isShown();
}

bool WindowManager::isMoneyInputDialogShown() const {
    return moneyInputDialog_ && moneyInputDialog_->isShown();
}

void WindowManager::setTradePartnerItem(int slot, std::unique_ptr<inventory::ItemInstance> item) {
    if (tradeWindow_) {
        tradeWindow_->setPartnerItem(slot, std::move(item));
    }
}

void WindowManager::clearTradePartnerItem(int slot) {
    if (tradeWindow_) {
        tradeWindow_->clearPartnerSlot(slot);
    }
}

void WindowManager::setTradeOwnMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp) {
    if (tradeWindow_) {
        tradeWindow_->setOwnMoney(pp, gp, sp, cp);
    }
}

void WindowManager::setTradePartnerMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp) {
    if (tradeWindow_) {
        tradeWindow_->setPartnerMoney(pp, gp, sp, cp);
    }
}

void WindowManager::setTradeOwnAccepted(bool accepted) {
    if (tradeWindow_) {
        tradeWindow_->setOwnAccepted(accepted);
    }
}

void WindowManager::setTradePartnerAccepted(bool accepted) {
    if (tradeWindow_) {
        tradeWindow_->setPartnerAccepted(accepted);
    }
}

void WindowManager::setOnTradeAccept(TradeAcceptCallback callback) {
    onTradeAcceptCallback_ = callback;
    if (tradeWindow_) {
        tradeWindow_->setOnAccept(callback);
    }
}

void WindowManager::setOnTradeCancel(TradeCancelCallback callback) {
    onTradeCancelCallback_ = callback;
    if (tradeWindow_) {
        tradeWindow_->setOnCancel(callback);
    }
}

void WindowManager::setOnTradeRequestAccept(TradeRequestAcceptCallback callback) {
    onTradeRequestAcceptCallback_ = callback;
    if (tradeRequestDialog_) {
        tradeRequestDialog_->setOnAccept(callback);
    }
}

void WindowManager::setOnTradeRequestDecline(TradeRequestDeclineCallback callback) {
    onTradeRequestDeclineCallback_ = callback;
    if (tradeRequestDialog_) {
        tradeRequestDialog_->setOnDecline(callback);
    }
}

void WindowManager::setOnTradeError(TradeErrorCallback callback) {
    onTradeErrorCallback_ = callback;
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

    // Route to money input dialog if it's shown
    if (moneyInputDialog_ && moneyInputDialog_->isShown()) {
        // Handle numeric input, backspace, enter, and escape
        bool isBackspace = (key == irr::KEY_BACK);
        bool isEnter = (key == irr::KEY_RETURN);
        bool isEscape = (key == irr::KEY_ESCAPE);
        wchar_t keyChar = 0;
        if (key >= irr::KEY_KEY_0 && key <= irr::KEY_KEY_9) {
            keyChar = L'0' + (key - irr::KEY_KEY_0);
        } else if (key >= irr::KEY_NUMPAD0 && key <= irr::KEY_NUMPAD9) {
            keyChar = L'0' + (key - irr::KEY_NUMPAD0);
        }
        if (keyChar != 0 || isBackspace || isEnter || isEscape) {
            bool handled = moneyInputDialog_->handleKeyInput(keyChar, isBackspace, isEnter, isEscape);
            // If ESC wasn't handled (input wasn't focused), dismiss the dialog
            if (isEscape && !handled) {
                moneyInputDialog_->dismiss();
            }
            return true;
        }
        return true;  // Consume all keys when dialog is open
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
            // Determine target slot from click position
            int16_t targetSlot = inventory::SLOT_INVALID;
            if (clickedOnInventory && inventoryWindow_) {
                targetSlot = inventoryWindow_->getSlotAtPosition(x, y);
            } else if (clickedOnBag) {
                for (auto& [slotId, bagWindow] : bagWindows_) {
                    if (bagWindow->containsPoint(x, y)) {
                        targetSlot = bagWindow->getSlotAtPosition(x, y);
                        break;
                    }
                }
            }

            // Store target slot for auto-placement when item arrives on cursor
            pendingLootTargetSlot_ = targetSlot;
            LOG_DEBUG(MOD_UI, "Loot UI Setting pending target slot: {}", pendingLootTargetSlot_);

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
        if (spellGemPanel_->handleMouseDown(x, y, leftButton, shift, ctrl)) {
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

    // Check skills window
    if (skillsWindow_ && skillsWindow_->isVisible()) {
        if (skillsWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check note window
    if (noteWindow_ && noteWindow_->isVisible()) {
        if (noteWindow_->handleMouseDown(x, y, leftButton, shift)) {
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

    // Check trade request dialog first (modal)
    if (tradeRequestDialog_ && tradeRequestDialog_->isShown()) {
        if (tradeRequestDialog_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check money input dialog (modal)
    if (moneyInputDialog_ && moneyInputDialog_->isShown()) {
        if (moneyInputDialog_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check trade window (if open)
    if (tradeWindow_ && tradeWindow_->isOpen()) {
        if (tradeWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Check bank bag windows (on top of bank window)
    for (auto& [slotId, bagWindow] : bankBagWindows_) {
        if (bagWindow->handleMouseDown(x, y, leftButton, shift, ctrl)) {
            return true;
        }
    }

    // Check bank window (if open)
    if (bankWindow_ && bankWindow_->isVisible()) {
        if (bankWindow_->handleMouseDown(x, y, leftButton, shift, ctrl)) {
            return true;
        }
    }

    // Check tradeskill container window (if open)
    if (tradeskillWindow_ && tradeskillWindow_->isOpen()) {
        if (tradeskillWindow_->handleMouseDown(x, y, leftButton, shift, ctrl)) {
            return true;
        }
    }

    // Check hotbar window - handle cursor placement/swap
    if (hotbarWindow_ && hotbarWindow_->isVisible() && hotbarWindow_->containsPoint(x, y)) {
        if (leftButton && hotbarCursor_.hasItem()) {
            // Get button at position
            irr::core::recti contentArea = hotbarWindow_->getHotbarContentArea();
            int relX = x - contentArea.UpperLeftCorner.X;
            int relY = y - contentArea.UpperLeftCorner.Y;
            int buttonIndex = hotbarWindow_->getButtonAtPosition(relX, relY);

            if (buttonIndex >= 0) {
                const HotbarButton& targetButton = hotbarWindow_->getButton(buttonIndex);
                if (targetButton.type != HotbarButtonType::Empty) {
                    // Swap: save old button, place cursor, put old on cursor
                    HotbarButtonType oldType = targetButton.type;
                    uint32_t oldId = targetButton.id;
                    std::string oldEmoteText = targetButton.emoteText;
                    uint32_t oldIconId = targetButton.iconId;

                    // Place cursor on button
                    hotbarWindow_->setButton(buttonIndex, hotbarCursor_.getType(),
                                              hotbarCursor_.getId(), hotbarCursor_.getEmoteText(),
                                              hotbarCursor_.getIconId());

                    // Put old button on cursor
                    setHotbarCursor(oldType, oldId, oldEmoteText, oldIconId);
                } else {
                    // Empty slot: just place cursor item
                    hotbarWindow_->setButton(buttonIndex, hotbarCursor_.getType(),
                                              hotbarCursor_.getId(), hotbarCursor_.getEmoteText(),
                                              hotbarCursor_.getIconId());
                    clearHotbarCursor();
                }
                return true;
            }
        }

        // Let hotbar window handle other clicks
        if (hotbarWindow_->handleMouseDown(x, y, leftButton, shift, ctrl)) {
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

        // Don't return cursor item here - let click pass through to world
        // so targeting callback can check if we clicked on a player to trade
    }

    // Check chat window (behind other windows)
    if (chatWindow_ && chatWindow_->isVisible()) {
        if (chatWindow_->handleMouseDown(x, y, leftButton, shift)) {
            return true;
        }
    }

    // Clear hotbar cursor when clicking outside all windows with cursor active
    if (leftButton && hotbarCursor_.hasItem()) {
        // Check if click was on any hotbar-related source (spell panel, inventory)
        bool clickedSource = false;
        if (spellGemPanel_ && spellGemPanel_->isVisible() && spellGemPanel_->containsPoint(x, y)) {
            clickedSource = true;
        }
        if (inventoryWindow_ && inventoryWindow_->isVisible() && inventoryWindow_->containsPoint(x, y)) {
            clickedSource = true;
        }
        for (auto& [slotId, bagWindow] : bagWindows_) {
            if (bagWindow->containsPoint(x, y)) {
                clickedSource = true;
                break;
            }
        }
        if (hotbarWindow_ && hotbarWindow_->isVisible() && hotbarWindow_->containsPoint(x, y)) {
            clickedSource = true;
        }

        // If clicked outside all hotbar-related windows, clear cursor
        if (!clickedSource) {
            clearHotbarCursor();
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

    // Check skills window
    if (skillsWindow_ && skillsWindow_->isVisible()) {
        if (skillsWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check note window
    if (noteWindow_ && noteWindow_->isVisible()) {
        if (noteWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check trade window
    if (tradeWindow_ && tradeWindow_->isOpen()) {
        if (tradeWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check bank bag windows
    for (auto& [slotId, bagWindow] : bankBagWindows_) {
        if (bagWindow->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check bank window
    if (bankWindow_ && bankWindow_->isVisible()) {
        if (bankWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check tradeskill container window
    if (tradeskillWindow_ && tradeskillWindow_->isOpen()) {
        if (tradeskillWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check money input dialog
    if (moneyInputDialog_ && moneyInputDialog_->isShown()) {
        if (moneyInputDialog_->handleMouseUp(x, y, leftButton)) {
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

    // Check hotbar window (for drag release)
    if (hotbarWindow_ && hotbarWindow_->isVisible()) {
        if (hotbarWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check group window
    if (groupWindow_ && groupWindow_->isVisible()) {
        if (groupWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check player status window
    if (playerStatusWindow_ && playerStatusWindow_->isVisible()) {
        if (playerStatusWindow_->handleMouseUp(x, y, leftButton)) {
            return true;
        }
    }

    // Check spell gem panel
    if (spellGemPanel_ && spellGemPanel_->isVisible()) {
        if (spellGemPanel_->handleMouseUp(x, y, leftButton)) {
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

    // Check player status window (for dragging when UI unlocked)
    if (playerStatusWindow_ && playerStatusWindow_->isVisible()) {
        playerStatusWindow_->handleMouseMove(x, y);
        // Don't return true - allow other windows to update their hover state
    }

    // Check skills window
    if (skillsWindow_ && skillsWindow_->isVisible()) {
        skillsWindow_->handleMouseMove(x, y);
        // Don't return true - allow other windows to update their hover state
    }

    // Check note window
    if (noteWindow_ && noteWindow_->isVisible()) {
        noteWindow_->handleMouseMove(x, y);
        // Don't return true - allow other windows to update their hover state
    }

    // Check loot window
    if (lootWindow_ && lootWindow_->isOpen()) {
        if (lootWindow_->handleMouseMove(x, y)) {
            // Update tooltip for loot window items
            // Note: getHighlightedSlot returns display index, need to convert to corpse slot
            int16_t displayIndex = lootWindow_->getHighlightedSlot();
            if (displayIndex != inventory::SLOT_INVALID) {
                int16_t corpseSlot = lootWindow_->getCorpseSlotFromDisplayIndex(displayIndex);
                const inventory::ItemInstance* item = (corpseSlot != inventory::SLOT_INVALID)
                    ? lootWindow_->getItem(corpseSlot) : nullptr;
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

    // Check trade request dialog
    if (tradeRequestDialog_ && tradeRequestDialog_->isShown()) {
        if (tradeRequestDialog_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check money input dialog
    if (moneyInputDialog_ && moneyInputDialog_->isShown()) {
        if (moneyInputDialog_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check trade window
    if (tradeWindow_ && tradeWindow_->isOpen()) {
        if (tradeWindow_->handleMouseMove(x, y)) {
            // Update tooltip for trade window items
            int16_t highlightedSlot = tradeWindow_->getHighlightedSlot();
            if (highlightedSlot != inventory::SLOT_INVALID) {
                bool isOwn = tradeWindow_->isHighlightedSlotOwn();
                const inventory::ItemInstance* item = tradeWindow_->getDisplayedItem(highlightedSlot, isOwn);
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

    // Check bank bag windows
    for (auto& [slotId, bagWindow] : bankBagWindows_) {
        if (bagWindow->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check bank window
    if (bankWindow_ && bankWindow_->isVisible()) {
        if (bankWindow_->handleMouseMove(x, y)) {
            return true;
        }
    }

    // Check tradeskill container window
    if (tradeskillWindow_ && tradeskillWindow_->isOpen()) {
        if (tradeskillWindow_->handleMouseMove(x, y)) {
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

    // Check hotbar window (for drag movement and button hover)
    if (hotbarWindow_ && hotbarWindow_->isVisible()) {
        if (hotbarWindow_->handleMouseMove(x, y)) {
            // Don't return true - allow hover states to update
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
        if (skillsWindow_) skillsWindow_->setHovered(false);
        if (spellBookWindow_) spellBookWindow_->setHovered(false);
        if (hotbarWindow_) hotbarWindow_->setHovered(false);
        if (playerStatusWindow_) playerStatusWindow_->setHovered(false);
        if (spellGemPanel_) spellGemPanel_->setHovered(false);
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
    if (skillsWindow_) {
        skillsWindow_->setHovered(skillsWindow_->isVisible() && skillsWindow_->containsPoint(x, y));
    }
    if (spellBookWindow_) {
        spellBookWindow_->setHovered(spellBookWindow_->isVisible() && spellBookWindow_->containsPoint(x, y));
    }
    if (hotbarWindow_) {
        hotbarWindow_->setHovered(hotbarWindow_->isVisible() && hotbarWindow_->containsPoint(x, y));
    }
    if (playerStatusWindow_) {
        playerStatusWindow_->setHovered(playerStatusWindow_->isVisible() && playerStatusWindow_->containsPoint(x, y));
    }
    if (spellGemPanel_) {
        spellGemPanel_->setHovered(spellGemPanel_->isVisible() && spellGemPanel_->containsPoint(x, y));
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

    // Route mouse wheel to skills window if visible and mouse is over it
    if (skillsWindow_ && skillsWindow_->isVisible()) {
        if (skillsWindow_->containsPoint(mouseX_, mouseY_)) {
            return skillsWindow_->handleMouseWheel(delta);
        }
    }

    // Route mouse wheel to note window if visible and mouse is over it
    if (noteWindow_ && noteWindow_->isVisible()) {
        if (noteWindow_->containsPoint(mouseX_, mouseY_)) {
            return noteWindow_->handleMouseWheel(delta);
        }
    }

    return false;
}

void WindowManager::render() {
    if (!driver_ || !gui_) {
        return;
    }

    // Check for pending loot auto-placement
    // When user clicks inventory with loot cursor, we send loot request and store target slot.
    // When item arrives on cursor, we auto-place it to avoid requiring a second click.
    if (pendingLootTargetSlot_ != inventory::SLOT_INVALID && invManager_ && invManager_->hasCursorItem()) {
        LOG_DEBUG(MOD_UI, "Auto-placing looted item to pending slot {}", pendingLootTargetSlot_);
        int16_t targetSlot = pendingLootTargetSlot_;
        pendingLootTargetSlot_ = inventory::SLOT_INVALID;  // Clear before placing to avoid recursion

        // Check if we should stack instead of swap
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = invManager_->getItem(targetSlot);

        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId) {
            // Same stackable item - try to add to stack
            if (targetItem->stackSize > 0 && targetItem->quantity < targetItem->stackSize) {
                LOG_DEBUG(MOD_UI, "Auto-stacking looted item onto slot {}", targetSlot);
                invManager_->placeOnMatchingStack(targetSlot);
            } else {
                // Stack is full or stackSize is 0 (not stackable), do normal placement (will swap)
                invManager_->placeItem(targetSlot);
            }
        } else {
            // Not stackable or different items - normal placement
            if (!invManager_->placeItem(targetSlot)) {
                LOG_DEBUG(MOD_UI, "Target slot {} occupied, item may have swapped", targetSlot);
            }
        }
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

    // Render hotbar window
    if (hotbarWindow_ && hotbarWindow_->isVisible()) {
        hotbarWindow_->render(driver_, gui_);
    }

    // Render skills window
    if (skillsWindow_ && skillsWindow_->isVisible()) {
        skillsWindow_->render(driver_, gui_);
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

    // Render trade window
    if (tradeWindow_ && tradeWindow_->isOpen()) {
        tradeWindow_->render(driver_, gui_);
    }

    // Render bank window
    if (bankWindow_ && bankWindow_->isVisible()) {
        bankWindow_->render(driver_, gui_);
    }

    // Render bank bag windows
    for (auto& [slotId, bagWindow] : bankBagWindows_) {
        bagWindow->render(driver_, gui_);
    }

    // Render tradeskill container window
    if (tradeskillWindow_ && tradeskillWindow_->isOpen()) {
        tradeskillWindow_->render(driver_, gui_);
    }

    // Render trade request dialog (on top of other windows)
    if (tradeRequestDialog_ && tradeRequestDialog_->isShown()) {
        tradeRequestDialog_->render(driver_, gui_);
    }

    // Render money input dialog
    if (moneyInputDialog_ && moneyInputDialog_->isShown()) {
        moneyInputDialog_->render(driver_, gui_);
    }

    // Render bag windows
    for (auto& [slotId, bagWindow] : bagWindows_) {
        bagWindow->render(driver_, gui_);
    }

    // Render note window (on top of inventory/bags)
    if (noteWindow_ && noteWindow_->isVisible()) {
        noteWindow_->render(driver_, gui_);
    }

    // Render tooltip
    tooltip_.render(driver_, gui_, screenWidth_, screenHeight_);

    // Render buff tooltip (from buff window hover)
    buffTooltip_.render(driver_, gui_, screenWidth_, screenHeight_);

    // Render spell tooltip (from spell gem panel hover)
    renderSpellTooltip();

    // Render cursor item
    renderCursorItem();

    // Render hotbar cursor (if dragging a hotbar item)
    if (hotbarCursor_.hasItem()) {
        hotbarCursor_.render(driver_, gui_, &iconLoader_, mouseX_, mouseY_);
    }

    // Render cursor money
    renderCursorMoney();

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
    // Note: Don't clear pendingLootTargetSlot_ here - we want the auto-place to work
    // even after the loot cursor display is cleared
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
    // Store base currency values
    basePlatinum_ = platinum;
    baseGold_ = gold;
    baseSilver_ = silver;
    baseCopper_ = copper;

    if (inventoryWindow_) {
        inventoryWindow_->setHP(static_cast<int>(curHp), static_cast<int>(maxHp));
        inventoryWindow_->setMana(static_cast<int>(curMana), static_cast<int>(maxMana));
        inventoryWindow_->setStamina(static_cast<int>(curEnd), static_cast<int>(maxEnd));
        inventoryWindow_->setAC(ac);
        inventoryWindow_->setATK(atk);
        inventoryWindow_->setStats(str, sta, agi, dex, wis, intel, cha);
        inventoryWindow_->setResists(pr, mr, dr, fr, cr);
        inventoryWindow_->setWeight(weight, maxWeight);
        // Update currency display accounting for any money on cursor
        refreshCurrencyDisplay();
    }
}

void WindowManager::updateBaseCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    basePlatinum_ = platinum;
    baseGold_ = gold;
    baseSilver_ = silver;
    baseCopper_ = copper;

    // Update display accounting for any money on cursor
    refreshCurrencyDisplay();
}

void WindowManager::updateBankCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    bankPlatinum_ = platinum;
    bankGold_ = gold;
    bankSilver_ = silver;
    bankCopper_ = copper;

    // Update bank window currency display if open
    if (bankWindow_ && bankWindow_->isVisible()) {
        bankWindow_->setCurrency(bankPlatinum_, bankGold_, bankSilver_, bankCopper_);
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

    // Ctrl+click without shift and without inventory cursor = pickup for hotbar
    if (ctrl && !shift && !invManager_->hasCursorItem()) {
        const inventory::ItemInstance* item = invManager_->getItem(slotId);
        if (item) {
            LOG_DEBUG(MOD_UI, "WindowManager Ctrl+click: picking up item for hotbar, itemId={}, icon={}",
                     item->itemId, item->icon);
            setHotbarCursor(HotbarButtonType::Item, item->itemId, "", item->icon);
            return;
        }
    }

    // Left click (no modifiers, no cursor item) on tradeskill container = toggle open/close
    if (!shift && !ctrl && !invManager_->hasCursorItem()) {
        const inventory::ItemInstance* item = invManager_->getItem(slotId);
        if (item && item->isContainer() && inventory::isTradeskillContainerType(item->bagType)) {
            LOG_DEBUG(MOD_UI, "WindowManager Left-click on tradeskill container at slot {}", slotId);
            // Check if this specific container is already open
            if (isTradeskillContainerOpen() && tradeskillWindow_ &&
                !tradeskillWindow_->isWorldContainer() &&
                tradeskillWindow_->getContainerSlot() == slotId) {
                // Close it
                closeTradeskillContainer();
            } else {
                // Open it (this will close any existing one)
                openBagWindow(slotId);
            }
            return;
        }
    }

    if (invManager_->hasCursorItem()) {
        // Try to place item - check for auto-stacking
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = invManager_->getItem(slotId);

        // Check if we can stack onto an existing item of the same type
        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId &&
            targetItem->stackSize > 0 && targetItem->quantity < targetItem->stackSize) {
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

            // If picking up a container, close its window
            if (item && item->isContainer()) {
                if (inventory::isTradeskillContainerType(item->bagType)) {
                    // Close tradeskill container window if this container is open
                    if (isTradeskillContainerOpen() && tradeskillWindow_ &&
                        !tradeskillWindow_->isWorldContainer() &&
                        tradeskillWindow_->getContainerSlot() == slotId) {
                        closeTradeskillContainer();
                    }
                } else {
                    closeBagWindow(slotId);
                }
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

    // Ctrl+click without shift and without inventory cursor = pickup for hotbar
    if (ctrl && !shift && !invManager_->hasCursorItem()) {
        const inventory::ItemInstance* item = invManager_->getItem(slotId);
        if (item) {
            LOG_DEBUG(MOD_UI, "WindowManager Ctrl+click: picking up bag item for hotbar, itemId={}, icon={}",
                     item->itemId, item->icon);
            setHotbarCursor(HotbarButtonType::Item, item->itemId, "", item->icon);
            return;
        }
    }

    if (invManager_->hasCursorItem()) {
        // Try to place item - check for auto-stacking
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = invManager_->getItem(slotId);

        // Check if we can stack onto an existing item of the same type
        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId &&
            targetItem->stackSize > 0 && targetItem->quantity < targetItem->stackSize) {
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

void WindowManager::handleTradeSlotClick(int16_t tradeSlot, bool shift, bool ctrl) {
    LOG_DEBUG(MOD_UI, "WindowManager handleTradeSlotClick: tradeSlot={} shift={} ctrl={}", tradeSlot, shift, ctrl);
    if (tradeSlot == inventory::SLOT_INVALID) {
        return;
    }

    // Validate it's a valid trade slot (3000-3003)
    if (tradeSlot < inventory::TRADE_BEGIN || tradeSlot > inventory::TRADE_BEGIN + 3) {
        LOG_WARN(MOD_UI, "Invalid trade slot {}", tradeSlot);
        return;
    }

    // Convert to trade slot index (0-3)
    int tradeSlotIndex = tradeSlot - inventory::TRADE_BEGIN;

    if (invManager_->hasCursorItem()) {
        // Check if item can be traded (not NO_DROP)
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        if (cursorItem && cursorItem->noDrop) {
            LOG_DEBUG(MOD_UI, "Cannot trade NO_DROP item: {}", cursorItem->name);
            if (onTradeErrorCallback_) {
                onTradeErrorCallback_("You cannot trade that item.");
            }
            return;
        }

        // Place item from cursor into trade slot
        int16_t sourceSlot = invManager_->getCursorSourceSlot();
        LOG_DEBUG(MOD_UI, "WindowManager Placing cursor item (from slot {}) in trade slot {}", sourceSlot, tradeSlot);

        // Notify TradeManager about the item being added to trade
        if (tradeManager_) {
            tradeManager_->addItemToTrade(sourceSlot, tradeSlotIndex);
        }

        placeItem(tradeSlot);
    } else {
        // Pick up item from trade slot
        if (invManager_->hasItem(tradeSlot)) {
            LOG_DEBUG(MOD_UI, "WindowManager Picking up from trade slot {}", tradeSlot);

            // Notify TradeManager about the item being removed from trade
            if (tradeManager_) {
                tradeManager_->removeItemFromTrade(tradeSlotIndex);
            }

            pickupItem(tradeSlot);
        }
    }
}

void WindowManager::handleTradeMoneyAreaClick() {
    LOG_DEBUG(MOD_UI, "Trade money area clicked");

    if (!invManager_ || !tradeManager_) {
        return;
    }

    // Check if we have cursor money to add to trade
    if (!invManager_->hasCursorMoney()) {
        LOG_DEBUG(MOD_UI, "No cursor money to add to trade");
        return;
    }

    // Get current trade money amounts
    const TradeMoney& currentMoney = tradeManager_->getOwnMoney();
    uint32_t pp = currentMoney.platinum;
    uint32_t gp = currentMoney.gold;
    uint32_t sp = currentMoney.silver;
    uint32_t cp = currentMoney.copper;

    // Add cursor money to appropriate type
    uint32_t amount = invManager_->getCursorMoneyAmount();
    auto cursorType = invManager_->getCursorMoneyType();

    switch (cursorType) {
        case inventory::InventoryManager::CursorMoneyType::Platinum:
            pp += amount;
            LOG_DEBUG(MOD_UI, "Adding {} platinum to trade, new total: {}", amount, pp);
            break;
        case inventory::InventoryManager::CursorMoneyType::Gold:
            gp += amount;
            LOG_DEBUG(MOD_UI, "Adding {} gold to trade, new total: {}", amount, gp);
            break;
        case inventory::InventoryManager::CursorMoneyType::Silver:
            sp += amount;
            LOG_DEBUG(MOD_UI, "Adding {} silver to trade, new total: {}", amount, sp);
            break;
        case inventory::InventoryManager::CursorMoneyType::Copper:
            cp += amount;
            LOG_DEBUG(MOD_UI, "Adding {} copper to trade, new total: {}", amount, cp);
            break;
        default:
            LOG_WARN(MOD_UI, "Unknown cursor money type");
            return;
    }

    // Update trade money
    tradeManager_->setOwnMoney(pp, gp, sp, cp);

    // Clear cursor money
    invManager_->clearCursorMoney();
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

    // Update slot highlighting for cursor item validation feedback
    if (invManager_->hasCursorItem()) {
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        bool canPlace = invManager_->canPlaceItemInSlot(cursorItem, slotId);

        // Determine which window the slot belongs to and update its invalid drop highlighting
        if (inventory::isEquipmentSlot(slotId) || inventory::isGeneralSlot(slotId)) {
            // Inventory window slot
            if (inventoryWindow_) {
                if (!canPlace) {
                    inventoryWindow_->setInvalidDropSlot(slotId);
                } else {
                    inventoryWindow_->clearHighlights();
                }
            }
        } else if (inventory::isBankSlot(slotId) || inventory::isSharedBankSlot(slotId)) {
            // Bank window slot
            if (bankWindow_) {
                if (!canPlace) {
                    bankWindow_->setInvalidDropSlot(slotId);
                } else {
                    bankWindow_->clearHighlights();
                }
            }
        } else if (inventory::isBagSlot(slotId)) {
            // Inventory bag slot
            int16_t parentSlot = inventory::getParentGeneralSlot(slotId);
            auto it = bagWindows_.find(parentSlot);
            if (it != bagWindows_.end()) {
                if (!canPlace) {
                    it->second->setInvalidDropSlot(slotId);
                } else {
                    it->second->clearHighlights();
                }
            }
        } else if (inventory::isBankBagSlot(slotId) || inventory::isSharedBankBagSlot(slotId)) {
            // Bank bag slot
            int16_t parentSlot = inventory::getParentBankSlotAny(slotId);
            auto it = bankBagWindows_.find(parentSlot);
            if (it != bankBagWindows_.end()) {
                if (!canPlace) {
                    it->second->setInvalidDropSlot(slotId);
                } else {
                    it->second->clearHighlights();
                }
            }
        }
    }
}

void WindowManager::handleLootSlotHover(int16_t displayIndex, int mouseX, int mouseY) {
    if (displayIndex == inventory::SLOT_INVALID || !lootWindow_) {
        tooltip_.clear();
        return;
    }

    // Convert display index to actual corpse slot ID
    int16_t corpseSlot = lootWindow_->getCorpseSlotFromDisplayIndex(displayIndex);
    if (corpseSlot == inventory::SLOT_INVALID) {
        tooltip_.clear();
        return;
    }

    const inventory::ItemInstance* item = lootWindow_->getItem(corpseSlot);
    if (item) {
        tooltip_.setItem(item, mouseX, mouseY);
    } else {
        tooltip_.clear();
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

void WindowManager::handleCurrencyClick(CurrencyType type, uint32_t maxAmount) {
    // If holding cursor money, return it to inventory
    if (invManager_->hasCursorMoney()) {
        invManager_->returnCursorMoney();
        refreshCurrencyDisplay();  // Update display after returning money
        LOG_DEBUG(MOD_UI, "Returned cursor money to inventory");
        return;
    }

    // Can't pick up money if already holding an item
    if (invManager_->hasCursorItem()) {
        return;
    }

    if (maxAmount == 0) {
        return;
    }

    // Show money input dialog
    if (moneyInputDialog_) {
        // Center the dialog on screen
        int x = (screenWidth_ - 250) / 2;
        int y = (screenHeight_ - 130) / 2;
        moneyInputDialog_->setPosition(x, y);
        moneyInputDialog_->show(type, maxAmount);
    }
}

void WindowManager::refreshCurrencyDisplay() {
    if (!inventoryWindow_ || !invManager_) {
        return;
    }

    // Start with base currency values
    uint32_t displayPlatinum = basePlatinum_;
    uint32_t displayGold = baseGold_;
    uint32_t displaySilver = baseSilver_;
    uint32_t displayCopper = baseCopper_;

    // Subtract any money currently on cursor
    if (invManager_->hasCursorMoney()) {
        uint32_t cursorAmount = invManager_->getCursorMoneyAmount();
        switch (invManager_->getCursorMoneyType()) {
            case inventory::InventoryManager::CursorMoneyType::Platinum:
                displayPlatinum = (displayPlatinum >= cursorAmount) ? displayPlatinum - cursorAmount : 0;
                break;
            case inventory::InventoryManager::CursorMoneyType::Gold:
                displayGold = (displayGold >= cursorAmount) ? displayGold - cursorAmount : 0;
                break;
            case inventory::InventoryManager::CursorMoneyType::Silver:
                displaySilver = (displaySilver >= cursorAmount) ? displaySilver - cursorAmount : 0;
                break;
            case inventory::InventoryManager::CursorMoneyType::Copper:
                displayCopper = (displayCopper >= cursorAmount) ? displayCopper - cursorAmount : 0;
                break;
            default:
                break;
        }
    }

    inventoryWindow_->setCurrency(displayPlatinum, displayGold, displaySilver, displayCopper);
}

void WindowManager::handleMoneyInputConfirm(CurrencyType type, uint32_t amount) {
    if (amount == 0) {
        return;
    }

    // Convert CurrencyType to coin type constant (matches COINTYPE_* in packet_structs.h)
    int32_t coinType;
    switch (type) {
        case CurrencyType::Copper:
            coinType = 0;  // COINTYPE_CP
            break;
        case CurrencyType::Silver:
            coinType = 1;  // COINTYPE_SP
            break;
        case CurrencyType::Gold:
            coinType = 2;  // COINTYPE_GP
            break;
        case CurrencyType::Platinum:
            coinType = 3;  // COINTYPE_PP
            break;
        default:
            return;
    }

    // Check if this is a bank currency movement
    if (isBankWindowOpen() && currencyClickSource_ != CurrencyClickSource::None) {
        bool fromBank = (currencyClickSource_ == CurrencyClickSource::Bank);

        if (onBankCurrencyMoveCallback_) {
            onBankCurrencyMoveCallback_(coinType, static_cast<int32_t>(amount), fromBank);
            LOG_DEBUG(MOD_UI, "Bank currency move: {} of type {} (fromBank={})",
                      amount, coinType, fromBank);
        }

        // Clear the source tracker
        currencyClickSource_ = CurrencyClickSource::None;
        return;
    }

    // Original behavior: pick up money onto cursor (for trading)
    inventory::InventoryManager::CursorMoneyType cursorType;
    switch (type) {
        case CurrencyType::Platinum:
            cursorType = inventory::InventoryManager::CursorMoneyType::Platinum;
            break;
        case CurrencyType::Gold:
            cursorType = inventory::InventoryManager::CursorMoneyType::Gold;
            break;
        case CurrencyType::Silver:
            cursorType = inventory::InventoryManager::CursorMoneyType::Silver;
            break;
        case CurrencyType::Copper:
            cursorType = inventory::InventoryManager::CursorMoneyType::Copper;
            break;
        default:
            return;
    }

    // Pick up the money onto cursor
    invManager_->pickupMoney(cursorType, amount);
    refreshCurrencyDisplay();  // Update display after picking up money
    LOG_DEBUG(MOD_UI, "Money picked up: {} of type {}", amount, static_cast<int>(type));

    // Clear the source tracker
    currencyClickSource_ = CurrencyClickSource::None;
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

void WindowManager::renderCursorMoney() {
    if (!driver_ || !gui_ || !invManager_->hasCursorMoney()) {
        return;
    }

    const int CURSOR_SIZE = 32;
    const int OFFSET = 8;

    int x = mouseX_ + OFFSET;
    int y = mouseY_ + OFFSET;

    // Get currency color based on type
    irr::video::SColor coinColor;
    std::wstring coinLabel;
    switch (invManager_->getCursorMoneyType()) {
        case inventory::InventoryManager::CursorMoneyType::Platinum:
            coinColor = irr::video::SColor(255, 180, 180, 220);
            coinLabel = L"PP";
            break;
        case inventory::InventoryManager::CursorMoneyType::Gold:
            coinColor = irr::video::SColor(255, 255, 215, 0);
            coinLabel = L"GP";
            break;
        case inventory::InventoryManager::CursorMoneyType::Silver:
            coinColor = irr::video::SColor(255, 192, 192, 192);
            coinLabel = L"SP";
            break;
        case inventory::InventoryManager::CursorMoneyType::Copper:
            coinColor = irr::video::SColor(255, 184, 115, 51);
            coinLabel = L"CP";
            break;
        default:
            return;
    }

    // Draw coin background
    irr::core::recti coinRect(x, y, x + CURSOR_SIZE, y + CURSOR_SIZE);
    driver_->draw2DRectangle(coinColor, coinRect);

    // Draw border
    driver_->draw2DRectangleOutline(coinRect, irr::video::SColor(255, 255, 255, 255));

    // Draw amount text
    irr::gui::IGUIFont* font = gui_->getBuiltInFont();
    if (font) {
        std::wstringstream ss;
        ss << invManager_->getCursorMoneyAmount();
        irr::core::recti textRect(x, y + CURSOR_SIZE + 2, x + CURSOR_SIZE + 20, y + CURSOR_SIZE + 14);
        font->draw(ss.str().c_str(), textRect, irr::video::SColor(255, 255, 255, 255));

        // Draw coin type label
        irr::core::recti labelRect(x, y + 2, x + CURSOR_SIZE, y + 14);
        font->draw(coinLabel.c_str(), labelRect, irr::video::SColor(255, 0, 0, 0), true, true);
    }
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

    // Connect Ctrl+click pickup for hotbar
    spellGemPanel_->setHotbarPickupCallback([this](HotbarButtonType type, uint32_t id,
                                                    const std::string& emoteText, uint32_t iconId) {
        setHotbarCursor(type, id, emoteText, iconId);
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
// Hotbar Window Management
// ============================================================================

void WindowManager::initHotbarWindow() {
    hotbarWindow_ = std::make_unique<HotbarWindow>();
    hotbarWindow_->setIconLoader(&iconLoader_);
    hotbarWindow_->setHotbarCursor(&hotbarCursor_);
    hotbarWindow_->positionDefault(screenWidth_, screenHeight_);

    // Load layout configuration from UISettings (not button assignments)
    const auto& hotbarSettings = UISettings::instance().hotbar();
    hotbarWindow_->setButtonCount(hotbarSettings.buttonCount);

    // Note: Button assignments are loaded from main config via loadHotbarData()
    // This allows per-character hotbar configurations

    // Set up callbacks
    hotbarWindow_->setActivateCallback([this](int index, const HotbarButton& button) {
        if (hotbarActivateCallback_) {
            hotbarActivateCallback_(index, button);
        }
    });

    hotbarWindow_->setPickupCallback([this](int index, const HotbarButton& button) {
        // Pickup from hotbar to cursor
        setHotbarCursor(button.type, button.id, button.emoteText, button.iconId);
        hotbarWindow_->clearButton(index);
    });

    hotbarWindow_->setEmoteDialogCallback([this](int index) {
        // For now, open emote input state
        // A full implementation would show a modal text input dialog
        // For Phase 2, we set up the callback infrastructure
        emoteDialogSlot_ = index;
        emoteDialogActive_ = true;
        emoteDialogText_.clear();
        LOG_DEBUG(MOD_UI, "Emote dialog opened for hotbar slot {}", index);
    });

    // Wire up the changed callback if already set
    if (hotbarChangedCallback_) {
        hotbarWindow_->setChangedCallback(hotbarChangedCallback_);
    }

    hotbarWindow_->show();  // Hotbar visible by default

    LOG_DEBUG(MOD_UI, "Hotbar window initialized");
}

void WindowManager::toggleHotbar() {
    if (!hotbarWindow_) return;
    if (hotbarWindow_->isVisible()) {
        closeHotbar();
    } else {
        openHotbar();
    }
}

void WindowManager::openHotbar() {
    if (hotbarWindow_) {
        hotbarWindow_->show();
    }
}

void WindowManager::closeHotbar() {
    if (hotbarWindow_) {
        hotbarWindow_->hide();
    }
}

bool WindowManager::isHotbarOpen() const {
    return hotbarWindow_ && hotbarWindow_->isVisible();
}

void WindowManager::setHotbarActivateCallback(HotbarActivateCallback callback) {
    hotbarActivateCallback_ = std::move(callback);
}

void WindowManager::setHotbarChangedCallback(HotbarChangedCallback callback) {
    hotbarChangedCallback_ = std::move(callback);
    if (hotbarWindow_) {
        hotbarWindow_->setChangedCallback(hotbarChangedCallback_);
    }
}

Json::Value WindowManager::collectHotbarData() const {
    Json::Value hotbar;
    Json::Value buttons(Json::arrayValue);

    if (hotbarWindow_) {
        for (int i = 0; i < HotbarWindow::MAX_BUTTONS; i++) {
            const auto& btn = hotbarWindow_->getButton(i);
            Json::Value buttonData;
            buttonData["type"] = static_cast<int>(btn.type);
            buttonData["id"] = btn.id;
            buttonData["emoteText"] = btn.emoteText;
            buttonData["iconId"] = btn.iconId;
            buttons.append(buttonData);
        }
    }

    hotbar["buttons"] = buttons;
    return hotbar;
}

void WindowManager::loadHotbarData(const Json::Value& data) {
    if (!hotbarWindow_ || !data.isObject()) {
        return;
    }

    if (data.isMember("buttons") && data["buttons"].isArray()) {
        const auto& buttons = data["buttons"];
        for (Json::ArrayIndex i = 0; i < buttons.size() && i < HotbarWindow::MAX_BUTTONS; i++) {
            const auto& btnData = buttons[i];
            int type = btnData.get("type", 0).asInt();
            if (type != 0) {  // Skip empty buttons
                hotbarWindow_->setButton(
                    static_cast<int>(i),
                    static_cast<HotbarButtonType>(type),
                    btnData.get("id", 0).asUInt(),
                    btnData.get("emoteText", "").asString(),
                    btnData.get("iconId", 0).asUInt()
                );
            }
        }
    }

    LOG_DEBUG(MOD_UI, "Loaded hotbar data from config");
}

void WindowManager::startHotbarCooldown(int buttonIndex, uint32_t durationMs) {
    if (hotbarWindow_) {
        hotbarWindow_->startCooldown(buttonIndex, durationMs);
    }
}

void WindowManager::setHotbarCursor(HotbarButtonType type, uint32_t id,
                                     const std::string& emoteText, uint32_t iconId) {
    hotbarCursor_.setItem(type, id, emoteText, iconId);
}

void WindowManager::clearHotbarCursor() {
    hotbarCursor_.clear();
}

// ============================================================================
// Skills Window Management
// ============================================================================

void WindowManager::initSkillsWindow(EQ::SkillManager* skillMgr) {
    if (!skillMgr) {
        LOG_WARN(MOD_UI, "Cannot initialize skills window - SkillManager is null");
        return;
    }

    skillsWindow_ = std::make_unique<SkillsWindow>();
    skillsWindow_->setSkillManager(skillMgr);
    skillsWindow_->setSettingsKey("skills");

    // Load position from settings or use default
    const auto& settings = UISettings::instance().skills();
    if (settings.window.x >= 0 && settings.window.y >= 0) {
        skillsWindow_->setPosition(settings.window.x, settings.window.y);
    } else {
        skillsWindow_->positionDefault(screenWidth_, screenHeight_);
    }

    // Set up callbacks
    if (skillActivateCallback_) {
        skillsWindow_->setActivateCallback(skillActivateCallback_);
    }
    if (hotbarCreateCallback_) {
        skillsWindow_->setHotbarCallback(hotbarCreateCallback_);
    }

    LOG_DEBUG(MOD_UI, "Skills window initialized");
}

void WindowManager::toggleSkillsWindow() {
    if (!skillsWindow_) return;
    if (skillsWindow_->isVisible()) {
        closeSkillsWindow();
    } else {
        openSkillsWindow();
    }
}

void WindowManager::openSkillsWindow() {
    if (skillsWindow_) {
        skillsWindow_->refresh();
        skillsWindow_->show();
    }
}

void WindowManager::closeSkillsWindow() {
    if (skillsWindow_) {
        skillsWindow_->hide();
    }
}

bool WindowManager::isSkillsWindowOpen() const {
    return skillsWindow_ && skillsWindow_->isVisible();
}

void WindowManager::setSkillActivateCallback(SkillActivateCallback callback) {
    skillActivateCallback_ = callback;
    if (skillsWindow_) {
        skillsWindow_->setActivateCallback(callback);
    }
}

void WindowManager::setHotbarCreateCallback(HotbarCreateCallback callback) {
    hotbarCreateCallback_ = callback;
    if (skillsWindow_) {
        skillsWindow_->setHotbarCallback(callback);
    }
}

// ============================================================================
// Note Window Management
// ============================================================================

void WindowManager::showNoteWindow(const std::string& text, uint8_t type) {
    if (!noteWindow_) {
        noteWindow_ = std::make_unique<NoteWindow>();
        noteWindow_->setSettingsKey("note");
        noteWindow_->positionDefault(screenWidth_, screenHeight_);
    }

    noteWindow_->open(text, type);
    LOG_DEBUG(MOD_UI, "Note window opened: type={} textLen={}", type, text.length());
}

void WindowManager::closeNoteWindow() {
    if (noteWindow_) {
        noteWindow_->close();
    }
}

bool WindowManager::isNoteWindowOpen() const {
    return noteWindow_ && noteWindow_->isVisible();
}

void WindowManager::setOnReadItem(ReadItemCallback callback) {
    readItemCallback_ = callback;
}

// ============================================================================
// Tradeskill Container Window Management
// ============================================================================

void WindowManager::openTradeskillContainer(uint32_t dropId, const std::string& name,
                                             uint8_t objectType, int slotCount) {
    if (!tradeskillWindow_) {
        LOG_WARN(MOD_UI, "Tradeskill container window not initialized");
        return;
    }

    // Set up callbacks before opening
    tradeskillWindow_->setCombineCallback([this]() {
        if (tradeskillCombineCallback_) {
            tradeskillCombineCallback_();
        }
    });
    tradeskillWindow_->setCloseCallback([this]() {
        if (tradeskillCloseCallback_) {
            tradeskillCloseCallback_();
        }
    });

    tradeskillWindow_->openForWorldObject(dropId, name, objectType, slotCount);

    // Center on screen
    int windowWidth = tradeskillWindow_->getWidth();
    int windowHeight = tradeskillWindow_->getHeight();
    int x = (screenWidth_ - windowWidth) / 2;
    int y = (screenHeight_ - windowHeight) / 2;
    tradeskillWindow_->setPosition(x, y);

    LOG_DEBUG(MOD_UI, "Tradeskill container opened for world object: dropId={} name='{}' type={} slots={}",
              dropId, name, objectType, slotCount);
}

void WindowManager::openTradeskillContainerForItem(int16_t containerSlot, const std::string& name,
                                                    uint8_t bagType, int slotCount) {
    if (!tradeskillWindow_) {
        LOG_WARN(MOD_UI, "Tradeskill container window not initialized");
        return;
    }

    // Set up callbacks before opening
    tradeskillWindow_->setCombineCallback([this]() {
        if (tradeskillCombineCallback_) {
            tradeskillCombineCallback_();
        }
    });
    tradeskillWindow_->setCloseCallback([this]() {
        if (tradeskillCloseCallback_) {
            tradeskillCloseCallback_();
        }
    });

    tradeskillWindow_->openForInventoryItem(containerSlot, name, bagType, slotCount);

    // Center on screen
    int windowWidth = tradeskillWindow_->getWidth();
    int windowHeight = tradeskillWindow_->getHeight();
    int x = (screenWidth_ - windowWidth) / 2;
    int y = (screenHeight_ - windowHeight) / 2;
    tradeskillWindow_->setPosition(x, y);

    LOG_DEBUG(MOD_UI, "Tradeskill container opened for inventory item: slot={} name='{}' type={} slots={}",
              containerSlot, name, bagType, slotCount);
}

void WindowManager::closeTradeskillContainer() {
    if (tradeskillWindow_ && tradeskillWindow_->isOpen()) {
        // If closing a world container, clear the world container slots from inventory
        if (tradeskillWindow_->isWorldContainer() && invManager_) {
            invManager_->clearWorldContainerSlots();
        }
        tradeskillWindow_->close();
        LOG_DEBUG(MOD_UI, "Tradeskill container window closed");
    }
}

bool WindowManager::isTradeskillContainerOpen() const {
    return tradeskillWindow_ && tradeskillWindow_->isOpen();
}

void WindowManager::setOnTradeskillCombine(TradeskillCombineCallback callback) {
    tradeskillCombineCallback_ = callback;
}

void WindowManager::setOnTradeskillClose(TradeskillCloseCallback callback) {
    tradeskillCloseCallback_ = callback;
}

void WindowManager::handleTradeskillSlotClick(int16_t slotId, bool shift, bool ctrl) {
    LOG_DEBUG(MOD_UI, "WindowManager handleTradeskillSlotClick: slotId={} shift={} ctrl={}", slotId, shift, ctrl);

    if (slotId == inventory::SLOT_INVALID) {
        return;
    }

    // Check if this is a world container slot or inventory container slot
    bool isWorldSlot = inventory::isWorldContainerSlot(slotId);

    if (invManager_->hasCursorItem()) {
        // Place item in container
        const inventory::ItemInstance* cursorItem = invManager_->getCursorItem();
        const inventory::ItemInstance* targetItem = nullptr;

        if (isWorldSlot) {
            // For world containers, check the tradeskill window's cached items
            targetItem = tradeskillWindow_->getContainerItem(slotId);
        } else {
            // For inventory containers, check the inventory manager
            targetItem = invManager_->getItem(slotId);
        }

        // Check if we can stack onto an existing item of the same type
        if (cursorItem && targetItem && cursorItem->stackable && targetItem->stackable &&
            cursorItem->itemId == targetItem->itemId &&
            targetItem->stackSize > 0 && targetItem->quantity < targetItem->stackSize) {
            LOG_DEBUG(MOD_UI, "Auto-stacking cursor item onto tradeskill slot {}", slotId);
            invManager_->placeOnMatchingStack(slotId);
        } else {
            LOG_DEBUG(MOD_UI, "Placing cursor item at tradeskill slot {}", slotId);
            placeItem(slotId);
        }
    } else {
        // Try to pick up item from container
        const inventory::ItemInstance* item = nullptr;

        if (isWorldSlot) {
            item = tradeskillWindow_->getContainerItem(slotId);
        } else {
            item = invManager_->getItem(slotId);
        }

        if (item) {
            // Handle stackable items with modifiers
            if (item->stackable && item->quantity > 1) {
                if (ctrl) {
                    LOG_DEBUG(MOD_UI, "Ctrl+click: picking up 1 from tradeskill stack of {} at slot {}",
                              item->quantity, slotId);
                    invManager_->pickupPartialStack(slotId, 1);
                    tooltip_.clear();
                    return;
                } else if (shift) {
                    LOG_DEBUG(MOD_UI, "Shift+click: showing quantity slider for tradeskill stack of {} at slot {}",
                              item->quantity, slotId);
                    showQuantitySlider(slotId, item->quantity);
                    return;
                }
            }

            LOG_DEBUG(MOD_UI, "Picking up item from tradeskill slot {}", slotId);
            pickupItem(slotId);
        }
    }
}

void WindowManager::handleTradeskillSlotHover(int16_t slotId, int mouseX, int mouseY) {
    if (slotId == inventory::SLOT_INVALID) {
        tooltip_.clear();
        return;
    }

    const inventory::ItemInstance* item = nullptr;

    // Check if world container slot or inventory container slot
    if (inventory::isWorldContainerSlot(slotId)) {
        if (tradeskillWindow_) {
            item = tradeskillWindow_->getContainerItem(slotId);
        }
    } else {
        item = invManager_->getItem(slotId);
    }

    if (item) {
        tooltip_.setItem(item, mouseX, mouseY);
    } else {
        tooltip_.clear();
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
