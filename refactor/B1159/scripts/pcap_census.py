#!/usr/bin/env python3
"""B1159 (#478): the per-stream opcode census of the B1159 leg capture -- the card's
HARD RULE, checked from the pcap FIRST (before any frame measurement):

    0 OP_Death for char 103 (Oraclewatch) inside every cast window, and 6 OP_Action.

usage: pcap_census.py <capture_dir> [spell spell ...]
  (capture_dir has capture.pcap + video.calibration.tsv; default spells 38 54 93)

The observer's stream is selected by the client port of its C->S OP_ZoneEntry
(the B1134 rule, scripts/obs_measure.py); the caster's by its own. The
observer's spawn id is taken from the OP_Action target of the measured casts
(the casts land ON the observer), the caster's from the OP_BeginCast caster
field on the caster stream.
OP_Death payload[0:2] = the victim's spawn id (little-endian u16, the same
offset OP_DeleteSpawn uses, which the B1143 measure matches by id). A victim
that is neither of the two leg characters is a zone wanderer: it is attributed
by its id and by the preceding OP_Actions -- an OP_Death counts against the
leg only if its victim is the observer.
OP_FormattedMessage string_id sits at payload offset 4 (u32,
scripts/eqemu/oracle/vendor/eqpcap.py fields()), 9073 = the "A hits B for N"
damage line. Video-relative times use calibrated_t0 (the video's frame-0
epoch) from video.calibration.tsv, the same file obs_measure_b1143.py reads.
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
OP = next(iter(ports['Oraclewatch']))
CP = next(iter(ports['Testcaster']))

off = float([l.split('\t') for l in open(cap / 'video.calibration.tsv')][1][3])
print(f'calibrated_t0 (video frame 0) = {off}')

bc_all, act_all, death_all, dmg, seen = [], [], [], [], {}
for t, d, sp, cp, name, code, p in rows:
    if d != 'S->C':
        continue
    if name == 'OP_FormattedMessage' and len(p) >= 8:
        if struct.unpack_from('<I', p, 4)[0] == 9073:
            key = (name, cp, bytes(p))
            if key not in seen or t - seen[key] >= 0.1:
                dmg.append((t, eqpcap.fields(name, p)))
            seen[key] = t
    if cp not in (OP, CP):
        continue
    if name == 'OP_Action' and len(p) >= 31 and p[30] != 0:
        continue  # the 0x04 action-state twin of each landing (obs_measure's rule)
    key = (name, cp, bytes(p))
    if key in seen and t - seen[key] < 0.1:
        continue  # the capture point sees each datagram twice (obs_measure's dedup)
    seen[key] = t
    if name == 'OP_BeginCast' and len(p) >= 8:
        bc_all.append((t, cp, *struct.unpack_from('<HHI', p, 0)))
    elif name == 'OP_Action' and len(p) >= 31:
        tg, src = struct.unpack_from('<HH', p, 0)
        s = struct.unpack_from('<H', p, 27)[0]
        act_all.append((t, cp, tg, src, s))
    elif name == 'OP_Death' and len(p) >= 2:
        death_all.append((t, struct.unpack_from('<H', p, 0)[0], cp))

obs_id = next((a[2] for a in act_all if a[4] in spells and a[1] == OP), None)
caster_id = next((b[3] for b in bc_all if b[1] == CP), None)
print(f'observer (char 103) spawn id = {obs_id} (OP_Action target of the measured casts)')
print(f'caster (char 67) spawn id = {caster_id} (OP_BeginCast caster field)')


def who(sid):
    if sid == obs_id:
        return 'OBSERVER char 103'
    if sid == caster_id:
        return 'CASTER char 67'
    return 'zone wanderer'


print('\nOP_BeginCast (observer stream):')
for t, cp, c, s, ct in bc_all:
    if cp == OP:
        print(f'  {t:.3f} (video {t - off:+.3f}) caster {c} spell {s} cast_ms {ct}')
measured = [a for a in act_all if a[1] == OP and a[4] in spells and a[2] == obs_id]
print(f'OP_Action on the observer stream: {len([a for a in act_all if a[1] == OP])} total, '
      f'{len(measured)} measured (spell in {spells}, target = the observer)')
for t, cp, tg, src, s in measured:
    print(f'  {t:.3f} (video {t - off:+.3f}) target {tg} source {src} spell {s}')
print(f'OP_Death (whole leg, both streams): {len(death_all)}')
for t, v, cp in death_all:
    prior = [a for a in act_all if 0 <= a[0] - t < 3.0 and a[2] == v]
    note = ''
    for _, cp2, tg, src, s in prior:
        if tg == v and src in (obs_id, caster_id):
            note = f' <== hit by a leg character (source {src} spell {s})'
    print(f'  {t:.3f} (video {t - off:+.3f}) victim {v} = {who(v)}'
          f' [stream {names.get(cp)}]{note}')
print(f'OP_FormattedMessage 9073 (damage lines): {len(dmg)}')
for t, f in dmg:
    print(f'  {t:.3f} (video {t - off:+.3f}) {f}')

# ---- the hard rule --------------------------------------------------------
viol = []
for t, cp, tg, src, s in measured:
    lo, hi = t - 2.6, t + 5.0  # M1's death tick was the OP_Action tick itself
    d_in = [d for d in death_all if lo <= d[0] <= hi]
    d_obs = [d for d in d_in if d[1] == obs_id]
    if d_obs:
        viol.append((t, d_obs))
    print(f'window video [{lo - off:+.3f}..{hi - off:+.3f}]: spell {s} Act {t - off:+.3f}: '
          f'{len(d_in)} OP_Death in window, {len(d_obs)} of them the observer')
ok = len(measured) == 6 and not viol and obs_id is not None
print(f'\nHARD RULE: {len(measured)}/6 measured OP_Action, '
      f'{len(viol)} cast window(s) with an observer OP_Death -> {"PASS" if ok else "FAIL"}')
sys.exit(0 if ok else 1)
