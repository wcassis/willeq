#!/usr/bin/env python3
# B1195 probe: decode a leg pcap; report per-client (by C->S OP_ZoneEntry name) the death/combat
# events: OP_Death, OP_HealthUpdate hp=0, OP_Respawn, OP_Spawn (who spawned), and the first
# combat damage taken by each named client character.
import sys, importlib.util, struct
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
rows = eq.decode(sys.argv[1])
clients = {}   # clientport -> name
spawns = {}    # spawnid -> name (S->C OP_Spawn / OP_ZoneEntry)
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
    if name in ('OP_Spawn', 'OP_ZoneEntry') and d == 'S->C':
        try:
            sid = struct.unpack_from('<H', p, 0)[0] if len(p) >= 2 else None
        except Exception:
            sid = None
        if name == 'OP_Spawn' and sid and sid not in spawns:
            # OP_Spawn layout: spawnid, then type/level..., name at offset 37 (Titanium)
            try:
                spawns[sid] = p[37:p[37:60].index(b'\0') + 37].decode('latin-1')
            except Exception:
                pass
print('clients (clientport->name):', clients)
print('spawns seen (first 25):', dict(list(spawns.items())[:25]))
print()
for t, d, sp, cp, name, code, p in rows:
    if name not in ('OP_Death', 'OP_Respawn', 'OP_Awake', 'OP_Buff', 'OP_Action') or d != 'S->C':
        continue
    who = clients.get(cp, '?')
    if name == 'OP_Death':
        print(f'{t} {who} OP_Death size={len(p)}')
    elif name in ('OP_Respawn', 'OP_Awake'):
        print(f'{t} {who} {name}')
