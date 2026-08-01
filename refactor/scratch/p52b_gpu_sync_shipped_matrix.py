#!/usr/bin/env python3
"""P52b -- corrected GPU-sync matrix re-run under the FULL shipped preset.

P52 Phase 2 found two methodology bugs: (1) --camera-pose bypasses all
per-frame instrumentation (fixed by switching to --path-file, matching
P51's own scaling probe), (2) the 2-invocation recovery run that fixed (1)
omitted the shipped-preset flags (--merge-zone-draws --composite-scenegraph
--eqe1-path) that every other canonical replay (P38/P41/P51) carries --
B_stress_e205_pathfile measured 232.7ms avg, ~7x the honest 34.3-34.6ms
e205 band, an unmerged/uncomposited-draw confound, not a real number.

This script is p52_pi_gpu_sync_pathfile_fix.py (Bug-1 fix) + SHIPPED_PRESET_ARGS
(Bug-2 fix, lifted verbatim from p51_pi_scaling_probe.py) + the full
0/5/40/205 entity sweep the orchestrator asked for (P52's own charter item
2b: bracket P51's +0.455ms(e5)/+2.22ms(e205) entity-promotion deltas).

4 invocations (A_e5 with --gl-counters, B_e205, C_e40, D_e0), 1 reserve
(ceiling 5). Per-run sanity gate against the honest perf_truth_audit.md
references (e5 ~10.8ms avg total_ms; e205 34.3-34.6ms p95 total_ms) --
printed after each run; the caller (this script's operator) must
diagnose+stop rather than burn budget if a number is wildly off.
"""
import csv
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = Path(pc.PROJECT_DIR) / "benchmarks" / "pi_runs" / "p52b_gpu_sync"
OUT_DIR.mkdir(parents=True, exist_ok=True)
# CORRECTED after a live sanity-gate failure (see p52b run log / plan Review):
# befallen_fixedpose_300f.path (this script's original starting point, per
# p52_pi_gpu_sync_pathfile_fix.py) puts the camera in a near-stationary
# vantage with entities clustered along the (barely-moving) path -- at
# e205 it measured entities_visible avg=102/draws avg=686 vs P51's own
# honest e205 canonical run's entities_visible avg=34/draws avg=41.9 on
# befallen_seed42_1000f.path (a real zone traversal). That is a path-density
# confound distinct from Bug 1/Bug 2, caught by the sanity gate before
# further budget was spent. Switched to P51's own canonical path so results
# are directly comparable to the 10.818ms(e5)/34.3-34.6ms(e205) references.
PATH_FILE = Path(pc.PROJECT_DIR) / "benchmarks" / "paths" / "befallen_seed42_1000f.path"
REMOTE_PATH_FILE = "/tmp/p52b_befallen_seed42_1000f.path"
RES = (800, 600)
PROD_ARGS = pc.production_config_args()
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]

# Honest references (perf_truth_audit.md P51 section) -- sanity gate.
SANITY = {
    "A_e5": ("total_ms_avg", 10.818, 0.35),   # +/-35% band, generous but catches gross drift
    "B_e205": ("total_ms_p95", 34.47, 0.35),
}


def run_one(label, entities, extra_flags):
    remote_csv = f"/tmp/p52b_{label}.csv"
    remote_log = f"/tmp/p52b_{label}.log"
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", REMOTE_PATH_FILE, "--frames", "300"] + PROD_ARGS
    cmd += SHIPPED_PRESET_ARGS
    cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += extra_flags
    cmd += ["--drm", "--res", str(RES[0]), str(RES[1]), "--log-level=INFO", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=180, check=False)
    dt = time.time() - t0
    print(f"  [{label}] wall {dt:.1f}s exit={proc.returncode}")
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed ({label}), exit {proc.returncode}, wall {dt:.1f}s:\n{proc.stdout}\n{proc.stderr}")
    pc.rsync_from(remote_csv, OUT_DIR)
    pc.rsync_from(remote_log, OUT_DIR)


