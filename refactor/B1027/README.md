# B1027 (#347): headless ConsoleRenderer target HP stays frozen -- publish EntityStatsChanged on the state EventBus

**Status: CLOSED by this unit** (`task/kb-b1027-console-target-hp-publisher`,
cut at `origin/fable` `0136137b`, 2026-10-12). Found by AUDIT-WAVE8
(`review/AUDIT-WAVE8/sections/b602.md` finding 1).

## The defect, re-derived

T319 wired `ConsoleRenderer::updateCurrentTargetHP()` to
`GameEventType::EntityStatsChanged` on the STATE EventBus
(`src/client/output/console_renderer.cpp:113` subscribe, `:193` case --
filtered to the current target). The only src publisher of that enumerator
was the RENDERER-BRIDGE `pushEvent` in
`EntitiesComponent::ZoneProcessMobHealth`
(`src/client/entities/entities_component_appearance.cpp:200-207` at the
cut tip). Those are two disjoint channels:

- the STATE EventBus (`state::GameState::events()`,
  `include/client/state/game_state.h:61`) is what a headless
  `ConsoleRenderer` subscribes to;
- the renderer bridge (`GameStateBridge::pushEvent`,
  `include/client/bridge/game_state_bridge.h:36`) is drained by
  `IrrlichtBridge` (graphics) or `ConsoleBridge` (headless) -- and
  `ConsoleBridge::applyEvent` is LOG-only: it names
  `EntityStatsChanged` nowhere (measured: 0 occurrences in
  `src/client/bridge/console_bridge.cpp`) and calls no renderer method, so
  it never reaches a `ConsoleRenderer`.

`combat_state.cpp:76-84` states the invariant: nothing forwards between the
two channels. `CombatState::setTarget` fires `TargetChanged` only on an id
change (`combat_state.cpp:12-20`), so once a target was acquired the
status line's HP stayed at the percentage that `TargetChanged` happened to
carry. [measured: the RED test below, `seen = 0` state-bus events after an
OP_MobHealth replay]

B602 (`refactor/B602/README.md`) deleted the last state-bus publisher
(`EntityManager::fireEntityStatsChangedEvent`, genuinely zero-caller) and
claimed the bridge path fed the `ConsoleRenderer` -- the false premise this
unit corrects there.

## The fix

`ZoneProcessMobHealth` now publishes `EntityStatsChanged` on the state bus
(`ctx_.game_state.events().publish(...)`), right above its unchanged bridge
`pushEvent` -- `src/client/entities/entities_component_appearance.cpp:201-218`
at the head of this unit (state-bus publish line 216; bridge publish line 228;
the line numbers moved +20 from the cut tip). The handler already did the
equivalent state-layer writes there (the T221 "source-side writes":
`combat().setTarget`, `pet().updatePetStats`, lines 188-196 at the cut tip),
so the publish sits with its siblings rather than being routed through
`CombatState` -- whose two-channel comment is about `TargetChanged`
(`CombatManager` -> `CombatState` -> bus), which is unchanged.

Routing decision (card step 2 offered both): direct publish from the
handler, NOT `CombatState`. `EntityStatsChanged` is a whole-entity event
(payload: spawnId + hp, `event_payloads_entity.h:98`); it is not target
state, and `CombatState` holds no generic-entity role.

**Exactly one state-bus publisher** of the enumerator, at the head
[measured, grep `GameEventType::EntityStatsChanged` over src + include:
5 occurrences tree-wide -- the case in
`src/client/bridge/irrlicht_bridge.cpp:97`, the state-bus publish in
`src/client/entities/entities_component_appearance.cpp:216` (THE
publisher), the unchanged bridge publish in
`src/client/entities/entities_component_appearance.cpp:228`, and the
subscribe + case in `src/client/output/console_renderer.cpp:113,193`].

The bridge publish (line 228) is byte-unchanged apart from position; the
`test_zone_entities` `MobHealthUpdatesPercentAndPushesStatsEvent` case still
passes (bridge leg intact). `test_reachability_sweep` 5/5 green at the head
[measured, local].

## RED / GREEN

RED -- `tests/test_console_target_hp_feed.cpp` (new, registered in
`tests/cmake/renderer_guards.cmake` after the T319 block) at
`origin/fable` (commit `fb284c02` on this branch, the RED commit; built
from origin/fable sources after a forced recompile of
`entities_component_appearance.cpp`):

```
[ RUN      ] ConsoleTargetHpFeedTest.StatusLineFollowsTheTargetsHpDownAfterMobHealth
...
tests/test_console_target_hp_feed.cpp:119: Failure
Expected: (StatusLine(console).find("Target: Grimjaw 37%")) != (std::string::npos), actual: 18446744073709551615 vs 18446744073709551615
The status line never learned the new HP: EntityStatsChanged has no publisher on the state EventBus (the MobHealth bridge publish feeds the render-thread bridge, not the bus ConsoleRenderer subscribes to).
[  FAILED  ] ConsoleTargetHpFeedTest.StatusLineFollowsTheTargetsHpDownAfterMobHealth (1 ms)
[ RUN      ] ConsoleTargetHpFeedTest.MobHealthPublishesEntityStatsChangedOnTheStateBusExactlyOnce
...
tests/test_console_target_hp_feed.cpp:156: Failure
Expected equality of these values:
  seen
    Which is: 0
  1u
    Which is: 1
expected EXACTLY ONE state-bus EntityStatsChanged per MobHealth packet (the bridge publish is a different channel and does not count); got 0
[  FAILED  ] ConsoleTargetHpFeedTest.MobHealthPublishesEntityStatsChangedOnTheStateBusExactlyOnce (0 ms)
[  PASSED  ] 0 tests.
[  FAILED  ] 2 tests
```
(full output: `review/AUDIT-WAVE8`'s evidence path does not exist on this
host; the raw run is `/home/user/kb-evidence/b1027-console-target-hp-publisher/RED_output.txt`
and attached to the kanban task.)

GREEN -- at the head of this unit [measured, local, binaries rebuilt after
the fix]:

```
[  PASSED  ] 2 tests.                     (test_console_target_hp_feed)
[  PASSED  ] 5 tests.                     (test_reachability_sweep)
[  PASSED  ] 12 tests. + 1 SKIPPED        (test_zone_entities; the skip is
                                           the pre-existing no-capture
                                           FullZoneStreamReplay)
[  PASSED  ] 3 tests.                     (test_console_target_hp, T319)
ctest -L hygiene: 100% tests passed, 0 failed out of 29
```

`ctest -L hygiene` note: the first local run needed `generate_textures`,
`spell_sprite_atlas_builder`, `item_icon_atlas_builder`, `verify_eqe1`,
`willeq_db_builder`, `eqe1_builder` and `ui_atlas_builder` built (fresh
build tree); their absence is the only cause of the initial
`TexturesReproducible`/`PackReproducible` failures -- byte pins unchanged.

## Not verified / left open

- The CI test job's `Passed` line for `test_console_target_hp_feed` is
  pending the pipeline at this head (card acceptance D) -- the summary
  carries it once the pipeline lands.
- `include/client/state/CLAUDE.md`'s per-module table does not name
  `EntityStatsChanged`'s publisher, so no protected-file edit is needed
  there; if a later unit adds an EventBus publisher line to that table,
  the `ZoneProcessMobHealth` publish is the one to name.
