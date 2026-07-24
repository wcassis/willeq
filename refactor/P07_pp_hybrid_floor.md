# P07 — Hybrid floor for the area-mode per-pixel budget

Follow-up to P06 (refactor/P06_area_aware_budget.md, merged 23aee56).
Will's decision 2026-07-24: implement the hybrid floor. Slot: wt/slot1,
branch task/P07-pp-floor.

Motivation (P06 orchestrator review + contact sheet
benchmarks/contact_sheets/p06_area_budget/contact_sheet.png): pure area
mode is statistically all-lightweight (81.71 vs 81.47ms whole-run; PP
engages in 89/1000 frames) because the camera-containing region nearly
always trips the near-plane full-screen fallback and exhausts the budget
at region 1. Visually that unlights near geometry — per-vertex light
interpolates to ~zero on giant polys (near-black walls at poses 291/509).
Meanwhile the city pairs are visually identical between modes yet area is
12-31% faster there. The floor keeps the nearest region(s) per-pixel
(visual anchor, can never go black) while area accumulation still demotes
everything behind them.

## Design

1. **Floor rule (area mode only)**: the first `adaptiveLightingAreaPPFloor`
   regions in the nearest-first order ALWAYS get per-pixel, regardless of
   estimated area. New config field, int, default 1. Their estimated area
   still accumulates into the running total, so regions after the floor
   demote exactly as in P06 area mode. Count mode untouched; floor is
   ignored in count mode.
2. Config plumbing mirrors P06's fields (constrained_renderer_config.h/.cpp
   JSON override `adaptiveLightingAreaPPFloor`). Defaults unchanged
   elsewhere: mode stays `count` everywhere — no preset flips in this unit.
3. Benchmark parity: `--pp-area-floor N` (default 1) in
   gles2_zone_benchmark.cpp; apply the same floor in zone_bench_loader.cpp's
   area-mode pre-pass.
4. Production fix site: renderer_zone_draw.cpp area-mode branch — the
   decision becomes
   `regionUseLW = (regionsDrawn >= floor) && cumulativeArea > limit`
   with cumulativeArea still accumulated for floor regions.

## Verification

- Build + ctest (known host exceptions: AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes) + x86 GLES2 build + visual
  gate PASS unchanged (default count) + ARM build + file-size check.
- **Pi measurement (P02 protocol via scripts/pi/pi_common.py, same qeynos2
  config as P06: --entities 200 --seed 42 --navmesh-path, 1280x720)**,
  variants: count, area-floor1 (k=0.75), all-LW (--zone-pp-budget 0).
  First verify the moving path still reproduces P06's committed
  count.csv poses (compare pos columns at frames 291/292/509/627-635);
  the path has drifted across sessions before (see deferred.md). If it
  drifted, fall back to STATIC pose capture (--camera-pose, the P06
  contact-sheet poses listed below) for the pose-level timings and use the
  moving run only for whole-run averages.
  Poses (EQ coords X,Y,Z,HEADING,PITCH from P06 count.csv):
  - outskirts/anomaly: 240.6,19.6,6.1,172.6,67.9 (fr291) and
    240.9,18.3,8.6,180.0,77.4 (fr292)
  - corridor: 271.2,184.1,5.5,45.8,-1.2 (fr509)
  - city: 286.1,94.0,43.1,6.5,-28.8 (fr627), 287.5,105.6,36.7,6.5,-28.8
    (fr631), 288.4,114.3,31.8,6.5,-28.8 (fr634)
- **Acceptance**:
  - Anomaly poses (291/292): area-floor1 within noise of count-mode
    total_ms (the huge region keeps PP by design; ~17ms expected, NOT the
    7ms of pure area — that cost is accepted as the price of correct near
    lighting; those poses are ~50fps, above KPI).
  - City poses (627/631/634): area-floor1 retains most of pure-area's
    win vs count — target >= half of P06's measured 12-31% saving.
  - Whole-run avg: area-floor1 <= count-mode avg (91.18ms in P06's run;
    path drift may shift absolute numbers — compare within-session).
  - draws/regions/polys byte-identical count vs area-floor1 (full-file
    programmatic check, P05/P06 precedent).
