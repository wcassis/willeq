#ifndef HOTBAR_TYPES_H
#define HOTBAR_TYPES_H

#include <cstdint>
#include <string>
#include <chrono>

namespace eqt {
namespace ui {

/**
 * @brief Type of content in a hotbar button
 */
enum class HotbarButtonType : uint8_t {
    Empty,      // No content
    Spell,      // Spell from spellbook
    Skill,      // Combat skill (kick, bash, etc.)
    Item,       // Usable item
    Macro,      // Custom macro
    Combat      // Combat action (auto-attack, etc.)
};

/**
 * @brief Data structure for a hotbar button
 *
 * This is a stub for future hotbar implementation.
 * Currently used to store skill references from the Skills window.
 */
struct HotbarButton {
    HotbarButtonType type = HotbarButtonType::Empty;
    uint32_t id = 0;           // Spell ID, Skill ID, Item ID, etc.
    std::string label;         // Display name
    uint32_t icon_id = 0;      // Icon to display

    // Cooldown tracking
    std::chrono::steady_clock::time_point last_used;
    uint32_t recast_time_ms = 0;

    /**
     * @brief Check if the button action is ready to use
     * @return true if not on cooldown
     */
    bool isReady() const {
        if (recast_time_ms == 0) {
            return true;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_used).count();
        return elapsed >= recast_time_ms;
    }

    /**
     * @brief Get remaining cooldown time in milliseconds
     * @return Remaining cooldown, or 0 if ready
     */
    uint32_t getCooldownRemaining() const {
        if (recast_time_ms == 0) {
            return 0;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_used).count();
        if (elapsed >= recast_time_ms) {
            return 0;
        }
        return static_cast<uint32_t>(recast_time_ms - elapsed);
    }

    /**
     * @brief Mark the button as used (start cooldown)
     */
    void markUsed() {
        last_used = std::chrono::steady_clock::now();
    }

    /**
     * @brief Check if this button is empty
     */
    bool isEmpty() const {
        return type == HotbarButtonType::Empty;
    }

    /**
     * @brief Clear the button contents
     */
    void clear() {
        type = HotbarButtonType::Empty;
        id = 0;
        label.clear();
        icon_id = 0;
        recast_time_ms = 0;
    }
};

/**
 * @brief Constants for hotbar configuration
 */
constexpr int HOTBAR_SLOTS_PER_BAR = 10;
constexpr int MAX_HOTBARS = 10;
constexpr int TOTAL_HOTBAR_SLOTS = HOTBAR_SLOTS_PER_BAR * MAX_HOTBARS;

/**
 * @brief Pending hotbar button storage
 *
 * This is used to queue buttons for creation before
 * the full hotbar system is implemented.
 */
struct PendingHotbarButton {
    HotbarButtonType type;
    uint32_t id;
    std::string name;

    PendingHotbarButton(HotbarButtonType t, uint32_t i, const std::string& n)
        : type(t), id(i), name(n) {}
};

} // namespace ui
} // namespace eqt

#endif // HOTBAR_TYPES_H
