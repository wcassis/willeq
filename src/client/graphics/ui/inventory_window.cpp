#include "client/graphics/ui/inventory_window.h"
#include "client/graphics/ui/money_input_dialog.h"  // For CurrencyType definition
#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/entity_renderer.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include "common/logging.h"

namespace eqt {
namespace ui {

InventoryWindow::InventoryWindow(inventory::InventoryManager* manager)
    : WindowBase(L"Inventory",
                 UISettings::instance().inventory().window.width > 0
                     ? UISettings::instance().inventory().window.width : 365,
                 UISettings::instance().inventory().window.height > 0
                     ? UISettings::instance().inventory().window.height : 340)
    , manager_(manager)
{
    // Initialize layout constants from UISettings
    const auto& invSettings = UISettings::instance().inventory();
    SLOT_SIZE = invSettings.slotSize;
    SLOT_SPACING = invSettings.slotSpacing;
    EQUIP_START_X = invSettings.equipmentStartX;
    EQUIP_START_Y = invSettings.equipmentStartY;
    GENERAL_START_X = invSettings.generalStartX;
    GENERAL_START_Y = invSettings.generalStartY;
    STATS_WIDTH = invSettings.statsWidth;

    initializeSlots();
}

void InventoryWindow::initializeSlots() {
    // Equipment slot layout - paper doll style in middle section
    // All positions relative to window content area

    const int midX = EQUIP_START_X;
    const int midY = EQUIP_START_Y;
    const int slotStep = SLOT_SIZE + SLOT_SPACING;

    // Top row: L.EAR, NECK, HEAD, FACE, R.EAR (5 slots)
    const int topRowY = midY;
    const int topRowStartX = midX;
    equipmentSlots_[inventory::SLOT_EAR1] = ItemSlot(inventory::SLOT_EAR1, SlotType::Equipment, topRowStartX, topRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_EAR1].setLabel(L"EAR");
    equipmentSlots_[inventory::SLOT_NECK] = ItemSlot(inventory::SLOT_NECK, SlotType::Equipment, topRowStartX + slotStep, topRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_NECK].setLabel(L"NECK");
    equipmentSlots_[inventory::SLOT_HEAD] = ItemSlot(inventory::SLOT_HEAD, SlotType::Equipment, topRowStartX + slotStep * 2, topRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_HEAD].setLabel(L"HEAD");
    equipmentSlots_[inventory::SLOT_FACE] = ItemSlot(inventory::SLOT_FACE, SlotType::Equipment, topRowStartX + slotStep * 3, topRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_FACE].setLabel(L"FACE");
    equipmentSlots_[inventory::SLOT_EAR2] = ItemSlot(inventory::SLOT_EAR2, SlotType::Equipment, topRowStartX + slotStep * 4, topRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_EAR2].setLabel(L"EAR");

    // Left and right columns
    const int colY = topRowY + slotStep + 4;  // Start below top row with small gap
    const int leftColX = midX;
    const int rightColX = midX + slotStep * 4;

    // Left column: CHEST, ARMS, L.WRIST, WAIST, L.RING (CHEST replaces CHARM position)
    equipmentSlots_[inventory::SLOT_CHEST] = ItemSlot(inventory::SLOT_CHEST, SlotType::Equipment, leftColX, colY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_CHEST].setLabel(L"CHEST");
    equipmentSlots_[inventory::SLOT_ARMS] = ItemSlot(inventory::SLOT_ARMS, SlotType::Equipment, leftColX, colY + slotStep, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_ARMS].setLabel(L"ARMS");
    equipmentSlots_[inventory::SLOT_WRIST1] = ItemSlot(inventory::SLOT_WRIST1, SlotType::Equipment, leftColX, colY + slotStep * 2, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_WRIST1].setLabel(L"WRST");
    equipmentSlots_[inventory::SLOT_WAIST] = ItemSlot(inventory::SLOT_WAIST, SlotType::Equipment, leftColX, colY + slotStep * 3, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_WAIST].setLabel(L"WAIST");
    equipmentSlots_[inventory::SLOT_RING1] = ItemSlot(inventory::SLOT_RING1, SlotType::Equipment, leftColX, colY + slotStep * 4, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_RING1].setLabel(L"RING");

    // Right column: BACK, SHOULDER, R.WRIST, HANDS, R.RING
    equipmentSlots_[inventory::SLOT_BACK] = ItemSlot(inventory::SLOT_BACK, SlotType::Equipment, rightColX, colY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_BACK].setLabel(L"BACK");
    equipmentSlots_[inventory::SLOT_SHOULDERS] = ItemSlot(inventory::SLOT_SHOULDERS, SlotType::Equipment, rightColX, colY + slotStep, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_SHOULDERS].setLabel(L"SHLD");
    equipmentSlots_[inventory::SLOT_WRIST2] = ItemSlot(inventory::SLOT_WRIST2, SlotType::Equipment, rightColX, colY + slotStep * 2, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_WRIST2].setLabel(L"WRST");
    equipmentSlots_[inventory::SLOT_HANDS] = ItemSlot(inventory::SLOT_HANDS, SlotType::Equipment, rightColX, colY + slotStep * 3, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_HANDS].setLabel(L"HAND");
    equipmentSlots_[inventory::SLOT_RING2] = ItemSlot(inventory::SLOT_RING2, SlotType::Equipment, rightColX, colY + slotStep * 4, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_RING2].setLabel(L"RING");

    // Bottom section: LEGS, FEET, and CHARM in horizontal row
    const int bottomY = colY + slotStep * 5;
    const int centerX = midX + slotStep;  // Start position for bottom row
    equipmentSlots_[inventory::SLOT_LEGS] = ItemSlot(inventory::SLOT_LEGS, SlotType::Equipment, centerX, bottomY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_LEGS].setLabel(L"LEGS");
    equipmentSlots_[inventory::SLOT_FEET] = ItemSlot(inventory::SLOT_FEET, SlotType::Equipment, centerX + slotStep, bottomY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_FEET].setLabel(L"FEET");
    equipmentSlots_[inventory::SLOT_CHARM] = ItemSlot(inventory::SLOT_CHARM, SlotType::Equipment, centerX + slotStep * 2, bottomY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_CHARM].setLabel(L"CHRM");

    // Weapon/ammo row: PRI, SEC, RANGE, AMMO
    const int weaponRowY = bottomY + slotStep;
    const int weaponStartX = midX + slotStep / 2;
    equipmentSlots_[inventory::SLOT_PRIMARY] = ItemSlot(inventory::SLOT_PRIMARY, SlotType::Equipment, weaponStartX, weaponRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_PRIMARY].setLabel(L"PRI");
    equipmentSlots_[inventory::SLOT_SECONDARY] = ItemSlot(inventory::SLOT_SECONDARY, SlotType::Equipment, weaponStartX + slotStep, weaponRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_SECONDARY].setLabel(L"SEC");
    equipmentSlots_[inventory::SLOT_RANGE] = ItemSlot(inventory::SLOT_RANGE, SlotType::Equipment, weaponStartX + slotStep * 2, weaponRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_RANGE].setLabel(L"RNGE");
    equipmentSlots_[inventory::SLOT_AMMO] = ItemSlot(inventory::SLOT_AMMO, SlotType::Equipment, weaponStartX + slotStep * 3, weaponRowY, SLOT_SIZE);
    equipmentSlots_[inventory::SLOT_AMMO].setLabel(L"AMMO");

    // General inventory slots (8 slots in 2x4 grid on right side)
    // Column-major layout: slots 22-25 in left column (top to bottom), 26-29 in right column
    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        int col = i / 4;  // 0 for slots 22-25, 1 for slots 26-29
        int row = i % 4;  // 0-3 within each column
        int x = GENERAL_START_X + col * slotStep;
        int y = GENERAL_START_Y + row * slotStep;
        int16_t slotId = inventory::GENERAL_BEGIN + i;
        generalSlots_[i] = ItemSlot(slotId, SlotType::General, x, y, SLOT_SIZE);
    }