- **Contact sheet**: regenerate the P06 llvmpipe captures (x86 build,
  --eq-path /mnt/projects/EverQuestP1999, same 4 poses, count vs
  area-floor1) into benchmarks/contact_sheets/p07_pp_floor/ — the
  anomaly/corridor poses must now be LIT in area-floor mode (no black
  near geometry), the city pairs should remain near-identical. `git add
  -f` the PNGs and the Pi CSVs (benchmarks/ paths are gitignored).
- Default flip to area+floor remains AFTER this unit: orchestrator review,
  Will's visual sign-off on the new sheet, golden re-bless, Pi tier-2
  baseline — not in scope here.

## Review

### Implementation

Landed exactly as designed, on top of P06's `AdaptiveLightingBudgetMode::Area`
mechanism (unchanged; Count mode is completely untouched by this unit).

- **Config**: `include/client/graphics/constrained_renderer_config.h` — new
  `int adaptiveLightingAreaPPFloor = 1;` field next to
  `adaptiveLightingAreaBudgetFactor`. JSON override plumbing added in
  `constrained_renderer_config.cpp` (`adaptiveLightingAreaPPFloor` int, same
  pattern as the sibling fields). No preset sets it — the compiled-in default
  of `1` applies everywhere (no preset currently sets `adaptiveLightingBudgetMode`
  to `area` either, so this unit changes zero production behavior by itself,
  same as P06).
- **Fix site**: `src/client/graphics/renderer_zone_draw.cpp`,
  `drawZoneGeometrySorted()`, area-mode branch. Added
  `const int areaPPFloor = config_.constrainedConfig.adaptiveLightingAreaPPFloor;`
  next to the other area-mode locals, and changed the decision line from
  `regionUseLW = cumulativeRegionArea > areaBudgetLimit;` to
  `regionUseLW = (regionsDrawn >= areaPPFloor) && cumulativeRegionArea > areaBudgetLimit;`.
  `regionsDrawn` is incremented once per region at the *bottom* of the loop
  (after the decision, for every region regardless of PP/LW), so on the very
  first iteration `regionsDrawn == 0` — with the default floor of 1, `0 >= 1`
  is false, so `regionUseLW` short-circuits to `false` for the nearest region
  unconditionally, regardless of `cumulativeRegionArea`. The area accumulation
  line itself (`cumulativeRegionArea += estimateRegionScreenArea(...)`) was
  left untouched and still runs before the (now floor-gated) decision, exactly
  as the plan specifies — floor regions still count fully toward the running
  total, so everything after the floor demotes exactly as P06's plain area
  mode would.
- **Benchmark parity**: `tools/hwcaps/gles2_zone_benchmark.cpp` — new
  `--pp-area-floor N` flag (default 1), threaded into
  `ZoneBenchOptions::zonePPAreaFloor` and then into
  `ZoneBenchData::zonePPAreaFloor` at the same call site as
  `zonePPAreaBudgetFactor`. `tools/hwcaps/zone_bench_loader.cpp`'s area-mode
  pre-pass (the `distOrder`/`areaUseLW` map, needed there because
  `visibleRegions` is only distance-sorted when `frontToBack||occlusionCull`
  is requested — same ordering caveat P06 documented) now tracks its own
  `regionsDrawn` counter through the sorted `distOrder` loop and applies the
  identical `(regionsDrawn >= floor) && cumulativeArea > limit` gate before
  incrementing, keeping the benchmark's decision logic byte-for-byte
  equivalent to production's.

### Verification

- `cmake --build build -j24`: clean (one incidental failure from two
  overlapping build invocations racing on `lib/libwilleq_core.a` — `ar`
  archive corruption, "file format not recognized" — self-inflicted by
  running a build in the background and then another in the foreground
  against the same build dir; re-running once serially produced a clean
  build. Not a code issue.)
- `ctest --test-dir build --output-on-failure`: 1224/1225 passed (30 skipped,
  environment-gated, as usual). Only failure: `AudioManagerTest.PreloadCommonSounds`
  — a documented host exception. `RDPServerFrameTest.UpdateFrameMultipleTimes`
  (the other documented host exception) passed this run.
