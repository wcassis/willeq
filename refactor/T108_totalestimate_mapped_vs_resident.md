# T108 — `totalEstimate` counts mapped bytes as if they were resident

**Slot: slot1. Headless — the memory gate runs `gles2_zone_benchmark`, which
needs no server. Do not take the eqemu lock; T109 wants it.**

**Territory: `src/client/graphics/renderer_diagnostics.cpp`,
`scripts/ci/visual_gate.py`, `tests/goldens/` memory baselines, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T108/`.**

Read T105's, T106's and T107's Reviews first — they establish the numbers this
fix rests on.

## The defect

`renderer_diagnostics.cpp:646`:

```cpp
b.totalEstimate += b.eqe1MappedBytes;
```

`eqe1MappedBytes` is **mapped** — address space. T105 measured the EQE1
archives at **1.6-10.6% resident**; `lgequip.eqe1` is 23.6MB mapped and
**64KB resident**. So `totalEstimate` credits ~120MB as accounted-for when
roughly 110MB of it is not resident at all.

`untrackedBytes` (`:809`) is the residual against that inflated total, which
is why it read a comfortable "34-36%" when the true figure is **75-90% on
x86 and 32.8% on the Pi** (T106 corrected the arithmetic; T107 measured the
Pi).

**This is the blocker on blessing the memory-gate baselines.** Blessing them
as they stand would pin the error into the gate — and under the load-gate
design those budgets are meant to become a runtime control surface.

## The fix — decide between two shapes, and say why

**Option A: measure resident.** Read `/proc/self/smaps` for the EQE1 mappings
and sum `Rss`. Accurate, but costs a `/proc` parse at report time and needs
care about when it runs.

**Option B: stop counting it.** Remove `eqe1MappedBytes` from `totalEstimate`
and report it separately as an explicitly-mapped-not-resident figure.
Cheaper, honest, and makes `untrackedBytes` mean what it claims.

T105, T106 and T107 all used smaps parsing already; tooling exists in
`/mnt/projects/fable/review/T105/` and `T106/`. **Pick one, implement it, and
state the reasoning.** If you choose A, keep the mapped figure too — it is
useful, it just is not RSS.

## The baselines WILL move — that is the point

`scripts/ci/visual_gate.py`'s `MEMORY_BASELINE_FIELDS` asserts `totalEstimate`
and `eqe1MappedBytes` bit-identically across three golden zones. **This fix
changes `totalEstimate` by roughly 110MB.** The gate will fail until baselines
are regenerated.

That is expected and correct. **Regenerate them, and state the before/after
per zone explicitly** — the delta is the size of the error being removed, and
it is the number Will needs in order to bless.

Do **not** loosen the gate's zero-tolerance comparison to make it pass. The
comment at `visual_gate.py:95-98` is explicit that non-identical values are
"real news, not noise."

## Also fix, if it is genuinely small

T105 and T106 both found `/pmem`'s real RSS and untracked fields **unreachable
in a live client** — the populating code path has zero callers, so both units
had to work around it via the standalone benchmark. T106 additionally found
Audio counters that exist and are never reached for the same reason.

If wiring that up is small and clearly scoped, do it — a memory diagnostic
nobody can invoke live is not much of a diagnostic. **If it is not small, stop
and report**, and leave it in `deferred.md`.

## Tests

- A test pinning that `totalEstimate` does not include non-resident mapped
  bytes, whichever option you chose.
- **Prove it can fail in both directions** (the T95 standard): reintroduce the
  mapped-bytes credit, show red; restore, show green. Report that you did it.

## What you may not do

- Do not take the eqemu lock — T109 needs it and this unit does not.
- Do not change what is measured, only how it is accounted. No new categories.
- Do not loosen the gate's tolerance.
- **STOP and report** if correcting the arithmetic reveals a second
  double-count. Two errors compounding is a different finding from one.

## Verification

Full build and suite (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual gate must PASS
after baseline regeneration, with the **image** goldens untouched — only the
memory baselines should move. If an image golden moves, that is a real signal:
stop and say so.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T108-totalestimate`,
base fable tip `9e674bd3`. Headless throughout — the eqemu lock was never
taken (`gles2_zone_benchmark` needs no server; T109 owned eqemu/Pi in slot2).

