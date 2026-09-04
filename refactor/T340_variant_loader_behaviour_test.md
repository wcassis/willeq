# T340 — A behaviour test for the variant loader, then the de-duplication (B052)

Two legs, same slot (slot1), sequential. G07 measured the shape: one
`for (codeToTry : codesToTry)` candidate loop, `continue` = next
candidate, success `return`s from three loops deep; the animation-merge
body appears four times (0.92–0.96 similar) and the zone/global character
blocks are 0.91 similar. **No test drives the method.** Will's rule: EQE1
is ALL-OR-NOTHING — a race whose primary code misses but whose fallback
code hits must LOAD; all candidates exhausted must return null and cache
null.

## Leg A — the test (no production change)

`tests/test_variant_loader_candidates.cpp`, driving
`loadModelFromGlobalChrWithVariantsForAnimation` through a controllable
archive source. Read `src/client/graphics/eq/variant_loading.cpp` (the
caller, the `variantCache` negative entry at ~273) and
`variant_loading_animation.cpp` (the loop) to find the seam the loader
already has for where models come from (the EQE1 archive component /
`getOrOpenEQE1`, or the S3D path in tests — `tests/test_race_model*.cpp`,
`test_eqe1_*.cpp` and the model-viewer harness show what fixtures exist;
`data/race_models.json` carries `fallback_code`). Cases, each named for
the exit it exercises:
1. primary code hits on the first character → loads (the depth-5
   `return true`).
2. primary code misses every character, fallback code hits → loads
   (the outer `continue` at ~515 then success) — the case a bool-list
   would have broken.
3. a character with no usable model → `continue` to the next character,
   then success.
4. all candidates exhausted → returns false, caller caches null, second
   call does not re-scan (the ~845 `return false` and the negative cache).
5. `codesToTry` empty → the line-69 `return false`.
Fixtures: the smallest archives that make each path real (an existing
EQE1 test tree if one is mounted — `test_eqe1_archive_component` skips
when not; otherwise a stub archive source; say which). Assert on the
returned model AND on which candidate/character was chosen (add the
smallest read-only observation seam if none exists — e.g. the loader's
existing log line captured, or a `lastResolvedCandidate()` getter).
Prove each case is real by planting the fault G07 predicted: change one
`continue` to `return false` in the code → case 2 must fail; restore.

## Leg B — de-duplicate, behind the test (production change, move-only where possible)

Only after Leg A is merged and green: `mergeAnimationsFrom()` replaces
the four copies (diff the four first; where they differ, the difference
becomes a parameter — record each); the zone/global character blocks
become one `tryBuildVariantFrom(source, …)` parameterised by the
archive; `buildCodesToTry()` and `selectVariantParts()` as G07 laid out.
The loop keeps its shape. Proof: Leg A's five cases unchanged and green;
the G07-style exit census identical before/after (same exits, same
depths); the visual gate + ARM standalone; the model viewer builds.
Predicted 815 → ~250 across five functions.

STOP (Leg A): no seam lets a test control the candidate source without
a production change beyond a read-only getter — then report the seam
needed. STOP (Leg B): any of the four copies differs in a way that is
not a parameter (a behavioural difference) — report it, do not unify.

## Review — Leg A

**Done. Five cases, all real archives, both planted faults caught.**
Branch `task/T340-legA-variant-loader-test`, forked from `fable` at
`fccb2a45`. New file `tests/test_variant_loader_candidates.cpp` (307 lines),
registered in `tests/cmake/ui_rendering.cmake` beside `test_race_330_loader`
(same `EQT_GRAPHICS AND IRRLICHT_LIBRARY` gate, same `willeq_core` link, same
`WILLEQ_DB_REAL_PATH`).

### The seam

Two halves, and only the second needed a production edit.

