#include "client/graphics/ui/spell_book_window.h"
#include "client/graphics/ui/item_icon_loader.h"
#include "client/graphics/ui/ui_settings.h"
#include "client/spell/spell_manager.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>

namespace eqt {
namespace ui {

SpellBookWindow::SpellBookWindow(EQ::SpellManager* spell_mgr, ItemIconLoader* icon_loader)
    : WindowBase(L"Spellbook", 100, 100)  // Size calculated below
    , spellMgr_(spell_mgr)
    , iconLoader_(icon_loader)
{
    // Initialize layout constants from UISettings
    const auto& bookSettings = UISettings::instance().spellBook();
    PAGE_WIDTH = bookSettings.pageWidth;
    PAGE_SPACING = bookSettings.pageSpacing;
    ICON_SIZE = bookSettings.iconSize;
    ROW_HEIGHT = bookSettings.rowHeight;
    ROW_SPACING = bookSettings.rowSpacing;
    LEFT_PAGE_X = bookSettings.leftPageX;
    SLOTS_START_Y = bookSettings.slotsStartY;
    NAME_OFFSET_X = bookSettings.nameOffsetX;
    NAME_MAX_WIDTH = bookSettings.nameMaxWidth;
    NAV_BUTTON_WIDTH = bookSettings.navButtonWidth;
    NAV_BUTTON_HEIGHT = bookSettings.navButtonHeight;

    // Compute derived values
    RIGHT_PAGE_X = PAGE_WIDTH + PAGE_SPACING + 8;
    int windowWidth = PAGE_WIDTH * 2 + PAGE_SPACING + 16;
    int windowHeight = 280;  // Fixed height

    setSize(windowWidth, windowHeight);

    initializeLayout();
    refresh();
}

void SpellBookWindow::initializeLayout()
{
    // Initialize spell slot positions - two-page layout (left + right)
    // Left page: slots 0-7, Right page: slots 8-15
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        bool isLeftPage = (i < SPELLS_PER_PAGE);
        int pageSlot = isLeftPage ? i : (i - SPELLS_PER_PAGE);
        int x = isLeftPage ? LEFT_PAGE_X : RIGHT_PAGE_X;
        int y = SLOTS_START_Y + pageSlot * (ROW_HEIGHT + ROW_SPACING);

        // Bounds cover the full row (icon + name area) for hit testing
        spellSlots_[i].bounds = irr::core::recti(x, y, x + PAGE_WIDTH - 4, y + ROW_HEIGHT);
        spellSlots_[i].spell_id = EQ::SPELL_UNKNOWN;
        spellSlots_[i].is_empty = true;
    }

    // Get window dimensions from bounds
    int windowWidth = bounds_.getWidth();
    int windowHeight = bounds_.getHeight();

    // Navigation buttons at bottom (centered in window)
    int navY = windowHeight - getBorderWidth() - getContentPadding() - NAV_BUTTON_HEIGHT - 3;
    int prevX = LEFT_PAGE_X;
    int nextX = windowWidth - getBorderWidth() - NAV_BUTTON_WIDTH - 4;

    prevButtonBounds_ = irr::core::recti(prevX, navY, prevX + NAV_BUTTON_WIDTH, navY + NAV_BUTTON_HEIGHT);
    nextButtonBounds_ = irr::core::recti(nextX, navY, nextX + NAV_BUTTON_WIDTH, navY + NAV_BUTTON_HEIGHT);
}

void SpellBookWindow::refresh()
{
    layoutPage();
}

void SpellBookWindow::layoutPage()
{
    if (!spellMgr_) {
        return;
    }

    // Each spread shows TOTAL_SLOTS (16) spells
    int startSlot = currentPage_ * TOTAL_SLOTS;

    for (int i = 0; i < TOTAL_SLOTS; i++) {
        uint16_t bookSlot = static_cast<uint16_t>(startSlot + i);
        uint32_t spellId = spellMgr_->getSpellbookSpell(bookSlot);

        spellSlots_[i].book_slot = bookSlot;
        spellSlots_[i].spell_id = spellId;
        spellSlots_[i].is_empty = (spellId == EQ::SPELL_UNKNOWN || spellId == 0xFFFFFFFF);
        spellSlots_[i].is_hovered = false;
    }
}

int SpellBookWindow::getTotalPages() const
{
    // 400 spellbook slots / 16 per spread = 25 spreads
    return (EQ::MAX_SPELLBOOK_SLOTS + TOTAL_SLOTS - 1) / TOTAL_SLOTS;
}

void SpellBookWindow::nextPage()
{
    int maxPage = getTotalPages() - 1;
    if (currentPage_ < maxPage) {
        currentPage_++;
        layoutPage();
    }
}

void SpellBookWindow::prevPage()
{
    if (currentPage_ > 0) {
        currentPage_--;
        layoutPage();
    }
}

void SpellBookWindow::goToPage(int page)
{
    int maxPage = getTotalPages() - 1;
    currentPage_ = std::clamp(page, 0, maxPage);
    layoutPage();
}

bool SpellBookWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Handle title bar drag
    if (titleBarContainsPoint(x, y)) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check navigation buttons
    if (prevButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        prevPage();
        return true;
    }

