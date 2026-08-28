# T305 — Audit of the two never-surveyed 300-commit branches

Unit: T305 (audit only — no merge, no code change).
Base: `fable` @ `9bb156ea`.
Board item resolved: `refactor/deferred.md` tier 4 item 22 ("Two unmerged
branches are 300 commits ahead of `fable` and have never been audited").

Branches audited:

- `task/eqcpp-zoneprocess-track-b-remaining` — tip `127c81e5`
- `task/eqemu-zone-line-transition-scenario` — tip `83e8a19e`

---

## 0. The finding that reframes the whole question

**The two branches are not two branches. They are one branch with two
three-commit tips.**

```
merge-base(fable, both)          = 92e2f260  "Working on composite audit tool ..."
merge-base(A, B)                 = 4b5f2c8d  "Set up Track B ZoneProcess task"
rev-list --count 4b5f2c8d..A     = 3
rev-list --count 4b5f2c8d..B     = 3
rev-list --count fable..A        = 300
rev-list --count fable..B        = 300
rev-list --count A..fable        = 1292
```

297 of each branch's 300 commits are **shared**. The board's phrasing
("two unmerged branches, 300 commits ahead each") implies ~600 commits of
unreviewed content. The true figure is **300 shared commits plus six
divergent ones**.

The three divergent commits per tip:

| Branch | Commits | Content |
|---|---|---|
| A `...track-b-remaining` | `244be9a2`, `e4f70407`, `127c81e5` | Moves `ZoneProcessPlayerProfile` out of `eq.cpp` into `CharacterStatController`; +1 report file. Code motion only. |
| B `...zone-line-transition-scenario` | `c0f60a36`, `a6043b3d`, `83e8a19e` | **Two markdown files. Zero lines of code.** A task card and a checkpoint-0 feasibility report. |

`git diff --stat 4b5f2c8d task/eqemu-zone-line-transition-scenario`:

```
 docs/reports/eqemu_zone_line_transition_scenario_report.md  | 210 +++++
 docs/tasks/open/eqemu_zone_line_transition_scenario.md      | 169 +++++
 2 files changed, 379 insertions(+)
```

So branch B is fully answerable on its own terms before any code
comparison: **it never reached checkpoint 1.** It carries no
implementation. Everything else on it is branch A's content.

`fable` is meanwhile **1292 commits ahead** of both. The divergence is not
symmetric; it is 4.3:1 in `fable`'s favour.

---

## 1. What is actually on the shared 297 commits

Grouped by subsystem, ignoring the commit-by-commit recital. Total
`git diff --stat 92e2f260 A` = **387 files, +78,700 / -18,253**. Of the 343
newly-added files:

| Category | Files | Substance |
|---|---|---|
| `docs/tasks/open/*.md` | 119 | Task cards for the branch's own process |
| `docs/reports/*.md` | 116 | Per-task narrative reports |
| `docs/plans/`, `docs/pm/`, `docs/decisions/`, `docs/agent_tasks/` | 30 | Process/PM scaffolding |
| `scripts/agent-*.sh`, `scripts/bootstrap-*.sh`, `codex-pm-*` | 15 | A Codex/Claude agent-queue orchestration system |
| `test/harness/**` | 22 | Deterministic EQEmu integration harness (bash + SQL fixtures) |
| `src/client/**`, `include/client/**` | 28 | The runtime decomposition (see below) |
| `src/client/scenario*`, `tests/test_scenario_*` | 5 | In-client `--scenario` mode |
| `tests/test_daybreak_teardown_race.cpp` | 1 | Regression test for a net teardown race |

**265 of 343 added files (77%) are documentation and process scaffolding.**

The real signal is three things:

### (a) The `eq.cpp` ZoneProcess decomposition

The branch drives `src/client/eq.cpp` from 19,676 lines (at the merge-base)
to 10,059, by extracting ~14 named controller classes and replacing the
hand-written opcode `switch` with a **data-driven runtime dispatch table**
(`GameRuntime::registerZonePacketHandlers`, in
`src/client/game_runtime_zone_dispatch.cpp`). The classes:

`EntityTracker`, `PlayerMovement`, `SocialController`,
`InteractionController`, `WorldProjection`, `ChatSystem`,
`ConnectionLifecycle`, `ZoneEnvironment`, `CharacterStatController`,
`CharacterProfile`, `GameRuntime`, `CollisionSystem`, `LightingSystem`,
plus an `IBridgePublisher` narrowing seam
(`include/client/bridge/bridge_publisher.h`).

`src/client/eq_zone_dispatch.cpp` on the branch is an 83-line
**breadcrumb file with no code**, indexing where every former handler body
went.

### (b) The deterministic EQEmu harness and in-client `--scenario` mode

`test/harness/` is a bash pipeline — `precheck.sh` → `reset.sh` →
`seed.sh` → `normalize.sh` → `run-scenario.sh` — over SQL fixtures
(`account.sql` seeds `status=200`, `character.sql`, `character_bind.sql`)
and per-scenario `.env` profiles. It drives the client in a new
`--scenario <name> --timeout <n>` mode implemented by
`ScenarioRunner` + an `IScenario` registry, with a canonical exit-code
contract (`0` pass / `2` operator error / `10` assertion failed /
`11` timeout / `12` stack failure) shared between the client and the
scripts. One scenario exists: `arena-idle`.

### (c) A handful of genuine bug fixes carried along

`src/common/net/daybreak_connection.cpp` (+81) with
`tests/test_daybreak_teardown_race.cpp`; `rdp_server.cpp` (+81) with
`tests/test_rdp_server.cpp` (+218); `scripts/build-x86-gles2-irrlicht.sh`
and `docker/irrlicht-x86-gles2-patch.py` (x86 GLES2/llvmpipe bring-up).

---

## 2. What of it already exists on `fable` today

**Every single one of the 343 added paths is absent from `fable` by
filename.** That is a red herring — `fable` reimplemented the same goals
under a different decomposition. Checked against the code, not the commit
messages:

### (a) `eq.cpp` decomposition — SUPERSEDED, and `fable` went further

| Revision | `src/client/eq.cpp` lines |
|---|---|
| merge-base `92e2f260` | 19,676 |
| branch tip `127c81e5` | **10,059** |
| `fable` `9bb156ea` | **3,913** |

`fable` reached a *smaller* `eq.cpp` than the branch did, by splitting on
**topic-per-TU** rather than **class-per-responsibility**:

```
src/client/eq_action_handler.cpp   2202    src/client/eq_login_world.cpp     920
src/client/eq_bridge.cpp           2096    src/client/eq_merchant.cpp       1931
src/client/eq_chat.cpp              616    src/client/eq_movement.cpp       2033
src/client/eq_combat_actions.cpp    526    src/client/eq_player_info.cpp    2301
src/client/eq_commands*.cpp        2192    src/client/eq_strings.cpp         751
src/client/eq_entities.cpp         2725    src/client/eq_zone.cpp            978
src/client/eq_gm.cpp                307    src/client/eq_group.cpp           596
                                           src/client/eq_guild.cpp           257
```

Mapping the branch's classes onto `fable`'s equivalents:

| Branch class | `fable` equivalent |
|---|---|
| `EntityTracker` | `src/client/eq_entities.cpp` (2725 lines) |
| `PlayerMovement` | `src/client/eq_movement.cpp` (2033) |
| `ChatSystem` | `src/client/eq_chat.cpp` (616) + `src/client/chat/chat_message_buffer.cpp` |
| `SocialController` | `src/client/eq_group.cpp`, `eq_guild.cpp` |
| `InteractionController` | `src/client/eq_merchant.cpp`, `src/client/action/command_processor_loot.cpp`, `src/client/graphics/ui/loot_panel.cpp` |
| `ConnectionLifecycle` | `src/client/eq_login_world.cpp` (920) |
| `CharacterStatController` / `CharacterProfile` | `src/client/eq_player_info.cpp` (2301), `src/client/eq_profile_appearance.cpp` |
| `ZoneEnvironment` | `src/client/eq_zone.cpp` (978), `src/client/graphics/renderer_zone_load_env.cpp` |
| `WorldProjection` | `src/client/state/*`, `src/client/bridge/game_state_sync_bridge.cpp` |
| `LightingSystem` | `include/client/graphics/light_source.h` + renderer split |
| `CollisionSystem` | `src/client/graphics/renderer_player_physics.cpp`, `include/client/raycast_mesh.h` |
| `GameRuntime` shell | `src/client/application.cpp` + `src/client/mode/{game,graphical,headless,automated}_mode.cpp` |
| `IBridgePublisher` | *not present* — `fable` subsystems still hold `GameStateBridge*` |

The **one architectural idea `fable` does not have** is the data-driven
dispatch table. `fable` retains a hand-written `switch` in
`EverQuest::DispatchZonePacket` (`src/client/eq.cpp:800`–`1682`, **143
`case HC_OP_*` labels**) that delegates to the `eq_*.cpp` bodies. The
branch replaced that switch with a registration table. That is a style
difference, not a capability difference, and it is the *only* thing the
branch's 28 source files hold that `fable` lacks.

`fable` also has renderer decomposition far past the branch's:
`src/client/graphics/renderer_*.cpp` is **21 translation units**
(`renderer_frame`, `renderer_zone_load`, `renderer_zone_stream`,
`renderer_entities`, `renderer_gpu_upload`, `renderer_sim_bridge`, …)
against the branch's single `irrlicht_renderer.cpp` edit.

### (b) Test coverage — `fable` is 2.75x ahead

```
branch tests/*.cpp : 61 TUs
fable  tests/*.cpp : 168 TUs
present on fable, absent on branch : 112 TUs
present on branch, absent on fable : 5 TUs
```

Of those 5: `tests/test_wld.cpp` and `tests/test_pcap_replay.cpp` were
**deliberately restructured away** on `fable` (`250d303b` "T11: split
tests/test_wld.cpp into 9 TUs + shared fixture header"; `1f11507a` "R03:
register pcap protocol tests with capture guards"), not lost. That leaves
three genuinely branch-only test TUs: `test_daybreak_teardown_race.cpp`,
`test_scenario_registry.cpp`, `test_scenario_runner_exit_codes.cpp`.

**None of the branch's ~14 extracted controller classes has a unit test.**
`grep` over the branch's `tests/CMakeLists.txt` for any of
`entity_tracker|player_movement|social_controller|interaction_controller|world_projection|chat_system|connection_lifecycle|character_stat|game_runtime|zone_environment|lighting|collision`
returns nothing. The branch's decomposition is verified by narrative
reports; `fable`'s is verified by a ~3200-test suite.

### (c) Zone-line / zone-transition capability — `fable` is not merely
equivalent, it is far ahead

Branch B existed to write an automated proof that the client can zone from
`arena` to a second zone. On `fable` today:

- `data/zone_lines/*.json` — **115 zones** of extracted zone-line geometry,
  plus `data/zone_lines.json` and `data/zone_lines_unresolved.json`
- `src/client/zone_lines.cpp`, `include/client/zone_lines.h`,
  `tests/test_zone_lines.cpp`, `tools/generate_zone_lines.py`,
  `tools/zone_line_editor.cpp`, `refactor/T19_map_zone_lines.md`
- `EverQuest::RequestZoneChange()` at `src/client/eq_zone.cpp:248`, with a
  same-zone-relocation guard added by T231 (`src/client/eq_zone.cpp:73`–`102`)
  and a dedicated test seam documented at `include/client/eq.h:878`–`880`
- `tests/test_zone_connection_flow.cpp` — 35 in-process tests over the zone
  handshake, including
  `ZoneLoadSnapshot.TwoZoneSequenceResolvesAgainstSecondZoneTree` (:1266)
  and `ZoneLoadSnapshot.LateFinalizationTreeForPreviousZoneIsRejected`
  (:1291) — i.e. **the multi-zone transition assertion branch B set out to
  build, already implemented and running in the unit suite with no live
  server required.**

The branch has, against all of that, a `+59` line touch to
`src/client/zone_lines.cpp` and a markdown plan.

### (d) Process scaffolding — superseded by design

`docs/agent_tasks/`, `scripts/agent-task-*.sh`, `docs/pm/`, the
Codex-checkpoint protocol: all replaced on `fable` by
`refactor/EXECUTOR_PROTOCOL.md` + `refactor/deferred.md` + the slot-based
worktree model. Not a loss; a superseded generation of the same idea.

### (e) EQEmu bring-up — partially covered

`fable` has `scripts/eqemu/{up,down,seed,status,env,record_tour}.sh` and
`refactor/TESTING_CHARTER.md`'s cross-session `.eqemu-lock` protocol. That
covers "stand the stack up and get a character logged in", which is the
`precheck`/`reset`/`seed` third of the branch's harness. See §3.

---

## 3. What is genuinely missing from `fable`

Stripping out everything superseded, this is what abandoning the branches
actually costs. It is a short list, and it is not the list the branch names
suggest.

### M1 — Two real bug fixes in `src/common/net/daybreak_connection.cpp` (HIGHEST VALUE, and nothing to do with zone process)

The single most decision-relevant finding in this audit. The branch fixes a
callback-reentrancy family in the Daybreak connection manager that is **still
live on `fable` today**:

| Guard | Branch | `fable` `9bb156ea` |
|---|---|---|
| `Tick()` holds the dispatch connection by `shared_ptr` **value**, not `auto&` — a callback fired inside `ProcessPacket()` can erase the map entry while `ProcessPacket()` is still using `conn->RemoteEndpoint()` | present | **absent** — `src/common/net/daybreak_connection.cpp:37` (`Tick()`) still holds `auto& conn = m_connections.begin()->second;` across `ProcessPacket()` |
| `Process()` / `UpdateDataBudget()` / `ProcessResend()` iterate a **local `shared_ptr` snapshot**, re-looking-up each key and identity-comparing `.get()` before erasing | present | **absent** — `src/common/net/daybreak_connection.cpp:86`, `:151`, `:170` still iterate raw iterators |
| In the `StatusDisconnecting` timeout path, **erase the map entry before** `ChangeStatus(StatusDisconnected)` | present | **absent** — `src/common/net/daybreak_connection.cpp:100`–`101` does `ChangeStatus(StatusDisconnected); iter = m_connections.erase(iter);` — callback first, erase second |

The third one is a live user-visible defect, not a theoretical hazard. A
disconnect-status callback that calls `Connect()` on the same
endpoint/port hits an `emplace` that **silently no-ops against the still-present
old key**. The manager ends up with no entry for the new connection: the
reconnect dangles and inbound packets go undispatched. That is precisely the
zone-transition reconnect path.

Test coverage: `tests/test_daybreak_teardown_race.cpp` (230 lines, 3 tests —
`ReconnectSameKeyFromDisconnectCallbackReplacesEntry`,
`CallbackAddsDifferentKeyDuringProcess`,
`ManyDisconnectingEntriesWithReconnectCallbacks`). `fable` has
`tests/test_daybreak_state_machine.cpp` (24 tests, a *better* fixture than the
branch's ad-hoc stub, with `tests/fake_udp_transport.h`), but its closest test
— `ManagerProcessTearsDownAfterCloseTimeElapses` at `:351` — only asserts
`m_connections.size()==0` after a backdated close. **It never runs a callback
that mutates the map, so it passes with the bug in place.**

### M2 — Two resource leaks in `src/client/graphics/rdp/rdp_server.cpp` (REAL, small)

The branch's RDP work is three concerns; `fable` has one of them:

| Concern | Branch | `fable` |
|---|---|---|
| join listener thread before `listener_->Close()` | present | **present and better** — `src/client/graphics/rdp/rdp_server.cpp:325`–`345`, the T03b/T35 use-after-free fix. Not needed. |
| `start()`-failure cleanup: on `listener_->Open()` failure, `Close()` + `freerdp_listener_free()` + `listener_=nullptr` + `WSACleanup()` + `initialized_=false` | present | **absent** — `rdp_server.cpp:313`–`315` just logs and `return false`. `listener_` then leaks forever (`stop()` early-returns on `!running_`) and a retried `initialize()` early-returns on `initialized_=true`, silently reusing a half-owned listener. |
| init-without-start cleanup: relax the `stop()` guard to `if (!initialized_ && !running_) return;` | present | **absent** — `rdp_server.cpp:325`–`328` early-returns on `!running_.load()` alone. `~RDPServer()` (`:76`) calls `stop()`, so `initialize()`-then-destroy leaks the listener and the WSA ref. |

Branch tests: `RDPServerStartFailureCleanup.RepeatedFailedStartIsSafe`
(`tests/test_rdp_server.cpp:147`, binds a blocker socket to force `Open()`
failure) and `RDPServerInitWithoutStartCleanup.InitWithoutStartIsSafe` (`:255`).
Neither exists on `fable`.

The branch's `GTEST_SKIP` scaffolding for the RDP frame flake is **not** wanted
— `fable` solved the parallel-`ctest` port collision differently and better, by
moving `initialize()` per-`TEST_F` (`tests/test_rdp_server.cpp:65`–`66`,
`:209`–`212`).

### M3 — The live-server scenario lane (REAL, the largest chunk, and genuinely uncovered)

`fable` has a strong *offline* determinism story — the replay oracle
(`refactor/R02_replay_harness.md`, `tests/test_session_replay.cpp`), pcap
fixtures, the visual gate, ~3200 ctest cases — and better *stack operation*
than the branch (`scripts/eqemu/up.sh` boots the stack and waits for
`Zone bootup type`; `down.sh`, `status.sh`. **The branch has no up/down at all
— it assumes the stack is already running.**)

What `fable` has no equivalent of, at all:

1. **Any exit-code contract.** `fable:src/client/main.cpp` ends
   `app.run(); app.shutdown(); FlushThreadLog(); return 0;` — unconditionally
   zero after the loop. `git grep 'exitCode\|EXIT_' fable -- src/client
   include/client` is empty. Every live driver on `fable`
   (`benchmarks/audits/T*/run_verification*.sh`, ~15 of them) can only
   distinguish "process died" from "process didn't", and a human then reads a
   transcript. The branch defines 0 pass / 2 operator / 3 stale session refused
   / 4 kick failed / 10 assertion failed / 11 timeout / 12 stack failure, shared
   verbatim between `test/harness/harness_exit_codes.env` and
   `ScenarioRunner::exitCodeFor()`, with a guard so an external quit while the
   scenario is RUNNING can never masquerade as success
   (`src/client/application.cpp:638`–`643`).
2. **`--scenario <name> --timeout <n>` and a scenario registry.** `fable`'s flag
   list (`src/client/application.cpp:1956`–`2085`) has no such flags. Its
   `src/client/mode/automated_mode.cpp` has an injectable
   `m_automationCallback` and a command queue — a *seam* with no pass/fail, no
   timeout, no exit code, and **no registrations anywhere in the tree**. (Note
   for the board: that is a ninth-pattern reachability candidate in its own
   right.)
3. **Reset + create-from-scratch DB seeding.** `fable`'s
   `scripts/eqemu/seed.sh` explicitly *refuses* to create an account or
   character on an empty DB and tells the operator to use the official client
   (its own header says so). The branch's `seed.sh` does it, via the shipped
   `./bin/loginserver login-user:create` plus `fixtures/{account,character,
   character_bind}.sql`, with `reset.sh`/`cleanup.sql` deleting prior state
   first (children before parents). `fable` has no cleanup SQL and therefore no
   run-to-run determinism of DB state.
4. **The stale-session precheck.** `test/harness/precheck.sh` talks to the world
   **telnet console** (`127.0.0.1:9000`) as a second admin identity
   (`fixtures/admin_account.sql`, status 250), runs `who Testchar`, and with
   `HARNESS_AUTO_KICK=1` kicks and retries. It exists because a SQL `DELETE`
   alone leaves the world's in-memory `Client` struct, which `Save()`s back on
   logout and corrupts the next run. `fable`'s equivalent is the *manual*
   `.eqemu-lock` file convention — which does not see a leftover in-memory CLE
   at all. `fable`'s own drivers document the exact damage: T104's driver
   comments on ~15 leftover `a_scaled_wolf_pup000..014` from an earlier session
   at the same coordinates.
5. **A live protocol assertion with a named failure reason.**
   `scenario_arena_idle.cpp` is not a smoke test — it is a four-stage C→S→C
   position-acceptance oracle: WaitZoneIn (assert fully zoned, zone name
   `arena`, non-zero spawn id, within 10 units of the seeded point) →
   RoundtripPrime (snapshot `GetSelfClientUpdateCount()` and
   `GetPositionUpdateSentCount()`, nudge +5, send) → RoundtripAwaitSend (drive
   the throttled sender until the *sent* counter advances, 5s sub-timeout) →
   RoundtripValidate (assert the server echo lands at target or is reported as
   a warp-back, within 1.5 units). `fable`'s live checks are
   `grep -qE <pattern> transcript` plus one record-count threshold in
   `record_tour.sh`.
6. Parameterised fixture profiles (`FIXTURE_*`, `fixtures/profiles/*.env`,
   `FIXTURE_DRY_RUN=1` which renders SQL without a DB and is what makes the
   seeder unit-testable), per-phase structured logging
   (`harness_logging.env`), server-free harness self-tests
   (`test/harness/tests/run_all.sh`), and a `MOD_SCENARIO` logging module
   pinned to INFO so pass/fail survives `--log-level=NONE`
   (`src/client/main.cpp:33`–`42`).

This is the one part of the branches worth real money. It is also the part with
the least dependence on the branch's `eq.cpp` architecture — `ScenarioRunner`,
the registry, and the harness scripts touch `EverQuest` only through public
accessors (`IsFullyZonedIn`, `GetCurrentZoneName`, `GetEntityID`,
`GetPosition`, `SendPositionUpdate`, the update counters), all of which exist on
`fable` today.

Honest caveat on its value: `fable`'s in-process handshake suite already
asserts much of what a live scenario would.
`tests/test_zone_connection_flow.cpp` has 35 tests including
`ZoneLoadSnapshot.TwoZoneSequenceResolvesAgainstSecondZoneTree` (`:1266`) and
`DispatcherRobustness.*` (`:332`–`:427`, unknown opcode / zero-length /
truncated payload). The harness's unique catch is narrower than 22 files
suggests: it catches only what a real server produces.

### M4 — The data-driven zone-packet dispatch table (architectural, optional)

`fable` keeps a hand-written `switch` at `src/client/eq.cpp:800`–`1682` with
**143 `case HC_OP_*` labels**, delegating to the `eq_*.cpp` bodies. The branch
replaces it with `GameRuntime::registerZonePacketHandlers`
(`src/client/game_runtime_zone_dispatch.cpp`). This is a *preference*, not a
capability — and `fable`'s `eq.cpp` is already 61% smaller than the branch's
(3,913 vs 10,059). `fable`'s switch is directly tested; the branch's table is
not.

### M5 — `IBridgePublisher` narrowing seam (~50 lines)

`include/client/bridge/bridge_publisher.h`: a one-method interface so
subsystems publish events without seeing `GameStateBridge`'s
`drainEvents`/`drainIntents`/`applyEvent` surface. Genuinely absent on `fable`.
Trivially rebuildable; not worth a merge.

### M6 — Branch B specifically: nothing but one sentence

Branch B's unique content is 379 lines of markdown in two files and **zero
lines of code**. Its one durable output is an operational fact worth copying by
hand into `refactor/TESTING_CHARTER.md`:

> `EverQuest::SendChatMessage("#zone nexus", "say")` reaches the EQEmu GM
> command processor over the wire, because the server intercepts SAY messages
> whose first character is `#`. No client-side GM-command plumbing is needed to
> script a zone change; the account only needs `status >= 80` (the PEQ default
> for `#zone`).
> — `docs/reports/eqemu_zone_line_transition_scenario_report.md`, checkpoint 0

That is the entire harvest from branch B.

### Explicitly NOT missing (tested, not assumed)

- **Zone-line / zone-transition capability.** `fable` has `data/zone_lines/*.json`
  for **115 zones**, `src/client/zone_lines.cpp`, `tests/test_zone_lines.cpp`,
  `tools/generate_zone_lines.py`, `tools/zone_line_editor.cpp`,
  `refactor/T19_map_zone_lines.md`, `EverQuest::RequestZoneChange()`
  (`src/client/eq_zone.cpp:248`) with the T231 same-zone-relocation guard
  (`eq_zone.cpp:73`–`102`) and its test seam (`include/client/eq.h:878`–`880`).
  The branch's entire `zone_lines.cpp` +59 is one method,
  `ZoneLines::processServerZonepointsPacket()`, a byte-for-byte lift of the
  parser out of `EverQuest::ZoneProcessSendZonepoints` — same
  `2 + 4 + n*24` layout, same field offsets. `fable` has the identical code at
  `src/client/eq_entities.cpp:601`–`670` (dispatched from `src/client/eq.cpp:881`),
  and **additionally** has the post-zonepoints handshake steps
  (`ZoneSendSetServerFilter` fallback at `:663`, `ZoneSendClientReady` at
  `:669`) that the branch's extracted method *drops*. Taking the branch here
  would be a regression.
- **spell / skill / inventory / trade / buff manager changes.** All five diffs
  are verbatim "move the `ZoneProcess*` body onto the owning manager", with
  packet layouts, size checks, log lines and bridge publishes copied unchanged;
  the branch's own comments say so. The only real change is
  `m_eq->QueuePacket(...)` → `m_send_zone_packet_fn(...)`. Every behaviour is
  live on `fable`: `InventorySlotChanged` at `src/client/eq.cpp:632`–`641`,
  `:1099`–`1110` and `src/client/eq_merchant.cpp:69`–`76`; `SkillValueChanged`
  at `src/client/eq.cpp:1215`–`1221`; `SpellScribeCompleted` at
  `src/client/eq.cpp:1534`–`1540`; `UpdateInventoryStats()` defined at
  `src/client/eq_bridge.cpp:1455` with five live call sites.
  **`tests/test_spell_database.cpp` is net −296 lines on the branch** (125 ins /
  421 del, rewritten down to 7 tests). `fable`'s version has ~25 including
  `EffectSlotsMatchConfirmedReferenceSpell`,
  `UnusedSlotMarkerNeverMisreadAsRealEffect`,
  `NegativeSpellAffectIndexRoundTrips`, `CacheNeverExceedsCapacity` and four
  reagent round-trips. **Taking the branch's file would be a coverage
  regression.**
- **`irrlicht_renderer.cpp` / `lighting_system.cpp` / `collision_system.cpp`.**
  Extraction-only: `irrlicht_renderer.cpp` is 340 ins / 719 del (net −379),
  essentially all field-access rewrites (`fogEnabled_` →
  `lightingSystem_.fogEnabled()`, `regionTriangles_` →
  `collisionSystem_.rayTestRegion(...)`).
  `LightingSystem::computeTimeOfDayAmbient` uses the same constants and the same
  0–4/5–6/7–17/18–19/20–23 buckets as `fable`'s
  `src/client/graphics/irrlicht_renderer.cpp:1626`–`1680`.
  `CollisionSystem::extractWorldTriangles` / `findBspRegionForPoint` /
  `rayTestRegion` already exist as `IrrlichtRenderer` members at
  `src/client/graphics/renderer_player_physics.cpp:1146`, `:1029`
  (declared `include/client/graphics/irrlicht_renderer.h:806, 2008`).
  Meanwhile `fable` has **21** `renderer_*.cpp` TUs plus `graphics/detail/*` and
  `graphics/environment/*` (8 emitters, boids, storm layers) that the
  merge-base does not have. **Porting the branch's split would be a regression
  in decomposition granularity.**
- **x86 GLES2 / llvmpipe bring-up.** Different capability, and `fable`'s is more
  advanced. The branch's `scripts/build-x86-gles2-irrlicht.sh` (147 lines) +
  `docker/irrlicht-x86-gles2-patch.py` (385 lines) build a *parallel* X11+EGL
  GLES2 patch unrelated to the ARM DRM build. `fable`'s
  `scripts/build-gles2-x86.sh` (212 lines) deliberately reuses
  `docker/irrlicht-drm-patch.py` **unmodified** so x86 and production ARM share
  one `COpenGLES2Driver`/`CIrrDeviceFB` code path, forks only `CIrrDeviceFB` for
  a Mesa surfaceless-EGL fallback, and carries diagnostic modes
  (`--t122-texprobe`, `--t130-audio-tidprobe`). Nothing to port.
- **`tests/test_wld.cpp`, `tests/test_pcap_replay.cpp`.** These are the only two
  branch-only test TUs besides the three noted above, and both were
  *deliberately restructured* on `fable`, not lost: `250d303b` "T11: split
  tests/test_wld.cpp into 9 TUs + shared fixture header"; `1f11507a` "R03:
  register pcap protocol tests with capture guards".
- **Process scaffolding.** `docs/agent_tasks/`, `scripts/agent-task-*.sh`,
  `docs/pm/`, the Codex-checkpoint protocol — all superseded by
  `refactor/EXECUTOR_PROTOCOL.md` + `refactor/deferred.md` + the slot model.

---

## 4. What it would cost to land

### The merge does not work. Measured, not estimated.

`git merge-tree --write-tree fable task/eqcpp-zoneprocess-track-b-remaining`
exits 1 with **18 conflicted files**:

```
.gitignore                                CMakeLists.txt
docker/irrlicht-drm/COpenGLES2Driver.cpp  include/client/application.h
include/client/eq.h                       include/client/skill/skill_constants.h
src/client/application.cpp                src/client/combat.cpp
src/client/eq.cpp                         src/client/graphics/irrlicht_renderer.cpp
src/client/graphics/rdp/rdp_server.cpp    src/client/skill/skill_manager.cpp
tests/CMakeLists.txt                      tests/test_logging.cpp
tests/test_rdp_server.cpp                 tests/test_spell_database.cpp
tests/test_ui_sounds.cpp                  tests/test_wld.cpp (modify/delete)
```

Eighteen files sounds tractable. It is not — the volume inside them is the
story. Resolving the three-way merge per file with `git merge-file`:

| File | Conflict hunks | Lines inside conflict regions |
|---|---|---|
| `src/client/eq.cpp` | **54** | **11,171 of 12,740 merged lines (88%)** |
| `src/client/graphics/irrlicht_renderer.cpp` | **9** | **12,455** |
| `include/client/eq.h` | **36** | **1,317** |

**~25,000 lines of conflict** in three files. These are not conflicts in the
normal sense — they are two independent, complete rewrites of the same
19,676-line ancestor, and a textual resolution would be a line-by-line
re-derivation of `fable`'s architecture with extra steps.

### The clean part of the merge is worse than the conflicted part

The 343 files the branch *adds* merge **cleanly** — as additions. That means a
completed merge would drop `EntityTracker`, `PlayerMovement`, `SocialController`,
`InteractionController`, `WorldProjection`, `ChatSystem`, `ConnectionLifecycle`,
`CharacterStatController`, `GameRuntime`, `ZoneEnvironment`, `LightingSystem`
and `CollisionSystem` into a tree that already implements every one of those
responsibilities in `eq_entities.cpp`, `eq_movement.cpp`, `eq_group.cpp`,
`eq_merchant.cpp`, `eq_login_world.cpp`, `eq_player_info.cpp`, `eq_zone.cpp` and
`renderer_player_physics.cpp`.

That is **107 duplicate state stores (T286) all over again, but at
whole-subsystem scale, and self-inflicted.** Nine of T286's duplicates had
already drifted by the time they were found. This would create a dozen more,
each one a full packet-handler family, none of them unit-tested.

### The branch is not test-backed

```
branch tests/*.cpp : 61 TUs        fable tests/*.cpp : 168 TUs
on fable, not on branch : 112      on branch, not on fable : 5 (3 real)
```

`grep` of the branch's `tests/CMakeLists.txt` for any of
`entity_tracker|player_movement|social_controller|interaction_controller|world_projection|chat_system|connection_lifecycle|character_stat|game_runtime|zone_environment|lighting|collision` returns **nothing**. Not one of the ~14
extracted classes has a unit test. The branch's decomposition is verified by
116 narrative report files; `fable`'s is verified by ~3,200 assertions.

### Cost summary

| Option | Effort | Outcome |
|---|---|---|
| Merge branch A | Weeks. ~25k conflict lines across `eq.cpp`/`eq.h`/`irrlicht_renderer.cpp`, then a dozen duplicated subsystems to reconcile, with no test net on either the branch side or the merge. | A worse tree than we have now. |
| Merge branch B | Same as A (it *is* A) plus two markdown files. | Same. |
| Cherry-pick the daybreak + RDP fixes | **~half a unit.** ~120 lines of source across 2 files, plus 5 tests re-expressed on `fable`'s existing `fake_udp_transport.h` / per-`TEST_F` fixtures. Neither file is in the branch's decomposition. | Two real defects closed, with regression tests. |
| Rebuild the scenario lane fresh | **1–2 units.** The C++ side is ~500 lines (`ScenarioRunner`, registry, one scenario, two CLI flags, exit-code plumbing in `main.cpp`/`application.cpp`); the harness scripts are ~900 lines of bash + SQL that would want rewriting against `fable`'s `scripts/eqemu/env.sh` and `.eqemu-lock` protocol anyway. | The one genuinely uncovered lane, built on `fable`'s architecture. |

Cherry-picking the daybreak fix as a *commit* is not viable — it is spread
across the 297 shared commits and interleaved with the decomposition. Port it as
a patch, reading the branch as reference.

---

## 5. Recommendations

### `task/eqemu-zone-line-transition-scenario` — **ABANDON.** Confidence: very high.

It carries **zero lines of code** of its own (two markdown files, 379 lines).
Every code claim ever made about it belongs to branch A. The capability it was
written to prove — multi-zone transition — is not merely present on `fable` but
substantially ahead of the branch (115 zones of zone-line data,
`tests/test_zone_lines.cpp`, T231's relocation guard, and
`ZoneLoadSnapshot.TwoZoneSequenceResolvesAgainstSecondZoneTree` asserting the
two-zone sequence in-process with no server).

Before deleting: copy the `#zone` GM-over-SAY fact (M6) into
`refactor/TESTING_CHARTER.md`. That is the whole harvest.

*What would change my mind:* nothing short of finding code on it. There is none;
`git diff --stat 4b5f2c8d 83e8a19e` is two `.md` files.

### `task/eqcpp-zoneprocess-track-b-remaining` — **CHERRY-PICK TWO FIXES, REBUILD ONE CAPABILITY, ABANDON THE REST.** Confidence: high.

**Do not merge.** The refactor that gives the branch its name is superseded:
`fable` reached a 3,913-line `eq.cpp` where the branch reached 10,059, by a
different and better-tested route. Merging would import a dozen duplicate
subsystems and ~25,000 conflict lines to arrive somewhere worse.

Three actions, in priority order:

1. **Port the Daybreak teardown fixes now** (M1) — highest value in the audit and
   unrelated to anything the branch is named for. Three source guards in
   `src/common/net/daybreak_connection.cpp` (`Tick()` `shared_ptr` hold; snapshot
   loops in `Process()`/`UpdateDataBudget()`/`ProcessResend()`; erase-before-
   `ChangeStatus` in the `StatusDisconnecting` path) plus the branch's three test
   cases re-expressed on `fable`'s `tests/test_daybreak_state_machine.cpp`
   fixture. Class A, executor-ready, roughly a half unit.
2. **Port the two RDP resource-leak fixes** (M2) — `start()`-failure cleanup and
   the `!initialized_ && !running_` `stop()` guard, plus the two tests as
   `TEST_F`s on `fable`'s existing per-test-`initialize()` fixtures. Keep
   `fable`'s join-before-close block and drop the branch's `GTEST_SKIP`
   scaffolding. Class A, small.
3. **Rebuild the live-scenario lane fresh** (M3) rather than porting it — 1–2
   units, informed by the branch as a design reference. `fable` has the stack
   lifecycle the branch lacks (`up.sh`/`down.sh`/`status.sh`) and the branch has
   the scoring the `fable` lacks (exit codes, precheck, reset/seed, in-client
   assertions). The exit-code taxonomy alone is worth adopting: `fable`'s
   `main.cpp` returns 0 unconditionally, which is why every live verification on
   the board still ends with a human reading a transcript. **This one is a
   product/priority call for Will, not an executor decision** — it is new
   capability, not a defect fix.

Then delete both branches.

**Explicitly do not take:** `tests/test_spell_database.cpp` (net −296 lines,
a coverage regression), `zone_lines.cpp`'s extracted method (drops `fable`'s
post-zonepoints handshake steps), the renderer/lighting/collision split (coarser
than `fable`'s 21-TU decomposition), the x86-GLES2 scripts (a divergent second
code path where `fable` deliberately shares one with ARM), and the agent-queue
process scaffolding.

*What would change my mind:* (a) if Will wants the data-driven dispatch table
(M4) as an architectural direction, that is a legitimate `fable`-side unit — but
it should be written against `fable`'s 143-case switch, not merged from here;
(b) if the live-scenario lane is judged not worth 1–2 units, then items 1 and 2
are the entire harvest and both branches can be deleted after those two patches
land.

---

## 6. Board update owed

`refactor/deferred.md` tier 4 item 22 is **RESOLVED by this audit** and should
be struck, replaced by:

- a class-A item for the Daybreak teardown port (M1)
- a class-A item for the two RDP leaks (M2)
- a class-C item for Will on whether to build the live-scenario lane (M3)
- a one-line note in `refactor/TESTING_CHARTER.md` for the `#zone`-over-SAY fact (M6)
- a class-B note that `src/client/mode/automated_mode.cpp`'s
  `m_automationCallback` has **no registrations anywhere in the tree** — a fresh
  instance of the implemented-but-unreached pattern, found incidentally here

The "TWO UNMERGED BRANCHES ARE 300 COMMITS AHEAD" paragraph at
`refactor/deferred.md:435` should be corrected: it is 300 shared commits with
two 3-commit tips, not 600 commits of unreviewed work, and one of the two tips
contains no code.
