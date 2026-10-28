#!/usr/bin/env python3
"""B1176: wire z from a lane-5 capture, via the proven eqpcap decoder.

eqpcap.decode() handles the pcap link layer (SLL2 from `tcpdump -i any`, or
Ethernet) and the daybreak stream framing (per-opcode, with the zlib
decompression and CRC the raw byte-walk got wrong), returning one row per app
opcode:

    (t, dirn, sport, cport, nm, op, payload)

For OP_ClientUpdate (0x14cb) the payload is the 36-byte position struct
(src/client/movement/movement_update.cpp:751-761, eqemu common/patches/
titanium_structs.h:1226):

    +0  u16 spawn_id
    +2  u16 sequence
    +4  f32 y_pos      (server y)
    +8  f32 delta_z
    +12 f32 delta_x
    +16 f32 delta_y
    +20 u32 anim_and_delta_heading
    +24 f32 x_pos      (server x)
    +28 f32 z_pos      <-- the wire-frame z
    +32 u16 heading_and_padding
    +34 u8  unknown[2]

z_pos is the WIRE frame (include/client/position_frames.h, T311:
wireZ = groundZ + 0.625*size, measured exactly linear, no race term). The
official client adds a +0.001..0.003 settle epsilon as it lands; a standing
character's z is constant, so the median is the measurement.

The capture holds BOTH clients (A the subject, B the reference). We cluster by
(x,y) and report each cluster's z, so the caller reads A's wire z from the
cluster near A's seeded pose and B's from the cluster near B's.

usage: measure_wire_z.py <capture.pcap> [out.tsv]
Writes one row per (x,y) position cluster: cluster, x_med, y_med, z_med,
z_min, z_max, n, cports, first_utc, last_utc.
"""
import struct
import sys
import datetime
from collections import defaultdict
from pathlib import Path

# The proven daybreak decoder lives in the oracle vendor dir.
_VENDOR = Path(__file__).resolve().parents[3] / "scripts" / "eqemu" / "oracle" / "vendor"
sys.path.insert(0, str(_VENDOR))
import eqpcap  # noqa: E402

CLIENT_UPDATE = 0x14CB


def _f32(b, o):
    return struct.unpack("<f", b[o:o + 4])[0]


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    pcap = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None

    rows = []  # (t, cport, spawn, x, y, z)
    for t, dirn, sport, cport, nm, op, payload in eqpcap.decode(pcap):
        if op != CLIENT_UPDATE or len(payload) < 32:
            continue
        spawn = struct.unpack("<H", payload[0:2])[0]
        y = _f32(payload, 4)
        x = _f32(payload, 24)
        z = _f32(payload, 28)
        rows.append((t, cport, spawn, x, y, z))

    if not rows:
        print("no OP_ClientUpdate (0x14cb) payloads decoded", file=sys.stderr)
        sys.exit(1)

    # cluster by (x,y) rounded to 2 units: a standing character clusters tight
    clusters = defaultdict(list)
    for t, cport, spawn, x, y, z in rows:
        key = (round(x / 2.0), round(y / 2.0))
        clusters[key].append((t, cport, spawn, x, y, z))

    print("OP_ClientUpdate payloads: %d in %d clusters" % (len(rows), len(clusters)))
    lines = ["cluster\tx_med\ty_med\tz_med\tz_min\tz_max\tn\tcports\tfirst_utc\tlast_utc"]
    for key in sorted(clusters, key=lambda k: -len(clusters[k])):
        cl = clusters[key]
        xs = [r[3] for r in cl]
        ys = [r[4] for r in cl]
        zs = sorted(r[5] for r in cl)
        cports = sorted({r[1] for r in cl})
        n = len(zs)
        med = zs[n // 2]
        first = datetime.datetime.fromtimestamp(cl[0][0], datetime.timezone.utc).strftime("%H:%M:%S")
        last = datetime.datetime.fromtimestamp(cl[-1][0], datetime.timezone.utc).strftime("%H:%M:%S")
        line = "C%d\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%d\t%s\t%s\t%s" % (
            key[0] * 100000 + key[1],
            xs[n // 2], ys[n // 2], med, zs[0], zs[-1], n,
            ",".join(str(c) for c in cports[:4]), first, last)
        lines.append(line)
        print(line)
    if out:
        with open(out, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        print("wrote", out)


if __name__ == "__main__":
    main()
