/*
 * B460 (#156) -- the ESC window stack. See esc_window_stack.h.
 */

#include "client/graphics/ui/esc_window_stack.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

const char* escWindowName(EscWindow kind) {
    switch (kind) {
        case EscWindow::Options: return "options";
        case EscWindow::Friends: return "friends";
        case EscWindow::ItemDisplay: return "item display";
        case EscWindow::Tip: return "tip";
        case EscWindow::Container: return "container";
        case EscWindow::Tracking: return "tracking";
        case EscWindow::Trainer: return "trainer";
        case EscWindow::Bank: return "bank";
        case EscWindow::Merchant: return "merchant";
        case EscWindow::Tradeskill: return "tradeskill";
        case EscWindow::Loot: return "loot";
        case EscWindow::Spellbook: return "spellbook";
        case EscWindow::Skills: return "skills";
        case EscWindow::Inventory: return "inventory";
        case EscWindow::Count: break;
    }
    return "?";
}

void EscWindowStack::observe(const std::vector<EscWindowKey>& openNow) {
    // Closed since last time: drop.
    open_.erase(std::remove_if(open_.begin(), open_.end(),
                               [&](const Entry& e) {
                                   return std::find(openNow.begin(), openNow.end(), e.key) ==
                                          openNow.end();
                               }),
                open_.end());
    // Newly open: stamp in the caller's (draw) order.
    for (const EscWindowKey& k : openNow) {
        const bool known = std::any_of(open_.begin(), open_.end(),
                                       [&](const Entry& e) { return e.key == k; });
        if (!known) open_.push_back(Entry{k, next_++});
    }
}

std::optional<EscWindowKey> EscWindowStack::newest() const {
    if (open_.empty()) return std::nullopt;
    const auto it = std::max_element(open_.begin(), open_.end(),
                                     [](const Entry& a, const Entry& b) { return a.seq < b.seq; });
    return it->key;
}

}  // namespace Graphics
}  // namespace EQT
