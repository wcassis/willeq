#!/usr/bin/env python3
"""B1174: print a leg's decoded packets (the B1162 probe_b1162_msgs_local.py with the
vendored decoder of THIS tree). usage: pcap_lines.py <leg_dir> [t0 t1] [OP_A,OP_B,...]
Also prints the per-opcode census of the whole pcap with --census."""
import os, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../scripts/eqemu/oracle/vendor'))
import eqpcap
leg = sys.argv[1]
rows = eqpcap.decode(leg + '/capture.pcap')
if '--census' in sys.argv:
    c = collections.Counter((d, name) for t, d, sp, cp, name, code, p in rows)
    for (d, name), n in sorted(c.items()):
        print(f'{d}\t{name}\t{n}')
    sys.exit(0)
t0, t1 = float(sys.argv[2]), float(sys.argv[3])
names = set(sys.argv[4].split(',')) if len(sys.argv) > 4 else None
for t, d, sp, cp, name, code, p in rows:
    if t0 <= t <= t1 and (names is None or name in names):
        txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in p)
        print(f'{t:.3f}\t{d}\t{cp}\t{name}\t{len(p)}\t{p[:32].hex()}\t{txt[:90]}')