def check_drm_mode(log_path):
    text = Path(log_path).read_text(errors="replace")
    m = re.search(r"DRM: Found matching mode (\d+x\d+)@(\d+)Hz for requested (\d+x\d+)", text)
    expected = f"{RES[0]}x{RES[1]}"
    if not m:
        print(f"  [DRM-MODE] WARNING: no confirmation line in {log_path}")
        return
    ok = m.group(1) == expected and m.group(3) == expected
    print(f"  [DRM-MODE] {'confirmed' if ok else '*** SILENT MODE SUBSTITUTION ***'} "
          f"{m.group(1)}@{m.group(2)}Hz for requested {m.group(3)}")


def p95_of(vals):
    s = sorted(vals)
    idx = min(len(s) - 1, int(len(s) * 0.95))
    return s[idx]


def summarize(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))

    def col(name, cast=float):
        return [cast(r[name]) for r in rows]

    out = {"n_frames": len(rows)}
    for name in ["total_ms", "submission_ms", "gpu_ms", "collect_cull_ms", "swap_ms",
                 "swap_buffers_ms", "flip_wait_ms", "finish_readpixels_ms"]:
        vals = col(name)
        out[f"{name}_avg"] = sum(vals) / len(vals)
        out[f"{name}_p95"] = p95_of(vals)
    for name in ["draws", "polys", "entities_visible"]:
        vals = col(name, int)
        out[f"{name}_avg"] = sum(vals) / len(vals)
    return out


def sanity_gate(label, summary):
    if label not in SANITY:
        return True
    field, ref, tol = SANITY[label]
    val = summary[field]
    lo, hi = ref * (1 - tol), ref * (1 + tol)
    ok = lo <= val <= hi
    tag = "PASS" if ok else "*** FAIL ***"
    print(f"  [SANITY {tag}] {label}: {field}={val:.3f}ms vs honest ref {ref}ms (band [{lo:.2f},{hi:.2f}])")
    return ok


def main():
    pc.preflight()
    pc.investigate_console_drm()
    scratch = Path("/tmp/p52b_gpu_sync_stage")
    pc.strip_and_stage_binary(scratch)
    pc.scp_to(PATH_FILE, REMOTE_PATH_FILE)

    cma = pc.CmaMonitor(log_path=OUT_DIR / "cma_telemetry.tsv")
    governor = pc.GovernorPin()
    governor.__enter__()
    summaries = {}
    try:
        # A_e5 and B_e205 already completed this session (run3 log) -- this
        # invocation of the script covers the remaining 2 legs the
        # orchestrator authorized under the extended 6-invocation ceiling
        # (invocations 5 and 6, no reserve left).
        plan = [
            ("D_e0", 0, ["--gpu-sync-probe"]),
            ("C_e40", 40, ["--gpu-sync-probe"]),
        ]
        for label, entities, flags in plan:
            cma.sample(f"{label} (pre)")
            run_one(label, entities, flags)
            cma.sample(f"{label} (post)")
            check_drm_mode(OUT_DIR / f"p52b_{label}.log")
            summary = summarize(OUT_DIR / f"p52b_{label}.csv")
            summaries[label] = summary
            ok = sanity_gate(label, summary)
            print(f"  [{label}] n_frames={summary['n_frames']} total_ms_avg={summary['total_ms_avg']:.3f} "
                  f"total_ms_p95={summary['total_ms_p95']:.3f} entities_visible_avg={summary['entities_visible_avg']:.1f}")
            if not ok:
                raise RuntimeError(f"Sanity gate FAILED for {label} -- stopping before burning further budget. "
                                    f"Diagnose config drift before continuing.")
            time.sleep(15)
    finally:
        governor.__exit__(None, None, None)

    print("\n=== P52b shipped-preset matrix: complete ===")
    for label, s in summaries.items():
        print(f"  {label}: total_avg={s['total_ms_avg']:.3f} total_p95={s['total_ms_p95']:.3f} "
              f"submission_avg={s['submission_ms_avg']:.3f} swap_buffers_avg={s['swap_buffers_ms_avg']:.4f} "
              f"flip_wait_avg={s['flip_wait_ms_avg']:.4f} finish_readpixels_avg={s['finish_readpixels_ms_avg']:.3f}")


if __name__ == "__main__":
    main()
