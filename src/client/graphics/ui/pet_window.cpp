#include "client/graphics/ui/pet_window.h"
#include "client/graphics/ui/item_icon_loader.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "common/name_utils.h"

namespace eqt {
namespace ui {

PetWindow::PetWindow()
    : WindowBase(L"Pet", 100, 100)  // Size calculated below
{
    // Initialize layout constants (similar to group window)
    const auto& groupSettings = UISettings::instance().group();
    NAME_HEIGHT = groupSettings.nameHeight;
    LEVEL_WIDTH = 40;
    BAR_HEIGHT = groupSettings.barHeight;
    BAR_SPACING = groupSettings.barSpacing;
    BUTTON_WIDTH = 55;
    BUTTON_HEIGHT = 20;
    BUTTON_SPACING = 4;
    WINDOW_PADDING = groupSettings.padding;
    BUTTONS_PER_ROW = 3;
    BUTTON_ROWS = 3;

    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();

    // Calculate content width: 3 buttons + spacing
    int contentWidth = BUTTONS_PER_ROW * BUTTON_WIDTH + (BUTTONS_PER_ROW - 1) * BUTTON_SPACING;

    // Calculate content height: buff row + name + HP bar + mana bar + button rows
    int contentHeight = WINDOW_PADDING +
                        BUFF_ROW_HEIGHT + BAR_SPACING +  // Buff row at top
                        NAME_HEIGHT + BAR_SPACING +
                        BAR_HEIGHT + BAR_SPACING +
                        BAR_HEIGHT + BAR_SPACING +
                        BUTTON_ROWS * (BUTTON_HEIGHT + BUTTON_SPACING) - BUTTON_SPACING +
                        WINDOW_PADDING;

    int width = contentWidth + (borderWidth + contentPadding) * 2;
    int height = contentHeight + (borderWidth + contentPadding) * 2;
    setSize(width, height);

    setShowTitleBar(false);
    setSettingsKey("pet");
    initializeLayout();
}

void PetWindow::initializeLayout()
{
    // Content width: 3 buttons + spacing
    int contentWidth = BUTTONS_PER_ROW * BUTTON_WIDTH + (BUTTONS_PER_ROW - 1) * BUTTON_SPACING;

    // Buff row at top
    int yPos = WINDOW_PADDING;
    buffRowBounds_ = irr::core::recti(0, yPos, contentWidth, yPos + BUFF_ROW_HEIGHT);

    // Initialize buff slot positions within the buff row
    buffSlots_.clear();
    buffSlots_.reserve(MAX_VISIBLE_BUFFS);
    for (int i = 0; i < MAX_VISIBLE_BUFFS; i++) {
        BuffSlotLayout slot;
        int x = i * (BUFF_SIZE + BUFF_SPACING);
        // Center vertically in buff row
        int y = (BUFF_ROW_HEIGHT - BUFF_SIZE) / 2;
        slot.bounds = irr::core::recti(x, y, x + BUFF_SIZE, y + BUFF_SIZE);
        buffSlots_.push_back(slot);
    }
    yPos += BUFF_ROW_HEIGHT + BAR_SPACING;

    // Name and level below buff row
    nameBounds_ = irr::core::recti(0, yPos, contentWidth - LEVEL_WIDTH - 4, yPos + NAME_HEIGHT);
    levelBounds_ = irr::core::recti(contentWidth - LEVEL_WIDTH, yPos, contentWidth, yPos + NAME_HEIGHT);
    yPos += NAME_HEIGHT + BAR_SPACING;

    // HP bar
    hpBarBounds_ = irr::core::recti(0, yPos, contentWidth, yPos + BAR_HEIGHT);
    yPos += BAR_HEIGHT + BAR_SPACING;

    // Mana bar
    manaBarBounds_ = irr::core::recti(0, yPos, contentWidth, yPos + BAR_HEIGHT);
    yPos += BAR_HEIGHT + BAR_SPACING;

    // Initialize buttons
    // Row 1: Attack, Back Off, Follow
    // Row 2: Guard, Sit, Taunt
    // Row 3: Hold, Focus, Get Lost

    struct ButtonDef {
        std::wstring label;
        EQT::PetCommand command;
        EQT::PetButton buttonId;
        bool isToggle;
    };

    ButtonDef buttonDefs[9] = {
        { L"Attack",  EQT::PET_ATTACK,    EQT::PET_BUTTON_COUNT,  false },
        { L"Back",    EQT::PET_BACKOFF,   EQT::PET_BUTTON_COUNT,  false },
        { L"Follow",  EQT::PET_FOLLOWME,  EQT::PET_BUTTON_FOLLOW, true  },
        { L"Guard",   EQT::PET_GUARDHERE, EQT::PET_BUTTON_GUARD,  true  },
        { L"Sit",     EQT::PET_SIT,       EQT::PET_BUTTON_SIT,    true  },
        { L"Taunt",   EQT::PET_TAUNT,     EQT::PET_BUTTON_TAUNT,  true  },
        { L"Hold",    EQT::PET_HOLD,      EQT::PET_BUTTON_HOLD,   true  },
        { L"Focus",   EQT::PET_FOCUS,     EQT::PET_BUTTON_FOCUS,  true  },
        { L"Dismiss", EQT::PET_GETLOST,   EQT::PET_BUTTON_COUNT,  false },
    };

    for (int i = 0; i < 9; i++) {
        int row = i / BUTTONS_PER_ROW;
        int col = i % BUTTONS_PER_ROW;

        int buttonX = col * (BUTTON_WIDTH + BUTTON_SPACING);
        int buttonY = yPos + row * (BUTTON_HEIGHT + BUTTON_SPACING);

        buttons_[i].bounds = irr::core::recti(
            buttonX, buttonY,
            buttonX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT
        );
        buttons_[i].label = buttonDefs[i].label;
        buttons_[i].command = buttonDefs[i].command;
        buttons_[i].buttonId = buttonDefs[i].buttonId;
        buttons_[i].isToggle = buttonDefs[i].isToggle;
        buttons_[i].hovered = false;
    }
}

void PetWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Position below group window area on right side
    int x = screenWidth - getWidth() - 10;
    int y = 350;  // Below group window
    setPosition(x, y);
}

void PetWindow::update()
{
    if (!eq_) {
        hasPet_ = false;
        return;
    }

    hasPet_ = eq_->HasPet();
    if (!hasPet_) {
        petName_.clear();
        petLevel_ = 0;
        hpPercent_ = 0;
        manaPercent_ = 0;
        return;
    }

    // Get pet data from EverQuest
    std::string name = eq_->GetPetName();
    petName_ = EQT::toDisplayNameW(name);
    petLevel_ = eq_->GetPetLevel();
    hpPercent_ = eq_->GetPetHpPercent();

    // Pet mana not currently tracked - show 0 for now
    // Could be added if server sends pet mana updates
    manaPercent_ = 0;
}

void PetWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) {
        return;
    }

    // Draw window base (title bar, background, border)
    WindowBase::render(driver, gui);
}

void PetWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!driver) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // If no pet, show "No Pet" message
    if (!hasPet_) {
        if (gui) {
            irr::gui::IGUIFont* font = gui->getBuiltInFont();
            if (font) {
                irr::core::recti msgBounds(
                    contentX + WINDOW_PADDING,
                    contentY + WINDOW_PADDING + BUFF_ROW_HEIGHT + 10,
                    contentX + getWidth() - WINDOW_PADDING,
                    contentY + WINDOW_PADDING + BUFF_ROW_HEIGHT + 30
                );
                font->draw(L"No Pet", msgBounds, irr::video::SColor(255, 150, 150, 150));
            }
        }
        return;
    }

    // Draw buff row at top
    drawBuffRow(driver, gui);

    // Draw pet name
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            irr::core::recti nameBoundsAbs(
                contentX + nameBounds_.UpperLeftCorner.X,
                contentY + nameBounds_.UpperLeftCorner.Y,
                contentX + nameBounds_.LowerRightCorner.X,
                contentY + nameBounds_.LowerRightCorner.Y
            );
            font->draw(petName_.c_str(), nameBoundsAbs, irr::video::SColor(255, 255, 255, 200));

            // Draw level
            irr::core::recti levelBoundsAbs(
                contentX + levelBounds_.UpperLeftCorner.X,
                contentY + levelBounds_.UpperLeftCorner.Y,
                contentX + levelBounds_.LowerRightCorner.X,
                contentY + levelBounds_.LowerRightCorner.Y
            );
            std::wstring levelStr = L"Lv " + std::to_wstring(petLevel_);
            font->draw(levelStr.c_str(), levelBoundsAbs, irr::video::SColor(255, 200, 200, 200));
        }
    }

    // Draw HP bar
    irr::core::recti hpBoundsAbs(
        contentX + hpBarBounds_.UpperLeftCorner.X,
        contentY + hpBarBounds_.UpperLeftCorner.Y,
        contentX + hpBarBounds_.LowerRightCorner.X,
        contentY + hpBarBounds_.LowerRightCorner.Y
    );
    drawHealthBar(driver, hpBoundsAbs, hpPercent_);

    // Draw Mana bar
    irr::core::recti manaBoundsAbs(
        contentX + manaBarBounds_.UpperLeftCorner.X,
        contentY + manaBarBounds_.UpperLeftCorner.Y,
        contentX + manaBarBounds_.LowerRightCorner.X,
        contentY + manaBarBounds_.LowerRightCorner.Y
    );
    drawManaBar(driver, manaBoundsAbs, manaPercent_);

    // Draw command buttons
    for (const auto& button : buttons_) {
        // Check if button is in pressed/toggle state
        bool pressed = false;
        if (button.isToggle && button.buttonId < EQT::PET_BUTTON_COUNT && eq_) {
            pressed = eq_->GetPetButtonState(button.buttonId);
        }

        // Attack button requires a target to be enabled
        bool enabled = true;
        if (button.command == EQT::PET_ATTACK) {
            if (eq_) {
                CombatManager* combat = eq_->GetCombatManager();
                enabled = combat && combat->HasTarget();
            } else {
                enabled = false;
            }
        }

        drawCommandButton(driver, gui, button, pressed, enabled);
    }
}

