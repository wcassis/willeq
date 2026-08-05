# T46 — Wire the managers' bridge pointers in both modes (manager-originated events are dead headless)

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `EverQuest::setBridge()` (currently inline in
`include/client/eq.h`), the wiring block in
`EverQuest::InitGraphics()` (`src/client/eq_bridge.cpp`), and tests.**
Keep it narrow — the fix is a few lines; the value is that it completes
the headless story five units have been building.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry T45 added
about manager bridge pointers, then `refactor/T44_spell_state_via_bridge.md`
(whose spell deltas are the concrete casualty).

## The defect

`InventoryManager`, `SpellManager`, `SkillManager` and `HotbarModel` each
hold their own bridge pointer, and those are wired in exactly one place:
`EverQuest::InitGraphics()` (`eq_bridge.cpp:122-132`), which **never runs
headless**.

`EverQuest`'s own `m_bridge` *is* set headless — `Application` calls
`setBridge()` (`application.cpp:618`) on the ConsoleBridge+decorator path
— which is why the full-state snapshot and the `eq.cpp`/`eq_merchant.cpp`
publishes work there, and why T43's pin passes.

So the headless picture today is: **snapshot state ✓, EverQuest-side
deltas ✓, manager-side deltas ✗ — silently.** The concrete casualty is
`SpellManager`'s own `SpellGemChanged` publish, which T44's spell-delta
conversion depends on: in headless it is inert, so gem changes never
reach the state after the initial snapshot.

## The fix

`setBridge()` is a one-line inline setter in `eq.h` that assigns
`m_bridge` and nothing else. Extract the subsystem wiring into a helper
(`wireBridgeToSubsystems()` or similar) and call it from **both**
`setBridge()` and `InitGraphics()`, so the managers are wired whichever
path sets the bridge. `setBridge()` will need to move out of the header
to call into the manager types.

**Ordering is already verified — do not re-derive:** all four managers
are constructed in the `EverQuest` constructor (`eq.cpp:102-169`, ctor at
`:83`), and `Application` constructs `EverQuest` at `application.cpp:187`
before calling `setBridge()` at `:618`. So the managers exist when
`setBridge()` runs.

**Idempotency**: in graphics mode both paths may end up wiring with the
same pointer. That should be harmless — confirm it, and make the helper
safe to call twice.

## The teardown asymmetry — handle deliberately, do not quietly widen it

`Application::shutdown` sets its own `m_bridge = nullptr` and resets
`m_stateSyncBridge`/`m_irrlichtBridge`, **but nothing ever clears
`EverQuest::m_bridge`** — and that teardown block sits inside
`#ifdef EQT_HAS_GRAPHICS`. Nothing calls `setBridge(nullptr)` anywhere
today.

So `EverQuest` already holds a dangling bridge pointer after shutdown,
and wiring four managers adds four more of the same. That is pre-existing
and presumably survivable only because nothing uses them post-shutdown —
which is exactly the kind of reasoning that stops being true later.

Make the helper propagate whatever it is given, **including null**, so a
clear path exists. Then **report** what you find about teardown — whether
headless resets the bridge at all given the `#ifdef`, and whether
`setBridge(nullptr)` should be called on shutdown — rather than fixing
the lifecycle here. If you conclude it is a real hazard, that is a
finding worth its own unit, not a drive-by.

## STOP conditions

- Wiring in `setBridge()` turns out to run before some manager exists in
  a path not traced above → STOP and report the path.
- Making the managers live headless surfaces a manager publish that is
  *wrong* headless (rather than merely absent) → STOP and report; that
  would mean a manager assumes a renderer.
- Any graphics-mode behaviour change → STOP. Graphics already wires
  these; this must be a no-op there.
- Fixing the teardown lifecycle starts looking necessary to land the fix
  → STOP and report rather than expanding.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); T43's `FullStateSnapshot`,
   T44's spell tests and T45's timing tests must all stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `visual_gate` PASS with goldens untouched — graphics mode is supposed
   to be unaffected, so prove it.
