#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include "client/pet_constants.h"
#include <client/spell/buff_manager.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Forward declaration
class EverQuest;

namespace eqt {
namespace ui {
class ItemIconLoader;
}
}

namespace eqt {
namespace ui {

// Callback type for pet commands
using PetCommandCallback = std::function<void(EQT::PetCommand command, uint16_t targetId)>;

// Pet command button info
struct PetButton {
    irr::core::recti bounds;
    std::wstring label;
    EQT::PetCommand command;
    EQT::PetButton buttonId;  // For toggle state tracking (PET_BUTTON_COUNT means not a toggle)
    bool isToggle;
    bool hovered = false;
};

class PetWindow : public WindowBase {
public:
    PetWindow();
    ~PetWindow() = default;

    // Set EverQuest reference for pet data
    void setEQ(EverQuest* eq) { eq_ = eq; }

    // Set buff manager for pet buffs
    void setBuffManager(EQ::BuffManager* buffMgr) { buffMgr_ = buffMgr; }

    // Set icon loader for buff icons
    void setIconLoader(ItemIconLoader* iconLoader) { iconLoader_ = iconLoader; }

    // Position on screen
    void positionDefault(int screenWidth, int screenHeight);

    // Update from pet state (call each frame)
    void update();

    // Update flash timer for expiring buffs
    void updateFlashTimer(uint32_t currentTimeMs);

    // Get the currently hovered pet buff (for tooltip display)
    const EQ::ActiveBuff* getHoveredPetBuff() const;

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setCommandCallback(PetCommandCallback cb) { commandCallback_ = std::move(cb); }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeLayout();
    void drawHealthBar(irr::video::IVideoDriver* driver,
                      const irr::core::recti& bounds, uint8_t percent);
    void drawManaBar(irr::video::IVideoDriver* driver,
                    const irr::core::recti& bounds, uint8_t percent);
    void drawCommandButton(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui,
                          const PetButton& button, bool pressed, bool enabled);

    // Buff row rendering
    void drawBuffRow(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void drawBuffSlot(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                     const irr::core::recti& bounds, const EQ::ActiveBuff& buff);
    void drawDuration(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                     const irr::core::recti& bounds, const std::string& timeStr);
    int getBuffAtPosition(int x, int y) const;

    // Layout constants
    int NAME_HEIGHT;
    int LEVEL_WIDTH;
    int BAR_HEIGHT;
    int BAR_SPACING;
    int BUTTON_WIDTH;
    int BUTTON_HEIGHT;
    int BUTTON_SPACING;
    int WINDOW_PADDING;
    int BUTTONS_PER_ROW;
    int BUTTON_ROWS;

    // Buff row layout constants
    static constexpr int BUFF_SIZE = 20;           // Size of buff icons (smaller than player buff window)
    static constexpr int BUFF_SPACING = 2;         // Spacing between buff icons
    static constexpr int BUFF_ROW_HEIGHT = 24;     // Height of buff row (icon + padding)
    static constexpr int MAX_VISIBLE_BUFFS = 7;    // Max buffs visible in one row

    // Color accessors - read from UISettings (reuse group window colors)
    irr::video::SColor getHpBackground() const { return UISettings::instance().group().hpBackground; }
    irr::video::SColor getHpHigh() const { return UISettings::instance().group().hpHigh; }
    irr::video::SColor getHpMedium() const { return UISettings::instance().group().hpMedium; }
    irr::video::SColor getHpLow() const { return UISettings::instance().group().hpLow; }
    irr::video::SColor getManaBackground() const { return UISettings::instance().group().manaBackground; }
    irr::video::SColor getManaFill() const { return UISettings::instance().group().manaFill; }

    // Layout bounds (relative to content area)
    irr::core::recti buffRowBounds_;
    irr::core::recti nameBounds_;
    irr::core::recti levelBounds_;
    irr::core::recti hpBarBounds_;
    irr::core::recti manaBarBounds_;

    // Buff slot positions (relative to buff row)
    struct BuffSlotLayout {
        irr::core::recti bounds;
    };
    std::vector<BuffSlotLayout> buffSlots_;

    // Command buttons
    std::array<PetButton, 9> buttons_;

    // Cached pet data
    std::wstring petName_;
    uint8_t petLevel_ = 0;
    uint8_t hpPercent_ = 100;
    uint8_t manaPercent_ = 100;
    bool hasPet_ = false;

    // State
    EverQuest* eq_ = nullptr;

    // Buff management
    EQ::BuffManager* buffMgr_ = nullptr;
    ItemIconLoader* iconLoader_ = nullptr;
    int hoveredBuffSlot_ = -1;

    // Flash timer for expiring buffs
    uint32_t flashTimer_ = 0;
    bool flashOn_ = true;

    // Callbacks
    PetCommandCallback commandCallback_;
};

} // namespace ui
} // namespace eqt