**Controlling the candidate source: no production change.** The loader already
has one — `setClientPath()` + `loadZoneModels(zone)`, both public. Everything
`loadModelFromGlobalChrWithVariantsForAnimation` can open is reached by
concatenating `clientPath_` with a filename (`{zone}_chr.s3d`,
`global_chr.s3d`, the race row's `s3d_file`), so pointing the loader at a
directory that holds ONLY the archives a case is allowed to see makes every
miss path real rather than simulated. Each test builds such a directory under
`temp_directory_path()` and symlinks the shipped P99 archives into it. **No
fixture is a stub** — every byte the loader parses is the real
`/mnt/projects/EverQuestP1999` S3D file; the EQE1 tree
(`/mnt/projects/EverQuest_ETC1`) is not involved, because this loader is on
the S3D path only. The db is the shipped `data/willeq.db`; the test skips
without it or without the client tree.

**Observing the outcome: one read-only getter.** `variantModels_` cannot say
*which* candidate won, and it cannot tell "ran and returned false" from "never
ran, because the caller's negative cache short-circuited" — and case 4 turns
on exactly that distinction. Added `RaceModelLoader::variantLoadTrace()` and
the `VariantLoadTrace` struct (`include/client/graphics/eq/race_model_loader.h`):
`attempts`, `succeeded`, `resolvedCode`, `resolvedCharacter`, `resolvedSource`.
Written at three sites in `variant_loading_animation.cpp` — once on entry
(bump/clear) and once inside each of the two success blocks, **inside the
`variantModelsMutex_` lock those blocks already take**, so the seam inherits
the loader's existing thread discipline (this method also runs on
EntityPrepWorker's background thread). No control flow reads it; no production
caller consults it. That is the whole production diff: +31 header lines,
+23 `.cpp` lines, all additive.

The only public door into the method is `getAnimatedMeshWithAppearance`, and
it delegates to `getAnimatedMeshForRace` when head, body and texture variant
are all zero — so the tests pass `textureVariant=1, compositeAllowed=false`,
which takes the variant path with `headVariant==0, bodyVariant==0` and keeps
the P08d composite branch out of the way.

### The five cases

Exit line numbers below are as measured on this branch (the G07 census
re-measured at `fccb2a45` first — **69, 155, 280, 287, 505, 515, 525, 640,
646, 840, 845, identical to G07's table** — then shifted by the seam's +13
entry block and +4 per success block).

| # | test | fixture (client dir) | race | exit(s) |
|---|---|---|---|---|
| 1 | `PrimaryCodeHitsInTheRowsOwnArchive` | the whole real client tree | 71 QCM, m | **523** (G07 505) |
| 2 | `FallbackCodeLoadsAfterThePrimaryMisses` | `global4_chr.s3d` only, loaded as zone `global4` | 139 ICM → IKM, m | **533** (G07 515) then **523** |
| 3 | `MatchedCharacterWithNoUsableMeshIsSkipped` | `global6_chr.s3d` + `global_chr.s3d`, zone `global6` | 60 SKE, m | **293** (G07 280) then **863** (G07 840) |
| 4 | `AllCandidatesExhaustedCachesNullAndDoesNotRescan` | `global_chr.s3d` only | 35 EEL, m | **868** (G07 845) |
| 5 | `NoRaceCodeAtAllAbandonsImmediately` | empty directory | 60000 (no row) | **82** (G07 69) |

Those exits are **measured, not asserted**: the file was temporarily
instrumented with a `fprintf` at each of the eleven semantic exits, each test
run alone, and the file then restored byte-identically (`cmp` clean). Per-case
output was exactly the table above — one line each, no extra exits.

Case by case, and why each fixture is the smallest thing that makes the path
real:

1. **Primary code hits.** Race 71's row names `commons_chr.s3d`, which holds
   `QCM_HS_DEF` with both `QCM_DMSPRITEDEF` and `QCMHE00_DMSPRITEDEF`. This is
   the JSON-`s3d_file` source branch, so it needs the real tree — the case
   must not miss anything. Asserts `resolvedCode=="QCM"`,
   `resolvedCharacter=="QCM_HS_DEF"`, `resolvedSource=="zone"`, and a non-empty
   `combinedGeometry` out of `getVariantModelData`.
2. **Primary misses everywhere, fallback loads.** Will's all-or-nothing case.
   Race 139 is `ICM` with `fallback_code` `IKM`, row archive
   `cabeast_chr.s3d`. The fixture holds only `global4_chr.s3d` (IKS, SPE, BAF,
   BAM, IKF, IKM — **no ICM**), loaded as the current zone: candidate `ICM`
   finds nothing in the zone source, then `loadGlobalModels()` fails because
   there is no `global_chr.s3d` in this directory, so the OUTER `continue`
   fires — G07's line 515, literally the exit the plan named — and candidate
   `IKM` then hits `IKM_HS_DEF` in the same zone source. Asserts the FALLBACK
   won.
3. **A matched character with nothing usable.** `global6_chr.s3d` contains
   `SKE_HS_DEF` whose only mesh is `SKEMESH_DMSPRITEDEF` — it matches code
   `SKE` by name but carries neither `SKE_DMSPRITEDEF` nor
   `SKEHE00_DMSPRITEDEF`, so variant-part selection comes up empty and the
   character loop `continue`s. The remaining global6 characters (TPN, TIG,
   WOL, WOE) do not match, so the search falls through to `global_chr.s3d`,
   whose `SKE_HS_DEF` carries both — the global-path success. Race 60 has no
   `fallback_code`, which is asserted, so the case turns on the character
   loop and not on a second candidate.
4. **Exhaustion + the negative cache.** Race 35 `EEL`, no fallback, row
   archive `eel_chr.s3d`; the fixture holds only `global_chr.s3d`, which has
   no EEL. First call: `attempts==1`, `succeeded==false`,
   `getVariantModelData` null. Second identical call: `attempts` still **1** —
   the caller's `variantCache[key] = nullptr` short-circuited it, which is the
   only way to observe non-rescan.
5. **Empty candidate list.** Race 60000 has no `race_models` row, so
   `getRaceCode` and `getFallbackRaceCode` both return empty and the method
   abandons before touching an archive — hence the empty fixture directory.

### The planted faults

Both were planted, built, run and restored byte-identically (`cmp` against a
pre-edit copy, and `git diff` shows the file with only the three additive
trace hunks).

- **Fault 1 — G07's prediction, verbatim.** The outer candidate loop's
  `continue; // Failed to load globals, try next code` → `return false;`.
  Result: **case 2 alone fails**, on its own message —
  *"a race whose primary code misses but whose fallback_code hits must LOAD
  (EQE1 all-or-nothing) -- got a null instead"* — `trace.succeeded` false.
  Cases 1, 3, 4, 5 stayed green. That is the exact regression G07 said a bool
  list would introduce, and the test catches it and names it.
- **Fault 2 — the character-loop `continue`.** The "could not find variant
  meshes" `continue` → `return false;`. Result: **case 3 alone fails**. Cases
  1, 2, 4, 5 stayed green.

Each fault breaks exactly one case, so no case is passing by accident and none
of them overlap.

### Exits still uncovered — a Leg B note, not a gap in this leg

Six of the eleven semantic exits are now driven. The other five, and what was
measured about them:

- **G07 155 / 525** (`!character || character->parts.empty()`, both paths) —
  **unreachable with shipped data.** Every character model in all 378
  `*_chr.s3d` archives under `/mnt/projects/EverQuestP1999` was enumerated
  (`s3d_dump`, which reports `model->parts.size()` directly); **not one has
  zero parts.** No fixture built from real archives can reach these two
  `continue`s. Leg B may keep them, but nothing behavioural can pin them.
- **G07 640** — the global-path twin of case 3's 280. Reachable cheaply:
  symlink `global6_chr.s3d` into a fixture directory *as* `global_chr.s3d` and
  the same unusable `SKE_HS_DEF` is met on the global path instead. Worth
  adding in Leg B, because 640 sits in one of the four near-duplicate blocks
  `tryBuildVariantFrom()` is meant to absorb — merging the zone and global
  blocks with only the zone one under test is under-netted. Left out here to
  keep Leg A to the five dispatched cases.
- **G07 287 / 646** (`combineCharacterPartsWithTransforms` returned null after
  parts WERE selected) — no real archive found that reaches them; they would
  need a corrupt or hand-built mesh.

### Verification

All from `/mnt/projects/fable/wt/slot1`, classified by exit status.

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3793 tests, 1 failed** — `AudioManagerTest.PreloadCommonSounds` (the standing ALSA host exception) and nothing else. The five new tests are in the count and pass. |
| `scripts/refactor/check_file_sizes.sh --all` | **exit 0** — checked 874 files: 874 ok, **0 warn, 0 fail** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **exit 0, `=== VISUAL GATE: PASS ===`**, goldens untouched (`git status` lists only the four files of this change) |
| `scripts/build-arm-noble.sh` | **exit 0** |

The GLES2, visual-gate and ARM steps were run because the seam touches
`src/client/graphics/eq/variant_loading_animation.cpp`; they were not skipped.
`cmake` had to be re-run once to pick up the new target (`cmake -S . -B build`,
exit 0) — the first `--target` invocation failed with "No rule to make target",
which is a configure-staleness result, not a build failure.

### Deviations

- The plan's case 2 predicted "the outer `continue` at ~515 **then success**",
  and that is what was built — but it only works because the two candidates
  search the SAME zone source. `loadGlobalModels()` depends on `clientPath_`
  alone and is idempotent, so if it fails for the first candidate it fails for
  every later one; a case whose fallback lives in `global_chr.s3d` would take
  515 and then only ever reach the trailing `return false`. Race 139
  (ICM → IKM, both resolvable inside `global4_chr.s3d`) is what makes
  "515 then success" reachable at all. Worth knowing before Leg B moves the
  `loadGlobalModels()` call.
- A second planted fault was added beyond the one the plan asked for, so that
  case 3's exit is proved by mutation and not only by the instrumented census.
- The exit census was produced by temporary instrumentation rather than by the
  permanent seam. The seam deliberately records only what the plan sanctioned
  (which candidate/character resolved, plus the attempt count case 4 needs);
  tagging all eleven exits permanently would have been a larger production
  edit than "the smallest read-only observation seam". If Leg B wants the
  before/after exit census as a standing proof, that is the moment to decide
  whether the tags become permanent.
