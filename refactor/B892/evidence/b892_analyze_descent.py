#!/usr/bin/env python3
"""B892: measure the descent velocity of the classic client's self-levitation.

The classic client, when levitating, does NOT do a clean constant-rate descent;
it BOBS around the hover line. The 'descent from above the hover line' that
fable's kLevitationDescentRate models is best proxied by the high->low leg of
each bob cycle. Compute (z_high - z_low)/dt for every local-max -> next
local-min pair in the clean resting window, and report the distribution.
If it clusters near a value, that's the measured descent velocity; if it's
broad/oscillatory, a single constant is not a faithful model and we report the
range instead of fabricating a point value.
"""
import os, re
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
    if not date.startswith("10-09-2026") or clock < "05:34:10":
        continue
    x, y, z = float(m.group(4)), float(m.group(5)), float(m.group(6))
    if abs(x - 20.0) > 0.5 or abs(y - 3.1554) > 0.5:
        continue
    rows.append((clock, z))

# seconds from window start (1 Hz wire)
t0 = rows[0][0]
def secs(clock):
    # clock = HH:MM:SS
    h, mi, s = clock.split(":")
    return (int(h)*3600 + int(mi)*60 + int(s))
ts = [secs(c) for c, z in rows]
zs = [z for c, z in rows]

# find local maxima and the following local minimum
legs = []
n = len(zs)
i = 0
while i < n:
    # find a local max (peak of a bob)
    if 0 < i < n-1 and zs[i] > zs[i-1] and zs[i] >= zs[i+1]:
        peak_i = i
        # descend to the next local min
        j = peak_i
        while j < n-1 and zs[j+1] <= zs[j]:
            j += 1
        trough_i = j
        dt = ts[trough_i] - ts[peak_i]
        dz = zs[peak_i] - zs[trough_i]  # positive = dropped
        if dt > 0:
            legs.append((dt, dz, dz/dt, peak_i, trough_i))
        i = trough_i
    i += 1

print(f"clean window: {n} samples; {len(legs)} full high->low legs measured")
if legs:
    rates = [r[2] for r in legs]
    import statistics as st
    print("  per-leg descent rate (u/s): dt, dz, rate")
    for dt, dz, rate, pi, ti in legs:
        print(f"    t+{ts[pi]:4d}  dt={dt:3.0f}s  dz={dz:+.3f}  rate={rate:.3f} u/s")
    print(f"  rate: min={min(rates):.3f}  median={st.median(rates):.3f}  mean={st.mean(rates):.3f}  max={max(rates):.3f} u/s")
    print(f"  dz (drop per leg): mean={st.mean([r[1] for r in legs]):.3f} u")
    print(f"  dt (time per leg): mean={st.mean([r[0] for r in legs]):.1f} s")
