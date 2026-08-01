#!/usr/bin/env python3
"""P51 -- entity-shader-promotion scaling probe (Phase B re-baselining
support). The H1 probe (p51_pi_kpi_bisect.py) confirmed the KPI regression
is entity-shader-promotion cost at e205 (205 entities, default race cycle).
This script gets a second data point at e5 (5 entities, P38/P41's own
"realistic NPC count" shorthand -- perf_truth_audit.md) to give the
re-baselining memo a real (not hand-waved) sense of how the promotion cost
scales with entity count: with vs without --no-entity-shader-promotion,
same tip binary, same shipped-preset shape as P38's own e5 control row.
2 invocations.
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
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]


def run_replay(label, extra_args, frames=1000, entities=5, gl_counters=True):
    remote_csv = f"/tmp/p51_befallen_{label}.csv"
    remote_log = f"/tmp/p51_befallen_{label}.log"
    remote_path_file = "/tmp/p51_befallen.path"
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
    print(f"  [befallen/{label}] wall {dt:.1f}s exit={proc.returncode}")
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
    print("[preflight] ssh reachability + asset check...")
    pc.preflight()
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    print(f"  tip ARM binary: {local_bin}")

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry_scaling.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    drm_ok = {}
    try:
        pc.strip_and_stage_binary(SCRATCH)

        plan = [
            ("e5_shipped_withPromotion", SHIPPED_PRESET_ARGS),
            ("e5_shipped_noPromotion", SHIPPED_PRESET_ARGS + ["--no-entity-shader-promotion"]),
        ]
        for label, extra_args in plan:
            runner.before_invocation(label)
            csv_path, log_path = run_replay(label, extra_args)
            runner.after_invocation(label)
            s = summarize(csv_path)
            drm_ok[label] = check_drm_mode(log_path)
            results[label] = s
            print(f"  [{label}] draws_avg={s['draws_avg']:.1f} total_p95={s['total_ms_p95']:.3f} "
                  f"total_avg={s['total_ms_avg']:.3f}")
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

    w = results["e5_shipped_withPromotion"]
    n = results["e5_shipped_noPromotion"]
    print("\n=== e5 scaling probe (entity-shader-promotion delta at LOW entity count) ===")
    print(f"WITH promotion:    total_p95={w['total_ms_p95']:.3f} total_avg={w['total_ms_avg']:.3f} draws_avg={w['draws_avg']:.1f} drm_ok={drm_ok['e5_shipped_withPromotion']}")
    print(f"WITHOUT promotion: total_p95={n['total_ms_p95']:.3f} total_avg={n['total_ms_avg']:.3f} draws_avg={n['draws_avg']:.1f} drm_ok={drm_ok['e5_shipped_noPromotion']}")
    print(f"delta p95: {w['total_ms_p95'] - n['total_ms_p95']:+.3f} ms   "
          f"delta avg: {w['total_ms_avg'] - n['total_ms_avg']:+.3f} ms")

    print("\nCSVs/logs written to", OUT_DIR)


if __name__ == "__main__":
    main()
