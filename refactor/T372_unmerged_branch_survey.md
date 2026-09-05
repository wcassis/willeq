# T372 — survey of the two never-audited branches (B026)

Class P, slot3, read-only on the branches (no checkout of them into the
slot — use `git log`, `git diff`, `git show` against `fable`). No server.

Two branches predate the lettered programme and have never been audited:
`task/eqcpp-zoneprocess-track-b-remaining` and
`task/eqemu-zone-line-transition-scenario`. Their merge-base with `fable`
is old; "300 commits ahead" is mostly fable's own history moving on.

1. For each branch: merge-base, commits unique to the branch
   (`git log --oneline fable..<branch>`), files touched unique to the
   branch (`git diff --stat <merge-base> <branch>`), and for every file
   whether it still exists on `fable` at the same path (Batch K/L moved
   most of the client).
2. Classify each unique commit into: (a) already present on `fable` by
   another route (find the commit or unit that did it — cherry-pick
   detection with `git cherry` is a start, reading is the proof);
   (b) superseded (the code it touched no longer exists or was rewritten
   by a lettered unit — name the unit); (c) still relevant — a behaviour
   or test `fable` lacks. For (c) describe the behaviour in one sentence
   and where it would land on the current tree.
3. Recommendation per branch: DELETE (all a/b), or a list of (c) items to
   port as board entries (B130+; class by the usual rule). Do not port
   anything in this unit; do not delete the branches (the orchestrator
   does that on the ruling).
4. Review: the two tables. Strike B026 if both branches classify fully.

## Proof

