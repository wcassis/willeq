# P33 — Wire tree wind animation (Will's ruling: trees move)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

Will's ruling (2026-07-28, recorded in deferred.md): "trees are
supposed to move. the only time when they should not animate is if the
tree wind animation setting is disabled." P32's finding: 
`TreeWindController::update()` — the only code advancing wind time and
the weather-multiplier transition — is unreachable on the live GLES2
path (its old call site was the deleted CPU-instancing branch), so
wind has been frozen since the GLES2 backend shipped. Fix it.

## Work

1. **Find the setting.** Locate the tree-wind animation toggle Will
   refers to (grep config/display settings/constrained config for
   wind/tree animation keys; check what the wind shader family +
   TreeWindController already consult). If a real user-facing toggle
   exists, gate on it. If NONE exists, add one (config +
   display_settings.json, default ON) following the existing settings
   conventions — and say so plainly in the Review.
2. **Wire `TreeWindController::update()` into the render loop** at the
   cadence the controller was designed for (per-frame with deltaTime —
   check its API and the weather-multiplier transition logic), gated
   on the setting. Wind time must advance DETERMINISTICALLY from frame
   deltas, not wall-clock reads inside the controller, wherever the
   choice exists — the benchmark/gate depends on run-to-run
   determinism.
3. **Keep the golden/gate path deterministic and unchanged.** The
   visual gate's frames must remain byte-identical run-to-run and
   IDENTICAL TO CURRENT GOLDENS: pin wind in the benchmark/golden path
   (wind time held at its current frozen value — i.e. the gate renders
   exactly what it renders today). Do not re-bless; if you cannot keep
   the gate byte-stable, STOP and report.
4. **Evidence for Will's bless (production-visible change)**: live
   eqemu session, fixed camera on a tree cluster (gfaydark nekpine or
   qeynos2 gates — P20b's scenes), time-lapse stills at fixed
   intervals (e.g. t+0/2/4/6s, same pose by construction) showing
   branch/canopy movement between frames, PLUS the same sequence with
   the setting OFF showing stillness. Same-pose discipline per the
   P20b evidence bar. Stills to
   `/mnt/projects/fable/videos/p33_tree_wind/` + committed manifest
   under `benchmarks/audits/p33_tree_wind/`. You do NOT declare
   visual acceptability — Will judges.
5. Sanity: confirm the weather-multiplier transition now actually
   transitions (log evidence or a targeted unit test on
   TreeWindController if cheap).

## Rules

- Behavior scope: ONLY wind time/weather-multiplier animation becomes
  live; classification, materials, budgets untouched.
- Perf note: the wind vertex shaders already run (P20b) — this adds
  one CPU update call per frame. If your implementation adds anything
  heavier per-frame, STOP and reconsider.
- Territory: TreeWindController + the frame-loop call site
  (renderer_frame.cpp), the settings plumbing, benchmark wind-pinning
  if needed, evidence dirs. No shader changes, no zone loaders, no
  driver.
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown,
  lock removed).

## Verification

- Full current bar at baselines: GLES2 tree 1360/1361 (ALSA only),
  headless 939/940, `build-gles2-x86.sh` clean, visual gate PASS with
  goldens BYTE-IDENTICAL (this is the hard proof of step 3), ARM
  build clean, file sizes clean.
- Gate determinism double-run (two consecutive gate runs, both PASS).
- The step-4 evidence pairs (wind on: movement between stills; wind
  off: stillness), same pose by construction, manifest committed.

## Review

**The setting**: already exists, no addition needed. `display_settings.json`
`"animatedTrees": true` (default ON) -> `DisplaySettings::animatedTrees`
(`include/client/graphics/display_settings.h:31`) ->
`irrlicht_renderer.cpp:130` (`env.get("animatedTrees", true)`) ->
`applyEnvironmentalDisplaySettings()` (`irrlicht_renderer.cpp:1970`,
called once per zone load from `renderer_zone_load.cpp` Step 11) ->
`treeWindController_.setEnabled(settings.animatedTrees)`. This is exactly
Will's "tree wind animation setting."

