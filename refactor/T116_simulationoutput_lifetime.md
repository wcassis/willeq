# T116 — Make the `SimulationOutput` dangling-pointer class impossible by construction

**Slot: whichever is free. x86 only — no Pi, no eqemu lock needed** (see
Verification; if you think you need either, STOP and say why).

**Territory: `renderer_sim_bridge.cpp`, `renderer_zone_load.cpp`,
`renderer_zone_lifecycle.cpp`, `irrlicht_renderer.h`, `tests/`, `refactor/`.**

Read T115's Review first (the ASAN transcript and root cause), then T111's
(the first two instances).

## Why this unit exists: three instances, and the third was added under a comment warning about the first two

`IrrlichtRenderer` holds raw pointers into a `SimulationOutput`. The same
mistake has now been made **three times in the same struct**:

- **T111** fixed `particleRenderBuffer_` and `regionPvsDepthMap_`.
- **T115** fixed `portalVisibleRegions_` — added later, for portal occlusion,
  and it missed the same cleanup **directly beneath T111's comment explaining
  the hazard.**

That is the whole argument for this unit. A comment warning did not prevent
the next instance, because the hazard is structural: **one function,
`applySimulationOutput()`, serves two callers with opposite lifetime
guarantees**, and nothing in the code distinguishes them.

- `applySimulationResults()` (`renderer_sim_bridge.cpp:579`) passes
  `simulationWorker_->swapAndGetResults()` — a pointer into the worker's
  persistent double-buffered `output_[2]`. **Storing pointers into it is
  correct.**
- `loadZoneSequential()` Step12b (`renderer_zone_load.cpp:2804`) passes a
  **local** `SimulationOutput output;` on the loading thread. **Storing
  pointers into it is a use-after-scope**, and is what T111 and T115 each
  patched up after the fact by nulling the members again on the next line.

**Do not fix this by adding a fourth nulling line.** The goal is that a
future member added to `SimulationOutput` cannot repeat this, without anyone
having to remember.

## Ground truth — verified while scoping, take it as your starting map

Pointers stored into `SimulationOutput`, all assigned in exactly one function
(`applySimulationOutput`, `renderer_sim_bridge.cpp`):

| Member | Set at | Read at |
|---|---|---|
| `regionPvsDepthMap_` | `:611` | `renderer_zone_stream.cpp:605-607` |
| `portalVisibleRegions_` | `:640` | `renderer_zone_draw.cpp:385, :457` |
| `particleRenderBuffer_` | `:887` | `renderer_frame.cpp:1317` |

Nulling sites today: `renderer_zone_load.cpp:2812-2813` (all three, the
T111+T115 patch-ups) and `renderer_zone_lifecycle.cpp:178,180` (two of three
— **`particleRenderBuffer_` is missing there**; establish whether that is a
live hazard or merely an inconsistency, and say which).

### There is a fourth member of this class — `currentFrameSimResults_`

`currentFrameSimResults_` (`irrlicht_renderer.h:1341`) is set at
`renderer_sim_bridge.cpp:581` and read at `renderer_zone_draw.cpp:391-392`
and `:542-543` — **six lines from where T115's crash landed.**

I checked it while scoping, and it is **currently safe, by accident, and its
comment is wrong**:

- Safe because it is only ever set from the runtime path, and
  `simulationWorker_` is constructed once (`irrlicht_renderer.cpp:806`) and
  never reset, so `output_[2]`'s storage outlives every zone.
- Its comment claims "Valid during processFrame, null otherwise" — **it is
  never assigned `nullptr` anywhere.** It survives zone transitions pointing
  at recycled storage. `clearZoneData()` does `output_[i] =
  SimulationOutput()`, so post-transition reads see *empty* containers rather
  than freed memory, and both read sites happen to guard on `!empty()`.

So it is not a bug today, but it is the same shape held together by three
coincidences, and it would become a real dangle the moment anyone makes the
worker per-zone. **Bring it into whatever invariant you build, and fix the
lying comment.**

## The shape to build

**Split `applySimulationOutput()` by lifetime contract**, so that the code
that stores pointers is only reachable from the caller that has a persistent
buffer:

