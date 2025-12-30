#pragma once

#include "window_base.h"
#include <cstdint>
#include <functional>
#include <string>

namespace eqt {
namespace ui {

// Currency types (also defined in inventory_window.h, kept in sync)
enum class CurrencyType {
    Platinum,
    Gold,
    Silver,
    Copper
};

// Callback for when money amount is confirmed
using MoneyInputCallback = std::function<void(CurrencyType type, uint32_t amount)>;

class MoneyInputDialog : public WindowBase {
public:
    MoneyInputDialog();

    // Show the dialog for a specific currency type
    void show(CurrencyType type, uint32_t maxAmount);

    // Hide and reset the dialog
    void dismiss();

    // Check if dialog is currently shown
    bool isShown() const { return isShown_; }

    // Get current values
    CurrencyType getCurrencyType() const { return currencyType_; }
    uint32_t getSelectedAmount() const { return selectedAmount_; }
    uint32_t getMaxAmount() const { return maxAmount_; }

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleKeyInput(wchar_t key, bool isBackspace, bool isEnter, bool isEscape);

    // Callbacks
    void setOnConfirm(MoneyInputCallback callback) { onConfirm_ = callback; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void calculateLayout();
    void updateSliderFromAmount();
    void updateAmountFromSlider();
    std::wstring getCurrencyName() const;
    irr::video::SColor getCurrencyColor() const;

    // Button hit testing
    enum class ButtonId {
        None = -1,
        OK = 0,
        Cancel = 1
    };
    ButtonId getButtonAtPosition(int x, int y) const;
    irr::core::recti getButtonBounds(ButtonId button) const;

    // State
    bool isShown_ = false;
    CurrencyType currencyType_ = CurrencyType::Platinum;
    uint32_t maxAmount_ = 0;
    uint32_t selectedAmount_ = 0;

    // Text input state
    std::wstring inputText_;
    bool inputActive_ = false;

    // Slider state
    bool draggingSlider_ = false;
    float sliderPosition_ = 0.0f;  // 0.0 to 1.0

    // UI state
    ButtonId hoveredButton_ = ButtonId::None;

    // Bounds (relative to window)
    irr::core::recti sliderTrackBounds_;
    irr::core::recti sliderHandleBounds_;
    irr::core::recti inputFieldBounds_;

    // Callbacks
    MoneyInputCallback onConfirm_;

    // Layout constants
    static constexpr int DIALOG_WIDTH = 220;
    static constexpr int DIALOG_HEIGHT = 160;
    static constexpr int PADDING = 10;
    static constexpr int BUTTON_WIDTH = 60;
    static constexpr int BUTTON_SPACING = 20;
    static constexpr int LABEL_HEIGHT = 16;
    static constexpr int SLIDER_HEIGHT = 20;
    static constexpr int SLIDER_HANDLE_WIDTH = 12;
    static constexpr int INPUT_HEIGHT = 20;
    static constexpr int INPUT_WIDTH = 80;
};

} // namespace ui
} // namespace eqt
