/*
 * WillEQ Options Window
 *
 * Provides user-configurable settings for display, audio, controls, and game options.
 * Available in all modes (Player, Admin, Repair, etc).
 */

#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <functional>
#include <string>
#include <vector>

namespace eqt {
namespace ui {

// Callback for when display settings change
using DisplaySettingsChangedCallback = std::function<void()>;

// Environment effect quality levels
enum class EffectQuality {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

// Display settings structure (synced to config/display_settings.json)
struct DisplaySettings {
    // Render Distance (affects terrain, objects, entities - max 2000 = sky dome cutoff)
    float renderDistance = 300.0f;

    // Environment Effects
    EffectQuality environmentQuality = EffectQuality::Medium;
    bool atmosphericParticles = true;
    bool ambientCreatures = true;
    bool shorelineWaves = true;
    bool reactiveFoliage = true;
    bool rollingObjects = true;
    bool skyEnabled = true;
    bool animatedTrees = true;
    float environmentDensity = 0.5f;

    // Detail Objects (grass, plants, debris)
    bool detailObjectsEnabled = true;
    float detailDensity = 1.0f;
    float detailViewDistance = 150.0f;
    bool detailGrass = true;
    bool detailPlants = true;
    bool detailRocks = true;
    bool detailDebris = true;
};

class OptionsWindow : public WindowBase {
public:
    OptionsWindow();
    ~OptionsWindow() = default;

    // Position window in center of screen
    void positionDefault(int screenWidth, int screenHeight);

    // Tab selection
    enum class Tab { Display, Audio, Controls, Game };
    void selectTab(Tab tab);
    Tab getCurrentTab() const { return currentTab_; }

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleMouseWheel(float delta);

    // Settings access
    const DisplaySettings& getDisplaySettings() const { return displaySettings_; }
    void setDisplaySettings(const DisplaySettings& settings);

    // Callbacks
    void setDisplaySettingsChangedCallback(DisplaySettingsChangedCallback cb) {
        displaySettingsChangedCallback_ = std::move(cb);
    }

    // Settings persistence
    bool loadSettings(const std::string& path = "config/display_settings.json");
    bool saveSettings(const std::string& path = "");

    // Slider dragging state (for window manager to route events)
    bool isSliderDragging() const { return draggingSlider_; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    // Layout initialization
    void initializeLayout();
    void updateLayout();

    // Tab rendering
    void renderTabs(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderDisplayTab(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderAudioTab(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderControlsTab(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);
    void renderGameTab(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // UI element rendering helpers
    void renderSectionHeader(irr::video::IVideoDriver* driver,
                            irr::gui::IGUIEnvironment* gui,
                            const std::wstring& text,
                            int y);
    void renderCheckbox(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui,
                       const std::wstring& label,
                       bool checked,
                       int x, int y,
                       bool hovered = false);
    void renderSlider(irr::video::IVideoDriver* driver,
                     irr::gui::IGUIEnvironment* gui,
                     const std::wstring& label,
                     float value,
                     int x, int y, int width,
                     bool hovered = false);
    void renderQualitySelector(irr::video::IVideoDriver* driver,
                              irr::gui::IGUIEnvironment* gui,
                              const std::wstring& label,
                              EffectQuality quality,
                              int x, int y,
                              int hoveredOption = -1);

    // Hit testing
    int getTabAtPosition(int x, int y) const;
    bool isInCheckbox(int checkboxX, int checkboxY, int mouseX, int mouseY) const;
    int getSliderValue(int sliderX, int sliderWidth, int mouseX) const;
    int getQualityOptionAtPosition(int baseX, int baseY, int mouseX, int mouseY) const;

    // Settings change notification
    void notifyDisplaySettingsChanged();

    // Current tab
    Tab currentTab_ = Tab::Display;

    // Display settings
    DisplaySettings displaySettings_;
    std::string settingsPath_ = "config/display_settings.json";

    // Callbacks
    DisplaySettingsChangedCallback displaySettingsChangedCallback_;

    // Layout bounds (relative to content area)
    irr::core::recti tabBarBounds_;
    std::vector<irr::core::recti> tabBounds_;
    irr::core::recti contentAreaBounds_;

    // Interaction state
    int hoveredTab_ = -1;
    int hoveredCheckbox_ = -1;
    int hoveredSlider_ = -1;
    int hoveredQualityOption_ = -1;
    bool draggingSlider_ = false;
    int draggingSliderIndex_ = -1;

    // Scroll state for content
    int scrollOffset_ = 0;
    int maxScrollOffset_ = 0;

    // Layout constants
    static constexpr int WINDOW_WIDTH = 400;
    static constexpr int WINDOW_HEIGHT = 580;
    static constexpr int TAB_HEIGHT = 24;
    static constexpr int TAB_PADDING = 8;
    static constexpr int SECTION_HEADER_HEIGHT = 22;
    static constexpr int ROW_HEIGHT = 24;
    static constexpr int ROW_SPACING = 4;
    static constexpr int CHECKBOX_SIZE = 14;
    static constexpr int SLIDER_HEIGHT = 14;
    static constexpr int SLIDER_TRACK_HEIGHT = 6;
    static constexpr int QUALITY_BUTTON_WIDTH = 50;
    static constexpr int QUALITY_BUTTON_HEIGHT = 20;
    static constexpr int QUALITY_BUTTON_SPACING = 4;
    static constexpr int CONTENT_PADDING = 8;
    static constexpr int INDENT = 20;

    // Colors
    irr::video::SColor getTabBackground() const;
    irr::video::SColor getTabActiveBackground() const;
    irr::video::SColor getTabHoverBackground() const;
    irr::video::SColor getTabTextColor() const;
    irr::video::SColor getSectionHeaderColor() const;
    irr::video::SColor getSectionHeaderTextColor() const;
    irr::video::SColor getCheckboxBackground() const;
    irr::video::SColor getCheckboxBorder() const;
    irr::video::SColor getCheckboxCheck() const;
    irr::video::SColor getSliderTrackColor() const;
    irr::video::SColor getSliderFillColor() const;
    irr::video::SColor getSliderThumbColor() const;
    irr::video::SColor getQualityButtonBackground() const;
    irr::video::SColor getQualityButtonActiveBackground() const;
    irr::video::SColor getQualityButtonHoverBackground() const;
    irr::video::SColor getLabelTextColor() const;
    irr::video::SColor getValueTextColor() const;
};

} // namespace ui
} // namespace eqt
