#!/usr/bin/env python3
"""P41: Pi lighting-lever A/B at e205 (stress load) + resolution ride-along
(400x300, shipped preset, e5) on the canonical befallen replay.

Mirrors P38's methodology exactly (same replay file, same shipped-preset
flags, --gl-counters) so these numbers are directly comparable to P38's e5
table in refactor/perf_truth_audit.md. e5/800x600 data is REUSED from P38
(befallen_budget_moving_e5_shipped.csv / befallen_lightweight_lighting_shipped.csv)
rather than re-measured, per the plan's invocation budget.
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p41")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p41_lighting_lever"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p41_pi_scratch")

PROD_ARGS = pc.production_config_args()
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
SHIPPED_PRESET_ARGS = ["--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]
STRESS_N = 205  # P11-era stress calibration


def run_replay(label, extra_args, env_prefix="", res=(800, 600), frames=1000, entities=STRESS_N):
    remote_csv = f"/tmp/p41_befallen_{label}.csv"
    remote_log = f"/tmp/p41_befallen_{label}.log"
    remote_path_file = "/tmp/p41_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file]
    cmd += ["--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += ["--animate"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(res[0]), str(res[1]), "--gl-counters", "--csv", remote_csv]
    remote_cmd = (env_prefix + " " if env_prefix else "") + " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [befallen/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"befallen_{label}.csv"
    pc.rsync_from(remote_csv, OUT_DIR)
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    return local_csv


def summarize(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))

    def col(name, cast=float):
        return [cast(r[name]) for r in rows]

    def p95_of(vals):
        s = sorted(vals)
        idx = min(len(s) - 1, int(len(s) * 0.95))
        return s[idx]

    out = {"n_frames": len(rows)}
    for name in ["total_ms", "submission_ms", "gpu_ms", "collect_cull_ms", "swap_ms"]:
        vals = col(name)
        out[f"{name}_avg"] = sum(vals) / len(vals)
        out[f"{name}_p95"] = p95_of(vals)
    for name in ["draws", "polys"]:
        vals = col(name, int)
        out[f"{name}_avg"] = sum(vals) / len(vals)
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", choices=["lighting_e205", "res_400x300", "swap_interval"], required=True)
    args = ap.parse_args()

    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin}")
    print(f"  using ARM binary: {local_bin}")

    if args.plan == "lighting_e205":
        plan = [
            ("e205_perpixel_shipped", SHIPPED_PRESET_ARGS, "", dict(entities=STRESS_N)),
            ("e205_lightweight_shipped", SHIPPED_PRESET_ARGS + ["--lighting-model", "lightweight"], "", dict(entities=STRESS_N)),
        ]
    elif args.plan == "res_400x300":
        plan = [
            ("res_400x300_e5_shipped", SHIPPED_PRESET_ARGS, "", dict(entities=5, res=(400, 300))),
        ]
    elif args.plan == "swap_interval":
        plan = [
            ("swapinterval0_e5_shipped", SHIPPED_PRESET_ARGS, "WILLEQ_SWAP_INTERVAL_PROTOTYPE=0", dict(entities=5)),
        ]

    print(f"[plan] {len(plan)} invocations this call")
    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    try:
        for label, extra_args, env_prefix, kwargs in plan:
            runner.before_invocation(label)
            csv_path = run_replay(label, extra_args, env_prefix, **kwargs)
            runner.after_invocation(label)
            results[label] = summarize(csv_path)
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

    print("\n=== Summary ===")
    for key, s in results.items():
        print(f"{key:28s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} swap_avg={s['swap_ms_avg']:6.3f} "
              f"draws_avg={s['draws_avg']:6.1f} polys_avg={s['polys_avg']:7.1f}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
