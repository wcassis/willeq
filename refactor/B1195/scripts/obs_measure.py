#!/usr/bin/env python3
"""B1134: measure casts seen by the OBSERVER (the second official client, :99 = video.mp4) on the B1130
instruments, per cast.
usage: obs_measure.py <capture_dir> <out_root> <geom.json> <spell_id>... [--observer Oraclewatch]
         [--target caster|observer] [--keep-png]
geom.json (full-frame px of the observer's video, read off the leg's own settle still):
  {"crop": "x,y,w,h", "caster_feet": "x,y", "caster_px": N, "observer_feet": "x,y", "observer_px": N,
   "exclude": ["x,y,w,h", ...], "halfw": 0.7}
Packets: ONLY the observer's own stream (the client port whose C->S OP_ZoneEntry names --observer): what
the observing client was told and when. A cast = an OP_Action (effect_flag 0) of the spell; its
OP_BeginCast = the last BeginCast of that spell after the previous Action of it (its caster = the source), else the cast
is INSTANT (a /disc: tb = ta, every Source time is then after OP_Action). Also reported per cast: the
caster's OP_DeleteSpawn / OP_ZoneChange-side vanish as the observer saw it.
Per cast: B1122 fx_measure.py (BC-1.2 .. Act+10), fx_timing.py + landing_onset_abs.py on the caster height,
B1130 anchor.py SOURCE on the caster (|dx| <= halfw) and LANDING on --target (default caster)."""
import sys, os, json, glob, struct, subprocess, importlib.util
cap, root, gj = sys.argv[1:4]
args = sys.argv[4:]
obs = args[args.index('--observer') + 1] if '--observer' in args else 'Oraclewatch'
tgt = args[args.index('--target') + 1] if '--target' in args else 'caster'
keep = '--keep-png' in args
spells = [int(a) for a in args if a.isdigit()]
G = json.load(open(gj))
S22 = '/mnt/projects/fable/review/B1122/scripts'; S30 = '/mnt/projects/fable/review/B1130/scripts'
spec = importlib.util.spec_from_file_location('eqpcap', '/home/user/src/willeq-wt/kb-b1195-oracle-lanes/scripts/eqemu/oracle/vendor/eqpcap.py')
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
    # by spell, not by source: an OP_Action of a cast on ANOTHER player carries source = target (P2: 93 by
    # 259 on 260 arrives as target=260 source=260), so the caster is the BeginCast's.
    bc = [e for e in ev[:i] if e[1] == 'BC' and e[3] == s and e[0] > last_act.get(s, 0)]
    last_act[s] = t
    if bc:
        c = bc[-1][2]
    dele = next((e[0] for e in ev[i:] if e[1] == 'DEL' and e[2] == c), None)
    casts.append(dict(spell=s, source=c, target=x[0], tbc=bc[-1][0] if bc else t, cast_ms=bc[-1][4] if bc else 0,
                      instant=not bc, tact=t, delete=dele))
os.makedirs(root, exist_ok=True)
EXC = sum([['--exclude', e] for e in G.get('exclude', [])], [])
hw = str(G.get('halfw', 0.7))
tfeet, tpx = (G['caster_feet'], G['caster_px']) if tgt == 'caster' else (G['observer_feet'], G['observer_px'])
n = {}
for c in casts:
    n[c['spell']] = n.get(c['spell'], 0) + 1
    d = f"{root}/{os.path.basename(cap.rstrip('/'))}_{c['spell']}_{n[c['spell']]}"
    nxt = next((x['tbc'] for x in casts if x['tbc'] > c['tact'] + 0.05), None)
    tend = min(c['tact'] + 10.0, (nxt - 1.3) if nxt else c['tact'] + 10.0)
    head = (f"## spell {c['spell']} rep {n[c['spell']]} source {c['source']} target {c['target']} "
            f"BC {c['tbc']:.3f} (cast_ms {c['cast_ms']}{', INSTANT: no OP_BeginCast' if c['instant'] else ''}) "
            f"Act {c['tact']:.3f} cast {c['tact'] - c['tbc']:.3f}s; caster DeleteSpawn "
            + (f"Act{c['delete'] - c['tact']:+.3f}" if c['delete'] else 'none') + f"; window to Act{tend - c['tact']:+.2f}")
    log = [head]
    if not os.path.exists(d + '/frames.tsv'):
        subprocess.run(['python3', S22 + '/fx_measure.py', cap + '/video.mp4', str(off), str(c['tbc']), str(c['tact']),
                        str(tend), G['crop'], d, '--label', f"b1134_{c['spell']}_{n[c['spell']]}"], check=True,
                       capture_output=True)
    run = lambda *a: log.append(subprocess.run([str(x) for x in a], capture_output=True, text=True).stdout.strip())
    run('python3', S22 + '/fx_timing.py', d, tpx)
    run('python3', S22 + '/landing_onset_abs.py', d, tpx)
    run('python3', S30 + '/anchor.py', d, G['caster_feet'], G['caster_px'], 'SOURCE', '--halfw', hw, *EXC)
    run('python3', S30 + '/anchor.py', d, tfeet, tpx, 'LANDING', '--halfw', hw, *EXC)
    open(d + '.txt', 'w').write('\n'.join(log) + '\n')
    c.update(dir=d, rep=n[c['spell']], tend=tend)
    print(head, flush=True)
    for l in '\n'.join(log).splitlines():
        if l.startswith(('SOURCE', 'LANDING', 'ONSET0.3', 'PEAK', 'RUN', 'CAST', 'POST-ACT', 'RISE', 'ABS')):
            print('  ' + l)
    if not keep:
        for p in glob.glob(d + '/f*.png'):
            os.remove(p)
json.dump(casts, open(f"{root}/{os.path.basename(cap.rstrip('/'))}_casts.json", 'w'), indent=1)
