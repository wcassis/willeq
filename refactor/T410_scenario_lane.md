# T410 — a live-server scenario lane with exit codes (B138, B139; Will 2026-09-06: build NEW)

Class B (verification infrastructure), the slot that frees; eqemu lock
for the proof run. Read B138 (what the May branch built: `--scenario
<name> --timeout <n>`, a `ScenarioRunner` + registry, an exit-code
taxonomy replacing `main.cpp`'s unconditional `return 0`, a `MOD_SCENARIO`
log module, a precheck→reset→seed→normalize harness) and B139
(`AutomatedMode::setAutomationCallback` — the unreached per-tick hook it
plugs into); T372's §Review; the existing `--script` driver and
`AutomatedMode`; the live rigs under `review/T377|T386|T391|T401/scripts`
(what every live unit re-implements by hand).

Build it NEW (not ported):
1. `src/client/scenario/`: a `Scenario` interface (name, `step(tick)` →
   Running/Pass/Fail with a reason), a registry, a runner driven from the
   automated mode's callback (B139 wired), `--scenario <name>` and
   `--timeout <s>` flags on `Application`; `main.cpp` returns a taxonomy:
   0 pass, 1 fail, 2 timeout, 3 could-not-start (no server/zone), with the
   reason on stderr and in the log under `MOD_SCENARIO`.
