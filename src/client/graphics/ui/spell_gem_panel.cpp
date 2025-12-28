#include "client/graphics/ui/spell_gem_panel.h"
#include "client/graphics/ui/item_icon_loader.h"
#include "client/graphics/ui/ui_settings.h"
#include "client/spell/spell_manager.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <set>

namespace eqt {
namespace ui {

SpellGemPanel::SpellGemPanel(EQ::SpellManager* spellMgr, ItemIconLoader* iconLoader)
    : spellMgr_(spellMgr)
    , iconLoader_(iconLoader)
    , position_(0, 0)
{
    // Initialize layout constants from UISettings
    const auto& gemSettings = UISettings::instance().spellGems();
    GEM_WIDTH = gemSettings.gemWidth;
    GEM_HEIGHT = gemSettings.gemHeight;
    GEM_SPACING = gemSettings.gemSpacing;
    PANEL_PADDING = gemSettings.panelPadding;
    SPELLBOOK_BUTTON_SIZE = gemSettings.spellbookButtonSize;
    SPELLBOOK_BUTTON_MARGIN = gemSettings.spellbookButtonMargin;

    initializeLayout();
}

void SpellGemPanel::initializeLayout()
{
    // Vertical layout - gems stacked on top of each other
    for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
        int x = PANEL_PADDING;
        int y = PANEL_PADDING + i * (GEM_HEIGHT + GEM_SPACING);

        gems_[i].bounds = irr::core::recti(x, y, x + GEM_WIDTH, y + GEM_HEIGHT);
        gems_[i].iconBounds = irr::core::recti(x, y, x + GEM_WIDTH, y + GEM_HEIGHT);
        gems_[i].isHovered = false;
    }

    // Spellbook button at bottom of panel (small book icon, centered under gems)
    int buttonY = PANEL_PADDING + EQ::MAX_SPELL_GEMS * (GEM_HEIGHT + GEM_SPACING) + SPELLBOOK_BUTTON_MARGIN;
    int buttonX = PANEL_PADDING + (GEM_WIDTH - SPELLBOOK_BUTTON_SIZE) / 2;

    spellbookButtonBounds_ = irr::core::recti(
        buttonX, buttonY,
        buttonX + SPELLBOOK_BUTTON_SIZE, buttonY + SPELLBOOK_BUTTON_SIZE
    );
}

void SpellGemPanel::setPosition(int x, int y)
{
    position_.X = x;
    position_.Y = y;
}

irr::core::recti SpellGemPanel::getBounds() const
{
    int width = PANEL_PADDING * 2 + GEM_WIDTH;
    int height = PANEL_PADDING * 2 + EQ::MAX_SPELL_GEMS * GEM_HEIGHT +
                 (EQ::MAX_SPELL_GEMS - 1) * GEM_SPACING +
                 SPELLBOOK_BUTTON_MARGIN + SPELLBOOK_BUTTON_SIZE;
    return irr::core::recti(position_.X, position_.Y,
                           position_.X + width, position_.Y + height);
}

bool SpellGemPanel::containsPoint(int x, int y) const
{
    return getBounds().isPointInside(irr::core::vector2di(x, y));
}

int SpellGemPanel::getGemAtPosition(int x, int y) const
{
    if (!containsPoint(x, y)) {
        return -1;
    }

    int relX = x - position_.X;
    int relY = y - position_.Y;

    for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
        if (gems_[i].bounds.isPointInside(irr::core::vector2di(relX, relY))) {
            return i;
        }
    }
    return -1;
}

void SpellGemPanel::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_ || !driver) {
        return;
    }

    // Draw panel background
    irr::core::recti panelBounds = getBounds();
    driver->draw2DRectangle(irr::video::SColor(200, 20, 20, 30), panelBounds);
    driver->draw2DRectangleOutline(panelBounds, irr::video::SColor(255, 60, 60, 80));

    // Draw each gem
    for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
        drawGem(driver, gui, static_cast<uint8_t>(i), gems_[i]);
    }

    // Draw spellbook button
    drawSpellbookButton(driver);
}

