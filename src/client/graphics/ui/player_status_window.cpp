#include "client/graphics/ui/player_status_window.h"
#include "client/eq.h"
#include "client/combat.h"
#include "common/name_utils.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>

namespace eqt {
namespace ui {

PlayerStatusWindow::PlayerStatusWindow()
    : WindowBase(L"", 90, 70)  // Empty title (no title bar), taller for target info
{
    // Initialize layout constants from UISettings
    const auto& settings = UISettings::instance().playerStatus();
    NAME_HEIGHT = settings.nameHeight;
    BAR_HEIGHT = settings.barHeight;
    BAR_SPACING = settings.barSpacing;
    BAR_LABEL_WIDTH = settings.barLabelWidth;
    PADDING = settings.padding;

    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();

    // Calculate window size based on layout:
    // Player: Name + 3 bars (HP, Mana, Stamina) with spacing
    // Target: Name + 2 bars (HP, Mana) + spell name with spacing
    int playerHeight = NAME_HEIGHT + BAR_SPACING +
                       BAR_HEIGHT + BAR_SPACING +
                       BAR_HEIGHT + BAR_SPACING +
                       BAR_HEIGHT;
    int targetHeight = BAR_SPACING +  // Gap between player and target
                       NAME_HEIGHT + BAR_SPACING +
                       BAR_HEIGHT + BAR_SPACING +
                       BAR_HEIGHT + BAR_SPACING +
                       NAME_HEIGHT;  // Spell name line
    int contentHeight = playerHeight + targetHeight;

    int width = settings.window.width;
    int height = contentHeight + PADDING * 2 + (borderWidth + contentPadding) * 2;

    setSize(width, height);
    setPosition(settings.window.x, settings.window.y);
    setShowTitleBar(false);
    setAlwaysLocked(settings.window.alwaysLocked);

    if (settings.window.visible) {
        show();
    }
}

void PlayerStatusWindow::update()
{
    if (!eq_) {
        return;
    }

    // Get player name
    std::string firstName = eq_->GetMyName();
    std::string lastName = eq_->GetMyLastName();

    // Build full name
    std::wstring fullName;
    for (char c : firstName) {
        fullName += static_cast<wchar_t>(c);
    }
    if (!lastName.empty()) {
        fullName += L" ";
        for (char c : lastName) {
            fullName += static_cast<wchar_t>(c);
        }
    }

    // Invalidate cache if player name changed
    if (playerName_ != fullName) {
        playerName_ = fullName;
        cachedContentWidth_ = 0;  // Force recalculation
    }

    // Get vital stats
    currentHP_ = eq_->GetCurrentHP();
    maxHP_ = eq_->GetMaxHP();
    currentMana_ = eq_->GetCurrentMana();
    maxMana_ = eq_->GetMaxMana();
    currentStamina_ = eq_->GetCurrentEndurance();
    maxStamina_ = eq_->GetMaxEndurance();

    // Get target info
    CombatManager* combat = eq_->GetCombatManager();
    if (combat && combat->HasTarget()) {
        uint16_t targetId = combat->GetTargetId();
        const auto& entities = eq_->GetEntities();
        auto it = entities.find(targetId);
        if (it != entities.end()) {
            hasTarget_ = true;
            const Entity& target = it->second;

            // Convert server name to display name
            std::wstring newTargetName = EQT::toDisplayNameW(target.name);

            // Invalidate cache if target name changed
            if (targetName_ != newTargetName) {
                targetName_ = newTargetName;
                cachedContentWidth_ = 0;  // Force recalculation
            }

            targetHpPercent_ = target.hp_percent;
            targetCurrentMana_ = target.cur_mana;
            targetMaxMana_ = target.max_mana;
        } else {
            if (hasTarget_ || !targetName_.empty()) {
                cachedContentWidth_ = 0;  // Force recalculation
            }
            hasTarget_ = false;
            targetName_.clear();
            targetHpPercent_ = 0;
            targetCurrentMana_ = 0;
            targetMaxMana_ = 0;
            targetCastingSpell_.clear();
        }
    } else {
        if (hasTarget_ || !targetName_.empty()) {
            cachedContentWidth_ = 0;  // Force recalculation
        }
        hasTarget_ = false;
        targetName_.clear();
        targetHpPercent_ = 0;
        targetCurrentMana_ = 0;
        targetMaxMana_ = 0;
        targetCastingSpell_.clear();
    }
}

void PlayerStatusWindow::setTargetCastingSpell(const std::string& spellName)
{
    std::wstring newSpell;
    for (char c : spellName) {
        newSpell += static_cast<wchar_t>(c);
    }

    // Invalidate cache if spell name changed
    if (targetCastingSpell_ != newSpell) {
        targetCastingSpell_ = newSpell;
        cachedContentWidth_ = 0;  // Force recalculation
    }
}

void PlayerStatusWindow::clearTargetCastingSpell()
{
    if (!targetCastingSpell_.empty()) {
        targetCastingSpell_.clear();
        cachedContentWidth_ = 0;  // Force recalculation
    }
}

void PlayerStatusWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) {
        return;
    }

