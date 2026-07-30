#!/usr/bin/env python3
"""P45 Phase 2 resume: run1 already completed + landed (CmaFree abort fired
before run2, clean per protocol; Pi rebooted, CmaFree recovered to 102056kB).
Runs 2, 3 (tip binary), then re-stages pre-P37 binary and runs 4. Reuses
p45_pi_kpi_remeasure's functions/constants directly so the invocation shape
stays identical to what was already staged/reviewed."""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p45_pi_kpi_remeasure as m  # noqa: E402
import pi_common as pc  # noqa: E402 (already on sys.path via m's import)

print("[preflight] full P02 preflight...")
pc.preflight()
pc.investigate_console_drm()

results = {}
drm_ok = {}

# Pull in run1's already-landed data.
run1_csv = m.OUT_DIR / "befallen_e205_shipped_run1.csv"
run1_log = m.OUT_DIR / "befallen_e205_shipped_run1.log"
results["e205_shipped_run1"] = m.summarize(run1_csv)
drm_ok["e205_shipped_run1"] = m.check_drm_mode(run1_log)
print(f"[resume] loaded run1 from disk: draws_avg={results['e205_shipped_run1']['draws_avg']:.1f} "
      f"p95={results['e205_shipped_run1']['total_ms_p95']:.3f}")

runner = pc.SessionRunner(cma_log_path=str(m.OUT_DIR / "cma_telemetry_resume.tsv"))
governor = pc.GovernorPin()
governor.__enter__()
try:
    print("[stage] re-staging tip binary after reboot...")
    pc.strip_and_stage_binary(m.SCRATCH)

    tip_plan = [
        ("e205_shipped_run2", m.SHIPPED_PRESET_ARGS, "composite"),
        ("e205_realroster_tip", m.SHIPPED_PRESET_ARGS + ["--test-races", m.ZOM_HEAVY_TEST_RACES], "mixed"),
    ]
    for label, extra_args, expect in tip_plan:
        runner.before_invocation(label)
        csv_path, log_path = m.run_replay(label, extra_args)
        runner.after_invocation(label)
        s = m.summarize(csv_path)
        m.sanity_check(label, s, expect=expect)
        drm_ok[label] = m.check_drm_mode(log_path)
        results[label] = s
        time.sleep(pc.H04_COOLDOWN_SEC)

    print("\n[re-stage] switching to pre-P37 binary for run 4...")
    pc.ARM_BUILD_BIN_DIR = m.PRE_P37_BIN_DIR
    pc.strip_and_stage_binary(m.SCRATCH)

    label = "e205_realroster_preP37"
    runner.before_invocation(label)
    csv_path, log_path = m.run_replay(label, m.SHIPPED_PRESET_ARGS + ["--test-races", m.ZOM_HEAVY_TEST_RACES],
                                       gl_counters=False)
    runner.after_invocation(label)
    s = m.summarize(csv_path)
    m.sanity_check(label, s, expect="mixed_lower")
    drm_ok[label] = m.check_drm_mode(log_path)
    results[label] = s
except pc.SessionAbort as e:
    governor.__exit__(None, None, None)
    print(f"[ABORT] {e}")
    raise
except BaseException:
    governor.__exit__(None, None, None)
    raise
else:
    governor.__exit__(None, None, None)

print(f"\n[session] {runner.count} NEW invocations this resume (+1 already landed pre-abort = "
      f"{runner.count + 1} total), CmaFree samples: {[s[1] for s in runner.cma.samples]}")

print("\n=== KPI-verdict table: runs 1-2 vs P14 (32.313ms p95) / P39 (32.5ms p95) / "
      "30fps line (33.3ms) -- baseline-comparable ===")
for key in ("e205_shipped_run1", "e205_shipped_run2"):
    s = results[key]
    flag = " *** OVER 33.3ms LINE ***" if s["total_ms_p95"] > m.KPI_LINE_MS else ""
    print(f"{key:24s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
          f"total_p95={s['total_ms_p95']:7.3f}{flag} gpu_avg={s['gpu_ms_avg']:7.3f} "
          f"gpu_p95={s['gpu_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
          f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:6.1f} "
          f"polys_avg={s['polys_avg']:8.1f} drm_mode_ok={drm_ok[key]}")

print("\n=== P37-attribution table: run 3 (tip, real roster) vs run 4 (pre-P37, same "
      "roster) -- NEW invocation shape, comparable ONLY to each other, NOT to P14/P39 ===")
for key in ("e205_realroster_tip", "e205_realroster_preP37"):
    s = results[key]
    print(f"{key:24s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
          f"total_p95={s['total_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
          f"gpu_p95={s['gpu_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
          f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:6.1f} "
          f"polys_avg={s['polys_avg']:8.1f} drm_mode_ok={drm_ok[key]}")
tip_s, pre_s = results["e205_realroster_tip"], results["e205_realroster_preP37"]
print(f"\nP37 cost at ZOM-heavy real roster: draws_avg {pre_s['draws_avg']:.1f} (pre-P37) "
      f"-> {tip_s['draws_avg']:.1f} (tip) = {tip_s['draws_avg'] - pre_s['draws_avg']:+.1f} draws/frame; "
      f"total_ms p95 {pre_s['total_ms_p95']:.3f} -> {tip_s['total_ms_p95']:.3f} = "
      f"{tip_s['total_ms_p95'] - pre_s['total_ms_p95']:+.3f}ms")

print("\nCSVs/logs written to", m.OUT_DIR)
print("\nRun 5 reserved -- not spent (session completed within budget).")
