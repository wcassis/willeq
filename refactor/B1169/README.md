# B1169 (#488): FX harness frame determinism + the SpellLandingVolume flake

Base: fable f9f0cfcd (all before/after renders), merged with fable 9136b7a3
(B1163/B1164/B1168) for the final tip. Renders: the ThinkPad (Quadro RTX 3000,
NVIDIA 580.178.04), measured on dev-u24. Evidence: `review/B1169/worker/`
(frames, run logs, measure dirs, scripts); the small tables are copied here.

## 1. The variation sources, each with a measured diff (acceptance A)

Rig: the tour-7 plans of `refactor/B1150/scripts/tour7.sh` (B1165's
`run_tour.sh` rows), `--camera b1118 --seed 1`, rendered by
`review/B1169/worker/scripts/render_tour.sh`, compared frame by frame by
`review/B1169/worker/scripts/frame_diff.py` (now `scripts/tools/fx_frame_identity.py`).

**(1) Irrlicht's solid draw order -- THE source.** `CSceneManager::drawAll`
sorts the solid list by `DefaultNodeEntry::TextureValue`, the node's
material-0 texture POINTER (the patched Irrlicht the build uses,
`_irrlicht/69437f357c9fc809/source/Irrlicht/CSceneManager.h` lines 532-548 and
`CSceneManager.cpp` line 1485, `SolidNodeList.sort(); // sort by textures`).
In the b1118 scene four nodes are solid: the ground plane (no texture, key
null, always first), the caster (12 materials), her held item IT72 (a child
mesh node, 2 materials) and the distance-0 target (13 materials). Their keys
are heap addresses, and their ORDER changed with ASLR. The caster and the
target stand in the same place, so some of their pixels have tied depths; the
depth test (LEQUAL) keeps the later draw there.

Measured on spell 93, 20 ASLR-on runs of the pre-fix renderer
(`order_runs_93.tsv`; the instrumented binaries add only log lines -- the
step-1 binary's frames are byte-identical to the fable binary's: `base_b` ==
`instr_1a`, 0 of 435 differ):

| split by frames 0-89 md5 | runs | frames 0-89 md5 |
|---|---|---|
| 13 runs (`base_b`, `instr_*` x5, `hash_b/e`, `nodes_a-e`) | 13 | `a614cb1f` |
| 7 runs (`base_a`, `instr_2a`, `hash_a/c/d/f`, `nodes_f`) | 7 | `fe8094ba` |
| ASLR off (`setarch x86_64 -R`), 2 runs concurrently | 2 | `a614cb1f` |

The 13 / 7 split is by the frame md5, not by a logged texture-address order:
only the 6 `nodes_*` runs logged the held item's address (5 of them
`target < held item < caster`, `a614cb1f`; `nodes_f` `held item < target <
caster`, `fe8094ba`); the 12 `instr_*` / `hash_*` runs log `ground < target <
caster` for both hash groups; `base_*` logged nothing
(`order_runs_93.tsv`). Summary: split 13 a614cb1f / 7 fe8094ba by frame md5;
held-item order logged in 6 runs, matching the hash 6 of 6; all 6 fix1 runs
(forced order) a614cb1f. In the 6 `nodes_*` runs the frame hash follows
the order 6 of 6 times. The two orders differ on 206 of 435 frames (GBM): first
frame 1 (1 px at row 450 col 674, a delta of 36), largest frame 60 (49 px,
rows 406-456 cols 651-676, a delta of 125). Across all 206 frames the differing
pixels sit inside rows 404-462, cols 644-676: the caster's left shoulder,
where the target's arm pokes through. They come in runs of 3 frames, the
model's 10 Hz keyframes (`review/B1169/worker/frames/base_ab_93.tsv`).

