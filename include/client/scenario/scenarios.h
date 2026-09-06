#pragma once

// T410: constructors for the three shipped scenarios. Only
// scenario_registry.cpp and the state-machine tests name these; everything
// else goes through MakeScenario().

#include "client/scenario/scenario.h"

namespace eqt {
namespace scenario {

/** login -> zone -> IsFullyZonedIn. Replaces every "did it zone in?" rig. */
std::unique_ptr<IScenario> MakeZoneInScenario(const ScenarioOptions& options);

/** T391's pair: a second player arrives and both hold for holdSeconds. */
std::unique_ptr<IScenario> MakeTwoClientSurviveScenario(const ScenarioOptions& options);

/** Gem-cast a spell and wait for the landing (T348's OP_Action buff path). */
std::unique_ptr<IScenario> MakeCastLandsScenario(const ScenarioOptions& options);

}  // namespace scenario
}  // namespace eqt
