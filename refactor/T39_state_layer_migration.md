# T39 — Finish the Phase 7.x state-layer migration for combat/inventory

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg (lock protocol
applies).** No sibling unit.

**Territory: `src/client/state/combat_state.cpp` +
`src/client/state/inventory_state.cpp` (and their headers), the call
sites in `src/client/eq*.cpp` that own the authoritative target /
inventory-slot flow, `src/client/action/command_processor.cpp`'s
`cmdHail`, and the T02/T28 tests in `tests/test_state_modules*.cpp`.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred.md entry **"Dead
parallel state copies in the state layer, WITH LIVE READERS"** — read it
to the end, it carries a 2026-08-04 correction that materially shrinks
this unit — then `refactor/T28_state_layer_readers.md`'s Review.

Will's ruling 2026-08-03: **"I still want the migration but doing b now
and a later sounds correct."** T28 did (b). This is (a).

## What is actually left, and what the correction changed

The "Phase 7.x — sync to XState" migration was left half-done: setters
were written but never wired, while readers were repointed at the new
home anyway. T28 stopped the bleeding by repointing four `eq_commands.cpp`
readers at the authoritative `CombatManager`, deliberately **without**
deleting the state-layer scaffolding, so this unit has something to build
on.

**Correction that shapes this unit** (orchestrator, 2026-08-04 — verified,
in the deferred entry): an earlier version of that entry warned that
letting `CombatState::fireTargetChangedEvent()` publish would
double-publish, because `TargetChanged` is already pushed from three
sites. **That was wrong.** Those three sites push onto the
**GameStateBridge** (consumed by `IrrlichtBridge` and `ConsoleBridge`);
`CombatState` would publish onto the **EventBus** (consumed by
`ConsoleRenderer`). Nothing else publishes `TargetChanged` to the
EventBus, the bridges do not subscribe to it, and nothing forwards
between the channels — the EventBus is fed exclusively by
`src/client/state/*.cpp`.

So this is **"populate the state and let it publish"**, not "consolidate
three publishers". Confirm that channel separation yourself early; if you
find any consumer reading both, STOP, because the whole shape changes.

## Scope

### 1. Populate `CombatState`'s target from the authoritative flow

`combat().setTarget()` / `clearTarget()` / `setAutoAttacking()` /
`updateTargetHP()` have zero production callers. Wire them where the real
target changes — the same places that already push `TargetChanged` onto
the bridge are the obvious candidates. Keep `CombatManager` authoritative;
the state layer mirrors it. Do not invert that in this unit.

### 2. Let the stubs publish

With state populated, `fireTargetChangedEvent()` and `fireCombatEvent()`
become meaningful. Publish real data and delete the T28 "deliberately
non-publishing" comments, which will no longer be true.

### 3. Same for `InventoryState`'s occupancy half

`setEquipmentOccupied()` / `setGeneralOccupied()` / `setBankOccupied()`
also have zero production callers. T28 already made
`fireInventoryChangedEvent()` report the real occupied value instead of a
hardcoded `true`, so this is the wiring half. If the authoritative
inventory flow turns out not to have a clean place to call these, say so
and leave them — do not invent a synthetic call site to make the arrays
look populated.

### 4. Claim the fifth reader

`action/command_processor.cpp:771` (`cmdHail`) reads
`m_state.combat().hasTarget()`. T28 left it deliberately untouched with a
comment, because `CommandProcessor` holds only `state::GameState&` and
`ActionDispatcher&` by design (T12 decoupled the action layer) and wiring
`CombatManager` in would have fought this migration. Once `CombatState`
is populated, that reader is correct as written — **verify that and
remove the T28 comment**. That is the payoff for having left it alone.

### 5. Tests

`CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent` (T02,
kept-and-renamed by T28) now describes fixed behaviour — re-point it.
Add coverage that the events carry real data, and at least one test that
would have failed before this unit.

## STOP conditions

- Any consumer turns out to read **both** the bridge and the EventBus for
  these events → STOP; the channel separation this unit relies on is
  wrong and the design needs a ruling.