    // Button positions at very bottom, aligned to right side
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();
    int buttonHeight = getButtonHeight();
    int windowWidth = getWidth();
    int windowHeight = getHeight();

    int buttonY = windowHeight - borderWidth - contentPadding - buttonHeight - 5;
    int buttonWidth = 60;
    int buttonSpacing = 5;

    // Done button on far right
    doneButtonBounds_ = irr::core::recti(
        windowWidth - borderWidth - contentPadding - buttonWidth, buttonY,
        windowWidth - borderWidth - contentPadding, buttonY + buttonHeight
    );

    // Destroy button to the left of Done
    destroyButtonBounds_ = irr::core::recti(
        doneButtonBounds_.UpperLeftCorner.X - buttonSpacing - buttonWidth, buttonY,
        doneButtonBounds_.UpperLeftCorner.X - buttonSpacing, buttonY + buttonHeight
    );

    // Model view bounds: the area between left and right equipment columns
    // Left edge: after left column (leftColX + slotStep)
    // Right edge: before right column (rightColX)
    // Top: same as column start (colY)
    // Bottom: just before bottom row (bottomY)
    modelViewBounds_ = irr::core::recti(
        leftColX + slotStep,           // Left edge (after CHEST column)
        colY,                          // Top edge
        rightColX,                     // Right edge (before BACK column)
        bottomY                        // Bottom edge (above LEGS row)
    );
}

bool InventoryWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
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

