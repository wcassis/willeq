// T410 (board B138 + B139): where the scenario lane attaches to the client.
//
// This is the whole integration. It lives in its own TU rather than in
// application.cpp because the lane is opt-in machinery, not part of the
// application's lifecycle: an ordinary run constructs nothing here.

#include "client/application.h"
#include "client/eq.h"
#include "client/mode/automated_mode.h"
#include "client/scenario/scenario_world_app.h"
#include "client/scenario/scenarios.h"
#include "common/logging.h"

namespace eqt {

bool Application::initScenario(const ApplicationConfig& config) {
    if (config.scenarioName.empty()) {
        return true;  // ordinary run -- the lane does not exist
    }

    scenario::ScenarioOptions options;
    options.holdSeconds = config.scenarioHoldSeconds;
    options.castSpellId = config.scenarioSpellId;
    options.expectSelfMaterial = config.scenarioExpectMaterial;

    auto built = scenario::MakeScenario(config.scenarioName, options);
    if (!built) {
        // parseArguments/main validate the name before we get here, so this
        // is a belt-and-braces refusal rather than the operator-facing error.
        LOG_FATAL(MOD_SCENARIO, "Unknown scenario '{}'", config.scenarioName);
        return false;
    }

    // The lane REQUIRES automated mode: that is the only mode whose update()
    // carries a per-tick automation callback, and --scenario sets the mode
    // itself (application_config.cpp) so this can only fail if something
    // else overrode it afterwards.
    if (!m_gameMode || m_gameMode->getMode() != mode::OperatingMode::Automated) {
        LOG_FATAL(MOD_SCENARIO,
            "--scenario requires automated mode (mode is '{}') -- the runner has "
            "no per-tick hook otherwise",
            m_gameMode ? m_gameMode->getModeName() : "none");
        return false;
    }
    auto* automated = static_cast<mode::AutomatedMode*>(m_gameMode.get());

    if (!m_eqClient) {
        LOG_FATAL(MOD_SCENARIO, "--scenario has no EverQuest client to observe");
        return false;
    }

    // MOD_SCENARIO is pinned to INFO so the verdict trace survives the quiet
    // default and any --log-level the harness passes for other reasons. A
    // lane whose result can be silenced by a logging flag is not a lane.
    SetModuleLogLevel(MOD_SCENARIO, LOG_INFO);

    m_scenarioWorld = std::make_unique<scenario::ApplicationScenarioWorld>(*this, *m_eqClient);
    m_scenarioRunner = std::make_unique<scenario::ScenarioRunner>(
        std::move(built), *m_scenarioWorld,
        config.scenarioTimeoutSeconds, config.scenarioStartDeadlineSeconds);

    // B139: AutomatedMode's per-tick hook had zero registrants in src/,
    // include/, tests/ and benchmarks/ since it was written. This is its
    // caller. Returning false from the callback is AutomatedMode's own
    // contract for "stop": it sets m_quitRequested, the game thread's
    // isQuitRequested() check unwinds the run normally, and nothing calls
    // exit() from inside a tick.
    scenario::ScenarioRunner* runner = m_scenarioRunner.get();
    automated->setAutomationCallback([runner](float deltaTime) {
        return runner->Tick(deltaTime);
    });

    LOG_INFO(MOD_SCENARIO,
        "scenario lane armed: name='{}' timeout={}s start-deadline={}s hold={}s",
        config.scenarioName, config.scenarioTimeoutSeconds,
        config.scenarioStartDeadlineSeconds, config.scenarioHoldSeconds);
    return true;
}

int Application::scenarioExitCode() const {
    return m_scenarioRunner ? m_scenarioRunner->exitCode() : scenario::kScenarioPass;
}

std::string Application::scenarioReason() const {
    return m_scenarioRunner ? m_scenarioRunner->reason() : std::string();
}

std::string Application::scenarioName() const {
    return m_scenarioRunner ? std::string(m_scenarioRunner->scenarioName()) : std::string();
}

}  // namespace eqt
