#!/usr/bin/env python3
"""B1152 (#471): the per-row mechanism material for the gate table -- for
every sweep row, the B1151 vs B1150 shape.txt PEAK/END diff (the two runs'
own measured rows) and the record's Target-stage EDD motion words (the
nonzero ones; the words B1151's sim integrates that B1150's did not).
usage: gate_analysis.py [b1151_runs] [b1150_runs] [client_dir]
  b1151_runs  default /home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/runs
  b1150_runs  default /home/user/kb-evidence/kb-b1150-shapes/runs
  client_dir  default /mnt/projects/EverQuestP1999 (spellsnew.eff + .edd)
shape.txt columns (harness instrument, both legs):
  frame  t_act  cov  extent  top  centroid  frac>1H  rowtop
PEAK = the max-cov frame; END = the last frame with cov > 0.01.
"""
import struct
import sys
import os

B1151 = sys.argv[1] if len(sys.argv) > 1 else '/home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/runs'
B1150 = sys.argv[2] if len(sys.argv) > 2 else '/home/user/kb-evidence/kb-b1150-shapes/runs'
CLIENT = sys.argv[3] if len(sys.argv) > 3 else '/mnt/projects/EverQuestP1999'

LIST = '/home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/plans/sweep/list.tsv'


def shape_stats(path):
    """shape.txt -> (peak, end) dicts (None when the file is missing)."""
    if not os.path.exists(path):
        return None, None
    rows = []
    for line in open(path):
        if line.startswith('#') or not line.strip() or line.startswith('frame'):
            continue
        p = line.rstrip('\n').split('\t')
        try:
            rows.append(dict(frame=int(p[0]), t=float(p[1]), cov=float(p[2]),
                             ext=float(p[3]), cen=float(p[5])))
        except (ValueError, IndexError):
            continue
    if not rows:
        return None, None
    pk = max(rows, key=lambda r: r['cov'])
    end = [r for r in rows if r['cov'] > 0.01]
    return pk, (end[-1] if end else None)


# -- the EDD (416-byte CEmitterDef entries) and EFF (268-byte records) --
EDD = open(CLIENT + '/spellsnew.edd', 'rb').read()
EFF = open(CLIENT + '/spellsnew.eff', 'rb').read()
EHDR, EREC, XREC = 8, 416, 268


def ew(i, off):
    return struct.unpack_from('<I', EDD, EHDR + i * EREC + off)[0]


def ef(i, off):
    return struct.unpack_from('<f', EDD, EHDR + i * EREC + off)[0]


FLOAT_WORDS = {0xEC: 'upMin', 0xF0: 'upMax', 0xF4: 'upAccel', 0xF8: 'axis2Min',
               0xFC: 'axis2Max', 0x100: 'axis2Accel', 0x104: 'axis3Min',
               0x108: 'axis3Max', 0x10C: 'axis3Accel', 0x110: 'radialMin',
               0x114: 'radialMax', 0x118: 'radialAccel', 0x11C: 'swirlMin',
               0x120: 'swirlMax', 0x124: 'swirlAccel', 0x128: 'gravity',
               0x12C: 'driftX', 0x134: 'fps', 0x138: 'spinMin', 0x188: 'spinMax'}
INT_WORDS = {0x60: 'orient', 0x64: 'noDepth', 0x70: 'mode', 0x74: 'follow',
             0x130: 'N', 0x174: 'randSpin', 0x178: 'align', 0x198: 'drawNeg'}


def em_motion(em):
    """one EDD entry's nonzero motion words (the B1151-integrated set)."""
    bits = []
    for off, nm in FLOAT_WORDS.items():
        v = ef(em, off)
        if v != 0.0:
            bits.append(f'{nm}={v:g}')
    for off, nm in INT_WORDS.items():
        v = ew(em, off)
        if v:
            bits.append(f'{nm}={v}')
    return ', '.join(bits) if bits else '-'


def target_emitters(rec):
    off = rec * XREC + 64 + 2 * 68  # the Target stage (si=2)
    out = []
    for s in range(4):
        em, lvl, anch, att = struct.unpack_from('<4i', EFF, off + 4 + s * 16)
        if em >= 1:
            out.append((em, anch))
    return out


ANCH = {0: 'world', 1: 'caster', 2: 'missile', 3: 'target'}

print('# per row: B1151 vs B1150 shape.txt (PEAK = max-cov frame, END = last cov>0.01) + the')
print('# record Target-stage EDD emitters with their nonzero motion words (edd_census.py offsets).')
print()
print('| row | spell | B1150 peak (cov/ext/cen/t) | B1151 peak (cov/ext/cen/t) | d cov | d cen | B1150 end | B1151 end | T emitters (nonzero motion words) |')
print('|---|---|---|---|---|---|---|---|---|')

for line in open(LIST):
    line = line.rstrip('\n')
    if not line or line.startswith('#') or line.startswith('record'):
        continue
    rec, sid, tgt, cms = line.split('\t')[:4]
    p51, e51 = shape_stats(f'{B1151}/b1151_r{rec}_{sid}/measure/fable_{sid}/shape.txt')
    p50, e50 = shape_stats(f'{B1150}/b1150_r{rec}_{sid}/measure/fable_{sid}/shape.txt')

    def fmt(p, k):
        return f'{p[k]:.3f}' if p else '-'

    def fmtt(p):
        return f'{p["t"]:+.2f}' if p else '-'

    if p50 and p51:
        dcov, dcen = f'{p51["cov"] - p50["cov"]:+.3f}', f'{p51["cen"] - p50["cen"]:+.2f}'
        pk50 = f'{p50["cov"]:.3f}/{p50["ext"]:.2f}/{p50["cen"]:.2f}/{fmtt(p50)}'
        pk51 = f'{p51["cov"]:.3f}/{p51["ext"]:.2f}/{p51["cen"]:.2f}/{fmtt(p51)}'
    else:
        dcov = dcen = pk50 = pk51 = '-'
    ems = target_emitters(int(rec))
    ems_s = '; '.join(f'em{em}({ANCH.get(a, a)}) {em_motion(em)}' for em, a in ems) or 'none'
    print(f'| {rec} | {sid} | {pk50} | {pk51} | {dcov} | {dcen} | {fmtt(e50)} | {fmtt(e51)} | {ems_s} |')
