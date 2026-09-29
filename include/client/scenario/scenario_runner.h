#pragma once

// T410 (board B138/B139): the runner that drives one scenario from the
// automated mode's per-tick hook, and the process exit-code taxonomy.

#include "client/scenario/scenario.h"

#include <memory>
#include <string>

namespace eqt {
namespace scenario {

/**
 * The exit codes `--scenario` contracts with. main() returns exactly one of
 * these, and scripts/scenario.sh reads them; nothing else may be returned
 * from a scenario run.
 */
enum ScenarioExitCode : int {
    kScenarioPass = 0,           // the scenario asserted and the assertion held
    kScenarioFail = 1,           // the scenario ran and its assertion failed
    kScenarioTimeout = 2,        // zoned in, but no verdict inside --timeout
    kScenarioCouldNotStart = 3,  // never reached a zone (no server, bad login, ...)
};

const char* ScenarioExitCodeName(int code);

/**
 * B918 (issue #237): the exit code of a run OUTSIDE --scenario that ended
 * because of a FATAL that takes the ordinary quit path (today: the zone
 * loading thread failing -- checkLoadingComplete's "loading thread failed" --
 * and ZoneComponent::LoadZoneSources(); include/client/fatal_exit.h). The
 * FATALs that std::exit(1) were already non-zero.
 * Before this such a run returned 0, indistinguishable from a clean /q or
 * /camp. 4 collides with nothing: the scenario taxonomy above is 0..3, 1 is
 * main()'s own bad-arguments / failed-initialize code, and
 * scripts/scenario.sh + the eqemu selftests use 64..72.
 */
constexpr int kClientFatalExit = 4;

/**
 * main()'s exit code, decided in one place.
 *  - --scenario: the scenario taxonomy wins and nothing outside 0..3 is ever
 *    returned. A FATAL that ended the run before a verdict already reads as
 *    kScenarioTimeout (the unfinished-run code); a pass can never be reported
 *    for a run that died on a FATAL.
 *  - otherwise: kClientFatalExit when the run ended on a FATAL, else 0 (a
 *    clean /q, /camp, window close or SIGTERM).
 */
int ProcessExitCode(bool scenarioRequested, int scenarioCode, bool fatalShutdown);

/**
 * Drives one scenario.
 *
 * Tick() is the automation callback: it returns FALSE once there is a
 * verdict, which is AutomatedMode's contract for "stop the client" (it sets
 * m_quitRequested, the game thread sees isQuitRequested() and unwinds
 * normally -- no exit() from inside a tick).
 *
 * The runner, not the scenario, owns the pre-flight. Until the client is
 * fully zoned in it does not call Step() at all; if that never happens
 * within startDeadlineSeconds the verdict is kScenarioCouldNotStart. That
 * keeps "the server was down" from ever being reported as a scenario
 * failure, which is the distinction a CI lane actually needs.
 */
class ScenarioRunner {
public:
    ScenarioRunner(std::unique_ptr<IScenario> scenario, IScenarioWorld& world,
                   double timeoutSeconds, double startDeadlineSeconds);

    /** One game tick. Returns false once finished. */
    bool Tick(float deltaTime);

    bool finished() const { return finished_; }
    bool started() const { return started_; }
    double elapsed() const { return elapsed_; }

    /**
     * The process exit code.
     *
     * The external-quit guard: if the client stopped for any reason other
     * than this runner (a /quit, a SIGTERM, a crash-free early camp) the
     * runner is still unfinished, and an unfinished run reports
     * kScenarioTimeout -- never 0. A scenario lane that can report success
     * because something else ended the process is worse than no lane.
     */
    int exitCode() const { return finished_ ? code_ : kScenarioTimeout; }
    const std::string& reason() const { return reason_; }
    const char* scenarioName() const { return scenario_ ? scenario_->Name() : "(none)"; }

private:
    void Finish(int code, std::string reason);

    std::unique_ptr<IScenario> scenario_;
    IScenarioWorld& world_;
    double timeoutSeconds_;
    double startDeadlineSeconds_;

    double elapsed_ = 0.0;
    double startedAt_ = 0.0;
    bool started_ = false;
    bool finished_ = false;
    int code_ = kScenarioTimeout;
    std::string reason_ =
        "scenario did not reach a verdict before the client exited";
};

}  // namespace scenario
}  // namespace eqt
