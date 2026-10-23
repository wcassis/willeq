#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace eqt::bridge {

// Render-thread-only routing state for one caster's Source stage.
//
// B1144 (#464): the Source is drawn ONLY from a BeginCast. The official
// client plays a spellsnew.eff record's stage 0 from its begin-cast handler
// (eqgame.exe 0x49b39d) and from nowhere else: the one other stage-0 call
// (0x4bfe0f, the per-frame actor update) plays a Source that 0x49b39d itself
// deferred (it is the only writer of the actor's +0xecc/+0xed4 deferral
// words; the actor constructor 0x48f023 clears them). An OP_Action with no
// OP_BeginCast before it -- a /disc (B1134 record 51: the observer saw no
// sprite), a GM instant cast -- therefore draws no Source. B1105 used to arm
// a 2 s action-only glow there (an implementation bound, never an oracle
// claim); it is gone.
class CastGlowRoute {
public:
    using Clock = std::chrono::steady_clock;
    // How long after the cast time a repeated BeginCast of the same caster +
    // spell is the same cast (an AoE's per-target packet copies).
    static constexpr uint32_t kRepeatWindowMs = 2000;

    // True when this BeginCast starts a Source. `now` is a parameter so
    // tools/model_viewer's scripted harness can drive the route on its fixed-dt
    // clock (B1138); the client passes Clock::now().
    bool onBeginCast(uint16_t caster, uint32_t spell, uint32_t castTimeMs,
                     Clock::time_point now = Clock::now()) {
        auto it = active_.find(caster);
        if (it != active_.end() && it->second.spell == spell &&
            now < it->second.until && !it->second.successSeen)
            return false;
        const auto windowMs = std::min(castTimeMs, 60000u) + kRepeatWindowMs;
        active_[caster] = {spell, now + std::chrono::milliseconds(windowMs), false};
        return true;
    }

    // An OP_Action of this caster: the caller releases the caster's Source (if
    // it has one; releasing none is a no-op). The success repeat marks the
    // pair finished, so a later BeginCast of the same spell starts a new cast
    // even inside the window.
    void onComplete(uint16_t caster, uint32_t spell, bool isSuccess,
                    Clock::time_point now = Clock::now()) {
        auto it = active_.find(caster);
        const bool same = it != active_.end() && it->second.spell == spell &&
                          now < it->second.until;
        if (isSuccess && same) it->second.successSeen = true;
    }

    void onInterrupt(uint16_t caster) { active_.erase(caster); }

private:
    struct Active {
        uint32_t spell;
        Clock::time_point until;
        bool successSeen;
    };
    std::unordered_map<uint16_t, Active> active_;
};

} // namespace eqt::bridge
