#!/usr/bin/env python3
"""B1159 (#478): the per-stream opcode census of the B1159 leg capture -- the card's
HARD RULE, checked from the pcap FIRST (before any frame measurement):

    0 OP_Death for char 103 (Oraclewatch) inside every cast window, and 6 OP_Action.

usage: pcap_census.py <capture_dir> [spell spell ...]
  (capture_dir has capture.pcap + video.calibration.tsv;
   default spells 38 54 93)

The observer's stream is selected by the client port of its C->S OP_ZoneEntry
(the B1134 rule, scripts/obs_measure.py); the caster's by its own. OP_Action on
the observer stream names the caster (source) and the target; the observer's
own spawn id is taken from the caster-stream OP_Action target (the casts land
ON the observer), the caster's from the caster-stream OP_BeginCast caster field.
OP_Death payload[0:2] = the victim's spawn id; an unknown victim is labelled
by its OP_Spawn name where one is visible in the capture, else 'unknown'.
Video-relative times use calibrated_t0 (the video's frame-0 epoch) from
video.calibration.tsv, the same file obs_measure_b1143.py reads.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, '/home/user/src/willeq-wt/kb-b1159-missile-recapture/scripts/eqemu/oracle/vendor')
import eqpcap  # noqa: E402

cap = Path(sys.argv[1])
spells = [int(a) for a in sys.argv[2:]] or [38, 54, 93]
rows = eqpcap.decode(str(cap / 'capture.pcap'))

ports, names = {}, {}
for t, d, sp, cp, name, code, p in rows:
    if name == 'OP_ZoneEntry' and d == 'C->S' and len(p) >= 68:
        nm = p[4:68].split(b'\0')[0].decode('latin-1')
        ports.setdefault(nm, set()).add(cp)
        names.setdefault(cp, nm)
print(f'clients: {sorted(ports)}')
if 'Oraclewatch' not in ports or 'Testcaster' not in ports:
    sys.exit(f'expected Testcaster + Oraclewatch streams, got {sorted(ports)}')
OP, CP = ports['Oraclewatch'], ports['Testcaster']

off = float([l.split('\t') for l in open(cap / 'video.calibration.tsv')][1][3])
print(f'calibrated_t0 (video frame 0) = {off}')

obs_id = caster_id = None


def who(sid):
    if sid == obs_id:
        return 'OBSERVER (char 103)'
    if sid == caster_id:
        return 'CASTER (char 67)'
    return f'{spawns.get(sid, "unknown")}'


bc_obs, act_obs, death_all, spawns, dmg = [], [], [], {}, []
for t, d, sp, cp, name, code, p in rows:
    if d != 'S->C':
        continue
    if name == 'OP_Spawn' and len(p) >= 114:
        sid = struct.unpack_from('<H', p, 0)[0]
        nm = p[112:176].split(b'\0')[0].decode('latin-1', 'replace')
        if nm:
            spawns[sid] = nm
    if name == 'OP_FormattedMessage' and len(p) >= 2:
        sid = struct.unpack_from('<H', p, 0)[0]
        if sid == 9073:
            dmg.append((t, eqpcap.fields(name, p)))
    if cp not in (OP, CP):
        continue
    if name == 'OP_BeginCast' and len(p) >= 8:
        c, s, ct = struct.unpack_from('<HHI', p, 0)
        if cp == OP:
            bc_obs.append((t, c, s, ct))
    elif name == 'OP_Action' and len(p) >= 31:
        tg, src = struct.unpack_from('<HH', p, 0)
        s = struct.unpack_from('<H', p, 27)[0]
        if cp == OP:
            act_obs.append((t, tg, src, s))
    elif name == 'OP_Death' and len(p) >= 2:
        death_all.append((t, struct.unpack_from('<H', p, 0)[0], cp))


obs_id = next((a[1] for a in act_obs if a[3] in spells), None)
caster_id = next((b[1] for b in bc_obs), None)
print(f'observer spawn id = {obs_id} (from the cast-target OP_Action); caster spawn id = {caster_id}')

print('\nOP_BeginCast (observer stream):')
for t, c, s, ct in bc_obs:
    print(f'  {t:.3f} (video {t - off:+.3f}) caster {c} spell {s} cast_ms {ct}')
print(f'OP_Action (observer stream): {len(act_obs)} total')
for t, tg, src, s in act_obs:
    mark = ' <== measured cast' if s in spells and tg == obs_id else ''
    print(f'  {t:.3f} (video {t - off:+.3f}) target {tg} ({who(tg)}) source {src} spell {s}{mark}')
print(f'OP_Death (whole leg, both streams): {len(death_all)}')
for t, v, stream in death_all:
    print(f'  {t:.3f} (video {t - off:+.3f}) victim {v} = {who(v)} [stream {names.get(stream)}]')
print(f'OP_FormattedMessage 9073 (damage lines): {len(dmg)}')
for t, f in dmg:
    print(f'  {t:.3f} (video {t - off:+.3f}) {f}')

# ---- the hard rule -------------------------------------------------------
measured = [a for a in act_obs if a[3] in spells and a[1] == obs_id]
window = 5.0  # M1's death tick was the OP_Action tick itself; 5 s after covers any death
viol = []
for t, tg, src, s in measured:
    lo, hi = t - 2.6, t + window
    d_in = [d for d in death_all if lo <= d[0] <= hi]
    d_obs = [d for d in d_in if d[1] == obs_id]
    if d_obs:
        viol.append((t, d_obs))
    print(f'window video [{lo - off:+.3f}..{hi - off:+.3f}]: spell {s} Act video {t - off:+.3f}: '
          f'{len(d_in)} OP_Death in window, {len(d_obs)} of them char 103')
ok = len(measured) == 6 and not viol and obs_id is not None
print(f'\nHARD RULE: {len(measured)}/6 measured OP_Action, '
      f'{len(viol)} cast window(s) with a char-103 OP_Death -> {"PASS" if ok else "FAIL"}')
sys.exit(0 if ok else 1)
