#!/usr/bin/env python3
"""B1197: spell 253 (record 35) word dump -- the eff record's Target stage
slots and every EDD word of its landing emitters, per docs/spellsnew_format.md
(word meanings) and review/B1141/edd_fields_from_code.tsv (proven read sites).

usage: b1197_word_dump.py [spells_us.txt] [spellsnew.eff] [spellsnew.edd]
       (defaults under /mnt/projects/EverQuestP1999/)

Prints: field 145 of the spell-253 row; the eff record's three stages (sound +
4 slots: edd index, min caster level, anchor, attach point); and, for every
Target-stage slot, the EDD entry's full word table (run/lifetime, population,
fade/peak, shape, sprite size, colours, speeds, blend +0x68, attach scale
+0x6C, sprite mode +0x70).
"""
import struct
import sys

base = '/mnt/projects/EverQuestP1999'
spells_us = sys.argv[1] if len(sys.argv) > 1 else f'{base}/spells_us.txt'
eff_path = sys.argv[2] if len(sys.argv) > 2 else f'{base}/spellsnew.eff'
edd_path = sys.argv[3] if len(sys.argv) > 3 else f'{base}/spellsnew.edd'

# ---- spells_us.txt: spell 253, field 145 (0x44dbf0, the record name)
row = None
for line in open(spells_us, encoding='latin-1'):
    c = line.rstrip('\r\n').split('^')
    if c and c[0] == '253':
        row = c
        break
assert row, 'spell 253 not found in spells_us.txt'
# Field 145 in the client's (0-based) field numbering = c[145] here; fable's
# spells table stores it as spell_anim (104 for spell 253), and
# SpellsNewEffReader::getRecord() takes it as a 0-based record position.
rec_name = row[145] if len(row) > 145 else ''
if len(sys.argv) > 4:
    rec_name = sys.argv[4]  # override: try a candidate record index
print(f'spells_us.txt spell 253: name={row[1]!r}  field145={rec_name!r}  '
      f'nfields={len(row)}')

# ---- spellsnew.eff: records of 0x10c (268) bytes. Field 145 is a 1-based
# POSITIONAL record index (SpellsNewEffReader::getRecord returns
# m_records[index]; the eff "names" are the index as a string).
eff = open(eff_path, 'rb').read()
nrec = len(eff) // 268
print(f'spellsnew.eff records={nrec} bytes={len(eff)}')
try:
    rec_idx = int(rec_name)  # getRecord(index): 0-based position m_records[index]
except ValueError:
    print(f'field 145 is not an index: {rec_name!r}')
    sys.exit(1)
rec_off = rec_idx * 268
name = eff[rec_off:rec_off + 64].split(b'\x00')[0].decode(errors='replace')
print(f'eff record index={rec_idx} (0-based position per getRecord): eff name={name!r}')

ANCHOR = {0: 'world(fixed)', 1: 'caster', 2: 'missile', 3: 'target'}
ATTACH = {0: 'pos', 1: 'DEFAULT', 2: 'CHEST', 3: 'HEAD', 4: 'LHAND', 5: 'RHAND',
          6: 'LFOOT', 7: 'RFOOT', 8: 'origin', 9: 'LEYE', 10: 'REYE', 11: 'MOUTH', 12: 'CHEST-floor'}
STAGES = ['Source', 'Middle', 'Target']
target_emitters = []
for s in range(3):
    off = rec_off + 0x40 + 0x44 * s  # 0x44 = 68-byte stage groups
    sound = struct.unpack_from('<i', eff, off)[0]
    print(f'\nstage {s} {STAGES[s]}: sound={sound}')
    for k in range(4):
        so = off + 4 + 16 * k
        idx, lvl, anchor, attach = struct.unpack_from('<iiii', eff, so)
        if idx < 1:
            continue
        tag = ''
        if s == 2:
            target_emitters.append(idx)
            tag = f'   <= TARGET LANDING (anchor={ANCHOR.get(anchor, anchor)} attach={ATTACH.get(attach, attach)})'
        print(f'  slot {k}: edd={idx} minLevel={lvl} anchor={ANCHOR.get(anchor, anchor)} attach={ATTACH.get(attach, attach)}{tag}')

# ---- spellsnew.edd: entries of 0x1a0 (416) bytes.
# 1-BASED records, exactly the client reader's convention
# (spells_new_edd_reader.cpp: kSpellsNewEddMagicBytes = 8 -- "EDD\0" "110\0";
#  entryBytes(i) = bytes + 8 + i*416, i >= 1; record 0 at offset 8 is the
#  "None" placeholder and is never addressed). A 0-based offset reads the
#  PREVIOUS record: that off-by-one shifts every word by one emitter.
edd = open(edd_path, 'rb').read()
nedd = (len(edd) - 8) // 416
print(f'\nspellsnew.edd entries={nedd} bytes={len(edd)} (record 0 = {edd[8:8+32].split(b"\x00")[0]!r}, records 1..{nedd})')