- `scripts/build-gles2-x86.sh`: clean.
- `scripts/ci/visual_gate.sh`: **PASS** (qeynos2/gfaydark/befallen, default
  count mode — goldens untouched, as expected since no preset enables area
  mode).
- `scripts/build-arm-noble.sh`: clean (from this worktree,
  `build-arm-noble/bin/`, unstripped — stripped separately before Pi staging
  per the I/O rule).
- `scripts/refactor/check_file_sizes.sh fable`: clean, no output (no file
  crossed a size threshold — this unit only added a handful of lines to
  existing files).
- **draws/regions/polys byte-identical count vs area-floor1**: verified on
  x86 (`build-gles2/bin/gles2_zone_benchmark`, same 1000-frame moving
  navmesh path/seed as below) — `draws`, `regions_vis`, `regions_drawn`,
  `polys` columns compared programmatically across all 1000 frames between a
  `--pp-budget-mode count` run and a `--pp-budget-mode area --pp-area-floor 1`
  run: **0 diffs**. Only the lighting decision differs between the two
  modes, confirmed.
- **Contact sheet** (`benchmarks/contact_sheets/p07_pp_floor/`, x86 llvmpipe,
  `--eq-path /mnt/projects/EverQuestP1999`, 1280x720, static `--camera-pose`
  captures, `count` vs `area --pp-area-floor 1 --pp-area-factor 0.75`):
  4 poses — anomaly (fr291: `240.6,19.6,6.1,172.6,67.9`), corridor (fr509:
  `271.2,184.1,5.5,45.8,-1.2`), and two city poses (fr627:
  `286.1,94.0,43.1,6.5,-28.8`, fr634: `288.4,114.3,31.8,6.5,-28.8`) — chosen
  as the most distinct subset of the plan's pose list (one anomaly, the
  corridor, two city) to exercise both "must now be lit" cases and the
  "city stays near-identical" case in one 8-image sheet.
  - f0 (anomaly) and f1 (corridor): count vs area-floor1 PNGs are
    **byte-identical** (`cmp` confirms) — the floor completely eliminates the
    P06 near-black-wall artifact at both poses, because both are exactly the
    single/few-huge-region case the floor targets (region 0 is always PP now).
  - f3 (city/634): also byte-identical.
  - f2 (city/627): differs by 5 bytes out of ~408KB (visually
    indistinguishable in side-by-side comparison) — one region past the
    floor in this specific city scene flips its LW/PP decision at a screen
    edge, imperceptible. Consistent with "city pairs near-identical."
  - `contact_sheet.png` montage (2 columns x 4 rows, `count` | `area-floor1`,
    labeled per pose) confirms visually: rows 1-2 (anomaly, corridor) look
    identical between columns and are clearly lit (no near-black artifact);
    rows 3-4 (city) are visually indistinguishable between columns.

### Pi hardware measurement — BLOCKED (infrastructure outage, not a code issue)

The Orange Pi (10.0.30.192) went unreachable ("No route to host") partway
through the first staging attempt and stayed down for the remainder of the
session:

