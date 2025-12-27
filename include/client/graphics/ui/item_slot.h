#pragma once

#include "inventory_constants.h"
#include "item_instance.h"
#include "ui_settings.h"
#include <irrlicht.h>
#include <string>

namespace eqt {
namespace ui {

// Slot type for different visual treatments
enum class SlotType {
    Equipment,    // Equipment slots in inventory window
    General,      // General inventory slots
    Bag,          // Slots inside a bag
    Bank,         // Bank slots
    Trade         // Trade window slots
};

class ItemSlot {
public:
    ItemSlot();
    ItemSlot(int16_t slotId, SlotType type, int x, int y, int size = DEFAULT_SLOT_SIZE);

    // Setup
    void setSlotId(int16_t slotId) { slotId_ = slotId; }
    void setType(SlotType type) { type_ = type; }
    void setPosition(int x, int y);
    void setSize(int size);
    void setBounds(const irr::core::recti& bounds) { bounds_ = bounds; }
    void setLabel(const std::wstring& label) { label_ = label; }

    // Accessors
    int16_t getSlotId() const { return slotId_; }
    SlotType getType() const { return type_; }
    irr::core::recti getBounds() const { return bounds_; }
    const std::wstring& getLabel() const { return label_; }

    // State
    void setHighlighted(bool highlighted) { highlighted_ = highlighted; }
    void setInvalidDrop(bool invalid) { invalidDrop_ = invalid; }
    bool isHighlighted() const { return highlighted_; }
    bool isInvalidDrop() const { return invalidDrop_; }

    // Rendering
    void render(irr::video::IVideoDriver* driver,
               irr::gui::IGUIEnvironment* gui,
               const inventory::ItemInstance* item,
               irr::video::ITexture* iconTexture = nullptr);

    // Hit testing
    bool containsPoint(int x, int y) const;

    // Constants
    static constexpr int DEFAULT_SLOT_SIZE = 40;
    static constexpr int SMALL_SLOT_SIZE = 32;
    static constexpr int ICON_PADDING = 2;

private:
    void renderBackground(irr::video::IVideoDriver* driver);
    void renderItem(irr::video::IVideoDriver* driver,
                   const inventory::ItemInstance* item,
                   irr::video::ITexture* iconTexture);
    void renderLabel(irr::gui::IGUIEnvironment* gui);
    void renderStackCount(irr::gui::IGUIEnvironment* gui,
                         const inventory::ItemInstance* item);
    void renderHighlight(irr::video::IVideoDriver* driver);

    // Slot identity
    int16_t slotId_ = inventory::SLOT_INVALID;
    SlotType type_ = SlotType::General;

    // Position and size
    irr::core::recti bounds_;

    // Optional label (for equipment slots: "Head", "Chest", etc.)
    std::wstring label_;

    // State
    bool highlighted_ = false;
    bool invalidDrop_ = false;

    // Color accessors - read from UISettings
    irr::video::SColor getSlotBackground() const { return UISettings::instance().slots().background; }
    irr::video::SColor getSlotBorder() const { return UISettings::instance().slots().border; }
    irr::video::SColor getSlotHighlight() const { return UISettings::instance().slots().highlight; }
    irr::video::SColor getSlotInvalid() const { return UISettings::instance().slots().invalid; }
    irr::video::SColor getItemBackground() const { return UISettings::instance().slots().itemBackground; }
    irr::video::SColor getStackText() const { return UISettings::instance().slots().stackText; }
    irr::video::SColor getLabelText() const { return UISettings::instance().slots().labelText; }
};

} // namespace ui
} // namespace eqt