**Finding that reframes the unit**: P32's finding described
`TreeWindController::time_`/`weatherMultiplier_` (fed only into the dead
CPU-instancing `getDisplacement()` path) as "the" wind time, and concluded
wind was frozen since the GLES2 backend shipped. That's true for
`TreeWindController::time_`, but the actual GPU vertex-shader wind that
trees are drawn with (`zone_shader.cpp`'s `uWindTime`/`uWindParams`,
assigned to tree meshes unconditionally at zone-load in
`renderer_zone_stream.cpp:562-609`, out of this unit's territory and
untouched) is driven by a SEPARATE accumulator,
`IrrlichtRenderer::windTime_` (`irrlicht_renderer.h:1571`), which was
already incrementing unconditionally every render frame
(`renderer_frame.cpp` `processFrameRender`, pre-P33) regardless of the
`animatedTrees` setting or `TreeWindController::isEnabled()` — nothing
gated it. So the real live-production bug was the inverse of the plan's
mental model: trees were already animating whenever wind-material
assignment succeeded, with **no way to turn animation off** via the
setting at all (`treeWindController_.isEnabled()` was checked nowhere in
the render path before this unit — only in `renderer_sim_bridge.cpp:300`,
an unrelated SimulationWorker snapshot). Confirmed by grep across
`src/client/graphics/*.cpp`.

**Wiring** (`src/client/graphics/renderer_frame.cpp`, the only file
touched):
1. Tier-2 block (`processFrameSimulation`, ~20Hz cadence): restored
   `treeWindController_.update(deltaTime)`, now gated on
   `treeWindController_.isEnabled()` instead of the dead
   `getAnimatedTreeCount() > 0` P32 removed. This is what actually makes
   the weather-multiplier transition (`setWeather` -> `targetMultiplier_`
   -> `updateWeatherTransition()`) live — see Sanity below.
2. `processFrameRender`'s wind-uniform block: `weatherMult` is now
   `treeWindController_.isEnabled() ? treeWindController_.getWeatherMultiplier() : 0.0f`.
   Traced the GLES2 vertex shader (`zone_shader.cpp:585-590`):
   `strength = baseStr + gustStr * gust; pos.x += influence * strength *
   sway; pos.z += influence * strength * sin(...)` — no other term. With
   `weatherMult = 0`, both `baseStr` and `gustStr` (each `0.3f`/`0.5f`
   scaled by `weatherMult`) are exactly `0.0`, so `strength` is exactly
   `0.0` and displacement is exactly `0.0` — a true rest pose, not a
   frozen-but-nonzero bent pose. `windTime_` itself keeps incrementing
   either way (cheap, and avoids a phase discontinuity if the setting is
   re-enabled mid-session); it has zero visual effect while strength is 0.
   No shader changes — this is a pure C++ value computed before the
   existing `setWindParams()` call.

**Determinism**: `deltaTime` into both call sites is the real per-frame
delta passed down from the caller chain (`Application` -> `processFrame`),
never a wall-clock read inside `TreeWindController` itself — `update()`'s
only time source is its `deltaTime` parameter. Same as the pre-existing
`windTime_ += deltaTime` pattern. Frame-to-frame reproducibility for a
given input delta sequence is unchanged by this unit.

**Gate byte-stability proof**: `tools/hwcaps/gles2_zone_benchmark.cpp` (the
binary `scripts/ci/visual_gate.py` drives) never calls into
`IrrlichtRenderer` at all — confirmed by grep, zero references to
`TreeWindController`, `TreeIdentifier`, `setWindTime`, or `setWindParams`
anywhere under `tools/hwcaps/`. It drives `ZoneShaderManager` directly and
never calls `setWindTime`/`setWindParams`, so `windTime_`/`uWindParams`
inside that binary stay at their struct defaults (`windTime_ = 0.0f`,
`windParams_ = {0.3f, 0.4f, 0.5f, 0.1f}`, `zone_shader.h:280-281`) —
"pinned" by construction, untouched by this unit's changes. No pinning code
was needed. Verified empirically: two consecutive `scripts/ci/visual_gate.py`
runs (no `--bless`) both PASS (qeynos2/gfaydark/befallen), `git status`
shows zero golden-image changes — determinism + byte-identity to committed
goldens both hold.

