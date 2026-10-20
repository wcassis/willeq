#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace eqt::bridge {

// Render-thread-only routing state for the two packets that can announce one
// caster's Source stage. The success repeat closes the window for local/target
// casts; observers get only the animation packet, so their entry also expires.
class CastGlowRoute {
public:
    static constexpr uint32_t kActionGlowMs = 2000;

    struct Completion {
        bool remove = true;
        bool create = false;
    };

    bool onBeginCast(uint16_t caster, uint32_t spell, uint32_t castTimeMs) {
        const auto now = Clock::now();
        auto it = active_.find(caster);
        if (it != active_.end() && it->second.spell == spell &&
            now < it->second.until && !it->second.successSeen)
            return false;
        const auto windowMs = std::min(castTimeMs, 60000u) + kActionGlowMs;
        active_[caster] = {spell, now + std::chrono::milliseconds(windowMs), false, false};
        return true;
    }

    Completion onComplete(uint16_t caster, uint32_t spell,
                          bool isCastAnimation, bool isSuccess) {
        const auto now = Clock::now();
        auto it = active_.find(caster);
        const bool same = it != active_.end() && it->second.spell == spell &&
                          now < it->second.until;
        if (isCastAnimation) {
            if (same) return {!it->second.actionOnly, false};
            active_[caster] = {spell, now + std::chrono::milliseconds(kActionGlowMs), true, false};
            return {true, true};
        }
        // An action-only glow was made AFTER the animation packet's existing
        // removal. The flag-4 repeat may follow within milliseconds; let the
        // bounded emitter live long enough to reach a rendered frame.
        const bool keepActionGlow = isSuccess && same && it->second.actionOnly;
        // Keep the pair through the short action window: an AoE's remaining
        // target copies may arrive AFTER the caster's flag-4 repeat. A later
        // BeginCast is unambiguous and starts a new cast even in that window.
        if (isSuccess && same) it->second.successSeen = true;
        return {!keepActionGlow, false};
    }

    void onInterrupt(uint16_t caster) { active_.erase(caster); }

private:
    using Clock = std::chrono::steady_clock;
    struct Active {
        uint32_t spell;
        Clock::time_point until;
        bool actionOnly;
        bool successSeen;
    };
    std::unordered_map<uint16_t, Active> active_;
};

} // namespace eqt::bridge
