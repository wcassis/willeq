#include "client/graphics/ui/buff_window.h"
#include "client/graphics/ui/item_icon_loader.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include <fmt/format.h>

namespace eqt {
namespace ui {

BuffWindow::BuffWindow(EQ::BuffManager* buffMgr, ItemIconLoader* iconLoader)
    : WindowBase(L"Buffs", 100, 100)  // Size calculated below
    , buffMgr_(buffMgr)
    , iconLoader_(iconLoader)
{
    // Initialize layout constants from UISettings
    const auto& buffSettings = UISettings::instance().buff();
    BUFF_COLS = buffSettings.columns;
    BUFF_ROWS = buffSettings.rows;
    BUFF_SIZE = buffSettings.iconSize;
    BUFF_SPACING = buffSettings.spacing;
    WINDOW_PADDING = buffSettings.padding;

    // Calculate window size based on layout
    int width = BUFF_COLS * (BUFF_SIZE + BUFF_SPACING) + WINDOW_PADDING * 2 + 4;
    int height = BUFF_ROWS * (BUFF_SIZE + BUFF_SPACING) + WINDOW_PADDING * 2 + 4;
    setSize(width, height);

    setShowTitleBar(false);
    layoutBuffSlots();
}

BuffWindow::~BuffWindow() = default;

void BuffWindow::layoutBuffSlots()
{
    buffSlots_.clear();
    buffSlots_.reserve(BUFF_COLS * BUFF_ROWS);

    // Slots are adjacent with no spacing between them
    // WINDOW_PADDING provides space before first and after last slot
    for (int row = 0; row < BUFF_ROWS; row++) {
        for (int col = 0; col < BUFF_COLS; col++) {
            BuffSlotLayout slot;
            int x = WINDOW_PADDING + col * BUFF_SIZE;
            int y = WINDOW_PADDING + row * BUFF_SIZE;
            slot.bounds = irr::core::recti(x, y, x + BUFF_SIZE, y + BUFF_SIZE);
            buffSlots_.push_back(slot);
        }
    }
}

void BuffWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Position in upper-right area
    int x = 650;
    int y = 53;
    setPosition(x, y);
}

void BuffWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) {
        return;
    }

    // Draw window base (title bar, background, border)
    WindowBase::render(driver, gui);
}

void BuffWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!buffMgr_) {
        return;
    }

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Get buffs to display
    const std::vector<EQ::ActiveBuff>* buffs = nullptr;
    if (showingTarget_) {
        buffs = buffMgr_->getEntityBuffs(targetId_);
    } else {
        buffs = &buffMgr_->getPlayerBuffs();
    }

    // Draw all slots
    for (size_t i = 0; i < buffSlots_.size(); i++) {
        const BuffSlotLayout& slot = buffSlots_[i];

        // Calculate absolute position
        irr::core::recti absRect(
            contentX + slot.bounds.UpperLeftCorner.X,
            contentY + slot.bounds.UpperLeftCorner.Y,
            contentX + slot.bounds.LowerRightCorner.X,
            contentY + slot.bounds.LowerRightCorner.Y
        );

        // Find buff for this slot
        const EQ::ActiveBuff* buff = nullptr;
        if (buffs) {
            for (const auto& b : *buffs) {
                if (b.slot >= 0 && static_cast<size_t>(b.slot) == i) {
                    buff = &b;
                    break;
                }
            }
        }

        if (buff) {
            drawBuffSlot(driver, gui, static_cast<int>(i), *buff);
        } else {
            // Draw empty slot
            driver->draw2DRectangle(getEmptySlotColor(), absRect);
            driver->draw2DRectangleOutline(absRect, irr::video::SColor(100, 60, 60, 60));
        }

        // Draw hover highlight
        if (static_cast<int>(i) == hoveredSlot_) {
            driver->draw2DRectangleOutline(absRect, irr::video::SColor(255, 255, 255, 200));
        }
    }
}

void BuffWindow::drawBuffSlot(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                               int slotIndex, const EQ::ActiveBuff& buff)
{
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= buffSlots_.size()) {
        return;
    }

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    const BuffSlotLayout& slot = buffSlots_[slotIndex];
    irr::core::recti absRect(
        contentX + slot.bounds.UpperLeftCorner.X,
        contentY + slot.bounds.UpperLeftCorner.Y,
        contentX + slot.bounds.LowerRightCorner.X,
        contentY + slot.bounds.LowerRightCorner.Y
    );

    // Background
    driver->draw2DRectangle(getBuffBackground(), absRect);

    // Border color based on buff/debuff
    irr::video::SColor borderColor =
        (buff.effect_type == EQ::BuffEffectType::Inverse) ?
        getDebuffBorder() : getBuffBorder();
    driver->draw2DRectangleOutline(absRect, borderColor);

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
            irr::core::recti iconRect(absRect.UpperLeftCorner.X + 1, absRect.UpperLeftCorner.Y + 1,
                                      absRect.LowerRightCorner.X - 1, absRect.LowerRightCorner.Y - 1);

            // Scale to fit
            driver->draw2DImage(icon,
                iconRect,
                irr::core::recti(0, 0, iconSize.Width, iconSize.Height),
                nullptr,
                nullptr,
                true);
        }
    }

    // Draw duration timer at bottom
    if (!buff.isPermanent()) {
        std::string timeStr = buff.getTimeString();
        drawDuration(driver, gui, absRect, timeStr);
    }
}

