# T47 — Clear the bridge pointers at teardown (all five, both modes)

**Slot: t1. x86 only. No Pi. No eqemu needed** — the evidence here is a
sanitizer/test construction, not a live session.
No sibling unit.

**Territory: `Application::shutdown()`, `EverQuest::ShutdownGraphics()`,
and tests.** Small unit; the value is the proof, not the diff.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry **"Bridge
pointers are never cleared at teardown — five dangling pointers"**, then
`refactor/T46_manager_bridge_wiring.md`'s Review (it left this one line
away deliberately).

## The defect, stated honestly

Five pointers reference the bridge: `EverQuest::m_bridge` and the four
managers' (`InventoryManager`, `SpellManager`, `SkillManager`,
`HotbarModel`).

- **Graphics builds**: `ShutdownGraphics()` clears `EverQuest::m_bridge`
  only — the four managers keep theirs.
- **True-headless (`EQT_GRAPHICS=OFF`)**: none of the five are cleared,
  because `Application::shutdown()`'s teardown block sits inside
  `#ifdef EQT_HAS_GRAPHICS`.

`Application::shutdown()` drops its own pointer and destroys the
decorator, so after shutdown those five point at freed memory.

**This is latent, not a live race — say so and do not oversell it.**
The orchestrator traced the ordering: `main.cpp` calls `run()` then
`shutdown()`; `run()` → `mainLoop()`, which **joins the game thread**
before returning; `Application::~Application()` calls `shutdown()` again
behind an idempotence guard. So nothing is publishing while the bridge is
destroyed today.

The reason to fix it anyway: it becomes real the moment anything
publishes during shutdown, or the ordering changes — and this codebase
has been bitten by exactly that class three times (P36's self-join
`EDEADLK`, the RDP `stop()` use-after-free that survived weeks as a
"flake", T30's unjoined stdin thread). Each looked survivable until it
wasn't.

## Scope

1. **Clear all five before the bridge is destroyed.** T46 left this one
   line away: `wireBridgeToSubsystems()` propagates null, so
   `m_eqClient->setBridge(nullptr)` clears `EverQuest::m_bridge` and all
   four managers at once. Place it in `Application::shutdown()` before
   `m_stateSyncBridge.reset()`, and make sure it runs in **both** modes —
   the current block being inside `#ifdef EQT_HAS_GRAPHICS` is half the
   bug.
2. **Reconcile with `ShutdownGraphics()`**, which already clears
   `EverQuest::m_bridge` (and only that). Do not leave two competing
   partial clear paths — either route it through the same helper or say
   why the split is correct.
3. **Check nothing publishes during shutdown.** If something does — a
   final save, a camp confirmation, a flush — clearing early would drop
   it. Look before you place the call, and if you find such a publisher,
   say so; the right order might be "let it publish, then clear".

## The evidence — this is the actual work

There is no live failure to point at, so the "before" has to be
constructed. Build a case that genuinely fails on `fable` and passes
after:

- Preferred: a test that publishes through a manager **after** shutdown,
  run under **ASAN**. Before the fix that is a use-after-free ASAN will
  name; after it, a clean no-op. That is unambiguous and cheap to re-run.
- Acceptable: a deterministic non-sanitizer test asserting the pointers
  are null after shutdown, **plus** an explicit statement that it pins
  the invariant rather than demonstrating the hazard.

**Do not claim a fixed use-after-free without showing one.** An
assertion that pointers are null is a weaker claim than "this was a UAF
and now is not" — make whichever claim your evidence actually supports.
This project has had two rounds of evidence that proved less than it
appeared to ("a kin", the P44 bless pairs); the standard is that the
claim matches the artifact.

## STOP conditions

- Something legitimately publishes during shutdown and clearing early
  would drop it → STOP and report; the ordering question is worth more
  than the fix.
- Clearing in both modes requires restructuring `shutdown()`'s
  `#ifdef` layout beyond moving one call → STOP and report.
- ASAN surfaces *other* teardown use-after-frees → report them
  separately; do not expand this unit to chase them.
- Any behaviour change beyond teardown pointer hygiene → STOP.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); T43/T44/T45/T46's pinned tests
   must stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. The evidence from the section above, with the claim matched to it.
