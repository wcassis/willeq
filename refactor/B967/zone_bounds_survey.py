#!/usr/bin/env python3
"""B967 REMAINS 4: varying-range survey.

Parse `Zone bounds (EQ)` from each per-zone sweep log, compute the
XZ-plane diagonal (the relevant extent for the player-light varying,
|v| = d/64, where d is the distance in the XZ plane since the light is
vertical) and the full 3D diagonal, and report the max across all zones
plus the five gate zones' values.

The FS clamp point is 15,677 units (|v| = d/64; d2 = dot(v,v) overflows
FP16 past 15,677^2 = 245,763,889; the clamp is at 60,000 in d2-space,
which corresponds to d = sqrt(60000) * 64 = 4,989.9... wait no.
Actually: v = (uPlayerLightPos - worldPos) * 0.015625 = delta * (1/64).
d2 = dot(v, v) = |delta|^2 / 4096.
FP16 max is 65504. d2 overflows past d2 = 65504, i.e. |delta|^2 > 65504 * 4096
= 268,435,456, i.e. |delta| > 16,384.
The README says the clamp is at 60,000 in d2-space (the clamp value in the FS),
so the light goes to 0 at d2 = 60000, i.e. |delta| = sqrt(60000) * 64 = 4,989.9.
But the README also says "overflows FP16 only past 15,677 units". Let me re-read:
"Varying range: |v| = d/64; the FS square overflows FP16 only past 15,677 units
(clamped to 60,000 there, which yields coordinate 1 -> A = 0)."
So d (the world-space distance) = 15,677 units is where d2 = (d/64)^2 = 15677^2/4096
= 245,763,889 / 4096 = 59,999 ≈ 60,000. OK so the clamp value in the FS is 60,000
(d2-space), and it corresponds to d = 15,677 world units. Good.

So the survey threshold is: is the zone's XZ diagonal (or 3D diagonal) < 15,677?
"""
import os, re, sys, math

LOGS_DIR = sys.argv[1] if len(sys.argv) > 1 else "logs"
GATE_ZONES = {"qeynos2", "gfaydark", "befallen", "iceclad", "timorous"}
THRESHOLD = 15677  # units, the FS clamp point (d where d2 = 60000)

rows = []
for fn in sorted(os.listdir(LOGS_DIR)):
    if not fn.endswith(".log"):
        continue
    zone = fn[:-4]
    with open(os.path.join(LOGS_DIR, fn)) as f:
        text = f.read()
    m = re.search(
        r"Zone bounds \(EQ\): X=\[(-?[\d.]+),(-?[\d.]+)\] "
        r"Y=\[(-?[\d.]+),(-?[\d.]+)\] "
        r"Z=\[(-?[\d.]+),(-?[\d.]+)\]", text)
    if not m:
        print(f"{zone}: NO Zone bounds line", file=sys.stderr)
        continue
    x0, x1, y0, y1, z0, z1 = map(float, m.groups())
    dx, dy, dz = abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)
    xz_diag = math.hypot(dx, dz)
    full_diag = math.sqrt(dx * dx + dy * dy + dz * dz)
    rows.append((zone, x0, x1, y0, y1, z0, z1, dx, dy, dz, xz_diag, full_diag))

rows.sort(key=lambda r: -r[11])  # sort by full_diag desc

print(f"{'zone':<20} {'X range':>12} {'Y range':>12} {'Z range':>12} "
      f"{'XZ diag':>10} {'full diag':>10}  exceeds 15677?")
print("-" * 100)
exceeds = []
for (zone, x0, x1, y0, y1, z0, z1, dx, dy, dz, xz_diag, full_diag) in rows:
    flag = ""
    if full_diag > THRESHOLD:
        flag = "YES (full)"
        exceeds.append((zone, "full", full_diag))
    if xz_diag > THRESHOLD:
        flag += " XZ"
        if not any(z == zone for z, _, _ in exceeds):
            exceeds.append((zone, "xz", xz_diag))
    if zone in GATE_ZONES:
        print(f"{'*':<3}{zone:<17} [{x0:7.0f},{x1:7.0f}] [{y0:7.0f},{y1:7.0f}] "
              f"[{z0:7.0f},{z1:7.0f}] {xz_diag:10.1f} {full_diag:10.1f}  {flag}")
    elif flag:
        print(f"{' ':<3}{zone:<17} [{x0:7.0f},{x1:7.0f}] [{y0:7.0f},{y1:7.0f}] "
              f"[{z0:7.0f},{z1:7.0f}] {xz_diag:10.1f} {full_diag:10.1f}  {flag}")

print("-" * 100)
print(f"total zones parsed: {len(rows)}")
print(f"zones with full 3D diagonal > {THRESHOLD}: "
      f"{[z for z, k, _ in exceeds if k == 'full']}")
print(f"zones with XZ diagonal > {THRESHOLD}: "
      f"{[z for z, k, _ in exceeds if k == 'xz']}")

# Also print the 5 gate zones explicitly
print("\nFive gate zones:")
for (zone, x0, x1, y0, y1, z0, z1, dx, dy, dz, xz_diag, full_diag) in rows:
    if zone in GATE_ZONES:
        print(f"  {zone:<12} XZ diag {xz_diag:8.1f}  full diag {full_diag:8.1f}  "
              f"{'EXCEEDS' if full_diag > THRESHOLD else 'ok'}")
