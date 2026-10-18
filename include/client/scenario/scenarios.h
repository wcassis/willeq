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
 * B894 (issue #210): #forage and #fish until the caught item is on the
 * cursor with its skill line. The forage landing is the hard leg; the
 * fishing catch is best-effort and only reported.
 */
std::unique_ptr<IScenario> MakeForageLandsScenario(const ScenarioOptions& options);

/**
 * B892 (issue #208): cast a levitation spell on ourselves and wait until the
 * client is actually levitating -- the buff in the buff list AND the
 * LevitationChanged-derived state. The caster never gets its own
 * AT_FLYMODE, so this is the lane for the self-levitation the official
 * client derives from its buffs.
 */
std::unique_ptr<IScenario> MakeLevitateAppliesScenario(const ScenarioOptions& options);

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
// scripts/scenario_configs/b630_analyze.py. See src/client/scenario/scenario_interrupted_cast.cpp.

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

// B760 (issue #45): the repeated cast-tail lane. The caster alternates 12
// let-finish casts with 12 GM-interrupted ones in one zone-in; its peer stands
// still (quiet) or sits/stands/moves/talks throughout (traffic). The claims
// are read off the DEBUG log by scripts/scenario_configs/b760_tail_analyze.py.
// See src/client/scenario/scenario_cast_tail_repeat.cpp.

/** The caster, quiet condition (peer: cast_tail_quiet_peer). */
std::unique_ptr<IScenario> MakeCastTailRepeatQuietScenario(const ScenarioOptions& options);
/** The caster, traffic condition (peer: cast_tail_traffic_peer). */
std::unique_ptr<IScenario> MakeCastTailRepeatTrafficScenario(const ScenarioOptions& options);
/** The quiet peer: zones in and stands still for --scenario-hold seconds. */
std::unique_ptr<IScenario> MakeCastTailQuietPeerScenario(const ScenarioOptions& options);
/** The traffic peer: /sit /stand /move /jump /say every 0.25 s for the hold. */
std::unique_ptr<IScenario> MakeCastTailTrafficPeerScenario(const ScenarioOptions& options);

// UIGAP-live: the live legs owed by the wave-1 UI units. See
// src/client/scenario/scenario_ui_inventory.cpp and scenario_ui_social.cpp.

/** #151: general -> bag -> bank -> general by /pickup /putdown and clicks. */
std::unique_ptr<IScenario> MakePickupRoundtripScenario(const ScenarioOptions& options);
/** #151 two-client: the peer moves a weapon bag -> primary by two clicks. */
std::unique_ptr<IScenario> MakeClickEquipActorScenario(const ScenarioOptions& options);
/** Its observer: the peer's primary material, as the server reported it. */
std::unique_ptr<IScenario> MakeClickEquipObservedScenario(const ScenarioOptions& options);
/** #215: sewing-kit and world-forge combines; results on the cursor queue. */
std::unique_ptr<IScenario> MakeCombineSewingScenario(const ScenarioOptions& options);
/** #214: /train <id>; value and points move only on OP_SkillUpdate. */
std::unique_ptr<IScenario> MakeTrainSkillScenario(const ScenarioOptions& options);
/** #213: /friends <peer>, /friendswho. Peer = --scenario-cast-target. */
std::unique_ptr<IScenario> MakeFriendsWhoScenario(const ScenarioOptions& options);
/** #213: the ignored peer's tell is delivered but not shown. */
std::unique_ptr<IScenario> MakeIgnoreTellScenario(const ScenarioOptions& options);
/** The peer for friends_who / ignore_tell: two tells to the main character. */
std::unique_ptr<IScenario> MakeSocialPeerScenario(const ScenarioOptions& options);
/** #211: Ranger tracking list, 2 s direction cadence, lost-target 12102. */
std::unique_ptr<IScenario> MakeTrackListScenario(const ScenarioOptions& options);
/** Its peer: stays, walks off, camps out. */
std::unique_ptr<IScenario> MakeTrackPeerScenario(const ScenarioOptions& options);
/** B888 (issue #204): with --create-character, the created character is in
 *  world, in its home city, never the tutorial. */
std::unique_ptr<IScenario> MakeCreateCharacterScenario(const ScenarioOptions& options);

// UIGAP-live2: the live legs owed by B896 / B442b / UIGAP-small. See
// src/client/scenario/scenario_ui_trade.cpp.

/** #212: dagger + 5 pp to the peer by a world click, both press Trade; a
 *  second trade cancelled (eqstr 1450). Peer = --scenario-cast-target. */
std::unique_ptr<IScenario> MakeTradeRoundtripScenario(const ScenarioOptions& options);
/** Its partner: auto-ack, accept after the actor, see the cancel. */
std::unique_ptr<IScenario> MakeTradePeerScenario(const ScenarioOptions& options);
/** #212: a Gnoll Fang to Captain Tillin through the give window. */
std::unique_ptr<IScenario> MakeGiveNpcScenario(const ScenarioOptions& options);
/** #151: drop and destroy confirmations; purse and bank coin by OP_MoveCoin. */
std::unique_ptr<IScenario> MakeDropDestroyCoinScenario(const ScenarioOptions& options);
/** #26: /bank change takes both purses to the fewest coins. */
std::unique_ptr<IScenario> MakeBankChangeScenario(const ScenarioOptions& options);

// B1005 (#324): /camp. src/client/scenario/scenario_camp.cpp.

/** /camp: nothing leaves before the countdown, then the character-select
 *  state with a FRESH list, then Enter World again and zone in. */
std::unique_ptr<IScenario> MakeCampToCharselectScenario(const ScenarioOptions& options);
/** /camp desktop: the client stops only after the countdown and the server's
 *  logout reply (exit 0 through IScenario::OnClientStopped). */
std::unique_ptr<IScenario> MakeCampDesktopScenario(const ScenarioOptions& options);

// B1072 (#392): the zone-in bound. src/client/scenario/scenario_zone_connect_timeout.cpp.

/** /camp, a dead zone port for two connects, Enter World: back at character
 *  select after the attempt + one retry, then Enter World again and zone in. */
std::unique_ptr<IScenario> MakeZoneConnectTimeoutScenario(const ScenarioOptions& options);

// B1081 (#401): the zone-change flash. src/client/scenario/scenario_zone_change_no_flash.cpp.

/** The screen is up for the fresh login; a GM /gmzone to zoneChangeTargetZone
 *  must not bring it back up -- Pass when Activations() stays at 1. */
std::unique_ptr<IScenario> MakeZoneChangeNoFlashScenario(const ScenarioOptions& options);

// B893: fall damage. src/client/scenario/scenario_fall_damage.cpp.

/** A synthetic drop through the client's own fall-damage sender; Pass when
 *  the server's HP change arrives within fallWaitSeconds. */
std::unique_ptr<IScenario> MakeFallDamageScenario(const ScenarioOptions& options);

// B900 (#216): the single ranged attack, live on the lane. GM-spawn a dummy,
// target it, fire ONE /range and wait for the SERVER's hit-or-miss verdict
// (a combat line naming the target); the lane's post-check asserts the DB
// ammo count dropped by one. See scenario_ranged_single_shot.cpp.
std::unique_ptr<IScenario> MakeRangedSingleShotScenario(const ScenarioOptions& options);

// B1025-H5 (#345): the hotbar lifecycle, live. src/client/scenario/
// scenario_hotbar_lifecycle.cpp. Four slots (spell/item/skill/social)
// assigned and activated by BOTH the click and the keyboard route, then
// /camp + relog: the three typed slots must be restored from the
// per-character INI (the emote slot is the documented free-text gap).
std::unique_ptr<IScenario> MakeHotbarLifecycleScenario(const ScenarioOptions& options);

// B1025-H5 (#345): hotbar_lifecycle's PEER -- acceptance C. The second
// character on the account (Testbuddy) zones in and assigns ONE skill slot
// through the same user path; the lane's post-check reads both per-character
// INIs to prove the main character's three typed slots did not leak into the
// peer's file (and vice versa). The verdict lives in the post-check, not here.
std::unique_ptr<IScenario> MakeHotbarIsolationAssignScenario(const ScenarioOptions& options);

// B1088 (#408): the loading screen's "Entering <zone long name>..." line,
// live. Zone in (the era table's first line), then a GM zone request to
// expectZoneChangeId (the second refresh); the post-check asserts both INFO
// lines. See scenario_loading_name.cpp.
std::unique_ptr<IScenario> MakeLoadingNameScenario(const ScenarioOptions& options);

}  // namespace scenario
}  // namespace eqt
