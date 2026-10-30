#!/usr/bin/env python3
# B1195 probe: decode OP_SpecialMesg text and OP_Death payload in a window.
import sys, importlib.util, struct
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
rows = m.decode(sys.argv[1])
t0, t1 = float(sys.argv[2]), float(sys.argv[3])
clients = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
for t, d, sp, cp, name, code, p in rows:
    if not (t0 <= t <= t1):
        continue
    who = clients.get(cp, '?')
    if name == 'OP_SpecialMesg':
        # layout: u16 type, u16 param, u16 str1_len, str1, str2...
        try:
            st, par, l1 = struct.unpack_from('<HHH', p, 0)
            s1 = p[6:6 + l1].split(b'\0')[0].decode('latin-1', 'replace')
            print(f'{t - t0:+8.2f} {who:12s} SpecialMesg type={st} param={par} "{s1}"')
        except Exception as e:
            print(f'{t - t0:+8.2f} {who} SpecialMesg (undecoded {e})')
    elif name == 'OP_Death':
        try:
            sid, kind = struct.unpack_from('<HH', p, 0)
            print(f'{t - t0:+8.2f} {who:12s} OP_Death spawnid={sid} kind={kind} raw={p[:16].hex()}')
        except Exception:
            print(f'{t - t0:+8.2f} {who} OP_Death raw={p[:16].hex()}')
    elif name == 'OP_HealthUpdate' or name == 'OP_HPUpdate':
        try:
            sid, hp, mh = struct.unpack_from('<Hii', p, 0)
            print(f'{t - t0:+8.2f} {who:12s} {name} spawnid={sid} hp={hp}/{mh}')
        except Exception:
            pass
