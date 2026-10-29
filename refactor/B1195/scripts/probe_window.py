#!/usr/bin/env python3
# B1195 probe: dump all non-ClientUpdate S->C/C->S opcodes in a time window, per client.
import sys, importlib.util
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
rows = eq.decode(sys.argv[1])
t0, t1 = float(sys.argv[2]), float(sys.argv[3])
clients = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
n = 0
for t, d, sp, cp, name, code, p in rows:
    if not (t0 <= t <= t1):
        continue
    if name in ('OP_ClientUpdate', 'OP_KeepAlive', 'OP_WorldUpdate', 'OP_ServerList',
                'OP_Spawn', 'OP_UnSpawn', 'OP_MoveNPC', 'OP_NPCAnimation', 'OP_TimeOfDay',
                'OP_AmbientChange', 'OP_WeatherChange', 'OP_Sound', 'OP_TravelToNPC',
                'OP_ClientLookUpdate', 'OP_ClientPositionUpdate', 'OP_NPCPositionUpdate',
                'OP_NPCLookUpdate', 'OP_NPCAppearanceUpdate', 'OP_ZoneEntry', 'OP_Say',
                'OP_Yell', 'OP_Shout', 'OP_Tell'):
        continue
    who = clients.get(cp, '?')
    n += 1
    print(f'{t - t0:+8.2f} {d:5s} {who:12s} {name} size={len(p)}')
    if n > 120:
        print('...(truncated)')
        break
