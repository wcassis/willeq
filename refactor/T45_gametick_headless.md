# T45 — Make `GameTick()` run headless (cooldowns tick, buffs expire)

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `Application::updateGameState()`'s gate,
`EverQuest::GameTick()`'s own gate, and tests.** Keep it that narrow —
the fix is two conditions; the work is proving it is safe.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry
**"`GameTick()` never runs headless — spell cooldowns don't tick, buffs
never expire"**, then `refactor/T44_spell_state_via_bridge.md`'s Review
(which found it).

## The defect

`GameTick()` is gated on graphics **twice**:

1. `Application::updateGameState()` — `if (m_eqClient && m_graphicsInitialized) GameTick(...)`
2. inside `GameTick()` — `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized) return; }`

Its entire body is three calls:

```cpp
m_spell_manager->update(deltaTime);   // cooldowns, memorization, cast timeouts
m_buff_manager->update(deltaTime);    // buff durations, expirations
ProcessBridgeIntents();
```

None of that is renderer work. So headless today: **cooldowns never tick,
memorization never progresses, cast timeouts never fire, buffs never
expire, and renderer intents are never drained.**

## The subtlety that decides the fix — read before changing anything

**`m_graphics_initialized` is not a proxy for "headless".** It is also
false *in graphics mode* during startup and zone loading, until graphics
init completes.

So the naive fix — delete both gates — does not merely enable headless;
it makes `GameTick()` newly run **during the graphics-mode loading
window**, which is a behaviour change in the mode that currently works.
The gate may well have been protecting exactly that window.

The fix is therefore **not** "remove the gate" but "make the condition
mean what it should": run the tick when the *game* is ready, regardless
of whether graphics are. Establish what the right readiness signal is —
`OnGameStateComplete()`/zone-in state-ready is the same lifecycle T41
used for the snapshot, which is a reasonable precedent — and say why you
chose it.

**Do not change graphics-mode behaviour.** If your condition would let
`GameTick()` run during graphics-mode loading where it previously did
not, that is a STOP unless you can show it is safe, in which case say
how.

## What turning it on actually enables — safety analysis already done

The orchestrator traced this; verify rather than re-derive:

- **`ProcessBridgeIntents()` is the only caller of
  `drainIntents()`** (`eq_bridge.cpp:729`). So intents pushed in headless
  today are **never drained — the queue grows unboundedly.**
- **Nearly every intent pusher is renderer-side** (`renderer_frame.cpp`,
  `renderer_overlays.cpp`, `renderer_player_physics.cpp`,
  `static_ui_input.cpp`) and therefore absent headless.
- **The exception is `inventory_manager.cpp`**, which pushes
  `MoveItemIntent`/`EquipmentChangedIntent` on item pickup — and
  `InventoryManager` *does* run headless since T41/T42 un-gated its
  construction and packet path.

So un-gating also closes a latent unbounded-growth hazard. Confirm the
queue actually behaves as described, and check whether anything headless
currently reaches those pickup paths.

Also confirm no double-processing: T26 deliberately wired
`UpdateLootState()` alongside `UpdateMovement()` *because* `GameTick()`
was graphics-gated. It is not in `GameTick()`, so this should be fine —
verify rather than assume, since T26's reasoning was explicitly built on
the gate you are removing.

## Scope

1. Fix both gates so the tick runs on game-readiness, not graphics.
2. Verify the three enabled calls are safe headless — each null-checks
   its manager already, but check `ProcessBridgeIntents()`'s body for
   anything that assumes a renderer.
3. Tests: cooldown ticking and buff expiry in a headless build. T44's
   countdown model computes from a start time on read, so a *cooldown*
   may appear to work without the tick — assert something that genuinely
   requires `update()` to have run (memorization progress, cast timeout,
   or buff expiry).

## STOP conditions

- Your condition changes graphics-mode behaviour (including during the
  loading window) → STOP unless you can demonstrate safety.
- `ProcessBridgeIntents()` turns out to touch renderer state → STOP and
  report; that would mean the gate was load-bearing.
- Enabling the tick causes duplicate processing anywhere (loot, movement,
  intents) → STOP.
- Any behaviour change beyond "these timers now run headless" → STOP.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35);
   the golden replay does not skip (T36); T43's `FullStateSnapshot` and
   T44's spell tests must stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. **Graphics mode provably unaffected** — `visual_gate` PASS with
   goldens untouched, as T43 used it. This matters more than usual: the
   risk here is specifically to the mode that already works.
