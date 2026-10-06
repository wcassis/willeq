#pragma once

// UIGAP-live: a small sequential-step engine for the UI live legs.
//
// Each leg is a list of Steps. A step ACTS once (sends its commands, or
// clicks), then waits until its `done` predicate holds, failing the run if
// its `fail` predicate ever reports a reason first or its deadline passes.
// Every predicate reads only IScenarioWorld, so the whole leg runs against
// the FakeWorld in tests/test_scenario_state_machines.cpp.
//
// Private to src/client/scenario/ (the two scenario_ui_*.cpp TUs); nothing
// outside the lane includes it.

#include "client/scenario/scenario.h"

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {
namespace steps {

struct Step {
    std::string label;
    /** Runs once, the first tick the step is current. May be empty. */
    std::function<void(IScenarioWorld&)> act;
    /** True when the step's assertion holds. Empty = done right after act. */
    std::function<bool(IScenarioWorld&)> done;
    /** Non-empty = fail NOW with this reason (checked before `done`). */
    std::function<std::string(IScenarioWorld&)> fail;
    /** Describes the current state for a deadline failure. */
    std::function<std::string(IScenarioWorld&)> describe;
    double deadlineSeconds = 20.0;
    /** Seconds to keep still after `done` holds (lets the server persist). */
    double dwellSeconds = 0.0;
};

/** True when any chat line from `first` on contains `needle` and is not the
 *  echo of a typed command (the command processor echoes "/cmd args"). */
inline bool ChatHas(IScenarioWorld& w, size_t first, const std::string& needle) {
    const size_t n = w.ChatLineCount();
    for (size_t i = first; i < n; ++i) {
        const std::string line = w.ChatLine(i);
        const size_t bar = line.find('|', line.find('|') + 1);
        const std::string text = bar == std::string::npos ? line : line.substr(bar + 1);
        if (!text.empty() && text[0] == '/') continue;  // the typed-command echo
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}

class StepScenario : public IScenario {
public:
    StepScenario(const char* name, double settleSeconds, std::vector<steps::Step> steps)
        : name_(name), settleSeconds_(settleSeconds), steps_(std::move(steps)) {}

    const char* Name() const override { return name_; }

    StepResult Step(IScenarioWorld& world, const ScenarioTick& tick) override {
        if (tick.sinceStart < settleSeconds_) return StepResult::Running;
        if (!world.IsFullyZonedIn() && !allowZoneOut_) {
            SetReason("left the zone at step " + std::to_string(index_) + " (" + CurrentLabel() + ")");
            return StepResult::Fail;
        }
        while (index_ < steps_.size()) {
            auto& s = steps_[index_];
            if (!acted_) {
                acted_ = true;
                stepStart_ = tick.sinceStart;
                doneAt_ = -1.0;
                if (s.act) s.act(world);
                world.Note("step " + std::to_string(index_) + " '" + s.label + "' started at " +
                           Fmt(tick.sinceStart) + "s");
            }
            if (doneAt_ < 0.0) {
                if (s.fail) {
                    const std::string why = s.fail(world);
                    if (!why.empty()) {
                        SetReason("step " + std::to_string(index_) + " '" + s.label + "': " + why);
                        return StepResult::Fail;
                    }
                }
                if (!s.done || s.done(world)) {
                    doneAt_ = tick.sinceStart;
                    world.Note("step " + std::to_string(index_) + " '" + s.label + "' held after " +
                               Fmt(tick.sinceStart - stepStart_) + "s" +
                               (s.describe ? " -- " + s.describe(world) : std::string()));
                } else if (tick.sinceStart - stepStart_ >= s.deadlineSeconds) {
                    SetReason("step " + std::to_string(index_) + " '" + s.label +
                              "' did not hold within " + Fmt(s.deadlineSeconds) + "s" +
                              (s.describe ? ": " + s.describe(world) : std::string()));
                    return StepResult::Fail;
                } else {
                    return StepResult::Running;
                }
            }
            // done: dwell, re-checking the assertion still holds (a refused
            // server move answers with a resync that UNDOES the model) and
            // that the fail predicate stays quiet (a "must NOT happen" window).
            if (s.fail) {
                const std::string why = s.fail(world);
                if (!why.empty()) {
                    SetReason("step " + std::to_string(index_) + " '" + s.label + "' (dwell): " + why);
                    return StepResult::Fail;
                }
            }
            if (s.done && !s.done(world)) {
                SetReason("step " + std::to_string(index_) + " '" + s.label +
                          "' held and then REVERTED during the dwell" +
                          (s.describe ? ": " + s.describe(world) : std::string()));
                return StepResult::Fail;
            }
            if (tick.sinceStart - doneAt_ < s.dwellSeconds) return StepResult::Running;
            ++index_;
            acted_ = false;
        }
        SetReason(Summary(world));
        world.Note("PASS: " + Reason());
        return StepResult::Pass;
    }

protected:
    /** The pass line; a leg overrides it to print its MODEL line. */
    virtual std::string Summary(IScenarioWorld&) {
        return std::to_string(steps_.size()) + " steps held";
    }
    void AllowZoneOut() { allowZoneOut_ = true; }

    static std::string Fmt(double v) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(1);
        os << v;
        return os.str();
    }

private:
    std::string CurrentLabel() const {
        return index_ < steps_.size() ? steps_[index_].label : std::string("end");
    }

    const char* name_;
    double settleSeconds_;
    std::vector<steps::Step> steps_;
    size_t index_ = 0;
    bool acted_ = false;
    double stepStart_ = 0.0;
    double doneAt_ = -1.0;
    bool allowZoneOut_ = false;
};

}  // namespace steps
}  // namespace scenario
}  // namespace eqt
