#include "client/scenario/scenarios.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * cast_lands -- a gem cast, and the landing that proves the server agreed.
 *
 * The landing is keyed exactly where T348 (board B055) found it: eqemu never
 * sends a Titanium client an "applied" OP_Buff, so a buff that lands on the
 * player arrives ONLY as OP_Action with type 231 / effect_flag 4, which
 * CommandsComponent::ZoneProcessAction routes into
 * PlayerComponent::NoteBuffLandedOnPlayer -> BuffManager. So the observable
 * is the client's own buff list gaining the spell id we cast, and nothing
 * else -- a chat line or a SpellCastComplete would both be satisfied by a
 * cast the server refused.
 *
 * The prep sequence is T401's measured recipe (review/T401/scripts/
 * t401_caster.script), not invention: GM invulnerability refuses every
 * self-cast, #memspell takes <spellid> <gem, 0-based> while /cast takes a
 * 1-based gem, and an explicit self-target is what made T401's casts stop
 * dying on "Cast timeout waiting for server confirmation".
 */
class CastLandsScenario final : public IScenario {
public:
    explicit CastLandsScenario(uint32_t spellId) : spellId_(spellId) {}

    const char* Name() const override { return "cast_lands"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const double t = tick.sinceStart;

        if (!baselineTaken_) {
            baseline_ = world.PlayerBuffSpellIds();
            baselineTaken_ = true;
            BuildPrep(world);
        }

        // Prep, one command per window, in order. Times are relative to
        // zone-in and deliberately generous: every one is a server round trip.
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

        if (HasLanded(world)) {
            std::ostringstream os;
            os << "spell " << spellId_ << " landed on " << world.PlayerName()
               << " " << (t - lastCastAt_) << "s after gem cast #" << attempts_
               << " (buff slots " << baseline_.size() << " -> "
               << world.PlayerBuffSpellIds().size() << ")";
            SetReason(os.str());
            return StepResult::Pass;
        }

        if (attempts_ == 0 || (t - lastCastAt_) >= kCastWindow) {
            if (attempts_ >= kMaxAttempts) {
                std::ostringstream os;
                os << "spell " << spellId_ << " never landed after " << attempts_
                   << " gem casts (gem 1 must hold a beneficial, durational spell; "
                   << "buff list unchanged at " << world.PlayerBuffSpellIds().size()
                   << " slot(s))";
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

std::unique_ptr<IScenario> MakeCastLandsScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new CastLandsScenario(options.castSpellId));
}

}  // namespace scenario
}  // namespace eqt
