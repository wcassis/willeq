#!/usr/bin/env python3
# B1195 probe: map client -> own spawn id (S->C OP_ZoneEntry), and S->C OP_Spawn name table.
import sys, importlib.util, struct
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
rows = m.decode(sys.argv[1])
clients = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'S->C':
        who = clients.get(cp, '?')
        print(f'{who}: S->C OP_ZoneEntry size={len(p)} head={p[:12].hex()}')
    if name == 'OP_Spawn' and d == 'S->C' and len(p) > 40:
        sid = struct.unpack_from('<H', p, 0)[0]
        nm = p[37:p[37:40 + 23]].split(b'\0')[0].decode('latin-1', 'replace')
        if nm and len(nm) > 2:
            print(f'OP_Spawn sid={sid} name={nm!r}')