void PetWindow::drawHealthBar(irr::video::IVideoDriver* driver,
                               const irr::core::recti& bounds, uint8_t percent)
{
    // Background
    driver->draw2DRectangle(getHpBackground(), bounds);

    // Fill based on percent
    if (percent > 0) {
        int fillWidth = (bounds.getWidth() * percent) / 100;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );

        // Color based on health level
        irr::video::SColor color;
        if (percent > 50) {
            color = getHpHigh();        // Green
        } else if (percent > 25) {
            color = getHpMedium();      // Yellow
        } else {
            color = getHpLow();         // Red
        }

        driver->draw2DRectangle(color, fillBounds);
    }

    // Border
    driver->draw2DRectangleOutline(bounds, irr::video::SColor(255, 60, 60, 60));
}

void PetWindow::drawManaBar(irr::video::IVideoDriver* driver,
                             const irr::core::recti& bounds, uint8_t percent)
{
    // Background
    driver->draw2DRectangle(getManaBackground(), bounds);

    // Fill based on percent
    if (percent > 0) {
        int fillWidth = (bounds.getWidth() * percent) / 100;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );
        driver->draw2DRectangle(getManaFill(), fillBounds);
    }

    // Border
    driver->draw2DRectangleOutline(bounds, irr::video::SColor(255, 60, 60, 60));
}

void PetWindow::drawCommandButton(irr::video::IVideoDriver* driver,
                                   irr::gui::IGUIEnvironment* gui,
                                   const PetButton& button, bool pressed, bool enabled)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    irr::core::recti boundsAbs(
        contentX + button.bounds.UpperLeftCorner.X,
        contentY + button.bounds.UpperLeftCorner.Y,
        contentX + button.bounds.LowerRightCorner.X,
        contentY + button.bounds.LowerRightCorner.Y
    );

    // Draw the button normally (always clickable)
    drawButton(driver, gui, boundsAbs, button.label, button.hovered, !enabled);

    // Draw green border around active/pressed buttons
    if (pressed) {
        irr::video::SColor greenBorder(255, 50, 200, 50);  // Bright green border
        // Draw 2-pixel thick border by drawing outline twice
        driver->draw2DRectangleOutline(boundsAbs, greenBorder);
        irr::core::recti innerBounds(
            boundsAbs.UpperLeftCorner.X + 1,
            boundsAbs.UpperLeftCorner.Y + 1,
            boundsAbs.LowerRightCorner.X - 1,
            boundsAbs.LowerRightCorner.Y - 1
        );
        driver->draw2DRectangleOutline(innerBounds, greenBorder);
    }
}

