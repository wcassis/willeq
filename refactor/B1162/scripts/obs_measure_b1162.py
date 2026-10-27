#!/usr/bin/env python3
"""B1162 (fork of refactor/B1143/scripts/obs_measure_b1143.py; only the eqpcap path changed, to this tree's vendor copy): measure casts seen by the OBSERVER (the second official client) on the B1130
instruments, per cast -- the B1134 obs_measure.py fork with a THIRD anchor: the NPC target.

The B1134 rig only ever landed on the observer (player) or the caster; B1143's item 2
needs the landing on the #spawned TrollF (NPC), which in this rig stands on top of the
caster (both at the plan's pose spot). So the LANDING anchor is chosen PER CAST from the
OP_Action's target id: --npc-id <spawn id> -> the NPC's feet/px (geom key npc_feet/npc_px),
anything else (the observer's own spawn id) -> observer_feet/observer_px.

usage: obs_measure_b1143.py <capture_dir> <out_root> <geom.json> <spell_id>...
         [--observer Oraclewatch] [--npc-id 262] [--keep-png]
geom.json (full-frame px of the observer's video, read off the leg's own settle still):
  {"crop": "x,y,w,h", "caster_feet": "x,y", "caster_px": N,
   "npc_feet": "x,y", "npc_px": N, "observer_feet": "x,y", "observer_px": N,
   "exclude": ["x,y,w,h", ...], "halfw": 0.7}
"""
import sys, os, json, glob, struct, subprocess, importlib.util
cap, root, gj = sys.argv[1:4]
args = sys.argv[4:]
obs = args[args.index('--observer') + 1] if '--observer' in args else 'Oraclewatch'
npc_id = int(args[args.index('--npc-id') + 1]) if '--npc-id' in args else None
keep = '--keep-png' in args
spells = []
for i, a in enumerate(args):
    if a == '--npc-id':
        continue
    if a.isdigit() and (i == 0 or args[i - 1] not in ('--npc-id',)):
        spells.append(int(a))
