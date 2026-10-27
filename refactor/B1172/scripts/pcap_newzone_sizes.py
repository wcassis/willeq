#!/usr/bin/env python3
"""B1172: from an oracle capture print (1) every S->C OP_NewZone's dword at NewZone_Struct
offset 592 (0x250; the client copies the 700-byte struct to 0x9239e0, so this is the
global 0x923c30 that FUN_0047a0e9 tests) and (2) every Spawn_Struct (OP_ZoneEntry /
OP_NewSpawn / OP_ZoneSpawns: name @7, size float @75, race u32 @284, gender u8 @334)
whose name matches one of the given prefixes, and every OP_SpawnAppearance AT_Size (29).
usage: pcap_newzone_sizes.py <eqpcap.py> <capture.pcap> [name-prefix ...]"""
import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location('eqpcap', sys.argv[1])
eq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eq)
prefixes = sys.argv[3:]
for t, d, sp, cp, name, code, p in eq.decode(sys.argv[2]):
    if d != 'S->C':
        continue
    if name == 'OP_NewZone':
        zs = p[64:96].split(b'\0')[0].decode('latin-1')
        v = struct.unpack_from('<I', p, 592)[0] if len(p) >= 596 else None
        print(f'{t:.3f} OP_NewZone len={len(p)} zone={zs!r} dword@592={v}')
    elif name in ('OP_NewSpawn', 'OP_ZoneSpawns', 'OP_ZoneEntry'):
        for off in range(0, len(p) - 384, 385):
            s = p[off:off + 385]
            nm = s[7:71].split(b'\0')[0].decode('latin-1')
            if prefixes and not any(nm.startswith(x) for x in prefixes):
                continue
            size = struct.unpack_from('<f', s, 75)[0]
            race = struct.unpack_from('<I', s, 284)[0]
            sid = struct.unpack_from('<I', s, 340)[0] if len(s) >= 344 else -1
            print(f'{t:.3f} {name} name={nm!r} spawn_id={sid} race={race} gender={s[334]} size={size:.3f}')
    elif name == 'OP_SpawnAppearance' and len(p) >= 8:
        sid, typ, par = struct.unpack_from('<HHI', p, 0)
        if typ == 29:
            print(f'{t:.3f} OP_SpawnAppearance spawn={sid} AT_Size param={par}')