- `applySimulationOutput(const SimulationOutput&)` keeps all the **value**
  application — region/object/light visibility, `currentPvsRegion_`, the
  sorted draw list, portal-occlusion toggling, everything the load path
  actually needs — and **stores no pointers at all.**
- A separate, explicitly named step — e.g.
  `bindFrameSimulationBuffers(const SimulationOutput* persistent)` — stores
  the four pointer members, and is called **only** from
  `applySimulationResults()`, with the worker's stable pointer.

The load path then simply does not call the bind step. **The T111 and T115
nulling lines at `renderer_zone_load.cpp:2812-2813` get deleted, not
extended** — nothing was ever set, so nothing needs unsetting. That deletion
is the signal the fix is structural rather than another patch; if you find
yourself keeping them, stop and reconsider the split.

Naming matters here: the bind function's name and a comment on it should make
it obvious that its argument must outlive the frame, so the next person
adding a portal-occlusion-like feature adds their member to the obviously-
lifetime-critical function.

### Considered and rejected — don't spend the unit rediscovering these

- **Copy instead of point.** Copying an `unordered_set` and `unordered_map`
  every frame is precisely the cost the pointers exist to avoid, and the Pi
  is the platform that can least afford it. Rejected on performance.
- **Just add a fourth nulling line.** That is what T111 and T115 each did.
  It is what produced this unit.

### Optional stretch, only if the split lands cleanly

A stronger guarantee: make the members assignable only through a handle type
obtainable from `SimulationWorker`, so passing a stack local is a **compile
error** rather than a convention. Attempt this only with the split done and
green; **if it starts spreading through headers, stop and report it as a
follow-up.** The split alone is the required deliverable.

## STOP conditions

**STOP and report** rather than pressing on if:

- The split changes **which values get applied at zone load.** The load path
  depends on `applySimulationOutput()`'s value side effects (notably
  `currentPvsRegion_`, region node visibility, and the door PVS update that
  immediately follows it). Those must be byte-for-byte preserved.
- You find the runtime path depends on the pointers being set *before*
  something that currently runs between `:581` and the end of
  `applySimulationOutput()` — i.e. the bind cannot simply follow the apply.
- Fixing `currentFrameSimResults_` turns out to require changing the worker's
  lifetime or the swap protocol. That is a separate unit.

## What to establish

- That the load path leaves all four members untouched — **demonstrated by a
  test, not by reading.** This class has now escaped code review twice.
- That the runtime path still sets all four, in the right order, every frame.
- Whether `particleRenderBuffer_`'s absence from the
  `renderer_zone_lifecycle.cpp` nulling block is a live hazard or an
  inconsistency.
- The honest answer to "could a fifth member repeat this?" — if the answer is
  still yes, say so plainly and say what would close it.

## Verification

- x86 suite (current total **2266**; sole pre-existing failure the documented
  ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **New tests are required, and they are the point of the unit.** At minimum:
  the load path leaves the pointer members null, and the runtime path binds
  them. Note this codebase's dominant defect class is
  implemented-but-unreached — **assert on the real call paths, not on a
  hand-constructed fake that proves only that your test compiles.**
- **Visual gate, goldens must not move.** These members drive portal culling
  and the sorted draw list; a golden change here is a real signal, not noise.
  If a golden moves, STOP — do not rebless.
- **x86 ASAN re-run of T115's Befallen tour** (`review/T115/run_asan_x86.sh`
  is checked in and works). T115's pre-fix rate was 2 of 3; post-fix 3 of 3
  clean. **Reproduce that clean result** to show the refactor didn't reopen
  the window. This is cheap and directly on point.
