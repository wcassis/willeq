#!/usr/bin/env python3
"""T108 — regression test: totalEstimate must not credit EQE1's MAPPED bytes
as if they were RESIDENT.

Background (see refactor/T108_totalestimate_mapped_vs_resident.md's Review):
`eqe1MappedBytes` is address-space size, not RSS. T105 measured EQE1 archives
at 1.6-10.6% resident. Before T108, both `IrrlichtRenderer::
getMemoryReportBreakdown()` (src/client/graphics/renderer_diagnostics.cpp)
and its necessarily-duplicated benchmark-side twin, `computeMemorySnapshot()`
(tools/hwcaps/zone_bench_memory.cpp -- see that file's header comment for why
a second implementation exists), added `eqe1MappedBytes` into `totalEstimate`
-- crediting ~110-120MB of never-faulted-in address space as "accounted for"
per golden zone, which silently deflated `untrackedBytes` (RSS - totalEstimate)
by the same amount.

This test runs the ALREADY-BUILT `gles2_zone_benchmark` binary (no
production-code changes for the test to make, it drives the tool the memory
gate itself uses -- tools/hwcaps/zone_bench_memory.cpp's computeMemorySnapshot())
with --eqe1-path against a real EQE1 archive set, and asserts the emitted
`--memory-json`'s `totalEstimate` field is EXACTLY the sum of every other
tallied category and does NOT include `eqe1MappedBytes`.

SKIPs (exit 125, matching visual_gate.sh's SKIP_RETURN_CODE convention) if:
  - the benchmark binary hasn't been built (this build tree wasn't configured
    with EQT_GRAPHICS, or hasn't been built yet), or
  - no EQE1 archive data is available (data/eqe1_client, gitignored local
    symlink -- same convention scripts/ci/visual_gate.py's EQE1_PATH uses).

Usage: test_eqe1_totalestimate_accounting.py <path-to-gles2_zone_benchmark>
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP_RETURN_CODE = 125

PROJECT_DIR = Path(__file__).resolve().parents[2]
EQ_PATH = PROJECT_DIR / "data" / "eq_client"
EQE1_PATH = PROJECT_DIR / "data" / "eqe1_client"

# Every category computeMemorySnapshot() folds into totalEstimate, EXCEPT
# eqe1MappedBytes (T108) and textureCacheBytesUsed (already counted via GPU
# Textures, per renderer_diagnostics.cpp's pre-existing "do NOT add to
# totalEstimate here" rule that computeMemorySnapshot() also follows -- it
# never adds textureCacheBytesUsed to totalEstimate either).
SUMMED_FIELDS = [
    "zoneGeometryBytes",
    "placeableObjectsBytes",
    "s3dZoneSourceBytes",
    "gpuTextureBytes",
    "vboEboBytes",
    "framebufferBytes",
    "characterModelsBytes",
]


def main():
    if len(sys.argv) != 2:
        print("usage: test_eqe1_totalestimate_accounting.py <gles2_zone_benchmark path>", file=sys.stderr)
        return 1
    bench_bin = Path(sys.argv[1])

    if not bench_bin.exists():
        print(f"SKIP: {bench_bin} not built (EQT_GRAPHICS off, or not built yet)")
        return SKIP_RETURN_CODE
    if not EQE1_PATH.exists():
        print(f"SKIP: {EQE1_PATH} not present on this machine (gitignored local "
              f"symlink to an EQE1 archive directory -- see scripts/ci/visual_gate.py's "
              f"EQE1_PATH for the same convention)")
        return SKIP_RETURN_CODE
    if not EQ_PATH.exists():
        print(f"SKIP: {EQ_PATH} not present on this machine")
        return SKIP_RETURN_CODE

    with tempfile.TemporaryDirectory() as td:
        mem_path = Path(td) / "qeynos2_mem.json"
        cmd = [
            str(bench_bin),
            "--zone", "qeynos2",
            "--eq-path", str(EQ_PATH),
            "--eqe1-path", str(EQE1_PATH),
            "--frames", "1",
            "--warmup", "0",
            "--res", "800", "600",
            "--memory-json", str(mem_path),
            "--occluder-budget", "16",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if proc.returncode != 0:
            print(f"FAIL: benchmark exited {proc.returncode}\ncmd: {' '.join(cmd)}\n"
                  f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}", file=sys.stderr)
            return 1
        if not mem_path.exists():
            print(f"FAIL: expected memory JSON missing: {mem_path}\nstdout:\n{proc.stdout}", file=sys.stderr)
            return 1

        with open(mem_path) as f:
            mem = json.load(f)

    eqe1_mapped = mem.get("eqe1MappedBytes", 0)
    total = mem.get("totalEstimate", 0)

    # Sanity: this run must actually have exercised EQE1 archives, or the
    # assertions below would trivially pass for the wrong reason (nothing to
    # wrongly credit). qeynos2 has 28 EQE1 archives per T105/T106/T107's own
    # measurements (well over 1MB mapped even in the worst case).
    if eqe1_mapped < 1024 * 1024:
        print(f"FAIL: eqe1MappedBytes suspiciously small ({eqe1_mapped} bytes) -- "
              f"EQE1 archives may not have loaded; this test cannot exercise the "
              f"regression it's checking for. Full JSON: {mem}", file=sys.stderr)
        return 1

    expected_total = sum(mem.get(f, 0) for f in SUMMED_FIELDS)

    print(f"eqe1MappedBytes = {eqe1_mapped:,} bytes ({eqe1_mapped / (1024*1024):.1f} MB)")
    print(f"totalEstimate   = {total:,} bytes ({total / (1024*1024):.1f} MB)")
    print(f"sum of {SUMMED_FIELDS} = {expected_total:,} bytes")

    # THE regression check: totalEstimate must equal the sum of every OTHER
    # category, i.e. eqe1MappedBytes must NOT be folded in. Before T108, this
    # failed by exactly eqe1MappedBytes (~110-120MB on the golden zones).
    if total != expected_total:
        diff = total - expected_total
        print(f"FAIL: totalEstimate ({total:,}) != sum of tracked categories "
              f"({expected_total:,}); diff = {diff:,} bytes. If diff == "
              f"eqe1MappedBytes ({eqe1_mapped:,}), this is exactly the T108 "
              f"regression: totalEstimate is crediting EQE1's MAPPED bytes as "
              f"if they were RESIDENT.", file=sys.stderr)
        return 1

    # Belt-and-suspenders: also assert totalEstimate does not equal
    # (expected_total + eqe1MappedBytes), i.e. it isn't ALSO double-counting
    # some other way that happens to cancel out above.
    if total == expected_total + eqe1_mapped:
        print("FAIL: totalEstimate == sum(tracked) + eqe1MappedBytes -- "
              "eqe1MappedBytes IS being credited into totalEstimate.", file=sys.stderr)
        return 1

    print("PASS: totalEstimate excludes eqe1MappedBytes (mapped-not-resident EQE1 bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