    if (nextButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        nextPage();
        return true;
    }

    // Check spell slots
    int slotIndex = getSlotIndexAtPosition(relX, relY);
    if (slotIndex >= 0 && slotIndex < TOTAL_SLOTS) {
        const SpellSlot& slot = spellSlots_[slotIndex];
        if (!slot.is_empty && slot.spell_id != EQ::SPELL_UNKNOWN) {
            // Set spell on cursor for drag-to-gem memorization
            if (setSpellCursorCallback_ && spellMgr_ && iconLoader_) {
                const EQ::SpellData* spell = spellMgr_->getSpell(slot.spell_id);
                if (spell) {
                    irr::video::ITexture* icon = iconLoader_->getIcon(spell->gem_icon);
                    setSpellCursorCallback_(slot.spell_id, icon);
                }
            }
            return true;
        }
    }

    return true;  // Consume click even if nothing hit
}

bool SpellBookWindow::handleMouseUp(int x, int y, bool leftButton)
{
    if (dragging_) {
        dragging_ = false;
        LOG_INFO(MOD_UI, "Window 'Spellbook' moved to position ({}, {}) bounds=({},{} - {},{})",
                 bounds_.UpperLeftCorner.X, bounds_.UpperLeftCorner.Y,
                 bounds_.UpperLeftCorner.X, bounds_.UpperLeftCorner.Y,
                 bounds_.LowerRightCorner.X, bounds_.LowerRightCorner.Y);
        return true;
    }
    return false;
}

bool SpellBookWindow::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return false;
    }

    // Store mouse position for tooltip
    mouseX_ = x;
    mouseY_ = y;

    if (dragging_) {
        int newX = x - dragOffset_.X;
        int newY = y - dragOffset_.Y;
        setPosition(newX, newY);  // Use base class method to update both bounds_ and titleBar_
        return true;
    }

    if (!containsPoint(x, y)) {
        // Clear any hover state when leaving window
        if (hoveredSlotIndex_ >= 0) {
            spellSlots_[hoveredSlotIndex_].is_hovered = false;
            hoveredSlotIndex_ = -1;
            if (spellHoverEndCallback_) {
                spellHoverEndCallback_();
            }
        }
        prevButtonHovered_ = false;
        nextButtonHovered_ = false;
        return false;
    }

    // Convert to window-relative coordinates
    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Update button hover states
    prevButtonHovered_ = prevButtonBounds_.isPointInside(irr::core::vector2di(relX, relY));
    nextButtonHovered_ = nextButtonBounds_.isPointInside(irr::core::vector2di(relX, relY));

    // Update spell slot hover
    int newHoveredSlot = getSlotIndexAtPosition(relX, relY);

    if (newHoveredSlot != hoveredSlotIndex_) {
        // Clear old hover
        if (hoveredSlotIndex_ >= 0 && hoveredSlotIndex_ < TOTAL_SLOTS) {
            spellSlots_[hoveredSlotIndex_].is_hovered = false;
        }

        // Set new hover
        if (newHoveredSlot >= 0 && newHoveredSlot < TOTAL_SLOTS) {
            spellSlots_[newHoveredSlot].is_hovered = true;
            const SpellSlot& slot = spellSlots_[newHoveredSlot];
            if (!slot.is_empty && slot.spell_id != EQ::SPELL_UNKNOWN) {
                if (spellHoverCallback_) {
                    spellHoverCallback_(slot.spell_id, x, y);
                }
            }
        } else if (hoveredSlotIndex_ >= 0) {
            if (spellHoverEndCallback_) {
                spellHoverEndCallback_();
            }
        }

        hoveredSlotIndex_ = newHoveredSlot;
    }

    return true;
}