- **No Pi run is required.** T115 showed the ASAN build OOM-kills there, and
  this unit is a refactor of an already-fixed bug — the x86 ASAN tour is the
  relevant evidence. Do not spend a Pi session on it.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T116-simoutput-lifetime`, base fable tip `eb8f949e`.

### The split as landed

`applySimulationOutput(const SimulationOutput&)` (`renderer_sim_bridge.cpp`)
is now **value-only** — every pointer-store line that used to live in it
(`regionPvsDepthMap_ = &results->regionPvsDepth;`,
`portalVisibleRegions_ = &results->portalVisibleRegions;`,
`particleRenderBuffer_ = &results->particleOutput.renderBuffer;` / its
`else` null branch, and `currentFrameSimResults_ = results;`, which
previously lived in `applySimulationResults()` immediately before the call)
was removed. A new function, `bindFrameSimulationBuffers(const
SimulationOutput* persistent)`, holds all four stores, reproducing each
one's original gating condition exactly (the PVS pair still requires
`usePvsCulling_ && !regionVisible.empty()`; the particle buffer still
requires `particleOutput.valid && particleManager_`, nulling otherwise).
`applySimulationResults()` (the runtime path,
`simulationWorker_->swapAndGetResults()`) is the **only** caller of the
bind step:

```cpp
void IrrlichtRenderer::applySimulationResults() {
    if (!simulationWorker_ || !simulationWorker_->isRunning()) return;
    const SimulationOutput* results = simulationWorker_->swapAndGetResults();
    if (!results) return;
    bindFrameSimulationBuffers(results);
    applySimulationOutput(*results);
}
```

**Correction (post-review, before merge): the bind runs FIRST, then value
application** — this is the opposite of this Review's first draft, which
had them the other way around on the claim that "nothing in
`applySimulationOutput()` reads the four pointer members it used to set."
That claim was checked only by reading `applySimulationOutput()`'s own
function body, and it was **wrong via a transitive call**: inside the
`currentPvsRegion_ != prevPvsRegion` block, `applySimulationOutput()` calls
`onPvsRegionChanged()` (`renderer_zone_stream.cpp:644`), which calls
`getRegionPvsDepth()` (via `entityPrepWorker_->updateDepths()` and
`gpuUploadThread_->reprioritize()`), which reads `regionPvsDepthMap_`
(`renderer_zone_stream.cpp:603-608`). That block runs exactly when the
camera crosses a PVS-neighborhood boundary — the precise moment the depth
map is supposed to change — so binding *after* `applySimulationOutput()`
would have fed that frame's re-prioritization the **previous** frame's
depths, silently, with no test able to catch it (not the visual gate,
which only checks static frames, and not this unit's own new tests, which
call the two split functions independently rather than through
`applySimulationResults()`). Caught by review, not by this session's own
verification — flagged here rather than absorbed quietly. See
`renderer_sim_bridge.cpp`'s in-code comment on `applySimulationResults()`
for the full trace.

**Re-verified by tracing transitive callees, not just the function body**:
every function `applySimulationOutput()` calls —
`rebuildPvsNeighborhood()`, `onPvsRegionChanged()` (both call sites of
`getRegionPvsDepth()` inside it), and its `entityRenderer_`/
`particleManager_`/`spellVisualFX_`/`detailManager_`/`weatherSystem_`/
`skyRenderer_` callees (grepped for the four member names across
`renderer_entities.cpp`, `environment/particle_manager.cpp`,
`spell_visual_fx.cpp`, `detail/detail_manager.cpp` — zero hits) — was
checked for reads of any of the four pointer members. Only the
`getRegionPvsDepth()` path reads any of them (`regionPvsDepthMap_`
specifically; the other three are never read by any callee). Binding
first is safe: `bindFrameSimulationBuffers()`'s own gate condition
(`usePvsCulling_ && !regionVisible.empty()`) reads the same `results`/
`usePvsCulling_` state regardless of which call goes first, since nothing
between the two calls mutates either.

This restores the **pre-split ordering exactly** for all four members —
`currentFrameSimResults_` was originally set at old `renderer_sim_bridge.cpp:581`,
*before* the `applySimulationOutput(*results)` call; `regionPvsDepthMap_`/
`portalVisibleRegions_`/`particleRenderBuffer_` were set inline at their
old assignment points, all of which ran before `applySimulationOutput()`
returned. `loadZoneSequential()` Step12b (`renderer_zone_load.cpp`) still
calls only `applySimulationOutput(output)`, never the bind step — `output`
is the local `SimulationOutput` on the loading thread, exactly the object
T111 and T115 each found dangling pointers into. This part of the original
claim stands: the load path is unaffected by the ordering fix, since it
never calls the bind step at all.

Header (`irrlicht_renderer.h`) documents the contract at both ends: on the
two function declarations (`applySimulationOutput`'s says "safe with ANY
lifetime, including a stack local"; `bindFrameSimulationBuffers`'s says
"`persistent` MUST outlive every frame... reopens the exact dangling-pointer
hazard T111 and T115 each patched... This is why the load path does not
call it") and on each of the four member declarations ("set ONLY by
bindFrameSimulationBuffers()").

### The T111/T115 nulling lines: deleted, not extended

`renderer_zone_load.cpp:2812-2813` (pre-split) —
`particleRenderBuffer_ = nullptr; regionPvsDepthMap_ = nullptr;
portalVisibleRegions_ = nullptr;` plus the "CRITICAL" comment explaining
why — is **gone**, replaced with a two-line comment noting they used to be
here and why they aren't needed anymore. This is safe because there is
nothing left to unset: `applySimulationOutput()` cannot write any of the
three members under the new split, so the loading thread's `output` local
never has anything pointing into it in the first place. Confirmed by the
new test suite (below), which calls the real `applySimulationOutput()` with
a local `SimulationOutput`, under conditions built specifically to force
every formerly-pointer-storing branch, and asserts all four members are
byte-for-byte unmoved both immediately after the call and after the local's
scope ends.

This is the test the plan named as the acceptance bar for "structural, not
another patch" — the deletion is real, not merely visually absent while the
code still relies on some other guard.

### `currentFrameSimResults_`

Set only by `bindFrameSimulationBuffers()` now (previously set inline in
`applySimulationResults()` just before calling `applySimulationOutput()`).
Its comment — `"Valid during processFrame, null otherwise"` — was false
before this unit (nothing ever nulled it) and is now corrected in place to
say so explicitly: it's set only from the runtime path, safe today only
because `simulationWorker_` is constructed once and never reset,
`clearZoneData()` replaces `output_[i]` with a fresh empty
`SimulationOutput` rather than freeing it, and both read sites
(`renderer_zone_draw.cpp:391-392`, `:542-543`) guard on `!empty()`. The
split doesn't remove those three coincidences (fixing them is explicitly a
separate unit per the plan's STOP condition on the worker's lifetime/swap
protocol), but it does fold `currentFrameSimResults_` into the same
single-writer invariant as its three siblings, and — per the plan's ask to
"bring it into whatever invariant you build" — it is now also nulled in
`renderer_zone_lifecycle.cpp`'s zone-teardown block (next section).

### `particleRenderBuffer_`/lifecycle-nulling: inconsistency, not (post-split) a hazard

Before this unit, `renderer_zone_lifecycle.cpp`'s zone-teardown block nulled
`portalVisibleRegions_` and `regionPvsDepthMap_` but not
`particleRenderBuffer_` or `currentFrameSimResults_` — the plan flagged this
asymmetry while scoping and asked which it was.

**Answer: an inconsistency, not a dangling-pointer hazard, and specifically
not the class this unit exists to close.** Post-split, all four members can
only ever be written by `bindFrameSimulationBuffers()`, and its own
contract requires the passed pointer to be `SimulationWorker`'s persistent
`output_[2]` storage — which is never freed for the process's lifetime
(`simulationWorker_` is constructed once, `stopSimulationWorker()` does not
destroy it, `clearZoneData()` replaces contents in place). So leaving
`particleRenderBuffer_`/`currentFrameSimResults_` unset at zone teardown
cannot produce a use-after-free the way the pre-split load path could — the
storage they'd point at is still valid. The real (minor) cost was
**staleness**: a frame rendered in the unload→load gap could read the
*previous* zone's last particle buffer or occlusion-region data. Both are
now nulled in the teardown block alongside their two siblings, for
consistency and to close that staleness window — documented in-place as
"not because leaving them would be unsafe."

### New tests: `tests/test_simulation_output_lifetime.cpp`, 6 cases

All six call the **real, split production functions** —
`IrrlichtRenderer::applySimulationOutput()` and the new
`bindFrameSimulationBuffers()` — via hand-expanded `friend class
SimulationOutputLifetimeTest_<Name>_Test` declarations (gtest's
`FRIEND_TEST` macro's own expansion, written out directly rather than
`#include <gtest/gtest_prod.h>` — see the ARM cross-compile deviation
below for why). No fake/reimplementation of either function exists
anywhere in the test file. Same "default-constructed, never-`Init()`'d
`IrrlichtRenderer`, no Irrlicht device needed" idiom `test_irrlicht_camera_command.cpp`
(T66) and `test_progressive_load_rearm.cpp` (T111) already established in
this codebase.

