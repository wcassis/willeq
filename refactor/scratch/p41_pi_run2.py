#!/usr/bin/env python3
"""P41 continuation: just the e205 lightweight leg (perpixel already landed)."""
import sys
from pathlib import Path
sys.argv = [sys.argv[0], "--plan", "lighting_e205"]
PROJECT_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_DIR / "refactor" / "scratch"))
import p41_pi_run as m  # noqa: E402

# monkeypatch: only run the lightweight leg this call
orig_main = m.main


def main():
    import argparse
    ap = argparse.ArgumentParser()
    args = ap.parse_args([])
    print("[preflight] checking Pi reachability + binary staging...")
    proc = m.pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = m.pc.ARM_BUILD_BIN_DIR / m.pc.BENCH_LOCAL_NAME
    print(f"  using ARM binary: {local_bin}")
    plan = [
        ("e205_lightweight_shipped", m.SHIPPED_PRESET_ARGS + ["--lighting-model", "lightweight"], "", dict(entities=m.STRESS_N)),
    ]
    print(f"[plan] {len(plan)} invocations this call")
    m.pc.strip_and_stage_binary(m.SCRATCH)
    runner = m.pc.SessionRunner(cma_log_path=str(m.OUT_DIR / "cma_telemetry.tsv"))
    governor = m.pc.GovernorPin()
    governor.__enter__()
    results = {}
    try:
        for label, extra_args, env_prefix, kwargs in plan:
            runner.before_invocation(label)
            csv_path = m.run_replay(label, extra_args, env_prefix, **kwargs)
            runner.after_invocation(label)
            results[label] = m.summarize(csv_path)
            import time
            time.sleep(m.pc.H04_COOLDOWN_SEC)
    except m.pc.SessionAbort as e:
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
    for key, s in results.items():
        print(f"{key:28s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} swap_avg={s['swap_ms_avg']:6.3f} "
              f"draws_avg={s['draws_avg']:6.1f} polys_avg={s['polys_avg']:7.1f}")


main()
