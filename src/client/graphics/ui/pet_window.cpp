#include "client/graphics/ui/pet_window.h"
#include "client/eq.h"
#include "client/combat.h"
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

    // Calculate content height: name + HP bar + mana bar + button rows
    int contentHeight = WINDOW_PADDING +
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

    // Name and level at top
    int yPos = WINDOW_PADDING;
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
                    contentY + WINDOW_PADDING + 20,
                    contentX + getWidth() - WINDOW_PADDING,
                    contentY + WINDOW_PADDING + 40
                );
                font->draw(L"No Pet", msgBounds, irr::video::SColor(255, 150, 150, 150));
            }
        }
        return;
    }

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

    // Use base class drawButton for consistent styling
    // But override for pressed toggle buttons
    if (pressed) {
        // Draw pressed toggle button with green tint
        driver->draw2DRectangle(getToggleOnColor(), boundsAbs);
        driver->draw2DRectangleOutline(boundsAbs, irr::video::SColor(255, 100, 160, 100));

        if (gui) {
            irr::gui::IGUIFont* font = gui->getBuiltInFont();
            if (font) {
                font->draw(button.label.c_str(), boundsAbs, irr::video::SColor(255, 255, 255, 255),
                          true, true);  // Center text
            }
        }
    } else {
        // Use standard button drawing
        drawButton(driver, gui, boundsAbs, button.label, button.hovered, !enabled);
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
        return WindowBase::handleMouseMove(x, y);
    }

    irr::core::recti contentArea = getContentArea();
    irr::core::vector2di pos(x, y);

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

} // namespace ui
} // namespace eqt
