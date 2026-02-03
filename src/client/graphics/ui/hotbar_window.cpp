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

void HotbarWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!driver) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Draw visible buttons
    for (int i = 0; i < buttonCount_; i++) {
        drawButton(driver, gui, buttons_[i], i);
    }
}

void HotbarWindow::drawButton(irr::video::IVideoDriver* driver,
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
        bgColor = irr::video::SColor(200, 32, 32, 32);  // Darker for empty slots
    } else {
        bgColor = getButtonBackground();
    }
    driver->draw2DRectangle(bgColor, absBounds);

    // Draw icon or text if button has content
    if (button.type != HotbarButtonType::Empty) {
        // For skills, draw skill name text instead of icon (supports multiple lines)
        if (button.type == HotbarButtonType::Skill && gui && !button.emoteText.empty()) {
            irr::gui::IGUIFont* font = gui->getBuiltInFont();
            if (font) {
                std::string skillName = button.emoteText;
                int buttonWidth = absBounds.getWidth() - 4;  // 2px margin each side
                int buttonHeight = absBounds.getHeight() - 14;  // Leave room for number label
                int lineHeight = 10;  // Built-in font is ~10px tall

                // Convert skill name to uppercase for display
                std::transform(skillName.begin(), skillName.end(), skillName.begin(), ::toupper);

                // Split skill name into words and arrange into lines that fit
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
                        // Word fits on current line
                        if (!currentLine.empty()) currentLine += " ";
                        currentLine += word;
                    } else if (currentLine.empty()) {
                        // Single word too long - truncate it
                        while (!word.empty()) {
                            std::wstring wTrunc(word.begin(), word.end());
                            if (static_cast<int>(font->getDimension(wTrunc.c_str()).Width) <= buttonWidth) {
                                break;
                            }
                            word = word.substr(0, word.length() - 1);
                        }
                        lines.push_back(word);
                    } else {
                        // Start new line with this word
                        lines.push_back(currentLine);
                        currentLine = word;
                    }
                }
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                }

                // Limit lines to fit in button height
                int maxLines = buttonHeight / lineHeight;
                if (static_cast<int>(lines.size()) > maxLines && maxLines > 0) {
                    lines.resize(maxLines);
                }

                // Calculate total text block height and starting Y position (centered)
                int totalHeight = static_cast<int>(lines.size()) * lineHeight;
                int startY = absBounds.UpperLeftCorner.Y + (buttonHeight - totalHeight) / 2 + 2;

                // Draw each line centered horizontally
                for (size_t i = 0; i < lines.size(); i++) {
                    std::wstring wLine(lines[i].begin(), lines[i].end());
                    irr::core::dimension2du lineSize = font->getDimension(wLine.c_str());
                    int textX = absBounds.UpperLeftCorner.X + (absBounds.getWidth() - lineSize.Width) / 2;
                    int textY = startY + static_cast<int>(i) * lineHeight;

                    // Draw shadow
                    irr::core::recti shadowRect(textX + 1, textY + 1, textX + lineSize.Width + 1, textY + lineSize.Height + 1);
                    font->draw(wLine.c_str(), shadowRect, irr::video::SColor(200, 0, 0, 0));

                    // Draw text in gold/yellow color
                    irr::core::recti textRect(textX, textY, textX + lineSize.Width, textY + lineSize.Height);
                    font->draw(wLine.c_str(), textRect, irr::video::SColor(255, 255, 215, 0));
                }
            }
        } else if (iconLoader_) {
            // Draw icon for other button types
            uint32_t iconId = button.iconId;

            // Use default icons for types without specific icons
            if (iconId == 0 && button.type == HotbarButtonType::Emote) {
                iconId = EMOTE_ICON_ID;
            }

            if (iconId > 0) {
                irr::video::ITexture* iconTex = iconLoader_->getIcon(iconId);
                if (iconTex) {
                    // Draw icon centered in button with small margin
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

    // Draw cooldown overlay if on cooldown
    if (button.isOnCooldown()) {
        float progress = button.getCooldownProgress();  // 1.0 = full, 0.0 = done
        if (progress > 0.0f) {
            int margin = 2;
            int buttonHeight = absBounds.getHeight() - margin * 2;
            int buttonWidth = absBounds.getWidth() - margin * 2;
            int overlayHeight = static_cast<int>(buttonHeight * progress);

            // Draw main solid overlay (the cooled-down portion)
            if (overlayHeight > 0) {
                irr::core::recti overlayRect(
                    absBounds.UpperLeftCorner.X + margin,
                    absBounds.LowerRightCorner.Y - margin - overlayHeight,
                    absBounds.LowerRightCorner.X - margin,
                    absBounds.LowerRightCorner.Y - margin
                );
                irr::video::SColor overlayColor(180, 0, 0, 0);
                driver->draw2DRectangle(overlayColor, overlayRect);
            }

            // Draw gradient fade at the top edge of the overlay (sweep effect)
            int gradientHeight = 8;  // Height of the gradient fade zone
            int gradientTop = absBounds.LowerRightCorner.Y - margin - overlayHeight - gradientHeight;
            int gradientBottom = absBounds.LowerRightCorner.Y - margin - overlayHeight;

            // Clamp gradient to button bounds
            gradientTop = std::max(gradientTop, absBounds.UpperLeftCorner.Y + margin);
            gradientBottom = std::max(gradientBottom, absBounds.UpperLeftCorner.Y + margin);

            if (gradientBottom > gradientTop) {
                // Draw gradient lines from transparent (top) to semi-opaque (bottom)
                for (int y = gradientTop; y < gradientBottom; y++) {
                    float gradientProgress = static_cast<float>(y - gradientTop) / static_cast<float>(gradientBottom - gradientTop);
                    int alpha = static_cast<int>(180 * gradientProgress);
                    irr::video::SColor lineColor(alpha, 0, 0, 0);
                    driver->draw2DLine(
                        irr::core::position2di(absBounds.UpperLeftCorner.X + margin, y),
                        irr::core::position2di(absBounds.LowerRightCorner.X - margin, y),
                        lineColor
                    );
                }
            }

            // Draw countdown timer text in center of button
            if (gui && button.cooldownDurationMs > 0) {
                irr::gui::IGUIFont* font = gui->getBuiltInFont();
                if (font) {
                    // Calculate remaining time
                    auto now = std::chrono::steady_clock::now();
                    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        button.cooldownEndTime - now).count();
                    if (remaining > 0) {
                        // Format time: show seconds with 1 decimal for < 10s, whole seconds otherwise
                        std::wstring timeStr;
                        if (remaining < 10000) {
                            // Less than 10 seconds - show 1 decimal place
                            int tenths = (remaining / 100) % 10;
                            int seconds = remaining / 1000;
                            timeStr = std::to_wstring(seconds) + L"." + std::to_wstring(tenths);
                        } else {
                            // 10+ seconds - show whole seconds
                            int seconds = (remaining + 500) / 1000;  // Round to nearest second
                            timeStr = std::to_wstring(seconds);
                        }

                        // Calculate text position (centered in button)
                        irr::core::dimension2du textSize = font->getDimension(timeStr.c_str());
                        int textX = absBounds.UpperLeftCorner.X + (absBounds.getWidth() - textSize.Width) / 2;
                        int textY = absBounds.UpperLeftCorner.Y + (absBounds.getHeight() - textSize.Height) / 2;

                        // Draw shadow
                        irr::core::recti shadowRect(textX + 1, textY + 1,
                            textX + textSize.Width + 1, textY + textSize.Height + 1);
                        font->draw(timeStr.c_str(), shadowRect, irr::video::SColor(220, 0, 0, 0));

                        // Draw text in white/light color
                        irr::core::recti textRect(textX, textY,
                            textX + textSize.Width, textY + textSize.Height);
                        font->draw(timeStr.c_str(), textRect, irr::video::SColor(255, 255, 255, 255));
                    }
                }
            }
        }
    }

    // Draw button number in corner
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            // Number label (1-9, 0 for 10th)
            std::wstring label;
            if (index < 9) {
                label = std::to_wstring(index + 1);
            } else {
                label = L"0";
            }

            // Draw in bottom-right corner
            int labelX = absBounds.LowerRightCorner.X - 8;
            int labelY = absBounds.LowerRightCorner.Y - 10;

            // Shadow
            irr::core::recti shadowRect(labelX + 1, labelY + 1, labelX + 9, labelY + 11);
            font->draw(label.c_str(), shadowRect, irr::video::SColor(180, 0, 0, 0));

            // Text
            irr::core::recti textRect(labelX, labelY, labelX + 8, labelY + 10);
            font->draw(label.c_str(), textRect, irr::video::SColor(220, 200, 200, 200));
        }
    }

    // Draw border
    irr::video::SColor borderColor = button.hovered ?
        getBorderLightColor() : getBorderDarkColor();
    driver->draw2DRectangleOutline(absBounds, borderColor);
}

} // namespace ui
} // namespace eqt