5. **Live headless acceptance, and this is the point of the unit**: show
   a *manager-originated* publish arriving in headless that does not
   today. `SpellManager`'s `SpellGemChanged` is the natural one — memorize
   or cast so a gem changes state, and show it reaching the state via the
   decorator with no full-state snapshot involved. A before/after against
   `fable` makes it unambiguous. eqemu LOCK FIRST; `up.sh` only; restore
   Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190; `down.sh`
   after. Testcaster (Wizard) exists — T44 and T45 both used it. Evidence
   under `benchmarks/audits/T46/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

### The helper and both call sites

Added `EverQuest::wireBridgeToSubsystems(GameStateBridge*)` (declared
private in `include/client/eq.h`, defined in `src/client/eq_bridge.cpp`
right after the `#endif // EQT_HAS_GRAPHICS` block that closes
`InitGraphics()`/`ShutdownGraphics()` — deliberately unconditional, since
the headless path calls it too). It assigns `m_bridge` and then calls
`setBridge()` on each of the four managers
(`m_inventory_manager`/`m_spell_manager`/`m_skill_manager`/`m_hotbar_model`)
with the same pointer, guarded on the `unique_ptr` being non-null (matches
the guard style already used at the old `InitGraphics()` call site).

`EverQuest::setBridge()` (`eq.h:1037`, previously a one-line inline setter
assigning only `m_bridge`) is now a one-line declaration in the header,
defined in `eq_bridge.cpp` as `{ wireBridgeToSubsystems(bridge); }`. It had
to move out of the header because `eq.h` only forward-declares the four
manager types (confirmed by grep — `InventoryManager`/`SpellManager`/
`SkillManager`/`HotbarModel` are all `class X;` forward decls in `eq.h`),
so calling `->setBridge()` on them requires the complete types, which are
only available in `eq_bridge.cpp` (all four manager headers are already
included there unconditionally, not gated by `EQT_HAS_GRAPHICS` — confirmed
by reading the file's top).

`InitGraphics()`'s old inline wiring block (the four `if (m_x) { m_x->setBridge(m_bridge); }`
lines, `eq_bridge.cpp:121-133` pre-fix) is replaced with a single
`wireBridgeToSubsystems(bridge)` call; the hotbar's `setSaveCallback()` line
(not bridge-related) stays as a separate statement right after it.

Both call sites: `EverQuest::setBridge()` (the headless path — called from
`Application::init()`'s console-bridge branch, `application.cpp:618`, which
runs in every headless configuration: `EQT_GRAPHICS=OFF` unconditionally,
and the `EQT_HAS_GRAPHICS` tree whenever `--no-graphics`/non-interactive
mode leaves `m_graphicsInitialized` false) and `EverQuest::InitGraphics()`
(the graphics path, called from `Application::init()`'s graphics branch,
`application.cpp:545`).

### Ordering

Confirmed per the plan's pre-verified trace, not re-derived: all four
managers are constructed in the `EverQuest` constructor
(`eq.cpp:102-169`), and `Application` constructs `EverQuest` at
`application.cpp:187`, before either `setBridge()` (`:618`) or
`InitGraphics()` (`:545`) can run. Live evidence corroborates it directly
— a temporary diagnostic in `wireBridgeToSubsystems()` (added for this
verification, reverted before the final commit — see Deviations) logged
non-null manager pointers at the moment of wiring:
`t46_diag_after_fixed_nogfx.log:24`: `wireBridgeToSubsystems called
bridge=0x56fd...c60 inv=0x56fd...ff0 spell=0x56fd...0c0 skill=0x56fd...0c0
hotbar=0x56fd...a40` — all five pointers non-null.

### Idempotency

`wireBridgeToSubsystems()` is plain pointer assignment on `m_bridge` and on
each manager's own `setBridge()` (itself a plain assignment, e.g.
`SpellManager::setBridge()` is `{ m_bridge = bridge; }`) — calling it twice
with the same pointer is a no-op by construction, and calling it with a
different pointer simply re-points everything, no state to corrupt either
way.

Traced whether graphics mode's two potential wiring paths (`setBridge()`
and `InitGraphics()`) actually both fire in a single run: they do not.
`Application::init()`'s console-bridge branch is gated on
`!m_graphicsInitialized && !m_bridge` (`application.cpp:612`); if graphics
init succeeded, `m_graphicsInitialized` is true and that branch is skipped
entirely, so `setBridge()` is never called directly by `Application` in a
graphics-mode run — only `InitGraphics()` wires the managers. If graphics
init failed, `Application::init()` returns `false` before reaching that
branch (`application.cpp:605`), so the console-bridge path never runs
either. So in the code as it exists today, only one of the two call sites
ever fires per process; the plan's "may end up wiring with the same
pointer" scenario doesn't currently arise, but the helper is safe
regardless — confirmed by reading, not just asserted.