    // Check model view area for drag rotation
    if (modelView_ && modelViewBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        int modelX = relX - modelViewBounds_.UpperLeftCorner.X;
        int modelY = relY - modelViewBounds_.UpperLeftCorner.Y;
        modelView_->onMouseDown(modelX, modelY);
        return true;
    }

    // Check buttons
    if (destroyButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        if (destroyClickCallback_) {
            destroyClickCallback_();
        }
        return true;
    }

    if (doneButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        hide();
        if (closeCallback_) {
            closeCallback_();
        }
        return true;
    }

    // Check equipment slots
    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        if (equipmentSlots_[i].containsPoint(relX, relY)) {
            int16_t slotId = equipmentSlots_[i].getSlotId();
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

    // Check general slots
    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        if (generalSlots_[i].containsPoint(relX, relY)) {
            int16_t slotId = generalSlots_[i].getSlotId();
            LOG_DEBUG(MOD_UI, "InventoryWindow General slot {} clicked, slotId={}", i, slotId);

            const inventory::ItemInstance* item = manager_->getItem(slotId);

            // Ctrl+click on readable item = read it
            if (ctrl && item && !item->bookText.empty() && readItemCallback_) {
                readItemCallback_(item->bookText, static_cast<uint8_t>(item->bookType));
                return true;
            }

            // Check if this is a bag and handle accordingly
            if (item && item->isContainer() && !shift && !ctrl) {
                // Single click on bag = open/close bag window
                LOG_DEBUG(MOD_UI, "InventoryWindow Opening bag at slot {}", slotId);
                if (bagClickCallback_) {
                    bagClickCallback_(slotId);
                }
            } else {
                // Shift+click, Ctrl+click, or non-bag = normal slot click
                LOG_DEBUG(MOD_UI, "InventoryWindow Slot click for slot {}", slotId);
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
        LOG_DEBUG(MOD_UI, "InventoryWindow Currency clicked: type={} maxAmount={}",
                  static_cast<int>(currencyType), maxAmount);
        // Always call callback - even with maxAmount=0 to allow placing back cursor money
        if (currencyClickCallback_) {
            currencyClickCallback_(currencyType, maxAmount);
        }
        return true;
    }

    return true;  // Consume click even if not on a slot
}

bool InventoryWindow::handleMouseUp(int x, int y, bool leftButton) {
    // Release model view drag if active
    if (modelView_ && modelView_->isDragging()) {
        modelView_->onMouseUp();
    }
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool InventoryWindow::handleMouseMove(int x, int y) {
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    // Handle model view drag even if outside bounds (drag continues until mouse up)
    if (modelView_ && modelView_->isDragging()) {
        int relX = x - bounds_.UpperLeftCorner.X;
        int relY = y - bounds_.UpperLeftCorner.Y;
        int modelX = relX - modelViewBounds_.UpperLeftCorner.X;
        int modelY = relY - modelViewBounds_.UpperLeftCorner.Y;
        modelView_->onMouseMove(modelX, modelY);
        return true;
    }

    if (!visible_ || !containsPoint(x, y)) {
        clearHighlights();
        destroyButtonHighlighted_ = false;
        doneButtonHighlighted_ = false;
        if (slotHoverCallback_) {
            slotHoverCallback_(inventory::SLOT_INVALID, x, y);
        }
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check buttons
    destroyButtonHighlighted_ = destroyButtonBounds_.isPointInside(irr::core::vector2di(relX, relY));
    doneButtonHighlighted_ = doneButtonBounds_.isPointInside(irr::core::vector2di(relX, relY));

    // Find slot under cursor
    int16_t hoveredSlot = inventory::SLOT_INVALID;

    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        if (equipmentSlots_[i].containsPoint(relX, relY)) {
            hoveredSlot = equipmentSlots_[i].getSlotId();
            break;
        }
    }

    if (hoveredSlot == inventory::SLOT_INVALID) {
        for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
            if (generalSlots_[i].containsPoint(relX, relY)) {
                hoveredSlot = generalSlots_[i].getSlotId();
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

int16_t InventoryWindow::getSlotAtPosition(int x, int y) const {
    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        if (equipmentSlots_[i].containsPoint(relX, relY)) {
            return equipmentSlots_[i].getSlotId();
        }
    }

    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        if (generalSlots_[i].containsPoint(relX, relY)) {
            return generalSlots_[i].getSlotId();
        }
    }

    return inventory::SLOT_INVALID;
}

void InventoryWindow::setHighlightedSlot(int16_t slotId) {
    highlightedSlot_ = slotId;

    // Update slot highlighting
    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        equipmentSlots_[i].setHighlighted(equipmentSlots_[i].getSlotId() == slotId);
    }
    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        generalSlots_[i].setHighlighted(generalSlots_[i].getSlotId() == slotId);
    }
}

void InventoryWindow::setInvalidDropSlot(int16_t slotId) {
    invalidDropSlot_ = slotId;

    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        equipmentSlots_[i].setInvalidDrop(equipmentSlots_[i].getSlotId() == slotId);
    }
    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        generalSlots_[i].setInvalidDrop(generalSlots_[i].getSlotId() == slotId);
    }
}

