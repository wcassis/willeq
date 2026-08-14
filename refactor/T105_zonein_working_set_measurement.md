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

Executed in `/mnt/projects/fable/wt/slot2`, task branch `task/T105-working-set`,
base fable tip `23c63788`. Live eqemu measurement performed (lock free at
session start; taken, used, released cleanly). Full raw logs, benchmark
JSON, smaps dumps and the analysis script output are in
`/mnt/projects/fable/review/T105/` (README there indexes every file).
**Measures only — no gate, manifest, or per-entity texture selection was
built; production caching/eviction/loading behaviour is unchanged.**

### Method

Two independent measurement paths, cross-checked against each other and
against T80's/T91's/T94's own numbers:

1. **Live populated zone-in via eqemu** (Testchar→befallen, Testcaster→
   nektulos, Testmelee→qeynos2, `-d 4 --log-level=DEBUG`, no `--eqe1-path`).
   A single new `LOG_DEBUG` line was added at the top of
   `loadZoneStep8_Entities`'s per-entity loop (`renderer_zone_load.cpp`) that
   dumps `spawn/race/gender/dist-from-player/texture/helm/chestMat/equip7/
   equip8/isNPC/isPlayer` for every entity in the zone's real spawn list —
   this is the only production code touched. It reuses fields the adjacent
   Step8a-8d code already reads (T91/T94's territory), adds nothing to the
   spell/loading/caching logic, and is `LOG_DEBUG` only — **default-off at
   the project's default log level (NONE)**, exercised only because this
   session explicitly requested `--log-level=DEBUG`. Left in place rather
   than reverted, per the plan's "default-off instrumentation may stay"
   allowance — the same question (what's the real working set at a given
   load radius) will recur when the load gate itself gets designed, and the
   line costs nothing when disabled.
2. **Standalone `gles2_zone_benchmark`** (no eqemu, no network — loads the
   real zone S3D + a synthetic 5-race/5-entity population), run exactly as
   T80's coverage benchmark (`--eqe1-path data/eqe1_client --objects
   --entities 5 --test-races 1,2,3,4,5 --frames 5 --warmup 1
   --memory-json ...`). This reproduced T80's qeynos2 baseline essentially
   exactly (`eqe1MappedBytes` 119,943,391 vs T80's 119,943,391; 28 archives;
   `totalEstimate` 172,352,354 vs ~172MB) — confirms both this session's
   tooling and T80's are measuring the same thing, and gave a place to safely
   read `/proc/<pid>/smaps` (polled every 50-100ms while the process was
   alive) without needing a fragile live-session capture.

**One reading discarded as garbled, stated per the executor protocol's
"garbled evidence voids the claim" rule**: the first Befallen live attempt
used `--eqe1-path` pointed at the full `/mnt/projects/EverQuest_ETC1` tree
(1506 files) and printed `[EQE1 Flat Packs] 4544.5 MB mmap'd (1506 archives
open)`. This is internally impossible — `resolveEQE1Archives()` is
config-driven and bounded to the 37 names in `config/eqe1_archives.json`
(confirmed by reading `getOrOpenEQE1`/`tryOpenNextGlobalEQE1`: the lazy
opener only ever advances through `pendingGlobalEQE1Archives_`, which has
34 entries, and never resets or re-scans the directory) — and every other
reading of the identical directory in this same session (the benchmark tool,
three zones, twice for nektulos) shows 28 archives / ~110-120MB, matching
T80 exactly. The log file also has multiple lines out of timestamp order
around the same point, consistent with a threaded-logger flush race under
`--log-level=DEBUG`. Kept in `review/T105/` as
`befallen_garbled_first_attempt.log` for the record; nothing in this Review
is built on it. A clean re-run (no `--eqe1-path`, same zone/character)
produced sane, internally-consistent numbers and is what Part 1/3's live
figures below use.

### Part 1 — distinct (race,gender) pairs and equipment variants in range

The renderer's actual current load/render distance is **300 units**
(`Camera far plane: 2000, render distance: 300` — logged at every startup,
matches `src/client/graphics/CLAUDE.md`'s stated clip distance). That is the
`radius<=300` row below — the plausible number for "what would a load gate
matching today's render behaviour need." 150 and 600 are shown as bounds.

| Zone | Entities total | Race models currently loaded (undistanced) | Entities ≤300u | Distinct (race,gender) ≤300u | Distinct full appearance-variant tuples ≤300u | Distinct equipment model-IDs ≤300u |
|---|---|---|---|---|---|---|
| Befallen (indoor) | 178 | 17 | 69 | **9** | 18 | **8** |
| Nektulos (outdoor, populated) | 451 | 20 | 17 | **6** | 6 | **3** |
| Qeynos2 (varied races) | 195 | 16 | 33 | **9** | 14 | **5** |

("Race models currently loaded" is the renderer's own `/pmem` `[Entities] N
race models` count — Step 8 already dedups by (race,gender) across the
*whole* zone's entity list via `RaceModelLoader`'s cache, T91/T94's
territory; it is not distance-gated at all today.)

**Headline: distance-gating to the render distance the game already uses
would cut the number of distinct race models actually needed by 44-70%**
(befallen 9/17=53% of current, nektulos 6/20=30%, qeynos2 9/16=56% — i.e. a
30-56% reduction depending on zone population density near spawn).

**The equipment-variant number nobody had**: distinct worn-equipment model
IDs in range is **3-8** per zone at render distance — small. This is the
number that decides whether per-entity texture selection (Will's step 5) is
worth building as a separate mechanism versus just riding the existing
race-model dedup: with only 3-8 distinct equipped-weapon models actually in
view at once, the "lgequip 43MB" question from the plan's framing is not
about how many variants are visible at once (few) but about which few, which
can only be known at runtime — confirming the plan's stated correction that
this cannot be resolved statically.

Every zone also reproduced the same fixed placeholder-cube-fallback
population (T83/T91's "GM-summon debris" class — races with no model
anywhere: race=415/457 gender=2 in qeynos2/befallen, plus race=330/432/
436/440/468/471 in nektulos) — consistent across zones, not new, not
in scope to fix here.

### Part 2 — mapped versus resident

`/proc/<pid>/smaps` for the EQE1 mappings, all 28 configured archives open
in every zone (matches `eqe1ArchiveCount` from the memory-JSON):

| Zone | Archives | Total mapped (Size) | Total resident (Rss) | % resident |
|---|---|---|---|---|
| Qeynos2 | 28 | 117,176 KB (114.4 MB) | 12,424 KB (12.1 MB) | **10.6%** |
| Befallen | 28 | 110,488 KB (107.9 MB) | 5,532 KB (5.4 MB) | **5.0%** |
| Nektulos | 28 | 109,772 KB (107.2 MB) | 1,724 KB (1.7 MB) | **1.6%** |

Per-mapping detail (full table in `review/T105/bench/eqe1_smaps_summary.txt`).
The largest configured archive in every zone, `lgequip.eqe1` (23.6 MB
mapped), is **64 KB resident in all three zones** — 0.3% of its mapped size,
identically, every time. The zone's own small archive (`qeynos2.eqe1`,
`befallen.eqe1`, `nektulos_obj.eqe1` etc.) is the one exception with high
residency (72-98%) — expected, since it's read in full at zone load rather
than touched only for the handful of textures a given entity's variant
happens to need.

**This confirms the plan's framing precisely: the 43MB `lgequip` question is
overwhelmingly address-space, not real memory.** It is not an argument for
keeping `mmap` (see Pi section below) — but it does mean the *current* real
cost of the global equipment archive set is single-digit-MB, not 43MB, on
this host.

### Part 3 — where resident memory actually goes

Two sources, clearly separated (see Method above for why):

**(a) Standalone benchmark, synthetic 5-race population, real
`processRssBytes`/`untrackedBytes`** — reproduces T80's baseline:

| Category | Qeynos2 | Nektulos | Befallen |
|---|---|---|---|
| textureCacheBytesUsed | 9.16 MB | 3.66 MB | 2.73 MB |
| eqe1MappedBytes (mapped / **resident**, Part 2) | 119.9 MB / **12.4 MB** | 112.4 MB / **1.7 MB** | 113.1 MB / **5.5 MB** |
| zoneGeometryBytes | 1.27 MB | 0.65 MB | 1.73 MB |
| placeableObjectsBytes | 1.37 MB | 1.55 MB | 2.17 MB |
| s3dZoneSourceBytes | 7.78 MB | 2.76 MB | 6.13 MB |
| gpuTextureBytes | 9.68 MB | 4.14 MB | 2.99 MB |
| vboEboBytes | 2.73 MB | 2.30 MB | 4.00 MB |
| framebufferBytes | 3.84 MB | 3.84 MB | 3.84 MB |
| characterModelsBytes (5 synthetic races) | 25.7 MB | 25.7 MB | 25.5 MB |
| **totalEstimate (as reported, mapped basis)** | 172.4 MB | 153.3 MB | 159.5 MB |
| **processRssBytes (real)** | 263.5 MB | 232.9 MB | 249.1 MB |
| **untrackedBytes** | 91.2 MB (34.6%) | 79.6 MB (34.2%) | 89.7 MB (36.0%) |

**~34-36% of RSS is untracked, consistently, in every zone measured** — this
matches T69's original ~100MB/~273MB observation on qeynos2 almost exactly
and confirms it is a stable, real, zone-independent gap, not zone-specific
noise. This unit does not further decompose it (T69 already flagged gating
on it as deferred, and doing so would need a heap-profiling pass — malloc
arena overhead, Irrlicht/GLES2 driver internal allocations, llvmpipe's own
buffers, thread stacks, and `EQE1Loader`'s per-open bookkeeping structures
outside the mmap itself are the plausible contributors, in descending order
of likely size, but that is informed hypothesis, not a measurement this
unit performed).

**(b) Live client, real populated zone, no `--eqe1-path` (S3D-decoded
textures)** — `characterModelsBytes` scaling with the *real* distinct
race-model count (17-20, not 5):

| Zone | characterModelsBytes | Tracked total (`--- Tracked total ---`) |
|---|---|---|
| Befallen | 23.0 MB (tex 1.6, geom 2.8, skel 5.5, char 11.2, anim mesh 1.9) | 62.7 MB |
| Nektulos | 51.1 MB (tex 29.3, geom 4.0, skel 6.1, char 9.2, anim mesh 2.5) | 90.9 MB |
| Qeynos2 | 21.5 MB (tex 1.4, geom 2.6, skel 4.3, char 10.9, anim mesh 2.4) | 86.6 MB |

No live `processRssBytes` was captured for (b) — see "Instrumentation and
findings along the way" below; it is a genuine finding, not a data gap this
unit could close without touching production code further.

### Which category is the largest real cost

**Character Models.** Once EQE1 is corrected from mapped to resident (Part
2: 1.7-12.4 MB, not 110-120 MB), Character Models is the largest *tracked*
category in every zone by a wide margin over Zone Geometry, Placeable
Objects, S3D Zone Source, or the corrected EQE1 residency — both in the
synthetic 5-race benchmark reading (25.5-25.7 MB, ~40% of resident-corrected
tracked total) and, more decisively, in the live populated-zone reading
where the real race-model count (17-20, not 5) drives it to 21.5-51.1 MB —
2-4x zone geometry, and larger than every other single tracked category
combined in Nektulos. This directly answers the plan's "if some category is
unexpectedly fat, we need to know which" — it is not the equipment archives
(the plan's own suspect, `lgequip`), it is the base race models, and the
lever that controls its size is exactly the one Part 1 measured: distinct
(race,gender) pairs actually in range (30-56% smaller than what's loaded
today, un-distanced).

**Separately, and larger than any single tracked category in absolute
terms**: the untracked ~85-91 MB (34-36% of RSS) in every zone. It is not
attributable to any one subsystem by this unit's tooling, but it is real,
resident, and bigger than Character Models — stated plainly rather than
buried under the "largest tracked cost" framing, per the plan's honesty
requirement.

### What would differ on the Pi (stated, not measured)

- **Part 2's residency ratios are an x86-page-cache-warm best case, not a
  transferable number.** This dev VM never evicts (nothing here ever hit
  memory pressure) and its backing storage for `/mnt/projects/EverQuest_ETC1`
  is effectively RAM-speed. The Pi's SD-class storage makes a **major fault**
  on a cold/evicted `mmap` page cost single-digit milliseconds, not
  microseconds — at 512MB total RAM with real budget pressure, eviction
  under `mmap` is a real, expected event there (not the "zero evictions
  across every P13/P13b configuration" x86 has shown), so the *same* 1.6-
  10.6% residency figure could still mean stalls the x86 measurement cannot
  see. This is exactly Will's determinism argument from the plan, now with
  a concrete number attached: even though the resident *share* is small
  everywhere measured, a page fault against SD storage mid-frame is the
  actual risk, not the byte count.
- **Character Models being the largest tracked cost matters far more on the
  Pi.** 21-51 MB against this VM's effectively-unbounded RAM is noise; against
  the Orange Pi's ~128 MB constrained-preset budget it is 16-40% of the
  entire budget from race models alone, before equipment, zone geometry, or
  the untracked share. The 30-56% reduction available from distance-gating
  (Part 1) is proportionally a much bigger win on the Pi than the raw MB
  figures suggest on this host.
- **The ~34-36% untracked RSS share is more dangerous on a 512MB device.**
  If the same ratio holds on ARM (unmeasured — different malloc, different
  GLES2 driver, different libc), a budget built only from the tracked
  categories would be short by roughly a third of real usage, which is a
  much bigger fraction of 512MB total than of this VM's headroom.
- **ETC1 itself is Pi-specific value, not something this x86 measurement
  can validate.** llvmpipe (this VM's GPU) does not have native ETC1
  decode, so `gpuTextureBytes` here reflects the same driver-side expansion
  regardless of source format; the real 6:1 win only exists on the Mali400's
  native `GL_ETC1_RGB8_OES` path, per `src/client/graphics/CLAUDE.md`.

### Instrumentation added and how disposed of

One `LOG_DEBUG` line in `loadZoneStep8_Entities`
(`src/client/graphics/renderer_zone_load.cpp`) — see Method above.
**Left in place, default-off** (project default log level is NONE; only
active under `--log-level=DEBUG`, same discipline as every adjacent Step8a-
8d per-entity line). No other production code was touched: eviction,
caching, and loading behaviour are byte-for-byte unchanged from `23c63788`.

### Deviations from the plan

- The plan named `nektulos`/`befallen`/`qeynos2` as the three zones to
  cover (populated outdoor / indoor / varied-races); all three were done as
  specified, no substitution.
- One Befallen reading was discarded as garbled (see Method) and re-run
  clean — not silently absorbed, called out explicitly above and in
  `review/T105/README.md`.
- Live `processRssBytes` for the real populated-zone runs (Part 3(b)) was
  not obtainable: the only reachable `/pmem` command handler
  (`irrlicht_renderer.cpp:1219`) passes an **empty** `MemoryReportInput`,
  so `processRssBytes`/`untrackedBytes` are always 0 through that path —
  the code that *does* populate real RSS (`EverQuest::runPmemDiagnostics`/
  `runLoadDiagnostics`, `eq_commands.cpp`) is never called from anywhere
  reachable in a running client (grepped: `runLoadDiagnostics` has zero
  callers). This is a genuine finding — another instance of the
  implemented-but-unreached pattern the project's memory already tracks
  nine of — but fixing it is production behaviour, out of this unit's
  read-only scope, so it is **not fixed here**, only reported (added to
  `refactor/deferred.md`). Worked around by using the standalone benchmark
  tool (which does populate real RSS) for Part 2/3's authoritative
  mapped/resident/untracked numbers instead, cross-checked against the live
  client's `characterModelsBytes`/tracked-total for the real-population
  comparison in Part 3(b).

## Board

This measurement sizes the load gate Will's design needs, and determines
whether granular extraction or per-entity texture selection is the bigger win.
Record the numbers so the design unit does not re-measure.
