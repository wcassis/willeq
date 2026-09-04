# T352 — TSAN run over `RaceModelLoader` (B016): confirm or close

Class B measurement, slot1, no eqemu. B016 says ~20 unlocked
`loadedModels_` reads across four TUs; a blanket lock self-deadlocks.
Unconfirmed since T114.

1. Configure a TSAN tree the way T121 did (`refactor/T121_tsan_zone_crossing.md`
   records the flags — reuse them, in `/mnt/projects/fable/wt/slot1/build-tsan`,
   GLES2 config, tests on). Build `willeq` and the race-model tests.
2. Run: the T121 zone-crossing script (headless, three zones) and the
   race-model/EQE1 test binaries under TSAN with `--eqe1-path`. Collect
   every ThreadSanitizer report naming `loadedModels_`, `RaceModelLoader`,
   or the four TUs (`race_model_loader.cpp`, `model_loading*.cpp`,
   `variant_loading*.cpp`, `mesh_building.cpp`).
3. Verdict: (a) reports present → list each race (both stacks, the
   member, the two threads), and propose the minimal fix shape (a
   recursive mutex on the loader, or moving the read under the existing
   lock — measure which reads sit on the render thread vs the loading
   thread); do NOT fix in this unit unless it is one lock at one site;
   (b) no report → B016 closes as "not reproducible under TSAN at this
   tip" with the run log.
4. `review/T352/README.md` with the report excerpts; B016 updated.

STOP: the TSAN tree does not build (report the error; the T121 flags may
have rotted).

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch
`task/T352-tsan-race-model-loader`, base `fable` tip `210a9128` (verified
before any work). No eqemu — see Deviations. Full artifacts, both stacks,
every run log and a from-scratch reproduce script:
`/mnt/projects/fable/review/T352/`.

**Headline: verdict (a), but not on the member B016 names. TSAN reported
ZERO races on `loadedModels_` — the ~20 unlocked reads are real and never
collide, because every writer is main/loading-thread. What it reported
instead, 3 of 4 runs with both stacks fully symbolized, is
`RaceModelLoader::getMergedTextures()` (`mesh_building.cpp`) running with NO
lock at all against `storeOtherChrCacheLocked()`
(`race_model_loader.cpp:650`) on the EntityPrepWorker thread — reached from
`ensureModelDataLoaded()`, on B016's own call chain, inside B016's own four
TUs. Closed under the plan's one-lock exception: one `lock_guard` on the
mutex the writer already holds, at one site. 6 of 6 post-fix runs clean, no
deadlock.**

### B016's premise is half wrong, and the correction matters

B016 says "~20 unlocked `loadedModels_` reads across four TUs". Re-measured
at this tip: **every `loadedModels_` access in the three sibling TUs
(`model_loading.cpp`, `model_loading_chr.cpp`, `model_loading_search.cpp`)
is already under `loadedModelsMutex_`** — T113 closed them. The unlocked
reads are 25, confined to ONE function in ONE TU:
`RaceModelLoader::ensureModelDataLoaded()`, `race_model_loader.cpp:276-422`.

And they are not a race. Writers are `promotePreparedModels()`
(`race_model_loader.cpp:502`), the four `loadedModels_[key] = ...` sites in
`model_loading.cpp` and their siblings, and the two `clear()`s — all under
the lock, all on the main/loading thread. The one background-thread toucher,
`EntityPrepWorker::processRequest()`, reads through `isModelDataCached()`,
which takes the lock, and writes only `preparedModelData_` under
`preparedDataMutex_`. The self-deadlock B016 warns about is also confirmed
(the unlocked reads interleave with `loadModelFrom*()` calls that take the
lock themselves) — which is exactly why nothing should be done about them.

**`loadedModels_`'s unlocked reads are a documentation defect, not a
threading defect. The live race was next door.**

### The race — measured, not argued

Both stacks verbatim in `review/T352/README.md` and
`review/T352/pre_fix/tsan_report.505452`:

- **EntityPrepWorker thread**, holding `otherChrCacheMutex_`:
  `storeOtherChrCacheLocked` (`race_model_loader.cpp:650`) <- `preloadModelData`
  (`:798`, lambda at `:735`).
- **Render thread**, holding nothing: `getMergedTextures` (`mesh_building.cpp:15`)
  <- `loadModelFromGlobalChr` (`model_loading.cpp:387`) <-
  `ensureModelDataLoaded` (`race_model_loader.cpp:302`).

TSAN names the 1-byte `mergedTexturesCacheValid_` because that is the first
byte the two threads collide on. **The flag is not the damage.** The same
unsynchronised window covers step 4 of `getMergedTextures()`
(`mesh_building.cpp:57`), which iterates `otherChrCaches_` while the writer
inserts into it and LRU-evicts from it — iterator invalidation and a
dangling `OtherChrCache&`, in a loader whose crash arc (T113-T121) is
littered with exactly that failure mode.