G = json.load(open(gj))
S22 = '/mnt/projects/fable/review/B1122/scripts'
S30 = '/mnt/projects/fable/review/B1130/scripts'
spec = importlib.util.spec_from_file_location('eqpcap', '' + os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../scripts/eqemu/oracle/vendor/eqpcap.py') + '')
eq = importlib.util.module_from_spec(spec); spec.loader.exec_module(eq)
rows = eq.decode(cap + '/capture.pcap')
ports = {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        ports.setdefault(p[4:68].split(b'\0')[0].decode('latin-1'), set()).add(cp)
if obs not in ports:
    sys.exit(f'no C->S OP_ZoneEntry for {obs} in {cap} (ports: {ports})')
OP = ports[obs]
ev, seen = [], {}
for t, d, sp, cp, name, code, p in rows:
    if d != 'S->C' or cp not in OP or name not in ('OP_BeginCast', 'OP_Action', 'OP_DeleteSpawn', 'OP_InterruptCast'):
        continue
    key = (name, bytes(p))
    if key in seen and t - seen[key] < 0.1:
        continue
    seen[key] = t
    if name == 'OP_BeginCast' and len(p) >= 8:
        c, s, ct = struct.unpack_from('<HHI', p, 0); ev.append((t, 'BC', c, s, ct))
    elif name == 'OP_Action' and len(p) >= 31:
        tg, src = struct.unpack_from('<HH', p, 0); s = struct.unpack_from('<H', p, 27)[0]
        ev.append((t, 'ACT', src, s, (tg, p[30])))
    elif name == 'OP_DeleteSpawn' and len(p) >= 2:
        ev.append((t, 'DEL', struct.unpack_from('<H', p, 0)[0], 0, None))
    elif name == 'OP_InterruptCast':
        ev.append((t, 'INT', 0, 0, None))
off = float([l.split('\t') for l in open(cap + '/video.calibration.tsv')][1][3])
casts, last_act = [], {}
for i, (t, k, c, s, x) in enumerate(ev):
    if k != 'ACT' or s not in spells or x[1] != 0:
        continue
    bc = [e for e in ev[:i] if e[1] == 'BC' and e[3] == s and e[0] > last_act.get(s, 0)]
    last_act[s] = t
    if bc:
        c = bc[-1][2]
    dele = next((e[0] for e in ev[i:] if e[1] == 'DEL' and e[2] == c), None)
    casts.append(dict(spell=s, source=c, target=x[0], tbc=bc[-1][0] if bc else t, cast_ms=bc[-1][4] if bc else 0,
                      instant=not bc, tact=t, delete=dele))
os.makedirs(root, exist_ok=True)
PY = sys.executable
EXC = sum([['--exclude', e] for e in G.get('exclude', [])], [])
hw = str(G.get('halfw', 0.7))
n = {}
for c in casts:
    n[c['spell']] = n.get(c['spell'], 0) + 1
    is_npc = npc_id is not None and c['target'] == npc_id
    tfeet, tpx = (G['npc_feet'], G['npc_px']) if is_npc else (G['observer_feet'], G['observer_px'])
    d = f"{root}/{os.path.basename(cap.rstrip('/'))}_{c['spell']}_{n[c['spell']]}_{'npc' if is_npc else 'player'}"
    nxt = next((x['tbc'] for x in casts if x['tbc'] > c['tact'] + 0.05), None)
    tend = min(c['tact'] + 10.0, (nxt - 1.3) if nxt else c['tact'] + 10.0)
    head = (f"## spell {c['spell']} rep {n[c['spell']]} source {c['source']} target {c['target']} "
            f"({('NPC' if is_npc else 'player')}) "
            f"BC {c['tbc']:.3f} (cast_ms {c['cast_ms']}{', INSTANT: no OP_BeginCast' if c['instant'] else ''}) "
            f"Act {c['tact']:.3f} cast {c['tact'] - c['tbc']:.3f}s; caster DeleteSpawn "
            + (f"Act{c['delete'] - c['tact']:+.3f}" if c['delete'] else 'none') + f"; window to Act{tend - c['tact']:+.2f}")
    log = [head]
    if not os.path.exists(d + '/frames.tsv'):
        subprocess.run([PY, S22 + '/fx_measure.py', cap + '/video.mp4', str(off), str(c['tbc']), str(c['tact']),
                        str(tend), G['crop'], d, '--label', f"b1162_{c['spell']}_{n[c['spell']]}_{'npc' if is_npc else 'player'}"], check=True,
                       capture_output=True)
    run = lambda *a: log.append(subprocess.run([str(x) for x in a], capture_output=True, text=True).stdout.strip())
    run(PY, S22 + '/fx_timing.py', d, tpx)
    run(PY, S22 + '/landing_onset_abs.py', d, tpx)
    run(PY, S30 + '/anchor.py', d, G['caster_feet'], G['caster_px'], 'SOURCE', '--halfw', hw, *EXC)
    run(PY, S30 + '/anchor.py', d, tfeet, tpx, 'LANDING', '--halfw', hw, *EXC)
    open(d + '.txt', 'w').write('\n'.join(log) + '\n')
    c.update(dir=d, rep=n[c['spell']], tend=tend, target_kind='npc' if is_npc else 'player')
    print(head, flush=True)
    for l in '\n'.join(log).splitlines():
        if l.startswith(('SOURCE', 'LANDING', 'ONSET0.3', 'PEAK', 'RUN', 'CAST', 'POST-ACT', 'RISE', 'ABS')):
            print('  ' + l)
    if not keep:
        for p in glob.glob(d + '/f*.png'):
            os.remove(p)
json.dump(casts, open(f"{root}/{os.path.basename(cap.rstrip('/'))}_casts.json", 'w'), indent=1)
