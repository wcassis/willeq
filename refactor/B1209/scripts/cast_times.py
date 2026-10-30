#!/usr/bin/env python3
"""B1209 (#528): cast times + buff-packet dump for the observer's stream.

usage: cast_times.py <capture_dir> <spell_id>... [--observer Oraclewatch]

Reads <capture_dir>/capture.pcap with the repo's vendor eqpcap and reports,
for the OBSERVER client only (the C->S OP_ZoneEntry whose name is --observer):

  cast_times.tsv   spell  t_bc  t_act   (the last OP_BeginCast of the spell
      before its OP_Action effect_flag 0; the window for fx_measure.py)
  buff_dump_<spell>.tsv   every S->C packet the observer got in [t_act,
      t_act+8]:  t  name  code  hex(first 48 bytes). This is the evidence for
      the words the server sent with the aura buff (the client's per-frame
      buff-tick gate reads buff +0x22, eqgame.exe.c:49805).
  video.calibration.tsv is copied here unchanged (the offset fx_measure.py
      takes as its pts_offset_epoch argument).
"""
import sys, os, json, struct, importlib.util

cap = sys.argv[1]
args = sys.argv[2:]
obs = args[args.index('--observer') + 1] if '--observer' in args else 'Oraclewatch'
spells = [int(a) for a in args if a.isdigit()]

spec = importlib.util.spec_from_file_location(
    'eqpcap', '/home/user/src/willeq-wt/kb-b1209-aura-ground/scripts/eqemu/oracle/vendor/eqpcap.py')
eq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eq)

rows = eq.decode(cap + '/capture.pcap')
ports = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        ports.setdefault(p[4:68].split(b'\0')[0].decode('latin-1'), set()).add(cp)
if obs not in ports:
    sys.exit('no C->S OP_ZoneEntry for %s (ports: %s)' % (obs, ports))
OP = ports[obs]

ev = []
for t, d, sp, cp, name, code, p in rows:
    if d != 'S->C' or cp not in OP:
        continue
    ev.append((t, name, code, p))

evs = []
for t, name, code, p in ev:
    if name == 'OP_BeginCast' and len(p) >= 8:
        c, s, ct = struct.unpack_from('<HHI', p, 0)
        evs.append((t, 'BC', s, None))
    elif name == 'OP_Action' and len(p) >= 31:
        tg, src = struct.unpack_from('<HH', p, 0)
        s = struct.unpack_from('<H', p, 27)[0]
        flag = p[30]
        evs.append((t, 'ACT' if flag == 0 else 'AUX', s, (tg, flag)))

acts = {}
for i, (t, k, s, x) in enumerate(evs):
    if k == 'ACT' and s in spells:
        bc = [e for e in evs[:i] if e[1] == 'BC' and e[2] == s]
        acts[s] = (t, bc[-1][0] if bc else None)

with open(cap + '/cast_times.tsv', 'w') as f:
    f.write('spell\tt_bc\tt_act\tbc_present\n')
    for s in sorted(spells):
        if s in acts:
            t_act, t_bc = acts[s]
            f.write('%d\t%s\t%.3f\t%s\n' % (s, ('%.3f' % t_bc) if t_bc is not None else 'absent', t_act, t_bc is not None))
        else:
            f.write('%d\t-\tno-OP_Action-seen-by-observer\t-\n' % s)

for s in sorted(spells):
    if s not in acts:
        continue
    t_act = acts[s][0]
    with open(cap + '/buff_dump_%d.tsv' % s, 'w') as f:
        f.write('t\tname\tcode\thex48\n')
        for t, name, code, p in ev:
            if t_act <= t <= t_act + 8:
                f.write('%.3f\t%s\t%s\t%s\n' % (t, name, code, p[:48].hex()))

with open(cap + '/video.calibration.tsv') as f:
    cal = f.read()
with open(cap + '/video.calibration.tsv.b1209copy', 'w') as f:
    f.write(cal)
print(cal)
print('acts:', acts)
