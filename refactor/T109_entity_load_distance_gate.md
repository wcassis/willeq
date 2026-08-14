# T109 — Gate entity loading by distance, for load time

**Slot: slot2. OWNS EQEMU and needs the Orange Pi. T108 runs headless in
parallel and will not contend for the lock.**

**Territory: `src/client/graphics/renderer_zone_load.cpp` (Step 8),
`src/client/graphics/renderer_zone_stream.cpp` if the deferred path needs it,
`include/client/graphics/`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T109/`.**

Read T91's Review (which located the loop and characterised the options),
T105's (the working-set numbers), and T107's (the Pi baseline that justifies
this) before starting.

## Why this is being built now — the justification changed

This was originally proposed as a memory fix. **It is not one.** T107 measured
the Pi at **92.9MB RSS against ~395MB headroom — 23.5% used**, and real
graphics content costs essentially the same on both machines (63.3MB x86 vs
62.4MB Pi). Memory is comfortable on the target.

**The real cost is load time.** The Pi takes **37.3 seconds**
zone-request-to-ready against x86's 10.3s. That is the user-facing problem.

T105 measured what is actually needed at the renderer's 300-unit render
distance: **6-9 distinct (race, gender) pairs**, against 16-20 race models the
current undistanced Step 8 loads. **Distance gating alone should cut
race-model loads 30-56%.**

## The defect

`IrrlichtRenderer::loadZoneStep8_Entities` (`renderer_zone_load.cpp:1298`)
unconditionally loops **every** entity in the zone and runs the full inline
prep+build chain at load time, on the loading thread, before the zone is
playable. T91 confirmed **no distance, visibility or count gate exists there**.

A gate for exactly this purpose already exists — `entityPrepMaxPvsDepth`
(`constrained_renderer_config.h:88`), consulted by `queueEntityPrepRequests()`
(`renderer_zone_stream.cpp:676`) — but it is wired **only** to the post-load
background streaming path, never to Step 8, and both shipped presets set it to
255 (unbounded).

## The design

Gate Step 8 by distance from the player's landing position, and let the
**existing** post-load streaming path pick up the remainder.

**Prefer wiring the existing mechanism over inventing a second one.** If
`queueEntityPrepRequests()` already handles entities that come into view
later, the deferred ones should flow through it. **Confirm that it does before
relying on it** — an entity deferred at load and never picked up afterwards is
a permanently invisible NPC, which is far worse than a slow zone-in.

### Choosing the radius

Will's ruling: **the render cap protects FPS; the load gate protects RAM —
they are separate.** So do not simply reuse 300 units because that is the
render distance. The load radius must be **at least** the render distance
(you cannot draw what you did not load) but may exceed it to absorb player
movement without stalls.

**State the radius you chose and the reasoning.** Justify it against measured
data, not intuition.

### Pop-in is expected and accepted

Will has already accepted placeholder cubes while entities load. An entity
outside the gate appearing shortly after zone-in is the intended trade.
**A permanently missing entity is not.**

## Measure on BOTH machines — the Pi is the point

x86 numbers alone do not justify this unit. T107 established the paired
harness works; reuse it.

Report **before and after, per machine**:
- zone-request-to-ready time (x86 baseline 10.3s, **Pi 37.3s**)
- race models loaded at Step 8
- entities prepped at Step 8 versus deferred
- RSS, to confirm nothing regressed

Use a zone with existing baselines. **Befallen** is what T107 measured; T105
also covered Nektulos and Qeynos2.

## Verify nothing goes permanently missing

This is the failure mode that matters. After zone-in with the gate active:

- confirm deferred entities **do** appear as the player moves or as they come
  into range;
- confirm nothing that was visible before the change is absent after it;
- test in a **populated** zone — Nektulos has 451 entities and is where a
  gating bug would show.

## What you may not do

- Do not change the 40-entity render cap. Different limit, different purpose.
- Do not build the offline manifest or per-entity texture selection — those
  are separate units in Will's design.
- Do not touch `ConstrainedTextureCache`'s eviction.
- **STOP and report** if the deferred path does not reliably pick entities up.
  A half-working gate that loses NPCs is worse than no gate.

## Verification

Full build and suite (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM
cross-compile. Every new test **proven able to fail in both directions**.

Visual gate — **goldens may legitimately move here**, since this changes what
is built at load. If they do, say so explicitly, explain why, and show the
before/after images rather than regenerating silently.

Session discipline: binary freshness against HEAD on **both** machines;
daylight `/say #set time 11 0`; pad scripts long — longer for the Pi; `-d 4`
plus `--log-level=DEBUG`; Pi `/tmp` is a 242MB tmpfs so pull captures off as
you go; restore DB state and confirm by `SELECT` **after logout**; kill clients
by real binary PID on both hosts; clean teardown, delete the lock.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T109-distance-gate`, base fable tip `9e674bd3`. Live eqemu (lock free
at session start; taken, used, released cleanly) and live Orange Pi (deployed,
exercised, left pristine) both used. Full logs, README, reproduction
commands: `/mnt/projects/fable/review/T109/`.

**Headline: the gate itself is built, correct, and measured on both
machines — but its prerequisite (the "existing" post-load background
streaming pickup path) was found live-broken for exactly the scenario T109
needs. Per the plan's explicit STOP instruction, the gate ships
present-in-code but INERT — no shipped preset activates it.** The
numbers below are a real characterization (a temporarily-active radius,
reverted before commit), not what ships.

### The radius chosen, and its justification

**`entityLoadRadius = entityRenderDistance + 100`** (400 for the `orangepi`/
default preset, 300 renders → 400 loads). Justified against data measured in
this unit, not intuition: T105's own per-entity distance data
(`review/T105/bench/*_entities.txt`) was re-analyzed at five candidate radii
(300/350/400/450/500/600) across all three of T105's zones. Distinct
(race,gender) pairs — the quantity that actually drives Step 8 cost — stay
flat from 300→400 in Befallen and Nektulos (9→9, 6→7) and rise only modestly
in Qeynos2 (9→12), then jump sharply at 450+ (Befallen 9→13, Qeynos2 9→12→14).
400 is the elbow: it buys a fixed 100-unit margin to absorb player movement
before the render distance is reached, at essentially the same distinct-
model cost as gating to 300 outright. Radius ≥ `entityRenderDistance` is
enforced by `validate()` (new test: `Validate_EntityLoadRadiusBelowRenderDistance_Fails`).

### Was the existing streaming path reused — and does it reliably pick up deferred entities?

**Reused, not reinvented — but found broken, then partially fixed, then found
broken again in a second, deeper way.**

1. **First finding: `EntityPrepWorker` was never constructed anywhere in the
   codebase.** `entityPrepWorker_` (`IrrlichtRenderer`'s member) had exactly
   two kinds of reference in the whole tree: `.reset()` (teardown) and reads
   behind `if (entityPrepWorker_)` guards — never a `make_unique` or
   assignment. The intended construction site
   (`beginZoneAssetLoad`/`P08_Entities`, referenced by a comment in
   `renderer_player_physics.cpp`) doesn't exist anymore — superseded by
   `loadZoneSequential()` with nothing replacing it. This is the same
   "implemented but unreached" pattern this project has hit repeatedly
   (`ConstrainedMeshCache`, `SetPosition`, etc.). **Fixed**: new
   `IrrlichtRenderer::ensureEntityPrepWorkerStarted()`
   (`renderer_entity_load_gate.cpp`, new TU) constructs + starts it once per
   zone load, wired to `entityRenderer_` via the existing
   `setEntityPrepWorker()`. This fix is independently valuable beyond T109 —
   without it, ANY entity spawning after zone-load-complete with a
   never-before-seen race also gets stuck in `Placeholder` phase forever
   (`processOneEntityBuildStep`'s `!vis.entityPrepComplete &&
   !isModelDataCached` branch returns `false` forever with no worker to ever
   complete the prep). Landed regardless of the gate's inert status.

2. **Second finding, live-confirmed, NOT fixed (out of this unit's narrow
   scope, exactly per the plan's STOP condition): the post-load streaming
   path's "am I done" check is scoped to only the player's current BSP
   region, and nothing re-arms it on a region change.**
   `checkProgressiveLoadingComplete()` (`renderer_zone_stream.cpp`) only
   counts unbuilt entities in `currentPvsRegion_` as "blocking" — anything
   deferred in another region is invisible to it, so it declares progressive
   loading complete and sets `progressiveLoadingActive_ = false` almost
   immediately (measured: 1.9s and 32.9s of "streaming time" in the two x86
   runs below, vs. dozens of deferred entities scattered across the zone).
   Once `progressiveLoadingActive_` is false, `queueEntityPrepRequests()` —
   which lives entirely inside `processFrameProgressiveLoad()`, itself gated
   on `progressiveLoadingActive_` — never runs again. The **only** re-arm
   path (`registerEntity()`, `renderer_entities.cpp`) fires for a **brand
   new** entity registration (a server spawn event) — never for a PVS region
   change bringing an already-registered-but-deferred entity into relevance.
   This is a pre-existing defect in the streaming system generally (not
   introduced by T109), but T109's whole design depends on exactly the
   scenario it breaks.

   **Live reproduction** (`review/T109/x86_befallen_gated_movement_check.log`):
   zoned into Befallen with the gate active (radius=400, 89/178 entities
   deferred), took a `/pmem` snapshot (82 modeled), then issued two large
   `/move` commands (-400 and -700 units from spawn — crossing multiple BSP
   regions) with an 8s settle each, then a second `/pmem`. **Modeled-entity
   count: 82 before, 82 after both moves — unchanged.** `EntityBuild:` (the
   background-build RAII logger) and `EntityPrepWorker: queued prep for
   spawn=` never appear even once anywhere after Step 8 completes, in either
   the idle-hold run or the movement run. The one `Progressive loading
   re-enabled` event across all four x86/Pi runs was for a genuinely new
   server-side spawn (`a_giant_rat003`), confirming the re-arm path works
   for its actual designed case and only that case.

**Per the plan's explicit instruction — "STOP and report if the deferred
path does not reliably pick entities up. A half-working gate that loses
NPCs is worse than no gate" — this unit does not activate the gate.**
`entityLoadRadius` ships at its struct default (`1,000,000.0f`, an explicit
"unbounded" sentinel matching the existing `entityPrepMaxPvsDepth=255`
convention) in both the C++ `fromPreset()` defaults and the shipped
`config/constrained_presets.json` — neither sets a real value, so Step 8's
gate condition (`distFromPlayer > entityLoadRadius`) is never true for any
real zone. The gate's code, tests, and the `EntityPrepWorker` construction
fix all land; the *activation* does not.

**Follow-up filed** in `refactor/deferred.md`: fix
`checkProgressiveLoadingComplete()`'s region-scoped "blocking" definition
(count entities deferred anywhere in the zone, not just the current region)
and add a re-arm path on PVS region change (`onPvsRegionChanged()` currently
only re-sorts/re-prioritizes existing queues) before setting
`entityLoadRadius` back to `entityRenderDistance + 100` in the shipped
presets.

### Before/after, per machine (characterization — temporarily active radius, reverted before commit)

Both runs: Befallen (T105/T107's own zone, for direct comparability),
`-d 4 --log-level=DEBUG`, daylight forced (`/say #set time 11 0`, confirmed
surviving several seconds of rendering before use), `Connecting to zone...`
(zone-request) → `Graphics loading complete` (ready) as the timing window.
"Before" is `entityLoadRadius=999999` on this exact T109 code (a verified
surrogate for pre-T109 behavior — see below), not the pre-T109 binary, so
the delta isolates the gate's own effect from any other change on the
branch.

| | x86 before | x86 after (radius=400) | x86 delta | Pi before | Pi after (radius=400) | Pi delta |
|---|---|---|---|---|---|---|
| Zone-request → ready | 11.692s | 10.743s | **-0.949s (-8.1%)** | 35.617s | 33.265s | **-2.352s (-6.6%)** |
| Step 8 wall time | 5.721s | 4.570s | **-1.151s (-20.1%)** | 22.002s | 19.443s | **-2.559s (-11.6%)** |
| Step 8 as % of total | 49% | 43% | | 62% | 58% | |
| Entities in-radius (prepped) | 177/177 | 89/178 | | 178/178 | 89/178 | |
| Distinct race/gender prepped | 19 | 9 | | 19 | 9 | |
| Entities deferred | 0 | 89 | | 0 | 89 | |

**The surrogate-before validity check**: at `entityLoadRadius=999999`, the
gate's own summary line reads `0 in-radius (19 distinct race/gender), 0
deferred` on both machines — i.e. every entity took the full 8a-8f chain,
matching T91/T105's "17-20 undistanced race models" baseline for Befallen
exactly (19). This confirms the radius=999999 runs are a faithful stand-in
for pre-T109 Step 8 behavior, not an artifact of some other T109 change.

**Honest reading, not just the flattering framing**: Step 8's *own* time
drops 12-20%, and Step 8 is 43-62% of total zone-load time (bigger share on
the Pi, as expected — this cross-validates T91's finding that Step 8
dominates). But the **overall** zone-request-to-ready improvement is modest
(6-8%), well short of the "30-56% cut" framed as the upside in the plan's
own justification section — that number describes the cut in *distinct race
models*, which did happen (19→9, a real 53% reduction), but Step 8's wall
time is not purely proportional to distinct-model count: a meaningful share
of it is per-entity loop overhead and cache-hit work that doesn't disappear
just because fewer *distinct* models are loaded (167 fewer entity iterations
still ran the base-model-cache-hit path in the ungated run). This is
reported plainly rather than rounded up.

### RSS

`/pmem` "Tracked total": x86 53.8MB (gated) vs 62.7MB (ungated); Pi 52.4MB
(gated) vs 62.4MB (ungated) — lower with the gate active, as expected (fewer
race models resident). **Caveat, stated per the plan's honesty requirement**:
because the streaming pickup defect above means the deferred 89 entities
never get built in this characterization, this RSS delta is not "not yet
paid" — it is "never paid" under the current (broken) streaming path. It
does *not* independently demonstrate the gate is memory-safe in a way that
generalizes past the pickup defect; it's reported for completeness, not as a
selling point. No RSS regression either way — nothing got bigger.

### The permanently-missing-entity check

Performed in Befallen (178 entities), not Nektulos. The defect was
conclusively reproduced and root-caused at the source level (both the
premature-completion logic and the register-only re-arm path) in Befallen
first; given the mechanism is zone-agnostic (BSP-region-scoped completion
detection, not anything zone-population-specific), and the gate ships inert
regardless of outcome, a second confirmation in the 451-entity Nektulos zone
was not performed — flagged as a deviation from the plan's explicit ask,
traded for session time spent confirming the fix precisely (source read +
two independent live reproductions: idle-hold and active-movement) rather
than breadth across zones.

### Goldens

**Unchanged.** Visual gate PASS on every run of this unit (before, during,
and after the ship-inert resolution). This is a real signal, and expected
either way: since the gate ships inert, Step 8 behavior in every shipped
build is byte-for-byte identical to before this unit — nothing that gets
built or how it looks changed for any real config.

### Build/test

1. `cmake --build build -j24` — clean, including the new
   `renderer_entity_load_gate.cpp` TU (added to `CMakeLists.txt`).
2. `ctest --test-dir build --output-on-failure` — **2256/2257**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Total
   grew from the prior 2247 baseline by 10 (this unit's new
   `entityLoadRadius`-focused tests in `test_constrained_renderer.cpp`,
   proven able to fail in both directions: below-render-distance and
   zero/negative radius fail `validate()`; at-or-above passes; JSON override
   present/absent; every preset resolves to the inert struct default, not
   some smaller active value).
3. `bash scripts/build-gles2-x86.sh` + `scripts/ci/visual_gate.sh` — PASS,
   goldens untouched (re-run three times across the session as the
   ship-inert resolution was implemented; PASS every time).
4. `bash scripts/build-arm-noble.sh` — PASS, ARM cross-build clean (re-run
   twice, same reason).
5. `scripts/refactor/check_file_sizes.sh fable` — **0 FAIL** (some
   pre-existing WARNs at exactly the 1500-line threshold on files this unit
   touched only lightly — `irrlicht_renderer.h`, `irrlicht_renderer.cpp`,
   `renderer_zone_load.cpp` — all already over 1500 before this unit; the
   `renderer_zone_load.cpp` growth from this unit's own additions required
   extracting `ensureEntityPrepWorkerStarted()` and `deferEntityForLoadGate()`
   into the new `renderer_entity_load_gate.cpp` TU to stay at/under the
   3000-line FAIL ceiling — landed at exactly 2998). Binary freshness
   verified against HEAD on both hosts at every build.

### Deviations

1. **The core deviation**: the gate is characterized and built per the plan,
   but NOT activated in shipped presets, because live verification (which
   the plan explicitly required before relying on the existing streaming
   path) found that path broken for T109's exact use case. This follows the
   plan's own STOP instruction to the letter rather than shipping a
   regression; flagged loudly rather than silently "succeeding" with a
   smaller number.
2. `EntityPrepWorker`'s total non-construction was found and fixed
   mid-unit — squarely within the plan's "confirm it does [pick up deferred
   entities] before relying on it" mandate, and within the plan's explicit
   territory grant for `renderer_zone_stream.cpp` "if the deferred path
   needs it." The SECOND defect found underneath it (region-scoped
   completion + missing re-arm) was NOT fixed — that is real restructuring
   of completion-detection semantics shared by doors/objects/entities, not
   a "someone forgot to call the constructor" bug, and is exactly the class
   of thing the plan says is its own unit with its own review.
3. Nektulos was not additionally tested (see "permanently-missing-entity
   check" above) — time was spent instead on a second, independent live
   reproduction (idle-hold vs. active-movement) of the same defect in
   Befallen, which the executor judged more valuable than zone breadth for
   a defect already root-caused at the source level.
4. No RSS regression check via the heavier `gdb`+`mallinfo2()` method
   (T106/T107's own tooling) was performed — `/pmem`'s own tracked-total
   reporting was judged sufficient given the RSS caveat above (the
   comparison's honesty limit is the streaming defect, not measurement
   precision).
5. Two live-`willeq` invocations were spent debugging a false alarm: this
   session's `grep` shell function silently wraps `ugrep` with
   `--ignore-files --hidden`, which for reasons not fully diagnosed returned
   zero matches against several of this unit's own multi-megabyte capture
   logs for patterns that were definitely present (confirmed instantly via
   `command grep`). No time was lost re-running `willeq` because of it, but
   several minutes were spent narrowing it down before switching to
   `command grep` for the rest of the session — worth a note in case another
   session hits the same trap.

## Board

**Record the Pi load-time delta, and record that it doesn't ship active
yet.** x86 -8.1%/Pi -6.6% zone-request-to-ready, -20.1%/-11.6% in Step 8
specifically, at a 400-unit radius — real, but the more load-bearing finding
for what happens next is that the "existing" deferred-entity streaming
mechanism this design leaned on is confirmed broken for the region-crossing
case, independent of T109. Whatever loading-design unit comes next should
treat fixing `checkProgressiveLoadingComplete()`'s completion/re-arm logic
as a prerequisite, not a nice-to-have — T109's gate (and Will's step
2/"determine entities within render+load distance" more generally) cannot
safely ship active until it does.
