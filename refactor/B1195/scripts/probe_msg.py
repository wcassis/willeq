#!/usr/bin/env python3
# B1195 probe: dump raw SpecialMesg payloads (all printable runs) + who/when in a window.
import sys, importlib.util, re
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
rows = m.decode(sys.argv[1])
t0, t1 = float(sys.argv[2]), float(sys.argv[3])
clients = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
seen = set()
for t, d, sp, cp, name, code, p in rows:
    if not (t0 <= t <= t1) or name != 'OP_SpecialMesg' or d != 'S->C':
        continue
    who = clients.get(cp, '?')
    runs = re.findall(rb'[\x20-\x7e]{4,}', p)
    key = (t, who, tuple(runs))
    if key in seen:
        continue
    seen.add(key)
    txt = b' | '.join(runs).decode('latin-1')
    print(f'{t - t0:+8.2f} {who:12s} {txt[:220]}')