### Fix: one lock at one site (the plan's stated exception)

`std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);` over the
whole body of `getMergedTextures()`. No new mutex, no recursive mutex, no
restructure — the writer already holds this exact mutex everywhere it
mutates.

Deadlock-freedom was CHECKED, not assumed (the plan's warning that a
deadlock is worse than the race): (1) none of the 12 call sites holds
`otherChrCacheMutex_` — T114 already made releasing it first an explicit
invariant at `model_loading_chr.cpp:336-345`, and `model_loading.cpp:181`'s
guard closes two lines later in another function; (2) the only calls inside
are `loadNumberedGlobalModels()`/`loadArmorTextures()`, neither of which
takes any loader mutex; (3) no new lock-ordering edge — the callers'
`loadedModelsMutex_` acquisitions all happen after this returns; (4)
measured — 6 of 6 post-fix runs COMPLETED (a self-deadlock hangs, it does
not fail).

### Instrument honesty

The five TSAN-built race-model/EQE1 test binaries ran clean — and that
proves nothing, because all five drive the loader from one thread. It is
recorded in the README as a negative, not as coverage. Before claiming any
clean result, the harness was validated with a deliberate two-thread
positive control through the identical `TSAN_OPTIONS`, suppression file and
`setarch -R` (`review/T352/poscontrol.497463`): it fired.

Suppressions: T114/T121's file verbatim, one library-only entry
(`race:libgallium*.so*`). No willeq symbol suppressed. Nothing added.

### Left open (new board item, deliberately not folded in)

The lock closes the measured race; it does not make `getMergedTextures()`
wholly thread-safe. It also reads `globalTextures_`,
`numberedGlobalTextures_`, `armorTextureCache_` and `zoneTextures_`, mutated
elsewhere without this mutex. TSAN reported none of those in any run, so
they are a code-level observation, not a measurement — and covering them is
the restructure this plan forbids.

Separately, `model_loading_chr.cpp:363-367` writes
`otherChrCaches_[lowerFilename]` directly instead of through
`storeOtherChrCacheLocked()` — the LRU-accounting bypass T91 was written to
eliminate, surviving at one site. Under the mutex, so not a race; an
untracked cache entry. Both are on the board (B064, B065).

### Verification

| Step | Result |
|---|---|
| TSAN tree configure + build (`build-tsan`, T121's flags, GLES2 config, `EQT_BUILD_TESTS=ON`) | PASS, exit 0 — the T121 flags have **not** rotted, so the plan's STOP was not reached |
| `cmake --build build -j24` | PASS, exit 0, zero `error:` lines |
| `ctest --test-dir build -j6 --output-on-failure` | **3,852 of 3,853**; sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. `visual_gate` PASS within the suite |
| `scripts/refactor/check_file_sizes.sh --all` | 880 files, **880 ok, 0 warn, 0 fail**; `mesh_building.cpp` 664 lines |
| `scripts/build-gles2-x86.sh` | PASS, exit 0, zero `error:` lines |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** — goldens untouched, none moved, no rebless |
| `scripts/build-arm-noble.sh` | PASS, exit 0; `build-arm-noble/bin/willeq` is ELF 32-bit ARM EABI5 |
| TSAN driver, pre-fix | 3 of 4 runs reported the race |
| TSAN driver, post-fix | **6 of 6 clean**, every run completed with an identical result — no deadlock |

### Deviations

1. **The T121 zone-crossing tour was NOT run.** The dispatch said "you need
   no server: the T121 tour is headless." It is not: the tour logs in to
   `127.0.0.1:5998` and issues six `#zone` commands, and `willeq` has no
   offline zone mode. `.eqemu-lock` was absent and 5998 was not listening,
   but slot3 was named as the holder, so the lock was not taken. In its
   place, `review/T352/t352_loader_race_driver.cpp` reproduces the exact
   production thread pairing offline against the real client tree, real S3D
   archives and real `willeq.db` under an `EDT_NULL` device — the same
   offline harness `tests/test_race_330_loader.cpp` uses. That driver is
   what found the race the tour was meant to look for.
2. **No ctest test pins the fix.** A green non-TSAN run of the driver proves
   nothing about a data race, and shipping one as a "pin" would manufacture
   exactly the false signal T119/T120 were caught by. The reproducer is
   archived instead, with a one-command rebuild
   (`review/T352/build_and_run.sh`), so the next TSAN unit re-runs it rather
   than re-derives it. Flagged, not silently absorbed.
