# T80 — Three memory categories are baselined at zero and guard nothing

**Slot: first free. x86 only. No Pi. NO EQEMU.**

**Territory: `scripts/ci/visual_gate.py`, `scripts/ci/visual_gate.sh`,
`tools/hwcaps/gles2_zone_benchmark.cpp` (only if the memory snapshot needs
a field it does not already emit), and `benchmarks/` baselines.**

Do not change rendering. Do not change the existing golden-image runs —
see the constraint below, it is the crux of the unit. `refactor/deferred.md`
is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T69_memory_enforcement_gate.md`'s
Review in full** — it built the gate and measured everything you are about
to extend.

## Why this unit exists

T69 wired `/pmem`'s per-subsystem accounting into the visual gate. Will
has **held off blessing it** for a specific reason: three of the thirteen
measured categories baseline at **zero**, because the gate's benchmark
invocation never exercises them.

- `eqe1MappedBytes` / `eqe1ArchiveCount` — the gate does not pass
  `--eqe1-path`.
- `placeableObjectsBytes` — the gate does not pass `--objects`.
- `characterModelsBytes` — the golden zones load no entities.

Blessing today would record "guarded" for three categories that guard
nothing. A character-model memory regression would sail straight through.
**The gate covers less than its field list implies**, and that gap is
exactly the kind of thing that reads as coverage a year from now.

## The constraint that shapes the whole unit

**Do not add these flags to the existing golden-image runs.**

Loading objects and entities changes what is rendered, so the golden
images would move — which would require Will to re-bless *images*, a
separate and much larger judgement, and would conflate two unrelated
blessings. It would also make the image goldens hostage to entity
placement.

**Add a separate memory-coverage run instead.** The image-comparison runs
stay exactly as they are; a new invocation exercises full asset loading
and supplies the memory baselines. The two want opposite things — image
goldens want minimal and stable, memory coverage wants maximal — and
separating them is correct on its own merits, not just as a workaround.

The flags exist already: `--eqe1-path`, `--objects`, `--entities N`,
`--test-races` (`gles2_zone_benchmark.cpp:342, 380, 421, 581`). Use them.

## The trap — re-measure determinism, do not inherit it

T69's **zero-tolerance exact-match** assertions were an empirically
measured choice: 9 runs across 3 zones, every baseline field
bit-identical. **But that was measured on a path that loaded no objects
and no entities.**

Entity placement, model loading order and object loading may not be
deterministic in the same way. **Re-run the determinism measurement for
the newly-exercised categories** — same discipline as T69's Step 3, repeat
runs, report the per-field spread — and set tolerance from what you
measure.

If a newly-covered category is genuinely jittery, **report it and do not
assert on it** rather than inventing a tolerance to make it fit. T69's
plan said an honest gap beats a false alarm, and that still holds: a gate
that fires spuriously gets waived, and this project already paid for that
lesson when T35 found the RDP "flake" waiver had masked a real
use-after-free.

## Do not bless

**Report the numbers; Will blesses in chat.** He does not run commands —
this is his stated workflow and I got it wrong once already.

So: produce the new baseline values, per zone and per category, in a form
he can read and approve. State plainly which categories now measure real
values, which remain zero and why, and what tolerance each will use. If
anything still cannot be exercised, say so rather than leaving a zero
looking like coverage.

## STOP conditions

- The golden images moving. That means the coverage run is not properly
  separated.
- Needing to change rendering behaviour to exercise a category.
- A newly-covered category proving non-deterministic in a way that needs a
  design decision — report rather than choosing a tolerance to paper over
  it.

## Verification

Full protocol bar, plus: proof the golden-image runs are byte-identical to
before (that is the separation working), the determinism data for the
newly-covered categories, and the proposed baseline table.

Also re-confirm T69's deliberate-failure proof still holds — breach a
budget and show the gate fails — since you are changing the code that
performs it.

## Review

### How the coverage run is separated

A second, independent benchmark invocation was added — `run_memory_coverage_benchmark()`
in `scripts/ci/visual_gate.py` — driven by new `MEMORY_COVERAGE_*` constants
(`--eqe1-path data/eqe1_client --objects --entities 5 --test-races 1,2,3,4,5
--frames 5 --warmup 1`, fixed and explicit rather than the loader's implicit
defaults). It is called from `bless_memory_coverage()`/`verify_memory_coverage()`,
which are invoked *after* the existing golden-image loop in `bless()`/`verify()`
and write/read a **separate manifest key**, `memoryCoverage`, never touching
`manifest["zones"]` (the golden-image section T69 built). Three structural
guarantees keep the two runs from ever interfering:

1. **No pixel output at all.** The coverage invocation passes no
   `--dump-frames`/`--dump-png-prefix`/`--csv` — only `--memory-json`. It
   cannot write a golden PNG because it never asks the benchmark to produce
   one.
2. **Separate output filename.** `{zone}_coverage_mem.json` vs the
   golden-image run's `{zone}_mem.json` in the same `ARTIFACTS_DIR` — the two
   never collide even when run back-to-back for the same zone.
3. **Separate manifest key.** `memoryCoverage.zones[].memory` vs
   `zones[].memory`. `bless_memory_coverage()`/`verify_memory_coverage()`
   read and write only the former.

`data/eqe1_client` is a new gitignored local symlink to
`/mnt/projects/EverQuest_ETC1` (mirrors the pre-existing `data/eq_client` →
`/mnt/projects/EverQuestP1999` convention, same NFS-backed asset directory
`scripts/pi/env.sh`'s `PI_EQE1_PATH` already names as canonical). If it's
absent on a given machine, `bless_memory_coverage()`/`verify_memory_coverage()`
print a loud `SKIP` for the coverage section only and continue — the
golden-image checks still run and still gate. This is a real, visible
coverage gap on that machine, not a silent pass; see the "Do not bless"
section below for why the committed manifest ships with no baseline yet
regardless.

**Proof the golden-image runs are unaffected**, measured three ways:
1. `git diff tests/goldens/manifest.json` against the fable tip (before any
   gate run) shows a single purely-additive hunk: the new `memoryCoverage`
   key appended after the existing `zones` array. The `zones` block itself
   — the golden-image PNG/metric/memory baselines T69 built — is
   byte-for-byte untouched.
2. Ran the real, unmodified `scripts/ci/visual_gate.sh` against the actual
   committed goldens: **PASS**. `git status --porcelain tests/goldens/`
   before and after the run shows only the pre-existing `manifest.json`
   diff from (1) — no PNG was written or modified by running the gate.
3. The deliberate-failure harness (see below) blessed a **scratch** copy
   of `tests/goldens/` and repeatedly broke/fixed fields; `git status
   --porcelain tests/goldens/` against the real committed repo was checked
   after every step and never showed anything beyond the (1) diff.

### What each newly-covered category now measures

All three categories T69 flagged as guarding nothing now read real,
nonzero values whenever the coverage run executes:

| Category | Before (golden-image run) | Now (coverage run, qeynos2 example) |
|---|---|---|
| `eqe1MappedBytes` | 0 (no `--eqe1-path`) | 119,943,391 bytes |
| `eqe1ArchiveCount` | 0 | 28 archives |
| `placeableObjectsBytes` | 0 (no `--objects`) | 1,365,564 bytes |
| `characterModelsBytes` | 0 (no entities loaded) | 25,736,893 bytes |

**One bug fixed along the way, in Territory (`gles2_zone_benchmark.cpp`/its
new-TU sibling) since it's the same field this unit is extending:**
`eqe1MappedBytes` stayed hardcoded at 0 even with `--eqe1-path` passed,
because `zone_bench_memory.cpp`'s `computeMemorySnapshot()` only counted
`eqe1ArchiveCount` and carried a comment claiming `EQE1Loader` exposed no
per-loader byte accessor. That's wrong — `EQE1Loader::getMappedBytes()`
(`include/client/graphics/eqe1_loader.h`) has existed since P04, predating
T69. Fixed to sum `getMappedBytes()` across `ZoneBenchData::eqe1Loaders`,
the exact same accessor and summation
`IrrlichtRenderer::getEQE1MappedBytesTotal()` performs in production
(`renderer_effects.cpp:417-424`) — same reuse discipline as every other
category in that file. Without this fix, `eqe1MappedBytes` would have
stayed at 0 even under the new coverage run, silently reintroducing exactly
the gap this unit exists to close.

### Determinism data and resulting tolerances

Same discipline as T69's Step 3: repeat runs under the coverage flags,
report the per-field spread, choose tolerance from what's measured — not
inherited from T69's own (unloaded) measurement. 5 runs on qeynos2, 3 each
on gfaydark/befallen (matching T69's 9-run/3-zone shape), all under
`--eqe1-path data/eqe1_client --objects --entities 5 --test-races 1,2,3,4,5`:

| Category | qeynos2 (5 runs) | gfaydark (3 runs) | befallen (3 runs) |
|---|---|---|---|
| `eqe1MappedBytes` | bit-identical | bit-identical | bit-identical |
| `eqe1ArchiveCount` | bit-identical (28) | bit-identical (28) | bit-identical (28) |
| `placeableObjectsBytes` | bit-identical | bit-identical | bit-identical |
| `characterModelsBytes` | bit-identical | bit-identical | bit-identical |
| `zoneGeometryBytes`/`s3dZoneSourceBytes`/`gpuTextureBytes`/`vboEboBytes`/`textureCacheBytesUsed`/`textureCount`/`totalEstimate` | bit-identical | bit-identical | bit-identical |
| `processRssBytes` | spread 274,432 B (0.10%) | spread 307,200 B (0.10%) | spread 151,552 B (0.06%) |
| `untrackedBytes` | spread 274,432 B (0.27%) | spread 307,200 B (0.28%) | spread 151,552 B (0.15%) |

Every field this unit gates (`MEMORY_COVERAGE_FIELDS`:
`eqe1MappedBytes`/`eqe1ArchiveCount`/`placeableObjectsBytes`/
`characterModelsBytes`) was bit-identical across every repeat run in every
zone. **This empirically confirms the trap the plan warned about did not
materialize** — entity placement and object/EQE1 loading are deterministic
on this llvmpipe path the same way plain zone geometry was under T69 — so
**exact-match (zero tolerance) is used**, same as T69's bucket (b), and for
the same reason: measured, not assumed. No newly-covered category needed
the "too jittery to gate, report only" treatment. RSS/untracked behave
exactly as T69 found for the golden-image path (small, real spread) and are
printed report-only in the coverage run too, never asserted — consistent
with T69's Step 4 ruling, not re-litigated here.

Raw per-run JSON and the analysis script used to produce the table above
were kept in this session's scratchpad (`t80_det/`), not committed —
matching T69's `run_gate_test.py` precedent of scratch-only tooling.

### Category still at zero, and why

None of the four `MEMORY_COVERAGE_FIELDS` categories are zero anymore. The
full list of categories still without a *configured budget* (a separate
question — see the deferred.md update below) is unchanged from T69's list;
this unit did not add or remove entries from that list, only changed which
of them are *exercised*. What remains genuinely unmeasured by any benchmark
invocation, coverage or golden-image — entity per-instance game-state data,
equipment models, HCMap/collision, audio, network, door/spell-DB estimates
— is exactly the "not measured by the gate at all" list T69 already
recorded (`gles2_zone_benchmark`'s `ZoneBenchData` never constructs those
subsystems); T80 did not extend the benchmark's object graph to reach them,
per its own Territory (memory-JSON emission only, not new subsystem
construction) and STOP conditions ("needing to change rendering behaviour
to exercise a category").

### Proposed baseline table for Will

**Not blessed in this unit** — see "Do not bless" below. These are the
measured values `--bless` would write to `memoryCoverage.zones[].memory` if
approved (same numbers as the determinism table above, one run's worth,
since every run was bit-identical):

| Zone | `eqe1MappedBytes` | `eqe1ArchiveCount` | `placeableObjectsBytes` | `characterModelsBytes` |
|---|---|---|---|---|
| qeynos2 | 119,943,391 | 28 | 1,365,564 | 25,736,893 |
| gfaydark | 118,609,483 | 28 | 5,624,100 | 25,368,423 |
| befallen | 113,093,979 | 28 | 2,168,052 | 25,496,300 |

Report-only context (not gated, printed for visibility, same treatment as
T69's RSS/untracked — note these totals are **not comparable** to the
golden-image run's RSS/`totalEstimate` since the coverage run's asset mix
is deliberately heavier: it loads 28 EQE1 archives, zone objects, and 5
entity models the golden-image run never touches):

| Zone | `totalEstimate` | `processRssBytes` |
|---|---|---|
| qeynos2 | 172,352,354 | ~272.9–273.2 MB |
| gfaydark | 183,871,942 | ~294.2–294.5 MB |
| befallen | 159,450,602 | ~258.7–259.0 MB |

Proposed tolerance: **exact match (zero tolerance)** for all four
`MEMORY_COVERAGE_FIELDS`, per the bit-identical determinism data above —
same empirical standard T69 used, not a default inherited without
re-measuring.

### Do not bless

Per instruction, the committed `tests/goldens/manifest.json` ships with a
`memoryCoverage.zones` list (zone names only) but **no `memory` baseline for
any zone** — the mechanism ships, unblessed, exactly as T69 shipped its new
bucket-(b) fields unblessed in the real manifest. `verify_memory_coverage()`
detects the missing baseline and prints `NOT YET BLESSED ... skipping
assertion` per zone rather than false-failing or silently passing (confirmed
live against the real committed manifest — see the verification section
below). The moment Will approves the table above and someone runs `--bless`
for real, `bless_memory_coverage()` populates all three fields for all three
zones and `verify_memory_coverage()`'s exact-match assertions activate for
the first time.

### Proof of the deliberate failure (re-confirmed, since this unit changes
the code that performs it)

Same harness pattern T69 established (`run_gate_test.py`, imported the real
`bless()`/`verify()` against an isolated scratch copy of `tests/goldens/`,
monkeypatching only `GOLDENS_DIR`/`MANIFEST_PATH`/`ARTIFACTS_DIR` — never the
committed repo). Kept in this session's scratchpad, not committed. Seven
steps, all passed:

1. `bless()` against the scratch copy populates `memoryCoverage.zones[].memory`
   for the first time, with all four `MEMORY_COVERAGE_FIELDS` nonzero for
   all three zones (values matched the proposed table above exactly).
2. `verify()` against its own fresh baseline: **PASS** (both the
   golden-image and coverage sections).
3. Monkeypatched `run_memory_coverage_benchmark()` to add 999 bytes to
   `placeableObjectsBytes` (a T80 baseline-guarded field) — `verify()`
   **FAILED** with `memory-coverage qeynos2: placeableObjectsBytes mismatch
   (expected 1365564, got 1366563)` (and the same for gfaydark/befallen).
4. Monkeypatched to add 12345 bytes to `eqe1MappedBytes` instead — `verify()`
   **FAILED** with `memory-coverage qeynos2: eqe1MappedBytes mismatch
   (expected 119943391, got 119955736)`.
5. Removed the injection — `verify()` **PASSED** again, proving the
   failures above were caused by the injected breach, not a broken
   assertion.
6. Monkeypatched the **golden-image** `run_benchmark()` (unrelated code
   path) to add 1 byte to `textureCacheBytesUsed` — the **pre-existing H02/
   T69 assertion** — `verify()` **FAILED** with `qeynos2: texture cache
   bytes used mismatch (expected 42226984, got 42226985)`, confirming T69's
   original assertion still behaves exactly as before this unit touched the
   file.
7. Checked `git status --porcelain tests/goldens/` against the real,
   committed repo after every step above: only the pre-existing
   `memoryCoverage` structural addition (documented in the "how the
   coverage run is separated" proof) — nothing from the scratch bless/break/
   fix cycle ever touched it.

### Verification (full protocol bar)

1. `cmake --build build -j24` — clean build, `willeq` + full test suite
   built.
2. `ctest --test-dir build --output-on-failure` — **2057/2058**, sole
   failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host
   exception), matching the standing baseline exactly. `visual_gate` (test
   #2058) itself **PASSED** within ctest.
3. `bash scripts/build-gles2-x86.sh` — clean build, `willeq` +
   `gles2_zone_benchmark` (now including the new `zone_bench_memory.cpp`
   fix) built successfully.
4. Visual gate: **PASS**, goldens untouched — see the separation proof
   above.
5. `bash scripts/build-arm-noble.sh` — clean cross-build, `willeq` +
   `gles2_zone_benchmark` and the rest of the ARM tool set built
   successfully (340MB unstripped `willeq`, standard for this target).
6. `check_file_sizes.sh fable` (run via `env -C <slot> bash
   scripts/refactor/check_file_sizes.sh fable`, T67's documented cwd
   workaround) — 1 changed source file checked
   (`tools/hwcaps/zone_bench_memory.cpp`, 242 lines): **ok**. `0 warn, 0
   fail`.

### Deviations

1. **`eqe1MappedBytes` bug fix, disclosed above** — a real correction to
   T69-era code in `zone_bench_memory.cpp` (see "What each newly-covered
   category now measures"), not a new mechanism; in Territory since it's
   the same file/field this unit is extending coverage for.
2. **New gitignored local path, `data/eqe1_client`** — a symlink to
   `/mnt/projects/EverQuest_ETC1`, added to `.gitignore` alongside the
   pre-existing `data/eq_client` entry. Not code; matches an established
   convention exactly (see "how the coverage run is separated").
3. **`check_file_sizes.sh`'s cwd dependency (T67's documented finding,
   reproduced identically to T69's Review)** — running the script from the
   ambient shell cwd resolves the wrong repo root; worked around with `env
   -C <slot> bash scripts/...`, no script changes made (out of Territory).
   Noted here as confirmation of the existing board entry, not a new
   finding.
4. **Territory's "`benchmarks/` baselines" actually means `tests/goldens/manifest.json`.**
   The gate's baseline data (`p95CpuMsBaseline`, `metrics`, `memory` — the
   things `--bless` writes) lives in `tests/goldens/manifest.json`, not
   under a literal `benchmarks/` directory (that directory holds Pi-run
   captures/contact-sheets/audits from other units, a different thing).
   T69's own Territory/edits touched the same file under the same
   description; following that precedent rather than treating the wording
   literally.
5. **No production memory *behavior* was changed anywhere.** Every edit
   either (a) fixes a benchmark-only accounting bug (`eqe1MappedBytes`), (b)
   adds a second, separate CI-only benchmark invocation and its Python
   glue, or (c) adds manifest/board bookkeeping. No new caches, no runtime
   budget enforcement, no eviction changes, no rendering-behavior changes —
   confirmed by inspection, consistent with the plan's explicit
   prohibitions and STOP conditions (none of which were triggered: the
   golden images did not move, no rendering behavior needed to change, and
   no newly-covered category proved non-deterministic).

Board (`refactor/deferred.md`, appended within the existing "Memory
categories with no configured budget" entry per the plan's explicit
instruction to update that entry, nothing deleted): a "T80 update"
paragraph block distinguishes *exercised by the gate* (now true for
`eqe1MappedBytes`/`eqe1ArchiveCount`/`placeableObjectsBytes`/
`characterModelsBytes`, per this unit) from *has a configured budget*
(still false for every bucket-(b) category, unchanged by this unit — a
separate, still-open Will decision), and records the `eqe1MappedBytes` bug
fix.