def f32(e, o):
    return struct.unpack_from('<f', e, o)[0]


def i32(e, o):
    return struct.unpack_from('<i', e, o)[0]


for idx in sorted(set(target_emitters)):
    e = edd[8 + idx * 416:8 + (idx + 1) * 416]
    name = e[0:32].split(b'\x00')[0].decode(errors='replace')
    tex = e[0x40:0x40 + 32].split(b'\x00')[0].decode(errors='replace')
    print(f'\n--- EDD {idx}  {name}  tex={tex}')
    words = [
        ('+0x60 orient_to_bone', i32(e, 0x60)), ('+0x64 no_depth_write', i32(e, 0x64)),
        ('+0x68 ADDITIVE_BLEND', i32(e, 0x68)), ('+0x6C attach_scale', i32(e, 0x6C)),
        ('+0x70 sprite_mode', i32(e, 0x70)), ('+0x74 follow_emitter', i32(e, 0x74)),
        ('+0x78 run_T', f32(e, 0x78)), ('+0x7C life_L', f32(e, 0x7C)),
        ('+0x80 burst', i32(e, 0x80)), ('+0x84 per_tick', i32(e, 0x84)),
        ('+0x88 ticks_s', f32(e, 0x88)), ('+0x8C start_delay', f32(e, 0x8C)),
        ('+0x90 fade_in', f32(e, 0x90)), ('+0x94 fade_out', f32(e, 0x94)),
        ('+0x98 grow_in', f32(e, 0x98)), ('+0x9C shrink_out', f32(e, 0x9C)),
        ('+0xA0 density_dist', f32(e, 0xA0)), ('+0xA4 peak_alpha', f32(e, 0xA4)),
        ('+0xA8 spawn_shape', i32(e, 0xA8)),
        ('+0xAC shape_rX', f32(e, 0xAC)), ('+0xB0 shape_rY', f32(e, 0xB0)),
        ('+0xB4 shape_h', f32(e, 0xB4)),
        ('+0xB8 off1', f32(e, 0xB8)), ('+0xBC off2', f32(e, 0xBC)), ('+0xC0 off3', f32(e, 0xC0)),
        ('+0xC4 tilt1', f32(e, 0xC4)), ('+0xC8 tilt2', f32(e, 0xC8)),
        ('+0xCC width_min', f32(e, 0xCC)), ('+0x17C height_min', f32(e, 0x17C)),
        ('+0x184 width_max', f32(e, 0x184)), ('+0x180 height_max', f32(e, 0x180)),
        ('+0x18C one_random', i32(e, 0x18C)), ('+0x190 grow_w', f32(e, 0x190)),
        ('+0x194 shrink_w', f32(e, 0x194)), ('+0x19C scale_size', f32(e, 0x19C)),
        ('+0xD0 depth_bias', f32(e, 0xD0)),
        ('+0xD4 start_R', i32(e, 0xD4)), ('+0xD8 start_G', i32(e, 0xD8)),
        ('+0xDC start_B', i32(e, 0xDC)), ('+0xE0 end_R', i32(e, 0xE0)),
        ('+0xE4 end_G', i32(e, 0xE4)), ('+0xE8 end_B', i32(e, 0xE8)),
        ('+0xEC up_vmin', f32(e, 0xEC)), ('+0xF0 up_vmax', f32(e, 0xF0)),
        ('+0xF4 up_acc', f32(e, 0xF4)),
        ('+0xF8 ax2_vmin', f32(e, 0xF8)), ('+0xFC ax2_vmax', f32(e, 0xFC)),
        ('+0x100 ax2_acc', f32(e, 0x100)),
        ('+0x104 ax3_vmin', f32(e, 0x104)), ('+0x108 ax3_vmax', f32(e, 0x108)),
        ('+0x10C ax3_acc', f32(e, 0x10C)),
        ('+0x110 radial_vmin', f32(e, 0x110)), ('+0x114 radial_vmax', f32(e, 0x114)),
        ('+0x118 radial_acc', f32(e, 0x118)),
        ('+0x11C swirl_vmin', f32(e, 0x11C)), ('+0x120 swirl_vmax', f32(e, 0x120)),
        ('+0x124 swirl_acc', f32(e, 0x124)),
        ('+0x128 gravity', f32(e, 0x128)), ('+0x12C driftX', f32(e, 0x12C)),
        ('+0x130 frames', i32(e, 0x130)), ('+0x134 frameRate', f32(e, 0x134)),
    ]
    for nm, v in words:
        if isinstance(v, float):
            print(f'  {nm:18s} = {v:.4g}')
        else:
            print(f'  {nm:18s} = {v}')
print('\nTARGET LANDING EMITTERS (record 35 / spell 253):', sorted(set(target_emitters)))