5. **Live headless acceptance with a real before/after**: show a timer
   that does not advance on `fable` and does advance after the fix —
   a buff expiring or memorization completing is the clearest. This is
   behaviour that has never run; asserting it works is the deliverable.
   eqemu LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after. Testcaster
   (Wizard) exists and T44 used it for spell work. Evidence under
   `benchmarks/audits/T45/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

### Readiness condition chosen, and why

**`EverQuest::IsGraphicsReady()`** (`eq.h`, pre-existing: `m_loading_phase
== LoadingPhase::COMPLETE`), OR'd into both gates rather than substituted
for them:

- `Application::updateGameState()` (`application.cpp`):
  `if (m_eqClient && (m_graphicsInitialized || m_eqClient->IsGraphicsReady()))`
- `EverQuest::GameTick()` (`eq_bridge.cpp`, inside `#ifdef EQT_HAS_GRAPHICS`):
  `if (!m_graphics_initialized && !IsGraphicsReady()) return;`

Despite its graphics-sounding name, `IsGraphicsReady()` is set true from
`EverQuest::OnGameStateComplete()`'s no-graphics branch (`eq.cpp`, via
`SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!")`) — exactly the
game-lifecycle "state ready" signal T41 used for the full-state snapshot,
already reused by three `test_zone_connection_flow.cpp` tests as the
observable proxy for headless zone-in completion. No new state was added;
the existing accessor already meant the right thing, it just had no caller
outside tests until now.

### Why OR, not replace — graphics mode provably unaffected including the loading window

Traced `m_graphicsInitialized`'s (Application) full lifecycle rather than
assume: it is a **one-way latch**, set exactly once inside `Init()`
(`application.cpp`, inside the `if (config.graphicsEnabled && ...)` block,
itself inside `#ifdef EQT_HAS_GRAPHICS`) — **before `mainLoop()` /
`gameThreadLoop()` ever runs** — and reset only by `ShutdownGraphics()` at
teardown. If `InitGraphics()` fails, `Init()` returns `false` and the
process never reaches `mainLoop()` at all (`application.cpp`:602-606). So
for the entire lifetime of `gameThreadLoop()` in every successful
graphics-mode run, `m_graphicsInitialized` is **already `true` on every
single iteration**, including during the first zone-in and every re-zone
(re-zone does not reset it — `ShutdownGraphics()` is only ever called from
app teardown, one call site, `application.cpp:669`). Same structure for
`EverQuest::m_graphics_initialized` (`eq_bridge.cpp`'s `InitGraphics()`/
`ShutdownGraphics()`, called from the identical `Application::Init()` call
site).

Consequence: OR'ing in `IsGraphicsReady()` is a no-op for graphics mode by
construction — the left operand is already `true` for the entire window
GameTick() could possibly run in that mode, so the right operand's value
never changes the result. This is what let me use "OR" instead of "replace
the condition with game-readiness" (which the plan flagged as the risky
naive fix): the fix doesn't change *when* graphics mode ticks, only adds a
*second, independent* path that's live exclusively in the window graphics
mode's flag is permanently false (headless).

**Verified, not just reasoned:**
- `visual_gate.sh` PASS, goldens untouched (`git status` showed only the
  three source/test files touched) — pixel-identical to committed goldens
  across all three zones (qeynos2, gfaydark, befallen), which is a stronger
  check than "didn't crash": any changed graphics-mode timing that altered
  a single rendered frame would fail this.