bool PetWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_ || !leftButton || !hasPet_) {
        return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
    }

    // Check if click is in content area
    irr::core::recti contentArea = getContentArea();
    irr::core::vector2di clickPos(x, y);

    // Check button clicks
    for (const auto& button : buttons_) {
        irr::core::recti boundsAbs(
            contentArea.UpperLeftCorner.X + button.bounds.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + button.bounds.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + button.bounds.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + button.bounds.LowerRightCorner.Y
        );

        if (boundsAbs.isPointInside(clickPos)) {
            // Check if button is enabled
            bool enabled = true;
            if (button.command == EQT::PET_ATTACK) {
                if (eq_) {
                    CombatManager* combat = eq_->GetCombatManager();
                    enabled = combat && combat->HasTarget();
                } else {
                    enabled = false;
                }
            }

            if (enabled && commandCallback_) {
                // Get target ID for attack command
                uint16_t targetId = 0;
                if (button.command == EQT::PET_ATTACK && eq_) {
                    CombatManager* combat = eq_->GetCombatManager();
                    if (combat && combat->HasTarget()) {
                        targetId = combat->GetTargetId();
                    }
                }
                commandCallback_(button.command, targetId);
            }
            return true;
        }
    }

    return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
}

bool PetWindow::handleMouseMove(int x, int y)
{
    if (!visible_) {
        hoveredBuffSlot_ = -1;
        return WindowBase::handleMouseMove(x, y);
    }

    irr::core::recti contentArea = getContentArea();
    irr::core::vector2di pos(x, y);

    // Check buff slot hover
    hoveredBuffSlot_ = getBuffAtPosition(x, y);

    // Update button hover states
    for (auto& button : buttons_) {
        irr::core::recti boundsAbs(
            contentArea.UpperLeftCorner.X + button.bounds.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + button.bounds.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + button.bounds.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + button.bounds.LowerRightCorner.Y
        );
        button.hovered = boundsAbs.isPointInside(pos);
    }

    return WindowBase::handleMouseMove(x, y);
}

void PetWindow::updateFlashTimer(uint32_t currentTimeMs)
{
    // Update flash timer for expiring buffs (toggle every ~250ms)
    static constexpr uint32_t FLASH_INTERVAL_MS = 250;

    if (flashTimer_ == 0) {
        flashTimer_ = currentTimeMs;
    }

    uint32_t elapsed = currentTimeMs - flashTimer_;
    if (elapsed >= FLASH_INTERVAL_MS) {
        flashOn_ = !flashOn_;
        flashTimer_ = currentTimeMs;
    }
}

const EQ::ActiveBuff* PetWindow::getHoveredPetBuff() const
{
    if (hoveredBuffSlot_ < 0 || !buffMgr_) {
        return nullptr;
    }

    const std::vector<EQ::ActiveBuff>& petBuffs = buffMgr_->getPetBuffs();

    // Find buff in hovered slot
    for (const auto& buff : petBuffs) {
        if (buff.slot >= 0 && buff.slot == hoveredBuffSlot_) {
            return &buff;
        }
    }
    return nullptr;
}

void PetWindow::drawBuffRow(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!buffMgr_) {
        return;
    }

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Calculate absolute buff row position
    int buffRowX = contentX + buffRowBounds_.UpperLeftCorner.X;
    int buffRowY = contentY + buffRowBounds_.UpperLeftCorner.Y;

    const std::vector<EQ::ActiveBuff>& petBuffs = buffMgr_->getPetBuffs();

    // Draw each buff slot
    for (size_t i = 0; i < buffSlots_.size(); i++) {
        const BuffSlotLayout& slot = buffSlots_[i];

        // Calculate absolute slot position
        irr::core::recti slotAbs(
            buffRowX + slot.bounds.UpperLeftCorner.X,
            buffRowY + slot.bounds.UpperLeftCorner.Y,
            buffRowX + slot.bounds.LowerRightCorner.X,
            buffRowY + slot.bounds.LowerRightCorner.Y
        );

        // Find buff for this slot
        const EQ::ActiveBuff* buff = nullptr;
        for (const auto& b : petBuffs) {
            if (b.slot >= 0 && static_cast<size_t>(b.slot) == i) {
                buff = &b;
                break;
            }
        }

        if (buff) {
            drawBuffSlot(driver, gui, slotAbs, *buff);
        } else {
            // Draw empty slot background
            irr::video::SColor emptyColor(100, 30, 30, 30);
            driver->draw2DRectangle(emptyColor, slotAbs);
            driver->draw2DRectangleOutline(slotAbs, irr::video::SColor(100, 60, 60, 60));
        }

        // Draw hover highlight
        if (static_cast<int>(i) == hoveredBuffSlot_) {
            driver->draw2DRectangleOutline(slotAbs, irr::video::SColor(255, 255, 255, 200));
        }
    }
}