**Weather-transition sanity**: with the tier-2 `update()` call restored
and gated on `isEnabled()`, `TreeWindController::updateWeatherTransition()`
(unit-testable, pure function of `deltaTime`/`targetMultiplier_`) now
executes on the live path whenever the setting is enabled, so a
`setWeather()` call (already wired unconditionally from
`irrlicht_renderer.cpp`'s `WeatherSystem` callback) will visibly ramp
`weatherMultiplier_` toward the new target at `transitionSpeed_` (0.5/s)
instead of it being permanently stuck at whatever it initialized to. No
existing unit test covers `TreeWindController` directly (none added — out
of the file-size/territory budget for this unit and not requested); this
is a code-path-reachability finding, not independently measured at runtime
(the live-eqemu weather-transition observation planned for step 5 could not
be captured — same blocker as the stills, see the manifest).

**Evidence**: BLOCKED this session by a pre-existing eqemu environment bug
(static-zone-fleet port collision on `gfaydark` autobootup, causing an
infinite login/world/zone-transfer retry loop before any P33 code runs) —
fully root-caused and documented in
`benchmarks/audits/p33_tree_wind/EVIDENCE_MANIFEST.txt`. No stills exist.
Will cannot bless the visual change from this dispatch.

**Verification bar**:

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1376/1377 passed; only `AudioManagerTest.PreloadCommonSounds` failed (documented known host exception; `RDPServerFrameTest.UpdateFrameMultipleTimes` did not flake this run) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | `python3 scripts/ci/visual_gate.py` (no `--bless`) run TWICE consecutively -> PASS both times (qeynos2/gfaydark/befallen), goldens untouched (`git status` clean for all golden paths) — byte-stability + determinism both proven |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/refactor/check_file_sizes.sh fable` | No new FAILs. `src/client/eq.cpp` FAIL (3697 lines) is pre-existing, untouched by this unit. `renderer_frame.cpp` WARN (1638 lines, >1500) was ALREADY a WARN on `fable` tip before this unit (1623 lines) — this unit's +15 net lines did not newly cross the threshold. `include/client/eq.h` and `eq_entities.cpp` WARNs also pre-existing, untouched. |

**eqemu / server end state**: `scripts/eqemu/down.sh` run, confirmed clean
via `status.sh` (no containers, ports 5998/9000 closed). Testchar restored
via direct DB `UPDATE character_data SET zone_id=36, x=-77.9232, y=21.9086,
z=3, heading=190 WHERE name='Testchar'` (verified by re-SELECT) — live
sessions never stabilized long enough for GM `#goto`/`#setheading` to be a
viable restore path (same fallback P20b/P25 document).
`/mnt/projects/fable/.eqemu-lock` removed.

**Deviations**:
1. A local, uncommitted evidence-capture workaround (degrade the
   missing-race=330-model `LOG_FATAL`-abort to `LOG_WARN`-and-skip in
   `loadZoneStep8a_BaseModelPrep()`, `renderer_zone_load.cpp`) was applied
   during diagnosis to make a live gfaydark session possible at all, then
   fully reverted before the final build/commit — `git diff --stat`
   contains only `renderer_frame.cpp`. Matches P20b's own precedent
   exactly (EXECUTOR_PROTOCOL.md "parked live-client issues").
2. Evidence (step 4 of the plan's Work list, and the weather-transition
   live observation in step 5) was NOT captured — blocked by a
   pre-existing eqemu-side environment bug (root-caused above and in the
   committed manifest), unrelated to and unreachable from this unit's code
   changes. This is a STOP-and-report per EXECUTOR_PROTOCOL.md rather than
   a silent gap: the code change is complete, reasoned, and passes the
   full non-visual verification bar, but Will cannot bless the visual
   behavior from this dispatch alone.

**P33b evidence addendum (2026-07-28)**: a follow-up errand (task/P33b-
wind-evidence, evidence-only, no code changes to this unit's committed
diff) captured the step-4 stills this dispatch could not, using qeynos2
instead of gfaydark (per P20b/P21/P29's proven zone-in path: login ->
befallen -> GM `#zone qeynos2`) — sidestepping the gfaydark port-collision
bug documented above entirely rather than fixing it. 8 same-pose-within-
series time-lapse stills (wind ON t+0/2/4/6s, wind OFF t+0/2/4/6s) are on
NFS at `/mnt/projects/fable/videos/p33_tree_wind/stills/` (p33b_ prefix);
full details, a newly-found-and-reverted client-side crash workaround
(`ParticleManager::renderUnified()` dangling-`atlasTexture_` SIGSEGV,
unrelated to tree wind, NOT fixed, only worked around for the capture
session and fully reverted before commit), and per-pair pixel-diff
numbers (both whole-frame and a canopy-only crop that isolates wind
motion from chat-text/HUD/character-animation noise) are in the
"P33b — evidence capture SUCCEEDED via qeynos2" section of
`benchmarks/audits/p33_tree_wind/EVIDENCE_MANIFEST.txt`. Headline result:
the wind-ON canopy crop changes 30-38% of pixels by >15/255 between any
two of the four frames; the wind-OFF canopy crop changes 0% of pixels by
>15/255 across all pairs (max absolute diff 4/255, i.e. true rest pose).
Will still needs to eyeball the actual PNGs before blessing — the pixel
diffs are supporting evidence, not a self-declaration of visual
acceptability.

**P33c video addendum (2026-07-28)**: a further follow-up errand
(task/P33c-wind-video, evidence-only, no code changes to this unit's
committed diff) captured wind ON/OFF as VIDEO rather than stills, per
Will's request that motion "reads much better" that way. Same qeynos2
gates-trees scene and capture convention as P33b (`up.sh qeynos2`, login
-> befallen -> `#zone qeynos2` -> `#time 12` -> fixed `/turn 180`, then a
real-time recording window instead of discrete `/screenshot`s), using a
new LOCAL/UNCOMMITTED `--dump-frames-dir`/`--dump-every` capture flag
(reimplemented fresh against current `fable`, not cherry-picked from the
stale unmerged `task/anim-video-capture` reference branch) plus the same
`ParticleManager::renderUnified()` workaround P33b needed for live
qeynos2 stability — both fully reverted before commit (`git status`
clean, confirmed by a post-revert rebuild). Two videos at
`/mnt/projects/fable/videos/p33_tree_wind/`: `wind_on.mp4` (151 frames,
55.47s, 2.7222fps, `animatedTrees: true` shipped default) and
`wind_off.mp4` (85 frames, 25.17s, 3.3770fps, `animatedTrees: false` via
a session-local config copy — committed config untouched). Both fps
values are measured (from frame mtimes), not assumed, and playback is
real-time (not time-lapsed). Own verification (frame-count/duration via
`ffprobe`, canopy-crop pixel diffs on spot-extracted and raw frames) shows
ON consistently nonzero motion (2-6% of the canopy crop changing by
>15/255 across various frame separations) and OFF consistently at the
~4/255 encoder/readback noise floor — same direction as P33b's stills but
smaller magnitude (full analysis and an honest discussion of why in
`benchmarks/audits/p33_tree_wind/EVIDENCE_MANIFEST.txt`'s "P33c — video
capture" section). Will still needs to watch the actual mp4s before
blessing — pixel diffs over sparse frame samples understate what
continuous playback shows, which is the whole point of using video here.
