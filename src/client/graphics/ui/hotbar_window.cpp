#include "client/graphics/ui/hotbar_window.h"
#include "client/graphics/ui/hotbar_cursor.h"
#include "client/graphics/ui/item_icon_loader.h"
#include <fmt/format.h>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace eqt {
namespace ui {

HotbarWindow::HotbarWindow()
    : WindowBase(L"Hotbar", 100, 50)  // Size calculated in initializeLayout
{
    setShowTitleBar(false);
    initializeLayout();
}

HotbarWindow::~HotbarWindow()
{
    if (contentRT_ && cachedDriver_) {
        cachedDriver_->removeTexture(contentRT_);
        contentRT_ = nullptr;
    }
}

void HotbarWindow::initializeLayout()
{
    const auto& hotbarSettings = UISettings::instance().hotbar();

    int buttonSize = hotbarSettings.buttonSize;
    int buttonSpacing = hotbarSettings.buttonSpacing;
    int padding = hotbarSettings.padding;
    buttonCount_ = std::min(std::max(hotbarSettings.buttonCount, 1), MAX_BUTTONS);

    // Layout buttons horizontally
    for (int i = 0; i < MAX_BUTTONS; i++) {
        int x = padding + i * (buttonSize + buttonSpacing);
        int y = padding;
        buttons_[i].bounds = irr::core::recti(x, y, x + buttonSize, y + buttonSize);
        buttons_[i].type = HotbarButtonType::Empty;
        buttons_[i].id = 0;
        buttons_[i].emoteText.clear();
        buttons_[i].iconId = 0;
        buttons_[i].hovered = false;
        buttons_[i].pressed = false;
    }

    updateWindowSize();
}

void HotbarWindow::updateWindowSize()
{
    int buttonSize = getButtonSize();
    int buttonSpacing = getButtonSpacing();
    int padding = getPadding();
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();

    // Calculate content size (only showing buttonCount_ buttons)
    int contentWidth = padding * 2 + buttonCount_ * buttonSize + (buttonCount_ - 1) * buttonSpacing;
    int contentHeight = padding * 2 + buttonSize;

    // Add borders
    int totalWidth = contentWidth + (borderWidth + contentPadding) * 2;
    int totalHeight = contentHeight + (borderWidth + contentPadding) * 2;

    setSize(totalWidth, totalHeight);
}

int HotbarWindow::getButtonSize() const
{
    return UISettings::instance().hotbar().buttonSize;
}

int HotbarWindow::getButtonSpacing() const
{
    return UISettings::instance().hotbar().buttonSpacing;
}

int HotbarWindow::getPadding() const
{
    return UISettings::instance().hotbar().padding;
}

void HotbarWindow::setButtonCount(int count)
{
    buttonCount_ = std::min(std::max(count, 1), MAX_BUTTONS);
    updateWindowSize();
}

void HotbarWindow::setButton(int index, HotbarButtonType type, uint32_t id,
                              const std::string& emoteText, uint32_t iconId)
{
    if (index < 0 || index >= MAX_BUTTONS) return;

    buttons_[index].type = type;
    buttons_[index].id = id;
    buttons_[index].emoteText = emoteText;
    buttons_[index].iconId = iconId;
    contentDirty_ = true;

    // Notify that hotbar configuration changed
    if (changedCallback_) {
        changedCallback_();
    }
}

void HotbarWindow::clearButton(int index)
{
    if (index < 0 || index >= MAX_BUTTONS) return;

    buttons_[index].type = HotbarButtonType::Empty;
    buttons_[index].id = 0;
    buttons_[index].emoteText.clear();
    buttons_[index].iconId = 0;
    contentDirty_ = true;

    // Notify that hotbar configuration changed
    if (changedCallback_) {
        changedCallback_();
    }
}

const HotbarButton& HotbarWindow::getButton(int index) const
{
    static const HotbarButton emptyButton;
    if (index < 0 || index >= MAX_BUTTONS) return emptyButton;
    return buttons_[index];
}

void HotbarWindow::swapButtons(int indexA, int indexB)
{
    if (indexA < 0 || indexA >= MAX_BUTTONS) return;
    if (indexB < 0 || indexB >= MAX_BUTTONS) return;
    if (indexA == indexB) return;

    std::swap(buttons_[indexA].type, buttons_[indexB].type);
    std::swap(buttons_[indexA].id, buttons_[indexB].id);
    std::swap(buttons_[indexA].emoteText, buttons_[indexB].emoteText);
    std::swap(buttons_[indexA].iconId, buttons_[indexB].iconId);
    contentDirty_ = true;

    // Notify that hotbar configuration changed
    if (changedCallback_) {
        changedCallback_();
    }
}

void HotbarWindow::activateButton(int index)
{
    if (index < 0 || index >= buttonCount_) return;

    const auto& button = buttons_[index];
    if (button.type == HotbarButtonType::Empty) return;

    // Don't activate if on cooldown
    if (button.isOnCooldown()) {
        return;
    }

    if (activateCallback_) {
        activateCallback_(index, button);
    }
}

void HotbarWindow::startCooldown(int index, uint32_t durationMs)
{
    if (index < 0 || index >= MAX_BUTTONS) return;
    if (durationMs == 0) return;

    buttons_[index].cooldownDurationMs = durationMs;
    buttons_[index].cooldownEndTime = std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(durationMs);
}

void HotbarWindow::startSkillCooldown(uint32_t skillId, uint32_t durationMs)
{
    if (durationMs == 0) return;

    // Find all buttons with this skill and start their cooldowns
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (buttons_[i].type == HotbarButtonType::Skill && buttons_[i].id == skillId) {
            buttons_[i].cooldownDurationMs = durationMs;
            buttons_[i].cooldownEndTime = std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds(durationMs);
        }
    }
}

