# B952 -- impostor billboards, part 1: measure the impostor memory row and pick the impostor size

GitLab #270. Branch `task/B952-impostor-measure`, cut from `fable` 267e5a36.
Worktree `/mnt/projects/fable/wt/audit-impostor`. Review media:
`/mnt/projects/fable/review/B952/`. Results: `refactor/B952/README.md`.

## Will's design (chat, 2026-09-30) -- the contract this unit measures for

1. Impostor memory is its OWN memory-ledger row with its own blessed budget,
   measured on the Orange Pi.
2. Unique per-spawn impostors while that row fits; shared per-race/sex(/variant)
   impostors when it would not -- a cap check, not a mode.
3. The production `FrameBudgetGovernor` decides the LOD tier boundary.

Standing rules: memory first, frame rate second; 100 % accounting (a shared
block is counted once, with its claimants named); no aesthetic verdicts, pixels
are verified mechanically; the original client is not the oracle for perf.

## Scope -- benchmark only, no production code change

All code lands in `tools/hwcaps/` (the `gles2_zone_benchmark` family). No file
under `src/`, `include/`, `config/` or `docker/` changes; no budget, golden,
baseline or gate assertion changes. The impostor budget is a NEW row Will
blesses from the table in `refactor/B952/README.md` -- proposed, never set.

1. **Bounded, measurable impostor cost** (new TU `zone_bench_impostor.{h,cpp}`):
   - one shared scratch render target (its FBO + DEPTH16 renderbuffer) for every
     impostor render; an impostor is a colour texture only (raw GL
     `glTexImage2D` RGBA8, wrapped with the driver's existing
     `gles2WrapTexture()` bridge, swapped in as the scratch FBO's colour
     attachment). No driver change is needed: `COpenGLES2Driver::setRenderTarget`
     binds whatever the FBO holds. The pre-B952 per-impostor
     `addRenderTargetTexture()` (RGBA8 + its own DEPTH16) stays reachable as
     `--impostor-fbo per` for the A/B.
   - `--impostor-size {64,128,256}` (default 256, the pre-B952 value);
   - `--impostor-mode {unique,shared}` -- shared keys on race + gender (the
     benchmark places texture variant 0 only; the variant is part of the key);
   - `--impostor-refresh N` -- re-render every live impostor every N frames;
   - a `tex.impostor` owner row under `textures_driver`, counted once per
     texture with its claimant count, printed as `[memledger-owner]` beside the
     `[memledger]` points and as an `impostor` object in `--memory-json`
     (emitted only when impostors exist, so the gate's JSON is byte-identical);
     the ledger/RssAnon/RssShmem/gpu_driver/`gpuTextureBytes` deltas across
     impostor creation are printed with it, plus the DRM fd's `drm-*` fdinfo
     keys and system `Shmem` where the platform exposes them.
2. **Scenes**: `--impostor-scene far|mixed` re-places the `--entities N`
   entities in rows in front of the first camera pose (all beyond
   `--lod-dist`, or half inside / half beyond it); `--entity-genders 0|1|both`
   doubles the race set to race x gender variants. The adaptive path gains a
   distance tier (`--adaptive-tier-dist`, 0 = off = pre-B952 behaviour): an
   entity beyond it is always an impostor, and the composite cap/frame-budget
   limit applies inside it.
3. **Impostor work moves out of the draw**: creation and refresh run in a
   pre-pass before `beginScene()`, each bracketed by `glFinish()`, so the
   creation time per impostor and the refresh cost per frame are measured
   times, not submission times, and a mid-frame FBO switch never lands inside
   the frame on a tiler.
4. **Pixel evidence**: `--impostor-pixel-test DIR` renders, for each loaded
   variant at 40/80/160 units, the background alone, the full model (same
   idle pose, same lights) and the impostor at 64/128/256; it prints per-pixel
   diff numbers (max abs channel difference, pixels differing, pixels differing
   by more than 32) over the frame, the model's on-screen bounding box
   (height in pixels, mechanical), and writes a 4x crop sheet per distance.
5. **Measure** on x86 (llvmpipe) and the Orange Pi (CI ARM build,
   `bin/willeq-nollvm` Mesa prefix via `mesa_nollvm_select.sh` on a copy of
   the benchmark), N = 1/50/200, sizes 64/128/256, unique/shared, qeynos2 and
   gfaydark, far and mixed poses.

## File territory

`tools/hwcaps/zone_bench_impostor.{h,cpp}` (new), `zone_bench_lod.cpp`,
`zone_bench_loader.{h,cpp}` (entity race/gender fields, gender list),
`zone_bench_options.{h,cpp}`, `gles2_zone_benchmark.cpp` (wiring only),
`zone_bench_memory.cpp` (the JSON object), `CMakeLists.txt` (the new TU in the
three targets that link `zone_bench_lod.cpp`), `refactor/B952*`,
`refactor/BOARD.md`, `scripts/b952/` (the measurement drivers).

## STOP conditions

- The swapped colour attachment is FBO-incomplete on the Pi (report, fall back
  to `--impostor-fbo per` for the Pi numbers, do not patch the driver blind).
- Any golden run, gate script or ctest names one of the new flags.
- The Pi lock is held by someone else.

## Verification bar

Build `build/` (full suite), `build-gles2/` (`scripts/build-gles2-x86.sh`), the
CI ARM build; `check_file_sizes.sh`; ctest; the visual gate unaffected (no
manifest entry uses these flags -- grep, plus the CI `gate` job).

## Review

See `refactor/B952/README.md`.
