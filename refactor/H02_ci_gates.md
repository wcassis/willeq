# H02 — CI gates: golden frames, metric assertions, memory ceilings

Build the automated visual/performance/memory verification gate on top of
H01's x86 GLES2 build (`build-gles2/`, real `COpenGLES2Driver` under
llvmpipe via EGL surfaceless — no Xvfb even needed for the benchmark path;
verify whether the benchmark still wants an X display or runs fully
headless with the surfaceless fallback, and prefer fully headless).

## Design

1. **Golden set**: 3 zones × ~3 designated frames each along the fixed
   camera path, chosen for coverage: `qeynos2` (city/outdoor, proven in
   H01), `gfaydark` (forest/organic), and one dungeon interior (pick
   `blackburrow` or similar — confirm asset presence). Use the benchmark's
   `--dump-png` (H01) extended if needed to dump specific frame indices
   (e.g. `--dump-frames 0,30,59`).
2. **Golden storage**: `tests/goldens/<zone>_f<N>.png`, committed to the
   repo. Alongside: `tests/goldens/manifest.json` recording zone, frame
   index, benchmark flags, resolution, and the mesa/llvmpipe version string
   (from `glGetString(GL_VERSION)`/`GL_RENDERER`) the goldens were blessed
   under.
3. **Comparison**: decoded-pixel comparison, not PNG byte compare (encoder
   drift immunity). Python3 + PIL if available, else ImageMagick `compare
   -metric AE`; exact-zero-diff is the pass bar (llvmpipe is
   deterministic). On mismatch, write a diff image + both frames to an
   artifacts dir and fail with a clear message including the mesa version
   check (if mesa version != manifest version, say "mesa upgraded —
   re-bless" instead of "regression").
4. **Metric assertions**: from the benchmark CSV per zone —
   - exact-match assertions: total draw calls, state changes, polygon
     counts per designated frame (deterministic on a fixed path);
   - threshold assertions: p95 CPU frame ms ≤ recorded baseline × 2.0
     (llvmpipe timing varies with load; generous ceiling catches
     blocking-the-render-loop regressions, not noise).
   Baselines live in the same manifest.
5. **Memory assertions**: ConstrainedTextureCache / mesh cache byte totals
   after zone load (the benchmark logs/exposes these — extend its CSV or
   summary output minimally if needed) asserted ≤ the constrained preset's
   budget, and exact-match against manifest (deterministic loads).
6. **Entry points**:
   - `scripts/ci/visual_gate.sh` — runs all zones, all assertions; exit
     nonzero on any failure. `--bless` regenerates goldens + manifest.
   - Register a ctest entry (`visual_gate`) that invokes the script and
     SKIPs when `build-gles2/` is absent, so the default suite stays
     independent.
7. **Contact sheet**: `--bless` also emits
   `tests/goldens/contact_sheet.html` — all goldens embedded (base64 data
   URIs), labeled with zone/frame/what-to-check. The orchestrator publishes
   this for Will's blessing; do not mark anything "approved" yourself.

## Verification

- `scripts/ci/visual_gate.sh --bless` then `scripts/ci/visual_gate.sh`:
  bless → verify passes end-to-end on this VM.
- Deliberately perturb one golden (temporarily) → gate FAILS with diff
  artifacts → restore. Document this negative test in the review.
- Default build + full ctest unregressed; new ctest entry SKIPs cleanly
  when build-gles2 is absent.

## Review

### Headless findings

Confirms H01's review: the benchmark needs **no Xvfb at all**. Running
`gles2_zone_benchmark` with `DISPLAY` unset goes straight through the DRM
probe failure → EGL-surfaceless fallback → llvmpipe context, identical to
under Xvfb. All golden captures and gate runs in this unit run fully headless
on the bare VM.

### Determinism (verified before designing the gate around it)

Two independent runs of the same zone/camera-path config produce **byte-identical**
PNGs (`md5sum` match) for every designated frame, and CSV columns for
draws/state-changes/polys/regions are exactly reproduced frame-for-frame; only
the wall-clock timing columns (`cpu_ms`/`gpu_ms`/`total_ms`) vary between runs
(scheduler noise). This is exactly the split the plan's design anticipated:
exact-match assertions for geometry/draw metrics, threshold assertions for
timing. llvmpipe determinism holds — no design change needed.

### Zone/frame selection — the orbit-fallback camera needed a fix to be usable

`qeynos2` (frames=60, warmup=5, default camera settings, no new flags) reused
H01's already-proven config directly — city/outdoor, draws 8-11/frame,
25-40 polys, exactly matching H01's report.

For the other two zone types, the **existing orbit-fallback camera** (anchor =
zone bounding-box center, height = `findSurfacePosition()`'s Z, fixed for the
whole orbit) turned out to be unusable on non-flat zones: `gfaydark` (forest)
and every dungeon zone tried (`blackburrow`, `crushbone`) rendered **entirely
blank frames** (single-color, exactly the clear color) despite the benchmark
reporting nonzero draws/regions-visible. Diagnosed by re-running with
`--no-frustum-cull --no-pvs --no-occlusion-cull` (forces the *entire* zone
through the pipeline, ~2000-4000 draws) — still blank. That rules out a
culling bug; the camera itself was positioned somewhere with no geometry in
the near/far clip range for the *whole* orbit (embedded in terrain/rock, or
floating over empty sky above uneven ground), which the bounding-box-center +
fixed-height heuristic doesn't guard against on non-flat zones.

Fix: added an opt-in orbit-camera-anchoring mode to the benchmark tool
(`--orbit-anchor-surface`, `--orbit-radius F`, `--orbit-height-offset F` —
`tools/hwcaps/gles2_zone_benchmark.cpp`), which anchors the orbit to
`findSurfacePosition()`'s XY (the densest surface-level region found during
load) instead of the raw zone-bounds center, at a small configurable radius.
Default behavior (no flags) is **byte-for-byte unchanged** — this is
diagnostic-tool-only, flag-gated, opt-in.