bool HotbarWindow::isButtonOnCooldown(int index) const
{
    if (index < 0 || index >= MAX_BUTTONS) return false;
    return buttons_[index].isOnCooldown();
}

irr::core::recti HotbarWindow::getHotbarContentArea() const
{
    return getContentArea();
}

void HotbarWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Position at bottom center, above chat window
    int x = (screenWidth - getWidth()) / 2;
    int y = screenHeight - getHeight() - 140;  // Above chat
    setPosition(x, y);
}

int HotbarWindow::getButtonAtPosition(int relX, int relY) const
{
    irr::core::vector2di pos(relX, relY);

    for (int i = 0; i < buttonCount_; i++) {
        if (buttons_[i].bounds.isPointInside(pos)) {
            return i;
        }
    }
    return -1;
}

bool HotbarWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_) {
        return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
    }

    // Check if click is within window bounds
    if (!containsPoint(x, y)) {
        return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
    }

    // Get relative position within content area
    irr::core::recti contentArea = getContentArea();
    int relX = x - contentArea.UpperLeftCorner.X;
    int relY = y - contentArea.UpperLeftCorner.Y;

    int buttonIndex = getButtonAtPosition(relX, relY);

    if (buttonIndex >= 0) {
        // Right-click: open emote dialog for empty buttons
        if (!leftButton && !ctrl) {
            if (buttons_[buttonIndex].type == HotbarButtonType::Empty) {
                if (emoteDialogCallback_) {
                    emoteDialogCallback_(buttonIndex);
                }
            }
            return true;
        }

        // Left-click handling
        if (leftButton) {
            // Check if hotbar cursor has an item
            bool hasCursorItem = hotbarCursor_ && hotbarCursor_->hasItem();

            if (ctrl && !hasCursorItem) {
                // Ctrl+click: pickup button to cursor
                if (buttons_[buttonIndex].type != HotbarButtonType::Empty) {
                    if (pickupCallback_) {
                        pickupCallback_(buttonIndex, buttons_[buttonIndex]);
                    }
                }
                return true;
            }

            if (hasCursorItem) {
                // Has cursor item: place or swap
                // This is handled by WindowManager which coordinates with HotbarCursor
                // Just consume the click here, WindowManager does the swap logic
                return true;
            }

            // Normal left-click: activate the button
            if (!ctrl && buttons_[buttonIndex].type != HotbarButtonType::Empty) {
                activateButton(buttonIndex);
            }
            return true;
        }
    }

    // Click not on a button - handle dragging for titlebar-less window
    if (leftButton && canMove()) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
}

bool HotbarWindow::handleMouseUp(int x, int y, bool leftButton)
{
    // Clear any pressed states
    for (auto& button : buttons_) {
        button.pressed = false;
    }

    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool HotbarWindow::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return WindowBase::handleMouseMove(x, y);
    }

    // Clear all hover states first
    for (auto& button : buttons_) {
        button.hovered = false;
    }

    // Check if mouse is in content area
    irr::core::recti contentArea = getContentArea();
    if (contentArea.isPointInside(irr::core::vector2di(x, y))) {
        int relX = x - contentArea.UpperLeftCorner.X;
        int relY = y - contentArea.UpperLeftCorner.Y;

        int buttonIndex = getButtonAtPosition(relX, relY);
        if (buttonIndex >= 0 && buttonIndex < buttonCount_) {
            buttons_[buttonIndex].hovered = true;
        }
    }

    return WindowBase::handleMouseMove(x, y);
}

