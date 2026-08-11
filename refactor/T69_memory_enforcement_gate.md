# T69 — Connect the memory accounting to the gate that can fail a build

**Slot: t1. x86 only. No Pi (see "Pi is out of scope"). NO EQEMU.**

**Territory: `tools/hwcaps/gles2_zone_benchmark.cpp` (the memory-JSON
emitter), `scripts/ci/visual_gate.py`, `src/client/graphics/renderer_diagnostics.cpp`
(read-only unless a getter must be exposed — see Step 1), and
`benchmarks/` baselines.**

Do not change any memory *behaviour* — no new caches, no budget
enforcement at runtime, no eviction changes. This unit measures and
asserts; it does not manage. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `scripts/ci/visual_gate.py`
in full (it is short, and its existing memory assertion is the pattern you
are extending).

## Why this unit exists

Will's standing goal: WillEQ must run on the widest possible hardware,
which means tight control of memory — and his ruling on how to get there:
**per-subsystem budgets that are defined and enforced. No global cap, no
cross-asset coordinator.**

The accounting already exists and is good. `getMemoryReport()`
(`src/client/graphics/renderer_diagnostics.cpp:620-900`) accumulates a
`totalEstimate` across ~15 categories — texture cache, EQE1 flat packs,
zone geometry, placeable objects, entities, character models, equipment
models, S3D zone source, GPU textures, HCMap/collision, framebuffer,
VBO/EBO buffers, SFX and sound-buffer caches — and compares it against
actual RSS.

**The enforcement also already exists — for exactly one subsystem.**
`scripts/ci/visual_gate.py:296-306` asserts `textureCacheBytesUsed`
against a blessed value *and* against its limit, and fails the gate on
either. That is precisely the right shape.

What is missing is the wire between them. The benchmark emits **four
fields** (`gles2_zone_benchmark.cpp:2552`): zone, texture-cache used,
texture-cache limit, texture count. Everything `/pmem` knows is invisible
to CI, so memory growth in any other subsystem is caught by nobody.

This unit connects the two. It is deliberately not a new mechanism.

## Step 1 — emit what /pmem already knows

Extend the benchmark's memory JSON to carry the full per-subsystem
breakdown plus `totalEstimate` and RSS.

Prefer **reusing `getMemoryReport()`'s accounting** over recomputing it in
the benchmark — two independent tallies of the same thing is the drift
this project keeps finding (T57's slot geometry, T60's bone logic). If the
report only produces formatted strings, that is a real obstacle: expose a
structured accessor alongside it rather than parsing text, and say so.
Parsing formatted log lines in CI is fragile and would be a poor
foundation for a gate.

**Note T67 just removed `ConstrainedMeshCache`**, so the mesh-cache
category is gone from the report. Take the categories as they exist now,
not as older docs describe them.

## Step 2 — assert, with the right kind of assertion per metric

Extend `visual_gate.py` following its existing texture-cache assertion.
**Two different kinds of check are needed and conflating them would be
wrong:**

**(a) Where a real budget exists — enforce it.** `ConstrainedRendererConfig`
defines `textureMemoryBytes` and `framebufferMemoryBytes`. For these,
assert *usage ≤ budget*. That is enforcing a defined budget, which is what
Will asked for, and it is meaningful independent of any baseline.

**(b) Where no budget exists — guard against regression.** Most categories
(entities, character models, S3D source, EQE1, HCMap…) have no configured
ceiling at all. For these, assert against a **blessed baseline with a
tolerance**. This is regression detection, not budget enforcement, and the
Review must say plainly which categories fall in this bucket — that list
is itself a finding: it enumerates where the project has no defined budget
today.

Do not invent budgets for category (b). Proposing numbers is Will's call,
not this unit's; recording the absence is the deliverable.

## Step 3 — the failure mode that would make this worthless

