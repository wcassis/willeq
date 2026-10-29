#!/usr/bin/env python3
# B1195 probe: list every C->S OP_CastSpell (with the S->C OP_Action of that spell it produced).
import sys, importlib.util, struct
spec = importlib.util.spec_from_file_location('eqpcap',
    '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
rows = m.decode(sys.argv[1])
clients = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        clients[cp] = p[4:68].split(b'\0')[0].decode('latin-1')
casts = []
acts = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_CastSpell' and d == 'C->S':
        who = clients.get(cp, '?')
        # C->S OP_CastSpell: u16 slot?, ... spell id at offset 18 (Titanium)
        try:
            slot, spell = struct.unpack_from('<HH', p, 0)
        except struct.error:
            slot = spell = None
        casts.append((t, who, slot, spell, p[:24].hex()))
    if name == 'OP_Action' and d == 'S->C' and len(p) >= 31:
        tg, src = struct.unpack_from('<HH', p, 0)
        sp_ = struct.unpack_from('<H', p, 27)[0]
        acts.setdefault(sp_, []).append((t, clients.get(cp, '?'), tg, src))
print(f'{len(casts)} OP_CastSpell:')
for t, who, slot, spell, hx in casts:
    print(f'  {t:.3f} {who:12s} slot={slot} spell_field={spell} head={hx}')
print('OP_Action by spell field:')
for sp_, evs in sorted(acts.items()):
    if sp_ in (93, 253, 38, 54, 41, 458, 656) or len(evs) > 6:
        print(f'  spell={sp_}: ' + ', '.join(f'{t:.3f}({w},tg{tg},src{src})' for t, w, tg, src in evs[:10]))