void BuffWindow::drawDuration(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
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
    irr::core::dimension2du textSize = font->getDimension(timeW.c_str());

    // Draw semi-transparent background at bottom of slot
    int bgHeight = 10;
    irr::core::recti bgRect(
        bounds.UpperLeftCorner.X,
        bounds.LowerRightCorner.Y - bgHeight,
        bounds.LowerRightCorner.X,
        bounds.LowerRightCorner.Y
    );
    driver->draw2DRectangle(getDurationBackground(), bgRect);

    // Center text in background
    int textX = bounds.UpperLeftCorner.X + (bounds.getWidth() - static_cast<int>(textSize.Width)) / 2;
    int textY = bounds.LowerRightCorner.Y - bgHeight + (bgHeight - static_cast<int>(textSize.Height)) / 2;

    irr::core::recti textRect(textX, textY, textX + textSize.Width, textY + textSize.Height);
    font->draw(timeW.c_str(), textRect, getDurationTextColor());
}

int BuffWindow::getBuffAtPosition(int x, int y) const
{
    irr::core::recti contentArea = getContentArea();
    int relX = x - contentArea.UpperLeftCorner.X;
    int relY = y - contentArea.UpperLeftCorner.Y;

    for (size_t i = 0; i < buffSlots_.size(); i++) {
        if (buffSlots_[i].bounds.isPointInside(irr::core::vector2di(relX, relY))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void BuffWindow::update(uint32_t currentTimeMs)
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

void BuffWindow::showTargetBuffs(uint16_t targetId)
{
    showingTarget_ = true;
    targetId_ = targetId;
    setTitle(L"Target Buffs");
}

void BuffWindow::showPlayerBuffs()
{
    showingTarget_ = false;
    targetId_ = 0;
    setTitle(L"Buffs");
}

bool BuffWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    // First handle window dragging from base class
    if (WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl)) {
        return true;
    }

    if (!visible_ || !containsPoint(x, y)) {
        return false;
    }

    int slot = getBuffAtPosition(x, y);
    if (slot < 0) {
        return true;  // Still consumed click on window
    }

    if (!leftButton) {
        // Right-click to cancel buff (player buffs only)
        if (!showingTarget_ && cancelCallback_) {
            cancelCallback_(static_cast<uint8_t>(slot));
        }
        return true;
    }

    return true;
}

bool BuffWindow::handleMouseMove(int x, int y)
{
    // Handle window dragging
    if (WindowBase::handleMouseMove(x, y)) {
        return true;
    }

    if (!visible_) {
        hoveredSlot_ = -1;
        return false;
    }

    int newHoveredSlot = -1;
    if (containsPoint(x, y)) {
        newHoveredSlot = getBuffAtPosition(x, y);
    }

    if (newHoveredSlot != hoveredSlot_) {
        hoveredSlot_ = newHoveredSlot;

        if (hoveredSlot_ >= 0) {
            // Could show tooltip here
            showTooltip_ = true;
        } else {
            showTooltip_ = false;
        }
    }

    return containsPoint(x, y);
}

void BuffWindow::showBuffTooltip(irr::gui::IGUIEnvironment* gui,
                                  const EQ::ActiveBuff& buff, int x, int y)
{
    // Tooltip rendering would be handled by WindowManager or a separate tooltip class
    // For now, this is a placeholder
}

const EQ::ActiveBuff* BuffWindow::getHoveredBuff() const
{
    if (hoveredSlot_ < 0 || !buffMgr_) {
        return nullptr;
    }

    // Get buffs to search
    const std::vector<EQ::ActiveBuff>* buffs = nullptr;
    if (showingTarget_) {
        buffs = buffMgr_->getEntityBuffs(targetId_);
    } else {
        buffs = &buffMgr_->getPlayerBuffs();
    }

    if (!buffs) {
        return nullptr;
    }

    // Find buff in hovered slot
    for (const auto& b : *buffs) {
        if (b.slot >= 0 && b.slot == hoveredSlot_) {
            return &b;
        }
    }
    return nullptr;
}

} // namespace ui
} // namespace eqt
