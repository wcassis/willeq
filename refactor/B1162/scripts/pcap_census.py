#!/usr/bin/env python3
"""B1162 (#481): the per-stream opcode census of a B1162 leg capture -- the card's
HARD RULES, checked from the pcap FIRST (before any frame measurement):

  disc leg:     ZERO C->S OP_CastSpell is the hard-rule FAIL; a non-zero count is
                the PASS. Also: no S->C OP_BeginCast for a /disc (a discipline is
                not cast through OP_BeginCast), and the landing is an S->C
                OP_Action for one of the record-325 spells on the OBSERVER stream.
  combat leg:   ZERO C->S OP_InstillDoubt (0x389e) is the hard-rule FAIL. On
                success there is exactly the G-key InstillDoubt, and the landing
                is an S->C OP_Action for Fear 229 with NO OP_BeginCast (the
                intimidator is not casting).

usage: pcap_census.py <capture_dir> [spell spell ...]
  (capture_dir has capture.pcap + video.calibration.tsv)

The observer's stream is selected by the client port of its C->S OP_ZoneEntry
(Oraclewatch); the caster's by Testcaster's (the B1134 rule). Video-relative
times use calibrated_t0 (the video's frame-0 epoch) from video.calibration.tsv.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, '/home/user/src/willeq-wt/kb-b1162-combat-fx/scripts/eqemu/oracle/vendor')
import eqpcap  # noqa: E402

cap = Path(sys.argv[1])
spells = [int(a) for a in sys.argv[2:]] or [229, 4504, 6236, 6268, 8604, 8617, 8642]
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

try:
    off = float([l.split('\t') for l in open(cap / 'video.calibration.tsv')][1][3])
except Exception:
    off = 0.0
print(f'calibrated_t0 (video frame 0) = {off}')

c2s, s2c = {}, {}      # name -> list of (t, cp, code, payload)
begin, action, chat = [], [], []
seen = set()
for t, d, sp, cp, name, code, p in rows:
    if d == 'C->S':
        c2s.setdefault(name, []).append((t, cp, code, p))
        continue
    if cp not in (OP, CP):
        continue
    key = (name, cp, bytes(p))
    if key in seen:
        continue  # capture point sees each datagram twice
    seen.add(key)
    if name == 'OP_BeginCast' and len(p) >= 8:
        c, s, ct = struct.unpack_from('<HHI', p, 0)
        begin.append((t, cp, c, s, ct))
    elif name == 'OP_Action' and len(p) >= 31:
        if p[30] != 0:
            continue  # the 0x04 action-state twin of each landing
        tg, src = struct.unpack_from('<HH', p, 0)
        s = struct.unpack_from('<H', p, 27)[0]
        action.append((t, cp, tg, src, s))
    elif name in ('OP_FormattedMessage', 'OP_SimpleMessage', 'OP_SpecialMesg') and len(p) >= 4:
        chat.append((t, cp, name, eqpcap.fields(name, p)))

# ---- C->S census (the hard rule) ------------------------------------------
print('\nC->S opcode census (both streams):')
for name in sorted(c2s):
    lst = c2s[name]
    if name in ('OP_InstillDoubt', 'OP_CastSpell') or len(lst) <= 40:
        for t, cp, code, p in lst:
            extra = ''
            if name == 'OP_CastSpell' and len(p) >= 8:
                extra = ' spell16=%d spell32=%d' % (
                    struct.unpack_from('<H', p, 0)[0], struct.unpack_from('<I', p, 0)[0])
            if name == 'OP_InstillDoubt' and len(p) >= 4:
                extra = ' target=%d' % struct.unpack_from('<I', p, 0)[0]
            print(f'  {t:.3f} (vid {t-off:+.3f}) [{names.get(cp)}] OP_{name} 0x{code:04x} len={len(p)}{extra}')
    else:
        print(f'  {name}: {len(lst)}')

# ---- S->C BeginCast / Action ----------------------------------------------
print(f'\nS->C OP_BeginCast (both streams): {len(begin)}')
for t, cp, c, s, ct in begin:
    print(f'  {t:.3f} (vid {t-off:+.3f}) [{names.get(cp)}] caster {c} spell {s} cast_ms {ct}')

obs_act = [a for a in action if a[1] == OP and a[4] in spells]
cas_act = [a for a in action if a[1] == CP and a[4] in spells]
print(f'S->C OP_Action spell-in{spells}: observer-stream {len(obs_act)}, caster-stream {len(cas_act)}')
for t, cp, tg, src, s in sorted(obs_act + cas_act):
    print(f'  {t:.3f} (vid {t-off:+.3f}) [{names.get(cp)}] target {tg} source {src} spell {s}')

print(f'\nS->C chat lines: {len(chat)}')
for t, cp, name, f in chat:
    print(f'  {t:.3f} (vid {t-off:+.3f}) [{names.get(cp)}] {name}: {f}')

# ---- hard rule summary -----------------------------------------------------
inst = c2s.get('OP_InstillDoubt', [])
cast = c2s.get('OP_CastSpell', [])
print(f'\nHARD-RULE counts: C->S OP_InstillDoubt = {len(inst)}, C->S OP_CastSpell = {len(cast)}')
print(f'  S->C OP_Action spell-in{spells} observer-stream = {len(obs_act)}')
print(f'  (interpret per leg: disc leg PASS iff len(OP_CastSpell)>0; combat leg PASS iff len(OP_InstillDoubt)>0)')
sys.exit(0)
