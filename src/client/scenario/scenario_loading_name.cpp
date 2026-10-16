// B1088 (GitLab #408) -- the loading screen's "Entering <zone long name>..."
// line, live on the lane.
//
// The client's own INFO line ([LOADING] Entering <name>...) is the assertion
// a lane can grep out of a plain log without pixels (step 4 iii), so the
// in-client half of the scenario is the transition itself:
//   1. zone in (the runner's pre-flight; zone 1, the era table's "South
//      Qeynos" -- the first INFO line lands at the character's enter-world
//      refresh),
//   2. SendCommand("/gmzone <self> <expectZoneId>") -- the GM zone request;
//      the server's OP_ZoneChange approval stores the pending zone and the
//      loading screen's line refreshes from the era table,
//   3. Pass once the client is fully zoned in to expectZoneId (the second
//      zone-in -- the post-check asserts BOTH INFO lines made it to the log).
//
// A non-GM character gets "GM command and you were not in GM mode" back and
// never zones: the runner reports the verdict after the timeout, so the lane
// fails loudly instead of hanging.

#include "client/scenario/scenarios.h"

#include <sstream>

namespace eqt {
namespace scenario {
namespace {

class LoadingNameScenario final : public IScenario {
public:
    explicit LoadingNameScenario(const ScenarioOptions& options)
        : expectZoneId_(options.expectZoneChangeId), expectZoneName_(options.expectZoneName) {}

    const char* Name() const override { return "loading_name"; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (sentGmZone_ == 0) {
            // First Step: the client is fully zoned in to the INITIAL zone.
            // (The pre-flight waited for it.) Send the GM zone request to
            // the target -- the same path the official client's GM button
            // drives, and the same server-side OP_ZoneChange the loading
            // screen refreshes from.
            std::ostringstream cmd;
            cmd << "/gmzone " << world.PlayerName() << " " << expectZoneId_;
            world.Note("loading_name: sending " + cmd.str());
            world.SendCommand(cmd.str());
            sentGmZone_ = 1;
            return StepResult::Running;
        }

        // The GM zone change re-enters world and zone. When we are fully
        // zoned in to the TARGET again, the second zone-in happened and the
        // post-check can grep both lines.
        if (world.IsFullyZonedIn() &&
            (expectZoneName_.empty() || world.ZoneName() == expectZoneName_)) {
            std::ostringstream os;
            os << "zoned in to '" << world.ZoneName() << "' via the GM zone request "
               << "(post-check asserts the loading screen's INFO lines)";
            SetReason(os.str());
            return StepResult::Pass;
        }

        if (tick.sinceStart > 240.0) {
            SetReason("the GM zone change never completed (a non-GM character "
                      "is refused with 'GM mode' and stays in the initial zone)");
            return StepResult::Fail;
        }
        return StepResult::Running;
    }

private:
    int sentGmZone_ = 0;
    uint32_t expectZoneId_;
    std::string expectZoneName_;
};

}  // namespace

std::unique_ptr<IScenario> MakeLoadingNameScenario(const ScenarioOptions& options) {
    return std::unique_ptr<IScenario>(new LoadingNameScenario(options));
}

}  // namespace scenario
}  // namespace eqt
