# T105 — What is the zone-in working set actually made of?

**Slot: whichever is free. Live measurement — eqemu lock normally. T103 and
T104 may still be running; if the lock is held, wait, then report outstanding
rather than stealing it.**

**Territory: measurement instrumentation only, `refactor/`,
`/mnt/projects/fable/review/T105/`. PRODUCTION BEHAVIOUR IS READ-ONLY — this
unit measures. Temporary instrumentation is fine if it is removed or left
default-off, and said so.**

## Why this unit exists

Will's design direction: at zone-in, load only the assets actually required
for what is in range, and load/free dynamically as entities enter and leave.
The loading sequence he specified:

1. server sends the entity list with equipment
2. determine entities within render+load distance
3. sort by race/gender to avoid duplicate loads
4. load models
5. load the specific textures for worn equipment and variants

Before building any of that, **establish what the working set actually is.**
If it is genuinely small — 1999-era assets, ETC1-compressed — the gate is easy
to size and the design gets simpler. If some category is unexpectedly fat, we
need to know *which* before designing around it.

**This unit does not build the gate. It measures the target.**

## What is already known — do not re-derive

- The EQE1 set configured per zone is ~156MB across 37 archives (36 present),
  of which **28 archives / ~120MB are actually mapped** at zone-in. Only 3 of
  the 37 are zone-specific; the rest is one fixed global list applied to every
  zone (`config/eqe1_archives.json` has a single `default` profile).
- Memory-gate baselines exist and are instrumented: `eqe1MappedBytes`,
  `characterModelsBytes`, `placeableObjectsBytes`, `gpuTextureBytes`,
  `textureCacheBytesUsed`, `totalEstimate`, `processRssBytes`
  (`renderer_diagnostics.cpp`). Reuse them; do not build parallel counters.
- The 40-entity limit is a **render** cap for FPS, applied after building.
  It is **not** a load gate. Load limits are about RAM. Related, separate —
  do not conflate them in your analysis.

## Part 1 — The working set, by distinct asset

For a zone-in, measure **what is genuinely needed** for entities in range:

- How many **distinct (race, gender)** pairs are within a plausible load
  radius? Not entity count — distinct model pairs, since step 3 dedups.
- How many **distinct worn-equipment variants/textures** do those entities
  actually reference? This is the step-5 question and the one nobody has
  numbers for.
- What do those cost, **ETC1-compressed**, versus what is currently resident?

Do at least: one populated outdoor zone, one indoor zone, and one with varied
races. `nektulos` (452 entities), `befallen` (171) and `qeynos2` are the
zones with existing baselines to compare against.

## Part 2 — Mapped versus resident

`eqe1MappedBytes` counts **mapped** bytes — address space, not resident
memory. `mmap` is demand-paged, so the real cost may be far lower.

Measure it: read `/proc/<pid>/smaps` for the EQE1 mappings after zone-in and
report **`Rss` against `Size` per mapping**, plus the totals.

This is not an argument for keeping `mmap` — Will's determinism argument
against it stands regardless, since a clean page evicted under pressure means
a major fault against SD-class storage mid-frame on the Pi. But knowing the
resident share tells us **how much of the 120MB is real today**, which sizes
the win and orders the work.

## Part 3 — Where does the resident memory actually go?

`processRssBytes` was ~273MB for qeynos2 against a `totalEstimate` of ~172MB.
**Something is not accounted for.** Break down the resident set: EQE1
mappings, decoded textures in `ConstrainedTextureCache` (CPU copies), GPU
uploads, model geometry and skeletons, zone geometry, and whatever remains
untracked.

The untracked share is itself a finding — `untrackedBytes` already exists as a
category and T69 flagged gating on it as a deferred question.

## What to produce

A table a design decision can be made from:

| category | mapped | resident | ETC1-compressed need | waste |

plus the distinct-model and distinct-variant counts per zone, and an explicit
statement of **which category is the largest real cost.**

## Pi context, stated not measured

The target is an Orange Pi One: **Mali400, 512MB, SD-class storage**. Do not
attempt Pi measurements in this unit — x86 numbers plus the ARM cross-compile
are enough to size the design. But **state which findings would differ on the
Pi** and why, particularly anything where page-cache behaviour or storage
latency changes the answer.

## What you may not do

- **Do not build the load gate, the manifest, or per-entity texture
  selection.** This unit measures; the design follows from it.
- Do not change eviction, caching or loading behaviour.
- Do not conflate the render cap with a load limit.
- Any instrumentation added must be removed or default-off, and stated.

## Verification

Full build and suite (current total 2217; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual gate —
goldens must not move, since behaviour is unchanged.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
restore DB state and confirm by `SELECT` **after logout**; kill clients by real
binary PID, not `nohup`'s `$!`; clean teardown, delete the lock.

## Review

(executor fills: distinct (race,gender) pairs and distinct equipment variants
in range, per zone; mapped vs resident per EQE1 mapping; the full resident
breakdown including untracked; the ETC1-compressed size of the genuine working
set; **which category is the largest real cost**; what would differ on the Pi;
instrumentation added and how it was disposed of; deviations)

## Board

This measurement sizes the load gate Will's design needs, and determines
whether granular extraction or per-entity texture selection is the bigger win.
Record the numbers so the design unit does not re-measure.