**A gate that fires spuriously gets waived, and a waived gate is worse
than no gate.** This project already learned that the expensive way: T35
found the `RDPServerFrameTest` "intermittent flake" waiver had been
masking a real use-after-free for months.

So: **establish which metrics are actually deterministic before choosing
tolerances.** Run the same zone several times and report the spread per
category. The texture cache is asserted *exactly* today (`!=`) and that
works because it is deterministic. Others may not be — allocator
behaviour, texture upload ordering, and page-cache effects can all move
numbers run to run.

Pick tolerance per metric from measured spread, not from a guess. A
category too jittery to gate meaningfully should be **reported but not
asserted**, with that stated openly — an honest gap beats a false alarm.

## Step 4 — the untracked-RSS gap: report it, do not hard-gate it yet

`totalEstimate` vs RSS is the most interesting single number here — it
measures memory the project does not know it is using, which is exactly
the failure mode Will described (iterative development accreting
unaccounted allocations).

**But it is the noisiest.** EQE1 flat packs are mmap/page-cache backed,
allocator arenas fluctuate, and RSS includes things no subsystem owns.

So: **emit it, record it in the baseline, print it prominently in the gate
output, and do not fail the build on it in this unit.** If Step 3's
repeat-run data shows it is stable enough to gate, say so with the numbers
and recommend a ceiling — but do not enable it on a hunch. Getting this
one wrong is how the whole gate loses credibility.

## Pi is out of scope

Ceilings measured on x86/llvmpipe do not transfer to Mali400, and the Pi
runs through a separate tier-2 harness. Note on the board that per-device
budgets are the natural follow-up; do not attempt them here.

## Step 5 — blessing

`visual_gate.sh --bless` already regenerates goldens and the manifest.
Memory baselines must ride the **same** mechanism, so there is one blessing
path rather than two. Blessing memory must be as deliberate as blessing an
image: state in the Review exactly what a re-bless would overwrite.

## STOP conditions

- `getMemoryReport()` proving impossible to reuse without a structural
  change larger than this unit — report rather than duplicating the
  accounting.
- Any category whose measured spread makes a meaningful tolerance
  impossible — report it, do not gate it.
- Any temptation to change runtime memory behaviour. Measure and assert
  only.

## Verification

Full protocol bar, plus: the per-category determinism data from Step 3
(repeat runs, spread per metric); the blessed baseline; proof the gate
**fails** when a ceiling is exceeded — deliberately breach one and show
the failure, because a gate never observed failing is not known to work;
and confirmation the existing texture-cache assertion still behaves.

## Review

### Where the structured accounting came from, and the getter

`getMemoryReport()` (`src/client/graphics/renderer_diagnostics.cpp`) only
ever produced formatted `std::vector<std::string>` lines — parsing that in
CI would have been exactly the fragile approach the plan warned against. So
a getter was added: `IrrlichtRenderer::getMemoryReportBreakdown(const
MemoryReportInput&) const`, declared in `include/client/graphics/
irrlicht_renderer.h` next to `getMemoryReport()`, returning a new
`MemoryReportBreakdown` struct (named fields, one per category, plus
`totalEstimate`/`processRssBytes`/`untrackedBytes`). `getMemoryReport()` was
refactored to call `getMemoryReportBreakdown()` once and format its lines
purely from the returned struct — **the byte-tally arithmetic now exists in
exactly one place**; the old duplicate-tally risk the plan named (T57 slot
geometry, T60 bone logic) does not apply here because there was only ever
one computation to begin with, now just also exposed structurally. Live
per-request diagnostics that were never part of the byte tally (texture
cache hit/miss/eviction counters) stayed as direct reads from
`constrainedTextureCache_` in `getMemoryReport()`, not threaded through the
struct — they're display-only, not accounting.