Docs-only commit (the plan's Review + board). `ctest -R 'Doc'` for the
hygiene tests.

STOP: none expected.

---

## Review

Executed T372 on `task/T372-unmerged-branch-survey`, base `fable` @ `3964d100`.
Docs-only; both branches read with `git log`/`diff`/`show` from slot3, neither
checked out. No server.

### 0. The branches are one branch with two three-commit tips

| Measure | Value |
|---|---|
| `merge-base(fable, A)` = `merge-base(fable, B)` | `92e2f260` |
| `merge-base(A, B)` | `4b5f2c8d` "Set up Track B ZoneProcess task" |
| `rev-list --count fable..A` | 300 |
| `rev-list --count fable..B` | 300 |
| `rev-list --count 92e2f260..4b5f2c8d` (shared by BOTH) | **297** |
| `rev-list --count A..B` / `B..A` | 3 / 3 |

So the board's "two branches, 300 commits each" is **297 shared commits plus
six divergent ones**, not ~600 commits of unreviewed work. Every code claim
about branch B belongs to the shared trunk; B's own three commits are two
markdown files, zero lines of code
(`git diff --stat 4b5f2c8d 83e8a19e` = 2 files, +379).

`refactor/T305_branch_audit.md` (base `9bb156ea`) already surveyed this
lineage and its two class-A recommendations **landed**: T306 ported the
Daybreak teardown fixes (`src/common/net/daybreak_connection.cpp`, comments
cite T306, plus five `TEST_F`s in `tests/test_daybreak_state_machine.cpp:792+`
explicitly "ported from the audited branch") and the two RDP resource leaks
(`releaseListenerResources()` at the `Open()`-failure path and the
`!initialized_ && !running_` `stop()` guard). T372 re-verifies that against
the current tree and completes the per-commit classification B026 asked for.

### 1. Classification — the 297 shared commits (identical for both branches)

| Class | Count | What it is | Evidence it is (a) or (b) |
|---|---|---|---|
| **(a)** already on `fable` by another route | **8** | Daybreak `Process()`/`Tick()` teardown hardening (`c97c4d0c`); three RDP lifecycle fixes (`c7c7a226`, `68056043`, `80a87572`); logout-driven disconnect deferred out of the packet-callback stack (`57f7d526`); `/time` override mirrored into `GameState::world()` (`3760e694`); the registered zone-packet dispatch table replacing the opcode `switch` (`d6f5c64f`, `9dbf9092`) | T306 for the net + RDP work (source comments name it); H03/T322 for the logout deferral (`src/client/player/player_component.cpp:211`, `src/client/movement/movement_update.cpp:72`); `src/client/commands/commands_movement.cpp:323` for `/time`; K09's `kZoneOpcodeTable` (`src/client/zone/zone_dispatch.cpp:687`) with **zero** `case HC_OP_` left in the tree |
| **(b)** superseded — decomposition | **76** | The `eq.cpp` ZoneProcess programme: ~14 controller classes (`EntityTracker`, `PlayerMovement`, `ChatSystem`, `ConnectionLifecycle`, `ZoneEnvironment`, `SocialController`, `InteractionController`, `CharacterProfile`/`CharacterStatController`, `WorldProjection`, `GameRuntime`, `LightingSystem`, `CollisionSystem`, `IBridgePublisher`) | Batches J/K/L/G. Every one of the 33 new `src/client`/`include/client` paths is absent from `fable` because the same responsibilities live in the K components (`src/client/entities/`, `movement/`, `chat/`, `login_world/`, `zone/`, `player/`, `commands/`) and the eleven L renderer components. `fable` reached a smaller core than the branch and pinned it with the M00 1,500-line ceiling |
| **(b)** superseded — process scaffolding | **180** | Codex task cards, per-task narrative reports, `scripts/agent-task-*.sh`, `docs/pm/`, worker-prompt tuning, queue reconcilers | 235 of the branch's 343 added files are `docs/tasks/` + `docs/reports/`; all of it is replaced by `refactor/EXECUTOR_PROTOCOL.md` + `refactor/BOARD.md` + the slot model |
| **(b)** superseded — second x86 GLES2 code path | **9** | `scripts/build-x86-gles2-irrlicht.sh` + `docker/irrlicht-x86-gles2-patch.py`, a parallel X11+EGL Irrlicht patch | `fable`'s `scripts/build-gles2-x86.sh` deliberately reuses the **unmodified** `docker/irrlicht-drm-patch.py` so x86 and ARM share one `COpenGLES2Driver`; taking the branch's fork would re-split them |
| **(c)** still relevant | **24** | 2 small defects + the 22-commit live-scenario lane — itemised in §3 | — |
| | **297** | | |

### 2. Classification — the six divergent commits

| Branch | Commit | Content | Class |
|---|---|---|---|
| A `…track-b-remaining` | `244be9a2` | ownership plan / risk inventory (markdown) | (b) process |
| A | `e4f70407` | `ZoneProcessPlayerProfile` moved out of `eq.cpp` into `CharacterStatController` | (b) — `fable` has it at `src/client/zone/zone_entry.cpp:241` (K09), and T332 has since rewritten its z handling |
| A | `127c81e5` | cp1 fixup: use public `EverQuest` accessors instead of the friend seam | (b) — B038 (T366) already removed every non-`CombatManager`/`TradeManager` friend and every `EverQuest&` transition handle |
| B `…zone-line-transition-scenario` | `c0f60a36` | task card (markdown) | (b) process |
| B | `a6043b3d` | checkpoint-0 feasibility inventory (markdown) | (a) — its one durable fact, that `/say #zone <zone>` reaches the eqemu GM processor because the server intercepts SAY messages beginning with `#`, is established practice across `fable` (`refactor/P20b`, `P33`, `P34`, `P13b`, `P15`, `BOARD.md:1506`) |
| B | `83e8a19e` | records the checkpoint-0 commit hash (markdown) | (b) process |

**Branch B never reached checkpoint 1. It carries no implementation of its own.**

### 3. The (c) list — what `fable` genuinely lacks

1. **`m_pending_door_clicks` is never cleared at zone change** (`ec7f14da`).
   Door IDs are `uint8_t` and are reissued by each zone's `SpawnDoor`, so a
   click sent just before zoning leaves a stale entry that flags the *next*
   zone's first matching `OP_MoveDoor` as user-initiated — which changes the
   `userInitiated` field of the `DoorStateChanged` event the renderer consumes,
   and the debug log line. **Lands:** the zone-cleanup path of
   `eqt::entities::EntitiesComponent`, beside where the door state manager is
   cleared; member declared `include/client/entities/entities_component.h:115`,
   erased at `src/client/entities/entities_component_world.cpp:139`, inserted at
   `:255`. Confirmed by grep: those three sites plus the declaration are the
   only occurrences tree-wide, and none of them is a clear-on-zone-change.
   → **B130**, class A (small).
2. **`HC_OP_TargetCommandReply` has no symbolic opcode** (`6c082a4f`). `fable`
   registers it as the bare literal `0x61f9` with a trailing comment
   (`src/client/zone/zone_dispatch.cpp:773`) while every sibling entry uses an
   `HC_OP_*` enumerator; the branch added the enumerator plus a self-check that
   every registered opcode resolves to a name. **Lands:**
   `include/client/eq_opcodes.h` (next to `HC_OP_TargetCommand = 0x1477`),
   the table entry above, and `src/client/core/eq_strings.cpp`.
   → **B131**, class B (hygiene).
3. **The live-server scenario lane** (22 commits: `3f2e90e6`, `0ce0c016`,
   `59c0c07e`, `547e6808`, `0b0dd650`, `48dbedcc`, `1265532e`, `8ec891f1`,
   `1650437b`, `493e8ec0`, `9222683c`, `59d31da7`, `818ead31`, `6383ad14`,
   `f0dda9bc`, `4db4a0cc`, `ad9481eb`, `2515bb74`, `31c3c143`, `d790cb91`,
   `ebeb555f`, `501e694a`). A `--scenario <name> --timeout <n>` client mode
   driven by a `ScenarioRunner` + `IScenario` registry, a shared exit-code
   taxonomy (0 pass / 2 operator / 3 stale session / 4 kick failed / 10
   assertion / 11 timeout / 12 stack failure) with a guard so an external quit
   mid-run cannot masquerade as success, a `MOD_SCENARIO` log module pinned
   above `--log-level`, one real scenario (`arena-idle`, a four-stage C→S→C
   position-acceptance oracle), and `test/harness/`
   (`precheck`→`reset`→`seed`→`normalize`→`run-scenario` over SQL fixtures,
   with a world-telnet stale-session kick). **Verified still absent:**
   `src/client/main.cpp` ends `return 0;` unconditionally (`:163`), there is no
   `scenario` token anywhere in `main.cpp` or the `application_*.cpp` argument
   parser, and `src/client/scenario/`, `include/client/scenario_runner.h`,
   `test/harness/` do not exist. **Lands:** new `src/client/scenario/` +
   `include/client/scenario_runner.h`, two flags in the `Application`
   argument parser, exit plumbing through `main.cpp`, and harness scripts
   beside `scripts/eqemu/up.sh`/`down.sh` (which the branch lacks and which
   would be kept). T305 sized a fresh rebuild at 1–2 units and called it a
   product decision. → **B132**, class C (new capability, Will's call).

Incidental, found while checking (3): `AutomatedMode::setAutomationCallback`
(`include/client/mode/automated_mode.h:65`) has **zero callers anywhere in
`src/`, `include/`, `tests/` or `benchmarks/`** — the seam the scenario lane
would plug into is a live instance of the implemented-but-unreached pattern.
→ **B133**, class B. (T305 flagged this at `9bb156ea`; it is still true at
`3964d100`.)

Also noted, not boarded: `GLContextHandles`
(`include/client/graphics/loading_thread.h:71`–`86`) still declares its three
EGL members under `#ifdef EQT_HAS_GLES2`, so its size and layout depend on a
compile flag. The branch hit a real ODR bug from exactly this (`ad9481eb`) and
made the members `void*`. `fable` is safe **only because** `EQT_HAS_GLES2` is
set with directory-scope `ADD_DEFINITIONS` at `CMakeLists.txt:762`, before
`ADD_SUBDIRECTORY(tests)` at `:2053`, so every TU that includes
`application.h` sees the same layout. Anyone moving that to
`target_compile_definitions` reintroduces the bug silently.

### 4. Explicitly do NOT take (re-confirmed at this tip)

`tests/test_spell_database.cpp` (net −296 on the branch, a coverage
regression); `ZoneLines::processServerZonepointsPacket` (the branch's extracted
parser **drops** the post-zonepoints `ZoneSendSetServerFilter` fallback and
`ZoneSendClientReady` that `fable` keeps at
`src/client/entities/entities_component.cpp:547`/`:553`); the
`LightingSystem`/`CollisionSystem` renderer split (coarser than Batch L's
eleven components); the x86-GLES2 scripts; the agent-queue scaffolding; the
`IBridgePublisher` seam (superseded by K13's `ClientContext`/context services).

### 5. Recommendation

| Branch | Recommendation |
|---|---|
| `task/eqemu-zone-line-transition-scenario` | **DELETE.** Zero lines of its own code. Its 297 shared commits are branch A's, and its one durable fact is already established `fable` practice. Nothing to port. |
| `task/eqcpp-zoneprocess-track-b-remaining` | **DELETE, after B130/B131 are worked and B132 is ruled on.** Its two class-A recommendations from T305 already landed in T306. What remains is two small items (B130, B131) that are a couple of lines each and do not need the branch to stay alive — both are described in full above — plus the scenario lane (B132), which T305 and T372 both recommend **rebuilding fresh** against `fable`'s architecture rather than porting, using the branch only as a design reference. A merge is not viable: `git merge-tree` gives 18 conflicted files with ~25,000 lines inside conflict regions in `eq.cpp`/`eq.h`/`irrlicht_renderer.cpp`, and the *clean* part of the merge is worse — it would drop a dozen duplicate subsystems into a tree that already implements all of them. |

Deletion is the orchestrator's call, not this unit's; nothing was merged,
pushed, ported or deleted.

### 6. Verification

`ctest --test-dir build -R Doc --output-on-failure` — **7/7 passed** (`DocCitations`, `CoordinateSystemDocumentation`, `SpellsEffReaderMappingTest.KnownCounterexamplesDocumented`, `ActionDispatcherTest.PetFamily_...`, `PositionFrameRoundTrip....DocumentedConvention`, `ZoneEntitiesNewSpawn.ParsesDocumentedFieldsIncludingXYSwap`, `ShippedMeshCollisionZTest.HumanMaleMatchesTheDocumentedThreeOhFive`), exit 0.
Docs-only unit: no build, no server, no goldens touched.
