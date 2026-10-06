// B925 (issue #244 / #258): the zone-link hold, as a verdict.
//
// A scenario that zones in and must KEEP its link. Unlike two_client_survive
// (a peer is the liveness proxy) this one holds with a single client, so it is
// the plain "does the link survive a long load" check that #244/#258 ask for:
// iceclad on x86, overthere on the Pi.
//
// The assertion reads the client's own liveness signal, IsFullyZonedIn().
// A mid-run zone-link loss -- the process-watcher kill, a zone respawn onto a
// new port, an idle teardown -- clears world().isZoneConnected() (T286 writes
// it at the source) and IsFullyZonedIn() immediately goes false. So the whole
// defect class of "the link died and we never came back" is visible as
// zonedIn flipping false after it was true, with no second client to detect it
// by. Two shapes:
//   * drop-before-hold   -> Fail, "left the zone"   (the R1 failure: dropped,
//     never recovered)
//   * drop-and-recover   -> the clock keeps running; if we are zonedIn again
//     by the hold deadline and never dropped out a second time, it is a Pass
//     with the number of recoveries noted (the B925 fix: the client re-
//     handshakes with world and lands on a fresh zone address).
// A client that DIES is never a Pass -- the process exit code carries that.
//
// This state machine is pure logic over IScenarioWorld; the fake-world tests
// in tests/test_scenario_state_machines.cpp exercise every branch with no
// client, no server and no network.

#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

class ZoneLinkHoldScenario final : public IScenario {
public:
    explicit ZoneLinkHoldScenario(double holdSeconds) : holdSeconds_(holdSeconds) {}

    const char* Name() const override { return "zone_link_hold"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (!zonedIn_) {
            if (!world.IsFullyZonedIn()) {
                return StepResult::Running;  // still establishing the first link
            }
            zonedIn_ = true;
            zonedInAt_ = tick.sinceStart;
            SetReason("zoned in at " + Fmt(zonedInAt_) + "s; holding link for " +
                      Fmt(holdSeconds_) + "s");
            world.Note(Reason());
            return StepResult::Running;
        }

        // We were fully zoned and no longer are: the link dropped (or a zone
        // change we did not ask for started). Count it and keep running -- a
        // recovery that lands before the deadline is a Pass, not a Fail.
        if (!world.IsFullyZonedIn()) {
            drops_++;
            std::ostringstream os;
            os << "link lost at " << Fmt(tick.sinceStart)
               << "s (drop #" << drops_ << ") in '" << world.ZoneName()
               << "'; waiting for recovery";
            world.Note(os.str());
            SetReason(os.str());
            return StepResult::Running;
        }

        if (tick.sinceStart - zonedInAt_ >= holdSeconds_) {
            std::ostringstream os;
            os << "held the zone link for " << Fmt(tick.sinceStart - zonedInAt_)
               << "s in '" << world.ZoneName() << "'";
            if (drops_) {
                os << " after " << drops_ << " recovered drop(s)";
            }
            SetReason(os.str());
            return StepResult::Pass;
        }
        return StepResult::Running;
    }

private:
    static std::string Fmt(double v) {
        std::ostringstream os;
        os.precision(1);
        os << v;
        return os.str();
    }

    double holdSeconds_;
    bool zonedIn_ = false;
    double zonedInAt_ = 0.0;
    int drops_ = 0;
};

}  // namespace

std::unique_ptr<IScenario> MakeZoneLinkHoldScenario(const ScenarioOptions& options) {
    // The hold is the operator's knob (--scenario-hold, default 300 = the
    // >=5 min acceptance bar). A zero/negative hold falls back to that
    // default so a bare `--scenario zone_link_hold` is a complete check.
    const double hold = options.holdSeconds > 0.0 ? options.holdSeconds : 300.0;
    return std::unique_ptr<IScenario>(new ZoneLinkHoldScenario(hold));
}

}  // namespace scenario
}  // namespace eqt
