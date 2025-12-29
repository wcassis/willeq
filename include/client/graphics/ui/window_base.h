#pragma once

#include <irrlicht.h>
#include <string>
#include <functional>

namespace eqt {
namespace ui {

// Forward declaration
class UISettings;

// Window close callback
using WindowCloseCallback = std::function<void()>;

class WindowBase {
public:
    WindowBase(const std::wstring& title, int width, int height);
    virtual ~WindowBase() = default;

    // Rendering
    virtual void render(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui);

    // Input handling - returns true if input was consumed
    virtual bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false);
    virtual bool handleMouseUp(int x, int y, bool leftButton);
    virtual bool handleMouseMove(int x, int y);

    // Position and size
    void setPosition(int x, int y);
    void setSize(int width, int height);
    irr::core::recti getBounds() const { return bounds_; }
    int getX() const { return bounds_.UpperLeftCorner.X; }
    int getY() const { return bounds_.UpperLeftCorner.Y; }
    int getWidth() const { return bounds_.getWidth(); }
    int getHeight() const { return bounds_.getHeight(); }
    int getRight() const { return bounds_.LowerRightCorner.X; }
    int getBottom() const { return bounds_.LowerRightCorner.Y; }

    // Visibility
    void show();
    void hide();
    void setVisible(bool visible);
    bool isVisible() const { return visible_; }

    // Title
    void setTitle(const std::wstring& title) { title_ = title; }
    const std::wstring& getTitle() const { return title_; }

    // Title bar visibility
    void setShowTitleBar(bool show) { showTitleBar_ = show; }
    bool getShowTitleBar() const { return showTitleBar_; }

    // Focus/z-order
    void bringToFront();
    int getZOrder() const { return zOrder_; }
    void setZOrder(int z) { zOrder_ = z; }

    // Close callback
    void setCloseCallback(WindowCloseCallback callback) { closeCallback_ = callback; }

    // Hit testing
    bool containsPoint(int x, int y) const;
    bool titleBarContainsPoint(int x, int y) const;

    // State
    bool isDragging() const { return dragging_; }

    // UI Lock support
    // Returns true if this window is locked (cannot be moved/resized)
    // Checks both global UI lock and per-window alwaysLocked setting
    bool isLocked() const;

    // Returns true if the window can be moved (not locked)
    bool canMove() const { return !isLocked(); }

    // Returns true if the window can be resized (not locked)
    // Note: Base WindowBase doesn't support resize, but subclasses may
    bool canResize() const { return !isLocked(); }

    // Per-window lock state (independent of global UI lock)
    void setAlwaysLocked(bool locked) { alwaysLocked_ = locked; }
    bool isAlwaysLocked() const { return alwaysLocked_; }

    // Hover state (for visual feedback when UI is unlocked)
    void setHovered(bool hovered) { hovered_ = hovered; }
    bool isHovered() const { return hovered_; }

    // Settings key (identifies this window in config file)
    void setSettingsKey(const std::string& key) { settingsKey_ = key; }
    const std::string& getSettingsKey() const { return settingsKey_; }

protected:
    // Subclass rendering
    virtual void renderContent(irr::video::IVideoDriver* driver,
                              irr::gui::IGUIEnvironment* gui) {}

    // UI element helpers
    void drawWindow(irr::video::IVideoDriver* driver);
    void drawTitleBar(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void drawButton(irr::video::IVideoDriver* driver,
                   irr::gui::IGUIEnvironment* gui,
                   const irr::core::recti& bounds,
                   const std::wstring& text,
                   bool highlighted = false,
                   bool disabled = false);

    // Draw highlight border when UI is unlocked and window is hovered/movable
    void drawUnlockedHighlight(irr::video::IVideoDriver* driver);

    // Content area (inside window borders)
    irr::core::recti getContentArea() const;

    // Color accessors - get colors from UISettings
    irr::video::SColor getWindowBackground() const;
    irr::video::SColor getTitleBarBackground() const;
    irr::video::SColor getTitleBarTextColor() const;
    irr::video::SColor getBorderLightColor() const;
    irr::video::SColor getBorderDarkColor() const;
    irr::video::SColor getButtonBackground() const;
    irr::video::SColor getButtonHighlightColor() const;
    irr::video::SColor getButtonTextColor() const;
    irr::video::SColor getButtonDisabledBackground() const;
    irr::video::SColor getButtonDisabledTextColor() const;

    // Layout accessors - get layout values from UISettings
    int getTitleBarHeight() const;
    int getBorderWidth() const;
    int getButtonHeight() const;
    int getButtonPadding() const;
    int getContentPadding() const;

    // Legacy static colors - kept for backwards compatibility
    // New code should use the accessor methods above
    static const irr::video::SColor COLOR_WINDOW_BG;
    static const irr::video::SColor COLOR_TITLE_BAR;
    static const irr::video::SColor COLOR_TITLE_TEXT;
    static const irr::video::SColor COLOR_BORDER_LIGHT;
    static const irr::video::SColor COLOR_BORDER_DARK;
    static const irr::video::SColor COLOR_BUTTON_BG;
    static const irr::video::SColor COLOR_BUTTON_HIGHLIGHT;
    static const irr::video::SColor COLOR_BUTTON_TEXT;

    // Legacy layout constants - kept for backwards compatibility
    // New code should use the accessor methods above
    static constexpr int TITLE_BAR_HEIGHT = 20;
    static constexpr int BORDER_WIDTH = 2;
    static constexpr int BUTTON_HEIGHT = 22;
    static constexpr int BUTTON_PADDING = 4;
    static constexpr int CONTENT_PADDING = 4;


    // Window state
    irr::core::recti bounds_;
    irr::core::recti titleBar_;
    std::wstring title_;
    bool visible_ = false;
    bool showTitleBar_ = true;
    int zOrder_ = 0;

    // Dragging state
    bool dragging_ = false;
    irr::core::vector2di dragOffset_;

    // Lock state
    bool alwaysLocked_ = false;

    // Hover state (for unlock highlighting)
    bool hovered_ = false;

    // Settings key for config file persistence
    std::string settingsKey_;

    // Callbacks
    WindowCloseCallback closeCallback_;
};

} // namespace ui
} // namespace eqt
