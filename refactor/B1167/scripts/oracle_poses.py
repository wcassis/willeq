#!/usr/bin/env python3
"""B1167: the caster's and the target's world position and heading in an oracle capture.

Reads (decoded by review/UIGAP-ORACLE/scripts/eqpcap.py):
- C->S OP_ClientUpdate (36 B, Titanium PlayerPositionUpdateClient_Struct: spawn_id u16 @0,
  y float @4, x float @24, z float @28, heading u16 @32 low 12 bits = EQ 512ths x 4, src/client/movement/movement_update.cpp:734);
  x/y are SERVER coordinates; the zone geometry's (willeq's) x = server y, y = server x
  (src/client/entities/entities_component.cpp:156-158).
- S->C OP_NewSpawn (385 B, include/common/packets/spawn.h Spawn_Struct: name @7, the position
  bitfield @94: word0 deltaHeading:10 x:19, word1 y:19 animation:10, word2 z:19 deltaY:13,
  word3 deltaX:13 heading:12; x/y/z signed 19-bit fixed point /8, heading raw/4 = EQ 512ths, the client's
  rule src/client/entities/entities_component.cpp:174-175, raw*360/2048 degrees);
- S->C OP_ClientUpdate (22 B: spawn_id u16 @0, then the same five-word bitfield @2).
Prints one line per packet for the named spawns (or the client's own updates), with its time.

usage: oracle_poses.py <eqpcap.py> <capture.pcap> <name-substring> [t0 t1]
"""
import sys, struct, importlib.util


def s19(v):
    v &= 0x7FFFF
    return v - (1 << 19) if v & (1 << 18) else v


def bitfield(b):
    w = struct.unpack_from('<5I', b, 0)
    x = s19(w[0] >> 10) / 8.0
    y = s19(w[1]) / 8.0
    z = s19(w[2]) / 8.0
    heading = ((w[3] >> 13) & 0xFFF) / 4.0
    return x, y, z, heading


def main():
    spec = importlib.util.spec_from_file_location('eqpcap', sys.argv[1])
    eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
    want = sys.argv[3]
    t0 = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    t1 = float(sys.argv[5]) if len(sys.argv) > 5 else 1e12
    ids = {}
    for t, d, sp, cp, name, code, p in eq.decode(sys.argv[2]):
        if d == 'S->C' and name == 'OP_NewSpawn' and len(p) == 385:
            nm = p[7:71].split(b'\0')[0].decode('latin-1')
            sid = struct.unpack_from('<I', p, 340)[0]
            if want in nm:
                ids[sid] = nm
                x, y, z, h = bitfield(p[94:114])
                if t0 <= t <= t1:
                    print(f'{t:.3f} NewSpawn id={sid} {nm} x={x:.3f} y={y:.3f} z={z:.3f} heading={h:.3f}')
        elif d == 'S->C' and name == 'OP_ClientUpdate' and len(p) == 22:
            sid = struct.unpack_from('<H', p, 0)[0]
            if sid in ids and t0 <= t <= t1:
                x, y, z, h = bitfield(p[2:22])
                print(f'{t:.3f} S->C ClientUpdate id={sid} x={x:.3f} y={y:.3f} z={z:.3f} heading={h:.3f}')
        elif d == 'C->S' and name == 'OP_ClientUpdate' and len(p) == 36 and want == 'SELF':
            sid = struct.unpack_from('<H', p, 0)[0]
            y = struct.unpack_from('<f', p, 4)[0]
            x = struct.unpack_from('<f', p, 24)[0]
            z = struct.unpack_from('<f', p, 28)[0]
            h = (struct.unpack_from('<H', p, 32)[0] & 0xFFF) / 4.0
            if t0 <= t <= t1:
                print(f'{t:.3f} C->S ClientUpdate id={sid} x={x:.3f} y={y:.3f} z={z:.3f} heading={h:.3f}')


if __name__ == '__main__':
    main()
