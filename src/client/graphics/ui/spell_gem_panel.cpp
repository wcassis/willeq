#include "client/graphics/ui/spell_gem_panel.h"
#include "client/graphics/ui/item_icon_loader.h"
#include "client/graphics/ui/ui_settings.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <set>

namespace eqt {
namespace ui {

SpellGemPanel::~SpellGemPanel()
{
    if (contentRT_ && cachedDriver_) {
        cachedDriver_->removeTexture(contentRT_);
        contentRT_ = nullptr;
    }
}

void SpellGemPanel::setGemData(uint8_t slot, const CachedGemData& data) {
    if (slot >= EQ::MAX_SPELL_GEMS) return;
    gemData_[slot] = data;
    contentDirty_ = true;
}

SpellGemPanel::SpellGemPanel(EQ::SpellManager* /* spellMgr - D20b4: no longer stored */, ItemIconLoader* iconLoader)
    : iconLoader_(iconLoader)
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
    // Height: top padding + gems with spacing + button margin + button + bottom padding
    int height = PANEL_PADDING + EQ::MAX_SPELL_GEMS * (GEM_HEIGHT + GEM_SPACING) +
                 SPELLBOOK_BUTTON_MARGIN + SPELLBOOK_BUTTON_SIZE + PANEL_PADDING;
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

void SpellGemPanel::ensureContentRT(irr::video::IVideoDriver* driver)
{
    // RTT caching only works correctly on hardware-accelerated drivers
    if (driver->getDriverType() != irr::video::EDT_OPENGL
#ifdef _IRR_COMPILE_WITH_OGLES2_
        && driver->getDriverType() != irr::video::EDT_OGLES2
#endif
        ) return;

    irr::core::recti panelBounds = getBounds();
    int w = panelBounds.getWidth();
    int h = panelBounds.getHeight();

    if (contentRT_ && contentRTWidth_ == w && contentRTHeight_ == h) {
        return;
    }

    if (contentRT_) {
        driver->removeTexture(contentRT_);
        contentRT_ = nullptr;
    }

    contentRT_ = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(w, h), "SpellGemCache",
        irr::video::ECF_A8R8G8B8);

    if (contentRT_) {
        contentRTWidth_ = w;
        contentRTHeight_ = h;
        contentDirty_ = true;
        LOG_DEBUG(MOD_UI, "SpellGemPanel: Created content cache RTT {}x{}", w, h);
    }

