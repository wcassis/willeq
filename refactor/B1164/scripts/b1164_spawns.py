#!/usr/bin/env python3
"""B1164: print race / gender / size / level of the named spawns in an oracle capture (OP_NewSpawn /
OP_ZoneSpawns / OP_ZoneEntry Spawn_Struct, offsets of include/common/packets/spawn.h: size +75, race +284).
usage: b1164_spawns.py <capture.pcap> <name substring> ..."""
import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('eqpcap', '/mnt/projects/fable/review/B1118/rig/scripts/eqemu/oracle/vendor/eqpcap.py')
eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
seen=set()
for t, d, sp, cp, name, code, p in eq.decode(sys.argv[1]):
    if name not in ('OP_NewSpawn','OP_ZoneSpawns','OP_ZoneEntry'): continue
    for off in range(0, len(p)-384, 385):
        s=p[off:off+385]
        nm=s[7:71].split(b'\0')[0].decode('latin1')
        if not any(k in nm for k in sys.argv[2:]): continue
        key=(name,nm,bytes(s))
        if key in seen: continue
        seen.add(key)
        size=struct.unpack_from('<f',s,75)[0]; race=struct.unpack_from('<I',s,284)[0]
        print(f'{t:.3f} {d} {name} {nm} race={race} gender={s[334]} size={size:.3f} level={s[151]} npc={s[83]} br={struct.unpack_from("<f",s,344)[0]:.3f} id={struct.unpack_from("<I",s,340)[0]}')