- Populating the state requires making `CombatState` authoritative over
  `CombatManager` (rather than mirroring it) → STOP; that is a much
  larger change than Will approved.
- The inventory half has no clean authoritative call site → do items 1/2/4
  and report on 3; do not force it.
- Any gameplay behaviour change beyond event delivery → STOP. Populating
  state and publishing on a channel whose only consumer is
  `ConsoleRenderer` should be observable in headless output, not in play.

## Verification bar

1. `cmake --build <slot>/build -j24` clean; also build the headless
   configure (`EQT_GRAPHICS=OFF`) — `ConsoleRenderer` is the live consumer
   of the channel you are populating.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). **RDP is no longer
   waived (T35) and the golden replay no longer skips (T36)** — a failure
   or a `Skipped` on either is a real signal, not background noise.
3. Golden replay unchanged and confirmed to have RUN.
4. **Acceptance**: a live session (eqemu LOCK FIRST; `up.sh` only;
   restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190)
   driven by piped slash commands, showing (a) `/hail` now taking the
   target branch when a target is set — the concrete user-visible payoff
   — and (b) target changes reaching `ConsoleRenderer`'s output. Evidence
   under `benchmarks/audits/T39/`.
5. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that is a failed verification.

## Review

**Channel-separation confirmation (done first, per the plan).** Verified by
reading, not re-derived from the deferred entry: `eq_bridge.cpp`'s three
`TargetChanged` pushes (`OnSpawnRemovedGraphics` clear, `ProcessBridgeIntents`
TargetIntent set, `eq_entities.cpp:1728` HP update) all call
`m_bridge->pushEvent(...)` onto `GameStateBridge`. `console_bridge.cpp`
handles `GameEventType::TargetChanged` (line 75) but has zero references to
`EventBus` anywhere in the file — confirmed by grep. `irrlicht_bridge.cpp`
has zero `EventBus` references either. `console_renderer.cpp` subscribes to
`TargetChanged` on `state.events()` (the `EventBus`), and nothing forwards
between the two queues. Channel separation holds exactly as the corrected
deferred entry says. STOP condition ("any consumer reads both") not
triggered.

**A second architectural finding that changed the wiring site (not a STOP,
but a real deviation from the plan's suggested location — documented in
full because it determines everything else below).** The plan suggested
wiring `combat().setTarget()`/etc. at "the same places that already push
`TargetChanged` onto the bridge" (`eq_bridge.cpp`/`eq_entities.cpp`, i.e.
inside `EverQuest`). Investigating those sites surfaced that
`EverQuest::m_game_state` (`include/client/eq.h:1163`, a plain value member)
and `Application::m_gameState` (`include/client/application.h:430`, a
separately heap-allocated `std::unique_ptr<state::GameState>`,
`application.cpp:178`) are **two distinct `state::GameState` instances**,
each auto-wiring its own submodules to its own `EventBus`
(`game_state.cpp:6-16`). `CommandProcessor` (and therefore `cmdHail`) is
constructed with `*m_gameState` (`application.cpp:266`), and
`HeadlessMode`/`GraphicalMode` — and therefore `ConsoleRenderer`, via
`HeadlessMode::initialize` → `m_renderer->connectToGameState(state)` — are
also initialized with `*m_gameState` (`application.cpp:302`,
`headless_mode.cpp:33`). So `Application::m_gameState` is the instance that
actually matters for this unit's payoff; `EverQuest::m_game_state` is a
second, disconnected copy that only a few of `EverQuest`'s own getters read
internally. Wiring `combat().setTarget()` at the `eq_bridge.cpp` sites as
suggested would have populated the WRONG instance — `cmdHail` and
`ConsoleRenderer` would have kept seeing zeros/no-events, and the plan's own
acceptance bar would have been unmet. Confirmed this wasn't an isolated
combat-specific issue: `Application::m_gameState->player()`'s
position/stats are essentially never set either (only vendor/banker/trainer
sub-state and a `zoneConnected` flag) — the "Phase 7 Migration Note" in
`syncGameStateFromClient()` about player/world being "synced directly from
EQ class packet handlers" turns out to mean `EverQuest::m_game_state`, not
`Application::m_gameState`. This is a second, likely wider, live instance of
the same "dead parallel state copy" pattern the deferred entry named, but
fixing it beyond combat/inventory is out of this unit's scope — noting it
here as a finding, not fixing it. Not a STOP: `CombatManager` stays
authoritative either way, this only changes which `GameState` instance
mirrors it, and it is what the plan's own acceptance criteria require.

