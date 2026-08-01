#!/usr/bin/env python3
"""P52 -- GPU-sync characterization Pi experiment matrix (Phase 2 ONLY).

STOP: this script does not run automatically and must not be invoked until
the orchestrator gives the explicit Phase-2 go-ahead AFTER P51 (slot1)
releases the Pi. Phase 1 (this unit's x86 prep) never touches the Pi --
see refactor/P52_gpu_sync_characterization.md.

Before running (Phase 2 setup, do this first, not just before running):
  1. Rebuild the ARM binary so it includes the --gpu-sync-probe
     instrumentation: `bash scripts/build-arm-noble.sh`.
  2. Read P51's Review in refactor/P51_*.md (will be merged into `fable` by
     Phase 2 time) -- if it changed tests/goldens/manifest.json's
     "productionConfig" block (the "honest instrument config", e.g. entity
     promotion ON), THAT is what production_config_args() below will pick
     up automatically (no code change needed here, by design -- see the
     P52 plan's parameterization note). If P51 instead landed a NEW flag
     that isn't in productionConfig, add it to HONEST_CONFIG_EXTRA below
     and say so in the Phase 2 Review.
  3. Confirm no other unit holds the Pi (check with the orchestrator, not
     by probing the hardware yourself).

Design: reuses scripts/pi/pi_common.py's existing panic-safe machinery
(GovernorPin, CmaMonitor/SessionAbort, strip_and_stage_binary,
run_zone_bench_generic, pose_flags_repeated, production_config_args) --
same idiom as P38/P41/P43's own scratch scripts. Zone: befallen (P38/P41/
P43's canonical dungeon zone, 3 static camera-pose fixtures already in
tests/goldens/manifest.json -- no navmesh path dependency).

Every invocation passes --gpu-sync-probe. Resolutions are EXACT DRM modes
ONLY (P43/P49 rule) -- this Pi/monitor's confirmed exact-match list: 640x480,
720x480, 720x576, 800x600, 832x624, 1024x768, 1152x864, 1280x720, 1280x1024,
1400x1050, 1440x576, 1440x900, 1680x1050, 1920x1080/1200/1440, 2560x1440.
400x300 (P38/P40/P41's old "isolation" resolution) is NEVER used again here
-- it silently substitutes 2560x1440 (P43 root cause).

Experiment matrix (6 invocations, budget ceiling 8):

  A. canonical         -- 800x600, e5 (5 entities), honest config,
                           --gpu-sync-probe --gl-counters together.
                           Primary per-phase attribution number AND the
                           state-overhead cross-check (P42 precedent) in
                           one invocation.
  B. stress-load        -- 800x600, e205 (205 entities), honest config,
                           --gpu-sync-probe. Entity-count sweep high end +
                           swap-floor load-invariance check (compares
                           flip_wait_ms/swap_buffers_ms against A -- if the
                           swap floor is genuinely display-cadence-bound
                           (P41's hypothesis), these should NOT scale with
                           entity count the way the zone/entity GPU-ns
                           columns do).
  C. mid-load            -- 800x600, e40 (40 entities, roughly the
                           orangepi preset's visible-entity cap), honest
                           config, --gpu-sync-probe. Third entity-sweep
                           point (vertex/tiler load) between A and B.
  D. zero-load           -- 800x600, e0 (zone geometry only, no entities),
                           honest config, --gpu-sync-probe. Sharpest
                           swap-floor isolation: if flip_wait_ms/
                           swap_buffers_ms stay ~constant here too (near-
                           zero GPU work submitted), that is strong
                           evidence the floor is display-cadence-bound,
                           not GPU-completion-bound.
  E. fill-lo             -- 640x480 (exact DRM match, smaller than
                           shipped), e5, honest config, --gpu-sync-probe.
                           Fill-rate isolation point (below 800x600).
  F. fill-hi             -- 1024x768 (exact DRM match, larger than
                           shipped), e5, honest config, --gpu-sync-probe.
                           Fill-rate isolation point (above 800x600) --
                           E+F bracket 800x600 for a real 3-point fill
                           sweep without ever touching a non-exact mode.

300 measured frames + default warmup per invocation (P08's canonical
fixed-pose frame count; lighter than P38/P41's 1000-frame convention on
purpose -- this is diagnostic characterization, not a blessed KPI number,
and 6 invocations x 1000 frames would eat a disproportionate share of the
session budget for a report-only deliverable). Bump FRAMES below if Phase 2
execution finds the per-phase percentiles noisy at 300.

Predicted attributions (informed by P38 e5 GPU-sync=54%/6.02ms avg, P41's
eglSwapInterval(0) null result, and CIrrDeviceFB.cpp's drmPageFlip() ordering
-- waitForFlipComplete() BEFORE eglSwapBuffers()):
  - gpu_swap_ns (GPU timer query around Swap) should read near-zero or noisy-
    small on Lima too, same as the x86 sanity run found (~1-3ms, likely
    trailing-command-completion, not real swap-submission GPU work) --
    the swap "cost" is expected to live almost entirely in flip_wait_ms
    (CPU-side), not gpu_swap_ns.
  - flip_wait_ms is the leading suspect for the ~14.4-15.0ms wall-clock
    floor perf_truth_audit's finding (c) named, and P41 explicitly could
    not rule it in or out (out of P41's diagnostic-only scope). Prediction:
    flip_wait_ms is roughly constant across A/B/C/D (entity-load-invariant)
    and across E/F (fill-load-invariant) -- i.e. it tracks the display's
    vblank cadence, not the frame's own GPU/CPU work. If flip_wait_ms
    instead scales with load, that reopens P41's ruled-out eglSwapInterval
    hypothesis in a different form (frame-time-dependent flip queuing) and
    changes the ranked-options note this unit's Phase 2 must write.
  - gpu_zone_ns / gpu_entity_ns should track polys_avg / draws_avg roughly
    linearly across the A/C/B entity sweep (D isolates the zone-only floor
    of gpu_zone_ns with gpu_entity_ns ~0).
  - tex_binds/tex_param_calls (A's --gl-counters cross-check) should be
    flat-ish across A vs the entity-sweep runs if state churn isn't a
    hidden confound in the "GPU-sync" bucket P38 originally lumped together
    -- if binds scale with entity count in lockstep with gpu_entity_ns, that
    reopens P38's state-dedup lever as a bigger factor than P38 measured at
    e5 alone.

None of these predictions are conclusions -- Phase 2 replaces every one of
them with the actual measured numbers in perf_truth_audit.md's dated
section, ranked by measured contribution, per the plan.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

ZONE = "befallen"
RESOLUTIONS = {
    "shipped": {"width": 800, "height": 600},
    "fill_lo": {"width": 640, "height": 480},
    "fill_hi": {"width": 1024, "height": 768},
}
FRAMES = 300

# Any flag P51 lands OUTSIDE tests/goldens/manifest.json's productionConfig
# block (which production_config_args() already picks up automatically) --
# empty unless Phase 2 setup step 2 above found one.
HONEST_CONFIG_EXTRA = []

EXPERIMENTS = [
    # (label, resolution_key, entities, extra_flags)
    ("A_canonical_e5",   "shipped", 5,   ["--gpu-sync-probe", "--gl-counters"]),
    ("B_stress_e205",    "shipped", 205, ["--gpu-sync-probe"]),
    ("C_mid_e40",        "shipped", 40,  ["--gpu-sync-probe"]),
    ("D_zero_e0",        "shipped", 0,   ["--gpu-sync-probe"]),
    ("E_fill_lo_640x480", "fill_lo", 5,  ["--gpu-sync-probe"]),
    ("F_fill_hi_1024x768", "fill_hi", 5, ["--gpu-sync-probe"]),
]
assert len(EXPERIMENTS) <= 8, "P02 budget: this unit's invocation ceiling is 8"


def build_extra_cli_args(entities, extra_flags):
    args = list(HONEST_CONFIG_EXTRA) + list(extra_flags)
    # --entities 0 means "zone only" -- gles2_zone_benchmark accepts this
    # (P08's own zone-only isolation config uses the same flag/value).
    args += ["--entities", str(entities)]
    if entities > 0:
        args += ["--animate"]  # matches P38/P41's canonical entity config
    return args


def main():
    scratch = Path("/tmp/p52_gpu_sync_stage")
    out_dir = Path(pc.PROJECT_DIR) / "benchmarks" / "pi_runs" / "p52_gpu_sync"
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = pc.preflight()
    zone_cfg = next(z for z in manifest["zones"] if z["zone"] == ZONE)

    pc.investigate_console_drm()
    pc.strip_and_stage_binary(scratch)

    def run_one(exp):
        label, res_key, entities, extra_flags = exp
        resolution = RESOLUTIONS[res_key]
        extra_cli_args = build_extra_cli_args(entities, extra_flags)
        remote_csv = f"{pc.PI_REMOTE_TMP}/p52_{label}.csv"
        remote_log = f"{pc.PI_REMOTE_TMP}/p52_{label}.log"
        pc.run_zone_bench_generic(
            zone_cfg, resolution, FRAMES, extra_cli_args,
            remote_csv, remote_log, label_extra=f" [{label}]",
        )
        pc.rsync_from(remote_csv, out_dir)
        pc.rsync_from(remote_log, out_dir)
        # P43/P49: verify the achieved resolution matches what was
        # requested -- all three RESOLUTIONS entries are exact DRM matches
        # for this Pi/monitor, so this should never fire; a fire here means
        # the confirmed-exact-mode list in this file's docstring is stale
        # (monitor swapped, EDID changed) and the run must be discarded.
        rows = pc.read_csv_metrics(out_dir / f"p52_{label}.csv")
        if rows:
            aw, ah = int(rows[0]["actual_width"]), int(rows[0]["actual_height"])
            if (aw, ah) != (resolution["width"], resolution["height"]):
                raise RuntimeError(
                    f"{label}: DRM mode fallback fired unexpectedly -- requested "
                    f"{resolution['width']}x{resolution['height']}, achieved {aw}x{ah}. "
                    f"This resolution is no longer an exact DRM match on this Pi/monitor; "
                    f"discard this run and update the docstring's confirmed-exact-mode list."
                )

    runner = pc.run_session(
        EXPERIMENTS, run_one,
        cma_log_path=out_dir / "cma_telemetry.tsv",
        label_fn=lambda e: e[0],
    )
    print(f"\n=== P52 matrix complete: {runner.count} invocations, "
          f"CmaFree samples: {len(runner.cma.samples)} ===")


if __name__ == "__main__":
    main()
