# P49 — DRM mode fallback: fail loud (P43 follow-up)

**Slot: slot2. x86 ONLY — no Pi (P48 owns it), no eqemu. Authorized
by Will 2026-07-31 (item 2).**

## Background

P43 (refactor/P43_res_visibility_root_cause.md): when --res has no
exact DRM mode match, docker/irrlicht-drm/CIrrDeviceFB.cpp:335-382
silently substitutes the connector's preferred mode and overwrites
Width/Height — every historical "400x300" Pi run actually rendered
2560x1440. The trap is still armed for anyone requesting a
non-native resolution.

## Work (split by consumer, keep production robust)

1. **Client (production)**: keep the fallback (a client that can't
   boot on an unknown monitor is worse than one at the wrong res) but
   make it LOUD: ERROR-level log stating requested vs actual mode,
   repeated once post-init (not just buried in device-creation
   spew).
2. **Benchmark**: gles2_zone_benchmark REFUSES to run when the
   achieved mode != requested resolution, unless
   --allow-mode-fallback is passed; either way the ACTUAL WxH goes
   into the CSV header/columns so no dataset can ever be mislabeled
   again.
3. x86 has no DRM path, so behavioral verification is limited here:
   unit-verify the decision logic (extract it into a small testable
   helper if needed), build-verify via the ARM cross-build, and
   register the 1-invocation Pi behavioral check (request 400x300,
   expect refusal + log line) as a RIDE-ALONG item in deferred.md
   for the next Pi-owning unit — do NOT touch the Pi yourself.

## Rules

- Files: docker/irrlicht-drm/CIrrDeviceFB.cpp + benchmark tools +
  tests. Do NOT touch entity/texture code (slot1/slot3 territory).
- Full bar: GLES2 ctest baseline, visual gate PASS (should be
  trivially unaffected — llvmpipe path has no DRM), ARM cross-build
  clean, file sizes.
- Small unit — if it grows beyond the above shape, STOP and report.

## Review

Executed 2026-07-31 on `wt/slot2`, branch `task/P49-drm-fail-loud`, cut from
`fable` at `9a61ed3`. x86 only, as scoped — no Pi/ssh use.

### 1. Client (production): loud, repeated

`docker/irrlicht-drm/CIrrDeviceFB.cpp`'s mode-fallback path (P43 root
cause, formerly `ELL_WARNING`) is now `ELL_ERROR`, stating requested-vs-
actual mode explicitly:

```
DRM: MODE FALLBACK -- no mode matching requested 400x300; using
2560x1440@60Hz instead (requested resolution NOT honored)
```

logged once at `initDRM()` time (unchanged location, upgraded severity),
and repeated once more — same message, `(post-init)` tag — the first time
`CIrrDeviceFB::run()` is called (i.e. once the render loop actually
starts), so the signal survives past device-creation-time log spew. State
(`modeFallbackOccurred_`, `modeFallbackPostInitLogged_`,
`fallbackRequestedWidth_/Height_`) lives on `CIrrDeviceFB` itself
(`CIrrDeviceFB.h`). The fallback behavior itself (client still boots on an
unmatched resolution) is unchanged, per the plan's rationale — only the
logging changed.

### 2. Benchmark: refuse-by-default + unmislabelable CSV

`gles2_zone_benchmark` now compares the achieved resolution
(`driver->getScreenSize()`, read right after `ZoneBench::loadZone()`
returns) against `--res`. On mismatch: prints an ERROR (or WARNING if
`--allow-mode-fallback` was passed) naming both resolutions, and without
the flag, exits 1 before any frame is measured. `--allow-mode-fallback`
downgrades the message and lets the run proceed. Independent of the flag,
`zone_bench_metrics`'s `MetricsCollector::setActualResolution()` is called
once (also right after device creation) and two new trailing CSV columns,
`actual_width,actual_height`, are written on every row (append-only
convention, matching every prior P0x CSV column addition) — so even an
`--allow-mode-fallback` run can never again be silently mislabeled with
the requested-but-not-achieved resolution.

### 3. Decision-logic extraction + verification without a Pi

The exact mode-selection algorithm (exact match highest-refresh, else
preferred, else first) was extracted verbatim from `initDRM()` into a
pure, dependency-free helper: `docker/irrlicht-drm/drm_mode_select.h`,
`eqt_drm::selectDRMMode()` — plain structs (`DRMModeCandidate`,
`DRMModeSelection`), no libdrm/gbm/EGL/Irrlicht includes, so it compiles
and runs identically on any host. `CIrrDeviceFB.cpp`'s `initDRM()` now
adapts `drmModeConnector`'s mode array into `DRMModeCandidate`s, calls
`selectDRMMode()`, and uses `.exactMatch` to decide whether to log/flag
the fallback — the decision itself no longer lives inline.

- **Unit-verified** (x86, no DRM device involved):
  `tests/test_drm_mode_select.cpp`, 7 cases against
  `GTest::gtest_main`/ctest, including the exact P43 hardware scenario
  (Pi's real mode list — 800x600 exact-match, 2560x1440 preferred, no
  400x300 entry) plus edge cases (no preferred mode, empty mode list,
  zero-resolution request, multiple PREFERRED flags). All 7 pass
  (`./build/bin/test_drm_mode_select`, and via `ctest -R DRMModeSelect`).