void InventoryWindow::clearHighlights() {
    highlightedSlot_ = inventory::SLOT_INVALID;
    invalidDropSlot_ = inventory::SLOT_INVALID;

    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        equipmentSlots_[i].setHighlighted(false);
        equipmentSlots_[i].setInvalidDrop(false);
    }
    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        generalSlots_[i].setHighlighted(false);
        generalSlots_[i].setInvalidDrop(false);
    }
}

void InventoryWindow::renderContent(irr::video::IVideoDriver* driver,
                                    irr::gui::IGUIEnvironment* gui) {
    renderCharacterInfo(driver, gui);
    renderModelView(driver, gui);  // Render before equipment slots so slots overlay
    renderEquipmentSlots(driver, gui);
    renderGeneralSlots(driver, gui);
    renderCurrency(driver, gui);
    renderButtons(driver, gui);
}

void InventoryWindow::renderCharacterInfo(irr::video::IVideoDriver* driver,
                                          irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int statsWidth = getStatsWidth();
    static constexpr int STATS_START_X = 5;  // Fixed offset for stats panel

    int x = bounds_.UpperLeftCorner.X + borderWidth + STATS_START_X;
    int y = bounds_.UpperLeftCorner.Y + borderWidth + titleBarHeight + 2;
    int lineHeight = 10;
    int valueX = x + statsWidth - 2;  // Right edge for right-aligned values

    irr::video::SColor textColor(255, 200, 200, 200);
    irr::video::SColor labelColor(255, 150, 150, 150);
    irr::video::SColor valueColor(255, 255, 255, 255);

    // Helper lambda to draw a stat line with left-aligned label and right-aligned value
    auto drawStatLine = [&](const wchar_t* label, const std::wstring& value,
                           irr::video::SColor labelCol, irr::video::SColor valueCol) {
        // Draw label left-aligned
        font->draw(label, irr::core::recti(x, y, x + statsWidth, y + lineHeight), labelCol);
        // Draw value right-aligned (calculate text width and offset)
        irr::core::dimension2du valueDim = font->getDimension(value.c_str());
        int valueStartX = valueX - valueDim.Width;
        font->draw(value.c_str(), irr::core::recti(valueStartX, y, valueX, y + lineHeight), valueCol);
        y += lineHeight;
    };

    // === Top section: Name, Level, Deity ===

    // Name
    if (!characterName_.empty()) {
        font->draw(characterName_.c_str(),
                  irr::core::recti(x, y, x + statsWidth, y + lineHeight),
                  valueColor);
        y += lineHeight;
    }

    // Level
    if (characterLevel_ > 0) {
        std::wstring levelStr = L"Lv " + std::to_wstring(characterLevel_);
        font->draw(levelStr.c_str(),
                  irr::core::recti(x, y, x + statsWidth, y + lineHeight),
                  textColor);
        y += lineHeight;
    }

    // Deity
    if (!characterDeity_.empty()) {
        font->draw(characterDeity_.c_str(),
                  irr::core::recti(x, y, x + statsWidth, y + lineHeight),
                  textColor);
        y += lineHeight;
    }

    y += 4;  // Gap before resources section

    // === Resources: HP, Mana, Stamina ===
    {
        std::wstringstream ss;
        ss << currentHP_ << L"/" << maxHP_;
        drawStatLine(L"HP", ss.str(), labelColor, valueColor);
    }
    {
        std::wstringstream ss;
        ss << currentMana_ << L"/" << maxMana_;
        drawStatLine(L"Mana", ss.str(), labelColor, valueColor);
    }
    {
        std::wstringstream ss;
        ss << currentStamina_ << L"/" << maxStamina_;
        drawStatLine(L"End", ss.str(), labelColor, valueColor);
    }

    y += 2;  // Gap before base stats

    // === Base stats: STR, STA, AGI, DEX, WIS, INT, CHA ===
    const wchar_t* statNames[] = {L"STR", L"STA", L"AGI", L"DEX", L"WIS", L"INT", L"CHA"};
    for (int i = 0; i < 7; i++) {
        drawStatLine(statNames[i], std::to_wstring(stats_[i]), labelColor, textColor);
    }

    y += 2;  // Gap before resistances

    // === Resistances ===
    const wchar_t* resistNames[] = {L"Poison", L"Magic", L"Disease", L"Fire", L"Cold"};
    irr::video::SColor resistColors[] = {
        irr::video::SColor(255, 100, 200, 100),  // Poison - green
        irr::video::SColor(255, 150, 150, 255),  // Magic - blue
        irr::video::SColor(255, 200, 200, 100),  // Disease - yellow
        irr::video::SColor(255, 255, 100, 100),  // Fire - red
        irr::video::SColor(255, 100, 200, 255),  // Cold - cyan
    };
    for (int i = 0; i < 5; i++) {
        drawStatLine(resistNames[i], std::to_wstring(resists_[i]), resistColors[i], resistColors[i]);
    }

    y += 2;  // Gap before bonuses

    // === Additional bonuses (only show non-zero values) ===
    if (haste_ != 0) {
        std::wstring hasteStr = std::to_wstring(haste_) + L"%";
        drawStatLine(L"Haste", hasteStr, labelColor, textColor);
    }
    if (spellDmg_ != 0) {
        drawStatLine(L"Spell Dmg", std::to_wstring(spellDmg_), labelColor, textColor);
    }
    if (healAmt_ != 0) {
        drawStatLine(L"Heal Amt", std::to_wstring(healAmt_), labelColor, textColor);
    }
    if (regenHP_ != 0) {
        drawStatLine(L"HP Regen", std::to_wstring(regenHP_), labelColor, textColor);
    }
    if (regenMana_ != 0) {
        drawStatLine(L"Mana Rgn", std::to_wstring(regenMana_), labelColor, textColor);
    }

    y += 2;  // Gap before weight

    // === Weight ===
    {
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(0) << weight_ << L"/" << static_cast<int>(maxWeight_);
        drawStatLine(L"Weight", ss.str(), labelColor, textColor);
    }
}