void SpellGemPanel::drawGem(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                            uint8_t slot, const GemSlotLayout& gem)
{
    if (!spellMgr_) {
        return;
    }

    uint32_t spellId = spellMgr_->getMemorizedSpell(slot);
    EQ::GemState state = spellMgr_->getGemState(slot);

    // Calculate absolute position
    irr::core::recti absRect(
        position_.X + gem.bounds.UpperLeftCorner.X,
        position_.Y + gem.bounds.UpperLeftCorner.Y,
        position_.X + gem.bounds.LowerRightCorner.X,
        position_.Y + gem.bounds.LowerRightCorner.Y
    );

    irr::core::recti absIconRect(
        position_.X + gem.iconBounds.UpperLeftCorner.X,
        position_.Y + gem.iconBounds.UpperLeftCorner.Y,
        position_.X + gem.iconBounds.LowerRightCorner.X,
        position_.Y + gem.iconBounds.LowerRightCorner.Y
    );

    // Background color based on state
    irr::video::SColor bgColor;
    switch (state) {
        case EQ::GemState::Empty:
            bgColor = irr::video::SColor(200, 40, 40, 40);
            break;
        case EQ::GemState::Ready:
            bgColor = irr::video::SColor(255, 50, 50, 90);
            break;
        case EQ::GemState::Casting:
            bgColor = irr::video::SColor(255, 80, 80, 180);
            break;
        case EQ::GemState::Refresh:
            bgColor = irr::video::SColor(200, 70, 35, 35);
            break;
        case EQ::GemState::MemorizeProgress:
            bgColor = irr::video::SColor(200, 70, 70, 35);
            break;
    }

    // Draw gem background
    driver->draw2DRectangle(bgColor, absRect);

    // Draw border - highlighted if hovered
    irr::video::SColor borderColor = gem.isHovered ?
        irr::video::SColor(255, 255, 255, 200) :
        irr::video::SColor(255, 80, 80, 100);
    driver->draw2DRectangleOutline(absRect, borderColor);

    // Draw spell icon if memorized
    if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
        const EQ::SpellData* spell = spellMgr_->getSpell(spellId);
        if (!spell) {
            // Log once per spell ID to avoid spam
            static std::set<uint32_t> loggedMissing;
            if (loggedMissing.find(spellId) == loggedMissing.end()) {
                LOG_WARN(MOD_UI, "SpellGem slot {} - spell ID {} not found in database", slot, spellId);
                loggedMissing.insert(spellId);
            }
        }
        if (spell && iconLoader_) {
            // Use gem_icon for the spell gem display
            // Log once per slot to debug icon loading
            static std::set<uint8_t> loggedSlots;
            if (loggedSlots.find(slot) == loggedSlots.end()) {
                LOG_DEBUG(MOD_UI, "SpellGem slot {} spell '{}' (ID {}) requesting gem_icon {}",
                         slot, spell->name, spellId, spell->gem_icon);
                loggedSlots.insert(slot);
            }
            irr::video::ITexture* icon = iconLoader_->getIcon(spell->gem_icon);
            if (!icon) {
                // Log once per gem_icon to avoid spam
                static std::set<uint16_t> loggedMissingIcons;
                if (loggedMissingIcons.find(spell->gem_icon) == loggedMissingIcons.end()) {
                    LOG_WARN(MOD_UI, "SpellGem slot {} spell '{}' (ID {}) - icon not found for gem_icon {}",
                             slot, spell->name, spellId, spell->gem_icon);
                    loggedMissingIcons.insert(spell->gem_icon);
                }
            }
            if (icon) {
                irr::core::dimension2du iconSize = icon->getOriginalSize();
                // Log once per slot to debug rendering
                static std::set<uint8_t> loggedRender;
                if (loggedRender.find(slot) == loggedRender.end()) {
                    LOG_DEBUG(MOD_UI, "SpellGem slot {} drawing icon {}x{} to rect ({},{} - {},{})",
                             slot, iconSize.Width, iconSize.Height,
                             absIconRect.UpperLeftCorner.X, absIconRect.UpperLeftCorner.Y,
                             absIconRect.LowerRightCorner.X, absIconRect.LowerRightCorner.Y);
                    loggedRender.insert(slot);
                }
                // Scale icon to fit within the gem slot - use color array for proper alpha
                irr::video::SColor colors[4] = {
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255)
                };
                driver->draw2DImage(icon,
                    absIconRect,  // Destination rect (scaled to gem size)
                    irr::core::recti(0, 0, iconSize.Width, iconSize.Height),  // Source rect (full icon)
                    nullptr,   // No clip rect
                    colors,    // Corner colors
                    true);     // Use alpha
            }

            // Draw mana cost in bottom-right corner
            if (gui) {
                irr::gui::IGUIFont* font = gui->getBuiltInFont();
                if (font && spell->mana_cost > 0) {
                    std::string manaStr = std::to_string(spell->mana_cost);
                    std::wstring manaCostW(manaStr.begin(), manaStr.end());

                    irr::core::dimension2du textSize = font->getDimension(manaCostW.c_str());
                    int textX = absRect.LowerRightCorner.X - static_cast<int>(textSize.Width) - 2;
                    int textY = absRect.LowerRightCorner.Y - static_cast<int>(textSize.Height) - 1;

                    // Draw shadow
                    font->draw(manaCostW.c_str(),
                        irr::core::recti(textX + 1, textY + 1, textX + textSize.Width + 1, textY + textSize.Height + 1),
                        irr::video::SColor(200, 0, 0, 0));
                    // Draw text
                    font->draw(manaCostW.c_str(),
                        irr::core::recti(textX, textY, textX + textSize.Width, textY + textSize.Height),
                        irr::video::SColor(255, 180, 180, 255));
                }
            }
        }
    }

    // Draw cooldown overlay
    if (state == EQ::GemState::Refresh) {
        float progress = spellMgr_->getGemCooldownProgress(slot);
        drawCooldownOverlay(driver, gem, progress);
    }

    // Draw memorization progress
    if (state == EQ::GemState::MemorizeProgress) {
        float progress = spellMgr_->getMemorizeProgress(slot);
        drawMemorizeProgress(driver, gem, progress);
    }

    // Draw casting highlight
    if (state == EQ::GemState::Casting) {
        drawCastingHighlight(driver, gem);
    }

    // Draw gem number (1-8) in top-left corner
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            std::wstring numStr = std::to_wstring(slot + 1);
            // Draw shadow
            font->draw(numStr.c_str(),
                irr::core::recti(absRect.UpperLeftCorner.X + 3, absRect.UpperLeftCorner.Y + 2,
                                 absRect.UpperLeftCorner.X + 15, absRect.UpperLeftCorner.Y + 14),
                irr::video::SColor(200, 0, 0, 0));
            // Draw number
            font->draw(numStr.c_str(),
                irr::core::recti(absRect.UpperLeftCorner.X + 2, absRect.UpperLeftCorner.Y + 1,
                                 absRect.UpperLeftCorner.X + 14, absRect.UpperLeftCorner.Y + 13),
                irr::video::SColor(220, 255, 255, 255));
        }
    }
}

