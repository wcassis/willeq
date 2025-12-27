/*
 * WillEQ Casting Bar UI Component
 *
 * Displays spell casting progress as a horizontal bar with spell name.
 * Shows during active spell casting with progress fill and interruption state.
 */

#pragma once

#include <irrlicht.h>
#include <string>
#include <chrono>
#include "ui_settings.h"

namespace eqt {
namespace ui {

class CastingBar {
public:
    CastingBar();
    ~CastingBar() = default;

    // Start casting display
    // spell_name: Name of the spell being cast
    // cast_time_ms: Total casting time in milliseconds
    void startCast(const std::string& spell_name, uint32_t cast_time_ms);

    // Cancel casting (interrupted by damage, movement, etc.)
    void cancel();

    // Complete casting (spell finished successfully)
    void complete();

    // Update casting progress
    // delta_time: Time since last update in seconds
    void update(float delta_time);

    // Render the casting bar
    void render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Set position (centered horizontally at x, y is top of bar)
    void setPosition(int x, int y);

    // Set screen dimensions for centering
    void setScreenSize(int width, int height);

    // Status queries
    bool isActive() const { return m_is_casting; }
    bool wasInterrupted() const { return m_was_interrupted; }
    float getProgress() const;
    const std::string& getSpellName() const { return m_spell_name; }

    // Configuration
    void setBarWidth(int width) { m_bar_width = width; }
    void setBarHeight(int height) { m_bar_height = height; }
    void setBarColor(const irr::video::SColor& color) { m_bar_color = color; }
    void setBackgroundColor(const irr::video::SColor& color) { m_bg_color = color; }
    void setInterruptColor(const irr::video::SColor& color) { m_interrupt_color = color; }
    void setTextColor(const irr::video::SColor& color) { m_text_color = color; }

    // Position presets
    void centerOnScreen();  // Center horizontally, position above spell gems
    void positionAboveGems(int gem_panel_y);  // Position above spell gem panel

private:
    // Casting state
    bool m_is_casting = false;
    bool m_was_interrupted = false;
    std::string m_spell_name;
    uint32_t m_cast_time_ms = 0;
    std::chrono::steady_clock::time_point m_start_time;

    // Position and size
    int m_position_x = 0;
    int m_position_y = 0;
    int m_bar_width = 200;
    int m_bar_height = 16;
    int m_screen_width = 800;
    int m_screen_height = 600;

    // Colors
    irr::video::SColor m_bar_color{255, 100, 150, 255};       // Blue casting progress
    irr::video::SColor m_bg_color{200, 40, 40, 60};           // Dark background
    irr::video::SColor m_border_color{255, 150, 150, 180};    // Light border
    irr::video::SColor m_text_color{255, 255, 255, 255};      // White text
    irr::video::SColor m_interrupt_color{255, 255, 80, 80};   // Red for interrupted

    // Animation state for fade-out after completion/interrupt
    float m_fade_timer = 0;
    static constexpr float FADE_DURATION = 0.5f;
};

} // namespace ui
} // namespace eqt
