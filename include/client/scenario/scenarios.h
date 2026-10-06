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
 * B925 (issue #244/#258): zone in and keep the link for holdSeconds. A single
 * client -- the plain "does the zone link survive a long load" check. A
 * mid-run drop that recovers before the deadline passes; one that does not,
 * or a dead process, does not.
 */
std::unique_ptr<IScenario> MakeZoneLinkHoldScenario(const ScenarioOptions& options);

/**
 * T494 (board B796): the OBSERVER half of the /equip proof. Sends nothing;
 * asserts the PEER's primary equipment material changed, which only the
 * server's OP_WearChange can do.
 */
std::unique_ptr<IScenario> MakeEquipObservedScenario(const ScenarioOptions& options);

/** Its peer: settles, runs one `/equip <inventory.slot_id> 13`, then exits. */
std::unique_ptr<IScenario> MakeEquipActorScenario(const ScenarioOptions& options);

/** The peer's SECOND login: sends nothing, just stays visible to be observed. */
std::unique_ptr<IScenario> MakeEquipActorReturnScenario(const ScenarioOptions& options);

// B630 (issue #48) + B760 (issue #45): the two-client interrupted-cast lane.
// Each caster leg is a driver that runs its command sequence on a calibrated
// clock; the assertions are read off the run's DEBUG log by
// scripts/ci/b760_analyze.py. See src/client/scenario/scenario_interrupted_cast.cpp.

/** Leg A: cast a 3 s spell and GM-`#interrupt` it mid-cast (caster side). */
std::unique_ptr<IScenario> MakeCastInterruptScenario(const ScenarioOptions& options);
/** D1: cast a nuke and let it finish -- the B760 SUCCESS tail. */
std::unique_ptr<IScenario> MakeCastSuccessTailScenario(const ScenarioOptions& options);
/** D2: cast twice into the server recast window -- the B760 RECAST stop. */
std::unique_ptr<IScenario> MakeCastRecastStopScenario(const ScenarioOptions& options);
/** D3: lay a stronger same-resist blocker, then cast a buff that won't take hold. */
std::unique_ptr<IScenario> MakeCastBuffNotakeScenario(const ScenarioOptions& options);
/** The caster for the lane: runs legs A, D1, D2, D3 in one zone-in. */
std::unique_ptr<IScenario> MakeCastInterruptChainScenario(const ScenarioOptions& options);
/** The observer: targets the caster and records what a bystander receives. */
std::unique_ptr<IScenario> MakeCastInterruptObserveScenario(const ScenarioOptions& options);
/** Leg B: the non-GM caster loops gem casts until the fizzle roll lands. */
std::unique_ptr<IScenario> MakeCastFizzleScenario(const ScenarioOptions& options);

}  // namespace scenario
}  // namespace eqt