int SpellBookWindow::getSlotIndexAtPosition(int relX, int relY) const
{
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        if (spellSlots_[i].bounds.isPointInside(irr::core::vector2di(relX, relY))) {
            return i;
        }
    }
    return -1;
}

uint32_t SpellBookWindow::getSpellAtPosition(int x, int y) const
{
    if (!containsPoint(x, y)) {
        return EQ::SPELL_UNKNOWN;
    }

    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    int slotIndex = getSlotIndexAtPosition(relX, relY);
    if (slotIndex >= 0 && slotIndex < TOTAL_SLOTS) {
        return spellSlots_[slotIndex].spell_id;
    }

    return EQ::SPELL_UNKNOWN;
}

void SpellBookWindow::renderContent(irr::video::IVideoDriver* driver,
                                    irr::gui::IGUIEnvironment* gui)
{
    // Draw spine line between the two pages
    int spineX = bounds_.UpperLeftCorner.X + LEFT_PAGE_X + PAGE_WIDTH + PAGE_SPACING / 2;
    driver->draw2DLine(
        irr::core::position2di(spineX, bounds_.UpperLeftCorner.Y + SLOTS_START_Y - 4),
        irr::core::position2di(spineX, bounds_.UpperLeftCorner.Y + bounds_.getHeight() - 40),
        irr::video::SColor(150, 80, 80, 100)
    );

    // Render spell slots (both pages)
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        renderSpellSlot(driver, gui, spellSlots_[i]);
    }

    // Render navigation
    renderNavigationButtons(driver, gui);
    renderPageInfo(driver, gui);

    // Render tooltip for hovered spell (rendered last to be on top)
    if (hoveredSlotIndex_ >= 0 && hoveredSlotIndex_ < TOTAL_SLOTS) {
        const SpellSlot& slot = spellSlots_[hoveredSlotIndex_];
        if (!slot.is_empty && slot.spell_id != EQ::SPELL_UNKNOWN && spellMgr_) {
            const EQ::SpellData* spell = spellMgr_->getSpell(slot.spell_id);
            if (spell) {
                renderSpellTooltip(driver, gui, spell, mouseX_, mouseY_);
            }
        }
    }
}

void SpellBookWindow::renderSpellSlot(irr::video::IVideoDriver* driver,
                                      irr::gui::IGUIEnvironment* gui,
                                      const SpellSlot& slot)
{
    // Calculate absolute position for the row
    irr::core::recti absRect(
        bounds_.UpperLeftCorner.X + slot.bounds.UpperLeftCorner.X,
        bounds_.UpperLeftCorner.Y + slot.bounds.UpperLeftCorner.Y,
        bounds_.UpperLeftCorner.X + slot.bounds.LowerRightCorner.X,
        bounds_.UpperLeftCorner.Y + slot.bounds.LowerRightCorner.Y
    );

    // Background color for the row
    irr::video::SColor bgColor;
    if (slot.is_empty) {
        return;  // Don't draw empty slots
    } else if (slot.is_hovered) {
        bgColor = irr::video::SColor(255, 70, 70, 100);  // Highlighted
        driver->draw2DRectangle(bgColor, absRect);
    }

    // Draw spell icon and name
    if (!slot.is_empty && slot.spell_id != EQ::SPELL_UNKNOWN && spellMgr_) {
        const EQ::SpellData* spell = spellMgr_->getSpell(slot.spell_id);
        if (spell && iconLoader_) {
            // Icon destination rect (scaled to ICON_SIZE)
            irr::core::recti iconRect(
                absRect.UpperLeftCorner.X + 1,
                absRect.UpperLeftCorner.Y + 1,
                absRect.UpperLeftCorner.X + 1 + ICON_SIZE,
                absRect.UpperLeftCorner.Y + 1 + ICON_SIZE
            );

            // Use gem_icon which has the actual icon ID
            irr::video::ITexture* icon = iconLoader_->getIcon(spell->gem_icon);
            if (icon) {
                irr::core::dimension2du iconSize = icon->getOriginalSize();
                // Scale icon to fit - use color array for proper alpha
                irr::video::SColor colors[4] = {
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255)
                };
                driver->draw2DImage(icon,
                    iconRect,  // Destination (scaled)
                    irr::core::recti(0, 0, iconSize.Width, iconSize.Height),  // Source
                    nullptr,   // No clip rect
                    colors,    // Corner colors
                    true);     // Use alpha
            }

            // Draw spell name to the right of the icon
            if (gui) {
                irr::gui::IGUIFont* font = gui->getBuiltInFont();
                if (font) {
                    std::wstring spellName(spell->name.begin(), spell->name.end());

                    irr::core::dimension2du textSize = font->getDimension(spellName.c_str());
                    int textX = absRect.UpperLeftCorner.X + NAME_OFFSET_X;
                    int textY = absRect.UpperLeftCorner.Y + (ROW_HEIGHT - static_cast<int>(textSize.Height)) / 2;

                    // Text color - brighter if hovered
                    irr::video::SColor textColor = slot.is_hovered ?
                        irr::video::SColor(255, 255, 255, 255) :
                        irr::video::SColor(255, 200, 200, 200);

                    font->draw(spellName.c_str(),
                        irr::core::recti(textX, textY, textX + textSize.Width, textY + textSize.Height),
                        textColor);
                }
            }
        }
    }
}

