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

(executor fills: the split as landed; confirmation the T111/T115 nulling
lines are **deleted** and why that is safe; what happened to
`currentFrameSimResults_` and its wrong comment; the
`particleRenderBuffer_`/lifecycle-nulling answer; the new tests and which
real call paths they exercise; x86 ASAN result vs T115's 3-of-3; whether the
stretch was attempted and what it cost; whether a fifth member could still
repeat this; deviations)

## Board

If this closes, say what it means for the Pi stability arc — specifically
that shape 2 of the crash class ("pointers into objects the renderer does not
own") is structurally closed, leaving shape 1 (T114's 11 open inventory
items) and the still-unexplained `ZoneMergeBuffers::ranges_` corruption. Do
**not** claim this fixes `ranges_` — T115 established it is a separate,
uncaught defect.