- `gfaydark`: `--orbit-anchor-surface --orbit-radius 60` — draws 8-28/frame
  throughout a 60-frame run, real forest geometry visible in all 3 designated
  frames (confirmed: 660-4700 distinct colors per frame, not blank).
- Dungeon interior: **`blackburrow` and `crushbone` were tried first and
  rejected.** Both stayed completely blank across every camera config tried —
  default orbit, `--orbit-anchor-surface` at radii 30/50/80, height offsets
  6/15/30, and even the full-zone-unculled test. Not root-caused within this
  unit's scope (a plausible guess: these two zones have a distinct
  above-ground/below-ground split in their BSP data that the surface-position
  finder or orbit anchor doesn't handle the same way `befallen`'s single
  contiguous crypt volume does, but this is speculation, not confirmed).
  Per the task's escape hatch ("if a dungeon zone's assets are missing or a
  zone fails to load, pick an alternative and document"), substituted
  **`befallen`** (undead crypt) — `--orbit-anchor-surface --orbit-radius 30`
  renders correctly (391-2764 distinct colors across the 3 designated frames,
  draws ranging 87-2681/frame as the orbit sweeps through crypt rooms).

All three final zone configs' designated-frame PNGs were spot-checked
visually (color histograms, not just "nonzero") to confirm they show real
zone geometry, not a near-blank frame that happens to have >1 color.

### Metric/memory extensions to the benchmark tool

Minimal, flag-gated additions to `tools/hwcaps/gles2_zone_benchmark.cpp`
(all opt-in, zero effect when unused):
- `--dump-frames LIST --dump-png-prefix PREFIX`: dumps `PREFIX_f<N>.png` for
  each comma-separated measured-frame index, at the same beginScene/endScene
  point as the existing single-frame `--dump-png` (before the swap). The
  original `--dump-png` (last-frame-only) is untouched.
- `--memory-json FILE`: writes `{zone, textureCacheBytesUsed,
  textureCacheBytesLimit, textureCount}` from `ConstrainedTextureCache`'s
  existing accessors (`getCurrentUsage()`/`getMemoryLimit()`/
  `getTextureCount()` — no new production-cache code, just plumbing the
  existing numbers out). Also printed to stdout unconditionally
  (`Texture cache: N / M bytes (K textures)`).

