#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <client/spell/buff_manager.h>
#include <vector>
#include <functional>

namespace eqt {
namespace ui {

class ItemIconLoader;

// Callback for right-click to cancel buff
using BuffCancelCallback = std::function<void(uint8_t slot)>;

class BuffWindow : public WindowBase {
public:
    BuffWindow(EQ::BuffManager* buffMgr, ItemIconLoader* iconLoader);
    ~BuffWindow();

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseMove(int x, int y) override;

    // Update time display (call each frame)
    void update(uint32_t currentTimeMs);

    // View mode: show target's buffs instead of player's
    void showTargetBuffs(uint16_t targetId);
    void showPlayerBuffs();
    bool isShowingTarget() const { return showingTarget_; }
    uint16_t getTargetId() const { return targetId_; }

    // Callback for canceling buffs
    void setCancelCallback(BuffCancelCallback callback) { cancelCallback_ = callback; }

    // Get the currently hovered buff (or nullptr if none)
    const EQ::ActiveBuff* getHoveredBuff() const;

    // Position window in default location
    void positionDefault(int screenWidth, int screenHeight);

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    // Layout buff slots
    void layoutBuffSlots();

    // Draw a single buff slot
    void drawBuffSlot(irr::video::IVideoDriver* driver,
                     irr::gui::IGUIEnvironment* gui,
                     int slotIndex,
                     const EQ::ActiveBuff& buff);

    // Draw empty slot
    void drawEmptySlot(irr::video::IVideoDriver* driver,
                      int slotIndex);

    // Draw duration overlay
    void drawDuration(irr::video::IVideoDriver* driver,
                     irr::gui::IGUIEnvironment* gui,
                     const irr::core::recti& bounds,
                     const std::string& timeStr);

    // Get buff slot at position
    int getBuffAtPosition(int x, int y) const;

    // Show tooltip for a buff
    void showBuffTooltip(irr::gui::IGUIEnvironment* gui,
                        const EQ::ActiveBuff& buff, int x, int y);

    // Managers
    EQ::BuffManager* buffMgr_;
    ItemIconLoader* iconLoader_;

    // Layout constants - initialized from UISettings
    int BUFF_COLS;
    int BUFF_ROWS;
    int BUFF_SIZE;
    int BUFF_SPACING;
    int WINDOW_PADDING;

    // Color accessors - read from UISettings
    irr::video::SColor getBuffBackground() const { return UISettings::instance().buff().buffBackground; }
    irr::video::SColor getBuffBorder() const { return UISettings::instance().buff().buffBorder; }
    irr::video::SColor getDebuffBorder() const { return UISettings::instance().buff().debuffBorder; }
    irr::video::SColor getEmptySlotColor() const { return UISettings::instance().buff().emptySlot; }
    irr::video::SColor getDurationBackground() const { return UISettings::instance().buff().durationBackground; }
    irr::video::SColor getDurationTextColor() const { return UISettings::instance().buff().durationText; }

    // Buff slot layout
    struct BuffSlotLayout {
        irr::core::recti bounds;
    };
    std::vector<BuffSlotLayout> buffSlots_;

    // View mode
    bool showingTarget_ = false;
    uint16_t targetId_ = 0;

    // Hover state
    int hoveredSlot_ = -1;
    bool showTooltip_ = false;
    irr::core::recti tooltipBounds_;

    // Callback
    BuffCancelCallback cancelCallback_;

    // Flash timer for expiring buffs (toggles every ~250ms)
    uint32_t flashTimer_ = 0;
    bool flashOn_ = true;
};

} // namespace ui
} // namespace eqt
