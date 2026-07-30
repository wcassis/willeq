#!/usr/bin/env python3
"""P39 Pi spot-check: does the occlusionMaxOccluderRegions config-plumbing
fix (JSON preset now carries the P14-blessed 256, C++ default unchanged)
actually move the canonical befallen e205 whole-run p95 toward P14's
32.3ms, vs ~35.3ms without?

This is NOT a re-bless (P14 already blessed 256; no new lever is being
proposed here) -- it is a narrow reproduction of P14's own already-blessed
A/B (budget 16 vs 256) on the exact canonical replay, run under THIS
worktree's rebuilt-with-the-plumbing-fix ARM binary, to confirm the fixed
config plumbing doesn't change the shape of that already-established
result. Two invocations only (well under the 6-invocation P39 budget):
  1. occ16   -- --occluder-budget 16 (control, matches the pre-P14/
               plumbing-blind default -- what P38 measured as ~35.3ms)
  2. occ256  -- --occluder-budget 256 (the now-actively-shipping value)

Same bespoke --path-file moving-replay pattern as P14/P38's own scripts
(refactor/scratch/p14_pi_occluder_bf472.py, p38_pi_time_budget_audit.py).
IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
before importing pi_common (the pi_common.py default points at
/mnt/projects/fable/main -- the ARM_BUILD_BIN_DIR trap, P02).
"""
import csv
import os
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
os.environ.setdefault("ARM_BUILD_BIN_DIR", str(PROJECT_DIR / "build-arm-noble" / "bin"))
os.environ.setdefault("PROJECT_DIR", str(PROJECT_DIR))
os.environ.setdefault("MANIFEST_PATH", str(PROJECT_DIR / "tests/goldens/manifest.json"))
os.environ.setdefault("ARTIFACTS_DIR", str(PROJECT_DIR / "benchmarks/pi_runs"))
os.environ.setdefault("BASELINE_DIR", str(PROJECT_DIR / "benchmarks/pi_baseline"))
os.environ.setdefault("PI_HOST", "10.0.30.192")
os.environ.setdefault("PI_USER", "user")
os.environ.setdefault("PI_SSH_OPTS", "-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new")
os.environ.setdefault("PI_EQ_PATH", "/mnt/projects/EverQuestP1999")
os.environ.setdefault("PI_EQE1_PATH", "/mnt/projects/EverQuest_ETC1")
os.environ.setdefault("PI_REMOTE_TMP", "/tmp")
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p39")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p39_occluder_reland"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p39_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = {"width": 800, "height": 600}
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
CALIBRATED_N = 205  # P11b/P14 calibration -- "e205"


def run_replay(zone, path_file, extra_args, label, entities=CALIBRATED_N, seed=42, frames=1000):
    remote_csv = f"/tmp/p39_{zone}_{label}.csv"
    remote_log = f"/tmp/p39_{zone}_{label}.log"
    remote_path_file = f"/tmp/p39_{zone}.path"
    pc.scp_to(path_file, remote_path_file)
    cmd = [
        pc.PI_REMOTE_BIN,
        "--zone", zone,
        "--eq-path", pc.PI_EQ_PATH,
        "--path-file", remote_path_file,
        "--frames", str(frames),
    ] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", str(seed)]
    cmd += extra_args
    cmd += ["--drm", "--res", str(RES["width"]), str(RES["height"]), "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed ({zone}/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [{zone}/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"{zone}_{label}.csv"
    pc.rsync_from(remote_csv, OUT_DIR)
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    return local_csv


def summarize(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))
    total_ms = [float(r["total_ms"]) for r in rows]
    submission_ms = [float(r["submission_ms"]) for r in rows]
    gpu_ms = [float(r["gpu_ms"]) for r in rows]

    def p95_of(vals):
        s = sorted(vals)
        idx = min(len(s) - 1, int(len(s) * 0.95))
        return s[idx]

    return {
        "n_frames": len(rows),
        "total_ms_avg": sum(total_ms) / len(total_ms),
        "total_ms_p95": p95_of(total_ms),
        "submission_ms_avg": sum(submission_ms) / len(submission_ms),
        "submission_ms_p95": p95_of(submission_ms),
        "gpu_ms_avg": sum(gpu_ms) / len(gpu_ms),
        "gpu_ms_p95": p95_of(gpu_ms),
    }


def main():
    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh in THIS worktree first")
    print(f"  using ARM binary: {local_bin}")

    plan = [
        ("befallen", BEFALLEN_PATH,
         ["--animate", "--merge-zone-draws", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH,
          "--occluder-budget", "16"], "occ16"),
        ("befallen", BEFALLEN_PATH,
         ["--animate", "--merge-zone-draws", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH,
          "--occluder-budget", "256"], "occ256"),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session, P39 budget <=6)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    import random
    order = list(range(len(plan)))
    random.seed(39)
    random.shuffle(order)
    print(f"[order] randomized invocation order: {order}")
    try:
        for idx in order:
            zone, path_file, extra_args, label = plan[idx]
            key = f"{zone}_{label}"
            runner.before_invocation(key)
            csv_path = run_replay(zone, path_file, extra_args, label)
            runner.after_invocation(key)
            results[key] = summarize(csv_path)
            time.sleep(pc.H04_COOLDOWN_SEC)
    except pc.SessionAbort as e:
        governor.__exit__(None, None, None)
        print(f"[ABORT] {e}")
        raise
    except BaseException:
        governor.__exit__(None, None, None)
        raise
    else:
        governor.__exit__(None, None, None)

    print(f"\n[session] {runner.count} invocations completed, CmaFree samples: "
          f"{[s[1] for s in runner.cma.samples]}")

    print("\n=== Whole-run summary (befallen, e205, canonical replay) ===")
    for key in results:
        s = results[key]
        print(f"{key:20s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
