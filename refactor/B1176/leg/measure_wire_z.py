#!/usr/bin/env python3
"""B1176: wire z from a lane-5 capture.

A daybreak app datagram (OP_Padding, the B1142/B1057 tcpdump filter) is
[00 00][u16 app_opcode][36-byte payload]. For OP_ClientUpdate (0x14cb) the
payload layout is src/client/movement/movement_update.cpp:751-761 (eqemu
common/patches/titanium_structs.h:1226):

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

usage: measure_wire_z.py <capture.pcap> [out.tsv]
Writes one row per (x,y) position cluster: cluster, x_med, y_med, z_med,
z_min, z_max, n, first_utc, last_utc.
"""
import struct
import sys
import datetime
from collections import defaultdict


def read_datagrams(path):
    with open(path, "rb") as fh:
        gh = fh.read(24)
        magic = struct.unpack("<I", gh[:4])[0]
        if magic == 0xA1B2C3D4:
            endian = "<"
        elif magic == 0xD4C3B2A1:
            endian = ">"
        else:
            raise SystemExit("not a little/big-endian pcap: %r" % struct.pack("<I", 0) == gh[:4])
        while True:
            ph = fh.read(24)
            if len(ph) < 24:
                break
            _, _, incl, _ = struct.unpack(endian + "IIII", ph[:16])
            data = fh.read(incl)
            if len(data) < incl:
                break
            yield data


def udp_payload(datagram):
    # Ethernet (linktype 1) -> IPv4 -> UDP
    if len(datagram) < 14:
        return None
    ethertype = datagram[12:14]
    if ethertype != b"\x08\x00":
        return None
    ip = datagram[14:]
    if ip[0] >> 4 != 4:
        return None
    ihl = (ip[0] & 0x0F) * 4
    if ip[9] != 17 or len(ip) < ihl + 8:
        return None
    return ip[ihl + 8:]


def main():
    pcap = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    rows = []
    for i, dg in enumerate(read_datagrams(pcap)):
        if len(dg) < 40 or dg[0] != 0 or dg[1] != 0:
            continue
        opcode = struct.unpack("<H", dg[2:4])[0]
        if opcode != 0x14CB:
            continue
        payload = dg[2:]
        spawn = struct.unpack("<H", payload[0:2])[0]
        y_pos = struct.unpack("<f", payload[4:8])[0]
        x_pos = struct.unpack("<f", payload[24:28])[0]
        z = struct.unpack("<f", payload[28:32])[0]
        rows.append((i, spawn, x_pos, y_pos, z))
    if not rows:
        print("no OP_ClientUpdate (0x14cb) app datagrams found", file=sys.stderr)
        sys.exit(1)
    # cluster by (x,y) rounded to 2 units: a standing character clusters tight
    clusters = defaultdict(list)
    for i, spawn, x, y, z in rows:
        key = (round(x / 2.0), round(y / 2.0))
        clusters[key].append((i, spawn, x, y, z))
    lines = ["cluster\tx_med\ty_med\tz_med\tz_min\tz_max\tn\tspawn_ids"]
    print("datagrams with ClientUpdate: %d in %d clusters" % (len(rows), len(clusters)))
    for key in sorted(clusters, key=lambda k: -len(clusters[k])):
        cl = clusters[key]
        xs = [r[2] for r in cl]
        ys = [r[3] for r in cl]
        zs = [r[4] for r in cl]
        spawns = sorted({r[1] for r in cl})
        zs_sorted = sorted(zs)
        n = len(zs)
        med = zs_sorted[n // 2]
        line = "C%d\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%d\t%s" % (
            key[0] * 100000 + key[1],
            xs[n // 2], ys[n // 2], med, zs_sorted[0], zs_sorted[-1], n,
            ",".join(str(s) for s in spawns[:4]))
        lines.append(line)
        print(line)
    if out:
        with open(out, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        print("wrote", out)


if __name__ == "__main__":
    main()
