#!/usr/bin/env python3
"""B1150: the spawn-geometry words (+0xA8..+0xD0, +0x19C) of the 40 B1130
records' Source/Target emitters, from the raw spellsnew.eff + spellsnew.edd.
Also prints the file sha256 and the full-file +0xA8 census (the card's
distribution, re-derived independently of the B1151 trace)."""
import csv, hashlib, struct, sys

E = '/mnt/projects/EverQuestP1999'
EDD = open(E + '/spellsnew.edd', 'rb').read()
EFF = open(E + '/spellsnew.eff', 'rb').read()
print('# sha256 spellsnew.edd', hashlib.sha256(EDD).hexdigest())
assert EDD[:8] == b'EDD\x00110\x00', EDD[:8]
n_edd = (len(EDD) - 8) // 416
print(f'# edd entries {n_edd}; eff bytes {len(EFF)} records {len(EFF)//268}')

def f(b, o): return struct.unpack_from('<f', b, o)[0]
def i(b, o): return struct.unpack_from('<i', b, o)[0]

def edd(em):  # 1-based EDD index, like the reader
    return EDD[8 + (em - 1) * 416:]

census = [0] * 10
for k in range(n_edd):
    s = i(EDD[8 + k * 416:], 0xA8)
    if 0 <= s < 10: census[s] += 1
print('# +0xA8 census:', ' '.join(f'{s}:{c}' for s, c in enumerate(census)), 'sum', sum(census))

def eff_slots(rec, stage):
    b = EFF[rec * 268 + 64 + stage * 68:]
    out = []
    for k in range(4):
        em = i(b, 4 + k * 16)
        if em < 1: continue
        out.append((k, em, i(b, 8 + k * 16), i(b, 12 + k * 16)))  # slot, emitter, minLevel, anchor
    return out

rows = list(csv.DictReader(open('/mnt/projects/fable/review/B1130/record_table.tsv'), delimiter='\t'))
# The row -> spellsnew.eff record map: config/spell_effects.json animRecords.rows
# is {field-145 record: B1130 row id} (B1137's gen_anim_rows.py), inverted.
# B1130's row id is NOT the eff index (row 0 = spell 93 draws eff record 80,
# 'Ignite80ct V2'; eff record 0 is the 'None' placeholder).
import json as _json
_cfg = _json.load(open('/home/user/src/willeq-wt/kb-b1150-shapes/config/spell_effects.json'))
_inv = {}
for _k, _v in _cfg['animRecords']['rows'].items():
    _inv.setdefault(int(_v), []).append(int(_k))

print('record\tspell\ttargettype\tf145\tstage\tslot\tem\ttex\tshape\tAC\tB0\tB4\tB8\tBC\tC0\tC4\tC8\tD0\ts19c')
for r in rows:
    rec = int(r['record'])
    if rec == 51: continue  # B1134's discipline rows, out of this sweep's 40
    for f145 in _inv.get(rec, []):
        for stage, nm in ((0, 'S'), (2, 'T')):
            for k, em, lvl, anc in eff_slots(f145, stage):
                e = edd(em)
                tex = e[0x40:0x60].split(b'\x00')[0].decode().rstrip('.')
                print(f"{rec}\t{r['spell_id']}\t{r['targettype']}\t{f145}\t{nm}\t{k}\t{em}\t{tex}\t"
                      f"{i(e,0xA8)}\t{f(e,0xAC):g}\t{f(e,0xB0):g}\t{f(e,0xB4):g}\t"
                      f"{f(e,0xB8):g}\t{f(e,0xBC):g}\t{f(e,0xC0):g}\t"
                      f"{f(e,0xC4):g}\t{f(e,0xC8):g}\t{f(e,0xD0):g}\t{i(e,0x19C)}")