**(2) Depth precision bounds the effect; it is not the cause.** The ThinkPad
GBM context is `Config depth=16` and the surfaceless one
(`WILLEQ_DRM_DEVICE=/dev/dri/no-such-card`, B1150's `run_scene.sh`) is
`Config depth=24` (run logs). The same two orders differ on 206 frames, up to
49 px a frame, at 16 bits and on 71 frames, 1 px a frame, at 24 bits
(`frame_identity.txt`; the 24-bit pair reproduces B1163's
`frame_identity_tp_repeat.txt` 71-frame / 1-px reading). So the precision sets
how many pixels tie, and the order decides them. B1169 does not hide this:
the tie pixels are still drawn, now in one order every run.

**(3) Ruled out, with measurements:**
- *Particle RNG*: already seeded since B1138 (`--seed`, default 1,
  `tools/model_viewer/model_viewer_fx_harness.h:58` ->
  `tools/model_viewer/model_viewer_fx_harness.cpp:673`; `random_device` only
  when 0, `src/client/graphics/sim_particles.cpp:40`). In the pre-fix diffs no
  differing pixel lies outside the shoulder box above, and 41/253/458 never
  differed in 3 GBM + 3 surfaceless pre-fix tours (`frame_identity.txt`).
- *Timing*: the scripted clock (step `1/simHz`,
  `tools/model_viewer/model_viewer_fx_harness.cpp:1073`; the device timer is set
  per step, `:1213`). `timing.tsv` has the same md5 (93 `6d9d2874`, 458
  `baafabde`) in all 6 GBM before/after tours.
- *Worker threading*: the harness calls `SimulationWorker::computeOnce`
  synchronously (`src/client/graphics/simulation_worker.cpp:272`, called at
  `tools/model_viewer/model_viewer_fx_harness.cpp:676`); the worker thread is
  never started.
- *CPU geometry / uninitialised state*: with `WILLEQ_FX_MESH_HASH=1` the
  posed vertex+index bytes of both models hash the same on all 90 frames of 6
  runs (`hash_a`..`hash_f`, one log md5 `d8cdd97a`) while those runs' frames
  split 4/2 by order. After the fix nothing else varies (sec. 2).

**Not re-measured:** B1165's dev-u24 llvmpipe pairs (`review/B1165/worker/runs/`,
458 191 of 486). Renders run only on the ThinkPad now, and the llvmpipe path
is not used here.

## 2. The fix and the 3-run proof (acceptance B)

`HarnessSolidOrderNode` (`tools/model_viewer/model_viewer_fx_harness.cpp:918`,
built by `fixHarnessSolidOrder` at `:984`, called once before the frame loop at
`:1098`) takes the ground, target and caster out of the sorted list
(reparented under it, identity transform). It registers ONE solid entry and
draws them inside the same solid pass in a fixed order: ground, target, the
caster's held items, caster. There is NO client rule to follow: the production
client goes through the same Irrlicht texture-pointer sort
(`CSceneManager.cpp:1485` in the patched Irrlicht) and no oracle draw order has
been established. This order is a HARNESS CONVENTION, chosen as the majority
order (13 of 20 pre-fix runs), so those runs' frames are this order's frames
bit for bit. The fix is harness-only: compiled only into the `model_viewer`
target (`CMakeLists.txt:1313`); the production client is untouched. A node with a transparent material
registers for the transparent pass as its own `OnRegisterSceneNode` would, so
only the solid order is pinned. Each run logs
`solid draw order (fixed, B1169) at frame 0: ground < target < caster-held-item < caster`
and a `determinism (B1169)` line naming the seed
(`tools/model_viewer/model_viewer_fx_harness.cpp:1078`). `--fx-seed N`
(`tools/model_viewer/model_viewer_startup.cpp:56`, `--seed` kept as an alias)
defaults to 1. Fixed dt and the synchronous worker were already true (sec. 1).
Goldens, manifests and baselines are untouched (none reads these frames).

Frame-diff counts, run 1 against runs 2 and 3 (`frame_identity.txt`):

| spell | frames | GBM pre-fix (1v2 / 1v3) | GBM fixed, 3 consecutive (1v2 / 1v3) | surfaceless pre-fix | surfaceless fixed, 3 concurrent | merged tip, GBM 1v2 |
|---|---|---|---|---|---|---|
| 93 | 435 | 0 / 0 | 0 / 0 | 71 / 71 | 0 / 0 | 0 |
| 38 | 435 | 206 / 206 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |
| 54 | 435 | 206 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |
| 656 | 435 | 206 / 206 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |
| 41 | 435 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |
| 253 | 444 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |
| 458 | 483 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 |

