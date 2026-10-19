# B1037 (#356): lane 2 (Rock64, Lima) vs the Pi visual-gate goldens + lane 3 (ROCK 4D, Panfrost) through the gate once

The measurement B1033 asks for before any per-lane golden question
(`docs/decisions.md` 2026-10-12: "whether lane 2 matches the Pi goldens is a
measurement to take first"). REPORT ONLY: this unit changes no golden, no
manifest zone, no budget, no preset. If a lane is not pixel-identical, the
mechanism is named per the B1006 rule and a class-C ruling request is written
under `RULING REQUEST (orchestrator files it)`.

## Inputs (all verified present at run start, logged in `lane*/device.txt`)

- Binary: the published aarch64 fable build
  `/mnt/projects/fable/ci-arm64/015a9dd057aed831f65dcc1144274a6bba653c50/`
  (built 2026-10-19 04:23Z on gitlab-runner-2; `BUILD_INFO` quoted in
  `device.txt`; `by-branch/fable` points at this sha). Every number in this
  README is from THAT binary on THAT sha.
- Gate invocation source of truth: `scripts/ci/visual_gate.py`
  (`run_benchmark()` with the `--platform pi` `PlatformConfig`),
  `tests/goldens/manifest.json` (benchmark config, `productionConfig`,
  per-zone `extraArgs`/`designatedFrames`), Pi goldens
  `tests/goldens/pi/<zone>/f<n>.png` + `manifest["platforms"]["pi"]`.
- Lanes: `PI_LANE=2` (rock64, RK3328, Mali-450, system Mesa 25.2.8 — the same
  Mesa the Pi runs; `scripts/pi/env.sh` lane table), `PI_LANE=3` (rock4d,
  RK3576, Mali-G52, Panfrost, system Mesa 26.0.8; B1030's EGL-config fix is
  in the build, `docs/ci.md` "The aarch64 SBC lanes"). Neither lane mounts
  this worktree (hermes-ext has no NFS), so the python gate cannot run on the
  device: the device runs the gate's exact benchmark command line, and the
  gate's exact comparer runs locally against the pulled-back frames.

## The recipe (written before any run)

Per lane N ∈ {2, 3}:

1. `PI_LANE=N bash scripts/pi/env.sh` sourcing; `pi_lock_take B1037` (atomic
   noclobber ON the device; non-zero exit = stop).
2. `pi_hdmi_force_on` (the two boards share one KVM; the connector is
   `disconnected`, so the kernel is asked to offer its default modes;
   `pi_hdmi_restore` at cleanup — both env.sh, unchanged from lane_smoke.sh).
3. Copy `b1037_run.sh` to the device's `/tmp`, then
   `pi_remote "bash /tmp/b1037_run.sh /tmp/b1037"` which, per zone
   (qeynos2, gfaydark, befallen, iceclad, timorous — the five manifest gate
   zones, in manifest order) runs EXACTLY the gate's `--platform pi`
   benchmark invocation for that zone:

   ```
   WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1037/cache timeout 600 \
     $D/bin/gles2_zone_benchmark \
       --zone <zone> --eq-path /mnt/projects/EverQuestP1999 \
       --frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2 \
       --dump-png-prefix /tmp/b1037/<zone> \
       --csv /tmp/b1037/<zone>.csv --memory-json /tmp/b1037/<zone>_mem.json \
       --occluder-budget 16 \
       --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9 \
       --camera-pose ... --camera-pose ... --camera-pose ...   (the zone's manifest extraArgs) \
       --drm
   ```

   i.e. `run_benchmark()`'s command line word for word (manifest benchmark
   config + `productionConfig.args` + the zone's `extraArgs` + the pi
   config's `--drm`), with the pose strings copied from
   `tests/goldens/manifest.json` at 015a9dd0 into `b1037_run.sh`. While each
   run is alive the script snapshots `/proc/<pid>/maps` the moment
   libgallium is mapped — the gate's `poll_maps` driver-load proof. Frames are
   moved to the gate's nested layout `<zone>/f<n>.png` afterwards (the
   `run_benchmark` pi branch does the same).
4. Pull the run dir back over ssh to
   `/home/user/kb-evidence/kb-b1037-lane-goldens/lane<N>/`.
5. Run the gate's OWN compare rule locally:
   `python3 /home/user/kb-evidence/kb-b1037-lane-goldens/scripts/compare_lane.py <evidence-dir>`.
   That script imports `scripts/ci/visual_gate.py` and calls, unmodified:
   `compare_images_exact()` (the one compare rule, decoded-pixel RGB),
   `assert_memory_budgets()`, `assert_memory_ledger()` and
   `report_rss_target_gap()` (256 MiB ceiling assert + 64 MiB target gap
   printout), against the committed Pi goldens
   `tests/goldens/pi/<zone>/f<n>.png` and the `platforms.pi` section. It also
   records draws/stateChanges/polys per designated frame vs the Pi section's
   blessed metrics. `compare_report.txt` is program output, never typed.
6. `pi_lock_release B1037`; `pi_hdmi_restore`; verify `pgrep -af willeq`
   empty on the lane (no leftover processes; no reboot).

No golden, manifest, preset, budget or third_party file is touched anywhere
in this chain; the lanes are left as found (HDMI connector status restored
from the env.sh record; lock released owner-checked).

## Results

(Per-zone tables below are transcribed from each lane's `compare_report.txt`;
raw frames, CSVs, memory JSONs, logs, maps proofs and thermal blocks are in
`/home/user/kb-evidence/kb-b1037-lane-goldens/lane2/` and `.../lane3/`.)

### Lane 2 (Rock64, Lima 25.2.8)

(pending)

### Lane 3 (ROCK 4D, Panfrost, Mesa 26.0.8)

(pending)