    // Draw window base (background, border)
    WindowBase::render(driver, gui);

    // Draw animated combat border when auto-attack is enabled
    CombatManager* combat = eq_ ? eq_->GetCombatManager() : nullptr;
    if (combat && combat->IsAutoAttackEnabled()) {
        auto now = std::chrono::steady_clock::now();
        uint32_t currentTimeMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        drawCombatBorder(driver, currentTimeMs);
    }
}

void PlayerStatusWindow::updateDisplayNames(irr::gui::IGUIFont* font, int contentWidth)
{
    // Check if we need to recalculate (font, width, or source names changed)
    bool needsUpdate = (font != cachedFont_) ||
                       (contentWidth != cachedContentWidth_);

    if (needsUpdate) {
        cachedFont_ = font;
        cachedContentWidth_ = contentWidth;
        // Recalculate all display names
        displayPlayerName_ = truncateText(font, playerName_, contentWidth);
        displayTargetName_ = truncateText(font, targetName_, contentWidth);
        displayCastingSpell_ = truncateText(font, targetCastingSpell_, contentWidth);
    }
}

void PlayerStatusWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!driver) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;
    int contentWidth = contentArea.getWidth();

    // Update cached display names if needed
    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;
    if (font) {
        updateDisplayNames(font, contentWidth);
    }

    int y = contentY + PADDING;

    // Draw player name (using cached truncated name)
    if (gui && font) {
        irr::core::recti nameBounds(
            contentX, y,
            contentX + contentWidth, y + NAME_HEIGHT
        );
        font->draw(displayPlayerName_.c_str(), nameBounds, getNameTextColor());
    }
    y += NAME_HEIGHT + BAR_SPACING;

    // Draw HP bar with text overlay
    irr::core::recti hpBounds(
        contentX, y,
        contentX + contentWidth, y + BAR_HEIGHT
    );
    drawStatBar(driver, gui, hpBounds, currentHP_, maxHP_, getHpBackground(), getHpFill());
    drawBarText(gui, hpBounds, currentHP_, maxHP_);
    y += BAR_HEIGHT + BAR_SPACING;

    // Draw Mana bar with text overlay
    irr::core::recti manaBounds(
        contentX, y,
        contentX + contentWidth, y + BAR_HEIGHT
    );
    drawStatBar(driver, gui, manaBounds, currentMana_, maxMana_, getManaBackground(), getManaFill());
    drawBarText(gui, manaBounds, currentMana_, maxMana_);
    y += BAR_HEIGHT + BAR_SPACING;

    // Draw Stamina bar with text overlay
    irr::core::recti staminaBounds(
        contentX, y,
        contentX + contentWidth, y + BAR_HEIGHT
    );
    drawStatBar(driver, gui, staminaBounds, currentStamina_, maxStamina_, getStaminaBackground(), getStaminaFill());
    drawBarText(gui, staminaBounds, currentStamina_, maxStamina_);
    y += BAR_HEIGHT + BAR_SPACING;

    // --- Target Section ---
    y += BAR_SPACING;  // Extra gap before target

    // Draw target name (or "No Target"), using cached truncated name
    if (gui && font) {
        irr::core::recti nameBounds(
            contentX, y,
            contentX + contentWidth, y + NAME_HEIGHT
        );
        if (hasTarget_) {
            font->draw(displayTargetName_.c_str(), nameBounds, getNameTextColor());
        } else {
            font->draw(L"No Target", nameBounds, irr::video::SColor(150, 128, 128, 128));
        }
    }
    y += NAME_HEIGHT + BAR_SPACING;

    // Draw target HP bar (percent-based) with text overlay
    irr::core::recti targetHpBounds(
        contentX, y,
        contentX + contentWidth, y + BAR_HEIGHT
    );
    if (hasTarget_) {
        drawPercentBar(driver, targetHpBounds, targetHpPercent_, getHpBackground(), getHpFill());
        drawPercentBarText(gui, targetHpBounds, targetHpPercent_);
    } else {
        drawPercentBar(driver, targetHpBounds, 0, getHpBackground(), getHpFill());
    }
    y += BAR_HEIGHT + BAR_SPACING;

    // Draw target Mana bar with text overlay (only if target has mana)
    if (hasTarget_ && targetMaxMana_ > 0) {
        irr::core::recti targetManaBounds(
            contentX, y,
            contentX + contentWidth, y + BAR_HEIGHT
        );
        drawStatBar(driver, gui, targetManaBounds, targetCurrentMana_, targetMaxMana_,
                   getManaBackground(), getManaFill());
        drawBarText(gui, targetManaBounds, targetCurrentMana_, targetMaxMana_);
        y += BAR_HEIGHT + BAR_SPACING;
    }

    // Draw target casting spell name, using cached truncated name
    if (gui && font && !targetCastingSpell_.empty()) {
        irr::core::recti spellBounds(
            contentX, y,
            contentX + contentWidth, y + NAME_HEIGHT
        );
        font->draw(displayCastingSpell_.c_str(), spellBounds, irr::video::SColor(255, 200, 200, 255));
    }
}