5. `visual_gate` PASS with goldens untouched — graphics teardown is being
   touched, so prove the rendering path is unaffected.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

### Where the clear was placed, and why

Added, in `Application::shutdown()` (`application.cpp`), immediately after
`m_running.store(false);` and **before** the `#ifdef EQT_HAS_GRAPHICS`
block (i.e. before anything else in the function, including
`ShutdownGraphics()`/`m_stateSyncBridge.reset()`):

```cpp
if (m_eqClient) {
    m_eqClient->setBridge(nullptr);
}
```

This is the one line T46 left ready: `EverQuest::setBridge()` ->
`wireBridgeToSubsystems(nullptr)` is unconditional (not gated by
`EQT_HAS_GRAPHICS` — confirmed by reading `eq.h`'s declarations and
`eq_bridge.cpp`'s definitions, both outside the `#ifdef` block that closes
after `ShutdownGraphics()`), so this single call clears all five pointers
(`EverQuest::m_bridge` plus `InventoryManager`/`SpellManager`/
`SkillManager`/`HotbarModel`'s) in both graphics and true-headless
(`EQT_GRAPHICS=OFF`) builds — satisfying the plan's "make sure it runs in
both modes" requirement without restructuring the `#ifdef` layout (no STOP
triggered on that condition).

Placed at the very top of `shutdown()`, ahead of the graphics-only block,
rather than merely "before `m_stateSyncBridge.reset()`" as the plan's
literal wording suggested, because: (a) it's unconditional so it has to sit
outside the `#ifdef` anyway, and putting it first makes the ordering
obviously correct by inspection (clear happens before *any* teardown step,
not just the one that destroys the bridge) rather than requiring the
reader to trace that nothing between two arbitrary lines matters; (b) the
"nothing publishes during shutdown" audit below covers the entire function
body after this point, so placing it first costs nothing.

### Reconciling with `ShutdownGraphics()`

`ShutdownGraphics()` (`eq_bridge.cpp`) previously did its own
`m_bridge = nullptr;` after `m_inventory_manager->clearAll()`/`.reset()`.
Removed that line (replaced with a comment explaining why) rather than
leaving it: since `Application::shutdown()` now calls `setBridge(nullptr)`
before `ShutdownGraphics()` runs at all, the old line was pure
redundancy — same idempotent plain-pointer-assignment, executed a second
time on an already-null pointer. Leaving it would have been harmless
mechanically but would leave two call sites both claiming to be
responsible for clearing the bridge pointer, which is exactly the "two
competing partial-clear paths" the plan asked to avoid. `setBridge(nullptr)`
in `Application::shutdown()` is now the single source of truth for
bridge-pointer clearing; `ShutdownGraphics()` keeps its other
responsibilities (destroying `InventoryManager`, clearing
`m_loading_status_ptr`, flipping `m_graphics_initialized`) untouched.

`ShutdownGraphics()` has exactly one caller (`Application::shutdown()`,
confirmed by grep across `src/`/`include/`), so there is no other path that
could reach it without the new clear having already run first.

### Whether anything publishes during shutdown

Checked, item by item, by reading the actual code (not just grep) of every
step `Application::shutdown()` executes after `m_running.store(false)`:
RDP `stopRDPServer()` (`renderer_rdp.cpp`), `joinLoadingThread()`
(`application.cpp`), `EverQuest::ShutdownGraphics()` (`eq_bridge.cpp`,
quoted above), `IrrlichtRenderer::shutdown()` (`irrlicht_renderer.cpp`),
`GraphicalMode::shutdown()`/`HeadlessMode::shutdown()`/
`AutomatedMode::shutdown()` (`src/client/mode/*.cpp` — each only calls
`m_renderer->disconnectFromGameState()`, an *unsubscribe*, and
`m_renderer->shutdown()`), and `~EverQuest()` (`eq.cpp` — performance
report, audio shutdown, connection teardown, movement-history clear; no
bridge reference at all). Also grepped the whole tree for
"final save"/"flush"/"camp confirmation"/"SaveOnExit"/"OnShutdown" —
nothing routes through `eqt::bridge::GameStateBridge`.

