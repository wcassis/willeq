#!/usr/bin/env python3
"""H04 -- run one full timed pass on the Orange Pi: all three golden zones,
with and without EQE1 (when archives are present), 60 static-pose frames per
zone/variant (repeats of the manifest's poses -- see pi_common.pose_flags_repeated).

Usage: pi_bench_run.py [--run-name NAME] [--no-eqe1]

Writes benchmarks/pi_runs/<run-name>/ (gitignored) containing per
zone/variant CSV + log + a summary.json. Prints the headline numbers.

I/O rule: during the timed loop, ALL benchmark output stays on the Pi's
/tmp tmpfs. Results are copied to NFS only in the post-run phase below.
"""
import argparse
import datetime
import json
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pi_common as pc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-name", default=None)
    ap.add_argument("--no-eqe1", action="store_true", help="skip the EQE1 A/B variant")
    ap.add_argument("--scratch", default="/tmp/h04_scratch", help="local scratch dir for the stripped binary")
    args = ap.parse_args()

    run_name = args.run_name or datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
    run_dir = pc.ARTIFACTS_DIR / run_name
    run_dir.mkdir(parents=True, exist_ok=True)

    manifest = pc.preflight()
    pc.investigate_console_drm()
    pc.strip_and_stage_binary(Path(args.scratch))

    resolution = manifest["benchmark"]["resolution"]
    total_frames = pc.H04_TOTAL_FRAMES

    variants = [False] if args.no_eqe1 else [False, True]

    results = {"run_name": run_name, "zones": {}}

    with pc.GovernorPin():
        remote_files = []
        print(f"\n=== Timed phase: {len(manifest['zones'])} zones x {len(variants)} variant(s), "
              f"{total_frames} frames each ===")
        for zone_cfg in manifest["zones"]:
            zone = zone_cfg["zone"]
            results["zones"][zone] = {}
            for use_eqe1 in variants:
                tag = "eqe1" if use_eqe1 else "rgba"
                remote_csv = f"{pc.PI_REMOTE_TMP}/h04_{zone}_{tag}.csv"
                remote_log = f"{pc.PI_REMOTE_TMP}/h04_{zone}_{tag}.log"
                if use_eqe1:
                    # Skip cleanly if this zone has no EQE1 archives (checked in preflight
                    # only as a warning -- re-check here defensively before the ssh call).
                    check = pc.ssh_run(
                        f"test -f {pc.PI_EQE1_PATH}/{zone}.eqe1 && echo YES || echo NO",
                        check=False,
                    )
                    if "YES" not in check.stdout:
                        print(f"  [{zone} eqe1=True] SKIPPED (no {zone}.eqe1 archive)")
                        continue
                pc.run_zone_variant(zone_cfg, resolution, total_frames, use_eqe1, remote_csv, remote_log)
                remote_files.append((remote_csv, remote_log))
                time.sleep(pc.H04_COOLDOWN_SEC)

        print(f"\n=== Timed phase complete. Copying results from Pi /tmp -> {run_dir} (post-run, untimed) ===")
        pc.rsync_from(f"{pc.PI_REMOTE_TMP}/h04_*.csv", run_dir)
        pc.rsync_from(f"{pc.PI_REMOTE_TMP}/h04_*.log", run_dir)
        # Clean the Pi's tmpfs of everything this run wrote (binary stays --
        # cheap to leave for the next pass in the same session; removed at
        # the very end of the whole session by the caller/finish step).
        pc.ssh_run("rm -f /tmp/h04_*.csv /tmp/h04_*.log /tmp/h04_*.json", check=False)

    # Governor restored by the `with` block's __exit__ regardless of outcome above.

    print("\n=== Summary ===")
    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        n_poses = len(zone_cfg["designatedFrames"])
        for use_eqe1 in variants:
            tag = "eqe1" if use_eqe1 else "rgba"
            csv_path = run_dir / f"h04_{zone}_{tag}.csv"
            if not csv_path.exists():
                continue
            rows = pc.read_csv_metrics(csv_path)
            summary = pc.summarize_csv(rows, n_poses)
            results["zones"][zone][tag] = summary
            flag = " *** BELOW 30 FPS ***" if summary["fps_p95"] and summary["fps_p95"] < 30 else ""
            print(f"  {zone:12s} {tag:4s}  p95={summary['total_ms_p95']:.2f}ms  "
                  f"fps(p95)={summary['fps_p95']:.1f}{flag}")

    summary_path = run_dir / "summary.json"
    with open(summary_path, "w") as f:
        json.dump(results, f, indent=2, sort_keys=True)
    print(f"\nWrote {summary_path}")
    print(f"Run artifacts: {run_dir}")

    # Leave no state on the Pi's SD card / tmpfs beyond the run itself.
    pc.ssh_run(f"rm -f {pc.PI_REMOTE_BIN}", check=False)
    print(f"[cleanup] removed staged binary {pc.PI_REMOTE_BIN} from the Pi.")


if __name__ == "__main__":
    main()
