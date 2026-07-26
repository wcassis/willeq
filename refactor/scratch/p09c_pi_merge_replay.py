#!/usr/bin/env python3
"""P09c Phase D: the twice-deferred Pi measurement, merged vs unmerged, on
canonical replays. P02 protocol via scripts/pi/pi_common.py primitives (this
script is bespoke rather than pi_bench_run.py because pi_bench_run.py is
specialized for the H04 EQE1 A/B toggle and repeating-pose-cycle CSV
summarization; a --path-file moving replay has no pose cycle to discard
warmup from, so this script summarizes CSVs directly instead of via
summarize_csv/summarize_multi_csv).

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
(the pi_common.py default points at /mnt/projects/fable/main -- the
ARM_BUILD_BIN_DIR trap named in the P09c plan) before importing pi_common,
so env.sh's default is never consulted.
"""
import csv
import os
import sys
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p09c")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p09c_measure"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p09c_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = {"width": 800, "height": 600}


def run_replay(zone, path_file, merged, entities=None, seed=42, frames=1000, label=""):
    remote_csv = f"/tmp/p09c_{zone}_{label}.csv"
    remote_log = f"/tmp/p09c_{zone}_{label}.log"
    remote_path_file = f"/tmp/p09c_{zone}.path"
    pc.scp_to(path_file, remote_path_file)
    cmd = [
        pc.PI_REMOTE_BIN,
        "--zone", zone,
        "--eq-path", pc.PI_EQ_PATH,
        "--path-file", remote_path_file,
        "--frames", str(frames),
    ] + PROD_ARGS
    if merged:
        cmd.append("--merge-zone-draws")
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", str(seed)]
    cmd += ["--drm", "--res", str(RES["width"]), str(RES["height"]), "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    import time
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed ({zone}/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [{zone}/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"{zone}_{label}.csv"
    pc.rsync_from(remote_csv, OUT_DIR)
    (OUT_DIR / f"{zone}_{label}.csv").exists()
    # rsync_from pulls to OUT_DIR with the remote basename.
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    return local_csv


def summarize(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))
    total_ms = [float(r["total_ms"]) for r in rows]
    submission_ms = [float(r["submission_ms"]) for r in rows]
    total_ms_sorted = sorted(total_ms)
    p95_idx = min(len(total_ms_sorted) - 1, int(len(total_ms_sorted) * 0.95))
    sub_sorted = sorted(submission_ms)
    sub_p95_idx = min(len(sub_sorted) - 1, int(len(sub_sorted) * 0.95))
    return {
        "n_frames": len(rows),
        "total_ms_avg": sum(total_ms) / len(total_ms),
        "total_ms_p95": total_ms_sorted[p95_idx],
        "submission_ms_avg": sum(submission_ms) / len(submission_ms),
        "submission_ms_p95": sub_sorted[sub_p95_idx],
        "draws_avg": sum(int(r["draws"]) for r in rows) / len(rows),
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

    plan = [
        ("befallen", "benchmarks/paths/befallen_seed42_1000f.path", False, None, "zone_unmerged"),
        ("befallen", "benchmarks/paths/befallen_seed42_1000f.path", True, None, "zone_merged"),
        ("befallen", "benchmarks/paths/befallen_seed42_1000f.path", False, 205, "combined_unmerged"),
        ("befallen", "benchmarks/paths/befallen_seed42_1000f.path", True, 205, "combined_merged"),
        ("qeynos2", "benchmarks/paths/qeynos2_seed42_1000f.path", False, None, "zone_unmerged"),
        ("qeynos2", "benchmarks/paths/qeynos2_seed42_1000f.path", True, None, "zone_merged"),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session, 2 sessions OK)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    try:
        for zone, path_file, merged, entities, label in plan:
            key = f"{zone}_{label}"
            runner.before_invocation(key)
            csv_path = run_replay(zone, PROJECT_DIR / path_file, merged, entities=entities, label=label)
            runner.after_invocation(key)
            results[key] = summarize(csv_path)
            import time
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

    print("\n=== Whole-run summary (total_ms avg/p95, submission_ms avg/p95, draws avg) ===")
    for key, s in results.items():
        print(f"{key:28s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:8.1f}")

    print("\n=== Pinned-pose extraction (frame indices 355,472,626,791,917,987 -- befallen only) ===")
    pinned_frames = [355, 472, 626, 791, 917, 987]
    for key in ("befallen_zone_unmerged", "befallen_zone_merged",
                "befallen_combined_unmerged", "befallen_combined_merged"):
        if key not in results:
            continue
        rows_by_frame = {int(r["frame"]): r for r in results[key]["rows"]}
        print(f"-- {key} --")
        for f in pinned_frames:
            r = rows_by_frame.get(f)
            if r is None:
                print(f"  frame {f}: MISSING")
                continue
            print(f"  frame {f:4d}: draws={r['draws']:>5s} state_changes={r['state_changes']:>3s} "
                  f"polys={r['polys']:>6s} total_ms={r['total_ms']:>7s} submission_ms={r['submission_ms']:>7s}")

    print("\n=== Draw reduction (merged vs unmerged, whole-run draws avg) ===")
    for zone, kind in [("befallen", "zone"), ("befallen", "combined"), ("qeynos2", "zone")]:
        u = results.get(f"{zone}_{kind}_unmerged")
        m = results.get(f"{zone}_{kind}_merged")
        if u is None or m is None:
            continue
        red = 100.0 * (1.0 - m["draws_avg"] / u["draws_avg"])
        print(f"{zone} {kind}: unmerged draws_avg={u['draws_avg']:.1f} merged draws_avg={m['draws_avg']:.1f} "
              f"reduction={red:.1f}%")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