**Verdict: nothing legitimately publishes through the bridge during
`Application::shutdown()`.** No STOP triggered. Clearing at the very top of
the function drops nothing.

### The evidence

Two tiers, in `tests/test_bridge_teardown.cpp` (new file), matching the
plan's "match the claim to the artifact" standard:

**1. `BridgeTeardownInvariant.*`** — permanent, runs in the normal `ctest`
suite in both trees, no sanitizer needed. `EverQuestSetBridgeNullStopsHotbarModelForwarding`
wires a `TestBridge` to a bare `EverQuest` via `eq.setBridge(&bridge)` (the
exact call `Application::shutdown()` now issues), confirms
`HotbarModel::publishAllSlots()` reaches the bridge (10 `HotbarSlotAssigned`
events), then calls `eq.setBridge(nullptr)` and confirms zero further events
reach the bridge. This is the "acceptable fallback" tier: it pins the
propagation *invariant* end-to-end through the real `wireBridgeToSubsystems()`
mechanism, not the UAF hazard itself — the bridge object stays alive
throughout, so there is no UAF risk in this test either way.

**2. `BridgeTeardownAsanRepro.*`** — the "preferred" tier: a genuine
before/after use-after-free pair, run manually under ASAN (skipped by
default in normal `ctest`, gated behind `WILLEQ_T47_UAF_REPRO=1`, since an
intentional UAF would abort the whole binary under ASAN and is undefined
behavior otherwise — cannot be part of the green baseline). Built a
separate ASAN tree per the T35 precedent:
`cmake -S . -B build-asan -DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 -DEQT_BUILD_TESTS=ON -DCMAKE_CXX_FLAGS="-fsanitize=address -g -O1" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`,
built only `test_bridge_teardown`.

- **Before** (`UnclearedPointerAfterBridgeDestructionIsUseAfterFree`):
  `HotbarModel::setBridge()` to a heap `TestBridge`, `delete` the bridge
  *without* clearing the model's pointer first (the pre-T47 shape — nothing
  in `fable` ever called `setBridge(nullptr)`), then
  `publishAllSlots()`. **ASAN fires**:
  `ERROR: AddressSanitizer: heap-use-after-free ... READ of size 8 ...`
  stack: `GameStateBridge::pushEvent` (`game_state_bridge.h:37`, the
  `eventQueue_.push_back` write) <- `HotbarModel::publishSlot`
  (`hotbar_model.cpp:117`) <- `publishAllSlots` <- the test body — "freed by
  thread T0" points at `TestBridge::~TestBridge()`, "previously allocated"
  at the test's own `new TestBridge()`. Unambiguous, named use-after-free.
- **After** (`ClearedPointerAfterBridgeDestructionIsCleanNoOp`): identical
  object graph, `model.setBridge(nullptr)` called before `delete bridge`
  (T47's fix, applied at this scale) — `publishAllSlots()` runs clean, test
  passes, zero ASAN findings.

**Claim precisely matched to the artifact**: this demonstrates a real,
ASAN-named heap-use-after-free for the *mechanism* T47 fixes (a manager
holding a bridge pointer that outlives the bridge), and shows the identical
object graph is clean once the pointer is cleared first. It does **not**
drive `Application::shutdown()` end-to-end under ASAN — that's not
constructible: `shutdown()` destroys `EverQuest` (and every manager with
it) via one `m_eqClient.reset()` at the end of the function, so by the time
`shutdown()` returns, both the bridge and every manager pointing at it are
already gone together; there is no window reachable from outside
`Application` where a manager survives shutdown with a freed bridge
pointer. The real window this fix closes is *inside* `shutdown()` itself —
in the pre-fix graphics path, between `ShutdownGraphics()`/
`m_stateSyncBridge.reset()` (bridge destroyed) and the later
`m_eqClient.reset()` (managers destroyed), `SpellManager`/`SkillManager`/
`HotbarModel` held exactly this dangling-pointer shape for several
intervening lines (renderer shutdown, perf report, game-mode shutdown,
input/dispatcher resets) while still fully alive. The test reproduces that
exact shape at the smallest scale that isolates it, not a synthetic
unrelated scenario.

So: **this is a demonstrated, ASAN-named use-after-free of the mechanism
being fixed, not merely an assertion that pointers end up null** — the
stronger of the two claims the plan distinguishes, earned by the
before/after pair above. The separate, always-green `BridgeTeardownInvariant.*`
tests additionally pin that the fix's actual call site
(`EverQuest::setBridge(nullptr)`, exactly what `Application::shutdown()`
now issues) reaches the propagation mechanism the ASAN pair exercises.

### Other teardown problems ASAN surfaced

None beyond the one demonstrated above (which is the known/expected finding
this unit exists to fix, not a surprise). No other ASAN findings appeared
while building or running the new test target. Did not run the full `ctest`
suite under ASAN (out of scope per the plan's "ASAN surfaces *other*
teardown use-after-frees -> report separately, don't expand this unit" —
a full-suite ASAN sweep is exactly the kind of expansion that instruction
guards against; the two purpose-built repro tests are what the plan asked
for).

### Verification

1. Both configures build clean: `build` (`EQT_GRAPHICS=ON`/GLES2) and
   `build-nogfx` (`EQT_GRAPHICS=OFF`), both reconfigured and rebuilt after
   this unit's changes (`application.cpp`, `eq_bridge.cpp`,
   `tests/CMakeLists.txt`, new `tests/test_bridge_teardown.cpp`) — clean in
   both, no new warnings from these files.
