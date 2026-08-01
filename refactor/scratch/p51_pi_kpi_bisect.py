#!/usr/bin/env python3
"""P51 -- KPI-regression bisect: H1 probe (instrument-honesty hypothesis)
+ P49 ride-along.

H1 (refactor/P51_kpi_regression_bisect.md): the canonical KPI-guard
regression (P48's EVIDENCE_MANIFEST.txt: 34.3-34.6ms p95 vs the blessed
32.24-32.5ms band, reproduced on a pre-P48 binary too) is caused by P46b's
applyEntityShaderMaterialPromotion() giving the benchmark's default-cycle
(non-masked) entities production's real per-pixel-lit shader, where
PRE-P46b they rendered on Irrlicht's built-in fixed-function EMT_SOLID.
P46c added --no-entity-shader-promotion as a benchmark-only opt-out.

H1 probe (1 invocation): tip ARM binary, canonical e205 KPI-guard spot
(identical shape to P14/P39/P41/P45/P48's own canonical replay), WITH
--no-entity-shader-promotion. Recovery into 32.24-32.5ms => H1 confirmed,
Phase A bisect skipped entirely.

Reuses P45/P48's exact canonical-replay invocation shape verbatim
(refactor/scratch/p48_pi_kpi_remeasure.py's run_replay/summarize/
check_drm_mode, copied not re-derived) so the new number is directly
comparable to the historical band and to P48's own 34.3-34.6ms figures.

Also runs the P49 ride-along (~0 timed cost): --res 400 300 WITHOUT
--allow-mode-fallback, expect refusal (exit 1) + both ERROR lines.
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p51")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p51_kpi_bisect"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p51_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = (800, 600)
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
STRESS_N = 205
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]

KPI_BAND_LOW, KPI_BAND_HIGH = 32.24, 32.5
KPI_LINE_MS = 33.3
# P48's own recorded tip-with-promotion figures (EVIDENCE_MANIFEST.txt),
# NOT re-measured here -- shown for direct comparison only.
P48_TIP_WITH_PROMOTION_P95 = [34.321, 34.624]


def run_replay(label, extra_args, frames=1000, entities=STRESS_N, gl_counters=True,
               expect_fail=False, res=RES):
    remote_csv = f"/tmp/p51_befallen_{label}.csv"
    remote_log = f"/tmp/p51_befallen_{label}.log"
    remote_path_file = "/tmp/p51_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file, "--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(res[0]), str(res[1])]
    if gl_counters:
        cmd += ["--gl-counters"]
    cmd += ["--log-level=INFO", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    print(f"  [befallen/{label}] wall {dt:.1f}s exit={proc.returncode}")
    if expect_fail:
        local_log = OUT_DIR / f"befallen_{label}.log"
        pc.rsync_from(remote_log, OUT_DIR)
        pulled_log = OUT_DIR / Path(remote_log).name
        if pulled_log != local_log and pulled_log.exists():
            pulled_log.rename(local_log)
        return None, local_log, proc.returncode
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
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
    return local_csv, local_log, proc.returncode


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
    print(f"  tip (P51, a94091d-equivalent) ARM binary: {local_bin} "
          f"({local_bin.stat().st_size} bytes)")

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    drm_ok = {}
    try:
        pc.strip_and_stage_binary(SCRATCH)

        # --- H1 probe: canonical KPI-guard spot, tip binary, WITH the
        # --no-entity-shader-promotion opt-out. 1 invocation.
        label = "e205_shipped_h1probe_noPromotion"
        runner.before_invocation(label)
        csv_path, log_path, rc = run_replay(label, SHIPPED_PRESET_ARGS + ["--no-entity-shader-promotion"])
        runner.after_invocation(label)
        s = summarize(csv_path)
        drm_ok[label] = check_drm_mode(log_path)
        results[label] = s
        print(f"  [{label}] draws_avg={s['draws_avg']:.1f} total_p95={s['total_ms_p95']:.3f}")
        time.sleep(pc.H04_COOLDOWN_SEC)

        h1_p95 = s['total_ms_p95']
        h1_in_band = KPI_BAND_LOW <= h1_p95 <= KPI_BAND_HIGH
        print("\n=== H1 PROBE RESULT ===")
        print(f"total_p95={h1_p95:.3f} draws_avg={s['draws_avg']:.1f} "
              f"in_band[{KPI_BAND_LOW},{KPI_BAND_HIGH}]={h1_in_band} drm_mode_ok={drm_ok[label]}")
        print(f"P48 tip WITH promotion (recorded, not re-measured): {P48_TIP_WITH_PROMOTION_P95}")
        if h1_in_band:
            print("*** H1 CONFIRMED: recovery into the blessed band with "
                  "--no-entity-shader-promotion. Phase A bisect SKIPPED. ***")
        else:
            print("*** H1 FALSISFIED (or partial): no full recovery into the blessed band. "
                  "Phase A bisect required. ***")

        # --- P49 ride-along: --res 400 300 WITHOUT --allow-mode-fallback,
        # expect refusal (exit 1) + both ERROR lines. Then confirm a normal
        # 800x600 run's log shows no fallback line (control = the H1 probe
        # run above, already captured).
        label2 = "p49_ridealong_400x300_refusal"
        runner.before_invocation(label2)
        _, log_path2, rc2 = run_replay(label2, SHIPPED_PRESET_ARGS, entities=8, frames=10,
                                        gl_counters=False, expect_fail=True, res=(400, 300))
        runner.after_invocation(label2)
        print(f"\n=== P49 RIDE-ALONG RESULT ===")
        print(f"exit_code={rc2} (expect 1 / non-zero)")
        log_text2 = Path(log_path2).read_text(errors="replace")
        error_lines = [l for l in log_text2.splitlines() if "ERROR" in l]
        print(f"ERROR lines found ({len(error_lines)}):")
        for l in error_lines:
            print(f"  {l}")
        found_2560 = "2560x1440" in log_text2
        print(f"substituted-mode 2560x1440 named: {found_2560}")
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
    print("\nCSVs/logs written to", OUT_DIR)


if __name__ == "__main__":
    main()