**1/2 — where `CombatState`'s target half is wired, and why there.** Added
`Application::syncCombatState()` (`application.cpp`, declared
`application.h`), called every frame from the existing
`syncGameStateFromClient()` alongside `syncPetState()`/
`syncNPCInteractionState()`/`syncSpellState()` — the same established
"poll the authoritative EverQuest-owned subsystem, mirror into
`m_gameState`" pattern already used for pet/vendor/banker/trainer/casting
state, not a new pattern. Reads `m_eqClient->GetCombatManager()`
(`HasTarget()`/`GetTargetId()`/`IsAutoAttackEnabled()`, all pre-existing
`CombatManager` accessors) and, on a target-id change, looks up the entity
via `m_eqClient->GetEntities()` for name/level/HP, then calls
`m_gameState->combat().setTarget(...)`/`clearTarget()`. Same-target HP
changes call `updateTargetHP()`. Auto-attack changes call
`setAutoAttacking()`. `CombatManager` remains authoritative throughout —
this only mirrors it, exactly as the plan requires; STOP condition
("CombatState becomes authoritative") not triggered. This wiring is
target-*change*-source-agnostic (it polls `CombatManager`'s own state
rather than hooking a specific call site), which turned out to matter: the
`/target <name>` command path (`EqActionHandler::targetEntityByName` →
`CombatManager::SetTarget(name)`, `combat.cpp:251`) does NOT go through any
of the three `eq_bridge.cpp` push sites at all — it sets `CombatManager`'s
target directly. Had I wired only at the bridge push sites (the plan's
literal suggestion), `/target`-driven changes would never have reached
`CombatState`, only renderer-click-driven ones (`TargetIntent`). The
acceptance run below exercises exactly the `/target`-command path, and it
works.

**2 — what the stubs now publish.** `CombatState::fireTargetChangedEvent()`
(`combat_state.cpp`) now builds a real `TargetChangedData` from
`m_targetId`/`m_targetName`/`m_targetLevel`/`m_targetHPPercent` and calls
`m_eventBus->publish(...)`; the T28 "deliberately non-publishing" comment is
replaced with the corrected channel-separation reasoning and a pointer to
`Application::syncCombatState()`.
`CombatState::fireCombatEvent()` (behind `setAutoAttacking()`) **stays a
non-publishing stub, on purpose** — this is a deviation from the plan's "let
the stubs publish" framing for both stubs, justified in code and here: the
only data struct it could plausibly use, `CombatEventData`
(`event_bus.h:365`), models a combat-round *outcome*
(Hit/Miss/Dodge/Parry/Block/Riposte/CriticalHit/Death against a
source/target pair with a damage amount) — no field of it, and no other
`GameEventType`, means "auto-attack toggled". Reusing it with a fabricated
`Type` or invented damage value to represent a toggle would be exactly the
"invent a synthetic value" trap the plan warns against for inventory,
applied to combat instead. `setAutoAttacking()` itself IS wired (mirrors
`CombatManager::IsAutoAttackEnabled()`) and `isAutoAttacking()` now reflects
real state; it simply has zero production readers today, same as the
inventory occupancy arrays.

