#!/usr/bin/env python3
"""B1166: every Spawn_Struct (385 B, include/common/packets/spawn.h: name @7, size
float @75, race u32 @284, gender u8 @334) in an oracle capture's S->C OP_NewSpawn /
OP_ZoneSpawns / OP_ZoneEntry, printed name/race/gender/size.
usage: r51_spawn_sizes.py <eqpcap.py> <capture.pcap>"""
import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('eqpcap', sys.argv[1])
eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
seen = set()
for t, d, sp, cp, name, code, p in eq.decode(sys.argv[2]):
    if d != 'S->C' or name not in ('OP_NewSpawn', 'OP_ZoneSpawns', 'OP_ZoneEntry'): continue
    for off in range(0, len(p) - 384, 385):
        s = p[off:off + 385]
        nm = s[7:71].split(b'\0')[0].decode('latin-1')
        size = struct.unpack_from('<f', s, 75)[0]
        race = struct.unpack_from('<I', s, 284)[0]; gender = s[334]
        key = (nm, race, gender, round(size, 3), name)
        if key in seen: continue
        seen.add(key)
        print(f'{t:.3f} port {cp} {name} name={nm!r} race={race} gender={gender} size={size:.3f}')
