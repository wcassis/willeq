#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

/**
 * two_client_survive -- T391's five-minute pair, as a verdict.
 *
 * The hand rig (review/T391/scripts/t391_run.sh) launched an observer, slept
 * 55 s, launched a partner, then polled `kill -0` for 330 s and printed
 * SURVIVED or OBSERVER DIED. Two things were only ever asserted by a human
 * reading the marks file: that a second player actually showed up (a partner
 * that failed to log in produced a cheerful SURVIVED), and that it stayed.
 *
 * Here both are the scenario's own assertion, and "the observer survived" is
 * carried for free by the exit code: a client that dies never returns 0.
 *
 * The partner is a PEER PROCESS, launched by scripts/scenario.sh running the
 * zone_in scenario on the second account. Nothing in the client spawns it --
 * a scenario that forks a second client would need the whole login config of
 * another character, and the harness already owns exactly that.
 */
class TwoClientSurviveScenario final : public IScenario {
public:
    explicit TwoClientSurviveScenario(double holdSeconds, double peerDeadlineSeconds)
        : holdSeconds_(holdSeconds), peerDeadlineSeconds_(peerDeadlineSeconds) {}

    const char* Name() const override { return "two_client_survive"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        const int peers = world.OtherPlayerCount();

        if (!peerSeen_) {
            if (peers >= 1) {
                peerSeen_ = true;
                peerAt_ = tick.sinceStart;
                std::ostringstream os;
                os << "second player visible after " << peerAt_ << "s; holding for "
                   << holdSeconds_ << "s";
                world.Note(os.str());
                SetReason(os.str());
                return StepResult::Running;
            }
            if (tick.sinceStart >= peerDeadlineSeconds_) {
                std::ostringstream os;
                os << "no second player appeared within " << peerDeadlineSeconds_
                   << "s of zone-in (zone '" << world.ZoneName() << "')";
                SetReason(os.str());
                return StepResult::Fail;
            }
            return StepResult::Running;
        }

        if (!world.IsFullyZonedIn()) {
            std::ostringstream os;
            os << "observer left the zone " << (tick.sinceStart - peerAt_)
               << "s into the hold";
            SetReason(os.str());
            return StepResult::Fail;
        }

        if (peers < 1) {
            std::ostringstream os;
            os << "second player vanished " << (tick.sinceStart - peerAt_)
               << "s into the hold (peers now " << peers << ")";
            SetReason(os.str());
            return StepResult::Fail;
        }

        const double held = tick.sinceStart - peerAt_;
        if (held >= holdSeconds_) {
            std::ostringstream os;
            os << "held " << held << "s with " << peers
               << " other player(s) in '" << world.ZoneName() << "' (peer arrived at "
               << peerAt_ << "s)";
            SetReason(os.str());
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    double holdSeconds_;
    double peerDeadlineSeconds_;
    bool peerSeen_ = false;
    double peerAt_ = 0.0;
};

}  // namespace

std::unique_ptr<IScenario> MakeTwoClientSurviveScenario(const ScenarioOptions& options) {
    // The peer window is deliberately independent of the hold: the harness
    // staggers the partner's launch (T391 slept 55 s so the partner arrives as
    // a FRESH spawn packet rather than in the bulk zone-in list), so a peer
    // that has not appeared inside three minutes is a broken partner, not a
    // slow one.
    return std::unique_ptr<IScenario>(
        new TwoClientSurviveScenario(options.holdSeconds, 180.0));
}

}  // namespace scenario
}  // namespace eqt
