# B1177 (#496): the FX harness's "did not solve" exit, and the b1130 target solve's cycle

Found by the B1172 review (MR !325 note 7955): `--camera b1130 --target 6:1:6` on a runner VM
cycled through feet rows 616 / 616 / 618 until the iteration cap, printed "did not solve" and
died with rc 139 (`/mnt/projects/fable/review/B1172/worker/wrong_model/r1_6_1_6.log:116-135`).
Evidence for this card: `/mnt/projects/fable/review/B1177/worker/` (run logs under `runs/`, every
exit code in `runs/rc.tsv`, the ctest and hygiene logs beside them). Built on the B1172 tip
(ea74acbb, kStandingPx 285), which this branch merges.

## 0. Result

- **Ask 1, the crash.** The failure path dropped the device and returned 2, skipping the teardown
  the normal exit runs. Two namespace-scope `unique_ptr<ConstrainedTextureCache>`s
  (`fxTextureCache` at `tools/model_viewer/model_viewer_fx_harness.cpp:35`, and
  `equipmentTextureCache` at `tools/model_viewer/model_viewer_state.cpp:53`) were then destroyed by the
  C runtime's exit handlers after the driver was gone. gdb on the ThinkPad, same failure forced
  with `--target-fit-iterations 1`:
  "Program received signal SIGSEGV" (`/mnt/projects/fable/review/B1177/worker/runs/tp_gdb_iter1.log:114`),
  frames `ConstrainedTextureCache::destroyTextures` <- `clear` <- `~ConstrainedTextureCache` <-
  `~unique_ptr` <- `__run_exit_handlers (status=2, ...)`
  (`/mnt/projects/fable/review/B1177/worker/runs/tp_gdb_iter1.log:116-120`). The Release frame does not
  say which of the two caches it was; the teardown resets both. The same pattern was already
  written down for the normal exit (`tools/model_viewer/model_viewer_fx_harness.cpp:659`, B1139's
  "SIGSEGV at exit" comment).
  **Fix:** one teardown, `shutdownViewer` (`tools/model_viewer.cpp:84`), used by every exit after
  the device exists: init failure 3 (`tools/model_viewer.cpp:95`), mesh failure 1
  (`tools/model_viewer.cpp:130`), "did not solve" 2 (`tools/model_viewer.cpp:155`) and the normal
  exit (`tools/model_viewer.cpp:170`). The code is the documented one, B1155's "exits 2"
  (`refactor/B1155/README.md:61`), now named `fxharness::kExitPresetDidNotSolve`
  (`tools/model_viewer/fx_target_anchor.h:152`, aliased as `kHarnessExitPresetDidNotSolve` at
  `tools/model_viewer/model_viewer_fx_harness.h:130`).
- **Ask 2, the solver.** 6:1:6 does not converge because of the solver's singular-Jacobian
  fallback, not the model's feet and not the anchor tolerance (sec. 2). It now terminates on a
  detected cycle or the cap with a diagnostic, and on both renderers 6:1:6 solves to the anchor
  and fails the self-check, exit 5 (sec. 3).
- **Unchanged:** the acceptance (|d| < 1 px on feet row and column, B1166) and every
  fx_target_anchor.h tolerance; the default b1130 preset solves along the same four fits to the
  same placement, frame 0 byte-identical before/after (sec. 4).

## 1. Ask 1: the exit, and its test

