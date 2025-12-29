#ifndef HOTBAR_TYPES_H
#define HOTBAR_TYPES_H

// Include the main hotbar types from hotbar_window.h
#include "hotbar_window.h"

namespace eqt {
namespace ui {

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