| Test | Exercises | What it proves |
|---|---|---|
| `ApplyOutputAloneLeavesAllFourPointersUntouched` | `applySimulationOutput()` alone, called exactly as Step12b calls it, with `usePvsCulling_`/`regionVisible`/`particleOutput.valid` all set to force every formerly-pointer-storing branch | The load path's real call leaves all four pointers unmoved, both immediately after the call and after the local's scope ends — the direct regression test for T111's and T115's bug class |
| `ApplyOutputAloneLeavesAllFourPointersUntouched_PvsCullingOff` | Same, with the gates OFF | No second, differently-gated code path in `applySimulationOutput()` still stores a pointer |
| `BindBindsAllFourFromThePassedPersistentPointer` | `bindFrameSimulationBuffers()`, with a real (headlessly-constructed) `ParticleManager` so the particle branch's true case is exercised for real | The runtime path's real bind step performs all four stores it's supposed to |
| `BindOnlyBindsPvsMembersWhenPvsCullingConditionsHold` | `bindFrameSimulationBuffers()` with the PVS gate false both ways (empty `regionVisible`; `usePvsCulling_` false) | Bind mirrors `applySimulationOutput()`'s own value gate exactly — never binds under conditions the value side wouldn't have populated under |
| `BindOnlyBindsParticleBufferWhenParticleOutputValid` | `bindFrameSimulationBuffers()` across all four combinations of `particleOutput.valid` × `particleManager_` presence | Bind requires BOTH conditions, matching the original `results->particleOutput.valid && particleManager_` gate, including the null-out the instant either goes false |
| `RepeatedApplyOutputWithFreshLocalsNeverDangles` | Two successive local `SimulationOutput`s through `applySimulationOutput()`, each going out of scope in turn (mirrors Step12b's actual per-zone-load pattern) | No aliasing across successive stack locals reusing the same slot |

Full suite result: `[ PASSED ] 6 tests` (`build/bin/test_simulation_output_lifetime`).

### x86 suite, ARM cross-compile, visual gate

- **x86 suite**: `2272` total (`2266` baseline + 6 new), `1` failure —
  `AudioManagerTest.PreloadCommonSounds`, the documented pre-existing ALSA
  issue, matching the plan's stated sole acceptable exception. No other
  failures.
- **File sizes**: `renderer_zone_load.cpp` moved from exactly 3000 to
  **2996** lines — the plan asked to verify the change removes lines from
  it, and it does (comment condensed to stay under the cap, same discipline
  T115 used for its own nulling-comment addition). `irrlicht_renderer.h` and
  `renderer_zone_load.cpp` both show pre-existing `WARN` (over the 1500-line
  informational threshold, not the 3000 `FAIL` cap) — no new `FAIL`s.
- **ARM cross-compile**: `PASS` — `build-arm-noble/bin/willeq` built with 0
  compiler errors (after the FRIEND_TEST fix below).
- **Visual gate**: `PASS`, goldens unchanged, all three zones including
  **befallen** (the exact zone T111/T115 fixed the crash in).

### x86 ASAN re-run of T115's Befallen tour: 3 of 3 clean, matching T115's post-fix result

Executed after the ordering fix above, so the evidence covers the code that
merges. Live eqemu (lock free at session start — confirmed via `ls
/mnt/projects/fable/.eqemu-lock`, no server listening on 5998 — taken,
used, released cleanly: `down.sh` run, lock file deleted, confirmed gone,
no leftover `willeq`/`akk-stack-*` containers). ASAN tree built at
`build-asan/` (`-fsanitize=address -g -O1 -fno-omit-frame-pointer`,
`-DEQT_BUILD_TESTS=OFF`, same GLES2/Irrlicht config as the normal tree);
confirmed real (41 `__asan_*` symbols via `nm`). Ran
`review/T116/run_asan_x86.sh` (adapted from `review/T115/run_asan_x86.sh`
for slot2's paths, `--no-audio` carried over to route around T115's
already-known, out-of-territory `tsf.h` audio overflow) three times against
the full 15-step Befallen tour (`review/T116/tour_x86.txt`, identical to
T115's): zone-in, `#set time 11 0`, `/pmem`, three `/move` waypoints
(`-400`/`-700`/`-400`) crossing the PVS-neighborhood hazard box each time,
`/pmem` again, `/camp`.

| Run | Outcome |
|---|---|
| 1 | Full tour to completion, `Application shutdown complete` reached, zero ASAN report, no `asan_report*` file written |
| 2 | Same — full tour, clean shutdown, zero ASAN report |
| 3 | Same — full tour, clean shutdown, zero ASAN report |

**3 of 3 clean**, matching T115's own post-fix 3-of-3 result exactly (T115's
pre-fix rate was 2 of 3 crashing). No ASAN report of any kind — no
`portalVisibleRegions_`-class SEGV, no other memory-safety finding — across
any of the three runs. Logs: `review/T116/x86_asan_run1.log` /
`run2.log` / `run3.log`. This demonstrates the ordering-fixed refactor did
not reopen the T111/T115 crash window, on the actual client binary through
a live zone connection — not merely via this unit's own unit tests.

### Fifth-member risk: reduced, not eliminated

The split makes it obvious *where* a new pointer-into-`SimulationOutput`
member belongs (`bindFrameSimulationBuffers()`, with a comment saying so in
both the function itself and on every existing member declaration) and
*where it must never go* (`applySimulationOutput()`, which the load path
calls with a non-persistent local). A future contributor reading either
function's header comment, or any of the four member declarations, would
have to actively ignore an explicit warning to repeat this bug — a real
improvement over "no comment distinguished the two callers at all," which
is what let the third instance land directly beneath the first two's
warning. But nothing makes it a **compile error**: a fifth member could
still be added to `applySimulationOutput()` by a contributor who doesn't
read the comments, the same way T115's contributor didn't. The plan's
optional stretch (a compile-time-enforced handle type obtainable only from
`SimulationWorker`) was **not attempted** — descoped to keep this unit to
the required split, per the plan's own "the split alone is the required
deliverable" framing. Flagged here as the honest answer to "could a fifth
member still repeat this": **yes, but the friction and the visibility of
the mistake are both substantially higher than before.**

### Deviations

1. **`#include <gtest/gtest_prod.h>` broke the ARM cross-compile.** First
   attempt included `gtest_prod.h` (a tiny, dependency-free macro header,
   documented by gtest itself as "useful in production code") in
   `irrlicht_renderer.h` to use `FRIEND_TEST(...)`. The x86 build compiled
   fine (gtest is fetched for the `EQT_BUILD_TESTS` tree), but
   `build-arm-noble.sh`'s Docker cross-compile toolchain has **no gtest
   headers at all** — gtest is only ever fetched for the x86 test tree — so
   this was a hard `fatal error: gtest/gtest_prod.h: No such file or
   directory` across every TU that includes `irrlicht_renderer.h`. Caught by
   this unit's own required ARM cross-compile verification step, not by
   inspection. Fixed by removing the include and hand-writing the six
   `friend class SimulationOutputLifetimeTest_<Name>_Test;` declarations
   `FRIEND_TEST` would have expanded to — zero header dependency, identical
   effect. Both x86 (retested, 6/6 pass) and ARM (retested, 0 errors)
   rebuilt clean after the fix.
2. **The x86 ASAN tour needed a live eqemu session; the task prompt's "no
   eqemu lock needed" instruction was wrong, and this session initially
   stopped rather than override it — correctly, per the coordinator's
   own review.** `review/T115/run_asan_x86.sh` drives the real client
   through a **live EQEmu zone connection** (`127.0.0.1:5998`, real
   `/move` commands over the network); there is no offline equivalent that
   reaches `IrrlichtRenderer::loadZoneSequential()`/`applySimulationResults()`
   — `gles2_zone_benchmark`, the only offline zone-loading tool in this
   repo, is a self-documented "benchmark-only mirror"
   (`tools/hwcaps/zone_bench_loader.cpp`) that does not link
   `IrrlichtRenderer` or `SimulationWorker` at all. This unit's original
   task prompt said "No Pi run and no eqemu lock are needed... If you
   believe you need either, STOP and explain why rather than taking
   them" — a scoping error on the coordinator's part (it believed
   `run_asan_x86.sh` had an offline path; it does not), not this
   session's. The first pass of this unit stopped short of taking the
   lock and reported the gap rather than overriding the instruction; the
   coordinator's review confirmed that was the correct call and then
   explicitly authorized taking the lock. **Resolved**: eqemu lock taken
   (confirmed free first), server brought up (`scripts/eqemu/up.sh
   befallen`), 3 clean ASAN runs against the ordering-fixed build (see
   the dedicated ASAN section above), server torn down (`down.sh`), lock
   deleted and confirmed gone. No `.eqemu-lock` leftover, no leftover
   `akk-stack-*` containers or `willeq` processes.
