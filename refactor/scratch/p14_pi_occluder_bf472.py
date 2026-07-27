#!/usr/bin/env python3
"""P14 Phase B: bf472-targeted occluder-budget sweep on the Pi.

Motivation (P11b): the full stack (merge + real animated composites) puts
befallen's whole-run p95 2.02ms over the 30fps budget, and the residual is
GPU-bound (GPU sync 49.6% of the p95 frame) -- driven by bf472's 704-region
dense zone-geometry pose. P11's "occluder not worth landing" verdict
(0.9-2.7%) was measured on the WHOLE-PATH AVERAGE, without the full stack,
when CPU submission still dominated. This unit re-tests the occluder-budget
lever under the new conditions: full stack + a budget sweep, with bf472 as
the headline pose.

Variants (all --animate --merge-zone-draws --composite-scenegraph, the
P11b full-stack config, calibrated N=205):
  budget 16   (default/control -- REUSED from P11b's own
               befallen_combined_animate_merge_composite.csv, schema-verified
               identical below -- budget 16 IS the P11b full-stack run)
  budget 64   (NEW)
  budget 256  (NEW)
  budget uncapped (NEW)

P02 protocol via scripts/pi/pi_common.py primitives, same bespoke
--path-file moving-replay pattern as P09c/P11/P11b's own scripts.

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
(the pi_common.py default points at /mnt/projects/fable/main -- the
ARM_BUILD_BIN_DIR trap) before importing pi_common.
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p14")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p14_occluder_bf472"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p14_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = {"width": 800, "height": 600}
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
CALIBRATED_N = 205  # P11b calibration
PINNED_FRAMES = [355, 472, 626, 791, 917, 987]


def run_replay(zone, path_file, extra_args, label, entities=CALIBRATED_N, seed=42, frames=1000):
    remote_csv = f"/tmp/p14_{zone}_{label}.csv"
    remote_log = f"/tmp/p14_{zone}_{label}.log"
    remote_path_file = f"/tmp/p14_{zone}.path"
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
    collect_ms = [float(r["collect_cull_ms"]) for r in rows]
    occ = [int(r["regions_occluded"]) for r in rows]
    drawn = [int(r["regions_drawn"]) for r in rows]

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
        "collect_ms_avg": sum(collect_ms) / len(collect_ms),
        "collect_ms_p95": p95_of(collect_ms),
        "draws_avg": sum(int(r["draws"]) for r in rows) / len(rows),
        "regions_occluded_avg": sum(occ) / len(occ),
        "regions_drawn_avg": sum(drawn) / len(drawn),
        "rows": rows,
    }


def main():
    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh in THIS worktree first")
    print(f"  using ARM binary: {local_bin}")

    # NEW variants only -- budget 16 is reused from P11b's own
    # befallen_combined_animate_merge_composite.csv (schema-verified
    # identical, same exact command modulo --occluder-budget which defaults
    # to 16 when unset -- see the plan's step 3).
    plan = [
        ("befallen", BEFALLEN_PATH,
         ["--animate", "--merge-zone-draws", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH,
          "--occluder-budget", "64"], "occ64"),
        ("befallen", BEFALLEN_PATH,
         ["--animate", "--merge-zone-draws", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH,
          "--occluder-budget", "256"], "occ256"),
        ("befallen", BEFALLEN_PATH,
         ["--animate", "--merge-zone-draws", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH,
          "--occluder-budget", "uncapped"], "occuncapped"),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    import random
    order = list(range(len(plan)))
    random.seed(14)
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

    print("\n=== Whole-run summary (NEW variants only -- budget16 is the reused P11b control) ===")
    for key in results:
        s = results[key]
        print(f"{key:30s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} collect_avg={s['collect_ms_avg']:6.3f} "
              f"draws_avg={s['draws_avg']:8.1f} occ_avg={s['regions_occluded_avg']:6.2f} "
              f"drawn_avg={s['regions_drawn_avg']:6.2f}")

    print("\n=== Pinned-pose extraction (bf355/472/626/791/917/987) ===")
    for key in results:
        rows_by_frame = {int(r["frame"]): r for r in results[key]["rows"]}
        print(f"-- {key} --")
        for f in PINNED_FRAMES:
            r = rows_by_frame.get(f)
            if r is None:
                print(f"  frame {f}: MISSING")
                continue
            print(f"  frame {f:4d}: total_ms={r['total_ms']:>8s} submission_ms={r['submission_ms']:>7s} "
                  f"gpu_ms={r['gpu_ms']:>7s} collect_cull_ms={r['collect_cull_ms']:>7s} "
                  f"draws={r['draws']:>5s} regions_drawn={r['regions_drawn']:>4s} "
                  f"regions_occluded={r['regions_occluded']:>4s}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