- Live: ran the T45 acceptance script (below) against
  `build/bin/willeq --no-graphics` (the `EQT_HAS_GRAPHICS` tree, exercising
  **both** gates, unlike the `EQT_GRAPHICS=OFF` tree where only
  Application's gate is compiled in) and got the identical fixed behavior
  as the `build-nogfx` tree (`benchmarks/audits/T45/t45_live_run_graphics_tree_nographics.log`)
  — confirms the fix behaves identically across both configures, not just
  in the abstract.
- Did not attempt a live real-rendering (non-`--no-graphics`) eqemu run —
  `visual_gate.sh`'s pixel-exact comparison against committed goldens is
  strictly stronger evidence for "graphics-mode timing is unchanged" than
  an eyeballed live session would have been, and the code-level proof above
  already rules out the loading-window risk the plan specifically named.

### `ProcessBridgeIntents()` — no renderer dependency confirmed

Extracted its full body (`eq_bridge.cpp:726`-`1384`, 658 lines — the whole
intent-handling switch) and grepped it for `m_renderer`, `GetRenderer`,
`Irrlicht`, `GUIEnvironment`, `SceneManager`: **zero matches**. Every branch
either calls into game-state managers (`m_combat_manager`,
`m_inventory_manager`, `m_trade_manager`, `m_spell_manager`, ...) or
publishes bridge events for the (possibly absent) renderer to consume
later. Matches the file's own P28 banner comment two lines above `GameTick()`
("plain game-thread/bridge logic called unconditionally by Application
regardless of `EQT_HAS_GRAPHICS`"). STOP condition did not trigger.

### The intent-queue finding — real, but currently dormant for a different, deeper reason

Confirmed the plan's core claim: `drainIntents()` (`game_state_bridge.h:46`)
has exactly one caller, `ProcessBridgeIntents()` (`eq_bridge.cpp:729`) —
grep-confirmed, no other call site anywhere. So the general claim ("intents
pushed but never drained grow the queue unboundedly") is architecturally
correct and matters going forward.

**But traced further and found the "exception" — `inventory_manager.cpp`'s
pickup-path intents — does not currently reach the queue at all, headless,
for a reason one level deeper than gating:** `InventoryManager::bridge_`
(its *own* `GameStateBridge*` member, separate from `EverQuest::m_bridge`)
is set **only** inside `EverQuest::InitGraphics()` (`eq_bridge.cpp:122`,
`m_inventory_manager->setBridge(m_bridge)`) — a graphics-only call path
(`InitGraphics()` itself is `#ifdef EQT_HAS_GRAPHICS`, and only ever called
from `Application::Init()`'s graphics-enabled branch). `EverQuest::setBridge()`
(`eq.h:1037`, called from `Application::Init()`'s headless branch) sets only
`EverQuest::m_bridge` — it does not propagate to
`m_inventory_manager`/`m_spell_manager`/`m_skill_manager`/`m_hotbar_model`,
each of which holds its *own* separate bridge pointer, wired **only**
inside `InitGraphics()`. So today, in every headless run
(`EQT_GRAPHICS=OFF`, or `EQT_HAS_GRAPHICS` with `--no-graphics`),
`InventoryManager::bridge_` (and the other three managers' bridge pointers)
is `nullptr` for the whole process lifetime, and every `if (bridge_)
bridge_->pushIntent(...)` in `inventory_manager.cpp` is a silent no-op —
confirmed by grep (`setBridge` on these four managers has exactly one call
site each, all inside `InitGraphics()`).

Net effect: turning on `ProcessBridgeIntents()` headless is safe regardless
of this — there is currently nothing reaching `pushIntent()` from those
managers to drain in the first place — but the "closes a latent
unbounded-growth hazard" framing doesn't apply *yet* for that specific
pathway, because a **different, deeper, pre-existing gap** (this bridge-wiring
gap, not the GameTick gate) is what's actually suppressing it. The hazard
becomes real the moment someone fixes *that* gap without also confirming
this unit's fix is in place first — worth a `deferred.md` entry (added,
see below) so it isn't rediscovered the hard way. This is squarely outside
T45's territory (`Application::updateGameState()`'s gate,
`EverQuest::GameTick()`'s own gate, and tests) — fixing four separate
managers' bridge wiring is its own unit.

### No double-processing — confirmed

`CombatManager::UpdateLootState()` (T26) is called directly from
`Application::updateGameState()` (`application.cpp`, right after the
now-fixed `GameTick()` gate, unconditionally on `m_eqClient` alone — not
inside `GameTick()`, not behind either gate) — read the surrounding code to
confirm, not just trust the comment. T26's own comment at that call site
(quoted, unchanged by this unit) explicitly names why: `GameTick()` was
graphics-gated, so loot had to live elsewhere. That reasoning is now
partially stale (`GameTick()` runs headless too as of this unit) but the
call site itself is unaffected — `UpdateLootState()` was never moved into
`GameTick()`, so there is no new double-call. Grepped for any other
`GameTick()` call site (`eq_bridge.cpp:227`'s definition and
`application.cpp:1263`'s sole call) — confirmed singular, so nothing calls
`SpellManager::update()`/`BuffManager::update()`/`ProcessBridgeIntents()`
from two places.

### Unit tests: the "requires update() to have run" proof

Added to `tests/test_spell_manager_timing.cpp` (private-access seam already
established there, no DB dependency needed — deliberately avoided
`BuffManager`, see Deviations):

- `GameTickHeadless.BeforeGameStateReady_GameTickDoesNotResolveMemorization`
  (`EQT_HAS_GRAPHICS`-only, see below) — backdates a memorization past its
  duration, calls `eq.GameTick(7.0f)` on a fresh (`IsGraphicsReady()==false`)
  `EverQuest`, asserts the gem state and `isMemorizing()` are **unchanged** —
  pins that the gate still blocks pre-readiness.
- `GameTickHeadless.AfterGameStateReady_GameTickResolvesMemorizationViaSpellManagerUpdate` —
  identical setup, but `eq.SetLoadingPhase(LoadingPhase::COMPLETE)` first;
  asserts the memorization **completed** (`GemState::Ready`,
  `getMemorizedSpell(3)==22222`) — proves `GameTick()` actually drove
  `SpellManager::update()` → `updateMemorization()`. Memorization was
  chosen over a buff (the plan's other suggestion) specifically because
  `BuffManager` needs `EverQuest::InitializeSpellSystem()` (a real
  `data/willeq.db` + a global `WilleqDatabase` singleton wired via
  `OpenDatabase()`) to exist at all — a `EverQuest::GetBuffManager()` call
  on a bare `MakeEq()`-constructed instance segfaults (verified: hit and
  root-caused a null-pointer crash inside `BuffManager::checkStacking()`
  during scoping, backtrace confirmed via `gdb`). Memorization needs none
  of that (`SpellManager` is constructed unconditionally in `EverQuest`'s
  plain constructor), matching the existing
  `BackdatedMemorizationCompletesOnUpdateRegardlessOfDeltaTime` test's own
  precedent one test up, which this pair directly extends by driving the
  same resolution through `GameTick()` instead of calling
  `updateMemorization()` directly.

**A real, useful negative finding surfaced by running this in both trees**:
the "Before" test **fails in `build-nogfx`** (`EQT_GRAPHICS=OFF`) if not
guarded — because `EverQuest::GameTick()`'s own internal gate is *entirely*
`#ifdef EQT_HAS_GRAPHICS`-wrapped, in the original code and unchanged by
this unit (only the condition *inside* the `#ifdef` changed). In that
configure `GameTick()` called directly (bypassing `Application`) has never
respected any readiness gate — the sole real gate for that configure has
always been `Application::updateGameState()`'s call site, which a
same-thread unit test of `EverQuest::GameTick()` doesn't exercise by
design (per the plan's territory, `Application` isn't unit-tested). Fixed
by wrapping the "Before" test itself in `#ifdef EQT_HAS_GRAPHICS` (the
"After" test needs no such guard — it passes in both trees, since it
doesn't depend on the gate blocking anything). This is pre-existing
asymmetry between the two configures, not something this unit introduced
or needed to fix — flagged here rather than silently worked around.

### Live headless acceptance: real before/after

**eqemu LOCK FIRST**: created at unit start, deleted after clean `down.sh`
at the end — confirmed absent both before and after
(`ls /mnt/projects/fable/.eqemu-lock`).

Seeded `Testcaster` (id 67, Wizard) with Frost Bolt memorized in gem 1
(`INSERT INTO character_spells/character_memmed_spells (id, slot_id,
spell_id) VALUES (67, 0, 54)`, T44's exact recipe), moved to `arena`
(zoneidnumber 77, matching `scripts/eqemu/record_tour.sh`'s established
"quiet zone, 0 native spawns" choice — an early attempt using Nektulos
Forest produced a confusing, ultimately-unrelated false lead, see
Deviations) and ran a 12-step script
(`benchmarks/audits/T45/t45_tour.txt`: GM-spawn a rat, target it, cast
Frost Bolt, wait 3s, cast again (expect `AlreadyCasting` — still mid-cast),
wait 20s, cast a third time) against **both** trees, before and after the
fix (before: `git stash` the two source files, rebuild, run; after:
restore, rebuild, run — same script, same seeded DB state both times):

| | `beginCastFromGem` @ step 8 (cast+3s) | `beginCastFromGem` @ step 10 (cast+23s) |
|---|---|---|
| **fable** (`t45_prefix_control_run.log`) | `state=2` (Casting) → `AlreadyCasting` | `state=2` (Casting) → `AlreadyCasting` **— still stuck** |
| **fixed** (`t45_live_run_nogfx_tree.log`) | `state=2` (Casting) → `AlreadyCasting` | `state=1` (Ready) → **new cast sent** |

This is the "waiting for server confirm" 5-second timeout inside
`SpellManager::update()` (T44's finding 3: the cast here never gets a
`BeginCast` echo back because `/target` with no prior selection resolves
to `target_id=0`, the same "no target" edge case T44's live leg hit) —
on `fable` it can never fire because `GameTick()` never runs headless, so
the gem is stuck in `Casting` forever (confirmed: still stuck 23 seconds
after the cast, an order of magnitude past its 5s timeout). After the fix,
by 23 seconds the timeout has resolved it back to `Ready` and a fresh cast
succeeds. Reproduced identically against the `EQT_HAS_GRAPHICS` tree run
with `--no-graphics` (`t45_live_run_graphics_tree_nographics.log`), exercising
both gates end-to-end, not just the `EQT_GRAPHICS=OFF` tree's single gate.

This is a genuine "timer that does not advance on `fable` and does advance
after the fix," per the plan's ask — the cast-confirm timeout rather than
a plain gem-cooldown-expiry or buff-expiry (both also gated by the same
fix and covered by the unit tests above), because the live target-selection
edge case made the cast-confirm timeout the one that actually fired in
this session; a cleaner target would very likely have shown the plain
`Refresh`→`Ready` cooldown transition instead, but this is at least as
strong a proof (it requires the exact same `SpellManager::update()` call
inside `GameTick()`).

Restored afterward: deleted the two seeded `character_spells`/
`character_memmed_spells` rows for id 67; restored `Testcaster` to its
original `zone_id=25, x=-259, y=-1201, z=-5.79298, heading=0`. `Testchar`
(id 65) was never touched — confirmed `zone_id=36, x=-77.9232, y=21.9086,
z=3, heading=190` unchanged before and after
(`SELECT` output captured in the session). `down.sh` run, lock removed.

### Deviations

1. **Zone choice false start (process note, not a code deviation).** First
   live attempt used Nektulos Forest (`Testcaster`'s zone at the time,
   inherited from T44's session) and appeared to hang for a fixed ~67s
   with no script progress — initially suspected as a bug caused by this
   unit's fix (network-packet starvation from Nektulos's ~20 roaming
   spawns, or a deadlock). Root-caused via a same-script control run
   against unmodified `fable` (`git stash`): identical ~67s behavior with
   *zero* of this unit's changes applied, proving it was pre-existing and
   unrelated. Further investigation found the actual cause was a **flaky
   local `grep` alias** (banner warnings identified it as `ugrep`-backed)
   silently returning false negatives against these particular log files —
   `/usr/bin/grep` against the identical files showed the script had been
   progressing normally the whole time. Switched to `/usr/bin/grep`
   explicitly for all subsequent log analysis in this unit. No code change
   resulted; recorded here because it consumed real investigation time and
   the next executor hitting unexplained "grep finds nothing" should
   suspect the shell alias before the binary.
2. **Memorization, not a buff, for the unit-test proof** (see "Unit tests"
   above) — the plan named buff-expiry as one acceptable option
   ("memorization progress, cast timeout, or buff expiry"); buff-expiry
   was tried first and hits a real, separate construction dependency
   (`BuffManager` needs `InitializeSpellSystem()`) not worth pulling into
   this unit's territory.
3. **Live leg observed the cast-confirm timeout, not a plain gem-cooldown
   or buff-expiry**, per the target-selection edge case explained above —
   still a direct proof of the same fix, explicitly flagged as a deviation
   from the plan's literal "clearest" suggestion rather than silently
   substituted.

Everything else matches the plan as written: territory held to the two
gate conditions plus tests (no changes to `ProcessBridgeIntents()`,
`SpellManager`, `BuffManager`, or the four managers' bridge-wiring gap
found along the way — that gap is now a `deferred.md` entry, not a T45
fix).