- **Build-verified** (ARM cross-build): `scripts/build-arm-noble.sh`
  clean, zero `error:` lines, `CIrrDeviceFB.cpp` compiles against the
  extracted header with only a pre-existing unrelated warning
  (`initEvdev()` truncation warning, not touched by this unit).
  `build-arm-noble/bin/willeq` produced, non-stub (338M unstripped with
  debug symbols, matching the script's normal output shape).
- **Behaviorally NOT verified here** (x86 has no DRM/KMS path at all — see
  `docker/CLAUDE.md`/`src/client/graphics/CLAUDE.md`): registered as a
  ride-along item in `refactor/deferred.md` (P43 entry) for the next
  Pi-owning unit — 1 invocation, `--res 400 300` without
  `--allow-mode-fallback`, expect refusal + both ERROR log lines, plus a
  `--res 800 600` control expecting no fallback line at all.

### Propagation (build note from the brief)

Confirmed via `docker/CLAUDE.md` and `scripts/build-gles2-x86.sh`:
`docker/irrlicht-drm/CIrrDeviceFB.cpp` is the ARM-only file — x86 uses a
**separate fork**, `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` (adds the
surfaceless-EGL fallback), copied in by `build-gles2-x86.sh` instead of
the file this unit edited. So x86 (`build/`, `build-gles2/`) is
**structurally unaffected** by the CIrrDeviceFB.cpp/`.h` edits — confirmed
by rebuilding both x86 trees clean with the visual gate and ctest baseline
unchanged. The ARM Dockerfiles (`Dockerfile.arm-noble`,
`Dockerfile.arm64-noble`) copy `docker/irrlicht-drm/*` file-by-file
(explicit `COPY`/`cp` lines, not a whole-directory copy) — the new
`drm_mode_select.h` needed an explicit line added to both, or the ARM
build would have silently kept building the OLD `CIrrDeviceFB.cpp`
inline logic with a missing-header compile failure never reached (the
`#include` would 404 at Docker copy time, not compile time, if skipped
entirely — verified this is NOT what happened by grepping the ARM build
log for the `drm_mode_select.h` COPY line and zero `error:` lines
afterward). `Dockerfile.arm64-noble` (Rock64, not this unit's ARM-build
bar target) was patched too for consistency, since it shares the same
per-file COPY pattern and would otherwise have silently gone stale.

### Deviations

- Touched `docker/Dockerfile.arm-noble` and `docker/Dockerfile.arm64-noble`
  (one `COPY` + one `cp` line each) beyond the plan's stated file list
  (`CIrrDeviceFB.cpp` + benchmark tools + tests). This was necessary
  plumbing for the "extract a small testable helper" instruction the plan
  itself authorized — the alternative (keeping the pure helper inline in
  `CIrrDeviceFB.cpp` instead of its own header) would have made it
  untestable on x86 at all, since the rest of that file depends on
  libdrm/gbm/EGL headers not appropriate for a host unit test. Flagged
  here rather than silently expanding scope; happy to revert to an
  inline-only version if this is judged out of bounds.
- No other scope growth. `entity/texture/composite` code untouched;
  `mesh_building.cpp`/`animated_mesh_creation.cpp` untouched.

### Verification (full bar)

- `ctest --test-dir build --output-on-failure -j4`: **1633/1635 passed**
  (2 skipped-test-group entries above are `Skipped`, not counted as
  fail); the 2 failures are the documented host exceptions
  (`AudioManagerTest.PreloadCommonSounds` — ALSA;
  `RDPServerFrameTest.UpdateFrameMultipleTimes` — RDP frame race),
  matching the `~1617/1619 class` baseline's documented exception shape.
  New `test_drm_mode_select` (7/7) included and passing.
- `scripts/build-gles2-x86.sh`: clean rebuild, non-stub binaries
  (`gles2_zone_benchmark` 14,450,056 bytes, `willeq` 79,057,752 bytes).
- `scripts/ci/visual_gate.sh`: **PASS** (qeynos2/gfaydark/befallen),
  goldens untouched (llvmpipe path has no DRM, trivially unaffected as
  predicted).
- `scripts/build-arm-noble.sh`: clean, zero `error:` lines,
  `build-arm-noble/bin/willeq` non-stub (338M, debug symbols retained per
  script default).
- `scripts/refactor/check_file_sizes.sh`: no new FAIL. The pre-existing
  `tools/hwcaps/gles2_zone_benchmark.cpp` WARN (2982 lines, > 1500) is
  unchanged in kind — this unit added ~35 lines to an already-over-
  threshold file; not introduced by this unit and not grown into FAIL
  territory.

### Files touched

`docker/irrlicht-drm/CIrrDeviceFB.cpp`, `docker/irrlicht-drm/CIrrDeviceFB.h`,
`docker/irrlicht-drm/drm_mode_select.h` (new), `docker/Dockerfile.arm-noble`,
`docker/Dockerfile.arm64-noble` (both: 1 `COPY` + 1 `cp` line only),
`tools/hwcaps/gles2_zone_benchmark.cpp`, `tools/hwcaps/zone_bench_metrics.h`,
`tools/hwcaps/zone_bench_metrics.cpp`, `tests/test_drm_mode_select.cpp` (new),
`tests/CMakeLists.txt`, `refactor/deferred.md` (P43 entry updated + P49
ride-along item registered).
