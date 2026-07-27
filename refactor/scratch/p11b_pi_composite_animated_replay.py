#!/usr/bin/env python3
"""P11b Phase B: the TRUE composite perf magnitude Pi measurement --
animated entities through the production-faithful path. Retires P08c's
-33.1% (stand-in path, own cull bug) and P11's ~0% (static scenegraph
route, never builds composite meshes -- composite meshes build ONLY on
the animated path, per P12b's node-wiring fix).

P02 protocol via scripts/pi/pi_common.py primitives, same bespoke
--path-file moving-replay pattern as P09c's p09c_pi_merge_replay.py and
P11's p11_pi_stacked_replay.py (pi_bench_run.py's pose-cycle helpers
don't apply to a moving replay).

Variants (all --animate, calibrated N=205 -- see Phase A calibration,
which found the animated path's visible-entity distribution at N=205
matches the static calibration exactly post-P12b: median 38 / p95 40):
  a. multibuffer-animated (baseline for THIS comparison)
  b. composite-animated (--composite-scenegraph) -- THE number
  c. merge + composite-animated (--merge-zone-draws --composite-scenegraph)
     -- the full honest stack

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's
build-arm-noble/bin (the pi_common.py default points at
/mnt/projects/fable/main -- the ARM_BUILD_BIN_DIR trap) before importing
pi_common.
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p11b")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p11b_composite_magnitude"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p11b_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = {"width": 800, "height": 600}
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
CALIBRATED_N = 205  # Phase A: matches static calibration exactly on the animated path


def run_replay(zone, path_file, extra_args, label, entities=CALIBRATED_N, seed=42, frames=1000):
    remote_csv = f"/tmp/p11b_{zone}_{label}.csv"
    remote_log = f"/tmp/p11b_{zone}_{label}.log"
    remote_path_file = f"/tmp/p11b_{zone}.path"
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
    anim_ms = [float(r["anim_cpu_ms"]) for r in rows]
    total_sorted = sorted(total_ms)
    p95_idx = min(len(total_sorted) - 1, int(len(total_sorted) * 0.95))
    sub_sorted = sorted(submission_ms)
    sub_p95_idx = min(len(sub_sorted) - 1, int(len(sub_sorted) * 0.95))
    gpu_sorted = sorted(gpu_ms)
    gpu_p95_idx = min(len(gpu_sorted) - 1, int(len(gpu_sorted) * 0.95))
    anim_sorted = sorted(anim_ms)
    anim_p95_idx = min(len(anim_sorted) - 1, int(len(anim_sorted) * 0.95))
    return {
        "n_frames": len(rows),
        "total_ms_avg": sum(total_ms) / len(total_ms),
        "total_ms_p95": total_sorted[p95_idx],
        "submission_ms_avg": sum(submission_ms) / len(submission_ms),
        "submission_ms_p95": sub_sorted[sub_p95_idx],
        "gpu_ms_avg": sum(gpu_ms) / len(gpu_ms),
        "gpu_ms_p95": gpu_sorted[gpu_p95_idx],
        "anim_cpu_ms_avg": sum(anim_ms) / len(anim_ms),
        "anim_cpu_ms_p95": anim_sorted[anim_p95_idx],
        "draws_avg": sum(int(r["draws"]) for r in rows) / len(rows),
        "entities_visible_avg": sum(int(r["entities_visible"]) for r in rows) / len(rows),
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

    # NOTE (fixed after invocations #1-3): --composite-scenegraph is a silent
    # no-op unless --eqe1-path is also passed (zone_bench_loader.cpp:576/610
    # gate composite mesh-building on `config.compositeEntities &&
    # !config.eqe1Path.empty()`, no warning printed otherwise) -- the first
    # attempt at this session omitted it, producing byte-identical draws
    # between "multibuffer" and "composite" runs (a real composite-engagement
    # FAILURE, not a ~0% perf finding -- caught via the draws_avg sanity
    # check, not asserted). Re-running composite + merge+composite with
    # --eqe1-path included; multibuffer baseline (invocation #1, unaffected
    # by eqe1Path since compositeEntities is false there) is kept.
    plan = [
        ("befallen", BEFALLEN_PATH, ["--animate", "--composite-scenegraph", "--eqe1-path", pc.PI_EQE1_PATH], "combined_animate_composite"),
        ("befallen", BEFALLEN_PATH, ["--animate", "--composite-scenegraph", "--merge-zone-draws", "--eqe1-path", pc.PI_EQE1_PATH], "combined_animate_merge_composite"),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    import random
    order = list(range(len(plan)))
    random.seed(11)
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
        # Reuse invocation #1's multibuffer-animated CSV from this same
        # session (already committed to OUT_DIR, unaffected by the eqe1Path
        # bug -- compositeEntities is false for that variant).
        baseline_csv = OUT_DIR / "befallen_combined_animate_multibuffer.csv"
        if baseline_csv.exists():
            results["befallen_combined_animate_multibuffer"] = summarize(baseline_csv)
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

    print("\n=== Whole-run summary ===")
    for key in ["befallen_combined_animate_multibuffer", "befallen_combined_animate_composite",
                "befallen_combined_animate_merge_composite"]:
        s = results[key]
        print(f"{key:42s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} anim_avg={s['anim_cpu_ms_avg']:6.3f} "
              f"anim_p95={s['anim_cpu_ms_p95']:6.3f} draws_avg={s['draws_avg']:8.1f} "
              f"entities_visible_avg={s['entities_visible_avg']:6.2f}")

    print("\n=== Pinned-pose extraction (frame indices 355,472,626,791,917,987) ===")
    pinned_frames = [355, 472, 626, 791, 917, 987]
    for key in results:
        rows_by_frame = {int(r["frame"]): r for r in results[key]["rows"]}
        print(f"-- {key} --")
        for f in pinned_frames:
            r = rows_by_frame.get(f)
            if r is None:
                print(f"  frame {f}: MISSING")
                continue
            print(f"  frame {f:4d}: draws={r['draws']:>5s} state_changes={r['state_changes']:>3s} "
                  f"entities_visible={r['entities_visible']:>3s} "
                  f"polys={r['polys']:>6s} total_ms={r['total_ms']:>7s} submission_ms={r['submission_ms']:>7s} "
                  f"gpu_ms={r['gpu_ms']:>7s} anim_cpu_ms={r['anim_cpu_ms']:>7s}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
