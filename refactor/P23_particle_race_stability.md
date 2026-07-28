# P23 — ParticleManager race real fix + llvmpipe stability items

**Slot: slot3. x86 ONLY — no Pi, no eqemu.** Siblings: P21 (slot1,
doors/entities + eqemu), P22 (slot2, GLES2 driver) — territories
disjoint; respect them. You have NO live-session harness this unit —
verify with tests/stress, not eqemu.

Two long-parked stability items from the video-capture era
(deferred.md, "x86 llvmpipe client stability needs a pass"):

## Item 1 — ParticleManager::renderUnified race (the real fix)

Known state (deferred.md): nondeterministic crash in
`ParticleManager::renderUnified` — dangling-pointer-shaped race against
the SimulationWorker result buffer; currently gated on `isEnabled()` as
a WORKAROUND (properly applied, P09c era); the real fix is owed.
History pointer: compare the RDP frame race fixed earlier in this
file's history for the established synchronization idiom.

1. Root-cause with evidence, not inspection alone: reproduce under
   stress (a headless/unit harness spinning the simulation worker +
   render loop; ThreadSanitizer build if practical — a local
   `-fsanitize=thread` target is fine, do not commit build-system
   changes unless they're cleanly optional).
2. Fix with the proper synchronization (match the file's existing
   worker/result-buffer patterns; no broad locks around render).
3. Prove it: the stress reproduction runs clean for a bounded soak
   (state iterations/duration); wire the stress test into ctest if it
   is deterministic-fast, otherwise commit it as a manual tool under
   `tools/` and document the invocation.
4. Remove or keep the isEnabled() gate deliberately: if the real fix
   makes the workaround redundant, remove it and say so; if it still
   guards an independent hazard, keep it and document why.

## Item 2 — screenshot-readback guard widening

Known state (P20 Review, deviation 2a; also the unmerged capture
branch): `saveScreenshot()`'s safe-readback path is compiled behind
`EQT_HAS_DRM` only; the x86 GLES2 dev build compiles it OUT and falls
through to `driver_->createScreenShot()`, which segfaults
nondeterministically under llvmpipe. Two separate agents have now
applied the same local widening (`EQT_HAS_DRM || EQT_HAS_GLES2`) as an
uncommitted workaround — land it properly this time: widen the guard,
verify the safe path actually works on the x86 GLES2 build (a
`/screenshot` smoke via the benchmark or a headless run — NO eqemu),
and confirm the ARM build still takes the same path it always did.

## Rules

- Territory: particle files (`src/client/graphics/particle*`,
  simulation worker), the `saveScreenshot()` site + its guard, tests/
  tools you add. Do NOT touch the GLES2 driver internals (P22),
  doors/entities (P21), zone loaders, or goldens.
- Goldens untouched (visual gate PASS) — particles are excluded from
  golden frames but run the gate anyway; STOP if it moves.
- The post-camp shutdown-thread aborts (documented class) are OUT OF
  SCOPE unless your particle fix directly resolves one — do not start
  a general shutdown refactor.

## Verification

- Full 6-step bar (known ctest exceptions per protocol).
- Item 1: stress evidence before (crash/TSAN report) and after (clean
  soak), synchronization mechanism explained in the Review.
- Item 2: smoke evidence of the safe readback on x86 GLES2 + ARM build
  unchanged-path confirmation.

## Review

### Item 1 — ParticleManager::renderUnified race: real fix

**Root cause (two distinct races in `SimulationWorker`, not one):**

`postSimulationInput()` calls `SimulationWorker::postInput()` once per
frame unconditionally, and `applySimulationResults()` calls
`swapAndGetResults()` once per frame, which explicitly degrades gracefully
(serves stale front-buffer data) rather than blocking when the worker
hasn't finished — `framesSkipped_` exists specifically to track this as a
*normal* condition, not an edge case. That combination made two things
possible on any frame slower than the worker's compute budget:

1. **`input_` read unsynchronized.** `workerLoop()` released `mutex_` as
   soon as the condition-variable predicate was satisfied and then read
   `input_` (a struct full of `std::vector`/`std::unordered_map` members)
   with no lock held, for the whole duration of `computeAll()`. A
   `postInput()` call arriving mid-compute would reassign `input_`
   (destroying/reallocating those containers) under `postSimulationInput()`
   on the main thread while `computeAll()` iterated them on the worker
   thread — a concurrent read/write data race with no synchronization at
   all on the read side.
2. **A buffer overwritten before being consumed.** Because `workReady_`
   could already be `true` (set by a `postInput()` that arrived while the
   worker was still busy) by the time the worker finished a compute and
   looped back to wait, it could start a *second* compute into
   `output_[backIdx_]` before the main thread had actually consumed the
   first result (i.e. before `swapAndGetResults()` reset `resultReady_` and
   swapped `frontIdx_`/`backIdx_`). If `swapAndGetResults()` then ran
   concurrently with that second compute, it would swap `frontIdx_` onto
   the very index the worker was mid-write on, handing
   `ParticleManager::renderUnified()` (via `particleRenderBuffer_ =
   &results->particleOutput.renderBuffer`) a buffer being actively mutated.
   This is the actual dangling-pointer-shaped race reported against this
   path — `output_[2]`'s double-buffering was sound in isolation, but
   nothing enforced that a new compute couldn't start before the previous
   one had been swapped out.

**Evidence — before fix** (`tools/particle_race_stress.cpp` under
`-fsanitize=thread`, unthrottled `postInput()`/`swapAndGetResults()` calls
with no frame-rate cap, run against unmodified `fable`'s
`simulation_worker.cpp`): **55 TSAN warnings** in a single 20k-iteration
run, including two confirmed
`SUMMARY: ThreadSanitizer: heap-use-after-free ... sim_particles.cpp:73/74`
reports directly inside `computeParticles()` (the function that builds
`output.particleOutput.renderBuffer`), plus races on
`SimulationInput::operator=`, `ParticleCommandData::operator=`, the
`frontIdx_`/`backIdx_` swap itself, and multiple `UnifiedParticle` vector
reserve/push_back/construct sites.

**Fix** (`src/client/graphics/simulation_worker.cpp`):
- `workerLoop()` now snapshots `input_` into a local `SimulationInput`
  copy, and `backIdx_` into a local `myBackIdx`, both taken under `mutex_`
  before releasing the lock and running `computeAll()` unlocked against the
  private copy — mirroring the file's existing double-buffered-output
  discipline (worker computes privately, publishes atomically) instead of
  holding a lock around the expensive compute itself.
- The wait predicate changed from `workReady_` to `workReady_ &&
  !resultReady_`, so the worker will not start a new compute until the
  main thread has actually consumed (swapped) the previous one.
  `swapAndGetResults()`'s swap branch now calls `cv_.notify_one()` after
  resetting `resultReady_` to wake a worker parked on that new predicate.
- `swapAndGetResults()`'s swap branch and its "worker busy, serve stale
  front buffer" fallback branch both now take `mutex_` around their
  `frontIdx_`/`output_[frontIdx_]` accesses (previously only the swap
  branch — TSAN caught a real race in the fallback branch too, once the
  first two fixes were in place).

**Evidence — after fix**: same harness, same build, 0 warnings across
three consecutive 100,000-iteration soaks plus the 20k-iteration run used
for the before/after comparison (`particle_race_stress` exit code 0 in all
cases; render-touch counts in the tens of thousands per run, confirming
the stress actually exercised the front-buffer read path, not just the
worker side).

**Workaround-gate decision: REMOVED.** `renderer_frame.cpp`'s call to
`particleManager_->renderUnified()` was gated on
`particleManager_->isEnabled()` — which reflects the unrelated
`atmosphericParticles` (weather/dust/pollen billboard emitter) setting,
not anything about the unified fire/spell-effect particle system. It only
"worked" as a crash workaround because `atmosphericParticles` is `false`
in the shipped `config/display_settings.json`. With the race fixed at its
actual source (`SimulationWorker`), this gate no longer guards anything —
it was purely masking the race, not an independent hazard — so it is
removed; `renderUnified()` now runs whenever `zoneReady_ &&
have3DTransforms_`, independent of the atmospheric-particle toggle. The
billboard `particleManager_->render()` call (checkpoint 4, a few lines
above) is intentionally left gated on `isEnabled()` — that one *is* the
atmospheric particle system the flag is named for.

**Stress tool**: `tools/particle_race_stress.cpp`, wired into CMake as
`particle_race_stress` (built whenever `EQT_GRAPHICS AND IRRLICHT_LIBRARY`,
same as `willeq` itself). Not registered with ctest — it's a bounded soak
tool, not a fast deterministic unit test. Manual invocation:
```
cmake --build build --target particle_race_stress -j24
./build/bin/particle_race_stress [iterations]     # default 20000, plain build
```
For TSAN evidence, configure a separate build dir with
`-DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"`, build the same target, and
run under `setarch $(uname -m) -R` (TSAN's shadow-memory layout is
incompatible with this host's default ASLR).

### Item 2 — screenshot-readback guard widening

`saveScreenshot()`'s safe `glReadPixels`-based readback path
(`src/client/graphics/irrlicht_renderer.cpp`) was compiled behind
`#ifdef EQT_HAS_DRM` only. `scripts/build-gles2-x86.sh` configures
`-DEQT_DRM=OFF -DEQT_GLES2=ON`, so `EQT_HAS_DRM` was never defined for the
x86 GLES2 dev build and the whole safe branch was compiled out, falling
through to `driver_->createScreenShot()` (confirmed to segfault
nondeterministically under llvmpipe per P20/prior agents). Widened the
guard to `#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)`.

**x86 GLES2 build verification:**
- `CXX_DEFINES` for the `build-gles2` tree confirmed: `-DEQT_HAS_GLES2`
  present, `-DEQT_HAS_DRM` absent.
- The compiled `irrlicht_renderer.cpp.o` for that tree references
  `glReadPixels` (`nm` shows it as an undefined symbol pulled in) —
  confirming the safe-readback branch is now compiled IN, where previously
  it would not appear in the object at all. `saveScreenshot()` is the only
  call site of `glReadPixels` in that file.
- Runtime smoke: `gles2_zone_benchmark --tree-test` (no eqemu — a
  standalone offline renderer that loads a zone and one tree mesh, no
  server/network) exercises the identical technique — `glReadPixels(...,
  GL_RGBA, ...)` under the same EGL-surfaceless/llvmpipe device — via its
  own screenshot helper, producing two valid 800x600x24bpp BMP files with
  13.4% and 12.5% non-clear pixels forming a recognizable tree silhouette
  (not garbage/uninitialized noise, and not all-clear). This confirms the
  underlying readback technique works correctly in this exact
  build/environment. Directly exercising
  `IrrlichtRenderer::saveScreenshot()` itself would require a live
  zoned-in client session, which is out of scope for this unit (no
  eqemu — that's slot1's territory and this unit has no live-session
  harness per its charter).

**ARM build verification:** `docker/Dockerfile.arm-noble` always passes
`-DEQT_DRM=ON` regardless of the `ENABLE_GLES2` build arg, so
`EQT_HAS_DRM` was already defined on ARM before this change — the widened
`defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)` condition is `true` via
the same first operand as before. Confirmed via the `build-arm-noble`
tree's `CXX_DEFINES`: both `-DEQT_HAS_DRM` and `-DEQT_HAS_GLES2` present.
Same preprocessor branch taken, byte-identical compiled code for that
function on ARM — the change is strictly additive for the x86-without-DRM
case.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1234/1235 passed. Only failure: `AudioManagerTest.PreloadCommonSounds` (documented known host exception). `RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run. `visual_gate` PASS. |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | PASS (part of ctest run above); goldens untouched — no golden files modified by this unit. |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/check_file_sizes.sh fable` | No FAILs. `irrlicht_renderer.cpp` (2819 lines) and `renderer_frame.cpp` (1617 lines) remain pre-existing WARNs (both were already over the 1500-line warn threshold before this unit; this unit added +12 and +19 net lines respectively, both comment-heavy). `simulation_worker.cpp` (561 lines, +93 net) stays well under warn. |

### Deviations

- Root cause turned out to be two compounding races in
  `SimulationWorker` (unsynchronized `input_` read, and a compute-start
  race that could overwrite a buffer before it was consumed), not a single
  issue — both are documented and fixed together since they share the same
  root assumption (see Item 1 above). The plan anticipated "the real
  fix... matching the file's existing worker/result-buffer patterns" —
  the actual fix extends the existing double-buffered-output discipline to
  cover `input_` and adds the missing "don't start until consumed" gate,
  which is exactly that pattern, just not yet applied to those two spots.
- `tools/particle_race_stress.cpp` required a new `CMakeLists.txt` target
  (`particle_race_stress`, gated on `EQT_GRAPHICS AND IRRLICHT_LIBRARY`
  like the other tools/ executables) since it links `SimulationWorker` and
  its full compute dependency set most simply via the already-built
  `willeq_core` static library. This is the same "manual tool under
  tools/, cleanly optional" pattern the plan anticipated for a
  non-ctest-registered stress harness — no change to any existing target's
  build behavior.
- Item 2's screenshot smoke test used `gles2_zone_benchmark --tree-test`'s
  own `glReadPixels`-based BMP writer rather than
  `IrrlichtRenderer::saveScreenshot()` directly, since exercising that
  function requires a live zoned-in client session (out of scope, no
  eqemu). The technique verified is identical (same GL calls, same
  driver/device path), so this is treated as sufficient runtime evidence
  alongside the static confirmation (defines + `nm` symbol check) that the
  widened guard compiles the real code path in.

### Final commit

(filled in after commit — see final report)