void SpellGemPanel::drawCooldownOverlay(irr::video::IVideoDriver* driver,
                                         const GemSlotLayout& gem, float progress)
{
    // Progress is 0.0 (just started cooldown) to 1.0 (ready)
    // Draw dark overlay that shrinks from top as cooldown completes
    int overlayHeight = static_cast<int>(gem.bounds.getHeight() * (1.0f - progress));

    if (overlayHeight > 0) {
        irr::core::recti overlayRect(
            position_.X + gem.bounds.UpperLeftCorner.X,
            position_.Y + gem.bounds.UpperLeftCorner.Y,
            position_.X + gem.bounds.LowerRightCorner.X,
            position_.Y + gem.bounds.UpperLeftCorner.Y + overlayHeight
        );
        driver->draw2DRectangle(irr::video::SColor(180, 0, 0, 0), overlayRect);
    }
}

void SpellGemPanel::drawMemorizeProgress(irr::video::IVideoDriver* driver,
                                          const GemSlotLayout& gem, float progress)
{
    // Progress is 0.0 (just started) to 1.0 (complete)
    // Draw dark overlay that shrinks from top as memorization completes
    int overlayHeight = static_cast<int>(gem.bounds.getHeight() * (1.0f - progress));

    if (overlayHeight > 0) {
        irr::core::recti overlayRect(
            position_.X + gem.bounds.UpperLeftCorner.X,
            position_.Y + gem.bounds.UpperLeftCorner.Y,
            position_.X + gem.bounds.LowerRightCorner.X,
            position_.Y + gem.bounds.UpperLeftCorner.Y + overlayHeight
        );
        driver->draw2DRectangle(irr::video::SColor(150, 40, 40, 0), overlayRect);
    }

    // Draw "MEM" text
    // Note: Would need gui parameter to draw text here
}

