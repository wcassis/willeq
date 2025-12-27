/*
 * WillEQ Casting Bar UI Implementation
 */

#include "client/graphics/ui/casting_bar.h"
#include "client/graphics/ui/ui_settings.h"
#include <algorithm>

namespace eqt {
namespace ui {

CastingBar::CastingBar()
{
    // Initialize from UISettings
    const auto& barSettings = UISettings::instance().castingBar();
    m_bar_width = barSettings.playerBar.width;
    m_bar_height = barSettings.playerBar.height;
    m_bar_color = barSettings.barFill;
    m_bg_color = barSettings.background;
    m_border_color = barSettings.border;
    m_text_color = barSettings.text;
    m_interrupt_color = barSettings.interrupted;

    centerOnScreen();
}

void CastingBar::startCast(const std::string& spell_name, uint32_t cast_time_ms)
{
    m_spell_name = spell_name;
    m_cast_time_ms = cast_time_ms;
    m_start_time = std::chrono::steady_clock::now();
    m_is_casting = true;
    m_was_interrupted = false;
    m_fade_timer = 0;
}

void CastingBar::cancel()
{
    if (m_is_casting) {
        m_was_interrupted = true;
        m_is_casting = false;
        m_fade_timer = FADE_DURATION;
    }
}

void CastingBar::complete()
{
    if (m_is_casting) {
        m_was_interrupted = false;
        m_is_casting = false;
        m_fade_timer = FADE_DURATION;
    }
}

void CastingBar::update(float delta_time)
{
    // Handle fade-out after cast ends
    if (m_fade_timer > 0) {
        m_fade_timer -= delta_time;
        if (m_fade_timer <= 0) {
            m_fade_timer = 0;
            m_spell_name.clear();
        }
    }

    // Auto-complete when cast time is reached
    if (m_is_casting) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_start_time).count();

        if (elapsed >= m_cast_time_ms) {
            complete();
        }
    }
}

float CastingBar::getProgress() const
{
    if (!m_is_casting && m_fade_timer <= 0) {
        return 0.0f;
    }

    if (m_cast_time_ms == 0) {
        return 1.0f;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_start_time).count();

    return std::clamp(static_cast<float>(elapsed) / m_cast_time_ms, 0.0f, 1.0f);
}

void CastingBar::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    // Don't render if nothing to show
    if (!m_is_casting && m_fade_timer <= 0) {
        return;
    }

    // Calculate fade alpha
    float alpha = 1.0f;
    if (m_fade_timer > 0) {
        alpha = m_fade_timer / FADE_DURATION;
    }

    // Calculate bar position (centered at m_position_x)
    int bar_left = m_position_x - m_bar_width / 2;
    int bar_top = m_position_y;
    int bar_right = bar_left + m_bar_width;
    int bar_bottom = bar_top + m_bar_height;

    // Background rectangle
    irr::core::recti bg_rect(bar_left, bar_top, bar_right, bar_bottom);
    irr::video::SColor bg = m_bg_color;
    bg.setAlpha(static_cast<uint32_t>(bg.getAlpha() * alpha));
    driver->draw2DRectangle(bg, bg_rect);

    // Progress fill
    float progress = getProgress();
    if (m_fade_timer > 0 && !m_was_interrupted) {
        progress = 1.0f;  // Show full bar on successful completion
    }

    int fill_width = static_cast<int>(m_bar_width * progress);
    if (fill_width > 0) {
        irr::core::recti fill_rect(bar_left, bar_top, bar_left + fill_width, bar_bottom);
        irr::video::SColor fill_color = m_was_interrupted ? m_interrupt_color : m_bar_color;
        fill_color.setAlpha(static_cast<uint32_t>(fill_color.getAlpha() * alpha));
        driver->draw2DRectangle(fill_color, fill_rect);
    }

    // Border
    irr::video::SColor border = m_border_color;
    border.setAlpha(static_cast<uint32_t>(border.getAlpha() * alpha));

    // Top border
    driver->draw2DLine(
        irr::core::position2di(bar_left, bar_top),
        irr::core::position2di(bar_right, bar_top),
        border);
    // Bottom border
    driver->draw2DLine(
        irr::core::position2di(bar_left, bar_bottom),
        irr::core::position2di(bar_right, bar_bottom),
        border);
    // Left border
    driver->draw2DLine(
        irr::core::position2di(bar_left, bar_top),
        irr::core::position2di(bar_left, bar_bottom),
        border);
    // Right border
    driver->draw2DLine(
        irr::core::position2di(bar_right, bar_top),
        irr::core::position2di(bar_right, bar_bottom),
        border);

    // Spell name below the bar
    if (!m_spell_name.empty() && gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            // Convert spell name to wide string
            std::wstring wide_name(m_spell_name.begin(), m_spell_name.end());

            // Position text centered below the bar
            int text_y = bar_bottom + 2;
            irr::core::recti text_rect(
                bar_left - 50,  // Extra width for longer names
                text_y,
                bar_right + 50,
                text_y + 14);

            irr::video::SColor text_color = m_text_color;
            text_color.setAlpha(static_cast<uint32_t>(text_color.getAlpha() * alpha));

            font->draw(wide_name.c_str(), text_rect, text_color,
                      true,   // hcenter
                      false); // vcenter
        }
    }

    // Show "INTERRUPTED" text if cancelled
    if (m_was_interrupted && m_fade_timer > 0 && gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            int text_y = bar_top - 16;
            irr::core::recti text_rect(bar_left, text_y, bar_right, bar_top - 2);

            irr::video::SColor int_text = m_interrupt_color;
            int_text.setAlpha(static_cast<uint32_t>(int_text.getAlpha() * alpha));

            font->draw(L"INTERRUPTED", text_rect, int_text, true, false);
        }
    }
}

void CastingBar::setPosition(int x, int y)
{
    m_position_x = x;
    m_position_y = y;
}

void CastingBar::setScreenSize(int width, int height)
{
    m_screen_width = width;
    m_screen_height = height;
}

void CastingBar::centerOnScreen()
{
    m_position_x = m_screen_width / 2;
    // Position in lower third of screen
    m_position_y = m_screen_height - 150;
}

void CastingBar::positionAboveGems(int gem_panel_y)
{
    m_position_x = m_screen_width / 2;
    m_position_y = gem_panel_y - m_bar_height - 30;
}

} // namespace ui
} // namespace eqt