### Option chosen: B — stop counting, don't measure resident

Removed `b.totalEstimate += b.eqe1MappedBytes;` (renderer_diagnostics.cpp:646)
rather than parsing `/proc/self/smaps` to compute a resident figure (Option
A). Reasoning: `eqe1MappedBytes` is still reported on its own line (now
labeled "not counted in Tracked total below, ... typically ~2-11%
resident"), so no visibility is lost; `untrackedBytes` is computed against
`processRssBytes`, which is *already* a real, correctly-measured RSS number
supplied by the caller — Option A would add a second, redundant resident
measurement (parsed at report time, with its own timing/staleness
questions the plan itself flagged) to recover a number the residual
(`RSS - totalEstimate`) already reflects correctly once the mapped-bytes
credit is removed. Option B is the honest, minimal fix; Option A would be
solving a problem this fix already dissolves.

### A second implementation had the identical bug — fixed, not a new/second double-count

`scripts/ci/visual_gate.py`'s memory gate does not call
`IrrlichtRenderer::getMemoryReportBreakdown()` directly — `gles2_zone_benchmark`
never constructs a full `IrrlichtRenderer` (documented in
`tools/hwcaps/zone_bench_memory.cpp`'s header comment: a deliberate,
disclosed, category-by-category duplicate, not the "two tallies over the
same object graph" pattern T57/T60 flagged). That duplicate,
`computeMemorySnapshot()`, had **the same** `snap.totalEstimate +=
snap.eqe1MappedBytes;` line (zone_bench_memory.cpp:166) — same conceptual
defect, not a second, different, compounding error, so this did not trigger
the plan's STOP condition. Fixed both. Without fixing the benchmark copy,
the fix would never have reached the CI gate the plan is written to
protect, since that gate reads only the benchmark's JSON output.
**No other double-count was found** — `check_file_sizes.sh`, the full
suite, and the arithmetic cross-check below (`totalEstimate` == sum of every
other emitted category, exactly) all confirm nothing else is compounding.

### Before/after `totalEstimate` per zone, and why the gate needed no re-bless

The plan expected the golden-zone baselines to move by ~110MB and require
regeneration. **They did not need to move, for two compounding reasons**,
both confirmed by inspection before touching anything:

1. The three golden-image zones (qeynos2/gfaydark/befallen) never pass
   `--eqe1-path` — `eqe1MappedBytes` is 0 there with or without this fix.
2. The committed `tests/goldens/manifest.json` does not actually carry a
   `totalEstimate`/`eqe1MappedBytes` baseline for those zones at all — only
   `textureCacheBytesUsed`/`textureCacheBytesLimit`/`textureCount` are
   blessed in `zones[].memory` (confirmed by direct inspection and by
   `git log -- tests/goldens/manifest.json`: T69's commits, which wired the
   mechanism, never touched this file). `verify()`'s `MEMORY_BASELINE_FIELDS`
   loop skips any key not present in the blessed dict, so this was always a
   silent no-op for these fields on the golden-zone path.

So `bash scripts/ci/visual_gate.py` (verify mode) passes **identically**
before and after this fix, with the golden zones' `tracked=`/`RSS=` print
lines bit-for-bit unchanged (`textureCache*` fields, the only ones actually
asserted, are untouched by this change) — confirmed by running it before
any code edit and again after. **No `--bless` was run**; there is nothing
blessed for these fields to regenerate, and re-blessing would mean
originating a brand-new baseline for the first time, which is Will's call
(same "ships unblessed pending sign-off" discipline T80 already established
for the sibling `memoryCoverage` section — see below).

The ~110-120MB the plan anticipated **does show up**, exactly where EQE1 is
actually exercised: the T80 `memoryCoverage` run (`--eqe1-path`/`--objects`/
`--entities`). That section already ships **unblessed pending Will's
sign-off** (`refactor/T80_memory_gate_coverage.md`); this unit did not
change that (same self-bless prohibition), but measured the corrected
numbers for when that sign-off happens:

| Zone | totalEstimate BEFORE | totalEstimate AFTER | Δ | eqe1MappedBytes (unchanged) |
|---|---|---|---|---|
| qeynos2  | 172,352,354 (164.4 MB) | 52,408,963 (50.0 MB) | **-119,943,391 (-114.4 MB)** | 119,943,391 |
| gfaydark | 183,871,942 (175.3 MB) | 65,262,459 (62.2 MB) | **-118,609,483 (-113.1 MB)** | 118,609,483 |
| befallen | 159,450,602 (152.1 MB) | 46,356,623 (44.2 MB) | **-113,093,979 (-107.8 MB)** | 113,093,979 |

Every delta equals that zone's `eqe1MappedBytes` exactly — the fix removes
precisely the mapped-not-resident credit and nothing else. `untrackedBytes`
(processRssBytes − totalEstimate) grew by the identical amount in each row,
which is the intended effect: it now reflects what's really unaccounted.
Raw JSON captures (before and after) are in `/mnt/projects/fable/review/T108/`.
(`data/eqe1_client` — a gitignored local symlink to `/mnt/projects/EverQuest_ETC1`,
same convention as `data/eq_client` — was created in this worktree to
exercise the coverage run at all; it did not exist here before this unit and
is not committed.)

### `/pmem`'s live reachability — fixed, not deferred

T105/T106 found `EverQuest::runPmemDiagnostics()` (real RSS from
`/proc/self/statm`, real audio/entity/door/spellDb data, pushed via the
`DiagnosticsMemoryReport` bridge event already fully handled in
`irrlicht_bridge.cpp`) had **zero callers** — its only caller,
`runLoadDiagnostics()`, also had zero callers. Meanwhile the live `/pmem`
slash command was wired entirely differently: the command registry routed
it through `rendererCmd("pmem")`, a `RendererCommand` bridge event handled
inside `IrrlichtRenderer::processSlashCommand`'s own `"/pmem"` branch, which
built an **empty** `MemoryReportInput{}` and could never see any of that
real data. This was small and clearly scoped (three files, ~35 net lines):
- `eq_commands_spells.cpp`: `pmem.handler` now calls `runPmemDiagnostics()`
  directly (game thread) instead of routing through `rendererCmd("pmem")`;
  headless builds keep the same "requires graphics mode" message.
- `irrlicht_bridge.cpp`: the `DiagnosticsMemoryReport` handler now also
  calls `renderer_->newUIChatBuffer_->addSystemMessage(line)` per report
  line (previously `LOG_INFO` only), preserving the in-game chat visibility
  the old (empty) handler had.
- `irrlicht_renderer.cpp`: deleted the now-unreachable empty-`MemoryReportInput{}`
  `"/pmem"` branch in `processSlashCommand` — confirmed unreachable first
  (`processSlashCommand` has exactly one call site, the `RendererCommand`
  bridge case, and nothing pushes `"/pmem"` through it anymore).

This also closes T106's audio-reachability finding as a side effect (same
`ext.audioAvailable` gate, now reached through the same real path). Not
independently re-verified live in-game this unit (headless, no eqemu lock
taken per the plan's explicit prohibition) — verified by build/link success,
by tracing `processSlashCommand`'s single call site to confirm the deletion
is safe, and by code inspection of the now-connected data flow
(`runPmemDiagnostics()` → `DiagnosticsMemoryReport` event → the bridge
handler that already existed and was already fully correct, per T105/T106).

### Proof the new test fails in both directions (T95 standard)

New test: `scripts/ci/test_eqe1_totalestimate_accounting.py`, registered as
ctest `test_eqe1_totalestimate_accounting` (`tests/CMakeLists.txt`, SKIPs
with code 125 if the benchmark isn't built or no EQE1 data is present,
matching `visual_gate`'s existing convention). It runs the real, already-built
`gles2_zone_benchmark` against qeynos2 with `--eqe1-path`, and asserts
`totalEstimate` equals the sum of every other tallied category (i.e.
`eqe1MappedBytes` is excluded).

- **With the fix in place**: `ctest -R test_eqe1_totalestimate_accounting` →
  **PASS** (`totalEstimate` = sum of tracked categories exactly).
- **Bug reintroduced** (temporarily restored
  `snap.totalEstimate += snap.eqe1MappedBytes;` in `zone_bench_memory.cpp`,
  rebuilt just that target): **FAIL**, with the diagnostic printing
  `diff = 119,943,391 bytes` — exactly `eqe1MappedBytes` for that run,
  confirming the test catches precisely this regression, not some other
  arithmetic mismatch.
- **Fix restored**, rebuilt: **PASS** again.

Both directions demonstrated and are reproducible from the commit history
of this session (the revert-and-restore was done in-place, not committed
as a separate broken commit).

### No other double-count found

`check_file_sizes.sh fable` shows no new FAILs (one file crossed 3000 lines
by +1 from an initial comment; trimmed the comment to net -4 lines vs base,
now 2996 — see Deviations). Full suite and visual gate both green (below).
Nothing else in `renderer_diagnostics.cpp`'s ~15 other `totalEstimate +=`
sites changed or needed to.

### Verification

1. `cmake --build build -j24` — PASS.
2. `ctest --test-dir build --output-on-failure` — **2247/2248 passed**
   (2246/2247 baseline + 1 new test), sole failure the documented
   pre-existing ALSA `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — PASS,
   `build-gles2/bin/{willeq,gles2_zone_benchmark}` rebuilt cleanly.
4. `python3 scripts/ci/visual_gate.py` (verify) — **PASS**, image goldens
   and all asserted metrics/memory fields untouched; the (already-unblessed)
   `memoryCoverage` section reports "NOT YET BLESSED" as before, unchanged.
5. `bash scripts/build-arm-noble.sh` — PASS, ARM cross-build succeeded.
6. `scripts/refactor/check_file_sizes.sh fable` — 4 ok, 2 warn
   (`irrlicht_renderer.h` pre-existing WARN at 2154 lines, `irrlicht_renderer.cpp`
   now WARN at 2996 lines having crossed the 1500-line WARN threshold long
   ago; **0 FAIL**, confirmed the file's FAIL ceiling of 3000 is not
   exceeded after trimming).

### Deviations from the plan

- **No baseline regeneration/`--bless` was performed**, contrary to the
  plan's expectation that one would be needed — see "Before/after" above
  for why (golden zones never exercise EQE1; the relevant fields were never
  actually blessed in the committed manifest to begin with). Flagged rather
  than silently following the plan's assumption; the corrected numbers
  Will needs for the T80 `memoryCoverage` sign-off are reported above
  instead of committed as a bless.
- **Fixed a second, textually-identical instance of the bug**
  (`tools/hwcaps/zone_bench_memory.cpp`) beyond the plan's single named
  file. Judged in-scope (same defect, not a compounding second error) per
  the "STOP if a *second double-count* surfaces" wording — this is one
  double-count present in two call sites, not two independent errors.
  Flagged explicitly rather than silently expanding scope.
- **`/pmem` live-reachability fix**: the plan allowed this only "if small
  and clearly scoped." Done — three files, ~35 net lines, described above.
- **`irrlicht_renderer.cpp` briefly exceeded the file-size FAIL ceiling**
  (3001 vs 3000, the file was already sitting exactly at the boundary
  before this unit) after the first version of a doc comment; trimmed to a
  3-line comment (net -4 vs base) before finishing, confirmed 0 FAIL by
  `check_file_sizes.sh`.
- **Created `data/eqe1_client`** (gitignored local symlink to
  `/mnt/projects/EverQuest_ETC1`, mirroring `data/eq_client`'s existing
  convention) — did not exist in this worktree before this unit. Needed to
  exercise the T80 coverage run and the new regression test's real EQE1
  data at all. Left in place (gitignored, harmless, matches an established
  convention) rather than removed, so the new test doesn't SKIP on this
  worktree going forward.

## Board

Record that the memory baselines are now regenerated and correct, so the bless
can proceed on numbers that mean what they say.
