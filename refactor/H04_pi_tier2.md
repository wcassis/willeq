# H04 — Orange Pi tier-2 automated frame-timing baseline

The Pi is online: `ssh user@10.0.30.192` (key auth, sudo available, same
NFS at /mnt/projects — no file copying needed). Build the automated
hardware-timing tier: run the cross-compiled benchmark on the Pi over ssh,
collect frame-timing CSVs, compare against a stored baseline. First run
establishes the baseline; later runs regression-gate.

## Facts (verified by orchestrator)

- armv7l, mainline 6.12.58-sunxi, Lima DRM (/dev/dri/card0,card1,renderD128),
  494 MB RAM, cpufreq governor `ondemand` (must pin `performance` during
  runs and restore — sudo available).
- ARM build lands at build-arm-noble/bin/ via scripts/build-arm-noble.sh
  (docker image willeq-arm-noble:latest cached; orchestrator has a build
  running — verify it completed, re-run if needed).
- The Pi runs the DRM/KMS path (EQT_DRM=ON) — the benchmark needs the
  console/DRM master. Investigate what the Pi's current console state is
  (getty on tty? X running?) — the benchmark must be able to acquire DRM;
  document what you find and automate whatever is needed (chvt, stopping a
  getty, etc. — sudo is available; keep it reversible and restore state).
- Timing on Pi is NOT pixel-golden work: llvmpipe goldens are VM-only. This
  tier asserts TIMING (and optionally draw/state counts, which should match
  the manifest exactly since the scene is deterministic — verify whether
  ETC1-vs-RGBA texture pipeline differences change draw counts; if they do,
  record Pi-specific expected counts in the Pi baseline, don't reuse the VM
  manifest).

## Design

1. `scripts/pi/env.sh` — PI_HOST=10.0.30.192, PI_USER=user, paths; all
   scripts take env overrides.
2. `scripts/pi/bench_pi.sh`:
   - Preflight: ssh reachable; binary present and newer-than-source warning
     if stale; zone assets reachable from Pi (/mnt/projects/EverQuestP1999).
   - Pin `performance` governor (restore on exit via trap).
   - Run build-arm-noble/bin/gles2_zone_benchmark for the three golden zones
     using the SAME poses/extraArgs as tests/goldens/manifest.json (read
     them from the manifest — single source of truth), 60 frames each,
     writing CSVs to a Pi-run artifacts dir (NFS: e.g.
     benchmarks/pi_runs/<timestamp>/ — gitignored; add the gitignore).
   - Optional EQE1: if ETC1 flat packs exist for these zones (investigate
     /mnt/projects/EverQuest_ETC1 or wherever eqe1 archives live —
     resolveEQE1Archives logic / config docs say how the Pi normally runs),
     run both with and without --eqe1-path and record both.
3. `scripts/pi/compare_pi.sh` — compare a run's CSVs against
   `benchmarks/pi_baseline/` (committed, small): p95 frame-time per zone
   within tolerance (suggest ±20% initially — Pi thermals vary; document
   observed variance across 3 consecutive runs and set tolerance from
   evidence), draw/poly counts exact vs the Pi baseline. `--bless` writes
   the baseline.
4. Run the whole thing 3× to measure run-to-run variance; then bless the
   baseline; then run compare once more to confirm PASS.
5. Report the headline numbers: fps/frame-time per golden zone on real
   Mali400 hardware. If any zone is below 30 fps at these poses, flag it
   loudly — that's the whole project's KPI.

## Rules

- **I/O rule (from Will — fps has been measurably hurt by this):** during
  timed runs, ALL benchmark output (CSVs, logs, any diagnostic capture)
  goes to /tmp on the Pi (242MB tmpfs — verified) — NEVER to NFS or the SD
  card while frames are being timed. Copy results from /tmp to the NFS
  artifacts dir only AFTER the timed portion completes (rsync/cp in the
  script's post-run phase). Keep client/benchmark log verbosity minimal
  during timed runs; if a verbose diagnostic capture is ever needed, do it
  as a separate untimed run. Watch the tmpfs ceiling.
- Nothing on the Pi's SD card gets modified except transient state
  (governor, console) — binaries/assets/output all live on NFS. Any
  apt/system change on the Pi: report needed, don't install without noting.
- The Pi may be in active use for nothing else, but be conservative:
  restore governor/console state on exit (traps), leave no processes
  running.
- ssh commands: BatchMode, ConnectTimeout, no interactive prompts.

## Verification

- 3 runs' CSVs collected; variance documented; baseline blessed; compare
  PASS on a 4th run.
- Governor/console state verified restored after runs.
- Default VM build + ctest untouched/unregressed (scripts only + possibly
  .gitignore; no production code expected — if you need a benchmark-tool
  change, apply the usual bar).

## Review

(executor fills in: console/DRM findings, EQE1 findings, variance data,
tolerance chosen with rationale, headline fps numbers, deviations)
