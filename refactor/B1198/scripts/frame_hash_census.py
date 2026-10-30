#!/usr/bin/env python3
"""B1198 (#517): the frame-hash byte-identity census -- the gate-path
correctness evidence.

sha256[:16] of every frame_*.ppm in a run directory, compared to the
driver-path run's frame at the same index. The committed verdict is
refactor/B1198/tables/surf/surfhash_verdict.txt (produced over the
recheck frame sets, /mnt/projects/fable/review/B1198-recheck/).

Usage: frame_hash_census.py <runs dir> <path tag> [out hashes.tsv]
  <runs dir>  a directory of <row>_<mode>_c1_s2.0/c1/ frame dirs
  <path tag>  the label for these runs in the TSV (surf | drm)
  [out]       default: <runs dir>/hashes.tsv
"""
import glob
import hashlib
import os
import sys


def main(runs, path, out=None):
    if out is None:
        out = os.path.join(runs, "hashes.tsv")
    with open(out, "w") as f:
        for c1 in sorted(glob.glob(os.path.join(runs, "*", "c1"))):
            run = os.path.basename(os.path.dirname(c1))
            # <row>_<mode>_c1_s2.0 with row in {tour_93, b1130_r2} and
            # mode in {driver, glread, pbo, fbo, dirty}: split from the right.
            head, _ = run.rsplit("_c", 1)
            row, mode = head.rsplit("_", 1)
            for p in sorted(glob.glob(os.path.join(c1, "frame_*.ppm"))):
                with open(p, "rb") as fh:
                    h = hashlib.sha256(fh.read()).hexdigest()[:16]
                f.write(f"{path}\t{row}\t{mode}\t{os.path.basename(p)}\t{h}\n")
    # the verdict: per run, frames identical to the driver run at the same
    # frame index (the driver run is the baseline: it IS the current path).
    hashes = {}
    with open(out) as f:
        for line in f:
            p, row, mode, fname, h = line.rstrip("\n").split("\t")
            hashes.setdefault((row, mode), {})[fname] = h
    for row in sorted({k[0] for k in hashes}):
        base = hashes.get((row, "driver"), {})
        line = f"{path} {row}: driver:{len(base)}/{len(base)}"
        for mode in ("glread", "pbo", "fbo", "dirty"):
            m = hashes.get((row, mode), {})
            common = set(base) & set(m)
            same = sum(1 for k in common if m[k] == base[k])
            line += f" {mode}:{same}/{len(m)}"
        print(line)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