The determinism claim is scoped to the measured paths (ThinkPad NVIDIA: GBM
16-bit depth and surfaceless 24-bit depth). The dev-u24 llvmpipe surfaceless
16-bit runs B1165/B1161 saw (93 src_cov 0.384/0.386; 458 differing 191/486;
diffs from frame 0 in box y 387..604 x 604..714) were NOT re-measured here and
have a different signature, so the claim does not cover llvmpipe.

The GBM fixed tours ran one after another (`after_gbm.sh`), and the
surfaceless ones ran 3 at a time (`surf.sh`). Every fixed tour equals one of
the pre-fix runs bit for bit (`after_vs_each_before.txt`).

**Gate values** (`gate_table_gbm.tsv`, `gate_table_surf.tsv`; B1139's
`harness_measure.py`, FX_KEEP=1, per `measure_tour.sh`): 7 spells x 8 metrics
(land_cov, end, src_cov, src_on25, src_cen, fxt_cov, roi_s, src_ext). Pre-fix
run-to-run noise is **zero on all 54 numeric cells on both paths**. The
measures do not read the shoulder pixels, which is what the old README line
said. All 56 fixed cells are identical across the 3 runs and equal the
pre-fix value (54 numeric in noise, 2 are 253's `-`/`none`). Against B1157's
tour rows (`refactor/B1157/b1118_table.tsv`, the `_n` columns), surfaceless:

| row | B1157 | before = after | owner |
|---|---|---|---|
| 93, 38, 41, 458: land_cov / end / src_cov / src_on25 / src_cen | as B1157 | same | -- |
| 54, 656 src_cov | 0.415 | 0.416 | pre-existing in all 3 pre-fix runs (tree since B1157), not B1169 |
| 253 land_cov / src_cov / src_cen | 5.306 / 3.804 / 0.99 | 4.148 / 3.323 / 0.95 | land_cov and src_cov are B1161's (#480 retired the 253 override; `refactor/B1161/README.md:287`: land_cov 5.326 -> 4.169, src_cov 3.818 -> 3.331 vs this table's 3.804 -> 3.323); only src_cen 0.99 -> 0.95 stays unowned (pre-exists in all 3 pre-fix runs, not B1169) |

## 3. The flaky test (acceptance C)

`SpellLandingVolume.TheSourceIsABodyWideCloudShapedLikeTheOracles`
(`tests/test_spell_landing_volume.cpp:552`) ran the worker with rngSeed 0 =
`std::random_device`. On the unseeded f9f0cfcd binary it failed **22 of 3000**
runs on the ThinkPad: 458 x19, 93 x2, 38 x1, widths 2.091-2.197 under the 2.2
floor (`flake_before_tally.txt`). That is about 1 in 136, worse than the
card's 1 in 300. Fix: `tests/particle_test_seed.h` (`kSeed = 1u`, line 18, the
harness default). All 17 particle-phase test inputs set `pi.rngSeed` to it.
`test_spell_source_anchors.cpp` already had its own 1150. The new
`SpellParticleSeedWiring.EveryTestThatRunsTheParticlePhaseSeedsIt`
(`tests/test_spell_landing_volume.cpp:722`) fails any future unseeded site.
No band moved (`EXPECT_GE(width, c.wMin)` is still 2.2, `:573`).

- the flaky test, `ctest --repeat until-fail:300` via tbuild: **300/300**
  (`review/B1169/worker/ctest_flaky_300.log`);
- every touched suite (111 tests over 12 executables) `--repeat until-fail:300`:
  100 % passed (`review/B1169/worker/ctest_touched_300.log`).

## 4. Docs (acceptance D)

`tools/model_viewer/README.md` "Determinism" (line 146): where the seed lives,
what pins time, the worker and the draw order, and how a card proves
"identical" (`scripts/tools/fx_frame_identity.py`, exit 0 = 0 differing frames;
GBM and surfaceless are different renders). The tour script
(`refactor/B1150/scripts/tour7.sh`) points at it.

## 5. Gates (acceptance E/F)

Targeted ctest, the 12 touched test executables: 111/111
(`review/B1169/worker/ctest_touched.log`). Hygiene (tbuild): 48/48, 1 skipped
(`review/B1169/worker/hygiene.log`). Cite check: see the MR.
