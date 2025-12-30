#pragma once

#include "window_base.h"
#include <cstdint>
#include <functional>
#include <string>

namespace eqt {
namespace ui {

// Callback types for trade request actions
using TradeRequestAcceptCallback = std::function<void()>;
using TradeRequestDeclineCallback = std::function<void()>;

class TradeRequestDialog : public WindowBase {
public:
    TradeRequestDialog();

    // Show the dialog with the requester's name and spawn ID
    void show(uint32_t spawnId, const std::string& playerName);

    // Hide and reset the dialog
    void dismiss();

    // Check if dialog is currently shown
    bool isShown() const { return isShown_; }

    // Get the spawn ID of the player requesting trade
    uint32_t getRequesterSpawnId() const { return requesterSpawnId_; }
    const std::string& getRequesterName() const { return requesterName_; }

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setOnAccept(TradeRequestAcceptCallback callback) { onAccept_ = callback; }
    void setOnDecline(TradeRequestDeclineCallback callback) { onDecline_ = callback; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void calculateLayout();

    // Button hit testing
    enum class ButtonId {
        None = -1,
        Accept = 0,
        Decline = 1
    };
    ButtonId getButtonAtPosition(int x, int y) const;
    irr::core::recti getButtonBounds(ButtonId button) const;

    // State
    bool isShown_ = false;
    uint32_t requesterSpawnId_ = 0;
    std::string requesterName_;

    // UI state
    ButtonId hoveredButton_ = ButtonId::None;

    // Callbacks
    TradeRequestAcceptCallback onAccept_;
    TradeRequestDeclineCallback onDecline_;

    // Layout constants
    static constexpr int DIALOG_WIDTH = 220;
    static constexpr int DIALOG_HEIGHT = 80;
    static constexpr int PADDING = 10;
    static constexpr int BUTTON_WIDTH = 70;
    static constexpr int BUTTON_SPACING = 20;
    static constexpr int MESSAGE_HEIGHT = 20;
};

} // namespace ui
} // namespace eqt
