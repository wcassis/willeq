#include "client/graphics/ui/item_slot.h"
#include <sstream>

namespace eqt {
namespace ui {

ItemSlot::ItemSlot() = default;

ItemSlot::ItemSlot(int16_t slotId, SlotType type, int x, int y, int size)
    : slotId_(slotId)
    , type_(type)
{
    setPosition(x, y);
    setSize(size);
}

void ItemSlot::setPosition(int x, int y) {
    int width = bounds_.getWidth();
    int height = bounds_.getHeight();
    if (width == 0) width = DEFAULT_SLOT_SIZE;
    if (height == 0) height = DEFAULT_SLOT_SIZE;

    bounds_.UpperLeftCorner.X = x;
    bounds_.UpperLeftCorner.Y = y;
    bounds_.LowerRightCorner.X = x + width;
    bounds_.LowerRightCorner.Y = y + height;
}

void ItemSlot::setSize(int size) {
    bounds_.LowerRightCorner.X = bounds_.UpperLeftCorner.X + size;
    bounds_.LowerRightCorner.Y = bounds_.UpperLeftCorner.Y + size;
}

void ItemSlot::render(irr::video::IVideoDriver* driver,
                     irr::gui::IGUIEnvironment* gui,
                     const inventory::ItemInstance* item,
                     irr::video::ITexture* iconTexture) {
    renderBackground(driver);

    if (item) {
        renderItem(driver, item, iconTexture);
        if (item->quantity > 1 || item->stackable) {
            renderStackCount(gui, item);
        }
    }

    if (!label_.empty() && !item) {
        renderLabel(gui);
    }

    if (highlighted_ || invalidDrop_) {
        renderHighlight(driver);
    }
}

bool ItemSlot::containsPoint(int x, int y) const {
    return bounds_.isPointInside(irr::core::vector2di(x, y));
}

void ItemSlot::renderBackground(irr::video::IVideoDriver* driver) {
    // Draw slot background
    driver->draw2DRectangle(getSlotBackground(), bounds_);

    // Draw border
    irr::video::SColor borderColor = getSlotBorder();
    // Top
    driver->draw2DRectangle(borderColor,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.UpperLeftCorner.Y + 1));
    // Left
    driver->draw2DRectangle(borderColor,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.UpperLeftCorner.X + 1,
                        bounds_.LowerRightCorner.Y));
    // Bottom
    driver->draw2DRectangle(borderColor,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.LowerRightCorner.Y - 1,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
    // Right
    driver->draw2DRectangle(borderColor,
        irr::core::recti(bounds_.LowerRightCorner.X - 1,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
}

void ItemSlot::renderItem(irr::video::IVideoDriver* driver,
                         const inventory::ItemInstance* item,
                         irr::video::ITexture* iconTexture) {
    // Calculate icon area (with padding)
    irr::core::recti iconRect(
        bounds_.UpperLeftCorner.X + ICON_PADDING,
        bounds_.UpperLeftCorner.Y + ICON_PADDING,
        bounds_.LowerRightCorner.X - ICON_PADDING,
        bounds_.LowerRightCorner.Y - ICON_PADDING
    );

    if (iconTexture) {
        // Draw the item icon texture
        driver->draw2DImage(iconTexture, iconRect,
            irr::core::recti(0, 0,
                            iconTexture->getSize().Width,
                            iconTexture->getSize().Height),
            nullptr, nullptr, true);
    } else {
        // Draw placeholder colored square based on item properties
        irr::video::SColor itemColor;

        // Color based on item type/rarity
        if (item->magic) {
            itemColor = irr::video::SColor(255, 100, 100, 200);  // Blue for magic
        } else if (item->isContainer()) {
            itemColor = irr::video::SColor(255, 139, 90, 43);    // Brown for bags
        } else if (item->slots != 0) {
            itemColor = irr::video::SColor(255, 100, 150, 100);  // Green for equipment
        } else {
            itemColor = irr::video::SColor(255, 150, 150, 150);  // Gray for misc
        }

        // Apply item tint if present
        if (item->color != 0) {
            uint8_t r = (item->color >> 16) & 0xFF;
            uint8_t g = (item->color >> 8) & 0xFF;
            uint8_t b = item->color & 0xFF;
            itemColor = irr::video::SColor(255, r, g, b);
        }

        driver->draw2DRectangle(itemColor, iconRect);

        // Draw a simple border around the item
        irr::video::SColor borderColor(255,
            std::min(255u, itemColor.getRed() + 40),
            std::min(255u, itemColor.getGreen() + 40),
            std::min(255u, itemColor.getBlue() + 40));

        // Top
        driver->draw2DRectangle(borderColor,
            irr::core::recti(iconRect.UpperLeftCorner.X,
                            iconRect.UpperLeftCorner.Y,
                            iconRect.LowerRightCorner.X,
                            iconRect.UpperLeftCorner.Y + 1));
        // Left
        driver->draw2DRectangle(borderColor,
            irr::core::recti(iconRect.UpperLeftCorner.X,
                            iconRect.UpperLeftCorner.Y,
                            iconRect.UpperLeftCorner.X + 1,
                            iconRect.LowerRightCorner.Y));
    }
}

void ItemSlot::renderLabel(irr::gui::IGUIEnvironment* gui) {
    if (label_.empty()) {
        return;
    }

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    // Draw label centered in slot
    irr::core::dimension2du textSize = font->getDimension(label_.c_str());
    int textX = bounds_.UpperLeftCorner.X +
               (bounds_.getWidth() - textSize.Width) / 2;
    int textY = bounds_.UpperLeftCorner.Y +
               (bounds_.getHeight() - textSize.Height) / 2;

    font->draw(label_.c_str(),
              irr::core::recti(textX, textY,
                              textX + textSize.Width,
                              textY + textSize.Height),
              getLabelText());
}

void ItemSlot::renderStackCount(irr::gui::IGUIEnvironment* gui,
                               const inventory::ItemInstance* item) {
    if (!item || (!item->stackable && item->quantity <= 1)) {
        return;
    }

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    // Format stack count
    std::wstringstream ss;
    ss << item->quantity;
    std::wstring countStr = ss.str();

    // Draw in bottom-right corner of slot
    irr::core::dimension2du textSize = font->getDimension(countStr.c_str());
    int textX = bounds_.LowerRightCorner.X - textSize.Width - 2;
    int textY = bounds_.LowerRightCorner.Y - textSize.Height - 2;

    // Draw shadow for readability
    font->draw(countStr.c_str(),
              irr::core::recti(textX + 1, textY + 1,
                              textX + textSize.Width + 1,
                              textY + textSize.Height + 1),
              irr::video::SColor(255, 0, 0, 0));

    // Draw text
    font->draw(countStr.c_str(),
              irr::core::recti(textX, textY,
                              textX + textSize.Width,
                              textY + textSize.Height),
              getStackText());
}

void ItemSlot::renderHighlight(irr::video::IVideoDriver* driver) {
    irr::video::SColor color = invalidDrop_ ? getSlotInvalid() : getSlotHighlight();

    // Draw highlight border (thicker than normal border)
    const int thickness = 2;

    // Top
    driver->draw2DRectangle(color,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.UpperLeftCorner.Y + thickness));
    // Left
    driver->draw2DRectangle(color,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.UpperLeftCorner.X + thickness,
                        bounds_.LowerRightCorner.Y));
    // Bottom
    driver->draw2DRectangle(color,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.LowerRightCorner.Y - thickness,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
    // Right
    driver->draw2DRectangle(color,
        irr::core::recti(bounds_.LowerRightCorner.X - thickness,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
}

} // namespace ui
} // namespace eqt
