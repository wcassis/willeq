#!/usr/bin/env python3
# B1195 probe: decode S->C OP_Spawn payloads; print spawnid/type/race/level for named spawns.
import sys, importlib.util, struct
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
rows = m.decode(sys.argv[1])
want = sys.argv[2] if len(sys.argv) > 2 else ''
seen = {}
for t, d, sp, cp, name, code, p in rows:
    if d != 'S->C' or name not in ('OP_Spawn', 'OP_ZoneEntry'):
        continue
    if len(p) < 80:
        continue
    try:
        sid, etype, race, level = struct.unpack_from('<HHHH', p, 0)
    except struct.error:
        continue
    nm = b''
    for off in range(20, min(len(p) - 2, 120)):
        if p[off] < 0x20:
            continue
        cand = p[off:off + 24].split(b'\0')[0].decode('latin-1', 'replace')
        if cand.isalpha() and len(cand) > 3:
            nm = cand
            break
    key = (sid, nm)
    if key in seen:
        continue
    seen[key] = 1
    if want and want.lower() not in nm.lower():
        continue
    print(f'{name} sid={sid} type={etype} race={race} level={level} name={nm!r} size={len(p)}')