void SpellGemPanel::drawCastingHighlight(irr::video::IVideoDriver* driver,
                                          const GemSlotLayout& gem)
{
    // Draw pulsing border for casting state
    irr::core::recti absRect(
        position_.X + gem.bounds.UpperLeftCorner.X - 1,
        position_.Y + gem.bounds.UpperLeftCorner.Y - 1,
        position_.X + gem.bounds.LowerRightCorner.X + 1,
        position_.Y + gem.bounds.LowerRightCorner.Y + 1
    );

    driver->draw2DRectangleOutline(absRect, irr::video::SColor(255, 150, 150, 255));

    // Inner glow
    irr::core::recti innerRect(
        position_.X + gem.bounds.UpperLeftCorner.X + 1,
        position_.Y + gem.bounds.UpperLeftCorner.Y + 1,
        position_.X + gem.bounds.LowerRightCorner.X - 1,
        position_.Y + gem.bounds.LowerRightCorner.Y - 1
    );
    driver->draw2DRectangleOutline(innerRect, irr::video::SColor(150, 100, 100, 255));
}

void SpellGemPanel::drawSpellbookButton(irr::video::IVideoDriver* driver)
{
    irr::core::recti absRect(
        position_.X + spellbookButtonBounds_.UpperLeftCorner.X,
        position_.Y + spellbookButtonBounds_.UpperLeftCorner.Y,
        position_.X + spellbookButtonBounds_.LowerRightCorner.X,
        position_.Y + spellbookButtonBounds_.LowerRightCorner.Y
    );

    // Button background
    irr::video::SColor bgColor = spellbookButtonHovered_ ?
        irr::video::SColor(255, 70, 70, 100) :
        irr::video::SColor(200, 40, 40, 60);
    driver->draw2DRectangle(bgColor, absRect);

    // Draw simple book icon (pages with spine)
    int cx = absRect.getCenter().X;
    int cy = absRect.getCenter().Y;
    irr::video::SColor lineColor(255, 180, 180, 200);

    // Book spine (left vertical line)
    driver->draw2DLine(
        irr::core::position2di(cx - 5, cy - 4),
        irr::core::position2di(cx - 5, cy + 4),
        lineColor
    );

    // Pages (3 horizontal lines)
    driver->draw2DLine(
        irr::core::position2di(cx - 4, cy - 3),
        irr::core::position2di(cx + 5, cy - 3),
        lineColor
    );
    driver->draw2DLine(
        irr::core::position2di(cx - 4, cy),
        irr::core::position2di(cx + 5, cy),
        lineColor
    );
    driver->draw2DLine(
        irr::core::position2di(cx - 4, cy + 3),
        irr::core::position2di(cx + 5, cy + 3),
        lineColor
    );

    // Border
    irr::video::SColor borderColor = spellbookButtonHovered_ ?
        irr::video::SColor(255, 200, 200, 255) :
        irr::video::SColor(255, 80, 80, 100);
    driver->draw2DRectangleOutline(absRect, borderColor);
}

bool SpellGemPanel::handleMouseDown(int x, int y, bool leftButton, bool shift)
{
    if (!visible_) {
        return false;
    }

    // Check spellbook button click first
    irr::core::recti spellbookAbs(
        position_.X + spellbookButtonBounds_.UpperLeftCorner.X,
        position_.Y + spellbookButtonBounds_.UpperLeftCorner.Y,
        position_.X + spellbookButtonBounds_.LowerRightCorner.X,
        position_.Y + spellbookButtonBounds_.LowerRightCorner.Y
    );

    if (spellbookAbs.isPointInside(irr::core::vector2di(x, y))) {
        if (leftButton && spellbookCallback_) {
            spellbookCallback_();
        }
        return true;
    }

    int gemIndex = getGemAtPosition(x, y);
    if (gemIndex < 0) {
        return false;
    }

    if (leftButton) {
        // Check if there's a spell on cursor - memorize it to this gem
        if (getSpellCursorCallback_) {
            uint32_t cursorSpell = getSpellCursorCallback_();
            if (cursorSpell != EQ::SPELL_UNKNOWN && cursorSpell != 0xFFFFFFFF && cursorSpell != 0) {
                // Memorize the cursor spell into this gem slot
                if (memorizeCallback_) {
                    memorizeCallback_(cursorSpell, static_cast<uint8_t>(gemIndex));
                }
                // Clear the cursor
                if (clearSpellCursorCallback_) {
                    clearSpellCursorCallback_();
                }
                return true;
            }
        }

        // Shift+left-click to forget/un-memorize spell
        if (shift) {
            if (forgetCallback_) {
                forgetCallback_(static_cast<uint8_t>(gemIndex));
            }
            return true;
        }

        // Check if gem is clickable (not on cooldown or empty)
        if (spellMgr_) {
            EQ::GemState state = spellMgr_->getGemState(static_cast<uint8_t>(gemIndex));
            if (state == EQ::GemState::Refresh || state == EQ::GemState::Empty ||
                state == EQ::GemState::MemorizeProgress) {
                // Gem is on cooldown, empty, or memorizing - don't cast
                return true;  // Still consume the click
            }
        }
        // Left-click to cast
        castGem(static_cast<uint8_t>(gemIndex));
        return true;
    }

    return true;
}

