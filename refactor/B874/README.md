# B874 -- pose-log evidence (audit, task/pose-log-e2e-test)

**Live run (GitLab #218, 2026-09-29): see `live/README.md`.** A zoned-in,
rendered client driven by `--script` wrote a real `--pose-log`, checked by
`scripts/pose_live_capture.sh` (ctest `PoseLogLiveCapture`, label `live`)
against the server's own view (`#loc`, `#save`d `character_data`) and
kChaseRig. Position and heading agree with the server to <= 0.005 units
through the documented transform; two findings (the rendered heading ignores
`/turn`, movestate stays idle on a `/move` walk) kept that test red (37/2).
**Both were fixed as B911/B912 (GitLab #230/#231) and the rerun on the VM
lane is 39/0: `live/run-20260929T042131Z/README.md`.** Still owed:
Deliverable A, the server-authoritative pose log for the OFFICIAL client. The
section below is the earlier no-server run.

`pose_e2e_chase_excerpt.tsv` is the header plus the first 20 of 24 rows of a
real `--pose-log` TSV, copied byte-for-byte out of a run. Nothing in it was
typed or synthesised.

- **Commit**: `4dd064d8c211066fa84e2612e575dac06ae59169` (branch
  `task/pose-log-e2e-test`), Release build.
- **Host**: `dev-u24` (x86, Mesa llvmpipe through surfaceless EGL).
- **Date**: 2026-09-28 (the `ts_unix_ms` column is that run's wall clock).
- **Command** (from the repo root, after building `test_pose_log_e2e`):

```bash
WILLEQ_POSE_E2E_KEEP=/path/to/pose_e2e_chase.tsv \
  build/bin/test_pose_log_e2e \
  --gtest_filter=PoseLogE2ETest.ChaseRunWritesTheCommandedPoseInTheDocumentedUnits
```

## What the run is

`tests/test_pose_log_e2e.cpp`: a real `IrrlichtRenderer` brought up by
`initLoadingScreen()` on the headless GLES2 device, the loading screen
dismissed, the pose commanded through `setPlayerPosition(428, -74, 3.119,
123.25)` and `/camera chase`, then 24 real `processFrame()` calls, each
followed by the `PoseLogWriter::onRenderedFrame()` call `Application::render()`
makes. The rows are what that writer wrote.

## What it is NOT

- **Not a zoned-in client.** No zone is loaded and no server is involved, so
  the camera is the unobstructed Chase fit (no wall pull-in) and movestate is
  idle throughout. The one production statement not on this path is the
  single `onRenderedFrame(*m_renderer)` call in `Application::render()`.
- A live run was not taken: the eqemu local lane was up, but its lock was
  held by another owner (`T504-load-step-audit`), and the lane is
  single-tenant.

## Reading a row

Client-internal EQ frame, Z up (see `include/client/graphics/pose_log_format.h`):
camera at the Chase fit (`kChaseRig`: pitch 6 deg, distance 4.0, look-at
7.0 above the feet) behind heading 123.25: camera 3.978 back along the
heading and 0.418 above the look-at. To join these rows to the server-side
log, swap X/Y and use `server_heading = (128 - heading0_512) mod 512`.