void InventoryWindow::renderEquipmentSlots(irr::video::IVideoDriver* driver,
                                           irr::gui::IGUIEnvironment* gui) {
    // Offset by window position
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    for (int i = 0; i < inventory::EQUIPMENT_COUNT; i++) {
        ItemSlot& slot = equipmentSlots_[i];
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

void InventoryWindow::renderGeneralSlots(irr::video::IVideoDriver* driver,
                                         irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    for (int i = 0; i < inventory::GENERAL_COUNT; i++) {
        ItemSlot& slot = generalSlots_[i];
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

void InventoryWindow::renderButtons(irr::video::IVideoDriver* driver,
                                    irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Destroy button
    irr::core::recti destroyBounds(
        destroyButtonBounds_.UpperLeftCorner.X + offsetX,
        destroyButtonBounds_.UpperLeftCorner.Y + offsetY,
        destroyButtonBounds_.LowerRightCorner.X + offsetX,
        destroyButtonBounds_.LowerRightCorner.Y + offsetY
    );
    drawButton(driver, gui, destroyBounds, L"Destroy", destroyButtonHighlighted_);

    // Done button
    irr::core::recti doneBounds(
        doneButtonBounds_.UpperLeftCorner.X + offsetX,
        doneButtonBounds_.UpperLeftCorner.Y + offsetY,
        doneButtonBounds_.LowerRightCorner.X + offsetX,
        doneButtonBounds_.LowerRightCorner.Y + offsetY
    );
    drawButton(driver, gui, doneBounds, L"Done", doneButtonHighlighted_);
}

void InventoryWindow::renderCurrency(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Position below the general inventory slots (2x4 grid)
    const int slotStep = SLOT_SIZE + SLOT_SPACING;
    int relX = GENERAL_START_X;  // Relative to window
    int relY = GENERAL_START_Y + slotStep * 4 + 8;  // Below the 4 rows of slots
    int lineHeight = 10;
    int currencyWidth = 70;

    // Store bounds relative to window for click detection
    platinumBounds_ = irr::core::recti(relX, relY, relX + currencyWidth, relY + lineHeight);
    goldBounds_ = irr::core::recti(relX, relY + lineHeight, relX + currencyWidth, relY + lineHeight * 2);
    silverBounds_ = irr::core::recti(relX, relY + lineHeight * 2, relX + currencyWidth, relY + lineHeight * 3);
    copperBounds_ = irr::core::recti(relX, relY + lineHeight * 3, relX + currencyWidth, relY + lineHeight * 4);

    // Currency colors
    irr::video::SColor platColor(255, 180, 180, 220);  // Light blue/silver
    irr::video::SColor goldColor(255, 255, 215, 0);    // Gold
    irr::video::SColor silverColor(255, 192, 192, 192); // Silver
    irr::video::SColor copperColor(255, 184, 115, 51);  // Copper

    int x = relX + offsetX;
    int y = relY + offsetY;

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

CurrencyType InventoryWindow::getCurrencyAtPosition(int relX, int relY, bool& found) const {
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

void InventoryWindow::initModelView(irr::scene::ISceneManager* smgr,
                                     irr::video::IVideoDriver* driver,
                                     EQT::Graphics::RaceModelLoader* raceLoader,
                                     EQT::Graphics::EquipmentModelLoader* equipLoader) {
    if (!smgr || !driver) {
        LOG_ERROR(MOD_UI, "InventoryWindow::initModelView: null scene manager or driver");
        return;
    }

    // Calculate model view dimensions from bounds
    int width = modelViewBounds_.getWidth();
    int height = modelViewBounds_.getHeight();

    if (width <= 0 || height <= 0) {
        LOG_ERROR(MOD_UI, "InventoryWindow::initModelView: invalid bounds {}x{}", width, height);
        return;
    }

    // Create the model view
    modelView_ = std::make_unique<CharacterModelView>();
    modelView_->init(smgr, driver, width, height);

    if (raceLoader) {
        modelView_->setRaceModelLoader(raceLoader);
    }
    if (equipLoader) {
        modelView_->setEquipmentModelLoader(equipLoader);
    }
    // Pass inventory manager so model can get equipment materials
    if (manager_) {
        modelView_->setInventoryManager(manager_);
    }

    LOG_INFO(MOD_UI, "InventoryWindow: Model view initialized {}x{}", width, height);
}

void InventoryWindow::setPlayerAppearance(uint16_t raceId, uint8_t gender,
                                           const EQT::Graphics::EntityAppearance& appearance) {
    if (modelView_) {
        modelView_->setCharacter(raceId, gender, appearance);
    }
}

void InventoryWindow::updateModelView(float deltaTimeMs) {
    if (modelView_) {
        modelView_->update(deltaTimeMs);
    }
}

void InventoryWindow::renderModelView(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui) {
    if (!modelView_ || !modelView_->isReady()) {
        return;
    }

    // Render model to its internal texture
    modelView_->renderToTexture();

    // Get the rendered texture
    irr::video::ITexture* tex = modelView_->getTexture();
    if (!tex) {
        return;
    }

    // Calculate destination rectangle in screen coordinates
    irr::core::recti destRect(
        bounds_.UpperLeftCorner.X + modelViewBounds_.UpperLeftCorner.X,
        bounds_.UpperLeftCorner.Y + modelViewBounds_.UpperLeftCorner.Y,
        bounds_.UpperLeftCorner.X + modelViewBounds_.LowerRightCorner.X,
        bounds_.UpperLeftCorner.Y + modelViewBounds_.LowerRightCorner.Y
    );

    // Source rectangle (entire texture)
    irr::core::recti srcRect(0, 0, tex->getSize().Width, tex->getSize().Height);

    // Draw the texture with alpha blending
    driver->draw2DImage(tex, destRect, srcRect, nullptr, nullptr, true);
}

} // namespace ui
} // namespace eqt