bool SpellGemPanel::handleMouseUp(int x, int y, bool leftButton)
{
    return false;
}

bool SpellGemPanel::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return false;
    }

    // Update spellbook button hover state
    irr::core::recti spellbookAbs(
        position_.X + spellbookButtonBounds_.UpperLeftCorner.X,
        position_.Y + spellbookButtonBounds_.UpperLeftCorner.Y,
        position_.X + spellbookButtonBounds_.LowerRightCorner.X,
        position_.Y + spellbookButtonBounds_.LowerRightCorner.Y
    );
    spellbookButtonHovered_ = spellbookAbs.isPointInside(irr::core::vector2di(x, y));

    int newHoveredGem = getGemAtPosition(x, y);

    if (newHoveredGem != hoveredGem_) {
        // Clear old hover
        if (hoveredGem_ >= 0 && hoveredGem_ < EQ::MAX_SPELL_GEMS) {
            gems_[hoveredGem_].isHovered = false;
        }

        // Set new hover
        if (newHoveredGem >= 0 && newHoveredGem < EQ::MAX_SPELL_GEMS) {
            gems_[newHoveredGem].isHovered = true;

            // Trigger hover callback
            if (hoverCallback_ && spellMgr_) {
                uint32_t spellId = spellMgr_->getMemorizedSpell(static_cast<uint8_t>(newHoveredGem));
                if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
                    hoverCallback_(static_cast<uint8_t>(newHoveredGem), spellId, x, y);
                }
            }
        } else if (hoveredGem_ >= 0) {
            // Left the panel
            if (hoverEndCallback_) {
                hoverEndCallback_();
            }
        }

        hoveredGem_ = newHoveredGem;
    }

    return containsPoint(x, y);
}

bool SpellGemPanel::handleRightClick(int x, int y)
{
    if (!visible_) {
        return false;
    }

    int gemIndex = getGemAtPosition(x, y);
    if (gemIndex < 0) {
        return false;
    }

    // Right-click to forget spell
    if (forgetCallback_) {
        forgetCallback_(static_cast<uint8_t>(gemIndex));
    }

    return true;
}

bool SpellGemPanel::handleKeyPress(int keyCode)
{
    if (!visible_) {
        return false;
    }

    // Handle 1-8 keys for casting
    // Irrlicht key codes: KEY_KEY_1 = 0x31, KEY_KEY_8 = 0x38
    if (keyCode >= 0x31 && keyCode <= 0x38) {
        uint8_t gemSlot = static_cast<uint8_t>(keyCode - 0x31);

        // Check if gem is castable (not on cooldown or empty)
        if (spellMgr_) {
            EQ::GemState state = spellMgr_->getGemState(gemSlot);
            if (state == EQ::GemState::Refresh || state == EQ::GemState::Empty ||
                state == EQ::GemState::MemorizeProgress) {
                // Gem is on cooldown, empty, or memorizing - don't cast
                return true;  // Still consume the key
            }
        }

        castGem(gemSlot);
        return true;
    }

    return false;
}

void SpellGemPanel::castGem(uint8_t gemSlot)
{
    if (gemSlot >= EQ::MAX_SPELL_GEMS) {
        return;
    }

    if (castCallback_) {
        castCallback_(gemSlot);
    }
}

} // namespace ui
} // namespace eqt