void HotbarWindow::ensureContentRT(irr::video::IVideoDriver* driver)
{
    // RTT caching only works correctly on OpenGL; software renderer has alpha issues
    if (driver->getDriverType() != irr::video::EDT_OPENGL) return;

    int w = bounds_.getWidth();
    int h = bounds_.getHeight();

    if (contentRT_ && contentRTWidth_ == w && contentRTHeight_ == h) {
        return;
    }

    if (contentRT_) {
        driver->removeTexture(contentRT_);
        contentRT_ = nullptr;
    }

    contentRT_ = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(w, h), "HotbarCache",
        irr::video::ECF_A8R8G8B8);

    if (contentRT_) {
        contentRTWidth_ = w;
        contentRTHeight_ = h;
        contentDirty_ = true;
    }

    cachedDriver_ = driver;
}

void HotbarWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) return;

    ensureContentRT(driver);

    if (!contentRT_) {
        WindowBase::render(driver, gui);
        return;
    }

    // Check dirty: button types/ids/icons/hover
    if (!contentDirty_) {
        for (int i = 0; i < buttonCount_; i++) {
            if (buttons_[i].type != lastButtonTypes_[i] ||
                buttons_[i].id != lastButtonIds_[i] ||
                buttons_[i].iconId != lastButtonIconIds_[i] ||
                buttons_[i].hovered != lastButtonHovered_[i]) {
                contentDirty_ = true;
                break;
            }
        }
    }
    if (!contentDirty_) {
        bool currentHovered = hovered_ || dragging_;
        if (currentHovered != lastWindowHovered_) {
            contentDirty_ = true;
        }
    }

    if (contentDirty_) {
        // Save real bounds and shift to origin
        auto realBounds = bounds_;
        auto realTitleBar = titleBar_;
        int w = bounds_.getWidth();
        int h = bounds_.getHeight();
        bounds_ = irr::core::recti(0, 0, w, h);
        titleBar_ = irr::core::recti(
            realTitleBar.UpperLeftCorner.X - realBounds.UpperLeftCorner.X,
            realTitleBar.UpperLeftCorner.Y - realBounds.UpperLeftCorner.Y,
            realTitleBar.LowerRightCorner.X - realBounds.UpperLeftCorner.X,
            realTitleBar.LowerRightCorner.Y - realBounds.UpperLeftCorner.Y);

        if (!driver->setRenderTarget(contentRT_, true, true,
                                     irr::video::SColor(0, 0, 0, 0))) {
            // RTT not supported - destroy and fall back
            bounds_ = realBounds;
            titleBar_ = realTitleBar;
            driver->removeTexture(contentRT_);
            contentRT_ = nullptr;
        } else {
            // Save dirty detection state
            for (int i = 0; i < MAX_BUTTONS; i++) {
                lastButtonTypes_[i] = buttons_[i].type;
                lastButtonIds_[i] = buttons_[i].id;
                lastButtonIconIds_[i] = buttons_[i].iconId;
                lastButtonHovered_[i] = buttons_[i].hovered;
            }
            lastWindowHovered_ = hovered_ || dragging_;

            drawWindow(driver);
            drawUnlockedHighlight(driver);
            renderContent(driver, gui);

            driver->setRenderTarget(nullptr, false, false);

            bounds_ = realBounds;
            titleBar_ = realTitleBar;
            contentDirty_ = false;
        }
    }

    if (contentRT_) {
        // Blit cached content to screen
        irr::core::recti destRect = bounds_;
        irr::core::recti srcRect(0, 0, contentRTWidth_, contentRTHeight_);
        driver->draw2DImage(contentRT_, destRect, srcRect, nullptr, nullptr, true);

        // Draw cooldown overlays live at screen coordinates
        for (int i = 0; i < buttonCount_; i++) {
            if (buttons_[i].isOnCooldown()) {
                drawButtonCooldown(driver, gui, buttons_[i]);
            }
        }
    } else {
        // Direct rendering fallback
        WindowBase::render(driver, gui);
        // Cooldowns are included in renderContent via drawButtonBase
    }
}

void HotbarWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!driver) return;

    // Draw visible buttons
    for (int i = 0; i < buttonCount_; i++) {
        drawButtonBase(driver, gui, buttons_[i], i);
    }
}