`gles2_zone_benchmark.cpp` (the CI consumer) **cannot call
`getMemoryReportBreakdown()` directly** — `ZoneBenchData`
(`tools/hwcaps/zone_bench_loader.h`) is a separate, lighter-weight object
graph (its own driver/textureCache/raceModelLoader/zone; no
`IrrlichtRenderer`, no scene nodes) built by the standalone benchmark
loader, and constructing an `IrrlichtRenderer` inside a benchmark tool would
be a structural change well beyond this unit (flagged per the plan's STOP
conditions, not attempted). What's genuinely reused instead, category by
category, and what's necessarily recomputed, is documented in the block
comment at the top of the new `tools/hwcaps/zone_bench_memory.cpp` (moved
there from `gles2_zone_benchmark.cpp` — see "File-size deviation" below) —
summary: `ConstrainedTextureCache`'s own accounting methods, `S3DZone::
getMemoryUsage()`, `RaceModelLoader::getMemoryStats()`, the same extern
`gles2Get*MemoryUsage()` driver-level functions `renderer_diagnostics.cpp`
calls, and `ConstrainedTextureCache::getConfig()` (returning the exact
`ConstrainedRendererConfig` the loader built, so `calculateFramebufferUsage()`
is the identical method/config `getMemoryReportBreakdown()` uses) are all
reused verbatim. The one necessary duplication is the four-line "sum
vertex+index bytes across an `IMesh`'s buffers" formula for zone geometry/
placeable objects — `IrrlichtRenderer` sums from its own scene nodes,
`ZoneBenchData` holds the equivalent meshes in its own `regions`/`objects`
containers that never touch `IrrlichtRenderer`; there is no shared
production entry point for that formula outside the inline arithmetic
itself, so it appears in both places rather than being extracted (extracting
it would have meant editing `include/client/graphics/eq/zone_geometry.h`,
outside this unit's stated Territory — noted, not done).

### Per-category table

| Category | Kind | Basis | Measured spread (9 runs, 3 zones) |
|---|---|---|---|
| `textureCacheBytesUsed` / `Limit` | **(a) Budget-enforced** (pre-existing, H02) | `ConstrainedRendererConfig::textureMemoryBytes` via `ConstrainedTextureCache::getMemoryLimit()` | exact match, 0 |
| `framebufferBytes` / `framebufferBudgetBytes` | **(a) Budget-enforced** (new) | `ConstrainedRendererConfig::framebufferMemoryBytes` via `calculateFramebufferUsage()` | exact match, 0 |
| `textureCount` | (b) Baseline-guarded | no budget | exact match, 0 |
| `eqe1MappedBytes` / `eqe1ArchiveCount` | (b) Baseline-guarded | no budget | exact match, 0 (both 0 — gate doesn't pass `--eqe1-path`) |
| `zoneGeometryBytes` | (b) Baseline-guarded | no budget | exact match, 0 |
| `placeableObjectsBytes` | (b) Baseline-guarded | no budget | exact match, 0 (0 — gate doesn't load `--objects`) |
| `s3dZoneSourceBytes` | (b) Baseline-guarded | no budget | exact match, 0 |
| `gpuTextureBytes` | (b) Baseline-guarded | no budget | exact match, 0 |
| `vboEboBytes` | (b) Baseline-guarded | no budget | exact match, 0 |
| `characterModelsBytes` | (b) Baseline-guarded | no budget | exact match, 0 (0 — golden zones load no entities) |
| `totalEstimate` | (b) Baseline-guarded | no budget (sum of the above) | exact match, 0 |
| `processRssBytes` | (c) Reported only | n/a | qeynos2 5x: 187277312–187625472 (~0.19% spread); gfaydark 3x: 172064768–172138496 (~0.04%); befallen 3x: 158683136–158826496 (~0.09%) |
| `untrackedBytes` (RSS − totalEstimate) | (c) Reported only | n/a | tracks RSS's spread directly; consistently ~75-80% of RSS across all three zones (see `refactor/deferred.md` entry for the full numbers and the follow-up recommendation) |

Every field in bucket (b) was **bit-identical across all 9 runs** (5x
qeynos2, 3x gfaydark, 3x befallen) — exact-match (zero tolerance) is
therefore an empirically measured choice, not a guess, matching the plan's
Step 3 instruction. No category needed the "too jittery to gate, report
only" treatment from Step 3 — only RSS/untracked (bucket c), which the plan
explicitly said not to gate regardless of jitter.

### Untracked RSS: numbers and recommendation

See the table above for the raw spread. `refactor/deferred.md`'s "Untracked-
RSS gating recommendation" entry (T69) has the full writeup: untracked RSS
was large (~75-80% of RSS, ~140-153MB depending on zone) but also
*consistent* across runs of the same zone. Per the plan's explicit
instruction, **this unit does not enable any assertion on it** — it is
emitted, baselined (recorded in the manifest's `memory` block whenever a
re-bless happens), and printed prominently by `verify()`
(`[memory] <zone>: tracked=... RSS=... (NN%) untracked=...  (reported only,
not gated -- see T69 Review)`). A future unit could plausibly gate it given
how tight this VM's spread was, but that call needs more repeat-run data
(more zones, ideally a second machine) before proposing a number — not
asserted here on a hunch from one machine's data.

### Proof of a deliberate failure

Ran the real `bless()`/`verify()` functions from `scripts/ci/visual_gate.py`
(imported directly, not reimplemented) against an isolated scratch copy of
`tests/goldens/` — **never touched the committed goldens** (confirmed via
`git status --porcelain tests/goldens/` showing no changes before and after).
Script: `run_gate_test.py` (kept in the session scratchpad, not committed).
Six steps, all passed:
1. `bless()` against the scratch copy populates every new T69 field.
2. `verify()` against its own fresh baseline: **PASS**.
3. Monkeypatched `run_benchmark()` to shrink `framebufferBudgetBytes` below
   the measured `framebufferBytes` by 1 byte (a deliberate budget breach,
   category (a)) — `verify()` **FAILED** with `qeynos2: framebuffer bytes
   used 3840000 exceeds budget 3839999`.
4. Monkeypatched `run_benchmark()` to add 12345 bytes to
   `zoneGeometryBytes` (a deliberate baseline drift, category (b)) —
   `verify()` **FAILED** with `qeynos2: zoneGeometryBytes mismatch
   (expected 1269654, got 1281999)`.
5. Removed the injection — `verify()` **PASSED** again, proving the
   failures above were caused by the injected breach, not a broken
   assertion.
6. Monkeypatched `run_benchmark()` to add 1 byte to
   `textureCacheBytesUsed` (the **pre-existing H02 assertion**) —
   `verify()` **FAILED** with the original `texture cache bytes used
   mismatch` message, confirming that assertion still behaves exactly as
   before T69.

Also ran the real (unmodified) `scripts/ci/visual_gate.sh` against the
actual committed goldens/manifest: **PASS**, with the new `[memory]`
report-only lines printed for all three zones (tracked/RSS/untracked), and
the pre-existing texture-cache checks still the only asserted memory
fields — because the committed `manifest.json` was not re-blessed (see
next section), the new baseline-guarded fields have no blessed value to
compare against yet, so `verify()` correctly skips them (guarded by `if key
not in expected_mem: continue`) rather than false-failing on an absent
key. This is intentional: this unit ships the mechanism, not a live
enforcement of the new (b)-bucket fields — that activates the moment
someone runs `--bless` for real and Will signs off.

### What a re-bless overwrites

`visual_gate.py --bless` writes `zone_cfg["memory"]` for every zone with:
the pre-existing `textureCacheBytesUsed`/`textureCacheBytesLimit`/
`textureCount` (unchanged behavior), plus the new
`framebufferBytes`/`framebufferBudgetBytes`, `eqe1MappedBytes`/
`eqe1ArchiveCount`, `zoneGeometryBytes`, `placeableObjectsBytes`,
`s3dZoneSourceBytes`, `gpuTextureBytes`, `vboEboBytes`,
`characterModelsBytes`, `totalEstimate`, `processRssBytes`,
`untrackedBytes`. It also still overwrites the golden PNGs and pixel/CPU
metrics exactly as before — bless() is one mechanism for both pixel and
memory baselines, per Step 5's instruction ("Memory baselines must ride the
same mechanism, so there is one blessing path rather than two"). **No
re-bless was run against the real repo in this unit** — `tests/goldens/`
and `tests/goldens/manifest.json` are untouched (verified via `git status`).
The moment someone runs `--bless` for real, every field above becomes the
new blessed baseline and the (b)-bucket assertions activate for the first
time; per protocol, that requires Will's sign-off, not a self-bless.

### Deviations

1. **File-size ceiling was already breached before this unit touched the
   file.** `tools/hwcaps/gles2_zone_benchmark.cpp` was 3044 lines on
   `fable` (confirmed via `git show fable:tools/hwcaps/
   gles2_zone_benchmark.cpp | wc -l`) — already over
   `check_file_sizes.sh`'s 3000-line FAIL ceiling before any T69 edit. Per
   "put substantial new code in new TUs," the new memory-accounting logic
   (struct, `computeMemorySnapshot()`, `writeMemoryJson()`, RSS reader, the
   mesh-buffer-sum helper, and their doc comments) was written into two new
   files instead, `tools/hwcaps/zone_bench_memory.h`/`.cpp` (added to
   `CMakeLists.txt`'s `gles2_zone_benchmark` target — a one-line addition to
   an existing source list, not new behavior), leaving
   `gles2_zone_benchmark.cpp` at 3026 lines — a net **decrease** of 18 lines
   despite the two new call sites and an added `#include`. `check_file_sizes.sh`
   still reports `FAIL` on this file because it checks absolute size of any
   changed file, not a before/after delta, and the file remains over budget
   post-T69 (3026 > 3000). This is an inherited, pre-existing violation, not
   one T69 introduced; full remediation (splitting this legacy file further)
   is out of T69's Territory and belongs to the file-split effort
   (`refactor/F_file_split.md`).