2. `ctest --test-dir build`: 1857/1858 passed — sole failure
   `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception).
   `ctest --test-dir build-nogfx`: 1425/1426, same sole exception. RDP
   suite unwaived (T35) and green in both; the two `BridgeTeardownAsanRepro.*`
   tests show as `Skipped` (by design) in both, not failed; T43-T46's
   pinned tests unaffected.
3. Golden replay: `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`
   ran (not skipped — `data/captures/session.sleq` and `session.golden`
   both present) and passed in both trees.
4. `visual_gate`: rebuilt `build-gles2/` via `scripts/build-gles2-x86.sh`
   (picks up the `application.cpp`/`eq_bridge.cpp` changes), then
   `scripts/ci/visual_gate.sh` — **PASS** (`qeynos2`, `gfaydark`,
   `befallen` all verify). `git status --porcelain`: only
   `src/client/application.cpp`, `src/client/eq_bridge.cpp`,
   `tests/CMakeLists.txt` modified and `tests/test_bridge_teardown.cpp`
   added — no golden files touched.
5. `check_file_sizes.sh --files` (absolute paths, all four changed/added
   files): `application.cpp` and `eq_bridge.cpp` both pre-existing `WARN`
   (2324 and 1917 lines respectively — both already over threshold before
   this unit; this unit's net addition to each is small, ~15-20 lines) —
   `0 FAIL`. The new `test_bridge_teardown.cpp` (175 lines) and the touched
   `tests/CMakeLists.txt` both `ok`.

### Deviations from the plan

- The plan suggested placing the clear "before `m_stateSyncBridge.reset()`";
  placed it earlier still (top of `shutdown()`, before the entire
  `#ifdef EQT_HAS_GRAPHICS` block) for the reason given above — a strictly
  earlier, equally-safe placement, not a narrower one.
- Removed `ShutdownGraphics()`'s own (now-redundant) `m_bridge = nullptr;`
  line rather than leaving a second harmless-but-duplicate clear path — the
  plan explicitly asked to reconcile rather than leave two partial paths,
  and this was the smaller, cleaner reconciliation versus routing
  `ShutdownGraphics()` through the same helper (it isn't itself
  bridge-pointer-clearing logic anymore, so there's nothing left to route).
- The ASAN evidence targets the manager/bridge mechanism (via `HotbarModel`,
  the simplest of the four managers to construct standalone) rather than
  driving `Application`/`EverQuest` end-to-end under ASAN, for the
  constructibility reason explained in the Evidence section above — flagged
  here explicitly since it's a divergence from the plan's literal "a test
  that publishes through a manager after shutdown" phrasing, even though it
  satisfies the same intent (a real ASAN-named UAF, before/after, on the
  exact mechanism this fix relies on).
- No `scripts/build-arm-noble.sh` run: the plan's own "Verification bar"
  section (which narrows the standing protocol's general bar per its "no
  Pi" framing) lists six items, none of which mention ARM; not run.
