#!/usr/bin/env python3
"""H04/P02 -- run one full timed pass on the Orange Pi: all three golden
zones, with and without EQE1 (when archives are present), 60 static-pose
frames per zone/variant (repeats of the manifest's poses -- see
pi_common.pose_flags_repeated).

Usage: pi_bench_run.py [--run-name NAME] [--no-eqe1] [--repeats N] [--seed N]

Writes benchmarks/pi_runs/<run-name>/ (gitignored) containing per
zone/variant/rep CSV + log, a cma_telemetry.tsv, and a summary.json. Prints
the headline numbers.

I/O rule: during the timed loop, ALL benchmark output stays on the Pi's
/tmp tmpfs. Results are copied to NFS only in the post-run phase below.

P02 panic-safe protocol (permanent, see pi_common.py and
refactor/P02_hardware_resume.md):
  - Zone/variant order is RANDOMIZED/INTERLEAVED per pass (fixes H3, the
    fixed rgba-then-eqe1 order confound P01 confirmed real and significant
    -- every prior H04 baseline number had this uncontrolled order effect
    baked in).
  - CmaFree/MemAvailable sampled before and after every invocation, logged
    to cma_telemetry.tsv; session aborts cleanly (governor restored) if
    CmaFree drops below the floor or ratchets down monotonically.
  - A hard 12-invocation-per-session cap triggers a scripted reboot +
    wait-for-ssh + NFS-mount reverification before continuing, closing and
    reopening the governor pin around the reboot.
"""
import argparse
import datetime
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pi_common as pc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-name", default=None)
    ap.add_argument("--no-eqe1", action="store_true", help="skip the EQE1 A/B variant")
    ap.add_argument("--repeats", type=int, default=1,
                     help="repeats per zone/variant combination this pass, randomized/"
                          "interleaved across the whole work list, not run back-to-back "
                          "(default: 1)")
    ap.add_argument("--seed", type=int, default=None,
                     help="RNG seed for the randomized order (default: system entropy)")
    ap.add_argument("--session-cap", type=int, default=pc.MAX_INVOCATIONS_PER_SESSION,
                     help="benchmark invocations per session before a scripted reboot "
                          f"cycle (default: {pc.MAX_INVOCATIONS_PER_SESSION}). Lower this "
                          "for EQE1-heavy runs: EQE1 invocations can drain CmaFree by "
                          "tens of MB each (P02 finding) and only a reboot restores it.")
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
    zones = [zc["zone"] for zc in manifest["zones"]]
    zone_cfg_by_name = {zc["zone"]: zc for zc in manifest["zones"]}

    # P02: randomized/interleaved order across the whole session, not a
    # fixed per-zone rgba-then-eqe1 sequence.
    work_items = pc.randomized_variant_order(zones, variants, repeats=args.repeats, rng_seed=args.seed)
    print(f"\n=== Randomized/interleaved order for this session ({len(work_items)} invocations) ===")
    print("  " + ", ".join(f"{z}/{'eqe1' if v else 'rgba'}" for z, v in work_items))

    results = {"run_name": run_name, "zones": {}, "seed": args.seed}
    rep_index = {}  # (zone, tag) -> next rep number, so filenames don't collide
    produced = {}   # (zone, tag) -> list of local csv paths pulled to run_dir
    skipped = set()

    cma_log = run_dir / "cma_telemetry.tsv"

    def label_fn(item):
        zone, use_eqe1 = item
        tag = "eqe1" if use_eqe1 else "rgba"
        return f"{zone}/{tag}#{rep_index.get((zone, tag), 0)}"

    def run_one(item):
        zone, use_eqe1 = item
        zone_cfg = zone_cfg_by_name[zone]
        tag = "eqe1" if use_eqe1 else "rgba"
        rep = rep_index.get((zone, tag), 0)
        rep_index[(zone, tag)] = rep + 1

        if use_eqe1 and (zone, tag) not in produced and (zone, tag) not in skipped:
            check = pc.ssh_run(
                f"test -f {pc.PI_EQE1_PATH}/{zone}.eqe1 && echo YES || echo NO",
                check=False,
            )
            if "YES" not in check.stdout:
                print(f"  [{zone} eqe1=True] SKIPPED (no {zone}.eqe1 archive)")
                skipped.add((zone, tag))
        if (zone, tag) in skipped:
            return

        remote_csv = f"{pc.PI_REMOTE_TMP}/h04_{zone}_{tag}_r{rep}.csv"
        remote_log = f"{pc.PI_REMOTE_TMP}/h04_{zone}_{tag}_r{rep}.log"
        pc.run_zone_variant(zone_cfg, resolution, total_frames, use_eqe1, remote_csv, remote_log)
        pc.rsync_from(remote_csv, run_dir)
        pc.rsync_from(remote_log, run_dir)
        pc.ssh_run(f"rm -f {remote_csv} {remote_log}", check=False)
        produced.setdefault((zone, tag), []).append(run_dir / f"h04_{zone}_{tag}_r{rep}.csv")

    try:
        pc.run_session(work_items, run_one, cma_log_path=str(cma_log), label_fn=label_fn,
                       cap=args.session_cap,
                       post_reboot_fn=lambda: pc.strip_and_stage_binary(Path(args.scratch)))
    except pc.SessionAbort as e:
        print(f"\n=== SESSION ABORT (panic-safe protocol): {e} ===")
        results["aborted"] = str(e)
        summary_path = run_dir / "summary.json"
        with open(summary_path, "w") as f:
            json.dump(results, f, indent=2, sort_keys=True)
        print(f"Partial results (if any) + abort reason written to {summary_path}")
        sys.exit(3)

    print("\n=== Summary (pooled across repeats where --repeats > 1) ===")
    for zone in zones:
        zone_cfg = zone_cfg_by_name[zone]
        n_poses = len(zone_cfg["designatedFrames"])
        results["zones"].setdefault(zone, {})
        for use_eqe1 in variants:
            tag = "eqe1" if use_eqe1 else "rgba"
            csv_paths = produced.get((zone, tag), [])
            if not csv_paths:
                continue
            summary = pc.summarize_multi_csv(csv_paths, n_poses)
            results["zones"][zone][tag] = summary
            flag = " *** BELOW 30 FPS ***" if summary["fps_p95"] and summary["fps_p95"] < 30 else ""
            reps_str = ",".join(f"{v:.1f}" for v in summary["reps"]) if len(summary["reps"]) > 1 else ""
            print(f"  {zone:12s} {tag:4s}  p95={summary['total_ms_p95']:.2f}ms  "
                  f"fps(p95)={summary['fps_p95']:.1f}{flag}"
                  + (f"  [per-rep p95: {reps_str}]" if reps_str else ""))

    summary_path = run_dir / "summary.json"
    with open(summary_path, "w") as f:
        json.dump(results, f, indent=2, sort_keys=True)
    print(f"\nWrote {summary_path}")
    print(f"CMA telemetry: {cma_log}")
    print(f"Run artifacts: {run_dir}")

    # Leave no state on the Pi's SD card / tmpfs beyond the run itself.
    pc.ssh_run(f"rm -f {pc.PI_REMOTE_BIN}", check=False)
    print(f"[cleanup] removed staged binary {pc.PI_REMOTE_BIN} from the Pi.")


if __name__ == "__main__":
    main()
