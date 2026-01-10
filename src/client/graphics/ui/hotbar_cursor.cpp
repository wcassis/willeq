#include "client/graphics/ui/hotbar_cursor.h"
#include "client/graphics/ui/item_icon_loader.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace eqt {
namespace ui {

void HotbarCursor::setItem(HotbarButtonType type, uint32_t id,
                            const std::string& emoteText, uint32_t iconId)
{
    hasItem_ = true;
    type_ = type;
    id_ = id;
    emoteText_ = emoteText;
    iconId_ = iconId;
}

void HotbarCursor::clear()
{
    hasItem_ = false;
    type_ = HotbarButtonType::Empty;
    id_ = 0;
    emoteText_.clear();
    iconId_ = 0;
}

void HotbarCursor::render(irr::video::IVideoDriver* driver,
                           irr::gui::IGUIEnvironment* gui,
                           ItemIconLoader* iconLoader,
                           int mouseX, int mouseY)
{
    if (!hasItem_ || !driver) return;

    // Draw position at cursor with offset
    int x = mouseX + CURSOR_OFFSET_X;
    int y = mouseY + CURSOR_OFFSET_Y;

    // Semi-transparent background
    irr::core::recti bgRect(x - 1, y - 1, x + CURSOR_ICON_SIZE + 1, y + CURSOR_ICON_SIZE + 1);
    driver->draw2DRectangle(irr::video::SColor(180, 32, 32, 32), bgRect);

    // For skills, draw skill name text instead of icon (supports multiple lines)
    if (type_ == HotbarButtonType::Skill && gui && !emoteText_.empty()) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            std::string skillName = emoteText_;
            int iconWidth = CURSOR_ICON_SIZE - 4;  // 2px margin each side
            int iconHeight = CURSOR_ICON_SIZE - 4;
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
                if (static_cast<int>(testSize.Width) <= iconWidth) {
                    // Word fits on current line
                    if (!currentLine.empty()) currentLine += " ";
                    currentLine += word;
                } else if (currentLine.empty()) {
                    // Single word too long - truncate it
                    while (!word.empty()) {
                        std::wstring wTrunc(word.begin(), word.end());
                        if (static_cast<int>(font->getDimension(wTrunc.c_str()).Width) <= iconWidth) {
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

            // Limit lines to fit in icon height
            int maxLines = iconHeight / lineHeight;
            if (static_cast<int>(lines.size()) > maxLines && maxLines > 0) {
                lines.resize(maxLines);
            }

            // Calculate total text block height and starting Y position (centered)
            int totalHeight = static_cast<int>(lines.size()) * lineHeight;
            int startY = y + (CURSOR_ICON_SIZE - totalHeight) / 2;

            // Draw each line centered horizontally
            for (size_t i = 0; i < lines.size(); i++) {
                std::wstring wLine(lines[i].begin(), lines[i].end());
                irr::core::dimension2du lineSize = font->getDimension(wLine.c_str());
                int textX = x + (CURSOR_ICON_SIZE - lineSize.Width) / 2;
                int textY = startY + static_cast<int>(i) * lineHeight;

                // Draw shadow
                irr::core::recti shadowRect(textX + 1, textY + 1, textX + lineSize.Width + 1, textY + lineSize.Height + 1);
                font->draw(wLine.c_str(), shadowRect, irr::video::SColor(200, 0, 0, 0));

                // Draw text in gold/yellow color
                irr::core::recti textRect(textX, textY, textX + lineSize.Width, textY + lineSize.Height);
                font->draw(wLine.c_str(), textRect, irr::video::SColor(255, 255, 215, 0));
            }
        }
    } else if (iconLoader) {
        // Draw icon for other button types
        uint32_t iconId = iconId_;

        // Use default icon for emotes
        if (iconId == 0 && type_ == HotbarButtonType::Emote) {
            iconId = 89;  // Speech/chat icon
        }

        if (iconId > 0) {
            irr::video::ITexture* iconTex = iconLoader->getIcon(iconId);
            if (iconTex) {
                irr::core::recti destRect(x, y, x + CURSOR_ICON_SIZE, y + CURSOR_ICON_SIZE);
                irr::core::dimension2du texSize = iconTex->getOriginalSize();
                irr::core::recti srcRect(0, 0, texSize.Width, texSize.Height);
                driver->draw2DImage(iconTex, destRect, srcRect, nullptr, nullptr, true);
            }
        }
    }

    // Draw border
    driver->draw2DRectangleOutline(bgRect, irr::video::SColor(255, 100, 100, 100));
}

} // namespace ui
} // namespace eqt