**3 — inventory verdict: wired, not "no clean site".** Investigated whether
a clean authoritative call site exists for `setEquipmentOccupied()`/
`setGeneralOccupied()`/`setBankOccupied()`. `InventoryManager::getItem(slotId)`
(`inventory_manager.h:115`) plus the bounded, well-defined slot ranges in
`inventory_constants.h` (`EQUIPMENT_BEGIN..END` = 0..21,
`GENERAL_BEGIN..END` = 22..29, `BANK_BEGIN..END` = 2000..2015) gave a clean,
enumerable site, so I did NOT invoke the "say so and leave it" escape
hatch — added `Application::syncInventoryState()` (same file, same call
site as `syncCombatState()`), looping the three ranges and calling the
setters only when `getItem(slot) != nullptr` differs from the state's
current `hasEquipment()`/`hasGeneralItem()`/`hasBankItem()`. Confirmed (T28
re-confirmed, still true after this unit) there is still **zero production
reader** of the resulting occupancy arrays — this is wiring without payoff,
exactly like `isAutoAttacking()`, which the plan explicitly allows ("this is
the wiring half" — readers were never a precondition, only "no clean call
site" was the escape hatch, and that didn't apply). Updated the stale T28
comment in `fireInventoryChangedEvent()` (`inventory_state.cpp`) that
claimed the setters "currently have zero production call sites" — no
longer true.

**4 — `cmdHail` payoff.** `command_processor.cpp:771`'s
`if (m_state.combat().hasTarget())` is unchanged code — confirmed via
`git diff` that only the comment above it changed, no include or
constructor parameter was added to the action layer (STOP condition not
triggered). It reads `m_state` = `Application::m_gameState`'s
`CombatState`, which `syncCombatState()` now mirrors from `CombatManager`
every frame, so this reader — deliberately left alone by T28 for exactly
this unit — is now correct as written. Live proof below.

**Console output landmine found and fixed while wiring item (b) of the
acceptance bar (deviation, file outside the plan's named territory, but
required — see below).** `console_renderer.cpp`'s `handleEvent()` takes
`std::lock_guard<std::mutex> lock(m_outputMutex)` for its whole switch, and
several of its existing cases (`ChatMessage` → `displayChatMessage()`,
`CombatEvent` → `displayCombatMessage()`, `PetCreated`/`PetStatsChanged`/
`PetRemoved` → their `display*`/`update*`/`clear*` methods) call public
methods that **each independently re-lock the same non-recursive
`std::mutex`** — a pre-existing double-lock in every existing case, latent
only because nothing published to `Application::m_gameState`'s `EventBus`
in production before this unit (confirmed: grepped for any
`m_gameState->events().publish` or equivalent prior to this unit's changes
and found none). The moment `TargetChanged` actually fires, adding a case
that calls `setCurrentTarget()`/`clearCurrentTarget()` (the existing,
previously-dead `IOutputRenderer` API at `console_renderer.cpp:311-343`,
which prints "Target: `<name>` (Level N) - HP: NN%" via direct `std::cout`)
would deadlock the game thread on the very first target change — not a
theoretical risk, reproduced once locally as a hang before the fix. Fixed
by changing `m_outputMutex` from `std::mutex` to `std::recursive_mutex`
(`console_renderer.h`), updating all twelve `lock_guard` sites in
`console_renderer.cpp` to match. `EventBus::publish()` invokes listeners
synchronously on the calling thread (`event_bus.cpp:32-48`), so every
acquisition is same-thread — safe for a recursive mutex, and this preserves
every existing case's behavior unchanged (verified: `ctest` full suite
still 1823/1824, no new failures). Technically outside the plan's stated
territory (`console_renderer.cpp`/`.h` aren't named), but not fixing it
would have made the plan's own acceptance criterion (b) hang the process,
so it was necessary collateral, not scope creep for its own sake — flagged
here rather than silently absorbed.

**Live acceptance (eqemu).** Lock protocol followed: checked
`/mnt/projects/fable/.eqemu-lock` absent, created it
(`T39-state-layer-migration 2026-08-04`), ran `up.sh cazicthule` (Testchar's
canonical zone, confirmed via DB query: `zone_id=36` = `cazicthule`),
deleted the lock after clean shutdown. (One false start: ran `up.sh
cazicthule` a second time without bringing the stack down first, which
started a second `world` process that logged "port 9001 already in use"
and left the login server unable to route to "WillEQ" — recovered by
`down.sh` + a single clean `up.sh cazicthule`, not a product bug.)

Ran `benchmarks/audits/T39/run_hail_target_acceptance.sh` against
`build/bin/willeq` in **headless mode** (`--no-graphics`, so `HeadlessMode`
+ `ConsoleRenderer` are the active mode/renderer — the channel this unit
populates) with `-d 1` for `eq_chat.cpp`'s unbuffered
`std::cout << "Sending {} message: '{}'"` debug line (deliberately NOT
relying on `LOG_INFO`/`LOG_DEBUG` for evidence — see
`refactor/deferred.md` "Non-FATAL log output never reaches stdout during
normal operation", still open as of this unit). Full transcript at
`benchmarks/audits/T39/transcript_hail_target_acceptance.txt`; relevant
lines, in session order:

```
Sending say message: 'Hail'                              <- (a) baseline: no target, /hail = hail()
CMD outcome=applied registry=action recognized=true cmd="/hail"
[CHAT] Testchar (say): Hail
...
CMD outcome=applied registry=game recognized=true cmd="/target a_rat"
Target: a_rat000 (Level 1) - HP: 100%                     <- (b) TargetChanged reached ConsoleRenderer's output
Sending say message: 'Hail, a rat'                        <- (a) payoff: /hail = hailTarget() now
CMD outcome=applied registry=action recognized=true cmd="/hail"
[CHAT] Testchar (say): Hail, a rat
```

Both acceptance-bar claims are directly demonstrated: (a) `/hail` sent
plain `"Hail"` with no target and `"Hail, a rat"` once `/target a_rat`
(resolved against the live entity map via `CombatManager::SetTarget`,
NOT the `TargetIntent` bridge path) set one — the exact `cmdHail` branch
flip. (b) The `Target: a_rat000 (Level 1) - HP: 100%` line is
`ConsoleRenderer::setCurrentTarget()`, called from the new `TargetChanged`
case this unit added to `handleEvent()` — before this unit, that
subscription existed but `handleEvent()`'s switch had no case for it (a
third half-wired thing found in passing, also fixed: see next paragraph),
so target changes reached zero output regardless of publishing. Session
ended with `/camp` (clean shutdown, `Console renderer shutdown` printed,
exit code 0, no orphaned process). Testchar drifted slightly during the
session (`x=-77.875,y=21.875` vs canonical `-77.9232,21.9086` — no explicit
movement command was sent; likely spawn/collision settling) and was
restored via DB `UPDATE` to `zone_id=36, x=-77.9232, y=21.9086, z=3,
heading=190` before `down.sh`.

**Third half-wired thing, fixed alongside (b)'s wiring, also outside named
territory but required for the same reason as the mutex fix.**
`console_renderer.cpp`'s `handleEvent()` already `subscribe()`d to
`GameEventType::TargetChanged` (present before this unit) but its `switch`
had no `case` for it — it silently fell through to `default: break;`. So
even with `fireTargetChangedEvent()` now publishing, nothing would have
been visible without also adding the case. Added it (calls
`setCurrentTarget()`/`clearCurrentTarget()` based on `data.spawnId`), with
a comment explaining the same "subscribed but unhandled" gap as the
combat/inventory theme this whole unit addresses.

**Tests re-pointed / added.**
- `tests/test_state_modules_secondary.cpp`:
  `CombatStateTest.DELIBERATE_GAP_SetTargetDoesNotPublishPendingMigration`
  replaced with three tests: `SetTargetPublishesTargetChangedWithRealData`
  (asserts the published `TargetChangedData` payload — spawnId/name/hp/level
  — matches what was set; would have FAILED before this unit, 0 events
  published), `ClearTargetPublishesTargetChangedWithZeroSpawnId`, and
  `SetAutoAttackingStillDoesNotPublish_NoMatchingEventType` (pins that
  `fireCombatEvent()`'s non-publishing stays intentional, not a silent
  regression).
- `tests/test_state_modules.cpp`:
  `InventoryStateTest.InventorySlotChangedEventReportsActualOccupiedState`
  (T28's rename) — behavior unchanged, only the stale "zero production call
  sites" comment updated to point at `Application::syncInventoryState()`.
- `tests/test_command_processor.cpp`: added
  `CommandProcessorTest.Hail_NoTarget_CallsHailNotHailTarget`,
  `Hail_WithTarget_CallsHailTargetNotHail`, and
  `Hail_TargetClearedAfterward_RevertsToHail` — exercise `cmdHail` directly
  against a `CommandProcessor`+`state::GameState` fixture (no `Application`
  involved), asserting the `RecordingActionHandler` receives `"hail()"` vs
  `"hailTarget()"`. `Hail_WithTarget_CallsHailTargetNotHail` would have
  FAILED before this unit (handler would always have recorded `"hail()"`,
  since `hasTarget()` was permanently false).
All new/changed tests run and pass (`test_state_modules`,
`test_state_modules_secondary`, `test_command_processor`, individually and
as part of the full suite below).

**Deviations, summarized** (all justified above, none silently absorbed):
(1) wiring site is `Application::syncCombatState()`/`syncInventoryState()`
(`application.cpp`/`.h`), not the `eq_bridge.cpp`/`eq_entities.cpp` sites the
plan suggested as "the obvious candidates" — required by the two-`GameState`
-instance finding. (2) `fireCombatEvent()` (auto-attack) stays
non-publishing — no matching `GameEventType` exists; publishing would mean
inventing data. (3) `console_renderer.h`/`.cpp` touched (mutex type +
`TargetChanged` case) — outside the plan's named territory, but required to
make (b) both non-deadlocking and actually visible.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | Clean, twice (once after `application.*`/tests, once after `combat_state.cpp`/`inventory_state.cpp`/`console_renderer.*`/`command_processor.cpp`). |
| `cmake --build build-nogfx -j24` (headless configure, `EQT_GRAPHICS=OFF`) | Clean. `ConsoleRenderer` is the live consumer of the channel this unit populates, so this configure matters independently of `build/`'s runtime `--no-graphics` flag. |
| `ctest --test-dir build --output-on-failure` | 1823/1824 passed. Sole failure: `AudioManagerTest.PreloadCommonSounds` — confirmed the known ALSA host quirk (`cannot find card '0'`), matches baseline. `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` RAN (not skipped) and passed. Full `RDPServerFrameTest`/`RDPServerLifecycleTest`/etc. family (all subsuites) passed, no use-after-free observed. `visual_gate` test (ctest #1824) passed — goldens untouched, no rendering-path files touched by this unit. Remaining ~17 `Skipped` entries are pre-existing categories unrelated to this unit's territory (`SpellManagerGems`, `PfsArchiveContentTest`, `DoorManagerGraphicsTest`, `InventoryModelViewTest`, `SoundBufferTest`) — not investigated further as they don't touch combat/inventory/command_processor/console_renderer. |
| `scripts/build-gles2-x86.sh` | Clean; `visual_gate` (ctest #1824, above) already exercises this tree. |
| `scripts/refactor/check_file_sizes.sh --files <10 abs paths touched>` | 9 ok, 1 WARN (`application.cpp`: 2412 lines, > 1500 warn threshold). Confirmed via `git show fable:src/client/application.cpp \| wc -l` = 2310 — already over the warn threshold on the fable baseline before this unit; this unit added 102 lines (two new sync functions). WARN, not FAIL — no new FAILs, per the bar. `application.cpp` is already a known-large file; splitting it is out of this unit's scope. |
| Live acceptance (eqemu) | PASS — see "Live acceptance" above. Evidence: `benchmarks/audits/T39/{willeq_t39.json, run_hail_target_acceptance.sh, transcript_hail_target_acceptance.txt, transcript_hail_target_acceptance.txt.driver}`. Lock created/deleted per protocol; Testchar restored to canonical position; server brought down cleanly. |

ARM build (`scripts/build-arm-noble.sh`) intentionally NOT run — the plan
states "Slot: t1. x86 only. No Pi.", which narrows the generic
`EXECUTOR_PROTOCOL.md` verification bar for this unit.
