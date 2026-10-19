#!/usr/bin/env python3
"""B1023 local comparer (same shape as B1037's compare_lane.py).

Runs the gate's OWN compare rule -- scripts/ci/visual_gate.py's
compare_images_exact (decoded-pixel, RGB; the Pi has no PIL so the
comparison happens here) -- for every designated frame of every manifest
zone, against the lane-1 captured frames, plus the gate's metrics assertion
vs the pi section's blessed metrics (from the run's CSV). Measurement only:
no golden edit, no gate.

Usage: compare_lane1.py <captured-dir>
Writes <captured-dir>/compare_report.txt; exit 0 always.
"""
import csv
import json
import sys
from pathlib import Path

REPO = Path("/home/user/src/willeq-wt/kb-b1023-pi-depth16")
sys.path.insert(0, str(REPO / "scripts" / "ci"))
import visual_gate as vg  # noqa: E402

cap = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
manifest = vg.load_manifest()
section = manifest.get("platforms", {}).get("pi")
assert section, "the manifest has no platforms.pi section"
by_zone_pi = {z["zone"]: z for z in section["zones"]}

lines = []
def w(s=""):
    lines.append(s)
    print(s)

# CSV row lookup: zone csv has per-frame rows (frame, draws, state_changes,
# polys, ...). Load whatever columns exist.
def load_csv(zone):
    p = cap / f"{zone}.csv"
    if not p.exists():
        return None
    with open(p) as f:
        rows = list(csv.DictReader(f))
    return rows

w("zone | frame | pixels vs Pi golden | metrics vs Pi (draws/state/polys)")
w("-----|-------|--------------------|-------------------------------------")
for zone_cfg in manifest["zones"]:
    zone = zone_cfg["zone"]
    if zone not in by_zone_pi:
        continue
    pi = by_zone_pi[zone]
    csvrows = load_csv(zone)
    for frame in pi["designatedFrames"]:
        gpath = REPO / "tests" / "goldens" / "pi" / zone / f"f{frame}.png"
        lpath = cap / zone / f"f{frame}.png"
        if not gpath.exists() or not lpath.exists():
            w(f"{zone} | f{frame} | MISSING golden={gpath.exists()} lane={lpath.exists()} |")
            continue
        identical, diff, size = vg.compare_images_exact(str(gpath), str(lpath))
        px = "IDENTICAL" if identical else f"differs ({diff:,} px)"
        # metrics: the CSV's measured frame rows (designatedFrames are the
        # measured frames); compare against the pi section's blessed metrics.
        m = pi["metrics"].get(str(frame), {})
        met = ""
        if csvrows is not None:
            # find the row for this frame (the CSV indexes frames from 0)
            row = None
            for r in csvrows:
                try:
                    if int(r.get("frame", -1)) == frame:
                        row = r
                        break
                except (ValueError, TypeError):
                    pass
            if row is None:
                met = "csv-row-missing"
            else:
                parts = []
                ok = True
                for key, csvkey in (("draws", "draws"), ("stateChanges", "state_changes"), ("polys", "polys")):
                    if key in m:
                        try:
                            v = int(float(row[csvkey]))
                        except (KeyError, ValueError):
                            met = "csv-key-missing"
                            ok = False
                            break
                        if v != m[key]:
                            ok = False
                            met = f"differs ({key} {v}!={m[key]})"
                if ok:
                    met = "bit-exact"
        w(f"{zone} | f{frame} | {px} | {met}")

out = cap / "compare_report.txt"
out.write_text("\n".join(lines) + "\n")
print(f"\nreport -> {out}")
