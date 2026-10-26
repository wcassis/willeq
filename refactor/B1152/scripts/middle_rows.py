#!/usr/bin/env python3
"""B1152 (#472): which of the 40 sweep rows carry a DRAWN Middle stage at
level 60 — the missile population the README's regression paragraph cites.
Re-derives it from the raw official-client files:
  spells_us.txt  -- '^'-separated, field 0 = spell id, field 145 = spell_anim
                    (the spellsnew.eff record the official client draws,
                    eqemu spells_new.spellanim; tools/willeq_db/db_loaders_text.cpp
                    "B1127: field 145")
  spellsnew.eff  -- 268-byte records (spells_new_eff_reader.cpp): 64 name
                    bytes, then 3 stages x 68 bytes, each = 4-byte sound +
                    4 slots x 16 bytes (emitter, minLevel, anchor, attachPoint).

The FX gate (spell_visual_fx.cpp:567-568, SpellVisualFX::stageSlots) is
  record = field 145; drawn iff hasContent() && some Middle slot has
  emitter > 0 (hasEmitter) and minLevel <= casterLevel (drawnAtLevel,
  spells_new_eff_reader.h:99-100 -- the client's 0x495cc3 level gate).

Command (this host):
  python3 refactor/B1152/scripts/middle_rows.py /mnt/projects/EverQuestP1999
"""
import struct
import sys

CLIENT = sys.argv[1] if len(sys.argv) > 1 else '/mnt/projects/EverQuestP1999'
LIST = '/home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/plans/sweep/list.tsv'
LEVEL = 60  # the sweep's caster level (README: "tour geometry, caster level 60")

# spells_us.txt: field 145 per spell id
f145 = {}
for line in open(CLIENT + '/spells_us.txt'):
    if not line.strip():
        continue
    f = line.rstrip('\n').split('^')
    if len(f) < 146:
        continue
    sid = int(f[0] or 0)
    if sid > 0:
        f145[sid] = int(f[145] or 0)

EFF = open(CLIENT + '/spellsnew.eff', 'rb').read()
R, NAME, STAGE = 268, 64, 68  # the spells_new_eff_reader.cpp layout
NREC = len(EFF) // R


def eff_rec(rec):
    # getRecord(index) -> m_records[index] (m_records[0] is the file's first
    # block; index <= 0 is rejected, so valid records are 1..NREC-1).
    if not (1 <= rec < NREC):
        return None, []
    b = EFF[rec * R:rec * R + R]
    name = b[:NAME].split(b'\x00')[0].decode('latin1')
    middle = []
    has_content = False
    for g in range(3):
        s = b[NAME + g * STAGE:]
        for k in range(4):
            em, lvl, anch, att = struct.unpack_from('<4i', s, 4 + k * 16)
            if em > 0:
                has_content = True
            if g == 1:
                middle.append((em, lvl, anch, att))
    return name, middle


rows = []
for line in open(LIST):
    line = line.rstrip('\n')
    if not line:
        continue
    row, sid, tgt, cms = line.split('\t')[:4]
    rows.append((int(row), int(sid), tgt))

print(f'# spellsnew.eff: {NREC} records; spells_us.txt: {len(f145)} spells')
print('# row  spell  rec(f145)  eff-name        drawn-Middle@60 (em/lvl/anch)  missile')
n_missile = 0
for (row, sid, tgt) in sorted(rows):
    rec = f145.get(sid)
    name, middle = eff_rec(rec)
    drawn = [m for m in middle if m[0] > 0 and m[1] <= LEVEL]
    missile = bool(name) and bool(drawn)
    n_missile += missile
    ds = ', '.join(f'em{em} lvl{lvl} a{anch}' for em, lvl, anch, att in drawn) or '-'
    print(f'{row:>4}  {sid:>4}  {rec}            {name or "-":<14}  {ds:<40}  {missile}')
print(f'# rows with a drawn Middle stage (the missile population): {n_missile}/40')
