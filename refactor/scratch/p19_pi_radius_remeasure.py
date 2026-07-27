#!/usr/bin/env python3
"""P19 -- radius-vs-count re-measurement under the FULL final stack (merge +
composite-scenegraph + --animate + occluder budget 256 + production config),
for the flip-decision report. REPORT ONLY -- no default is flipped by this
script or this unit.

Motivation (docs/decisions.md 2026-07-24 + 2026-07-25 addendum, P19 plan):
count mode stays default on TIMING ALONE (radius measured 1.1-17% slower in
Q03). Since Q03, the stack changed materially: the merge (P10), real animated
composites (P11b/P12b), and occluder budget 256 (P14, now the shipped
default -- constrained_renderer_config.cpp:288). This re-measures count vs
radius under ALL of that, on BOTH canonical replay paths (befallen at-KPI,
qeynos2 above-KPI), calibrated N=205 entities (P11b calibration).

Ad hoc script using scripts/pi/pi_common.py's P02 primitives directly, same
precedent as P07d/Q01/Q03/P14's own scripts (not committed into
pi_common.py itself).

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
(pi_common.py's env.sh default points at /mnt/projects/fable/main -- the
documented ARM_BUILD_BIN_DIR trap) before importing pi_common.
"""
import csv
import os
import random
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p19")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p19_radius_flip_remeasure"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p19_pi_scratch")

PROD_ARGS = pc.production_config_args()  # --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9
RES = {"width": 800, "height": 600}
CALIBRATED_N = 205  # P11b calibration, reused per P14/P19 plan
FULL_STACK_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                    "--eqe1-path", pc.PI_EQE1_PATH, "--occluder-budget", "256"]

ZONES = {
    "befallen": PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path",
    "qeynos2": PROJECT_DIR / "benchmarks/paths/qeynos2_seed42_1000f.path",
}
# bf pinned poses established by P08/P11b/P14 precedent.
PINNED_FRAMES = {
    "befallen": [355, 472, 626, 791, 917, 987],
    # qeynos2 has no established pinned-pose set in prior units; picked
    # post-hoc below from the actual replay (max total_ms, max regions_vis,
    # plus 2 evenly spaced) -- see review note.
    "qeynos2": None,
}


def run_replay(zone, path_file, variant_args, label, frames=1000):
    remote_csv = f"/tmp/p19_{zone}_{label}.csv"
    remote_log = f"/tmp/p19_{zone}_{label}.log"
    remote_path_file = f"/tmp/p19_{zone}.path"
    pc.scp_to(path_file, remote_path_file)
    cmd = [
        pc.PI_REMOTE_BIN,
        "--zone", zone,
        "--eq-path", pc.PI_EQ_PATH,
        "--path-file", remote_path_file,
        "--frames", str(frames),
        "--entities", str(CALIBRATED_N), "--seed", "42",
    ] + PROD_ARGS + FULL_STACK_ARGS + variant_args
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

    def col(name, cast=float):
        return [cast(r[name]) for r in rows]

    def p95_of(vals):
        s = sorted(vals)
        idx = min(len(s) - 1, int(len(s) * 0.95))
        return s[idx]

    total_ms = col("total_ms")
    return {
        "n_frames": len(rows),
        "total_ms_avg": sum(total_ms) / len(total_ms),
        "total_ms_p95": p95_of(total_ms),
        "regions_vis_avg": sum(col("regions_vis", int)) / len(rows),
        "regions_demoted_radius_avg": sum(col("regions_demoted_radius", int)) / len(rows),
        "regions_demoted_radius_frames_nonzero": sum(1 for v in col("regions_demoted_radius", int) if v > 0),
        "light_radius_const": sorted(set(col("light_radius")))[:5],
        "draws_avg": sum(col("draws", int)) / len(rows),
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

    plan = []
    for zone, path_file in ZONES.items():
        for mode, extra in [("count", ["--pp-budget-mode", "count"]),
                             ("radius", ["--pp-budget-mode", "radius"])]:
            plan.append((zone, path_file, extra, mode))
    print(f"[plan] {len(plan)} invocations this session (cap 12/session)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    order = list(range(len(plan)))
    random.seed(19)
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

    print("\n=== Whole-run summary ===")
    for key in results:
        s = results[key]
        print(f"{key:20s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} regions_vis_avg={s['regions_vis_avg']:7.2f} "
              f"demoted_radius_avg={s['regions_demoted_radius_avg']:6.3f} "
              f"demoted_nonzero_frames={s['regions_demoted_radius_frames_nonzero']:5d} "
              f"light_radius={s['light_radius_const']} draws_avg={s['draws_avg']:7.1f}")

    print("\n=== Pinned-pose extraction ===")
    for zone in ZONES:
        pinned = PINNED_FRAMES[zone]
        if pinned is None:
            # derive post-hoc from the count-mode run: worst total_ms, densest
            # regions_vis, plus 2 evenly spaced frames
            rows = results[f"{zone}_count"]["rows"]
            by_total = sorted(rows, key=lambda r: -float(r["total_ms"]))
            by_regions = sorted(rows, key=lambda r: -int(r["regions_vis"]))
            worst_total = int(by_total[0]["frame"])
            densest = int(by_regions[0]["frame"])
            evenly = [250, 750]
            pinned = sorted(set([worst_total, densest] + evenly))
            print(f"-- {zone}: no established pinned set, derived {pinned} "
                  f"(worst_total={worst_total}, densest_regions={densest}, +2 evenly spaced) --")
        for mode in ("count", "radius"):
            key = f"{zone}_{mode}"
            rows_by_frame = {int(r["frame"]): r for r in results[key]["rows"]}
            print(f"-- {key} --")
            for f in pinned:
                r = rows_by_frame.get(f)
                if r is None:
                    print(f"  frame {f}: MISSING")
                    continue
                print(f"  frame {f:4d}: total_ms={r['total_ms']:>8s} regions_vis={r['regions_vis']:>4s} "
                      f"regions_demoted_radius={r['regions_demoted_radius']:>3s} "
                      f"light_radius={r['light_radius']:>10s} draws={r['draws']:>5s}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
