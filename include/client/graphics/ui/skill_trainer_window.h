/*
 * WillEQ Skill Trainer Window
 *
 * Displays trainable skills from a trainer NPC with current/max values and costs.
 * Allows player to train skills up to the trainer's maximum.
 */

#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <vector>
#include <functional>
#include <cstdint>
#include <string>

namespace eqt {
namespace ui {

// Trainable skill data from trainer
struct TrainerSkillEntry {
    uint8_t skill_id;           // Skill ID (0-99)
    std::wstring name;          // Skill name
    uint32_t current_value;     // Player's current skill value
    uint32_t max_trainable;     // Max value this trainer can teach
    uint32_t cost;              // Cost in copper to train 1 point
};

// Callback types
using SkillTrainCallback = std::function<void(uint8_t skill_id)>;
using TrainerCloseCallback = std::function<void()>;

class SkillTrainerWindow : public WindowBase {
public:
    SkillTrainerWindow();
    ~SkillTrainerWindow() = default;

    // Open trainer window with skill data
    void open(uint32_t trainerId, const std::wstring& trainerName,
              const std::vector<TrainerSkillEntry>& skills);
    void close();

    // Update a single skill after training
    void updateSkill(uint8_t skill_id, uint32_t new_value);

    // Update player money display
    void setPlayerMoney(uint32_t platinum, uint32_t gold,
                        uint32_t silver, uint32_t copper);

    // Update practice points (free training sessions)
    void setPracticePoints(uint32_t points) { practicePoints_ = points; }
    void decrementPracticePoints() { if (practicePoints_ > 0) --practicePoints_; }
    uint32_t getPracticePoints() const { return practicePoints_; }

    // Position window in default location
    void positionDefault(int screenWidth, int screenHeight);

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleMouseWheel(float delta);

    // Callbacks
    void setTrainCallback(SkillTrainCallback cb) { trainCallback_ = std::move(cb); }
    void setCloseCallback(TrainerCloseCallback cb) { closeCallback_ = std::move(cb); }

    // State
    uint32_t getTrainerId() const { return trainerId_; }
    bool isOpen() const { return isVisible(); }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    // Layout initialization
    void initializeLayout();

    // Rendering helpers
    void renderTableHeader(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);
    void renderSkillList(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui);
    void renderSkillRow(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui,
                       const TrainerSkillEntry& skill,
                       int rowIndex,
                       int yPos);
    void renderPlayerMoney(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);
    void renderButtons(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui);
    void renderScrollbar(irr::video::IVideoDriver* driver);

    // Hit testing
    int getSkillAtPosition(int localX, int localY) const;
    bool isTrainButtonHit(int localX, int localY) const;
    bool isDoneButtonHit(int localX, int localY) const;
    bool isInScrollThumb(int localX, int localY) const;

    // Scrolling
    void scrollUp(int rows = 1);
    void scrollDown(int rows = 1);
    int getMaxScrollOffset() const;
    int getVisibleRowCount() const;

    // Price/money formatting
    std::wstring formatPrice(uint32_t copperAmount) const;
    std::wstring formatPlayerMoney() const;
    bool canAffordTraining(uint8_t skill_id) const;
    uint32_t getTrainingCost(uint8_t skill_id) const;

    // Trainer data
    uint32_t trainerId_ = 0;
    std::wstring trainerName_;
    std::vector<TrainerSkillEntry> skills_;

    // Player money (stored in copper for easy comparison)
    uint64_t playerMoneyCopper_ = 0;
    uint32_t platinum_ = 0;
    uint32_t gold_ = 0;
    uint32_t silver_ = 0;
    uint32_t copper_ = 0;

    // Practice points (free training sessions)
    uint32_t practicePoints_ = 0;

    // Selection state
    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;

    // Scroll state
    int scrollOffset_ = 0;
    bool scrollbarDragging_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;

    // Button hover states
    bool trainButtonHovered_ = false;
    bool doneButtonHovered_ = false;

    // Layout bounds (relative to content area)
    irr::core::recti headerBounds_;
    irr::core::recti listBounds_;
    irr::core::recti scrollbarBounds_;
    irr::core::recti scrollUpButtonBounds_;
    irr::core::recti scrollDownButtonBounds_;
    irr::core::recti scrollTrackBounds_;
    irr::core::recti scrollThumbBounds_;
    irr::core::recti moneyBounds_;
    irr::core::recti trainButtonBounds_;
    irr::core::recti doneButtonBounds_;

    // Layout constants
    static constexpr int WINDOW_WIDTH = 420;
    static constexpr int WINDOW_HEIGHT = 420;
    static constexpr int ROW_HEIGHT = 22;
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int MONEY_AREA_HEIGHT = 28;
    static constexpr int BUTTON_AREA_HEIGHT = 36;
    static constexpr int SCROLLBAR_WIDTH = 14;
    static constexpr int SCROLLBAR_BUTTON_HEIGHT = 14;
    static constexpr int NAME_COLUMN_WIDTH = 140;
    static constexpr int CURRENT_COLUMN_WIDTH = 55;
    static constexpr int MAX_COLUMN_WIDTH = 55;
    static constexpr int COST_COLUMN_WIDTH = 90;
    static constexpr int COLUMN_PADDING = 4;
    static constexpr int BUTTON_WIDTH = 100;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int BUTTON_SPACING = 20;

    // Colors
    irr::video::SColor getRowBackground() const;
    irr::video::SColor getRowAlternate() const;
    irr::video::SColor getRowSelected() const;
    irr::video::SColor getRowHovered() const;
    irr::video::SColor getHeaderBackground() const;
    irr::video::SColor getHeaderTextColor() const;
    irr::video::SColor getTextColor() const;
    irr::video::SColor getValueTextColor() const;
    irr::video::SColor getCostTextColor() const;
    irr::video::SColor getCostUnaffordableColor() const;
    irr::video::SColor getMoneyTextColor() const;
    irr::video::SColor getScrollbarBackground() const;
    irr::video::SColor getScrollbarThumb() const;
    irr::video::SColor getPlatinumColor() const;
    irr::video::SColor getGoldColor() const;
    irr::video::SColor getSilverColor() const;
    irr::video::SColor getCopperColor() const;

    // Callbacks
    SkillTrainCallback trainCallback_;
    TrainerCloseCallback closeCallback_;
};

} // namespace ui
} // namespace eqt
