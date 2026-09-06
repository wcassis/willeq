#include "client/scenario/scenario_runner.h"

#include <algorithm>
#include <sstream>

namespace eqt {
namespace scenario {

const char* ScenarioExitCodeName(int code) {
    switch (code) {
        case kScenarioPass:          return "pass";
        case kScenarioFail:          return "fail";
        case kScenarioTimeout:       return "timeout";
        case kScenarioCouldNotStart: return "could-not-start";
        default:                     return "unknown";
    }
}

ScenarioRunner::ScenarioRunner(std::unique_ptr<IScenario> scenario, IScenarioWorld& world,
                               double timeoutSeconds, double startDeadlineSeconds)
    : scenario_(std::move(scenario)),
      world_(world),
      timeoutSeconds_(timeoutSeconds > 0.0 ? timeoutSeconds : 300.0),
      startDeadlineSeconds_(0.0) {
    // A start deadline longer than the whole timeout is meaningless: the run
    // would be reported as a timeout while it was still waiting to start,
    // which is exactly the confusion exit code 3 exists to remove.
    const double requested = startDeadlineSeconds > 0.0 ? startDeadlineSeconds : 120.0;
    startDeadlineSeconds_ = std::min(requested, timeoutSeconds_);
    if (!scenario_) {
        Finish(kScenarioCouldNotStart, "no scenario was constructed");
    }
}

void ScenarioRunner::Finish(int code, std::string reason) {
    if (finished_) {
        return;
    }
    finished_ = true;
    code_ = code;
    reason_ = std::move(reason);
    std::ostringstream os;
    os << "VERDICT " << ScenarioExitCodeName(code_) << " (exit " << code_ << ") after "
       << elapsed_ << "s: " << reason_;
    world_.Note(os.str());
}

bool ScenarioRunner::Tick(float deltaTime) {
    if (finished_) {
        return false;
    }
    elapsed_ += static_cast<double>(deltaTime);

    if (!started_) {
        if (world_.IsFullyZonedIn()) {
            started_ = true;
            startedAt_ = elapsed_;
            std::ostringstream os;
            os << "scenario '" << scenario_->Name() << "' starting: zoned in to '"
               << world_.ZoneName() << "' after " << elapsed_ << "s";
            world_.Note(os.str());
        } else if (elapsed_ >= startDeadlineSeconds_) {
            std::ostringstream os;
            os << "never reached a zone within " << startDeadlineSeconds_
               << "s (connected=" << (world_.IsConnected() ? "yes" : "no") << ")";
            Finish(kScenarioCouldNotStart, os.str());
            return false;
        } else {
            return true;
        }
    }

    ScenarioTick tick;
    tick.deltaTime = deltaTime;
    tick.sinceStart = elapsed_ - startedAt_;

    const StepResult result = scenario_->Step(world_, tick);
    if (result == StepResult::Pass) {
        Finish(kScenarioPass, scenario_->Reason());
        return false;
    }
    if (result == StepResult::Fail) {
        Finish(kScenarioFail, scenario_->Reason());
        return false;
    }

    if (elapsed_ >= timeoutSeconds_) {
        std::ostringstream os;
        os << "no verdict within " << timeoutSeconds_ << "s (scenario '"
           << scenario_->Name() << "' still running";
        if (!scenario_->Reason().empty()) {
            os << ": " << scenario_->Reason();
        }
        os << ")";
        Finish(kScenarioTimeout, os.str());
        return false;
    }
    return true;
}

}  // namespace scenario
}  // namespace eqt