void PlayerStatusWindow::drawStatBar(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* /* gui */,
                                     const irr::core::recti& bounds,
                                     uint32_t current, uint32_t max,
                                     const irr::video::SColor& bgColor,
                                     const irr::video::SColor& fillColor)
{
    // Draw background
    driver->draw2DRectangle(bgColor, bounds);

    // Draw fill based on percentage
    if (max > 0 && current > 0) {
        uint32_t safeCurrent = (current > max) ? max : current;
        int fillWidth = (bounds.getWidth() * safeCurrent) / max;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );
        driver->draw2DRectangle(fillColor, fillBounds);
    }
}

void PlayerStatusWindow::drawBarText(irr::gui::IGUIEnvironment* gui,
                                     const irr::core::recti& bounds,
                                     uint32_t current, uint32_t max)
{
    if (!gui) return;

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    std::wstringstream wss;
    wss << current << L"/" << max;
    std::wstring text = wss.str();

    // Left-align text in bar, vertically centered
    font->draw(text.c_str(), bounds, getBarTextColor(), false, true);
}

void PlayerStatusWindow::drawPercentBar(irr::video::IVideoDriver* driver,
                                        const irr::core::recti& bounds,
                                        uint8_t percent,
                                        const irr::video::SColor& bgColor,
                                        const irr::video::SColor& fillColor)
{
    // Draw background
    driver->draw2DRectangle(bgColor, bounds);

    // Draw fill based on percentage
    if (percent > 0) {
        uint8_t safePercent = (percent > 100) ? 100 : percent;
        int fillWidth = (bounds.getWidth() * safePercent) / 100;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );
        driver->draw2DRectangle(fillColor, fillBounds);
    }
}

void PlayerStatusWindow::drawPercentBarText(irr::gui::IGUIEnvironment* gui,
                                            const irr::core::recti& bounds,
                                            uint8_t percent)
{
    if (!gui) return;

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    std::wstringstream wss;
    wss << static_cast<int>(percent) << L"%";
    std::wstring text = wss.str();

    // Left-align text in bar, vertically centered
    font->draw(text.c_str(), bounds, getBarTextColor(), false, true);
}

std::wstring PlayerStatusWindow::truncateText(irr::gui::IGUIFont* font,
                                              const std::wstring& text,
                                              int maxWidth) const
{
    if (!font || text.empty() || maxWidth <= 0) {
        return text;
    }

    // Check if text already fits
    irr::core::dimension2du dim = font->getDimension(text.c_str());
    if (static_cast<int>(dim.Width) <= maxWidth) {
        return text;
    }

    // Need to truncate - measure ellipsis width
    const std::wstring ellipsis = L"...";
    irr::core::dimension2du ellipsisDim = font->getDimension(ellipsis.c_str());
    int ellipsisWidth = static_cast<int>(ellipsisDim.Width);

    // If ellipsis alone doesn't fit, return empty or just ellipsis
    if (ellipsisWidth >= maxWidth) {
        return ellipsis;
    }

    // Binary search for the right truncation point
    int availableWidth = maxWidth - ellipsisWidth;
    size_t left = 0;
    size_t right = text.length();
    size_t bestFit = 0;

    while (left <= right && right > 0) {
        size_t mid = (left + right) / 2;
        std::wstring truncated = text.substr(0, mid);
        irr::core::dimension2du truncDim = font->getDimension(truncated.c_str());

        if (static_cast<int>(truncDim.Width) <= availableWidth) {
            bestFit = mid;
            left = mid + 1;
        } else {
            if (mid == 0) break;
            right = mid - 1;
        }
    }

    // Return truncated text with ellipsis
    if (bestFit > 0) {
        return text.substr(0, bestFit) + ellipsis;
    }
    return ellipsis;
}