void PetWindow::drawBuffSlot(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                              const irr::core::recti& bounds, const EQ::ActiveBuff& buff)
{
    // Background - use buff settings colors
    const auto& buffSettings = UISettings::instance().buff();
    driver->draw2DRectangle(buffSettings.buffBackground, bounds);

    // Border color based on buff/debuff
    irr::video::SColor borderColor =
        (buff.effect_type == EQ::BuffEffectType::Inverse) ?
        buffSettings.debuffBorder : buffSettings.buffBorder;
    driver->draw2DRectangleOutline(bounds, borderColor);

    // Draw spell icon (flash when about to expire)
    bool shouldDrawIcon = true;
    if (buff.isAboutToExpire() && !flashOn_) {
        // Hide icon during flash-off phase when buff is expiring
        shouldDrawIcon = false;
    }

    if (shouldDrawIcon && iconLoader_ && buffMgr_) {
        // Get spell icon from spell database
        uint32_t iconId = 0;
        EQ::SpellDatabase* spellDb = buffMgr_->getSpellDatabase();
        if (spellDb) {
            const EQ::SpellData* spell = spellDb->getSpell(buff.spell_id);
            if (spell) {
                iconId = spell->gem_icon;
            }
        }

        irr::video::ITexture* icon = iconLoader_->getIcon(iconId);
        if (icon) {
            irr::core::dimension2du iconSize = icon->getOriginalSize();
            irr::core::recti iconRect(bounds.UpperLeftCorner.X + 1, bounds.UpperLeftCorner.Y + 1,
                                      bounds.LowerRightCorner.X - 1, bounds.LowerRightCorner.Y - 1);

            // Scale to fit
            driver->draw2DImage(icon,
                iconRect,
                irr::core::recti(0, 0, iconSize.Width, iconSize.Height),
                nullptr,
                nullptr,
                true);
        }
    }

    // Draw duration timer at bottom (use smaller font area for small icons)
    if (!buff.isPermanent()) {
        std::string timeStr = buff.getTimeString();
        drawDuration(driver, gui, bounds, timeStr);
    }
}

void PetWindow::drawDuration(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                              const irr::core::recti& bounds, const std::string& timeStr)
{
    if (!gui) {
        return;
    }

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    std::wstring timeW(timeStr.begin(), timeStr.end());

    // Draw semi-transparent background at bottom of slot
    int bgHeight = 8;  // Smaller for pet buff icons
    irr::core::recti bgRect(
        bounds.UpperLeftCorner.X,
        bounds.LowerRightCorner.Y - bgHeight,
        bounds.LowerRightCorner.X,
        bounds.LowerRightCorner.Y
    );
    const auto& buffSettings = UISettings::instance().buff();
    driver->draw2DRectangle(buffSettings.durationBackground, bgRect);

    // Center text in background
    irr::core::dimension2du textSize = font->getDimension(timeW.c_str());
    int textX = bounds.UpperLeftCorner.X + (bounds.getWidth() - static_cast<int>(textSize.Width)) / 2;
    int textY = bounds.LowerRightCorner.Y - bgHeight + (bgHeight - static_cast<int>(textSize.Height)) / 2;

    irr::core::recti textRect(textX, textY, textX + textSize.Width, textY + textSize.Height);
    font->draw(timeW.c_str(), textRect, buffSettings.durationText);
}

int PetWindow::getBuffAtPosition(int x, int y) const
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Calculate absolute buff row position
    int buffRowX = contentX + buffRowBounds_.UpperLeftCorner.X;
    int buffRowY = contentY + buffRowBounds_.UpperLeftCorner.Y;

    // Check each buff slot
    for (size_t i = 0; i < buffSlots_.size(); i++) {
        const BuffSlotLayout& slot = buffSlots_[i];

        irr::core::recti slotAbs(
            buffRowX + slot.bounds.UpperLeftCorner.X,
            buffRowY + slot.bounds.UpperLeftCorner.Y,
            buffRowX + slot.bounds.LowerRightCorner.X,
            buffRowY + slot.bounds.LowerRightCorner.Y
        );

        if (slotAbs.isPointInside(irr::core::vector2di(x, y))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace ui
} // namespace eqt