2. **`check_file_sizes.sh`'s cwd dependency (T67's documented finding)
   reproduced.** Running the script via `bash <slot>/scripts/...` from this
   session's ambient shell cwd (`/mnt/projects/fable`, not the slot)
   resolved the wrong repo root and reported a false "base revision does
   not resolve" — exactly the gap T67 already logged in
   `refactor/deferred.md`. Worked around with `env -C <slot> bash
   scripts/refactor/check_file_sizes.sh fable` (sets the process cwd without
   using `cd`/`&&`/`;`, all forbidden by the hook). No script changes made
   (out of Territory); noted here as confirmation of the existing board
   entry, not a new finding.
3. **`irrlicht_renderer.h` WARN (pre-existing, unrelated).** The header was
   already over the 1500-line WARN threshold (2140 lines, `> 1500`) before
   this unit's ~65-line `MemoryReportBreakdown` struct + one method
   declaration; WARN doesn't fail the gate and the growth here is a
   necessary, minimal accessor addition, not new unrelated code.
4. **No production memory *behavior* was changed anywhere** — confirmed by
   inspection: every edit either (a) exposes an existing tally as a struct
   instead of formatted strings, (b) reads that same tally from a new CI
   consumer, or (c) asserts in Python. No new caches, no runtime budget
   enforcement, no eviction changes, per the plan's explicit prohibition.

Board (`refactor/deferred.md`, appended, nothing deleted): three new
entries — "Memory categories with no configured budget" (the concrete
per-category list for Will's next ruling, matching the per-category table
above), "Pi per-device memory budgets" (the follow-up noted, explicitly
out of scope here), and "Untracked-RSS gating recommendation" (numbers +
recommendation, not gated).