`--target-fit-iterations N` (`tools/model_viewer/model_viewer_startup.cpp:42`, default 24 =
B1166's loop) caps the target solve; with 1 only the start point (x -2, z 2) is measured, so the
preset cannot solve on any renderer. That is the deterministic non-solving preset.

| run | host | rc | file |
|---|---|---|---|
| B1172 tip, `--target 6:1:6` | gitlab-runner-2 (llvmpipe) | 139 | `runs/rc.tsv`, log `runs/r2_before_6_1_6.log` |
| 94f7a565 (knob, old teardown), `--target-fit-iterations 1`, under gdb | ThinkPad | SIGSEGV | `runs/tp_gdb_iter1.log` |
| this branch, `--target-fit-iterations 1` | ThinkPad / gitlab-runner-2 | 2 / 2 | `runs/tp_after_iter1.log`, `runs/r2_after_iter1.log` |

Test: `FxHarness.B1130PresetThatDoesNotSolveExitsTwoWithoutASignal`
(`tests/test_fx_harness.cpp:504`) runs that preset and asserts no signal, exit status 2 (a child
killed by signal n reaches `std::system` through the shell as 128 + n, so 139 fails it), the
"did not solve" line and the diagnostic naming the one candidate. Passed on gitlab-runner-2
(`/mnt/projects/fable/review/B1177/worker/ctest_runner2.log`).

## 2. Why 6:1:6 did not converge (ask 2)

The solve is a damped Newton on the two feet-pixel residuals with a finite-difference Jacobian
at d = 0.05 units. The feet row is a whole pixel; near the anchor 0.05 units of x or z do not
move it, so the row half of the Jacobian is zero and the determinant is 0. B1166's code then
fell back to a fixed nudge of 0.2 units on both axes, with BOTH signs inverted: `x -= 0.2 *
sign(r0)` moves a column that is right of the anchor further right (screen right is world -X),
and `z += 0.2 * sign(r1)` moves a feet row that is below the anchor further down (+z is closer,
lower). The llvmpipe log shows exactly that move, (-0.2, +0.2), from fit 7 to fit 8: "(x -8.927,
z 0.927) -> feet row 616.0 at column 940.2" then "(x -9.127, z 1.127) -> feet row 618.0 at column
948.8" (`/mnt/projects/fable/review/B1172/worker/wrong_model/r1_6_1_6.log:117-118`); from 618 a
regular Newton step returns to 616, the nudge fires again, and fits 6..23 repeat 616 / 616 / 618
(`/mnt/projects/fable/review/B1172/worker/wrong_model/r1_6_1_6.log:116-133`).
gitlab-runner-2 reproduces it fit for fit (`/mnt/projects/fable/review/B1177/worker/runs/r2_before_6_1_6.log:112-114`).
The ThinkPad (NVIDIA) takes the same inverted nudge at fits 7 -> 8
(`/mnt/projects/fable/review/B1177/worker/runs/tp_6_1_6.log:108-109`), but its next Newton step from
618 lands on 614 and then 615 (`/mnt/projects/fable/review/B1177/worker/runs/tp_6_1_6.log:110-111`), which
is why it solved there and only the runners showed the defect.

The zero row derivative is measured, not inferred: the fixed solver prints it on the same body,
"singular Jacobian at d 0.05 (d col/dx -35.52, d col/dz 7.23, d row/dx 0.00, d row/dz 0.00
px/unit)" (`/mnt/projects/fable/review/B1177/worker/runs/r2_after_6_1_6.log:115`; ThinkPad:
`/mnt/projects/fable/review/B1177/worker/runs/tp_after_6_1_6.log:110`).

Not the cause:
- **Model feet geometry.** The dark-elf female's feet reach the anchor exactly: after the fix,
  "feet row 615.0 at column 940.1" on llvmpipe (`/mnt/projects/fable/review/B1177/worker/runs/r2_after_6_1_6.log:117`)
  and on NVIDIA (`/mnt/projects/fable/review/B1177/worker/runs/tp_after_6_1_6.log:112`).
- **The anchor tolerance.** The anchor is reachable within the existing acceptance, so no
  tolerance needs to move and none was moved. One fact for the record: the solver's acceptance
  is |d| < 1 px (B1166's `ok`, now `fxharness::kSolveAcceptPx`,
  `tools/model_viewer/fx_target_anchor.h:157`), which on a requested-size surface means the
  anchor's own row 615; the frame-0 self-check allows +-1. Row 616 (d +1) would have passed the
  self-check's feet-row bound but not the solve. B1166's header comment called +-1 "the solver's own convergence
  bound"; the comment now states what the code does (`tools/model_viewer/fx_target_anchor.h:47`),
  and the code was not widened.

A second defect on the same path: when the loop ran out, the printed and stored placement was
(x, z) AFTER the last step, which no frame drew: fit 23 measured (x -8.713, z 1.127)
(`/mnt/projects/fable/review/B1177/worker/runs/r2_before_6_1_6.log:129`) while the summary line
says "target (x -8.887, y 0.580, z 0.977) -> rendered target feet row 618 at column 935"
(`/mnt/projects/fable/review/B1177/worker/runs/r2_before_6_1_6.log:130`). The solve now reports and
stores the measured point (`tools/model_viewer/model_viewer_fx_harness.cpp:407`). On a solve that
converges (the loop breaks right after the measure) nothing changes.

## 3. The terminating solver and its diagnostic