void HotbarWindow::drawButtonBase(irr::video::IVideoDriver* driver,
                                   irr::gui::IGUIEnvironment* gui,
                                   const HotbarButton& button, int index)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Calculate absolute button bounds
    irr::core::recti absBounds(
        contentX + button.bounds.UpperLeftCorner.X,
        contentY + button.bounds.UpperLeftCorner.Y,
        contentX + button.bounds.LowerRightCorner.X,
        contentY + button.bounds.LowerRightCorner.Y
    );

    // Draw button background
    irr::video::SColor bgColor;
    if (button.hovered) {
        bgColor = getButtonHighlightColor();
    } else if (button.type == HotbarButtonType::Empty) {
        bgColor = irr::video::SColor(200, 32, 32, 32);
    } else {
        bgColor = getButtonBackground();
    }
    driver->draw2DRectangle(bgColor, absBounds);

    // Draw icon or text if button has content
    if (button.type != HotbarButtonType::Empty) {
        if (button.type == HotbarButtonType::Skill && gui && !button.emoteText.empty()) {
            irr::gui::IGUIFont* font = gui->getBuiltInFont();
            if (font) {
                std::string skillName = button.emoteText;
                int buttonWidth = absBounds.getWidth() - 4;
                int buttonHeight = absBounds.getHeight() - 14;
                int lineHeight = 10;

                std::transform(skillName.begin(), skillName.end(), skillName.begin(), ::toupper);

                std::vector<std::string> lines;
                std::string currentLine;
                std::istringstream wordStream(skillName);
                std::string word;

                while (wordStream >> word) {
                    std::wstring testLine(currentLine.begin(), currentLine.end());
                    if (!currentLine.empty()) {
                        testLine += L" ";
                    }
                    std::wstring wWord(word.begin(), word.end());
                    testLine += wWord;

                    irr::core::dimension2du testSize = font->getDimension(testLine.c_str());
                    if (static_cast<int>(testSize.Width) <= buttonWidth) {
                        if (!currentLine.empty()) currentLine += " ";
                        currentLine += word;
                    } else if (currentLine.empty()) {
                        while (!word.empty()) {
                            std::wstring wTrunc(word.begin(), word.end());
                            if (static_cast<int>(font->getDimension(wTrunc.c_str()).Width) <= buttonWidth) {
                                break;
                            }
                            word = word.substr(0, word.length() - 1);
                        }
                        lines.push_back(word);
                    } else {
                        lines.push_back(currentLine);
                        currentLine = word;
                    }
                }
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                }

                int maxLines = buttonHeight / lineHeight;
                if (static_cast<int>(lines.size()) > maxLines && maxLines > 0) {
                    lines.resize(maxLines);
                }

                int totalHeight = static_cast<int>(lines.size()) * lineHeight;
                int startY = absBounds.UpperLeftCorner.Y + (buttonHeight - totalHeight) / 2 + 2;

                for (size_t i = 0; i < lines.size(); i++) {
                    std::wstring wLine(lines[i].begin(), lines[i].end());
                    irr::core::dimension2du lineSize = font->getDimension(wLine.c_str());
                    int textX = absBounds.UpperLeftCorner.X + (absBounds.getWidth() - lineSize.Width) / 2;
                    int textY = startY + static_cast<int>(i) * lineHeight;

                    irr::core::recti shadowRect(textX + 1, textY + 1, textX + lineSize.Width + 1, textY + lineSize.Height + 1);
                    font->draw(wLine.c_str(), shadowRect, irr::video::SColor(200, 0, 0, 0));

                    irr::core::recti textRect(textX, textY, textX + lineSize.Width, textY + lineSize.Height);
                    font->draw(wLine.c_str(), textRect, irr::video::SColor(255, 255, 215, 0));
                }
            }
        } else if (iconLoader_) {
            uint32_t iconId = button.iconId;
            if (iconId == 0 && button.type == HotbarButtonType::Emote) {
                iconId = EMOTE_ICON_ID;
            }
            if (iconId > 0) {
                irr::video::ITexture* iconTex = iconLoader_->getIcon(iconId);
                if (iconTex) {
                    int margin = 2;
                    irr::core::recti iconRect(
                        absBounds.UpperLeftCorner.X + margin,
                        absBounds.UpperLeftCorner.Y + margin,
                        absBounds.LowerRightCorner.X - margin,
                        absBounds.LowerRightCorner.Y - margin
                    );
                    irr::core::dimension2du texSize = iconTex->getOriginalSize();
                    irr::core::recti srcRect(0, 0, texSize.Width, texSize.Height);
                    driver->draw2DImage(iconTex, iconRect, srcRect, nullptr, nullptr, true);
                }
            }
        }
    }

    // Draw button number in corner
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            std::wstring label;
            if (index < 9) {
                label = std::to_wstring(index + 1);
            } else {
                label = L"0";
            }

            int labelX = absBounds.LowerRightCorner.X - 8;
            int labelY = absBounds.LowerRightCorner.Y - 10;

            irr::core::recti shadowRect(labelX + 1, labelY + 1, labelX + 9, labelY + 11);
            font->draw(label.c_str(), shadowRect, irr::video::SColor(180, 0, 0, 0));

            irr::core::recti textRect(labelX, labelY, labelX + 8, labelY + 10);
            font->draw(label.c_str(), textRect, irr::video::SColor(220, 200, 200, 200));
        }
    }

    // Draw border
    irr::video::SColor borderColor = button.hovered ?
        getBorderLightColor() : getBorderDarkColor();
    driver->draw2DRectangleOutline(absBounds, borderColor);
}

