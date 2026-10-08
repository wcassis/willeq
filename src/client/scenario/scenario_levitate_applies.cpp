#include "client/scenario/scenarios.h"
#include "client/state/event_types.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * levitate_applies -- B892 (GitLab #208): cast Levitate on ourselves and
 * wait until the client is actually levitating.
 *
 * The landing is keyed exactly where the cast_lands family keys it: eqemu
 * never sends a Titanium client an "applied" OP_Buff, so the buff arrives
 * only as the OP_Action 231/effect_flag-4 pair that reaches BuffManager,
 * and the client's own buff list is the observable the cast_lands
 * scenario proved (T348). Levitation adds ONE more observable, and it is
 * the whole point of the card: eqemu sends the caster the levitate
 * appearance with ignore_self, so the caster's own client never gets
 * AT_FLYMODE 2. The client derives its own levitation from the buff list
 * (any active buff whose spell carries SPA 57) and publishes
 * LevitationChanged -- IScenarioWorld::PlayerLevitating() and
 * EventCount(LevitationChanged) read exactly that.
 *
 * The prep is T401's measured recipe (review/T401/scripts/t401_caster.
 * script), identical to cast_lands: GM invulnerability refuses every
 * self-cast, #memspell takes <spellid> <gem, 0-based> while /cast takes a
 * 1-based gem, and the explicit self-target is what made T401's casts stop
 * dying on "Cast timeout waiting for server confirmation".
 */
class LevitateAppliesScenario final : public IScenario {
public:
    explicit LevitateAppliesScenario(uint32_t spellId) : spellId_(spellId) {}

    const char* Name() const override { return "levitate_applies"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!baselineTaken_) {
            baseline_ = world.PlayerBuffSpellIds();
            baselineTaken_ = true;
            BuildPrep(world);
        }

        // Prep, one command per window, in order. The same generous
        // round-trip times cast_lands uses.
        if (prepIndex_ < prep_.size()) {
            const PrepStep& step = prep_[prepIndex_];
            if (t < step.at) {
                return StepResult::Running;
            }
            world.Note("prep: " + step.command);
            world.SendCommand(step.command);
            ++prepIndex_;
            return StepResult::Running;
        }

        if (t < kFirstCastAt) {
            return StepResult::Running;
        }

        if (HasLanded(world) && world.PlayerLevitating()) {
            std::ostringstream os;
            os << "spell " << spellId_ << " landed on " << world.PlayerName()
               << " and the client is levitating " << (t - lastCastAt_)
               << "s after gem cast #" << attempts_ << " (buff slots "
               << baseline_.size() << " -> " << world.PlayerBuffSpellIds().size()
               << ", LevitationChanged events "
               << world.EventCount(static_cast<int>(
                      state::GameEventType::LevitationChanged)) << ")";
            SetReason(os.str());
            return StepResult::Pass;
        }

        if (attempts_ == 0 || (t - lastCastAt_) >= kCastWindow) {
            if (attempts_ >= kMaxAttempts) {
                std::ostringstream os;
                os << "spell " << spellId_ << " never levitated after "
                   << attempts_ << " gem casts (buff landed: "
                   << (HasLanded(world) ? "yes" : "no")
                   << ", client levitating: " << (world.PlayerLevitating() ? "yes" : "no")
                   << ", LevitationChanged events "
                   << world.EventCount(static_cast<int>(
                          state::GameEventType::LevitationChanged))
                   << "; gem 1 must hold the levitation spell)";
                SetReason(os.str());
                return StepResult::Fail;
            }
            ++attempts_;
            lastCastAt_ = t;
            world.Note("gem cast attempt " + std::to_string(attempts_) + ": /cast 1");
            world.SendCommand("/cast 1");
        }
        return StepResult::Running;
    }

private:
    static constexpr double kFirstCastAt = 21.0;
    static constexpr double kCastWindow = 20.0;
    static constexpr int kMaxAttempts = 3;

    struct PrepStep {
        double at;
        std::string command;
    };

    void BuildPrep(IScenarioWorld& world) {
        // #memspell takes the gem 0-based; /cast 1 is that same gem.
        prep_ = {
            {3.0, "/say #set invulnerable off"},
            {7.0, "/say #unmemspells"},
            {11.0, "/say #memspell " + std::to_string(spellId_) + " 0"},
            {16.0, "/target " + world.PlayerName()},
        };
    }

    bool HasLanded(IScenarioWorld& world) const {
        const std::vector<uint32_t> now = world.PlayerBuffSpellIds();
        const auto hits = std::count(now.begin(), now.end(), spellId_);
        const auto had = std::count(baseline_.begin(), baseline_.end(), spellId_);
        return hits > had;
    }

    uint32_t spellId_;
    bool baselineTaken_ = false;
    std::vector<uint32_t> baseline_;
    std::vector<PrepStep> prep_;
    size_t prepIndex_ = 0;
    int attempts_ = 0;
    double lastCastAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeLevitateAppliesScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new LevitateAppliesScenario(options.castSpellId));
}

}  // namespace scenario
}  // namespace eqt