The target solve (its B1177 comment at `tools/model_viewer/model_viewer_fx_harness.cpp:337`, loop at
`tools/model_viewer/model_viewer_fx_harness.cpp:353`):
- the finite difference retries at d 0.2 and 0.8 when the Jacobian is singular
  (`tools/model_viewer/model_viewer_fx_harness.cpp:369`), printing each singular one;
- if all three are singular, the nudge (0.05 units) moves only the axis that is out, toward its
  pixel (`tools/model_viewer/model_viewer_fx_harness.cpp:390`);
- every non-converged measure is a `FitSample`; `fxharness::fitCyclePeriod`
  (`tools/model_viewer/fx_target_anchor.h:177`) stops the solve when the pixel states (feet row,
  column rounded to 1 px) repeat with period 2 or 3 over the last 3p samples (two full repeats);
  a constant run is a stall, not a cycle;
- on failure `fxharness::fitDiagnostic` (`tools/model_viewer/fx_target_anchor.h:205`) prints the
  cycle's candidates, or the closest candidate after the cap, each through `fitSampleWhy`
  (`tools/model_viewer/fx_target_anchor.h:192`), e.g. "closest (x -2.000, z 2.000) -> feet row 636
  (d +21: misses |d| < 1), column 710.1 (d -229.9: misses |d| < 1)"
  (`/mnt/projects/fable/review/B1177/worker/runs/r2_after_iter1.log:114`).

On the recorded pre-fix sequence (fits 0..14 of r1_6_1_6.log) the detector fires at fit 14 with
period 3, and its three candidates are rows 616 (d +1), 616 (d +1) and 618 (d +3, column 948.6,
d +8.6): `B1177TargetSolve.ThePreB1177CycleIsDetected` (`tests/test_fx_harness.cpp:770`).
`B1177TargetSolve.AStallOrAnApproachIsNotACycle` (`tests/test_fx_harness.cpp:799`) keeps a stall
and a damped approach with one repeated swing from stopping the solve. With the fix the live
6:1:6 solve converges in four fits, so the cycle path is exercised offline only.

`FxHarness.B1130DarkElfFemaleTargetSolvesAndFailsTheSelfCheck` (`tests/test_fx_harness.cpp:532`)
re-runs 6:1:6 and requires the solve to reach "feet row 615 at column 940" and the self-check to
FAIL with exit 5: rows 355..615, 261 px against the 285 px ratchet
(`/mnt/projects/fable/review/B1177/worker/runs/r2_after_6_1_6.log:123`). That is B1166's original
negative (a wrong body exits 5, not 2 or a signal), now reached on llvmpipe again.

## 4. The default preset did not move

| | fits | placement | frame 0 md5 |
|---|---|---|---|
| B1172 tip, gitlab-runner-2 | 4 (`runs/r2_before_default.log:108-110`) | x -8.920, y 0.850, z 0.626 | c56e2237... |
| this branch, gitlab-runner-2 | 4 (`runs/r2_after_default.log:115-117`) | x -8.920, y 0.850, z 0.626 | c56e2237... |
| this branch, ThinkPad | 4 | x -8.920, y 0.850, z 0.626 | b75f95d8..., equal to B1172's `default_final/frame_00000.png` |

(paths under `/mnt/projects/fable/review/B1177/worker/`; the self-check PASS, 285 px, at
`/mnt/projects/fable/review/B1177/worker/runs/r2_after_default.log:122`.) The b1130 sweep tables are therefore
not re-run: the default solve never entered the singular branch, so its trajectory is unchanged.

## 5. Verification

- Targeted ctest on gitlab-runner-2, `-R 'B1130|B1166TargetAnchor|B1177TargetSolve'`: 11/11
  passed (`/mnt/projects/fable/review/B1177/worker/ctest_runner2.log`); all FX-harness tests
  `-R 'FxHarness|TargetAnchor|TargetSolve'`: 16/16
  (`/mnt/projects/fable/review/B1177/worker/ctest_runner2_all_fxharness.log`).
- Hygiene: 48/48 (`/mnt/projects/fable/review/B1177/worker/hygiene_runner2.log`).
  model_viewer_fx_harness.cpp stays under the 1500-line ceiling: the diagnostic lives in the
  header-only fx_target_anchor.h, which the offline tests include.
- CI: the MR pipeline.

## RULING REQUEST

None.

## FOLLOW-UP

None from this card.
