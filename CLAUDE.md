# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Additional context is in subdirectory CLAUDE.md files:
- `src/client/graphics/CLAUDE.md` - Rendering, shaders, GLES2, zone loading, performance
- `include/client/graphics/ui/CLAUDE.md` - Static UI panels, key bindings, slash commands
- `include/client/state/CLAUDE.md` - Game state modules, EventBus, 99 event types
- `include/client/bridge/CLAUDE.md` - Event/intent bridge, thread-safe game↔renderer communication
- `src/client/input/CLAUDE.md` - Input handlers, hotkey manager, command processor, action dispatch
- `src/client/audio/CLAUDE.md` - Audio system, sound categories, audio tests
- `docker/CLAUDE.md` - Cross-compilation, DRM/KMS device, GLES2 driver
- `docs/CLAUDE.md` - Coordinate systems, heading conventions, docs layout

## Orientation (read before large tasks)

- `docs/decisions.md` — standing architecture decisions. Settled; do not re-litigate.
- `docs/module_map.md` — functional map of every split translation unit (EverQuest, Application, CommandProcessor, InventoryManager, IrrlichtRenderer, EntityRenderer, WLDLoader, SimulationWorker, ZoneShader, tests/cmake). Consult it to find the right TU instead of scanning.
- `refactor/BOARD.md` — **the open-work queue of record. Start here.** One entry per open item, each confirmed against the tree at the tip named in its header, classed A (live defect) / B (unreached or hygiene) / C (needs Will's decision) / P (process). Nothing else is a work queue.
- `refactor/K_component_extraction.md` — the open structural programme (component extraction from `class EverQuest`, then `class IrrlichtRenderer`). The active unit plans are `refactor/K<nn>_*.md`.
- `refactor/completed/` and most of `docs/reference/` are historical snapshots (bannered); do not trust their findings as current. That includes `deferred_archive_20260903.md`, `progress_archive_20260903.md` and `testing_progress_archive_20260903.md` — the three trackers `BOARD.md` replaced (K00, 2026-09-03). They are research records; many entries were closed by later units without being struck.
- Workspace: canonical checkout at `/mnt/projects/fable/main` (branch `fable`; symlink from `/mnt/projects/willeq-fable`), executor worktree slots at `/mnt/projects/fable/wt/slot1..3` plus `t1`, `t2` (testing-session slots). Task branches: `task/<UNIT>-<name>`, e.g. `task/K01-guild-component`. The `renderer` branch is reference-only — never merge it.

**Reading order for a new agent:** `refactor/BOARD.md` → `docs/module_map.md` →
`docs/decisions.md` → the unit plan you were dispatched with.

## Process requirement

- Every unit of work is PLANNED by the orchestrator (a `refactor/<UNIT>_*.md`
  plan with scope, file territory, STOP conditions and a verification bar)
  and EXECUTED by executor agents in worktree slots under
  `refactor/EXECUTOR_PROTOCOL.md`. Independent legs are dispatched in
  parallel slots whenever their file territories do not overlap; the
  orchestrator serializes merges onto `fable` (`docs/decisions.md`,
  2026-07-22 workspace layout). Do not execute unplanned work inline in
  `main`.
- **Merge step (T337, 2026-09-04): before merging a branch, run the
  order/coverage golden tests for every TU the branch touched** (the G
  batch pinned `zone_dispatch`, `bridge_component_intents`,
  `command_processor_registration`, `application`, `renderer_frame_render`,
  `renderer_player_physics` with goldens). A branch cut before a golden's
  unit merged is green on its own and fails on the tip.
- **Batches J, K, L and G are complete; every file under `src/`, `include/`
  and `tests/` is under 1,500 lines and ctest enforces it.** The last two
  exceptions there are gone —
  `include/client/eq.h` fell to 899 lines and
  `include/client/graphics/irrlicht_renderer.h` to 1,343 — so
  `scripts/refactor/file_size_allowlist.txt` is EMPTY and a file over 1,500
  under `src/`, `include/`, `tests/`, `tools/` or `benchmarks/` is a **FAIL**
  in the `FileSizeCeiling` ctest test, not a warning (M00,
  `docs/decisions.md` 2026-09-04; T385 added the last two directories, board
  B167 — `--all` had never scanned `tools/`, and six files there were over the
  ceiling while the checker reported a clean tree). **`tools/` is not clean
  yet and `FileSizeCeiling` is RED at this tip**: T385 split four of the six,
  leaving `tools/hwcaps/zone_bench_loader.cpp` (3,280) and
  `tools/hwcaps/gles2_zone_benchmark.cpp` (3,060) over the 3,000 hard cap —
  board **B185**, owned by T387. Put
  substantial new code in new TUs; a file that genuinely must exceed the
  ceiling needs an allowlist entry with a reason and a no-grow max. Close-outs:
  `refactor/J_file_size_reclamation.md`, `refactor/K_component_extraction.md`,
  `refactor/L_renderer_component_extraction.md`,
  `refactor/G_oversize_methods.md`.

## Project Overview

WillEQ is a standalone EverQuest client targeting the Titanium client version with optional 3D graphics rendering. It was migrated from the akk-stack headless client to remove all eqemu dependencies. All opcodes are hard-coded for Titanium specifically.

## Build Commands

**IMPORTANT: `-j$(nproc)` causes problems when executed directly in a Bash tool command (the shell expansion doesn't work reliably in Claude Code interactive sessions). Use a hardcoded value instead, e.g. `cmake --build build -j24` for this 24-core host. If building from a script (e.g. `scripts/build-arm-noble.sh`), `$(nproc)` works fine.**

**Desktop-GL Irrlicht support has been removed (P30,
`refactor/desktop_gl_elimination_plan.md`).** `EQT_GRAPHICS=ON` (the default)
now requires a locally-built patched GLES2 Irrlicht — there is no more
system-`libirrlicht-dev`/X11-GLX fallback. Configuring without one fails
early at `cmake` time with a message naming the bootstrap command below (not
a build/link-time failure).

**One-time bootstrap** (only needed once per checkout, or after the patched
Irrlicht is deleted — `third_party/irrlicht-gles2-x86/` is gitignored):

```bash
bash scripts/build-gles2-x86.sh --irrlicht-only
```

```bash
# Configure (from project root) — the GLES2-config tree is now the primary
# dev/test tree (tests on, full ctest suite). Run it; since T392 (2026-09-05,
# board B215) the suite has NO expected failures — the long-standing
# "ALSA quirk" waiver on AudioManagerTest.PreloadCommonSounds was a
# misdiagnosis and the test is green. A red test is a real red.
mkdir -p build && cmake -S . -B build \
    -DEQT_GRAPHICS=ON \
    -DEQT_GLES2=ON \
    -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 \
    -DEQT_BUILD_TESTS=ON

# Build (no -j flag!)
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Live/integration tests (need a running eqemu server + display + EQ client
# files) are NOT in the default suite. Register them, then select by label:
#   cmake -S . -B build -DEQT_LIVE_TESTS=ON
#   ctest --test-dir build -L live
# The binaries build either way; only ctest registration is gated (T164).

# Run a single test
cd build && ctest -R TestName --output-on-failure
# Or directly:
./build/bin/test_packet

# Build output
./build/bin/willeq
```

This is the same patched Irrlicht / EGL+GLESv2 (surfaceless llvmpipe on this
dev VM, native GLES2 on the Orange Pi) backend as `scripts/build-gles2-x86.sh`
and `scripts/build-arm-noble.sh` use — see "Orange Pi (ARM)
Cross-Compilation" below. `scripts/build-gles2-x86.sh` (no args) additionally
produces a second, smaller tree (`build-gles2/`, tests off) with just
`willeq` + `gles2_zone_benchmark` — that is what `scripts/ci/visual_gate.sh`
targets; keep using it for the visual gate, it is unaffected by the above.

## Running

```bash
# With graphics (default: surfaceless EGL, no display required — same
# llvmpipe backend the GLES2 tree's tests and visual gate use)
./build/bin/willeq -c willeq.json

# With graphics, X11-windowed (P25; opt-in only, never DISPLAY-autodetected —
# see "Orange Pi (ARM) Cross-Compilation" below)
./build/bin/willeq -c willeq.json --egl-x11

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# With VNC (for headless servers)
./scripts/start-with-vnc.sh -c willeq.json
# Connect via: vnc://localhost:5950 (VNC_PORT in the script; its header comment
# still says 5999 — the variable is authoritative, T215)

# With VNC + Audio streaming (for headless servers with sound)
./scripts/start-with-vnc-audio.sh -c willeq.json
# Connect VNC: vnc://localhost:5910 (VNC_PORT=5910 in start-with-vnc-audio.sh)
# Connect Audio: vlc http://localhost:8085 (AUDIO_PORT=8085)

# With native RDP (alternative to VNC)
./build/bin/willeq -c willeq.json --rdp
# Connect via: mstsc.exe /v:hostname:3389 (Windows)
# Or: xfreerdp /v:hostname:3389 /cert:ignore (Linux)

# RDP with custom port
./build/bin/willeq -c willeq.json --rdp --rdp-port 13389
```

### Where the client's shipped data comes from

**The client no longer has to be started from the repo root** (T399, board
B235). Every shipped data file it reads — `config/*.json`, `data/willeq.db`,
`data/textures/*.bmp`, `data/zone_lines/*.json`, `data/item_models.json`,
`data/eqe1_texture_index.json` and the rest — goes through one resolution,
`eqt::ResolveDataPath()` (`include/client/data_paths.h`). Search order, first
existing file wins:

1. the path itself, if it is absolute (never rewritten);
2. **the path as given, i.e. relative to the current working directory** —
   deliberately first, so a run from the repo root reads exactly the bytes it
   read before this existed (the visual gate included);
3. `$WILLEQ_DATA_ROOT/<relative>`, for a staged install whose data tree is not
   beside the binary;
4. the directory holding the running executable, then up to three parents —
   which finds `<repo>/config/...` from `<repo>/build/bin/willeq`,
   `<repo>/build-gles2/bin/willeq` and `<repo>/build-arm-noble/bin/willeq`
   alike.

If nothing exists the input comes back unchanged, so the caller's own "could
not open X" names the X it asked for. `willeq.json` itself is resolved the
same way. **A new shipped-data load must call `ResolveDataPath()`** —
`tests/test_data_path_routing.cpp` re-takes the census on every ctest run and
fails on a bare `"config/…"` / `"data/…"` literal that does not.

**Generated caches follow the opposite rule: they are written OUTSIDE the
checkout** (T402, board B248). `snd_index_cache.json` (the sound-archive
index) and `gfx_index_cache.json` (the character-archive index) used to be
git-tracked files under `config/` that the client rewrote on every run, so a
plain `willeq` run left the working tree dirty and a careless `git add -A`
committed one machine's absolute EQ path. They now resolve through
`eqt::ResolveCachePathForRead()` / `ResolveCachePathForWrite()`
(`include/client/data_paths.h`), which pick the first writable of
`$WILLEQ_CACHE_DIR`, `$WILLEQ_DATA_ROOT/cache`, `<exe dir>/cache` (i.e.
`build/bin/cache`) and `cache/`. A miss is not an error — it costs a rescan,
measured at ~0.4 s for the 17 sound archives and ~30 ms for the 378
character archives. A new generated cache uses these, never `ResolveDataPath()`.

### The shipped data has build recipes — use them, do not hand-edit

Two shipped assets are **derived**, not hand-carried, and each has a ctest
test that fails if the checked-in bytes and their inputs drift apart:

| asset | recipe | pinned by | ctest |
|---|---|---|---|
| `data/textures/*.bmp` (64 files) | `scripts/build_textures.sh` → `tools/generate_textures` | `data/textures.sha256` | `TexturesReproducible` |
| `/mnt/projects/EverQuest_ETC1/willeq.eqe1` (126 entries since T411: 65 textures, 60 ETC1 `_alpha` companions, 1 raw) | `scripts/build_willeq_pack.sh` | `data/willeq.eqe1.sha256` | `PackReproducible` |

The pack's inputs are the 64 BMPs plus `tools/ui_atlas_builder`, so a texture
change cascades: regenerate the BMPs, rewrite `data/textures.sha256`
(`--write-pins`), rebuild the pack, update `data/willeq.eqe1.sha256` — all in
**one commit**, or both tests go red. The pack lives outside the repo and is
read live by every worktree; only `build_willeq_pack.sh` may install it (it
writes a temp file in the destination directory and `mv`s it into place).

Every one of those BMPs is produced by
`src/client/graphics/texture_generators.cpp` (plus
`DetailTextureAtlas::createAtlasImage()` for `detail_atlas.bmp`). That TU was
in **no CMake target at all** until T400/T403 — which is how the particle
atlas shipped alpha-less for a year (board B225). T406 closed the second
half of that (board B266): **60 of the 64 now ship 32-bit with their alpha
in-band**. T411 then took the pack side back: the pack's
`--rgba8888-texture` list is **down from 60 entries to one**, because the
three alpha-BLEND shaders
(`SOLID3D_FS`, `UI2D_FS`, the particle point sprite) now sample the ETC1
`_alpha` companion on texture unit 1 — the driver binds it from the unit-0
texture in one place, `COpenGLES2Driver::bindAlphaCompanionUnit()`, and
`ConstrainedTextureCache` records the pairing on the texture itself (boards
B261 and B287, both closed). The
four that stay 24-bit each carry a written reason in
`tests/test_texture_alpha_census.cpp`, whose `EveryShippedBmpIsAccountedFor`
makes a new generator declare which side it is on:
`spell_particle_fallback.bmp` (drawn additively, alpha provably unsampled —
board **B286**), `detail_atlas.bmp` (board **B265**: still the one file no
build reproduces), and `rain_streaks.bmp` + `snow_flakes.bmp` (no consumer
at all). `generate_textures --report` prints the census.

**A 32-bit entry is not free**: with all 60 in-band the pack went 2.83 MB to
19.99 MB and every zone's `eqe1MappedBytes` rose by 17.2 MB, because an ETC1
256x256 entry with mips is 43,704 bytes and its RGBA8888 form is 349,524.
T411 (board B287) took that back by fixing the shaders instead of the payload:
the pack is **5,532,254 bytes** (the extra over the pre-T406 2.83 MB is the
ETC1 companions, which are new — the old 24-bit BMPs had no alpha to carry),
and `eqe1MappedBytes` fell by 14,462,656 per zone. The one entry that keeps
an in-band payload is `ui_atlas`, and not for a single-sampler reason: a 1px
UI `drawRect` minifies the atlas 4:1 and selects companion mip level 2, where
ETC1 decodes the isolated `white` texel as 217/255 and dims the rect by 15%. **A new
`--rgba8888-texture` override is now a ctest FAIL**
(`TextureAlphaCensus.NoPackEntryNeedsTheRgba8888Payload`); if a consumer
genuinely cannot take a second sampler, it needs an entry in that test's
`kInBandAlpha` list naming the consumer and the reason.

A generator must be **bit-identical across optimisation levels** — a shipped
asset whose bytes depend on `CMAKE_BUILD_TYPE` makes `TexturesReproducible` a
coin toss. Two noise hashes overflowed signed `int` (UB) and GCC folded them
differently at -O0 and -O2, moving 157 texels of the particle atlas (board
B268, fixed in T403 by moving them to `uint32_t`). Keep new generator
arithmetic defined.

### Display Options Comparison

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Surfaceless EGL (default) | No display needed at all — llvmpipe GPU emulation | Local | Native |
| X11-windowed EGL (`--egl-x11`, P25) | Local display, GLES2 in a window | Local | Native |
| Xvfb + VNC | Remote access, cross-platform | None | VNC client |
| Xvfb + VNC + Audio | Remote with sound | HTTP stream | VNC + VLC |
| Native RDP | Windows users, better compression | RDP audio | mstsc.exe, xfreerdp |

RDP and VNC can run simultaneously if both X11 and RDP are enabled.

### The scenario lane — a live verification that exits with a code

**T410 (board B138/B139).** Before this, every live verification on the board
ended with a human reading a session transcript: `main.cpp` returned `0`
unconditionally, so a client that crashed, never logged in, or logged in and
did nothing at all were indistinguishable to a script. The scenario lane makes
the process exit code the result.

```bash
# One scenario, headless, against a running eqemu:
./build/bin/willeq -c config.json --scenario zone_in --timeout 120

# The whole rig -- lock, up.sh, snapshot, run, restore with the world DOWN,
# down.sh, release -- with the client's exit code passed through:
bash scripts/scenario.sh zone_in --timeout 120 --unit T4xx
```

**Exit-code taxonomy** (`include/client/scenario/scenario_runner.h`; nothing
else may be returned from a scenario run):

| code | meaning |
|---|---|
| 0 | pass — the scenario asserted and the assertion held |
| 1 | fail — the scenario ran and its assertion failed |
| 2 | timeout — zoned in, but no verdict inside `--timeout` (**also what an unfinished run reports**, so an external `/quit`, a SIGTERM or a crash can never masquerade as success) |
| 3 | could-not-start — never reached a zone (server down, bad login, unknown scenario name) |

`scripts/scenario.sh` adds its own operator codes on top: 64 lock held by
another session, 65 `up.sh` failed, 66 the server snapshot did not restore.

**How it hangs off the client.** `--scenario` forces Automated mode, and the
runner is registered on `AutomatedMode::setAutomationCallback` — the per-tick
hook that had zero registrants in the whole tree until this (board B139). The
callback runs on the GAME THREAD (`Application::processInput()`), one
`ScenarioRunner::Tick(dt)` per ~60 Hz tick; returning `false` is
`AutomatedMode`'s own contract for "stop", so the client unwinds through
`isQuitRequested()` exactly like `/camp` and never calls `exit()` from inside a
tick.

**The runner owns the pre-flight, not the scenarios.** Until
`IsFullyZonedIn()` is true it does not call `Step()` at all; if that never
happens inside the start deadline the verdict is 3. So "the server was down"
can never be reported as a scenario failure, and every scenario's first
`Step()` may assume a live, zoned-in client.

**Scenarios poll; they do not subscribe.** `GameState::events()` (which
`AutomatedMode` forwards) and the renderer-facing `GameStateBridge` are two
different streams, and the interesting landings — buffs via T348's `OP_Action`
path — go to the bridge, which a headless client does not drain. Polling reads
the same authoritative state either stream was published from.

| scenario | asserts | replaces |
|---|---|---|
| `zone_in` | login → zone → `IsFullyZonedIn()` with a non-empty zone name | every `grep "Zone connection complete"` rig |
| `two_client_survive` | a second player appears AND both hold for `--scenario-hold` seconds (default 300) | `review/T391/scripts/t391_run.sh`'s `kill -0` poll, which printed SURVIVED when the partner never logged in |
| `cast_lands` | T401's prep recipe, then `/cast 1`, then the buff actually reaching `BuffManager` | the hand-read chat panel in T386/T401 |

The partner for `two_client_survive` is a **peer process**, launched by
`scripts/scenario.sh` running `zone_in` on the second account — the client
never forks a second client.

The state machines are pure logic over `IScenarioWorld`
(`include/client/scenario/scenario.h`), so
`tests/test_scenario_state_machines.cpp` runs all twenty cases with no client,
no server and no network. A scenario needing something new adds a method
there, and `ApplicationScenarioWorld` is the only implementation that touches
`EverQuest`. Trace goes to the `MOD_SCENARIO` log module, pinned to INFO by
the lane itself so a `--log-level` cannot silence the verdict.

### Video capture (temporal evidence — attack swings, casting, zoning, particles)

Stills can't show a temporal sequence. When the thing under review is
temporal, record video instead of burst-capturing screenshots and hoping a
frame lands.

```bash
# Dump numbered PNG frames while connected/zoned (render-thread hook, off by
# default — a normal run with no --dump-* flags touches none of this):
./build/bin/willeq -c willeq.json \
    --dump-frames-dir /tmp/capture --dump-every 1 --dump-max-frames 600

# Encode the frame directory into a reviewable mp4 with MEASURED (not
# assumed) fps:
bash scripts/ci/encode_capture.sh /tmp/capture <UNIT_ID> [output_dir]
```

- `--dump-frames-dir <dir> --dump-every <N>` (T64, extracted from the P12
  anim-video-check session): writes a PNG via the existing
  `IrrlichtRenderer::saveScreenshot()` path every Nth rendered frame, from
  `Application::render()` right after a successful `processFrame()`. Empty
  dir or `--dump-every 0` is a complete no-op — zero cost beyond one
  string-empty + int check.
- `--dump-max-frames <N>` (default 600) bounds total frames written — the
  Orange Pi's `/tmp` tmpfs is only ~242MB, and unbounded dumping is a
  footgun there (T61 already had to deploy stripped binaries to make room).
  Dumping stops with one log line once the cap is hit.
- `scripts/ci/encode_capture.sh` computes the **effective fps from the
  frames' own file mtimes** — never assumes 30/60 — so the video plays back
  at true elapsed wall-clock time. It also writes `<UNIT_ID>_capture.txt`
  next to the video with the measured fps/frame-count/real-time span; keep
  that file with the video, don't discard it.
- **Capture fps is an artifact of the capture mechanism, not a performance
  measurement.** Per-frame `glReadPixels` + PNG encode is expensive — P12
  measured ~4-7.6 effective fps on this x86 dev VM under llvmpipe, far below
  real-time. A viewer without that context will wrongly conclude the game
  runs at ~5fps. Never draw a performance conclusion from a capture video.
- Verify frames are real images before encoding — `saveScreenshot()` has a
  documented history of returning garbage under the surfaceless path (P10,
  P20); a video of garbage is worse than no video. See
  `refactor/T64_video_capture_extraction.md` and `review/T64/README.md` for
  the worked example (proof frames, verification method, deviations).

### Linux Capabilities

The client requires specific Linux capabilities for optimal network performance:

```bash
# Set capabilities on the binary (required for reliable zone loading)
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

**Why this is needed:**
- `CAP_NET_RAW`: Required for raw packet capture (diagnostic feature)
- `CAP_NET_ADMIN`: Required for `SO_RCVBUFFORCE` to set UDP socket receive buffer above kernel limit

**Background:** During zone loading, the EQ server sends a burst of 40+ packets at nearly the same instant. The default Linux socket buffer (~425KB) can overflow, causing packet loss and zone loading failures (typically hanging at 40%). The client uses `SO_RCVBUFFORCE` to request a 1MB buffer, which requires `CAP_NET_ADMIN`.

Without `CAP_NET_ADMIN`, the client falls back to the kernel maximum (`net.core.rmem_max`, typically 212KB), which may cause intermittent zone loading failures on busy servers.

### Orange Pi (ARM) Cross-Compilation

The client cross-compiles for Orange Pi One (Allwinner H3, ARMv7-A Cortex-A7, Mali 400 GPU).

**Current target platform**: Ubuntu Noble (24.04) with mainline kernel and Lima open-source driver.
- **GPU rendering**: Native OpenGL ES 2.0 via custom `COpenGLES2Driver` (extends Irrlicht's `CNullDriver`)
- **Display**: DRM/KMS direct rendering without X11 (`--drm` flag)
- **Build**: `docker/Dockerfile.arm-noble` + `scripts/build-arm-noble.sh` (sets `-DEQT_GLES2=ON`)
- **Output**: `build-arm-noble/bin/willeq` (ELF 32-bit ARM, ~10MB stripped)

```bash
# Cross-compile
bash scripts/build-arm-noble.sh

# Run on Orange Pi (DRM/KMS, GLES2 — the only GLES2 windowing path on ARM)
./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600

# Run the GLES2 path on the x86 dev VM (llvmpipe via EGL surfaceless; H01)
bash scripts/build-gles2-x86.sh
./build-gles2/bin/willeq -c config.json --renderer gles2
```

Note (H01 finding, 2026-07-23): there is NO X11+GLES2 path — the ARM Irrlicht
patch compiles only the DRM/FB device. On x86 the forked device
(`docker/irrlicht-drm-x86/`) falls back to Mesa's surfaceless EGL platform
(llvmpipe), which is how CI runs the real GLES2 driver headlessly.

**GLES2 rendering backend**: The Orange Pi uses a custom native GLES2 driver (`COpenGLES2Driver`) rather than Irrlicht's desktop OpenGL driver. This eliminates all desktop GL → GLES translation complexity, enables native ETC1 compressed textures (real 6:1 memory savings via `GL_ETC1_RGB8_OES`), and provides a shared rendering path for the planned Android 4.4 port. The driver uses 6 built-in GLSL ES 1.0 shader programs (no fixed-function pipeline). See `src/client/graphics/CLAUDE.md` for details.

**Legacy platforms**: (1) Noble with desktop GL 2.1 (Lima/Mesa) — retired (P32, `docs/decisions.md` 2026-07-28 addendum): `scripts/build-arm-noble.sh --no-gles2` no longer exists, GLES2 is the only ARM renderer. (2) Debian Jessie (kernel 3.4) with proprietary Mali blob and gl4es translation — fully superseded.

**DRM input handling**: The DRM device (`docker/irrlicht-drm/CIrrDeviceFB.cpp`) reads keyboard/mouse via Linux evdev (`/dev/input/event*`). VT keyboard processing is disabled via `KDSKBMODE(K_OFF)` to prevent the console layer from intercepting Ctrl+key combinations.

## Dependencies

Required: OpenSSL, zlib, fmt, GLM (header-only), jsoncpp
Optional:
- Boost (waypoint pathfinding)
- librecast-dev (navmesh pathfinding via Recast/Detour)
- A locally-built patched GLES2 Irrlicht (3D graphics rendering) — see "Build
  Commands" above. Not an apt package: `libirrlicht-dev`/`libxxf86vm-dev`
  (desktop-GL Irrlicht) are no longer used (P30).
- freerdp3-dev, libwinpr3-dev (native RDP streaming)
- Audio uses header-only libraries: miniaudio.h (output), tsf.h + tml.h (MIDI synthesis), dr_wav.h (WAV loading)

## Architecture

### Threading Model (Batch D)

The application runs two threads, decoupled via a thread-safe event/intent bridge:

```
Game Thread                         Render Thread (main)
───────────                         ────────────────────
Network events (TickNetwork)        forwardStagedEvents() → IrrlichtBridge
Game state updates (GameTick)       processFrame() (Irrlicht render)
stagePendingEvents() (T220/T227:    Loading thread management
  ONE GameState, EverQuest-owned,
  written+read on this thread)
Intent processing (bridge intents)
Audio updates                       Zone load completion detection
```

- **GameStateBridge** (`include/client/bridge/game_state_bridge.h`) — thread-safe queue pair using swap-vector pattern. Game thread pushes events (state changes), renderer pushes intents (user actions). D24 coalesces EntityMoved events per entity and PlayerPositionChanged intents.
- **IrrlichtBridge** (`include/client/bridge/irrlicht_bridge.h`) — translates game events into IrrlichtRenderer calls (switch over the 99 `GameEventType` enumerators (T264: every event now has a publisher and a consumer, pinned by test_reachability_sweep))
- **ConsoleBridge** (`include/client/bridge/console_bridge.h`) — logs events to console in headless mode
- **Event types** (`include/client/state/event_bus.h`) — 99 event types covering all game state changes
- **Intent types** (`include/client/events/renderer_intents.h`) — 37 intent structs in the `RendererIntent` variant (T264 pruned; T268 deleted DestroyAllLootIntent -- no Titanium destroy-loot packet; T328 added PlaySoundIntent and T419 RecipeDetailsRequestIntent; pinned by test_reachability_sweep) for user actions

**Key separation**: EverQuest communicates with the renderer only through the bridge — no `m_renderer` member, no `GetRenderer()` accessor. Application owns the renderer and wires it to EverQuest via bridge pointers. Player movement flows renderer→game exclusively via the `PlayerPositionChanged` intent (S2 removed the legacy synchronous callback); inventory UI state flows game→renderer exclusively via enriched `InventorySlotChanged` events; bridge payloads are typed (no `void*`). The graphics-facing game code is concentrated in `src/client/bridge/bridge_component.cpp` (the boundary TU); `src/client/core/everquest.cpp` itself has only two graphics includes (loading status + command registry). K16 closed the last structural seam: `InitGraphics` takes `EQT::Graphics::IZoneLoadSurface*` (`include/client/graphics/zone_load_surface.h`), the two-method zone-load surface `IrrlichtRenderer` implements, so game logic names no renderer class. The asset loaders still live in the `EQT::Graphics` namespace — that is naming only and was left deliberately (see the closed B006 in `refactor/BOARD.md`). Do not "fix" coupling ad hoc.

### Connection Flow

The client connects through three stages, each with its own connection manager:
1. **Login Server** (`m_login_connection_manager`) - Authenticates and gets server list
2. **World Server** (`m_world_connection_manager`) - Character selection and zone info
3. **Zone Server** (`m_zone_connection_manager`) - Actual gameplay

### Core Components

**Application class** (`include/client/application.h`, `src/client/application.cpp` + 4 `application_*.cpp` TUs, J06)
- Top-level orchestrator — owns renderer, bridge, game thread, loading thread
- Spawns game thread running EverQuest tick loop
- Main thread runs render loop (or sleeps in headless mode)
- Manages zone loading thread (GL context transfer, snapshot handoff)

**EverQuest class** (`include/client/eq.h`, implementation split across `src/client/core/everquest.cpp` + domain TUs under `src/client/core/`, `src/client/action/` and the extracted `src/client/<family>/` components, Batch K)
- Game state and network logic. `src/client/core/everquest.cpp` itself (K14: moved from the old flat `eq.cpp`) holds only the core: lifecycle, connection setup, the zone packet-receive callback. Everything else (the zone-stream dispatcher, zone entry, movement, entities, chat, merchant, bridge, …) lives in domain TUs — consult `docs/module_map.md` to find the right file
- Manages all three connection stages
- Handles entity tracking, movement, chat, combat, and doors
- All Titanium opcodes defined as enums: `TitaniumLoginOpcodes`, `TitaniumWorldOpcodes`, `TitaniumZoneOpcodes`
- Zero graphics dependencies — communicates with renderer only via GameStateBridge

**IrrlichtRenderer class** (`include/client/graphics/irrlicht_renderer.h`, `src/client/graphics/renderer/` + eleven component directories, Batch L)
- The renderer. Its own four TUs (`irrlicht_renderer.cpp`, `renderer_init.cpp`,
  `renderer_entities.cpp`, `renderer_event_receiver.cpp`) live in
  `src/client/graphics/renderer/`; everything else was extracted into eleven
  components, each `class EQT::Graphics::<Name>Component` with its own
  directory under `src/client/graphics/` and `include/client/graphics/`:
  **RdpComponent** (`rdp/`), **Eqe1ArchiveComponent** (`eqe1/`),
  **OverlaysComponent** (`overlays/`), **DiagnosticsComponent**
  (`diagnostics/`), **PlayerCameraComponent** (`player/`),
  **SlashCommandsComponent** (`slash/`), **SimBridgeComponent** (`sim/`),
  **ZoneStreamComponent** (`zone_stream/`), **ZoneLifecycleComponent**
  (`zone_lifecycle/`), **ZoneLoadComponent** (`zone_load/`) and
  **FrameComponent** (`frame/`)
- `EQT::Graphics::RendererContext` (`include/client/graphics/renderer_context.h`)
  is the renderer's `ClientContext`: the shared members bound by reference plus
  a pointer to each sibling component (`ctx_.Frame()`, `ctx_.ZoneLoad()`, …).
  A component reaches shared state as `ctx_.member`, a member OWNED by another
  component as `ctx_.<Owner>().<accessor>()` (L13's pairwise rule: a member two
  components share moves to the one that writes it), and anything still on the
  class as `owner_.member_`. L13 took that last handle from 1,065 uses to
  151 — 81 of them public methods and 70 in thirteen listed stays, none of
  which may move because each crosses a thread boundary or backs a lifetime
  contract. That number drifts with normal work: T366's census read 163, T371's
  read 165 and T388's reads **174** — re-measure, do not quote it. See the closed **B050**
- Lifecycle entry points (`initLoadingScreen`, `initDeferredGraphics`,
  `processFrame`, `shutdown`, `run`) stay on `IrrlichtRenderer`: `Application`
  and `IrrlichtBridge` must not learn a component exists
- Consult `docs/module_map.md` for which TU holds which body

**Network Stack** (`include/common/net/`, `src/common/net/`)
- `DaybreakConnection` - UDP reliable protocol with sequencing, fragmentation, CRC, and compression
- `Packet` - Base class with `StaticPacket` (fixed buffer) and `DynamicPacket` (resizable)
- `DaybreakConnectionManager` - Handles connection lifecycle via UdpTransport
- Socket receive buffer set to 1MB to handle server packet bursts during zone loading

**Pathfinding** (`include/client/pathfinder_*.h`)
- `IPathfinder` - Abstract interface
- `PathfinderNull` - Fallback (no pathfinding)
- `PathfinderNavMesh` - Detour navmesh (optional, requires EQT_HAS_NAVMESH)
- `PathfinderWaypoint` - Boost A* waypoint graph (optional, requires EQT_HAS_WAYPOINT)

**Maps** (`include/client/hc_map.h`, `include/client/raycast_mesh.h`)
- `HCMap` - Zone map loading (V1/V2 formats)
- `RaycastMesh` - AABB tree for collision/LOS checks

**Combat** (`include/client/combat.h`)
- `CombatManager` - Auto-attack, targeting, looting, combat abilities

**Trading** (`src/client/trade_manager.cpp`)
- `TradeManager` - Player-to-player trading, item/money exchange, trade state machine

**Spells** (`src/client/spell/`)
- `SpellManager` - Spell casting, gem state, cooldowns, memorization. The only
  one of these a player reaches: gem click / `/cast` -> `beginCastFromGem()` ->
  `validateCast()`. Its bodies live in six TUs (T407 split the 1,451-line
  `spell_manager.cpp` before it breached the ceiling) - see
  `docs/module_map.md` for which TU holds which method; the header's banners
  name the defining TU too.
- `SpellTypeProcessor` - **DELETED (T324, 2026-09-02)**. Client-side target
  resolution with zero callers; the server resolves spell targets and reports
  each landing.
- `SpellEffects` - **DELETED (T327, 2026-09-02)**, on Will's ruling. Effect
  application with zero callers once T324 removed its sole consumer; all 49
  per-effect handlers were logging stubs, and its value-scaling table was wrong
  against the server (recorded in `refactor/completed/deferred_archive_20260903.md`, item 45).
- Both deletions share one tombstone in `src/client/core/everquest.cpp`. Do not rebuild
  either; the server is authoritative and reports every landing per target.

**Skills** (`include/client/skill/`)
- `SkillManager` - Tracks player skills, handles activation, cooldowns
- `SkillData` - Skill properties (value, category, cooldown, requirements)
- `skill_constants.h` - Skill IDs, names, categories, animation mappings

### Packet Structures

Titanium-specific packet structures are defined in `include/common/packet_structs.h`. These are binary-compatible with the Titanium client protocol.

### Logging

**IMPORTANT**: All new logging code MUST follow the standards in `docs/standards/debug_logging_standards.md`.

Key points:
- Default (level 0/NONE): Quiet mode - errors still reported, no verbose output
- Seven levels: NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE
- Module-based filtering (NET, ENTITY, GRAPHICS, etc.)
- Runtime configurable via CLI, config file, or signals
- Output format: `[TIMESTAMP] [LEVEL] [MODULE] message`

Legacy logging in `include/common/logging.h` does not follow these standards and should be migrated when touching existing code.

## Test Organization

Tests are in `tests/` with one executable per test suite. Each test links only the minimal sources it needs:
- `test_packet.cpp` - Packet serialization
- `test_titanium_opcodes.cpp` - Opcode value verification
- `test_packet_structs.cpp` - Binary struct layout validation
- `test_integration_network.cpp` - Network protocol integration
- `test_formatted_message.cpp` - FormattedMessage parsing for NPC dialogue

Audio tests and graphics integration tests are documented in their respective CLAUDE.md files.

**Session replay oracle** (Batch R, `docs/plans/session_replay_oracle.md`): record a real play session with `--record-session data/captures/session.sleq`, then `test_session_replay` replays it into a headless EverQuest and asserts game state against a golden snapshot (self-created on first run). A synthetic-session mode keeps the test green without a recording. Protocol-level pcap tests (`test_daybreak_*`, `test_pcap_*`) skip unless a capture exists at `data/captures/session.pcap` (or `WILLEQ_PCAP`).

## Key Patterns

- **Bridge events/intents**: Game state changes are published as events via `m_bridge->pushEvent()`. Renderer user actions arrive as intents via `m_bridge->drainIntents()`. Never call renderer methods directly from EverQuest.
- **Zone loading**: Application creates a `ZoneLoadSnapshot` (immutable copy of game state), starts a loading thread that owns the GL context, builds GPU resources, then signals completion. Game thread continues processing network packets during loading.
- Zone connection requires specific packet sequencing - see `CheckZoneRequestPhaseComplete()` and the various `m_*_sent` / `m_*_received` flags
- Movement updates require `SendPositionUpdate()` to sync with server
- Entities tracked in `m_entities` map keyed by spawn_id
- Doors are tracked by `DoorStateManager`
  (`include/client/door_state_manager.h`, reached as
  `GameState::doors()`) — `doors_`, a `std::map<uint8_t, DoorState>` keyed by
  door id. There is no `m_doors` member on `EverQuest`. <!-- RETIRED-OK: naming the member that is gone is the point of this sentence --> Since T376 (B141) the
  same manager also owns the pending-click set (`pendingClicks_`,
  `addPendingClick()` / `isClickPending()`); the duplicate
  `EntitiesComponent::m_pending_door_clicks` and `ClearPendingDoorClicks()` <!-- RETIRED-OK: tombstone for the two names T376 deleted -->
  were deleted. `ZoneProcessMoveDoor` must ask `isClickPending()` BEFORE
  `setDoorState()` consumes the entry
- Friend class pattern: `CombatManager` and `TradeManager` are friends of
  `EverQuest` for internal access. **They are the only two** (T366, 2026-09-05,
  board B038 CLOSED): no extracted component is a friend and none holds an
  `EverQuest&` transition handle any more. A comment-filtered `owner_` grep
  under `src/client` / `include/client` returns **zero outside `graphics/`**;
  every `owner_` inside `graphics/` is a different pattern — Batch L's
  `IrrlichtRenderer& owner_` in the eleven renderer components (the
  RendererContext pattern) and a `ZoneShaderManager*` back-pointer in
  `zone_shader_callbacks.h`. A component reaches shared state as `ctx_.member`
  and a sibling family as `ctx_.<Family>().Foo()`; if something new needs
  `EverQuest`, it goes on `ClientContext` or a context service, not on a handle.
- **BSP tree** (`include/client/zone_bsp.h`): Zone spatial data shared between game state (water detection, zone lines) and renderer (PVS culling). No graphics dependencies.

## Configuration

**Main config** (willeq.json):
```json
{
    "eq_client_path": "/path/to/EverQuest"
}
```

**RDP configuration** (optional):
```json
{
    "eq_client_path": "/path/to/EverQuest",
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

**Spell effects configuration** (optional):
- `config/spell_effects.json` - JSON-configurable spell particle presets with hot-reload
- 3-tier hierarchy: global settings → per-style defaults → per-spell overrides
- Configures resist colors, spawn rates, lifetimes, motion types, blend modes

**User settings are NOT persisted at all** (T402, board B255). Nothing the
player changes at runtime — chat timestamps, panel visibility, chat
scrollback, UI colours — survives a restart: the static UI computes every
panel rect from the resolution (`include/client/graphics/ui/ui_layout.h`) and
no code writes a preferences file. The two files that looked like the
persistence, `config/chat_settings.json` and `config/ui_settings.json`, were
the DELETED `WindowManager`'s saved state (removed with it in `0f25cb57`,
"U07c2: Delete WindowManager + all old UI window files"); they described
draggable windows the client no longer has and were read by nothing, so T402
deleted them (board B245). Every shipped `config/*.json` that remains is
named by code, and `DataPathRouting.EveryShippedConfigFileIsNamedByCode`
keeps it that way.

Audio configuration is documented in `src/client/audio/CLAUDE.md`.

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON` in CMake). Requires EQ Titanium client files for zone geometry and character models. See `src/client/graphics/CLAUDE.md` for rendering details, and `include/client/graphics/ui/CLAUDE.md` for UI and key bindings.
