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

(executor fills: the radius chosen and its justification; whether the existing
streaming path was reused and the evidence it reliably picks up deferred
entities; before/after per machine for load time, race models and entity
counts; RSS confirmation; the permanently-missing-entity check in a populated
zone; whether goldens moved and why; deviations)

## Board

Record the Pi load-time delta. That number is the justification for this unit
and for whatever follows in Will's loading design.
