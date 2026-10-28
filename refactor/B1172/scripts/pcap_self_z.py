#!/usr/bin/env python3
"""B1172 fix leg: the official (P1999) client's transmitted z for its OWN spawn.
C->S OP_ClientUpdate in this client is 36 bytes: spawn id u16 @0, one horizontal
coordinate f32 @4, the other @24, z f32 @28 (read off the payload: at the T311
qeynos2 spot (-74, 428) the two horizontal floats are 428.0 and -74.0).
Prints the count of every (h@4, h@24, z@28) the given spawn sent.
usage: pcap_self_z.py <eqpcap.py> <capture.pcap> <spawn_id>"""
import collections
import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location('eqpcap', sys.argv[1])
eq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eq)
want = int(sys.argv[3])
seen = collections.Counter()
for t, d, sp, cp, name, code, p in eq.decode(sys.argv[2]):
    if d != 'C->S' or name != 'OP_ClientUpdate' or len(p) != 36:
        continue
    if struct.unpack_from('<H', p, 0)[0] != want:
        continue
    a, b, z = (struct.unpack_from('<f', p, o)[0] for o in (4, 24, 28))
    seen[(round(a, 3), round(b, 3), round(z, 4))] += 1
for (a, b, z), n in seen.most_common(6):
    print(f'spawn {want} h@4={a:.3f} h@24={b:.3f} z={z:.4f} packets={n}')