void PlayerStatusWindow::drawCombatBorder(irr::video::IVideoDriver* driver, uint32_t currentTimeMs)
{
    if (!driver) return;

    // Update animation offset based on time elapsed
    // Animation completes one full cycle every 1000ms
    const float animationSpeed = 0.001f;  // cycles per millisecond
    if (lastAnimationTime_ == 0) {
        lastAnimationTime_ = currentTimeMs;
    }
    uint32_t deltaMs = currentTimeMs - lastAnimationTime_;
    lastAnimationTime_ = currentTimeMs;
    animationOffset_ += deltaMs * animationSpeed;
    if (animationOffset_ >= 1.0f) {
        animationOffset_ -= 1.0f;
    }

    // Get window bounds
    irr::core::recti bounds = getBounds();
    const int borderWidth = 3;  // Width of combat border

    // Calculate total perimeter for gradient calculation
    // Top and bottom edges are extended by borderWidth on each side
    int width = bounds.getWidth();
    int height = bounds.getHeight();
    int extendedWidth = width + 2 * borderWidth;
    int perimeter = 2 * extendedWidth + 2 * height;

    // Draw border segments with scrolling gradient
    // The gradient creates alternating light and dark red bands that scroll

    auto getColorAtPosition = [this, perimeter](int position) -> irr::video::SColor {
        // Normalize position to 0.0 - 1.0
        float normalizedPos = static_cast<float>(position) / perimeter;

        // Add animation offset and wrap
        normalizedPos += animationOffset_;
        if (normalizedPos >= 1.0f) normalizedPos -= 1.0f;

        // Create a wave pattern with 4 cycles around the perimeter
        float wave = std::sin(normalizedPos * 4.0f * 3.14159f * 2.0f);

        // Map wave (-1 to 1) to color intensity (100 to 255 for red channel)
        int redIntensity = static_cast<int>(177.5f + 77.5f * wave);  // 100-255 range
        int otherIntensity = static_cast<int>(30.0f + 20.0f * wave);  // 10-50 range for some color variation

        return irr::video::SColor(255, redIntensity, otherIntensity, otherIntensity);
    };

    // Draw border as individual pixels/small segments for smooth gradient
    // Top and bottom edges extend to fill corners

    int position = 0;

    // Top edge (left to right, extended to fill corners)
    for (int x = bounds.UpperLeftCorner.X - borderWidth; x < bounds.LowerRightCorner.X + borderWidth; x++) {
        irr::video::SColor color = getColorAtPosition(position++);
        driver->draw2DRectangle(color,
            irr::core::recti(x, bounds.UpperLeftCorner.Y - borderWidth,
                            x + 1, bounds.UpperLeftCorner.Y));
    }

    // Right edge (top to bottom)
    for (int y = bounds.UpperLeftCorner.Y; y < bounds.LowerRightCorner.Y; y++) {
        irr::video::SColor color = getColorAtPosition(position++);
        driver->draw2DRectangle(color,
            irr::core::recti(bounds.LowerRightCorner.X, y,
                            bounds.LowerRightCorner.X + borderWidth, y + 1));
    }

    // Bottom edge (right to left, extended to fill corners)
    for (int x = bounds.LowerRightCorner.X + borderWidth - 1; x >= bounds.UpperLeftCorner.X - borderWidth; x--) {
        irr::video::SColor color = getColorAtPosition(position++);
        driver->draw2DRectangle(color,
            irr::core::recti(x, bounds.LowerRightCorner.Y,
                            x + 1, bounds.LowerRightCorner.Y + borderWidth));
    }

    // Left edge (bottom to top)
    for (int y = bounds.LowerRightCorner.Y - 1; y >= bounds.UpperLeftCorner.Y; y--) {
        irr::video::SColor color = getColorAtPosition(position++);
        driver->draw2DRectangle(color,
            irr::core::recti(bounds.UpperLeftCorner.X - borderWidth, y,
                            bounds.UpperLeftCorner.X, y + 1));
    }
}

} // namespace ui
} // namespace eqt
