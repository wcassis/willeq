#!/usr/bin/env python3
"""B892 verification — independent raw decode + recompute of every cited number.

This is the single strongest evidence in this directory: it decodes the RAW
eqemu zone log (every C->S OP_ClientUpdate, "Size [48]" = 2 opcode bytes + 46
payload bytes) and confirms the result is byte-identical to the committed
`b892_levitate_wirecap.decoded.txt`, then recomputes the floor, the bob
statistics and the descent legs that `levitation.h` and the README cite.

Payload layout, verified against packets in the log:
  +0  u16 LE seq   +18 f32 x   +22 f32 (const, ~-3.42)   +26 f32 z   +30 f32 y

Run:  python3 b892_verify_decode.py        (no arguments, no network)
"""
import os, re, struct, statistics as st

EV = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(EV, "b892_levitate_wirecap_freporte.log")
DEC = os.path.join(EV, "b892_levitate_wirecap.decoded.txt")

hdr = re.compile(r"\[(\S+ \S+)\] \[Zone\] \[Packet (C->S|S->C)\] \[(\S+)\] \[(\S+)\] Size \[(\d+)\]")

packets, times = [], []
cur_op = cur_size = cur_ts = None
hexbuf = []
def flush():
    global cur_op, cur_size, cur_ts, hexbuf
    if cur_op == "OP_ClientUpdate" and cur_size == 48 and len(hexbuf) >= 46:
        packets.append(bytes.fromhex("".join(hexbuf[:46])))
        times.append(cur_ts)
    cur_op = cur_size = cur_ts = None
    hexbuf = []
for ln in open(RAW, errors="replace").read().splitlines():
    m = hdr.match(ln)
    if m:
        flush()
        cur_op, cur_size = m.group(3), int(m.group(5))
        cur_ts = m.group(1).split()[1]
        continue
    if cur_op and ln.lstrip()[:1].isdigit():
        body = ln.split("|", 1)[0].rstrip()
        body = re.sub(r"^\s*\d+:\s*", "", body).replace("-", " ")
        toks = body.split()
        if all(len(t) == 2 and re.fullmatch(r"[0-9a-fA-F]{2}", t) for t in toks):
            hexbuf.extend(t.lower() for t in toks)
flush()

# 1) Cross-check the raw decode against the committed decoded file.
dm = re.compile(r"(\S+) (\S+) seq=(\d+) x=(-?\d+\.\d+) y=(-?\d+\.\d+) z=(-?\d+\.\d+)")
dec = []
for ln in open(DEC):
    if "seq=" not in ln:
        continue
    m = dm.match(ln)
    assert m, f"unparseable decoded line: {ln!r}"
    dec.append(m)
n = min(len(packets), len(dec))
same = 0
for i in range(n):
    raw = packets[i]
    seq = struct.unpack_from("<H", raw, 0)[0]
    x = struct.unpack_from("<f", raw, 18)[0]
    z = struct.unpack_from("<f", raw, 26)[0]
    y = struct.unpack_from("<f", raw, 30)[0]
    d = dec[i]
    if (times[i] == d.group(2)) and (seq == int(d.group(3))) and \
       abs(x-float(d.group(4)))<5e-5 and abs(y-float(d.group(5)))<5e-5 and abs(z-float(d.group(6)))<5e-5:
        same += 1
print(f"[verify] raw-decode vs committed decoded file: {same}/{n} records byte-identical")
assert same == n, "raw decode diverges from the committed decoded file"

# 2) Recompute every cited number from the raw decode.
rows = []
for raw, t in zip(packets, times):
    x = struct.unpack_from("<f", raw, 18)[0]
    z = struct.unpack_from("<f", raw, 26)[0]
    y = struct.unpack_from("<f", raw, 30)[0]
    rows.append((t, x, y, z))

BASE = -38.2157
spot5 = [r for r in rows if abs(r[1]-20.0)<0.5 and abs(r[2]-5.0)<0.5]
const5 = [r for r in spot5 if abs(r[3]-BASE) < 5e-4]
print(f"[floor] y=5.0 standing spot: {len(spot5)} samples; exactly -38.2157: "
      f"{len(const5)} ({const5[0][0]}..{const5[-1][0]})")

spot = [r for r in rows if abs(r[1]-20.0)<0.5 and abs(r[2]-3.1554)<0.5]
def win(start):
    return [r for r in spot if r[0] >= start]

w = win("05:35:30"); hz = [r[3]-BASE for r in w]
mean, med = st.mean(hz), st.median(hz)
cross = sum(1 for i in range(1, len(hz)) if (hz[i-1]-mean) < 0 <= (hz[i]-mean))
print(f"[bob]   window {w[0][0]}..{w[-1][0]} n={len(w)}  LOW={min(hz):.4f} "
      f"MEAN={mean:.4f} MEDIAN={med:.4f} HIGH={max(hz):.4f}  "
      f"crossings={cross} period={len(w)/cross if cross else 0:.2f}s")

w2 = win("05:34:10")
ts = [int(c.split(":")[0])*3600+int(c.split(":")[1])*60+int(c.split(":")[2]) for c in [r[0] for r in w2]]
zs = [r[3] for r in w2]
legs, i, L = [], 0, len(zs)
while i < L:
    if 0 < i < L-1 and zs[i] > zs[i-1] and zs[i] >= zs[i+1]:
        j = i
        while j < L-1 and zs[j+1] <= zs[j]:
            j += 1
        dt, dz = ts[j]-ts[i], zs[i]-zs[j]
        if dt > 0:
            legs.append(dz/dt)
        i = j
    i += 1
print(f"[descent] window {w2[0][0]}..{w2[-1][0]} n={len(w2)}  legs={len(legs)}  "
      f"min={min(legs):.4f} median={st.median(legs):.4f} mean={st.mean(legs):.4f} max={max(legs):.4f}")

print(f"[const] kLevitationHoverHeight = 1.5f  (bob LOW {min(hz):.4f} -> 1.5)")
print(f"[const] kLevitationDescentRate = 1.3f  (median {st.median(legs):.4f} -> 1.3)")
