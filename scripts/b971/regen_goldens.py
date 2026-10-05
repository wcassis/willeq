#!/usr/bin/env python3
"""B971: regenerate the 15 gate frames with the gate's OWN golden invocation
(scripts/ci/visual_gate.py run_benchmark(): same manifest poses, same
benchmark/productionConfig args, same --occluder-budget pin) from a worktree's
build-gles2 tree, WITHOUT --bless's other side effects (--bless also re-measures
p95CpuMsBaseline, the T69 memory block and the memoryCoverage block).

usage: regen_goldens.py <worktree> <out_dir> [--install]

- Runs every manifest zone once into <out_dir>/run1 and again into <out_dir>/run2
  (determinism check: the two must be pixel-identical).
- Prints, per frame, the committed golden (BEFORE) vs run1 (AFTER): pixels with a
  channel differing by >32, >8, any; max abs; mean abs (over all RGB values);
  and, of the >32 pixels, how many are DARKER (after < before on the max-diff
  channel) vs brighter (the B967 table's "all >32 darker?" column).
- Prints every manifest field the golden-image run would bless (metrics,
  memory) that differs from the committed manifest. Nothing but the PNGs is
  expected to move (the [memgpu] logical_depth_stencil row is not a manifest
  field, so it moves without being listed here).
- --install copies run1's PNGs over tests/goldens/<zone>_f<n>.png.
Writes <out_dir>/golden_delta.tsv and <out_dir>/manifest_fields.tsv.
"""
import json
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)


def main():
    wt = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    install = "--install" in sys.argv[3:]
    sys.path.insert(0, str(wt / "scripts" / "ci"))
    import visual_gate as vg  # noqa: E402

    assert vg.PROJECT_DIR == wt, (vg.PROJECT_DIR, wt)
    manifest = vg.load_manifest()
    bench_cfg = manifest["benchmark"]
    prod = manifest.get("productionConfig", {}).get("args", [])
    rows, fields = [], []
    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        runs = []
        for r in ("run1", "run2"):
            d = out / r
            d.mkdir(parents=True, exist_ok=True)
            pngs, csv_rows, memory, stdout = vg.run_benchmark(zone_cfg, bench_cfg, d, prod)
            (d / f"{zone}.log").write_text(stdout)
            runs.append((pngs, csv_rows, memory, stdout))
        pngs, csv_rows, memory, stdout = runs[0]
        for f in zone_cfg["designatedFrames"]:
            a1, a2 = load(runs[0][0][f]), load(runs[1][0][f])
            det = int((a1 != a2).any(axis=2).sum())
            g = load(vg.GOLDENS_DIR / f"{zone}_f{f}.png")
            dd = np.abs(g - a1)
            per = dd.max(axis=2)
            gt32 = per > 32
            # darker/brighter on the max-diff channel of each >32 pixel
            # (argmax breaks R/G/B channel ties to the earlier channel)
            ch = np.argmax(dd, axis=2)
            gv = np.take_along_axis(g, ch[:, :, None], axis=2).squeeze(-1)
            av = np.take_along_axis(a1, ch[:, :, None], axis=2).squeeze(-1)
            darker = int(((av < gv) & gt32).sum())
            brighter = int(((av > gv) & gt32).sum())
            rows.append((f"{zone}_f{f}", int((per > 32).sum()), int((per > 8).sum()), int((per > 0).sum()),
                         int(per.max()), float(dd.mean()), det, darker, brighter))
            row = csv_rows[f]
            for k, col in (("draws", "draws"), ("stateChanges", "state_changes"), ("polys", "polys")):
                old = zone_cfg["metrics"][str(f)][k]
                new = int(row[col])
                if old != new:
                    fields.append((zone, f"metrics.{f}.{k}", old, new))
        for k, old in zone_cfg.get("memory", {}).items():
            new = memory.get(k) if memory else None
            if new != old:
                fields.append((zone, f"memory.{k}", old, new))
        gl = vg.extract_gl_version(stdout)
        if gl != manifest["blessedUnder"]["glVersion"]:
            fields.append((zone, "blessedUnder.glVersion", manifest["blessedUnder"]["glVersion"], gl))
        if install:
            for f, p in pngs.items():
                shutil.copyfile(p, vg.GOLDENS_DIR / f"{zone}_f{f}.png")
    with open(out / "golden_delta.tsv", "w") as fh:
        fh.write("frame\tpx_gt32\tpx_gt8\tpx_any\tmax_abs\tmean_abs\trun1_vs_run2_px\tdarker_32\tbrighter_32\n")
        for r in rows:
            fh.write("%s\t%d\t%d\t%d\t%d\t%.4f\t%d\t%d\t%d\n" % r)
    with open(out / "manifest_fields.tsv", "w") as fh:
        fh.write("zone\tfield\told\tnew\n")
        for r in fields:
            fh.write("%s\t%s\t%s\t%s\n" % r)
    print(open(out / "golden_delta.tsv").read())
    print("manifest fields that moved:", len(fields))
    for r in fields:
        print("  %s %s %s -> %s" % r)
    print("installed" if install else "not installed")


if __name__ == "__main__":
    main()