2. Three scenarios that replace hand rigs used this week: `zone_in`
   (login → zone → `IsFullyZonedIn`), `two_client_survive` (T391's
   five-minute pair — needs the second client as a peer scenario or a
   flag), `cast_lands` (gem-cast a spell, expect the OP_Action landing —
   T395's keying). Each has a headless unit test on its state machine
   (pre-fix: no scenario exists) and one live PASS run recorded in
   `review/T410/` with the exit code.
3. A harness script `scripts/scenario.sh <name>` that takes the lock,
   `up.sh`, runs, restores the server snapshot (T401's snapshot/restore
   with the world down), `down.sh`, releases — the pieces every live unit
   re-implements.
4. Docs: `CLAUDE.md` gains the lane; DocClaims/DocCounts green. Strike
   B138, B139.

## Proof

Build, `ctest -j6`, the state-machine tests, three live runs with exit
codes, file sizes (every new TU under the ceiling).

STOP: the automated mode's tick cannot host a runner without restructuring
the main loop (report the shape).

---

## Review

Branch `task/T410-scenario-lane`, base `48ec30a0`. Built NEW; nothing was
ported from `task/eqcpp-zoneprocess-track-b-remaining` (its 22 scenario
commits were read for SHAPE only — `--scenario <name>`, an exit taxonomy, a
`MOD_SCENARIO` module, a harness — and every line here is new).

**The STOP condition did not fire.** The automated mode's tick hosts a runner
with no restructuring at all: `Application::processInput()` already calls
`m_gameMode->update(deltaTime)` every ~60 Hz game tick whenever graphics are
not initialised, `AutomatedMode::update()` already invokes
`m_automationCallback(dt)` and already treats a `false` return as "quit", and
the game thread already unwinds on `isQuitRequested()`. B139's seam was
complete; it simply had no caller.

### 1. The shape

| piece | file |
|---|---|
| `IScenario` + `IScenarioWorld` + `ScenarioOptions` | `include/client/scenario/scenario.h` (118) |
| `ScenarioRunner` + the exit-code enum | `include/client/scenario/scenario_runner.h` (85), `src/client/scenario/scenario_runner.cpp` (102) |
| registry | `include/client/scenario/scenarios.h` (22), `src/client/scenario/scenario_registry.cpp` (47) |
| the three scenarios | `scenario_zone_in.cpp` (45), `scenario_two_client_survive.cpp` (105), `scenario_cast_lands.cpp` (131) |
| the live adapter | `include/client/scenario/scenario_world_app.h` (42), `scenario_world_app.cpp` (72) |
| wiring | `scenario_application_wiring.cpp` (91) — `Application::initScenario()` + three result accessors |
| harness | `scripts/scenario.sh` (183) + `scripts/scenario_configs/*.json` |
| tests | `tests/test_scenario_state_machines.cpp` (360), 20 cases |

Two design decisions worth naming, because both were forced by measurement
rather than taste:

**The runner owns the pre-flight, not the scenarios.** Until
`world.IsFullyZonedIn()` is true it never calls `Step()`; if that never
happens inside the start deadline the verdict is **3, could-not-start**. That
is what keeps "the server was down" from ever being reported as a scenario
failure — the one distinction a CI lane actually needs — and it means every
scenario's first `Step()` may assume a live, zoned-in client, so none of them
re-implements a connection wait. The start deadline is clamped to the overall
timeout (pinned by `StartDeadlineIsClampedToTheOverallTimeout`), because a
deadline longer than the timeout would report a run that never started as a
*timeout* and erase the distinction.

**Scenarios POLL; they do not subscribe.** The obvious wiring — an event
callback on `AutomatedMode` that forwards every `GameState::events()` event
(deleted by B500, 2026-10-12 — it was unreached exactly as this note said)
— is a dead end for this lane. `GameState::events()` and the
renderer-facing `GameStateBridge` are two different streams, and the landing
`cast_lands` has to observe goes to the **bridge**
(`PlayerComponent::PublishPlayerBuffUpdated` -> `ctx_.bridge->pushEvent`),
which a headless client never drains. Polling `BuffManager` reads the
authoritative state the bridge event was published *from*, and works
identically headless.

### 2. Exit-code taxonomy

| code | meaning | where decided |
|---|---|---|
| 0 | pass | `IScenario::Step()` returned `Pass` |
| 1 | fail | `Step()` returned `Fail` |
| 2 | timeout | zoned in, no verdict inside `--timeout` — **and what an unfinished run reports** |
| 3 | could-not-start | never zoned in inside the start deadline; also an unknown `--scenario` name, and an `Application::initialize()` failure in scenario mode |

The **external-quit guard** is `ScenarioRunner::exitCode()`:
`finished_ ? code_ : kScenarioTimeout`. A client stopped by anything other
than the runner — SIGTERM, `/quit`, an early camp — leaves the runner
unfinished, and an unfinished run can never report 0. `main()` reads it AFTER
`app.shutdown()`, which is safe because `m_scenarioRunner` outlives
`m_gameMode`; the members are declared before `m_eqClient` so the world
adapter (which holds references to both `Application` and `EverQuest`) is
destroyed first.

Every non-scenario run still returns 0 exactly as it did before — the whole
taxonomy is behind `if (app.scenarioRequested())`.

`scripts/scenario.sh` layers its own operator codes above the client's: **64**
lock held by another session, **65** `up.sh` failed, **66** the snapshot did
not restore byte-identically (which deliberately OVERRIDES a passing
scenario — a green run that left the server dirty is not a green run).

### 3. The three scenarios and their tests

| scenario | state machine | replaces |
|---|---|---|
| `zone_in` | first `Step()` after zone-in: non-empty zone name -> Pass, empty -> Fail | every `grep "Zone connection complete"` in `review/T377\|T386\|T391\|T401` |
| `two_client_survive` | wait for `OtherPlayerCount() >= 1` (180 s window from zone-in, else Fail), then hold `--scenario-hold` seconds; the peer vanishing or the observer leaving the zone mid-hold is a Fail | `review/T391/scripts/t391_run.sh`'s `kill -0` poll |
| `cast_lands` | T401's measured prep at +3/+7/+11/+16 s (`#set invulnerable off`, `#unmemspells`, `#memspell <id> 0`, `/target <self>`), then `/cast 1` at +21 s and up to 3 attempts in 20 s windows; Pass when the spell id appears in `BuffManager`'s player buffs ABOVE its zone-in baseline | the hand-read chat panel in T386/T401 |

20 cases in `tests/test_scenario_state_machines.cpp`, all green
(`review/T410/logs/state_machine_tests.log`). They link the five scenario TUs
**directly, not `willeq_core`** — if that target ever needs the core library,
`IScenarioWorld` has grown a dependency it should not have. Cases worth
naming:

* `AnUnfinishedRunNeverReportsSuccess` — the external-quit guard.
* `NeverZonedInIsCouldNotStartNotAFailure` and
  `ZonedInButNoVerdictIsTimeout` — 3 and 2 are genuinely distinguished.
* `DoesNotStepTheScenarioBeforeZoneIn` — `cast_lands` issues its first prep
  command 3 s after ZONE-IN, not 3 s after process start.
* `ZonedInWithNoZoneNameIsAFailureNotAPass` — the case a log grep for "Zone
  connection complete" cannot see at all.
* `NoPeerAtAllIsAFailure` — the hand rig printed SURVIVED when the partner
  never logged in.
* `ABuffThatWasALREADYThereIsNotALanding` — the baseline is a multiset, so a
  character that zones in already buffed does not pass for free.

**Red before the fix:** the test names `client/scenario/scenario_runner.h`
and `client/scenario/scenarios.h`, neither of which exists on `48ec30a0`, so
every case fails to compile pre-fix. There is no runner to be red against —
that is the shape the plan specified.

### 4. Harness steps (`scripts/scenario.sh <name> [options]`)

1. take `/mnt/projects/fable/.eqemu-lock` with `set -o noclobber` (atomic; a
   held lock is exit 64 immediately — it NEVER waits and never overwrites);
2. `scripts/eqemu/up.sh` (exit 65 on failure);
3. snapshot `character_data` + `character_currency` to
   `review/<UNIT>/scenario/<name>_snapshot_before.tsv`;
4. for `two_client_survive` only, schedule the peer: a second `willeq` running
   `--scenario zone_in` on `peer.json`, staggered by 55 s (T391's stagger, so
   the partner arrives as a fresh spawn packet rather than inside the bulk
   zone-in list);
5. run the client with `--scenario/--timeout/--scenario-hold/--scenario-spell`
   and capture its exit code;
6. restore the snapshot **with world, zone, loginserver AND the spire launcher
   killed** and only mariadb up (T377's lesson, re-learned by T401: the world
   rewrites character rows on shutdown, and the eqemu container respawns
   `bin/world` from `/tmp/spire-launcher`, so that must be killed first), then
   re-snapshot and `diff`;
7. `scripts/eqemu/down.sh`, delete the lock;
8. exit with the client's code, unless the restore diff failed, which wins
   with 66.

### 5. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, zero `error:` lines. (The invocation exceeded the 600 s Bash ceiling and was backgrounded; it finished on its own and wrote `EXIT=0`, so no exit-143 truncation sweep applies.) |
| `ctest -j6` | **4,215 of 4,221.** Six reds, all one cause — see below |
| `test_scenario_state_machines` standalone | **20/20** |
| `DocClaims` / `DocCounts` | both **exit 0** after the CLAUDE.md, `docs/module_map.md` and `docs/decisions.md` edits |
| `check_file_sizes.sh fable` | 7 changed files, **7 ok, 0 warn, 0 fail**. New TUs: largest source 131 lines, largest header 118, test 360, script 183 — every one far under the 1,500 ceiling |
| three live runs | **OWED — board B305** |

**The six ctest reds are not this unit's.** They are
`PackReproducible`, `ParticleAtlasAlpha.PackEntryCarriesAlphaInBand`,
`TextureAlphaCensus.{Weather,SoftwareCursor,Billboard}PackEntriesCarryAlphaInBand`
and `visual_gate` — every one a consumer of the shared
`/mnt/projects/EverQuest_ETC1/willeq.eqe1`. That file's mtime is **06:23**,
i.e. it was replaced *during* this unit's ctest run, and its sha256 is
`aca03e7f...` against the tip's pin `2f76087a...` in `data/willeq.eqe1.sha256`;
its size is 5,532,254 bytes where the tip's 32-bit pack is ~19.99 MB. This is
exactly the shape the dispatch predicted for T411 (slot1) rebuilding the
shared pack. Nothing in this branch touches a texture, a generator, the pack
builder or a pin. Every other test in those binaries passed, and
`ApplicationInitOrderTest`, `LoggingTest` and all 20 scenario cases are green.

### 6. Two nets this unit had to teach

* **`tests/goldens/application_init_order_g04.txt`** learned one entry IN
  PLACE (correction 32): `308 if (!initScenario(config)) return false;`
  between the console bridge and the login connection, count 313 -> 314, with
  the reason written into the golden's header and the test's size comment.
  Named deviation: `initScenario()` is defined in
  `src/client/scenario/scenario_application_wiring.cpp`, not `application.cpp`,
  so `Expand()` cannot inline it and the golden pins the STEP CALL rather than
  its statements. That is deliberate — the lane is opt-in machinery in its own
  TU — and the load-bearing part of the order is still pinned (it must run
  after the console bridge, whose adapter it reads, and before
  `initLoginConnection()`, which is deliberately last).
* **`tests/test_logging.cpp`** `MOD_COUNT` 21 -> 22 for the new
  `MOD_SCENARIO` module, appended after `MOD_MAIN` so no existing ordinal
  moved.

### 7. Deviations

1. **Three flags beyond the plan's two.** The plan named `--scenario <name>`
   and `--timeout <n>`. `--scenario-hold <s>` and `--scenario-spell <id>`
   were added so `two_client_survive`'s hold and `cast_lands`'s spell are
   harness inputs rather than recompiles; both are documented in `--help` and
   in CLAUDE.md. No behaviour changes without them.
2. **The peer is a process, not a flag.** The plan allowed "a peer scenario or
   a flag". It is neither: `scripts/scenario.sh` launches a second `willeq`
   running `zone_in` on the second account. A scenario that forked a client
   would need another character's whole login config, which is exactly what
   the harness already owns.
3. **`AutomatedMode::setEventCallback` is deleted.** B139 named
   `setAutomationCallback`, which has a caller. The event callback was a
   separate unreached seam and was never wired — see the polling
   rationale in §1; wiring it would have given scenarios a stream that does not
   carry the landings they assert on. B500 (2026-10-12) deleted it and its
   `m_eventCallback` / `m_listenerHandles` members with the rest of the
   unreached `AutomatedMode` accessors.
4. **The live runs are owed, and one live run happened by accident.** A smoke
   test written to prove exit 3 against *no* server found the server up under
   T405's lock, logged Testcaster in, and passed with exit 0 in 4.95 s. It
   sent no command and moved no character, but it is a lock violation and is
   boarded as **B306** with the mechanical lesson: a "there is no server"
   negative test is not safe to run without checking the lock first, because
   the server's absence is an assumption, not a fact. Everything after that
   discovery was kept off the server.

### 8. What surprised me

`BuffManager` — not the event bus — is the only place a headless client can
observe a spell landing, and that is a direct consequence of T348's finding
that eqemu never sends a Titanium client an "applied" `OP_Buff`. The obvious
wiring for a scenario lane (subscribe to `GameState::events()`, which
`AutomatedMode` already forwards to a callback nobody registers) would have
produced a `cast_lands` that is green on a client where casting is completely
broken, because the events it could see are the ones the *client* publishes
about its own intent, not the ones the server confirms. Two unreached seams
sit side by side on `AutomatedMode`, and only one of them was the right one.
