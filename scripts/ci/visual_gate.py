#!/usr/bin/env python3
"""H02 — CI gate: golden-frame, metric, and memory verification.

Runs `gles2_zone_benchmark` (the real GLES2/llvmpipe path built by H01) for
each zone in tests/goldens/manifest.json, dumps its designated frames, and
checks them against three independent assertions:

  1. Golden-frame pixel comparison (decoded-pixel, not PNG-byte compare) --
     exact-zero-diff is the pass bar. llvmpipe is deterministic (verified in
     the H02 review), so any difference is a real regression, not noise.
  2. Metric assertions from the benchmark CSV: exact-match on draw calls /
     state changes / polygon counts per designated frame (deterministic on a
     fixed camera path), and a threshold assertion on p95 CPU-ms (<= 2x the
     recorded baseline -- generous, to catch a render-loop regression, not
     to catch scheduler noise).
  3. Memory assertions (T69 extends the H02 texture-cache-only check to the
     full per-subsystem breakdown emitted by gles2_zone_benchmark.cpp's
     --memory-json, itself sourced from IrrlichtRenderer::getMemoryReportBreakdown()'s
     accounting -- see that function's doc comment and this file's MEMORY_*
     lists below):
       (a) Budget-enforced: usage <= a real configured ceiling
           (ConstrainedRendererConfig fields), independent of any baseline.
           Texture cache (pre-existing) and framebuffer (new).
       (b) Baseline-guarded: exact-match against the blessed value. These
           categories have NO configured budget -- this is regression
           detection, not budget enforcement. T69 measured 9 repeat runs
           across all 3 golden zones and found EVERY one of these fields
           bit-identical run to run (see refactor/T69_memory_enforcement_gate.md
           Review), so exact match is an empirically justified tolerance of
           zero, not a guess.
       (c) Reported, not asserted: process RSS and the untracked-RSS gap
           (RSS minus every tallied category). Printed and baselined every
           run for visibility, but never gated in this unit -- see the T69
           Review for why (RSS is real but noisier than the categories in
           (b), and a spuriously-firing gate is worse than no gate).

Usage:
  visual_gate.py --bless    Regenerate goldens + manifest baselines + contact sheet.
  visual_gate.py            Compare against the committed goldens/manifest.

Exit code is nonzero on any failure (or when build-gles2/ is missing --
see the ctest wrapper, scripts/ci/visual_gate.sh, for the SKIP path).
"""
import argparse
import base64
import csv
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
GOLDENS_DIR = PROJECT_DIR / "tests" / "goldens"
MANIFEST_PATH = GOLDENS_DIR / "manifest.json"
BENCH_BIN = PROJECT_DIR / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = PROJECT_DIR / "data" / "eq_client"
ARTIFACTS_DIR = PROJECT_DIR / "build-gles2" / "ci_artifacts"  # gitignored (whole build-gles2/ is)

RESOLUTION_DEFAULT = {"width": 800, "height": 600}

# T69: per-category memory field classification (see module docstring).
# Every name here must match a key gles2_zone_benchmark.cpp's --memory-json
# writes (BenchMemorySnapshot in that file) -- kept as plain field-name
# lists (not a schema) so bless()/verify() share one source of truth for
# "which fields get baselined" and "which get asserted".

# (a) usage/budget pairs: assert usage <= budget, independent of baseline.
MEMORY_BUDGET_PAIRS = [
    ("textureCacheBytesUsed", "textureCacheBytesLimit", "texture cache"),
    ("framebufferBytes", "framebufferBudgetBytes", "framebuffer"),
]

# (b) no configured budget -- baseline with the tolerance measured in T69
# (zero: 9 repeat runs across all 3 golden zones, every field below was
# bit-identical run to run on this deterministic llvmpipe path). A future
# zone/config where these are NOT bit-identical is real news, not noise --
# see the T69 Review before loosening this to a nonzero tolerance.
MEMORY_BASELINE_FIELDS = [
    "textureCount",
    "eqe1MappedBytes",
    "eqe1ArchiveCount",
    "zoneGeometryBytes",
    "placeableObjectsBytes",
    "s3dZoneSourceBytes",
    "gpuTextureBytes",
    "vboEboBytes",
    "characterModelsBytes",
    "totalEstimate",
]

