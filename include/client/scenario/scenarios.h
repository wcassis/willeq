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

/**
 * T494 (board B796): the OBSERVER half of the /equip proof. Sends nothing;
 * asserts the PEER's primary equipment material changed, which only the
 * server's OP_WearChange can do.
 */
std::unique_ptr<IScenario> MakeEquipObservedScenario(const ScenarioOptions& options);

/** Its peer: settles, runs one `/equip <inventory.slot_id> 13`, then holds. */
std::unique_ptr<IScenario> MakeEquipActorScenario(const ScenarioOptions& options);

}  // namespace scenario
}  // namespace eqt