void SpellBookWindow::renderNavigationButtons(irr::video::IVideoDriver* driver,
                                              irr::gui::IGUIEnvironment* gui)
{
    // Previous button
    irr::core::recti prevAbs(
        bounds_.UpperLeftCorner.X + prevButtonBounds_.UpperLeftCorner.X,
        bounds_.UpperLeftCorner.Y + prevButtonBounds_.UpperLeftCorner.Y,
        bounds_.UpperLeftCorner.X + prevButtonBounds_.LowerRightCorner.X,
        bounds_.UpperLeftCorner.Y + prevButtonBounds_.LowerRightCorner.Y
    );

    bool prevEnabled = (currentPage_ > 0);
    drawButton(driver, gui, prevAbs, L"<", prevButtonHovered_ && prevEnabled, !prevEnabled);

    // Next button
    irr::core::recti nextAbs(
        bounds_.UpperLeftCorner.X + nextButtonBounds_.UpperLeftCorner.X,
        bounds_.UpperLeftCorner.Y + nextButtonBounds_.UpperLeftCorner.Y,
        bounds_.UpperLeftCorner.X + nextButtonBounds_.LowerRightCorner.X,
        bounds_.UpperLeftCorner.Y + nextButtonBounds_.LowerRightCorner.Y
    );

    bool nextEnabled = (currentPage_ < getTotalPages() - 1);
    drawButton(driver, gui, nextAbs, L">", nextButtonHovered_ && nextEnabled, !nextEnabled);
}

void SpellBookWindow::renderPageInfo(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* gui)
{
    if (!gui) return;

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // Page X of Y
    std::string pageStr = fmt::format("Page {} of {}", currentPage_ + 1, getTotalPages());
    std::wstring pageText(pageStr.begin(), pageStr.end());

    irr::core::dimension2du textSize = font->getDimension(pageText.c_str());
    int textX = bounds_.UpperLeftCorner.X + (bounds_.getWidth() - static_cast<int>(textSize.Width)) / 2;
    int textY = bounds_.UpperLeftCorner.Y + prevButtonBounds_.UpperLeftCorner.Y +
                (NAV_BUTTON_HEIGHT - static_cast<int>(textSize.Height)) / 2;

    font->draw(pageText.c_str(),
        irr::core::recti(textX, textY, textX + textSize.Width, textY + textSize.Height),
        irr::video::SColor(255, 200, 200, 200));
}

void SpellBookWindow::renderSpellTooltip(irr::video::IVideoDriver* driver,
                                         irr::gui::IGUIEnvironment* gui,
                                         const EQ::SpellData* spell,
                                         int mouseX, int mouseY)
{
    if (!driver || !gui || !spell) return;

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
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
    int tooltipX = mouseX + 15;
    int tooltipY = mouseY;

    // Ensure tooltip stays on screen (assume 800x600 min)
    if (tooltipX + tooltipWidth > 800) {
        tooltipX = mouseX - tooltipWidth - 5;
    }
    if (tooltipY + tooltipHeight > 600) {
        tooltipY = 600 - tooltipHeight;
    }

    irr::core::recti tooltipRect(tooltipX, tooltipY, tooltipX + tooltipWidth, tooltipY + tooltipHeight);

    // Draw tooltip background
    driver->draw2DRectangle(irr::video::SColor(240, 20, 20, 30), tooltipRect);

    // Draw border
    driver->draw2DRectangleOutline(tooltipRect, irr::video::SColor(255, 100, 100, 140));

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

} // namespace ui
} // namespace eqt
