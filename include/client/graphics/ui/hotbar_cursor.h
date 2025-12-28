#pragma once

#include "hotbar_window.h"
#include <irrlicht.h>
#include <string>

namespace eqt {
namespace ui {

// Forward declarations
class ItemIconLoader;

// Manages cursor state for hotbar drag-and-drop operations.
// Separate from the inventory cursor to avoid conflicts.
class HotbarCursor {
public:
    HotbarCursor() = default;
    ~HotbarCursor() = default;

    // Check if cursor has an item
    bool hasItem() const { return hasItem_; }

    // Set cursor item
    void setItem(HotbarButtonType type, uint32_t id,
                 const std::string& emoteText, uint32_t iconId);

    // Clear cursor
    void clear();

    // Getters for cursor content
    HotbarButtonType getType() const { return type_; }
    uint32_t getId() const { return id_; }
    const std::string& getEmoteText() const { return emoteText_; }
    uint32_t getIconId() const { return iconId_; }

    // Render cursor icon at mouse position
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui,
                ItemIconLoader* iconLoader,
                int mouseX, int mouseY);

private:
    bool hasItem_ = false;
    HotbarButtonType type_ = HotbarButtonType::Empty;
    uint32_t id_ = 0;
    std::string emoteText_;
    uint32_t iconId_ = 0;

    // Render size for cursor icon
    static constexpr int CURSOR_ICON_SIZE = 32;
    static constexpr int CURSOR_OFFSET_X = 8;
    static constexpr int CURSOR_OFFSET_Y = 8;
};

} // namespace ui
} // namespace eqt