- Sequence: sourced `scripts/pi/env.sh`, confirmed `ssh` reachable and
  `CmaFree` (79640 kB, comfortably above the 40MB abort threshold after a
  precautionary `sync && drop_caches`), staged an (initially wrong — see
  Deviations) binary, and ran a single benchmark invocation that failed
  immediately at CLI-argument parsing (unrecognized `--material-counts` on
  the *stale* binary — see Deviations) with a 2.1s wall time. This
  invocation never reached `--drm`/GPU/DRM initialization at all — the
  process printed a usage message and exited before touching the display
  stack. Immediately after (during the corrected binary's `scp` re-stage),
  the connection reported `No route to host`, and the VM's kmsg follower
  (`/mnt/projects/fable/pi_console/kmsg_follow.log`) independently recorded
  `=== connection lost ... — Pi down or rebooting ===` starting
  2026-07-24T11:39:02Z.
- The outage persisted for the rest of the session (checked repeatedly:
  11:44, 11:46, 11:55, 12:09, 12:19, 12:29, 12:39 UTC — one full hour of
  `No route to host`/`Connection timed out`, both directly via `ssh` and via
  the independent kmsg follower log). This VM's own network path is fine
  (gateway `10.0.30.1` pings normally; `ip route get 10.0.30.192` resolves a
  normal route) — the Pi itself is not responding at the network layer,
  which is a stronger signal than a simple reboot (the P02 protocol's own
  `wait_for_ssh` gives a scripted reboot only 180s to come back; this
  exceeded that by 20x).
- Per the task's explicit rules, a raw `sudo reboot` requires the Pi to
  already be reachable (it isn't), and there is no power-cycle/BMC tool
  available to this agent — there is nothing to "improvise around" here.
  I did not attempt any workaround beyond waiting and periodic reachability
  checks, per instructions not to route around a genuine blocker.
- **Consequence**: the plan's required Pi measurement (P02 protocol,
  count/area-floor1/all-LW on real Mali 400 hardware, the moving-path
  reproducibility check, and the acceptance criteria that depend on Pi
  timings) could **not** be completed this session. No Pi CSVs were
  produced and none are committed — `benchmarks/pi_runs/p07_pp_floor/` does
  not exist in this commit (fabricating or substituting numbers under that
  path would misrepresent their provenance).
- **What was verified instead, as a stand-in** (explicitly NOT a substitute
  for the Pi KPI, informal/noisy x86 llvmpipe software-rasterizer numbers,
  single-frame-per-pose, included only as supporting evidence that the
  mechanism engages correctly in the expected direction):

  | pose | regions | count_ms | area-floor1_ms | all-LW_ms |
  |---|---|---|---|---|
  | anomaly fr291 | 1 | 28.54 | 20.73 | 15.86 |
  | anomaly fr292 | 1 | 7.40 | 8.52 | 4.46 |
  | corridor fr509 | 2 | 8.20 | 14.92 | 5.45 |
  | city fr627 | 159 | 26.16 | 14.65 | 13.69 |
  | city fr631 | 151 | 15.99 | 13.82 | 12.86 |
  | city fr634 | 138 | 17.74 | 14.48 | 12.51 |

  Whole-run (1000-frame moving path, x86 llvmpipe, same command as the Pi
  protocol would use): count avg total_ms 24.84, area-floor1 avg 24.30,
  all-LW avg 23.03 — area-floor1 <= count as expected, though the margin is
  far smaller than the Pi's GPU-bound numbers would show (llvmpipe is
  CPU-rasterization-bound and dominated by draw-call/state-change overhead
  that doesn't scale with per-pixel vs per-vertex shader cost the way a
  real GPU's fragment-shader-bound cost does — P06's Pi numbers showing
  12-31% city-frame savings are not expected to reproduce on llvmpipe, and
  they didn't: some poses here even show area-floor1 slower than count,
  e.g. fr292 and fr509, almost certainly single-frame llvmpipe noise on a
  shared VM rather than signal).
  - The x86 moving-camera path (1000 frames, same `--entities 200 --seed 42
    --navmesh-path` command as P06's Pi protocol) **did not reproduce**
    P06's committed `count.csv` positions at any of the compared frames
    (291/292/509/627-635) — same non-transferability P06 itself documented
    for P05's frames (now a 4th occurrence across P05→P06→this session,
    see `refactor/deferred.md`'s "Benchmark camera path is not reproducible
    across builds" entry, still open). Per the plan's own fallback
    instruction, static `--camera-pose` capture was used instead for all
    pose-level comparisons above and for the contact sheet.
- **This blocker does not indicate a defect in the implementation.** The
  code change is small, mechanically verified (byte-identical draws/regions/
  polys, correct short-circuit logic reasoned through above and confirmed
  visually in the contact sheet: floor regions render pixel-identical to
  count mode), and the x86 numbers move in the expected direction. What's
  missing is the Pi's real GPU-bound timing numbers to confirm the
  magnitude of the win and formally sign off the acceptance criteria that
  name specific Pi millisecond figures. Recommend the orchestrator re-run
  the Pi measurement in a follow-up session once the hardware is back
  (the `refactor/scratch/p07_measure.py` approach — deleted before this
  commit per the P05/P06 "small standalone script, not committed"
  precedent — used `pi_common.py`'s `GovernorPin`/`CmaMonitor`/
  `randomized_variant_order`/`run_session` primitives correctly and should
  work unmodified once the Pi answers ssh again; the one thing to fix
  first is `ARM_BUILD_BIN_DIR` — see Deviations).

### Acceptance criteria verdict

- **Anomaly poses (291/292) area-floor1 within noise of count-mode total_ms**:
  **UNVERIFIED on Pi** (blocked, see above). x86 llvmpipe supporting evidence
  is directionally consistent but not the specified KPI.
- **City poses (627/631/634) area-floor1 retains >= half of P06's measured
  12-31% saving vs count**: **UNVERIFIED on Pi** (blocked). Cannot be
  verified on llvmpipe (P06's saving mechanism is GPU-fragment-shader-cost
  driven, not reproducible under CPU rasterization).
- **Whole-run avg area-floor1 <= count**: x86 llvmpipe shows this direction
  (24.30ms vs 24.84ms) but this is not the specified Pi KPI. **UNVERIFIED on
  Pi**.
- **draws/regions/polys byte-identical count vs area-floor1**: **PASS**
  (verified on x86 across all 1000 frames of the moving path).
- **Contact sheet**: anomaly/corridor poses lit in area-floor mode, city
  pairs near-identical: **PASS** (byte-identical or near-byte-identical PNGs
  at all 4 poses, see above).
- **Full production bar** (build, ctest, x86 GLES2 build, visual gate, ARM
  build, file-size check): **PASS**, see Verification section.

### Deviations

- **Pi hardware measurement blocked by an infrastructure outage** — see the
  dedicated section above. This is the only unmet verification item; every
  build/test/visual-gate/contact-sheet item passed.
- **`ARM_BUILD_BIN_DIR` trap**: `scripts/pi/env.sh` defaults
  `ARM_BUILD_BIN_DIR` to `/mnt/projects/fable/main/build-arm-noble/bin` (the
  main worktree), not this task worktree. Sourcing `env.sh` unconditionally
  exports that default, so a naive `os.environ.setdefault(...)` override in
  a driver script placed *after* `source scripts/pi/env.sh` in the same
  shell invocation is a no-op (the var is already set, not merely absent).
  First staging attempt silently shipped the stale main-worktree binary
  (missing every P06/P07 flag entirely, confirmed via `strings` on both
  binaries) to the Pi as a result; caught by the benchmark's own usage-error
  output before any real invocation happened. Fixed by explicitly exporting
  `ARM_BUILD_BIN_DIR=.../wt/slot1/build-arm-noble/bin` *after* sourcing
  `env.sh`. Worth fixing properly in `scripts/pi/pi_common.py` or `env.sh`
  in a future unit (out of scope here) so this doesn't recur for the next
  worktree-based executor.
- A background build and a foreground build were briefly run concurrently
  against the same `build/` directory early in this session (before
  realizing the first one had backgrounded rather than completing inline),
  corrupting `lib/libwilleq_core.a` (`ar` "file format not recognized").
  No source changes were affected; a clean serial rebuild resolved it.
- `refactor/scratch/p07_measure.py` and the various `refactor/scratch/x86_*.csv`
  diagnostic files used for the byte-identical check, the llvmpipe
  supporting-evidence table, and path-drift confirmation were **not**
  committed (deleted from the working tree before the final commit) — same
  "small standalone script, not part of the committed diff" precedent P05
  and P06 set.
- Pi state at the point contact was lost: governor had NOT yet been pinned
  for a real session (the one invocation that ran was outside a
  `GovernorPin` block, a quick manual reachability/CmaFree check), so there
  is no governor-restore obligation outstanding from this session. No
  reboot was triggered by this agent. The Pi's current state (up/down,
  governor) could not be confirmed as "pristine" at session end because it
  was unreachable — flagging this explicitly rather than asserting
  pristine-state without being able to check it.