### Comparison mechanism

`scripts/ci/visual_gate.sh` is a thin exec wrapper around
`scripts/ci/visual_gate.py` (PIL-based; the plan's ImageMagick `compare
-metric AE` fallback is implemented too, for a PIL-less environment, though
this VM has both). Decoded-pixel RGB comparison, exact-zero-diff pass bar.

**Bug found and fixed via the negative test**: the first implementation
converted images to RGBA before `ImageChops.difference()`/`.getbbox()`. Since
these are opaque framebuffer dumps (alpha is always 255, never differs),
`getbbox()` on an RGBA diff image effectively ignores color-channel
differences whenever the alpha channel is unchanged — it silently reported
"identical" for two images that were provably different by `numpy` compare
(1200 differing pixel-channel values, a deliberately-planted 20x20 magenta
patch clearly visible in one file, invisible to the comparison). Fixed by
converting to RGB (dropping alpha) before diffing — the negative test then
correctly failed. This would have been a silent no-op gate had the negative
test not been run. Comparison is per-designated-frame; on mismatch, both the
golden and the actual frame plus a diff image are written to
`build-gles2/ci_artifacts/` (gitignored — the whole `build-gles2/` tree
already is).

### Metric and memory assertions

Per designated frame: exact match on `draws`/`stateChanges`/`polys` (CSV
columns `draws`/`state_changes`/`polys`) against `tests/goldens/manifest.json`.
Per zone: p95 `cpu_ms` across the 60-frame run must be `<= baseline * 2.0`.
Texture-cache bytes-used: exact match against the manifest, and
`<= textureCacheBytesLimit` (the OrangePi constrained-preset budget, 64 MiB —
none of the three zones come close: 2.9-42.2 MB used).

### Baseline numbers (blessed manifest, `tests/goldens/manifest.json`)

Blessed under `llvmpipe (LLVM 20.1.2, 256 bits) (Mesa)` /
`OpenGL ES 3.2 Mesa 25.2.8-0ubuntu0.24.04.2`, 800x600, warmup=5, frames=60.

| Zone | Extra args | f0 draws/state/polys | f30 | f59 | p95 CPU ms | Tex cache used/limit |
|---|---|---|---|---|---|---|
| qeynos2  | (none)                                 | 10/4/27   | 9/4/25    | 9/3/38    | 1.71  | 42.2 MB / 64 MB |
| gfaydark | `--orbit-anchor-surface --orbit-radius 60` | 25/5/567  | 9/2/199   | 8/2/164   | 2.10  | 11.2 MB / 64 MB |
| befallen | `--orbit-anchor-surface --orbit-radius 30` | 87/6/415  | 1248/8/4457 | 373/7/1572 | 19.51 | 2.9 MB / 64 MB |

### ctest wiring

`ADD_TEST(NAME visual_gate ...)` + `SET_TESTS_PROPERTIES(... SKIP_RETURN_CODE
125)` in `tests/CMakeLists.txt` (unconditional — not gated behind an
`EQT_GLES2` CMake option, since `build-gles2/` is an independently-configured
second build tree, not something the `build/` cmake config can see). The
Python gate itself detects a missing `build-gles2/bin/gles2_zone_benchmark`
and exits 125, which ctest turns into a clean `Not Run (SKIP)` rather than a
failure.

### `.gitignore` fix required

`*.png` is a blanket-ignored pattern (screenshots/dumps). Added
`!tests/goldens/*.png` immediately after it so the committed goldens aren't
silently dropped by `git add` — verified with `git check-ignore -v` before and
after.

### Verification performed

1. `scripts/ci/visual_gate.sh --bless` then plain `scripts/ci/visual_gate.sh`:
   bless run wrote 9 PNGs + manifest + contact sheet; plain run reported
   `VISUAL GATE: PASS` for all 3 zones.
2. Negative test: hand-edited a 20x20 patch of `qeynos2_f30.png` to solid
   magenta, reran the gate — first attempt **silently passed** (the RGBA
   `getbbox()` bug above), fixed, reran — gate correctly reported
   `VISUAL GATE: FAIL` with `qeynos2 frame 30: PIXEL MISMATCH (400 differing
   pixels...)` and wrote `qeynos2_f30.png` / `_golden.png` / `_diff.png` to
   `build-gles2/ci_artifacts/`. Restored the original golden from a backup
   copy (byte-identical `md5sum` confirmed), reran, `VISUAL GATE: PASS` again.
3. Determinism: documented above (byte-identical PNGs, exact-match CSV
   columns across independent runs) — verified for both the original and the
   `--orbit-anchor-surface` camera paths before committing to the design.
4. Default build (`cmake --build build -j24`, desktop-GL, `EQT_GLES2=OFF`)
   reconfigured (to pick up the new `tests/CMakeLists.txt` entry) and built
   clean, including `gles2_zone_benchmark` itself (built fine against system
   desktop Irrlicht too, `EQT_HAS_GLES2` simply undefined so the new
   `--dump-frames`/`--memory-json`/`--orbit-anchor-surface` code paths are
   inert on that config's screenshot call but still compile — no `#ifdef
   EQT_HAS_GLES2` needed since they don't touch GLES2-specific APIs).
   `ctest --test-dir build`: **1223/1225 passed** (1225 total — the new
   `visual_gate` entry included). The new `visual_gate` test itself **passed**
   (6.18s) — `build-gles2/` happens to already exist in this persistent slot
   from H01, so the gate ran the real comparison rather than skipping. The 2
   failures are both pre-existing, documented flakes unrelated to this unit
   (`refactor/test_baseline.txt`): `AudioManagerTest.PreloadCommonSounds`
   (host ALSA can't resolve card 0, documented since F02/F14) and
   `RDPServerFrameTest.UpdateFrameMultipleTimes` (a load-dependent segfault,
   already documented as "rare, load-dependent... pre-existing, unrelated to
   Batch F"). No new regressions. Separately verified the **SKIP path**
   required by the task: renamed `build-gles2/` out of the way and re-ran
   `ctest --test-dir build -R visual_gate` — `1/1 Skipped`, `100% tests
   passed, 0 tests failed`, confirming `SKIP_RETURN_CODE 125` works cleanly;
   restored `build-gles2/` and reran `scripts/ci/visual_gate.sh` directly to
   confirm it still passes after the rename round-trip.
5. `bash scripts/refactor/check_file_sizes.sh --files scripts/ci/visual_gate.py
   scripts/ci/visual_gate.sh tools/hwcaps/gles2_zone_benchmark.cpp`: `ok` for
   the two new scripts (357 and 19 lines); `WARN` (not `FAIL`) for
   `gles2_zone_benchmark.cpp` at 1649 lines (was already 1556 before this
   unit, over the 1500 warn threshold from Batch F's ceiling but nowhere near
   the 3000 fail line) — exit code 0.

### Deviations from the plan

1. **Orbit-fallback camera anchoring added** (`--orbit-anchor-surface` +
   2 supporting flags) — not in the plan's original point 1, but required to
   get any usable (non-blank) golden frame out of the existing orbit fallback
   for non-flat zones. Flag-gated, opt-in, default behavior unchanged — judged
   in-scope under "dump-frames selection... fair game, keep minimal and
   flag-gated" since it's a benchmark-tool camera-placement fix, not a
   production-renderer change.
2. **Dungeon zone substitution**: `blackburrow`/`crushbone` (asset-present,
   zone-loads-fine) rejected in favor of `befallen` because neither rendered
   any visible geometry at any camera position tried — functionally
   equivalent to the plan's "assets missing / zone fails to load" escape
   hatch, documented in the manifest's per-zone `description` field and above.
3. **RGBA-vs-RGB image-diff bug**: caught only because the negative test was
   actually run before trusting the gate, per the task's verification
   requirement — flagged here since it's exactly the kind of bug a golden-
   image gate exists to catch, that would otherwise have infected the gate
   itself.