    cachedDriver_ = driver;
}

void SpellGemPanel::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_ || !driver) {
        return;
    }

    ensureContentRT(driver);

    if (contentRT_) {
        // RTT path: check dirty, render to texture, blit

        // Check dirty: spell IDs, gem states (transitions only), hover state
        if (!contentDirty_) {
            for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
                uint32_t spellId = gemData_[i].spellId;
                int gemState = static_cast<int>(gemData_[i].gemState);
                if (spellId != lastSpellIds_[i] || gemState != lastGemStates_[i]) {
                    contentDirty_ = true;
                    break;
                }
            }
        }
        if (!contentDirty_) {
            if (hoveredGem_ != lastHoveredGem_ ||
                spellbookButtonHovered_ != lastSpellbookHovered_ ||
                (hovered_ || dragging_) != lastPanelHovered_) {
                contentDirty_ = true;
            }
        }

        if (contentDirty_) {
            // Save real position and shift to origin for RTT rendering
            auto savedPosition = position_;
            position_ = irr::core::position2di(0, 0);

            if (!driver->setRenderTarget(contentRT_, true, true,
                                         irr::video::SColor(0, 0, 0, 0))) {
                // RTT not supported - destroy and fall back to direct rendering
                position_ = savedPosition;
                driver->removeTexture(contentRT_);
                contentRT_ = nullptr;
            } else {
                // Save state for dirty detection
                {
                    for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
                        lastSpellIds_[i] = gemData_[i].spellId;
                        lastGemStates_[i] = static_cast<int>(gemData_[i].gemState);
                    }
                }
                lastHoveredGem_ = hoveredGem_;
                lastSpellbookHovered_ = spellbookButtonHovered_;
                lastPanelHovered_ = (hovered_ || dragging_);

                // Draw panel background at origin
                irr::core::recti localBounds(0, 0, contentRTWidth_, contentRTHeight_);
                driver->draw2DRectangle(irr::video::SColor(200, 20, 20, 30), localBounds);
                driver->draw2DRectangleOutline(localBounds, irr::video::SColor(255, 60, 60, 80));

                // Draw unlock highlight (clipped to RTT bounds to avoid negative coords)
                bool canMove = !UISettings::instance().isUILocked();
                if (canMove && (hovered_ || dragging_)) {
                    irr::video::SColor highlightColor(200, 255, 200, 0);
                    const int hw = 2;
                    // Draw inside the bounds instead of outside (RTT can't render at negative coords)
                    driver->draw2DRectangle(highlightColor,
                        irr::core::recti(0, 0, localBounds.LowerRightCorner.X, hw));
                    driver->draw2DRectangle(highlightColor,
                        irr::core::recti(0, localBounds.LowerRightCorner.Y - hw,
                                        localBounds.LowerRightCorner.X, localBounds.LowerRightCorner.Y));
                    driver->draw2DRectangle(highlightColor,
                        irr::core::recti(0, 0, hw, localBounds.LowerRightCorner.Y));
                    driver->draw2DRectangle(highlightColor,
                        irr::core::recti(localBounds.LowerRightCorner.X - hw, 0,
                                        localBounds.LowerRightCorner.X, localBounds.LowerRightCorner.Y));
                }

                // Draw each gem base (bg, border, icon, number)
                for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
                    drawGemBase(driver, gui, static_cast<uint8_t>(i), gems_[i]);
                }

                // Draw spellbook button
                drawSpellbookButton(driver);

                driver->setRenderTarget(nullptr, false, false);
                position_ = savedPosition;
                contentDirty_ = false;
            }
        }
    }

    if (contentRT_) {
        // Blit cached content to screen
        irr::core::recti panelBounds = getBounds();
        irr::core::recti srcRect(0, 0, contentRTWidth_, contentRTHeight_);
        driver->draw2DImage(contentRT_, panelBounds, srcRect, nullptr, nullptr, true);

        // Draw dynamic overlays at screen coordinates
        renderDynamicOverlays(driver);
    } else {
        // Direct rendering fallback (no RTT support)
        irr::core::recti panelBounds = getBounds();
        driver->draw2DRectangle(irr::video::SColor(200, 20, 20, 30), panelBounds);
        driver->draw2DRectangleOutline(panelBounds, irr::video::SColor(255, 60, 60, 80));

        bool canMove = !UISettings::instance().isUILocked();
        if (canMove && (hovered_ || dragging_)) {
            irr::video::SColor highlightColor(200, 255, 200, 0);
            const int hw = 2;
            driver->draw2DRectangle(highlightColor,
                irr::core::recti(panelBounds.UpperLeftCorner.X - hw, panelBounds.UpperLeftCorner.Y - hw,
                                panelBounds.LowerRightCorner.X + hw, panelBounds.UpperLeftCorner.Y));
            driver->draw2DRectangle(highlightColor,
                irr::core::recti(panelBounds.UpperLeftCorner.X - hw, panelBounds.LowerRightCorner.Y,
                                panelBounds.LowerRightCorner.X + hw, panelBounds.LowerRightCorner.Y + hw));
            driver->draw2DRectangle(highlightColor,
                irr::core::recti(panelBounds.UpperLeftCorner.X - hw, panelBounds.UpperLeftCorner.Y,
                                panelBounds.UpperLeftCorner.X, panelBounds.LowerRightCorner.Y));
            driver->draw2DRectangle(highlightColor,
                irr::core::recti(panelBounds.LowerRightCorner.X, panelBounds.UpperLeftCorner.Y,
                                panelBounds.LowerRightCorner.X + hw, panelBounds.LowerRightCorner.Y));
        }

        for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
            drawGemBase(driver, gui, static_cast<uint8_t>(i), gems_[i]);
        }
        drawSpellbookButton(driver);
        renderDynamicOverlays(driver);
    }
}

void SpellGemPanel::renderDynamicOverlays(irr::video::IVideoDriver* driver)
{
    for (int i = 0; i < EQ::MAX_SPELL_GEMS; i++) {
        auto gemState = static_cast<EQ::GemState>(gemData_[i].gemState);

        if (gemState == EQ::GemState::Refresh) {
            float progress = 0.0f;
            if (gemData_[i].cooldownTotalMs > 0 && currentTimeMs_ < gemData_[i].cooldownEndMs) {
                uint32_t remaining = gemData_[i].cooldownEndMs - currentTimeMs_;
                progress = static_cast<float>(remaining) / gemData_[i].cooldownTotalMs;
            }
            drawCooldownOverlay(driver, gems_[i], progress);
        }

        if (gemState == EQ::GemState::MemorizeProgress) {
            float progress = 0.0f;
            if (gemData_[i].memorizeTotalMs > 0 && currentTimeMs_ < gemData_[i].memorizeEndMs) {
                uint32_t remaining = gemData_[i].memorizeEndMs - currentTimeMs_;
                progress = 1.0f - (static_cast<float>(remaining) / gemData_[i].memorizeTotalMs);
            }
            drawMemorizeProgress(driver, gems_[i], progress);
        }

        if (gemState == EQ::GemState::Casting) {
            drawCastingHighlight(driver, gems_[i]);
        }
    }
}

