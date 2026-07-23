#!/usr/bin/env python3
"""H04 -- compare a Pi timing run against the committed baseline
(benchmarks/pi_baseline/), or bless a run as the new baseline.

Usage:
  pi_bench_compare.py --run-name NAME              # compare NAME against baseline
  pi_bench_compare.py --run-name NAME --bless       # write NAME as the new baseline
  pi_bench_compare.py                               # compare the most recent run dir

Checks per zone/variant:
  - p95 total_ms within TOLERANCE_PCT of the baseline (default set in
    benchmarks/pi_baseline/tolerance.json, written at --bless time).
  - draws/state_changes/polys exact match per designated-frame position.

Exit code: 0 PASS, 1 FAIL/mismatch, 2 usage/missing-data error.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pi_common as pc

DEFAULT_TOLERANCE_PCT = 20.0


def latest_run_dir():
    runs = sorted([p for p in pc.ARTIFACTS_DIR.iterdir() if p.is_dir()]) if pc.ARTIFACTS_DIR.exists() else []
    if not runs:
        raise RuntimeError(f"no run directories found under {pc.ARTIFACTS_DIR}")
    return runs[-1]


def load_summary(run_dir):
    summary_path = run_dir / "summary.json"
    if not summary_path.exists():
        raise RuntimeError(f"{summary_path} missing -- run pi_bench_run.py first")
    with open(summary_path) as f:
        return json.load(f)


def bless(run_dir, tolerance_pct):
    summary = load_summary(run_dir)
    pc.BASELINE_DIR.mkdir(parents=True, exist_ok=True)
    for f in pc.BASELINE_DIR.glob("*.csv"):
        f.unlink()
    for f in run_dir.glob("*.csv"):
        (pc.BASELINE_DIR / f.name).write_bytes(f.read_bytes())
    baseline_meta = {
        "blessed_from_run": run_dir.name,
        "tolerance_pct": tolerance_pct,
        "zones": summary["zones"],
    }
    with open(pc.BASELINE_DIR / "baseline.json", "w") as f:
        json.dump(baseline_meta, f, indent=2, sort_keys=True)
    print(f"Blessed baseline from run '{run_dir.name}' -> {pc.BASELINE_DIR}")
    print(f"Tolerance set to +/-{tolerance_pct}% on p95 frame time.")


def compare(run_dir):
    baseline_path = pc.BASELINE_DIR / "baseline.json"
    if not baseline_path.exists():
        raise RuntimeError(f"no baseline at {baseline_path} -- run with --bless first")
    with open(baseline_path) as f:
        baseline = json.load(f)
    tolerance_pct = baseline["tolerance_pct"]

    summary = load_summary(run_dir)

    ok = True
    print(f"Comparing run '{run_dir.name}' against baseline (blessed from "
          f"'{baseline['blessed_from_run']}'), tolerance +/-{tolerance_pct}%\n")

    for zone, variants in baseline["zones"].items():
        cur_variants = summary["zones"].get(zone, {})
        for tag, base in variants.items():
            cur = cur_variants.get(tag)
            if cur is None:
                print(f"FAIL  {zone}/{tag}: missing in current run")
                ok = False
                continue

            base_p95 = base["total_ms_p95"]
            cur_p95 = cur["total_ms_p95"]
            pct_delta = 100.0 * (cur_p95 - base_p95) / base_p95
            within = abs(pct_delta) <= tolerance_pct
            status = "PASS" if within else "FAIL"
            print(f"{status}  {zone}/{tag}: p95 {cur_p95:.2f}ms vs baseline {base_p95:.2f}ms "
                  f"({pct_delta:+.1f}%, tolerance +/-{tolerance_pct}%) "
                  f"-- fps(p95)={cur['fps_p95']:.1f}")
            ok = ok and within

            for pos_str, base_pose in base["per_pose"].items():
                cur_pose = cur["per_pose"].get(pos_str)
                if cur_pose is None:
                    print(f"FAIL  {zone}/{tag} pose {pos_str}: missing in current run")
                    ok = False
                    continue
                for key in ("draws", "state_changes", "polys"):
                    if base_pose[key] != cur_pose[key]:
                        print(f"FAIL  {zone}/{tag} pose {pos_str}: {key} {cur_pose[key]} != "
                              f"baseline {base_pose[key]}")
                        ok = False

    print("\n" + ("PASS -- all zones within tolerance, draw/state/poly counts exact." if ok
                   else "FAIL -- see above."))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-name", default=None)
    ap.add_argument("--bless", action="store_true")
    ap.add_argument("--tolerance-pct", type=float, default=DEFAULT_TOLERANCE_PCT)
    args = ap.parse_args()

    run_dir = (pc.ARTIFACTS_DIR / args.run_name) if args.run_name else latest_run_dir()
    if not run_dir.exists():
        print(f"run dir not found: {run_dir}", file=sys.stderr)
        sys.exit(2)

    if args.bless:
        bless(run_dir, args.tolerance_pct)
        sys.exit(0)

    ok = compare(run_dir)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