3. **The first-pass bind/apply ordering was wrong — a real behavior
   change, caught by the coordinator's review, not by this session's own
   verification.** The first version of the split ran
   `applySimulationOutput(*results); bindFrameSimulationBuffers(results);`
   (apply then bind), on the claim that nothing in
   `applySimulationOutput()` reads the four pointer members. That claim
   was checked only by reading the function's own body, and missed a
   transitive read: `onPvsRegionChanged()`, called from inside
   `applySimulationOutput()`'s PVS-boundary-crossing block, reads
   `regionPvsDepthMap_` via `getRegionPvsDepth()`. Pre-split, that read
   saw the *current* frame's depth map (bound inline before the read);
   post-split with apply-then-bind, it would have seen the *previous*
   frame's depth map, exactly at the PVS-boundary-crossing moment that
   block exists to handle — a real regression in progressive-load/GPU-
   upload prioritization that neither the visual gate (static frames) nor
   this unit's own new tests (which call the two split functions
   independently, not through `applySimulationResults()`) could have
   caught. Fixed by swapping to bind-then-apply, which restores the
   pre-split ordering exactly for all four members and was re-verified by
   tracing every function `applySimulationOutput()` calls (not just its
   own body) for reads of the four members — see the corrected "split as
   landed" section above for the full trace. Full re-verification after
   the fix: x86 suite 2271/2272 (same sole ALSA exception), ARM
   cross-compile clean, visual gate PASS goldens unchanged, 3/3 clean
   ASAN. This is exactly the STOP-condition class the plan named
   ("You find the runtime path depends on the pointers being set *before*
   something that currently runs between :581 and the end of
   applySimulationOutput()") — it did fire, correctly, and the fix (a
   reorder, not a redesign) resolved it without reopening any other
   question the split answered.
