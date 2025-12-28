#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace eqt {
namespace ui {

// Forward declarations
class ItemIconLoader;
class HotbarCursor;

// Type of action stored in a hotbar button
enum class HotbarButtonType {
    Empty,      // No action assigned
    Spell,      // Spell from spellbook (by spell_id)
    Item,       // Item use (by item_id)
    Emote       // Custom emote string (right-click to set)
};

// Individual hotbar button data
struct HotbarButton {
    HotbarButtonType type = HotbarButtonType::Empty;
    uint32_t id = 0;              // spell_id or item_id
    std::string emoteText;        // For emote type only
    uint32_t iconId = 0;          // Cached icon ID for display
    irr::core::recti bounds;      // Button bounds (relative to content area)
    bool hovered = false;
    bool pressed = false;

    // Cooldown state
    std::chrono::steady_clock::time_point cooldownEndTime;
    uint32_t cooldownDurationMs = 0;  // Total cooldown duration for progress calculation

    bool isOnCooldown() const {
        if (cooldownDurationMs == 0) return false;
        return std::chrono::steady_clock::now() < cooldownEndTime;
    }

    // Returns 0.0-1.0, where 1.0 = just started, 0.0 = ready
    float getCooldownProgress() const {
        if (cooldownDurationMs == 0) return 0.0f;
        auto now = std::chrono::steady_clock::now();
        if (now >= cooldownEndTime) return 0.0f;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(cooldownEndTime - now).count();
        return static_cast<float>(remaining) / static_cast<float>(cooldownDurationMs);
    }
};

// Callback types
using HotbarActivateCallback = std::function<void(int index, const HotbarButton& button)>;
using HotbarPickupCallback = std::function<void(int index, const HotbarButton& button)>;
using HotbarEmoteDialogCallback = std::function<void(int index)>;

class HotbarWindow : public WindowBase {
public:
    static constexpr int MAX_BUTTONS = 10;

    HotbarWindow();
    ~HotbarWindow() = default;

    // Configuration
    void setButtonCount(int count);  // 1-10 buttons
    int getButtonCount() const { return buttonCount_; }

    // Button management
    void setButton(int index, HotbarButtonType type, uint32_t id,
                   const std::string& emoteText = "", uint32_t iconId = 0);
    void clearButton(int index);
    const HotbarButton& getButton(int index) const;
    void swapButtons(int indexA, int indexB);

    // Activation
    void activateButton(int index);

    // Cooldown management
    void startCooldown(int index, uint32_t durationMs);
    bool isButtonOnCooldown(int index) const;

    // Callbacks
    void setActivateCallback(HotbarActivateCallback cb) { activateCallback_ = std::move(cb); }
    void setPickupCallback(HotbarPickupCallback cb) { pickupCallback_ = std::move(cb); }
    void setEmoteDialogCallback(HotbarEmoteDialogCallback cb) { emoteDialogCallback_ = std::move(cb); }

    // Icon loader reference (required for rendering icons)
    void setIconLoader(ItemIconLoader* loader) { iconLoader_ = loader; }

    // Hotbar cursor reference (for cursor state checks)
    void setHotbarCursor(HotbarCursor* cursor) { hotbarCursor_ = cursor; }

    // Position helpers
    void positionDefault(int screenWidth, int screenHeight);

    // WindowBase overrides
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Hit testing (for WindowManager swap/place logic)
    int getButtonAtPosition(int relX, int relY) const;  // Returns -1 or 0-9

    // Get content area bounds (for external hit testing)
    irr::core::recti getHotbarContentArea() const;

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeLayout();
    void updateWindowSize();
    void drawButton(irr::video::IVideoDriver* driver,
                   irr::gui::IGUIEnvironment* gui,
                   const HotbarButton& button, int index);

    // Layout from UISettings
    int getButtonSize() const;
    int getButtonSpacing() const;
    int getPadding() const;

    // Button data
    std::array<HotbarButton, MAX_BUTTONS> buttons_;
    int buttonCount_ = 10;

    // References
    ItemIconLoader* iconLoader_ = nullptr;
    HotbarCursor* hotbarCursor_ = nullptr;

    // Callbacks
    HotbarActivateCallback activateCallback_;
    HotbarPickupCallback pickupCallback_;
    HotbarEmoteDialogCallback emoteDialogCallback_;

    // Emote icon constant (using a speech bubble from spell icons)
    static constexpr uint32_t EMOTE_ICON_ID = 89;  // Chat/speech icon
};

} // namespace ui
} // namespace eqt