void HotbarWindow::drawButtonCooldown(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui,
                                       const HotbarButton& button)
{
    float progress = button.getCooldownProgress();
    if (progress <= 0.0f) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    irr::core::recti absBounds(
        contentX + button.bounds.UpperLeftCorner.X,
        contentY + button.bounds.UpperLeftCorner.Y,
        contentX + button.bounds.LowerRightCorner.X,
        contentY + button.bounds.LowerRightCorner.Y
    );

    int margin = 2;
    int buttonHeight = absBounds.getHeight() - margin * 2;
    int overlayHeight = static_cast<int>(buttonHeight * progress);

    // Draw solid overlay
    if (overlayHeight > 0) {
        irr::core::recti overlayRect(
            absBounds.UpperLeftCorner.X + margin,
            absBounds.LowerRightCorner.Y - margin - overlayHeight,
            absBounds.LowerRightCorner.X - margin,
            absBounds.LowerRightCorner.Y - margin
        );
        driver->draw2DRectangle(irr::video::SColor(180, 0, 0, 0), overlayRect);
    }

    // Simplified gradient: single semi-transparent rectangle at sweep edge
    int gradientHeight = 8;
    int gradientTop = absBounds.LowerRightCorner.Y - margin - overlayHeight - gradientHeight;
    int gradientBottom = absBounds.LowerRightCorner.Y - margin - overlayHeight;
    gradientTop = std::max(gradientTop, absBounds.UpperLeftCorner.Y + margin);
    gradientBottom = std::max(gradientBottom, absBounds.UpperLeftCorner.Y + margin);

    if (gradientBottom > gradientTop) {
        driver->draw2DRectangle(irr::video::SColor(90, 0, 0, 0),
            irr::core::recti(absBounds.UpperLeftCorner.X + margin, gradientTop,
                            absBounds.LowerRightCorner.X - margin, gradientBottom));
    }

    // Draw countdown timer text
    if (gui && button.cooldownDurationMs > 0) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            auto now = std::chrono::steady_clock::now();
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                button.cooldownEndTime - now).count();
            if (remaining > 0) {
                std::wstring timeStr;
                if (remaining < 10000) {
                    int tenths = (remaining / 100) % 10;
                    int seconds = remaining / 1000;
                    timeStr = std::to_wstring(seconds) + L"." + std::to_wstring(tenths);
                } else {
                    int seconds = (remaining + 500) / 1000;
                    timeStr = std::to_wstring(seconds);
                }

                irr::core::dimension2du textSize = font->getDimension(timeStr.c_str());
                int textX = absBounds.UpperLeftCorner.X + (absBounds.getWidth() - textSize.Width) / 2;
                int textY = absBounds.UpperLeftCorner.Y + (absBounds.getHeight() - textSize.Height) / 2;

                irr::core::recti shadowRect(textX + 1, textY + 1,
                    textX + textSize.Width + 1, textY + textSize.Height + 1);
                font->draw(timeStr.c_str(), shadowRect, irr::video::SColor(220, 0, 0, 0));

                irr::core::recti textRect(textX, textY,
                    textX + textSize.Width, textY + textSize.Height);
                font->draw(timeStr.c_str(), textRect, irr::video::SColor(255, 255, 255, 255));
            }
        }
    }
}

} // namespace ui
} // namespace eqt