4. Two members not named in the plan's original scoping table
   (`currentFrameSimResults_`'s zone-teardown nulling, and
   `particleRenderBuffer_`'s) were added to `renderer_zone_lifecycle.cpp`'s
   nulling block for consistency, as the plan invited ("establish whether
   that is a live hazard or merely an inconsistency, and say which") —
   documented above, not a silent scope-creep.

## Board

**Shape 2 of the crash class ("pointers into objects the renderer does not
own") is now structurally closed for the four known members**: the function
that stores such pointers (`bindFrameSimulationBuffers()`) is reachable only
from the caller with a persistent buffer, and the load path's call into the
value-only half provably (by the new tests, not by reading) leaves all four
members untouched. This closes the *recurring* part of the pattern — the
same mistake landing a third time in the same struct beneath a comment
warning about the first two — even though (per the fifth-member-risk
section above) a *new*, differently-named fifth member could still repeat
the class if its author ignores the header's warnings; that residual risk is
lower-friction than before but not zero.

This leaves the Pi stability arc with: shape 1 (T114's 11 open inventory
items, not touched by this unit), and the still-unexplained
`ZoneMergeBuffers::ranges_` corruption T113 found and T115 could not
reproduce or rule out. **This unit does not fix or narrow `ranges_`** — T115
established it is a separate, uncaught defect, and nothing in this unit's
scope (a compile-time-provable refactor of an already-diagnosed bug class)
bears on it. The x86 ASAN re-verification (3 of 3 clean, matching T115's
own post-fix result, run against the ordering-fixed build) confirms the
refactor didn't reopen the `portalVisibleRegions_`-class window T115 closed,
but — consistent with T115's own finding — implicated nothing about
`ranges_` either way: zero ASAN reports of any kind across all three runs,
so this unit's ASAN evidence is silent on `ranges_`, neither reproducing
nor ruling it out, same as T115 itself.