### Before/after: a manager-originated publish arriving headless that did not before

**eqemu LOCK FIRST**: created (`.eqemu-lock`) before `up.sh`, deleted after
clean `down.sh` — confirmed absent both before and after.

Seeded `Testcaster` (id 67, Wizard) with Frost Bolt scribed/memorized in
gem 1 (`INSERT INTO character_spells/character_memmed_spells (id, slot_id,
spell_id) VALUES (67, 0, 54)`, T44/T45's exact recipe), moved to the
`arena` zone (zoneidnumber 77, T45's "quiet zone" precedent) at the exact
coordinates a GM-spawned rat (`#npctypespawn 351043`) appears at in that
zone (`x=146, y=-1009, z=51` in DB-column terms — determined empirically:
`checkRange` debug output showed the rat consistently spawning at a fixed
point regardless of the caster's own position, so the caster was moved to
meet it rather than the reverse), so `/target rat` + `/cast 1` would pass
range/LOS validation. Ran the same 8-step script
(`benchmarks/audits/T46/t46_tour.txt`: spawn rat, target it, cast Frost
Bolt, wait, camp) against the `build-nogfx` (`EQT_GRAPHICS=OFF`) tree,
before and after the fix (before: `git stash` the two changed files,
rebuild, run; after: restore, rebuild, run — identical script, identical
seeded DB state both times, `--log-level=DEBUG --log-module=SPELL:DEBUG`
for full internal-state visibility).

**Observation method** — chose to observe `SpellGemChanged` landing in
`GameStateSyncBridge`'s decorator (the plan's literally-recommended
target), not `ConsoleRenderer`'s casting-message text. Tried the
`ConsoleRenderer` route first (it subscribes to `CastingStateChanged`,
which only fires from `SpellState::setCasting()`, itself only reachable
via the decorator's `SpellCastStarted` case) and found it blocked by an
**unrelated, pre-existing bug**, not caused by this unit (see "Found along
the way" below) — so switched to instrumenting the actual mechanism the
plan names: a temporary `LOG_INFO` in
`GameStateSyncBridge::applyEvent()`'s `SpellGemChanged` case, logging the
gem slot/spellId/state every time the decorator applies one. This is
genuine proof of decorator delivery (not inferred from `SpellManager`'s
own always-fires debug logs, the exact anti-pattern `deferred.md`'s T45
entry flagged) — the decorator's own `applyEvent()` is compiled once and
executed once per drained event, so a log inside its `SpellGemChanged` case
firing is direct evidence the event reached that code, not evidence of
`SpellManager`'s internal state alone. This instrumentation (and two
similar temporary diagnostics — one in `SpellManager::publishCastStarted()`
logging its `m_bridge` pointer, one in `wireBridgeToSubsystems()` logging
the manager pointers) was **added only to produce this evidence and
reverted before the final commit** — see Deviations; the evidence logs
below were captured while it was present.

| | Full-state-snapshot gem dump (zone-in, unaffected by this unit) | Cast-triggered `SpellGemChanged` reaching the decorator (this unit's target) |
|---|---|---|
| **fable** (`t46_diag_before_fable_nogfx.log`) | 8 lines, gem=0..7, all logged (lines 63-70) — expected, `EverQuest`'s own bridge already works headless (T41/T43) | **Zero lines.** `publishCastStarted m_bridge=0x0` (line 99) confirms `SpellManager::m_bridge` is null; `SpellManager`'s own log lines still show the full internal lifecycle succeeding (`Beginning cast...`, `Cast of 'Frost Bolt' completed successfully`, `Gem 1 on cooldown for 2250ms`, `Gem 1 cooldown complete`) but none of it reaches the decorator — every `publishGemChanged()` call after zone-in silently early-returns on the null `m_bridge` |
| **fixed** (`t46_diag_after_fixed_nogfx.log`) | Same 8-line dump (lines 76-83), unchanged | **Three lines**, well after the snapshot: `gem=0 ... gemState=2` (Casting, line 116, the instant `beginCastFromGem()` fires — before any server round-trip), `gemState=3` (Refresh/cooldown, line 131, after `SpellAction` completes), `gemState=1` (Ready, line 142, after cooldown expiry) — `publishCastStarted m_bridge=0x56fd...c60` (line 112) confirms the same pointer `wireBridgeToSubsystems()` wired (line 24) is the one `SpellManager` is now pushing through |

This is the plan's "manager-originated publish arriving in headless that
does not today," demonstrated live, via the decorator, with the
full-state-snapshot path excluded from the comparison by construction (the
cast happens ~9 seconds after zone-in, well past the one-time snapshot
batch, and the snapshot's own 8-line dump is identical in both runs — only
the cast-triggered lines differ).

Restored afterward: deleted the seeded `character_spells`/
`character_memmed_spells` rows for id 67; restored `Testcaster` to its
original `zone_id=25, x=-259, y=-1201, z=-5.79298, heading=0` (T45's
recorded original); confirmed via `SELECT` before finishing. `Testchar`
(id 65) was never touched by this unit — confirmed
`zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190` unchanged throughout
(it already matched the required restore position at the start of this
unit, left over from a prior session). `down.sh` run, lock removed.

### Teardown asymmetry — found more than the plan anticipated, still not fixed

Read `Application::shutdown()` (`application.cpp:642-702`) and
`EverQuest::ShutdownGraphics()` (`eq_bridge.cpp`) end to end, since the
plan asked to report rather than fix.

The plan's framing ("nothing ever clears `EverQuest::m_bridge`") is
**half right**: `ShutdownGraphics()` (only compiled under
`EQT_HAS_GRAPHICS`) *does* set `m_bridge = nullptr` — but only for
`EverQuest`'s own pointer, and only in a graphics-configured build. It also
fully destroys `m_inventory_manager` (`clearAll()` then `.reset()`), but
does **not** call `setBridge(nullptr)` (or any wiring helper) on
`m_spell_manager`/`m_skill_manager`/`m_hotbar_model` — those three survive
shutdown with their own bridge pointers left dangling at the
just-freed/about-to-be-freed bridge object, an inconsistency this unit's
fix does not introduce (the old `InitGraphics()`-only wiring had the exact
same shape: nothing ever unwired them either) but also doesn't fix,
per the plan's explicit "do not fix the lifecycle" instruction.

In a true headless build (`EQT_GRAPHICS=OFF`, no `EQT_HAS_GRAPHICS` at
all), `ShutdownGraphics()` doesn't exist as compiled code, and
`Application::shutdown()`'s own `m_bridge = nullptr` /
`m_stateSyncBridge.reset()` / `m_irrlichtBridge.reset()` block
(`application.cpp:674-676`) sits inside `#ifdef EQT_HAS_GRAPHICS` too — so
in that configure, **neither** `Application::m_bridge` nor
`EverQuest::m_bridge` nor any of the four managers' bridge pointers are
ever cleared on shutdown. All of them (now five pointers instead of one,
after this fix) dangle at the destroyed `GameStateSyncBridge`/
`ConsoleBridge` objects once `Application::shutdown()` returns and its
`unique_ptr` members go out of scope.

Whether this is a live hazard depends entirely on whether anything runs
`EverQuest`/manager code between `shutdown()` returning and process exit.
Grepped for `Application::shutdown()` callers and post-shutdown use: it is
called from `main()` at normal exit and from signal handlers: process exit
follows immediately in every path found, so the dangling window is real
but currently unexercised in production. **This is a live hazard in one
concrete case this repo already has**: any test or tool that constructs an
`Application`, calls `shutdown()`, and then keeps running in the same
process (e.g. a test harness that shuts down and reuses/inspects the
object, or a future re-init path) would now touch five dangling pointers
instead of one if it dereferenced them post-shutdown — this unit widens
the blast radius of a pre-existing bug by construction (exactly what the
plan warned against "quietly widening"), which is precisely why the plan
asked for `wireBridgeToSubsystems()` to propagate null cleanly: **the fix
in this unit does not itself create the hazard, but it does mean a future
`setBridge(nullptr)` call (once someone adds one) will correctly clear all
five pointers in one call, rather than needing four more one-off fixes
alongside it.**

**Recommendation, not a fix here**: this is worth its own small unit —
call `EverQuest::setBridge(nullptr)` from `Application::shutdown()`
(unconditionally, not gated by `EQT_HAS_GRAPHICS`, alongside or replacing
the `#ifdef`-gated block), which after this unit's fix would cleanly null
all five pointers (`EverQuest::m_bridge` plus the four managers') in one
call, including in true-headless builds where nothing clears any of them
today. Not done here per the plan's explicit "fixing the teardown
lifecycle starts looking necessary → STOP and report rather than
expanding" instruction — this finding is additive to, not a widening of,
what the plan anticipated.

### Found along the way (not fixed, out of territory): `PlayerSpawnIdSet` is `#ifdef EQT_HAS_GRAPHICS`-gated at its main publish site

While chasing why `ConsoleRenderer`'s `CastingStateChanged`-driven "Casting
Spell #54..." text never appeared even after this unit's fix (which is why
the before/after above uses `SpellGemChanged` instead, per the plan's own
suggestion), found: `eq_entities.cpp:396-405`'s `PlayerSpawnIdSet` publish
(the "found our spawn in ZoneSpawns" site, the one that fires earliest at
zone-in) is wrapped in `#ifdef EQT_HAS_GRAPHICS` — so in a true headless
build (`EQT_GRAPHICS=OFF`), that specific publish is compiled out
entirely, even though the same function unconditionally sets
`m_game_state.player().setSpawnId(m_my_spawn_id)` directly on
**`EverQuest`'s own internal `GameState` member** (a separate object from
`Application::m_gameState`, which is what `GameStateSyncBridge`/
`ConsoleRenderer` actually read). Live evidence
(`t46_diag_before_fable_nogfx.log:108`, `t46_diag_after_fixed_nogfx.log:117,123`):
`GameStateSyncBridge`'s `SpellCastStarted` case logged
`playerSpawnId=0` even after this unit's fix, so its `d.casterId ==
m_state.player().spawnId()` self-vs-other filter (`game_state_sync_bridge.cpp:67`)
never matches — `Application::m_gameState`'s player spawn id is never set
in `EQT_GRAPHICS=OFF` builds via this path, at all.

This is **not a T46 regression and not manager-bridge-shaped** — it's
`EverQuest`'s own publish (already established as working headless per the
plan's framing), gated by an `#ifdef` this unit's territory doesn't touch,
and it predates this unit entirely (T44 built the
`SpellCastStarted`/`SpellCastComplete` self-filter on the assumption this
already worked, per its Review: "published reliably at zone-in... well
before any cast can occur" — that claim was never actually exercised live
in `EQT_GRAPHICS=OFF` before this unit's diagnostic run). Net effect:
`CastingStateChanged`-based casting-state reconstruction (T44's stated
deliverable) is silently broken in every `EQT_GRAPHICS=OFF` build, though
`SpellGemChanged` (this unit's chosen proof, and the gem-occupancy/state
half of spell reconstruction) is unaffected since its decorator case does
not filter on caster id at all. Reported per the same "worth its own unit"
standard as the teardown finding above — not fixed here, both because it's
outside `setBridge()`/`InitGraphics()`/tests territory and because a STOP
on "manager publish that is wrong headless" doesn't technically apply
(this isn't a manager, and the underlying data — `EverQuest`'s own copy —
is correct; only the copy `Application`'s decorator/renderer path reads is
stale).

### STOP conditions — none triggered

- Wiring order: verified as pre-traced, no exception found (see above).
- Manager publish wrong headless (not merely absent): not found. Every
  manager publish this unit newly enables (`SpellManager`'s
  `SpellGemChanged`/`SpellCastStarted`/`SpellCastComplete`,
  `InventoryManager`'s intents, `SkillManager`'s, `HotbarModel`'s) is
  exactly the same event data the graphics path already sends — nothing
  manager-side assumes a renderer; the two bugs found (teardown asymmetry,
  `PlayerSpawnIdSet` gating) are both pre-existing and orthogonal to this
  unit's actual change.
- Graphics-mode behaviour change: none — `ctest` (RDP suite unwaived,
  golden replay ran and matched, `visual_gate` PASS with goldens untouched
  per `git status --porcelain`), same pass counts as the `fable` baseline
  in both trees.
- Teardown lifecycle: reported, not fixed (see above).

### Verification bar

1. Both configures build clean: `build` (GLES2-config,
   `-DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_BUILD_TESTS=ON`) and
   `build-nogfx` (`EQT_GRAPHICS=OFF`) — `cmake --build ... -j24`, no new
   warnings from the touched files.
2. `ctest --output-on-failure`: `build` 1853/1854 passed (sole exception
   `AudioManagerTest.PreloadCommonSounds`, ALSA, documented); `build-nogfx`
   1421/1422, same sole exception. Identical shape to the pre-unit
   baseline. RDP suite (graphics tree) 58/58, unwaived, no `Skipped`.
   `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` ran (not
   skipped) and passed in both trees. `FullStateSnapshot.*` (T43, all 3
   tests), `SpellManagerCastComplete.*`/`SpellManagerTiming.*`/
   `GameStateSyncBridgeTest.*` (T44), and the `GameTick`-headless tests
   (T45) all passed, re-run explicitly by name.
3. Golden replay confirmed to have run (not `Skipped`) in both trees, per
   above.
4. `visual_gate` PASS (`scripts/ci/visual_gate.sh`, all three zones:
   qeynos2/gfaydark/befallen), goldens untouched — `git status --porcelain`
   showed only `include/client/eq.h`/`src/client/eq_bridge.cpp` modified
   throughout, no golden PNGs touched.
5. Live headless acceptance: see "Before/after" above.
6. `bash scripts/build-arm-noble.sh` — clean cross-compile, `willeq`
   binary produced.
7. `check_file_sizes.sh --files` (absolute paths, both touched files): 0
   fail. `eq.h` 2026 lines (WARN, pre-existing at 2009 on `fable`, +17
   lines); `eq_bridge.cpp` 1910 lines (WARN, pre-existing at 1879 on
   `fable`, +31 lines) — both already over the 1500-line WARN threshold
   before this unit, growth is the helper function itself plus its
   explanatory comments, no FAIL introduced.

### Deviations

- **Temporary diagnostic instrumentation, added and reverted within this
  unit, not part of the final commit.** To produce genuine (not
  log-line-inferred) proof that `SpellGemChanged` reaches
  `GameStateSyncBridge::applyEvent()`, added three throwaway `LOG_INFO`
  calls: one in `wireBridgeToSubsystems()` (logging the bridge/manager
  pointers at wiring time), one in `SpellManager::publishCastStarted()`
  (logging `m_bridge` and the caster id), and one in
  `GameStateSyncBridge::applyEvent()`'s `SpellGemChanged` case (logging the
  applied gem slot/spellId/state). All three were used only to capture the
  before/after evidence above and were removed (`git checkout` on the two
  files that had no real-fix changes, manual removal of the one line added
  to `eq_bridge.cpp`) before the final build/test/commit — the committed
  diff contains only the real fix (`eq.h` + `eq_bridge.cpp`, no diagnostic
  lines). Flagged as a deviation since it's a form of temporary
  product-code modification outside the plan's named territory, even
  though fully reverted.
- **Live target/position workaround, not a code deviation.** `/target`
  with no argument was avoided (T44/T45 both hit "no target" edge cases
  with it); used `/target rat` (partial-match by name) instead. The
  GM-spawned rat (`#npctypespawn 351043`) turned out to spawn at a fixed
  zone coordinate rather than at the caster's location, so `Testcaster`
  was moved to meet it (determined by reading `checkRange` debug output,
  not by guessing) rather than by moving the rat.
- Two pre-existing bugs found and reported, not fixed (both out of T46's
  territory): the teardown asymmetry (see above, more precisely
  characterized than the plan's framing — `ShutdownGraphics()` does clear
  `EverQuest::m_bridge` in graphics builds, just not the four managers';
  true-headless builds clear none of the five), and `PlayerSpawnIdSet`'s
  `#ifdef EQT_HAS_GRAPHICS`-gated publish site in `eq_entities.cpp`
  (breaks `CastingStateChanged`-based casting-state reconstruction in
  `EQT_GRAPHICS=OFF` builds specifically — unrelated to manager bridge
  wiring, discovered only while choosing an observation method for this
  unit's live leg).
- Seeded and then deleted `character_spells`/`character_memmed_spells` rows
  for `Testcaster` (id 67), and moved/restored its position — same pattern
  T44/T45 used, fully reverted and verified via `SELECT` before finishing.