void SpellGemPanel::drawGemBase(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                                uint8_t slot, const GemSlotLayout& gem)
{
    uint32_t spellId = gemData_[slot].spellId;
    auto state = static_cast<EQ::GemState>(gemData_[slot].gemState);

    // Calculate absolute position (using current position_, which is origin during RTT render)
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

    // Draw spell icon if memorized (D20b4: use cached icon ID)
    if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
        uint32_t iconId = gemData_[slot].iconId;
        if (iconId != 0 && iconLoader_) {
            irr::video::ITexture* icon = iconLoader_->getIcon(iconId);
            if (!icon) {
                static std::set<uint32_t> loggedMissingIcons;
                if (loggedMissingIcons.find(iconId) == loggedMissingIcons.end()) {
                    LOG_WARN(MOD_UI, "SpellGem slot {} spell '{}' (ID {}) - icon not found for iconId {}",
                             slot, gemData_[slot].spellName, spellId, iconId);
                    loggedMissingIcons.insert(iconId);
                }
            }
            if (icon) {
                irr::core::dimension2du iconSize = icon->getOriginalSize();
                static std::set<uint8_t> loggedRender;
                if (loggedRender.find(slot) == loggedRender.end()) {
                    LOG_DEBUG(MOD_UI, "SpellGem slot {} drawing icon {}x{} to rect ({},{} - {},{})",
                             slot, iconSize.Width, iconSize.Height,
                             absIconRect.UpperLeftCorner.X, absIconRect.UpperLeftCorner.Y,
                             absIconRect.LowerRightCorner.X, absIconRect.LowerRightCorner.Y);
                    loggedRender.insert(slot);
                }
                irr::video::SColor colors[4] = {
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::video::SColor(255, 255, 255, 255)
                };
                driver->draw2DImage(icon,
                    absIconRect,
                    irr::core::recti(0, 0, iconSize.Width, iconSize.Height),
                    nullptr, colors, true);
            }
        }
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

bool SpellGemPanel::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check if UI is unlocked - allow dragging the entire panel
    bool canMove = !UISettings::instance().isUILocked();
    if (canMove && leftButton) {
        dragging_ = true;
        dragOffset_.X = x - position_.X;
        dragOffset_.Y = y - position_.Y;
        return true;
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

    LOG_DEBUG(MOD_UI, "SpellGemPanel: Click on gem {} (left={} shift={} ctrl={})",
        gemIndex + 1, leftButton, shift, ctrl);

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

        // Ctrl+click to pickup spell for hotbar
        if (ctrl && !shift) {
            if (hotbarPickupCallback_) {
                uint32_t spellId = gemData_[gemIndex].spellId;
                if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
                    hotbarPickupCallback_(HotbarButtonType::Spell, spellId,
                                         "", gemData_[gemIndex].iconId);
                }
            }
            return true;
        }

        // Shift+left-click to forget/un-memorize spell
        if (shift) {
            if (forgetCallback_) {
                forgetCallback_(static_cast<uint8_t>(gemIndex));
            }
            return true;
        }

        // Check if gem is clickable (not on cooldown or empty)
        {
            auto state = static_cast<EQ::GemState>(gemData_[gemIndex].gemState);
            LOG_DEBUG(MOD_UI, "SpellGemPanel: gem {} state={}",
                gemIndex + 1, static_cast<int>(state));
            if (state == EQ::GemState::Refresh || state == EQ::GemState::Empty ||
                state == EQ::GemState::MemorizeProgress) {
                // Gem is on cooldown, empty, or memorizing - don't cast
                LOG_DEBUG(MOD_UI, "SpellGem {} click blocked: state={}", gemIndex + 1,
                    state == EQ::GemState::Refresh ? "Refresh" :
                    state == EQ::GemState::Empty ? "Empty" : "MemorizeProgress");
                return true;  // Still consume the click
            }
            LOG_DEBUG(MOD_UI, "SpellGem {} clicked: proceeding to castGem", gemIndex + 1);
        }
        // Left-click to cast
        castGem(static_cast<uint8_t>(gemIndex));
        return true;
    }

    return true;
}

bool SpellGemPanel::handleMouseUp(int x, int y, bool leftButton)
{
    if (dragging_ && leftButton) {
        dragging_ = false;
        LOG_INFO(MOD_UI, "SpellGemPanel moved to position ({}, {})", position_.X, position_.Y);
        return true;
    }
    return false;
}

bool SpellGemPanel::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return false;
    }

    // Handle dragging when UI is unlocked
    if (dragging_) {
        int newX = x - dragOffset_.X;
        int newY = y - dragOffset_.Y;
        setPosition(newX, newY);
        return true;
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
            if (hoverCallback_) {
                uint32_t spellId = gemData_[newHoveredGem].spellId;
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
    // Key handling for spell gems is now done through HotkeyManager in irrlicht_renderer
    // HotbarSlot1-8 (1-8 keys) and SpellGem1-8 (Alt+1-8) are configured in hotkeys.json
    (void)keyCode;  // Unused
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
