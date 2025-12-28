#include "client/graphics/ui/hotbar_cursor.h"
#include "client/graphics/ui/item_icon_loader.h"

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
    if (!hasItem_ || !driver || !iconLoader) return;

    // Get icon texture
    uint32_t iconId = iconId_;

    // Use emote icon for emote type if no icon specified
    if (type_ == HotbarButtonType::Emote && iconId == 0) {
        iconId = 89;  // Speech/chat icon
    }

    if (iconId == 0) return;

    irr::video::ITexture* iconTex = iconLoader->getIcon(iconId);
    if (!iconTex) return;

    // Draw icon at cursor position with offset
    int x = mouseX + CURSOR_OFFSET_X;
    int y = mouseY + CURSOR_OFFSET_Y;

    irr::core::recti destRect(
        x, y,
        x + CURSOR_ICON_SIZE, y + CURSOR_ICON_SIZE
    );

    irr::core::dimension2du texSize = iconTex->getOriginalSize();
    irr::core::recti srcRect(0, 0, texSize.Width, texSize.Height);

    // Semi-transparent background
    irr::core::recti bgRect(x - 1, y - 1, x + CURSOR_ICON_SIZE + 1, y + CURSOR_ICON_SIZE + 1);
    driver->draw2DRectangle(irr::video::SColor(180, 32, 32, 32), bgRect);

    // Draw the icon
    driver->draw2DImage(iconTex, destRect, srcRect, nullptr, nullptr, true);

    // Draw border
    driver->draw2DRectangleOutline(bgRect, irr::video::SColor(255, 100, 100, 100));
}

} // namespace ui
} // namespace eqt
