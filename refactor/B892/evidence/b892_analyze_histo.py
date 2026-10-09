#!/usr/bin/env python3
"""B892: compute the steady-state levitation bob stats from the decoded wire log,
restricted to samples strictly after the buff landed (05:35:30), and to samples
where x/y are at the resting spot (no walking). Baseline = -38.2157 (feet).
"""
import os, re, statistics as st
BASE = -38.2157
rows = []
# The committed decoded capture (one "ts seq x y z" line per C->S packet);
# md5-identical to the /tmp working file the scripts were first run against.
DECODED = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "b892_levitate_wirecap.decoded.txt")
for ln in open(DECODED, errors="replace").read().splitlines():
    m = re.search(r"(\S+) (\S+)\s+seq=(\d+)\sx=(-?\d+\.\d+) y=(-?\d+\.\d+) z=(-?\d+\.\d+)", ln)
    if not m:
        continue
    date, clock = m.group(1), m.group(2)
    if not (date.startswith("10-09-2026")):
        continue
    # only the clean levitation window, after the buff (05:34:05) settled
    if clock < "05:35:30":
        continue
    x, y, z = float(m.group(4)), float(m.group(5)), float(m.group(6))
    # resting: at the goto spot
    if abs(x - 20.0) > 0.5 or abs(y - 3.1554) > 0.5:
        continue
    rows.append((clock, z))

if not rows:
    print("NO CLEAN ROWS"); raise SystemExit(1)
hz = [z - BASE for _, z in rows]
mn, mx = min(hz), max(hz)
mean = st.mean(hz)
med = st.median(hz)
# The levitation bob is periodic; the hover LINE the client settles on is the
# low of the bob (the point where the client re-anchors the feet). Report all.
print(f"clean resting window: {len(hz)} samples, {rows[0][0]} .. {rows[-1][0]}")
print(f"baseline (feet, standing) = {BASE}")
print(f"hover height above floor (feet):")
print(f"  LOW  (min)  = {mn:.3f}   <- the hover line (client re-anchors here)")
print(f"  MEAN        = {mean:.3f}")
print(f"  MEDIAN      = {med:.3f}")
print(f"  HIGH (max)  = {mx:.3f}   <- top of the levitation bob")
print(f"  amplitude   = {mx-mn:.3f} (bob peak-to-peak)")
# Period: one upward crossing per cycle (~1 Hz wire, so sample count ~= seconds)
cross = sum(1 for i in range(1, len(hz)) if (hz[i-1]-mean) < 0 <= (hz[i]-mean))
period = (len(hz)) / max(1, cross) if cross else 0
print(f"  bob period  ~ {period:.1f} s (from {cross} upward-crossings over {len(hz)} samples)")
# show the low-region samples (the hover line)
lows = sorted(rows, key=lambda r: r[1])[:8]
print("  lowest 8 samples (the hover line):")
for c, z in lows:
    print(f"    {c} z={z:.4f} h={z-BASE:+.3f}")