# (c) reported/baselined for visibility, never asserted in this unit -- see
# module docstring and refactor/T69_memory_enforcement_gate.md Review.
MEMORY_REPORT_ONLY_FIELDS = [
    "processRssBytes",
    "untrackedBytes",
]


def load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def run_benchmark(zone_cfg, bench_cfg, out_dir, production_args=None):
    """Run the benchmark for one zone, dumping designated frames + CSV + memory JSON.

    Returns (png_paths_by_frame, csv_rows, memory_dict, stdout_text).
    """
    zone = zone_cfg["zone"]
    frames = zone_cfg.get("designatedFrames", [0])
    prefix = out_dir / zone
    csv_path = out_dir / f"{zone}.csv"
    mem_path = out_dir / f"{zone}_mem.json"

    cmd = [
        str(BENCH_BIN),
        "--zone", zone,
        "--eq-path", str(EQ_PATH),
        "--frames", str(bench_cfg["frames"]),
        "--warmup", str(bench_cfg["warmup"]),
        "--res", str(bench_cfg["resolution"]["width"]), str(bench_cfg["resolution"]["height"]),
        "--dump-frames", ",".join(str(f) for f in frames),
        "--dump-png-prefix", str(prefix),
        "--csv", str(csv_path),
        "--memory-json", str(mem_path),
        # P42 item 3: the benchmark's --occluder-budget default now resolves
        # from ConstrainedRendererConfig (orangepi preset -> 256) instead of
        # the old hardcoded 16 -- correct for perf measurement (matches
        # shipped production), but these committed goldens (pixel PNGs +
        # draws/polys counts in manifest.json) were captured under the old
        # default and are untouched by this unit ("goldens untouched" is a
        # hard rule). Pin the gate's own invocation to the prior literal
        # value so it stays on the exact code path the goldens were blessed
        # against; --occluder-budget is an explicit-flag-wins override (see
        # zone_bench_loader.cpp), so this does not fight item 3's default
        # fix -- Pi measurements (scripts/pi/pi_common.py) and any other
        # caller that doesn't pass this flag still get the corrected 256
        # default. A future re-bless (Will's call, out of this unit's scope)
        # can drop this pin once goldens are regenerated under the new default.
        "--occluder-budget", "16",
    ] + (production_args or []) + zone_cfg.get("extraArgs", [])

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(
            f"benchmark failed for zone {zone} (exit {proc.returncode}):\n"
            f"cmd: {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )

    png_paths = {f: Path(f"{prefix}_f{f}.png") for f in frames}
    for f, p in png_paths.items():
        if not p.exists():
            raise RuntimeError(f"expected golden frame PNG missing: {p}\nstdout:\n{proc.stdout}")

    csv_rows = list(csv.DictReader(open(csv_path)))

    memory = None
    if mem_path.exists():
        with open(mem_path) as f:
            memory = json.load(f)

    return png_paths, csv_rows, memory, proc.stdout


def extract_gl_version(stdout_text):
    m = re.search(r"\[OpenGL ES [^\]]*\]", stdout_text)
    if m:
        return m.group(0).strip("[]")
    return None


def p95(values):
    values = sorted(values)
    idx = int(len(values) * 0.95)
    if idx >= len(values):
        idx = len(values) - 1
    return values[idx]


def compare_images_exact(path_a, path_b):
    """Decoded-pixel comparison. Returns (identical: bool, diff_count: int, size).

    Compares RGB only (not RGBA): these renders are opaque framebuffer dumps,
    and ImageChops.difference()/Image.getbbox() on RGBA silently ignores
    color-channel differences whenever the alpha channel is unchanged (which
    it always is here, alpha is always 255) -- comparing RGBA looks correct
    but never detects a real pixel mismatch. Confirmed with the H02 negative
    test (see review): an RGBA compare of a deliberately perturbed golden
    reported "identical" despite a 20x20 magenta patch; switching to RGB
    fixed it.
    """
    try:
        from PIL import Image, ImageChops
        a = Image.open(path_a).convert("RGB")
        b = Image.open(path_b).convert("RGB")
        if a.size != b.size:
            return False, -1, (a.size, b.size)
        diff = ImageChops.difference(a, b)
        bbox = diff.getbbox()
        if bbox is None:
            return True, 0, a.size
        # Count differing pixels for a clearer failure message.
        diff_data = diff.getdata()
        diff_count = sum(1 for px in diff_data if any(px))
        return False, diff_count, a.size
    except ImportError:
        return compare_images_imagemagick(path_a, path_b)


def compare_images_imagemagick(path_a, path_b):
    """Fallback: ImageMagick `compare -metric AE`."""
    proc = subprocess.run(
        ["compare", "-metric", "AE", str(path_a), str(path_b), "/dev/null"],
        capture_output=True, text=True,
    )
    # compare writes the AE count to stderr regardless of match/mismatch.
    try:
        diff_count = int(proc.stderr.strip())
    except ValueError:
        diff_count = -1
    return diff_count == 0, diff_count, None


def save_diff_artifact(golden_path, actual_path, out_path):
    try:
        from PIL import Image, ImageChops
        a = Image.open(golden_path).convert("RGB")
        b = Image.open(actual_path).convert("RGB")
        if a.size == b.size:
            diff = ImageChops.difference(a, b)
            diff.save(out_path)
            return
    except ImportError:
        pass
    subprocess.run(["compare", str(golden_path), str(actual_path), str(out_path)],
                    capture_output=True)


def bless(manifest):
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    bench_cfg = manifest["benchmark"]
    production_args = manifest.get("productionConfig", {}).get("args", [])

    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        print(f"[bless] {zone} ...")
        png_paths, csv_rows, memory, stdout_text = run_benchmark(
            zone_cfg, bench_cfg, ARTIFACTS_DIR, production_args)

        gl_version = extract_gl_version(stdout_text)
        if gl_version:
            manifest["blessedUnder"]["glVersion"] = gl_version

        metrics = {}
        for f in zone_cfg["designatedFrames"]:
            row = csv_rows[f]
            metrics[str(f)] = {
                "draws": int(row["draws"]),
                "stateChanges": int(row["state_changes"]),
                "polys": int(row["polys"]),
            }
        zone_cfg["metrics"] = metrics
        zone_cfg["p95CpuMsBaseline"] = p95([float(r["cpu_ms"]) for r in csv_rows])
        if memory:
            # T69: bless every field the benchmark now emits -- budget pairs
            # (blessed for display/debugging even though the check is
            # against the live budget, not the blessed value), baseline
            # fields (the actual regression-detection targets), and the
            # report-only fields (recorded so a re-bless's diff shows RSS
            # movement over time even though nothing gates on it).
            mem = {}
            for used_key, limit_key, _label in MEMORY_BUDGET_PAIRS:
                mem[used_key] = memory[used_key]
                mem[limit_key] = memory[limit_key]
            for key in MEMORY_BASELINE_FIELDS:
                mem[key] = memory[key]
            for key in MEMORY_REPORT_ONLY_FIELDS:
                mem[key] = memory[key]
            zone_cfg["memory"] = mem

        for f, png_path in png_paths.items():
            dest = GOLDENS_DIR / f"{zone}_f{f}.png"
            shutil.copyfile(png_path, dest)
            print(f"  wrote {dest}")

    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[bless] manifest written: {MANIFEST_PATH}")

    write_contact_sheet(manifest)
    print("[bless] done. Goldens are NOT approved -- Will's sign-off is required before merge.")
    return 0


def verify(manifest):
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    bench_cfg = manifest["benchmark"]
    production_args = manifest.get("productionConfig", {}).get("args", [])
    failures = []
    mesa_mismatch_warned = False

    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        print(f"[verify] {zone} ...")
        try:
            png_paths, csv_rows, memory, stdout_text = run_benchmark(
                zone_cfg, bench_cfg, ARTIFACTS_DIR, production_args)
        except RuntimeError as e:
            failures.append(f"{zone}: {e}")
            continue

        gl_version = extract_gl_version(stdout_text)
        expected_gl_version = manifest["blessedUnder"]["glVersion"]
        if gl_version and gl_version != expected_gl_version and not mesa_mismatch_warned:
            print(f"  NOTE: mesa/GLES version differs from blessed baseline "
                  f"(blessed='{expected_gl_version}', current='{gl_version}') -- "
                  f"mesa upgraded, re-bless with --bless instead of treating diffs as regressions.")
            mesa_mismatch_warned = True

        # 1. Golden-frame pixel comparison.
        for f, actual_path in png_paths.items():
            golden_path = GOLDENS_DIR / f"{zone}_f{f}.png"
            if not golden_path.exists():
                failures.append(f"{zone} frame {f}: no golden at {golden_path} (run --bless first)")
                continue
            identical, diff_count, size = compare_images_exact(golden_path, actual_path)
            if not identical:
                diff_out = ARTIFACTS_DIR / f"{zone}_f{f}_diff.png"
                save_diff_artifact(golden_path, actual_path, diff_out)
                artifact_golden = ARTIFACTS_DIR / f"{zone}_f{f}_golden.png"
                shutil.copyfile(golden_path, artifact_golden)
                failures.append(
                    f"{zone} frame {f}: PIXEL MISMATCH ({diff_count} differing pixels, size={size}). "
                    f"Artifacts: {actual_path}, {artifact_golden}, {diff_out}"
                )

        # 2. Metric assertions.
        for f_str, expected in zone_cfg["metrics"].items():
            f = int(f_str)
            row = csv_rows[f]
            for key, csv_key in (("draws", "draws"), ("stateChanges", "state_changes"), ("polys", "polys")):
                actual_val = int(row[csv_key])
                if actual_val != expected[key]:
                    failures.append(
                        f"{zone} frame {f}: {key} mismatch (expected {expected[key]}, got {actual_val})"
                    )

        p95_cpu = p95([float(r["cpu_ms"]) for r in csv_rows])
        baseline = zone_cfg["p95CpuMsBaseline"]
        ceiling = baseline * 2.0
        if p95_cpu > ceiling:
            failures.append(
                f"{zone}: p95 CPU ms {p95_cpu:.3f} exceeds ceiling {ceiling:.3f} (baseline {baseline:.3f} x2.0)"
            )

        # 3. Memory assertions (T69).
        if memory:
            expected_mem = zone_cfg["memory"]

            # (a) Budget pairs: usage <= a real configured ceiling, checked
            # against the LIVE limit (not the blessed one) since the budget
            # is a config value, not a measurement -- and separately, exact-
            # match the usage against its blessed value (regression
            # detection on top of the budget check; texture cache did this
            # from H02 on, framebuffer usage is exact-deterministic per T69's
            # repeat-run data, same as every other new field).
            for used_key, limit_key, label in MEMORY_BUDGET_PAIRS:
                if used_key not in memory or used_key not in expected_mem:
                    continue  # field predates this bless (old manifest); skip rather than false-fail
                if memory[used_key] != expected_mem[used_key]:
                    failures.append(
                        f"{zone}: {label} bytes used mismatch (expected "
                        f"{expected_mem[used_key]}, got {memory[used_key]})"
                    )
                if memory[used_key] > memory[limit_key]:
                    failures.append(
                        f"{zone}: {label} bytes used {memory[used_key]} "
                        f"exceeds budget {memory[limit_key]}"
                    )

            # (b) Baseline-guarded fields: exact match, no configured budget
            # (see MEMORY_BASELINE_FIELDS' comment for why zero tolerance is
            # measured, not assumed).
            for key in MEMORY_BASELINE_FIELDS:
                if key not in memory or key not in expected_mem:
                    continue
                if memory[key] != expected_mem[key]:
                    failures.append(
                        f"{zone}: {key} mismatch (expected {expected_mem[key]}, got {memory[key]})"
                    )

            # (c) Report-only: printed prominently, recorded in the baseline
            # via bless(), never asserted in this unit.
            rss = memory.get("processRssBytes")
            untracked = memory.get("untrackedBytes")
            total = memory.get("totalEstimate")
            if rss is not None:
                pct = (100.0 * total / rss) if (total is not None and rss) else 0.0
                print(f"  [memory] {zone}: tracked={total} RSS={rss} ({pct:.0f}%) "
                      f"untracked={untracked}  (reported only, not gated -- see T69 Review)")

    if failures:
        print("\n=== VISUAL GATE: FAIL ===")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("\n=== VISUAL GATE: PASS ===")
    return 0


def write_contact_sheet(manifest):
    rows_html = []
    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        rows_html.append(f'<h2>{zone}</h2><p>{zone_cfg.get("description", "")}</p><div class="frames">')
        frame_descriptions = zone_cfg.get("frameDescriptions", {})
        for f in zone_cfg["designatedFrames"]:
            png_path = GOLDENS_DIR / f"{zone}_f{f}.png"
            data = base64.b64encode(png_path.read_bytes()).decode("ascii")
            metrics = zone_cfg["metrics"].get(str(f), {})
            desc = frame_descriptions.get(str(f), "")
            desc_html = f'<br><strong>{desc}</strong>' if desc else ""
            rows_html.append(
                f'<figure><img src="data:image/png;base64,{data}" alt="{zone} frame {f}">'
                f'<figcaption>frame {f} -- draws={metrics.get("draws")} '
                f'state={metrics.get("stateChanges")} polys={metrics.get("polys")}{desc_html}<br>'
                f'What to check: geometry silhouette, texture placement, lighting/fog tone.</figcaption></figure>'
            )
        rows_html.append("</div>")

    html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>H02 golden contact sheet (NOT APPROVED)</title>
<style>
body {{ font-family: sans-serif; background: #111; color: #eee; margin: 2rem; }}
h1 {{ color: #f80; }}
.frames {{ display: flex; flex-wrap: wrap; gap: 1rem; }}
figure {{ margin: 0; background: #222; padding: 0.5rem; border-radius: 4px; }}
img {{ max-width: 260px; display: block; }}
figcaption {{ font-size: 0.8rem; color: #ccc; margin-top: 0.4rem; }}
.banner {{ background: #542; border: 1px solid #f80; padding: 0.75rem; margin-bottom: 1.5rem; }}
</style></head>
<body>
<h1>H02 Golden Frame Contact Sheet</h1>
<div class="banner"><strong>NOT APPROVED.</strong> These frames were generated by the executor
and are pending Will's visual sign-off. This page does not constitute approval.</div>
<p>Blessed under: {manifest['blessedUnder']['glRenderer']} / {manifest['blessedUnder']['glVersion']}</p>
{''.join(rows_html)}
</body></html>
"""
    out_path = GOLDENS_DIR / "contact_sheet.html"
    out_path.write_text(html)
    print(f"[bless] contact sheet: {out_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bless", action="store_true", help="Regenerate goldens + manifest + contact sheet")
    args = parser.parse_args()

    if not BENCH_BIN.exists():
        print(f"SKIP: {BENCH_BIN} not found (build-gles2/ absent -- run scripts/build-gles2-x86.sh first)")
        return 125  # ctest SKIP_RETURN_CODE (see scripts/ci/visual_gate.sh / tests/CMakeLists.txt)

    manifest = load_manifest()
    if args.bless:
        return bless(manifest)
    return verify(manifest)


if __name__ == "__main__":
    sys.exit(main())
