# J22 — Lift the zone config table out of detail_manager.cpp (one cut clears the file)

**Slot: slot3. Branch `task/J22-detail-manager-split` cut from `fable`.
x86 only. No Pi. No eqemu.**

Wave 6, and the cheapest unit in the batch: `src/client/graphics/detail/detail_manager.cpp`
is **1,754 lines** and **`DetailManager::createDefaultConfig()` at `:1203` is a
~548-line data table** — a per-zone list of foliage/detail settings. Lifting that
one method into its own TU clears the file in a single cut, with no cluster
analysis needed.

**MOVE-ONLY.** The method moves verbatim, signature unchanged, still a
`DetailManager` member — this is a file move, **not** a conversion to a free
function, a data file, or a lookup table. **No method decomposition** (that is
Batch G's shape and is out of scope for every Batch J unit).

## The cut

| range | goes to | ≈ lines |
|---|---|---|
| `createDefaultConfig` (from its leading banner/comment through its closing brace, around `1203`-`1751`) | **`src/client/graphics/detail/detail_zone_configs.cpp`** (new) | ~600 incl. preamble |
| everything else | **stays** `detail_manager.cpp` | ~1,210 |

⚠ **Re-derive both boundaries at your tip** — twenty-three units merged today
and any line number here is indicative only. Confirm the real end of the method
rather than trusting `1751`.

⚠ **~1,210 lines still leaves the stays-file over the 1,500 soft limit? No — it
does not, but CHECK.** If your measured split leaves either file over 1,500, say
so and propose the second cut rather than declaring victory; a split that leaves
an oversize file is not done.

## Standing rules — each has caught a unit today

- **`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo /mnt/projects/fable/wt/slot3`**, run **after committing**. Three ways it lies: no `--repo` (verifies `main/`), wrong base rev (shows another unit's work as removals), uncommitted tree (prints "No C++ files changed", **exits 0**).
- **Correction 21:** confirm every file-local helper the method uses is in the SAME range as its consumer. `createDefaultConfig` is a data table and may reference file-scope constants — **if it does, they must travel with it or be promoted**; J08's plan split a helper from its only caller and would not have compiled. Paste the grep.
- **Correction 26:** `grep -rn` across `tests/` for add-sites — **never name `tests/CMakeLists.txt`**, which J20 split into `tests/cmake/*.cmake` fragments. The root site is `CMakeLists.txt:548` (verify; CMake commands here are **UPPERCASE**, so `grep -i`).
- Run `grep -rn 'detail_manager' tests/ scripts/` before cutting — a test that reads a source file as *text* joins your territory (J11 and J13 each found one; one would have silently asserted nothing).
- Cut at a blank line whose predecessor closes a method; verify comment depth 0.
- **Both proofs**: the verifier, plus a byte-identical re-concatenation / line-multiset check.
- After a build that **exits 143** (different from a slow build the tool backgrounded), scan every binary for valid **ELF magic — not size**: today's truncated binaries were 21–377 MB with all-zero headers, four separate instances.
- A visual-gate failure on the **CPU p95 metric with zero image mismatches** is wave contention — re-run it. ⚠ **This file drives foliage rendering, so a genuine image mismatch is a real risk here** — if pixels move, STOP and report rather than re-blessing.
- `chmod 644` created files. Stale `tests/` line-number citations are pre-existing doc rot owned by J21 — note and move on.
- The scratchpad is shared across slots: `rm -f` your exit file before launching, and a `wait_for.sh` MET after 0 seconds is a stale file, not a fast build.

## Acceptance

Build by exit code · `ctest -j6` (**NOT -j24**) with zero new failures vs
`refactor/test_baseline.txt` · both proofs · **visual gate PASS with goldens
untouched** · `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` ·
`check_file_sizes.sh --files` **0 warn, 0 fail** on both files.

## Territory

`src/client/graphics/detail/`, `CMakeLists.txt`, this file. Concurrent: slot1
J13 (`eq_bridge.cpp` + a 3-line repoint in `tests/test_reachability_sweep.cpp`)
and slot2 T323 (the CRC flag, live server).

## Review

**Base tip:** `cb32a38e5f91c4748cbb8b5187eecf2ae2dc9f94` (fable, "J22 plan: lift the
548-line zone config table out of detail_manager.cpp").
**Commit:** `a7657a99bfd6255c1a72c033beabaebfc1a61738` on `task/J22-detail-manager-split`.

### One cut sufficed

| file | before | after |
|---|---|---|
| `src/client/graphics/detail/detail_manager.cpp` | 1,754 | **1,205** |
| `src/client/graphics/detail/detail_zone_configs.cpp` | — | **568** |

Both under the 1,500 soft limit; no second cut needed.
`check_file_sizes.sh --files` on both: `2 ok, 0 warn, 0 fail`.

### Boundaries re-derived at the tip

`DetailManager::createDefaultConfig()` spans **1203..1750** at `cb32a38e` (the plan's
indicative `1203`-`1751` was one line long: 1751 is the blank separator, 1752-1754 the
three namespace closers). Line 1202 was the blank separator after
`isExcludedByBsp()`'s closing brace at 1201 — comment depth 0, predecessor closes a
method, so the cut is `1202..1750` deleted from the stays-file and `1203..1750` moved.

### Correction 21 — no file-scope dependencies travel

`detail_manager.cpp` declares **nothing** at namespace scope other than
`DetailManager` members: no anonymous namespace, no file-scope constants, no free
helpers. The grep, run on the pre-cut file:

```
$ grep -n 'namespace {\|^static\|^ *static \|^constexpr\|^const ' detail_manager.cpp
408:const FoliageDisturbanceConfig& DetailManager::getFoliageDisturbanceConfig() const {
442:const FootprintConfig& DetailManager::getFootprintConfig() const {
669:            static bool loggedZoneMeshStatus = false;
720:    static bool loggedMeshInfo = false;
813:                        static int meshTexLogCount = 0;
827:    static int noMatchLogCount = 0;
857:    static bool loggedCoords = false;
880:    static int debugCount = 0;
920:            static int logCount = 0;
975:    static int classifyLogCount = 0;
```

Every hit is either a member-function return type or a function-local `static` inside
a method that STAYS — none is in 1203..1750 and none is referenced from it.

Every symbol the moved range names:

```
$ grep -on '[A-Za-z_][A-Za-z0-9_]*::\|std::[a-z_]*\|[a-zA-Z_][A-Za-z0-9_]*(' <moved range> | sort -u
AtlasTile::  createDefaultConfig(  DetailCategory::  DetailManager::
DetailOrientation::  getTileUV(  irr::  push_back(  SColor(  std::string
SurfaceType::  video::
```

`getTileUV()` and `AtlasTile` are both in
`include/client/graphics/detail/detail_texture_atlas.h` (`:85` inline, `:18` enum);
everything else comes via `detail_manager.h`. The new TU therefore includes exactly
`detail_manager.h`, `detail_texture_atlas.h` and `<string>`.

### Both proofs

1. **Verifier** (after committing, with `--repo`):
   `python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo /mnt/projects/fable/wt/slot3`
   → `Files verified: 2 · Boilerplate: +15 / -0 lines (allowed) · PASS: move-only (modulo boilerplate).`
   (`CMakeLists.txt` reported as a non-C++ change: the single add-site, see below.)
2. **Byte-identical re-concatenation:** `head -1201 detail_manager.cpp` + one blank
   line + `detail_zone_configs.cpp` lines 17..564 (i.e. minus the 16-line preamble and
   the 4-line trailer) + `tail -4 detail_manager.cpp` → `cmp` exit 0 against
   `git show HEAD~:...detail_manager.cpp`; md5 `d3883ec9d90043b164287d96e8600168` both
   sides. Not just a line multiset — the reconstruction is byte-for-byte the original.

### Add-sites (Correction 26)

`grep -rn -i 'detail_manager' tests/ scripts/` → **no hits at all**; no test reads the
file as text. The only build-system reference in the entire tree is
`CMakeLists.txt:548` (confirmed by `grep -rn --include=*.txt --include=*.cmake -i
detail_manager .`, which finds nothing under `tests/cmake/`). New TU inserted at
`:549`, next to its sibling.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** (no exit 143, so no ELF-magic sweep needed); `detail_zone_configs.cpp.o` present in the log |
| `ctest -j6 --output-on-failure` | 2 failures, **0 new** — see below |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, EXIT=0, all six scenarios, **zero image mismatches**, goldens untouched (`git status --short` empty after the run) |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `check_file_sizes.sh --files` | 2 ok, 0 warn, 0 fail |

**ctest failures:** `AudioManagerTest.PreloadCommonSounds` is the documented ALSA host
quirk in `refactor/test_baseline.txt`.
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` (#2144) is **not**
in the baseline; it **passes in isolation** (`Passed 5.06 sec`) immediately after the
suite run, on the same binaries. It is a timing-sensitive held-command test and this
was a `-j6` run concurrent with slot1's and slot2's waves — load-dependent flake, not
a regression. Nothing in this unit touches the headless shutdown path (the diff is a
foliage data table moving between two TUs, byte-identical).

**Foliage-render risk did not materialise:** the visual gate's image comparison passed
on all three zones plus the three memory-coverage variants, so no pixels moved. No
re-bless was needed or attempted.

### What the template still gets wrong

- The plan's `1751` end-of-method line was off by one (real end `1750`); the "re-derive
  at your tip" warning covered it, but note the *pattern*: the plan counted the blank
  separator into the method. Worth having plans quote the closing brace line, not the
  blank after it.
- **`detail_zone_configs.cpp` had to name three namespace closers plus a three-line
  namespace opener** — 20 lines of preamble/trailer for a 548-line body. The verifier
  counts 15 of those as boilerplate and is happy; the byte-identical proof needs the
  preamble/trailer line counts computed, not assumed (I derived them from `wc -l`
  rather than counting the heredoc by eye — counting by eye gives 15 and 4, which is
  wrong by one on the preamble).
- **Unrelated but in the same directory:** `src/client/graphics/detail/detail_texture_atlas.cpp`
  is **1,826 lines** — the largest file in `detail/` and *above* the file that J22 was
  written to fix. It was not in J22's scope and is untouched, but it is a bigger
  offender than the unit's own target and should be boarded for a future Batch J unit.
