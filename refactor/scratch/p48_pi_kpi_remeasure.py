#!/usr/bin/env python3
"""P48 Pi re-measure: does composite-sheet alpha (P48) recover P45's measured
P37 cost at the ZOM-heavy real-roster shape, plus 1 canonical-replay KPI
guard spot.

Reuses P45's exact real-roster invocation shape verbatim
(refactor/scratch/p45_pi_kpi_remeasure.py's run_replay/summarize/
sanity_check/check_drm_mode -- copied, not re-derived, so the numbers are
directly comparable to P45's own recorded run 3/run 4 and run 1/run 2
tables). Only 2 invocations planned (budget <=6 total for this unit,
final-remeasure-only per the plan header):
  Run A: e205_realroster (--test-races ZOM-heavy, tip binary w/ P48) --
         compare against P45's recorded e205_realroster_tip (57.650ms p95 /
         348.3 draws, P37 active/no alpha) and e205_realroster_preP37
         (39.304ms p95 / 113.6 draws) -- P48's payoff is recovery TOWARD the
         pre-P37 number.
  Run B: e205_shipped (canonical replay, default race cycle, shipped preset)
         -- KPI guard vs the blessed 32.24-32.5ms band. Regression here =
         STOP per the plan.
"""
import csv
import os
import re
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p48")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p48_sheet_alpha"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p48_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = (800, 600)
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
STRESS_N = 205
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]
ZOM_HEAVY_TEST_RACES = "67,67,67,67,34,17,35,89,53,33,31"

# P45's recorded numbers (refactor/P45_p37_kpi_remeasure.md Review), for
# direct comparison -- NOT re-measured here.
P45_TIP_P95_MS = 57.650
P45_TIP_DRAWS = 348.3
P45_PREP37_P95_MS = 39.304
P45_PREP37_DRAWS = 113.6
P14_P95_MS = 32.313
P39_P95_MS = 32.5
KPI_BAND_LOW, KPI_BAND_HIGH = 32.24, 32.5
KPI_LINE_MS = 33.3


def run_replay(label, extra_args, frames=1000, entities=STRESS_N, gl_counters=True):
    remote_csv = f"/tmp/p48_befallen_{label}.csv"
    remote_log = f"/tmp/p48_befallen_{label}.log"
    remote_path_file = "/tmp/p48_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file, "--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(RES[0]), str(RES[1])]
    if gl_counters:
        cmd += ["--gl-counters"]
    cmd += ["--log-level=INFO", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [befallen/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"befallen_{label}.csv"
    local_log = OUT_DIR / f"befallen_{label}.log"
    pc.rsync_from(remote_csv, OUT_DIR)
    pc.rsync_from(remote_log, OUT_DIR)
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    pulled_log = OUT_DIR / Path(remote_log).name
    if pulled_log != local_log and pulled_log.exists():
        pulled_log.rename(local_log)
    return local_csv, local_log


def check_drm_mode(log_path, expected=f"{RES[0]}x{RES[1]}"):
    text = Path(log_path).read_text(errors="replace")
    m = re.search(r"DRM: Found matching mode (\d+x\d+)@(\d+)Hz for requested (\d+x\d+)", text)
    if not m:
        print(f"  [DRM-MODE] WARNING: no 'Found matching mode' line in {log_path}")
        return False
    matched, hz, requested = m.group(1), m.group(2), m.group(3)
    ok = matched == expected and requested == expected
    print(f"  [DRM-MODE] {'confirmed' if ok else '*** SILENT MODE SUBSTITUTION ***'} "
          f"{matched}@{hz}Hz for requested {requested}")
    return ok


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
    print("[preflight] full P02 preflight (reachability, asset paths, EQE1 archives)...")
    pc.preflight()
    pc.investigate_console_drm()
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh first")
    print(f"  tip (P48) ARM binary: {local_bin}")

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    drm_ok = {}
    try:
        pc.strip_and_stage_binary(SCRATCH)

        plan = [
            ("e205_realroster_p48tip", SHIPPED_PRESET_ARGS + ["--test-races", ZOM_HEAVY_TEST_RACES]),
            ("e205_shipped_kpiguard", SHIPPED_PRESET_ARGS),
        ]
        for label, extra_args in plan:
            runner.before_invocation(label)
            csv_path, log_path = run_replay(label, extra_args)
            runner.after_invocation(label)
            s = summarize(csv_path)
            drm_ok[label] = check_drm_mode(log_path)
            results[label] = s
            print(f"  [{label}] draws_avg={s['draws_avg']:.1f} total_p95={s['total_ms_p95']:.3f}")
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

    tip = results["e205_realroster_p48tip"]
    print("\n=== P48 recovery table (real-roster leg, comparable to P45's runs 3/4 ONLY) ===")
    print(f"{'label':28s} {'total_p95':>10s} {'draws_avg':>10s}")
    print(f"{'P45 e205_realroster_tip (P37, no alpha)':28s} {P45_TIP_P95_MS:10.3f} {P45_TIP_DRAWS:10.1f}")
    print(f"{'P45 e205_realroster_preP37 (target)':28s} {P45_PREP37_P95_MS:10.3f} {P45_PREP37_DRAWS:10.1f}")
    print(f"{'P48 e205_realroster_p48tip (this run)':28s} {tip['total_ms_p95']:10.3f} {tip['draws_avg']:10.1f}")
    recovered_pct = 100.0 * (P45_TIP_P95_MS - tip['total_ms_p95']) / (P45_TIP_P95_MS - P45_PREP37_P95_MS)
    print(f"\nRecovery: {P45_TIP_P95_MS:.3f} -> {tip['total_ms_p95']:.3f} "
          f"({recovered_pct:.1f}% of the way to pre-P37's {P45_PREP37_P95_MS:.3f})")
    print(f"drm_mode_ok={drm_ok['e205_realroster_p48tip']}")

    guard = results["e205_shipped_kpiguard"]
    print("\n=== KPI guard (canonical replay, vs 32.24-32.5ms band; P14=32.313 P39=32.5) ===")
    in_band = KPI_BAND_LOW <= guard['total_ms_p95'] <= KPI_BAND_HIGH
    flag = "" if guard['total_ms_p95'] <= KPI_LINE_MS else " *** OVER 33.3ms LINE ***"
    print(f"total_p95={guard['total_ms_p95']:.3f} draws_avg={guard['draws_avg']:.1f} "
          f"in_band[{KPI_BAND_LOW},{KPI_BAND_HIGH}]={in_band}{flag} drm_mode_ok={drm_ok['e205_shipped_kpiguard']}")
    if not in_band:
        print("*** KPI GUARD OUT OF BAND -- per the plan's STOP condition, this needs review "
              "before treating the unit as clean. ***")

    print("\nCSVs/logs written to", OUT_DIR)


if __name__ == "__main__":
    main()
