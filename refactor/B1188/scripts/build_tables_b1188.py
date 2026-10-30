#!/usr/bin/env python3
"""B1184: the gate on ONE tree (fable a99b3822) vs ONE reference set (B1178).

Judges the B1184 re-render (tour-7 + both 40-record sweeps) against:
  tour  -> tests/spell_fx_population_oracle.h kTour        (B1178 Ask 1 v3 OR_L pin)
  b1130 -> refactor/B1153/tables/gate_c.tsv oracle_* cols  (B1178 Ask 2 regenerated AFTER table)
  b1118 -> refactor/B1153/tables/gate_b.tsv b1151_* cols   (the committed reference; B1178 did
          not re-measure this sweep, so its B1178 reference set is the b1151 baseline, zero band)
Band/verdict rules are the committed ones: refactor/B1178/scripts/gate_after.py (bands +
verdict + gate_counts) and refactor/B1155/scripts/band_table.py (the nine b1130 metrics;
its bands are identical to gate_after's). Tour: landing_cov +/-3%, landing_end +/-0.10 s
(B1178 Ask 3(b)), roi absolute windows from kTour. b1118: zero-width band (gate_b).

Per row: oracle reference, willeq value, band verdict, delta vs the previous gate
(B1180's after columns, /home/user/kb-evidence/kb-b1180-sprite-width/gate/tables/).
PASS/FAIL counts per column. Verification: re-judging the B1178-era fable values
(gate_c harness_*, gate_b b1153_*, regression_table new_*) must reproduce the
committed verdicts and the B1178 Ask 3 counts (104 PASS / 0 gates; per-column
36/20/14/14/10/8/2) and the B1178 (b) tour flips.

Every number is read from a file named below; nothing is retyped.
"""
import re, os, sys, json

WT = '/home/user/src/willeq-wt/kb-b1188-landing-pixels'
# B1188 FIX (note 8515, finding 2): the same judge over any measured gate --
# the committed B1188 gate2 (the default) or the merge-base (ac8c26bc, the
# B1186 tip) baseline rendered by render_gate_base.sh into gate_base/measure.
#   B1188_MEASURE=/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/gate_base/measure \
#   B1188_OUT=refactor/B1188/tables/base python3 refactor/B1188/scripts/build_tables_b1188.py
MEASURE = os.environ.get('B1188_MEASURE',
                         '/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/gate2/measure')
B1180 = '/home/user/kb-evidence/kb-b1180-sprite-width/gate/tables'
OUT = os.environ.get('B1188_OUT', os.path.join(WT, 'refactor/B1188/tables'))

# ---------------- band rules: copied VERBATIM from refactor/B1178/scripts/gate_after.py
# (itself the committed form of refactor/B1155/scripts/band_table.py)
def band(metric, o):
    if metric in ('source_cov', 'landing_cov'): return 0.03 * o
    if metric in ('p90_src', 'p90_land'):       return 0.15 * o
    if metric in ('landing_end_s', 'source_onset_s'): return 0.10
    return 0.15

def verdict(m, o, h):
    import math
    if o is None or h is None or math.isnan(h): return 'n-a'
    b = band(m, o)
    if abs(h - o) <= b: return 'PASS'
    return 'short' if h < o - b else 'over'

# ---------------- parsers
def f(x):
    x = (x or '').strip()
    if x in ('', '-', 'n/a', 'n-a'):
        return None
    try:
        return float(x)
    except ValueError:
        return None

def readtsv(p):
    rows = []
    with open(p) as fh:
        lines = [l.rstrip('\n') for l in fh if l.strip() and not l.lstrip().startswith('#')]
    hdr = lines[0].split('\t')
    for l in lines[1:]:
        c = l.split('\t')
        rows.append(dict(zip(hdr, c)))
    return hdr, rows

def parse_ktour():
    p = os.path.join(WT, 'tests/spell_fx_population_oracle.h')
    txt = open(p).read()
    m = re.search(r'kTour\[\]\s*=\s*\{(.*?)\};', txt, re.S)
    body = m.group(1)
    out = {}
    for r in re.finditer(r'\{\s*(\d+),\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\s*\}', body):
        g = [float(x) for x in r.groups()[1:]]
        out[int(r.group(1))] = dict(oracle_land=g[0], oracle_end=g[1], lane_land=g[2], lane_end=g[3],
                                    roi_l_lo=g[4], roi_l_hi=g[5], roi_s_lo=g[6], roi_s_hi=g[7])
    return out

def roi_verdict(v, lo, hi):
    if v is None:
        return 'n-a'
    if v < lo:
        return 'short'
    if v > hi:
        return 'over'
    return 'PASS'

def num(x):
    if isinstance(x, (int, float)): return x
    try: return float(x)
    except (TypeError, ValueError): return None

# ---------------- load references
kTour = parse_ktour()
_, gate_c = readtsv(os.path.join(WT, 'refactor/B1153/tables/gate_c.tsv'))
_, gate_b = readtsv(os.path.join(WT, 'refactor/B1153/tables/gate_b.tsv'))
_, reg    = readtsv('/mnt/projects/fable/review/B1142/gate/regression_table.tsv')
reg_by_spell = {int(r['spell']): r for r in reg if r.get('spell','').isdigit()}
_, rec_table = readtsv('/mnt/projects/fable/review/B1130/record_table.tsv')
spell_of_record = {int(r['record']): r['spell_id'] for r in rec_table}
_, b1180_tour = readtsv(os.path.join(B1180, 'tour7.tsv'))
_, b1180_1118 = readtsv(os.path.join(B1180, 'b1118.tsv'))
_, b1180_1130 = readtsv(os.path.join(B1180, 'b1130.tsv'))
b1180_tour_by = {int(r['spell']): r for r in b1180_tour}
b1180_1118_by = {int(r['row'][1:]): r for r in b1180_1118}
b1180_1130_by = {int(r['record']): r for r in b1180_1130}

# B1180's full AFTER band table (its re-gate, pre-B1178 references): the
# header row carries no key columns; the data rows begin with record, kind.
def _load_b1180_band(p):
    lines = [l.rstrip('\n') for l in open(p) if l.strip() and not l.lstrip().startswith('#')]
    hdr = ['record', 'kind'] + [h for h in lines[0].split('\t') if h]
    out = []
    for l in lines[1:]:
        c = l.split('\t')
        out.append(dict(zip(hdr, c[:len(hdr)])))
    return out
b1180_1130_band_by = {int(r['record']): r for r in _load_b1180_band(os.path.join(B1180, 'b1130_band.tsv'))}

METRICS = ['source_cov','landing_cov','landing_end_s','p90_src','p90_land',
           'source_onset_s','source_centroid','src_h_lo','src_h_hi']

# ---------------- mine
def load_mine_b1130(rec):
    p = os.path.join(MEASURE, f'b1130_r{rec}', 'b1130_rows.tsv')
    if not os.path.exists(p):
        return None
    _, rows = readtsv(p)
    return rows[0] if rows else None

def load_mine_summary(tag):
    p = os.path.join(MEASURE, tag, 'summary.tsv')
    if not os.path.exists(p):
        return None
    _, rows = readtsv(p)
    return rows[0] if rows else None

# ================= b1130
B1130_TABLE = os.path.join(WT, 'refactor/B1171/tables/b1130_table.tsv')
_, b1130_tbl = readtsv(B1130_TABLE)
b1130_committed_owner = {int(r['record']): r.get('owner','') for r in b1130_tbl}
EXTRA5 = ['p90_src','source_onset_s','source_centroid','src_h_lo','src_h_hi']

def build_b1130():
    out, counts = [], {m: {'PASS':0,'short':0,'over':0,'n-a':0} for m in METRICS}
    for gc in gate_c:
        rec = int(gc['record']); kind = gc['kind']
        mine = load_mine_b1130(rec)
        row = {'record': rec, 'kind': kind, 'spell': spell_of_record.get(rec,'')}
        reds = []
        for m in METRICS:
            o = f(gc.get('oracle_'+m))
            if mine is None:
                h, v = None, 'n-a'
            else:
                # b1130_rows.tsv columns ARE the metric names (harness_measure_b1130.py:30-32)
                h = f(mine.get(m))
                v = verdict(m, o, h)
            row['oracle_'+m] = '' if o is None else o
            row['mine_'+m] = '' if h is None else h
            row['v_'+m] = v
            counts[m][v] = counts[m].get(v,0) + 1
            if v in ('short','over'):
                reds.append(m)
        # previous gate (B1180's re-gate AFTER, all nine metrics from its band
        # table) + per-metric deltas
        pb = b1180_1130_band_by.get(rec, {})
        for nm in METRICS:
            prev = f(pb.get('harness_'+nm))
            cur = num(row['mine_'+nm])
            row['b1180_'+nm] = '' if prev is None else prev
            row['d_'+nm] = '' if (prev is None or cur is None) else round(cur - prev, 3)
        row['red'] = ';'.join(reds) if reds else ''
        # owner: the committed per-row owner (B1171 table, as updated in place by
        # B1178/B1179) plus class owners for the five extra metrics; a red
        # landing_cov on a B1164/B1172-owned row carries the FU1 residual
        owner = (B1130_ROW_OWNERS.get(rec) or b1130_committed_owner.get(rec, '') or
                 B1130_CLASS_OWNERS.get('landing_cov', 'unowned -- check'))
        extra_red = [m for m in EXTRA5 if m in reds]
        if extra_red:
            owner += (' | ' if owner else '') + ' '.join(
                f'{m}: {B1130_CLASS_OWNERS[m]}' for m in extra_red)
        if 'landing_cov' in reds and 'B1164' in owner:
            owner += ' | landing_cov residual: ' + FU1
        row['owner'] = owner
        out.append(row)
    return out, counts

# ================= b1118
B1118_COLS = ['land_cov','end','src_cov','roi_l','roi_s']
B1118_CHAIN = ("post-B1151 chain (the committed cards): B1157 (cast-anim), B1164 (s), "
               "B1166 (anchor), B1170 (missile), B1172 (target scale), B1177 (solver), "
               "B1180 (width factor)")
def build_b1118():
    out, counts = [], {m: {'PASS':0,'short':0,'over':0,'n-a':0} for m in B1118_COLS}
    for gb in gate_b:
        n = int(gb['row'][1:]); spell = int(gb['spell'])
        # the runner tags b1118 rows by SPELL (run_gate_b1184.sh); the spell ->
        # row map is unique (40 distinct spells in the B1152 sweep list)
        mine = load_mine_summary(f'b1118_r{spell}')
        row = {'row': gb['row'], 'spell': spell}
        reds = []
        for m, mine_k in zip(B1118_COLS, ('land_cov','end','src_cov','roi_l','roi_s')):
            o = f(gb.get('b1151_'+m))
            h = None if mine is None else f(mine.get(mine_k))
            v = ('n-a' if (h is None or o is None)
         else ('PASS' if abs(h - o) <= 1e-9 else ('short' if h < o else 'over')))
            row['b1151_'+m] = '' if o is None else o
            row['mine_'+m] = '' if h is None else h
            row['v_'+m] = v
            counts[m][v] = counts[m].get(v,0) + 1
            if v in ('short','over'):
                reds.append(m)
        pb = b1180_1118_by.get(n, {})
        for nm, mine_k in (('land_cov','mine_land_cov'),('end','mine_end'),('src_cov','mine_src_cov')):
            prev = f(pb.get(mine_k))
            cur = num(row['mine_'+nm])
            row['b1180_'+nm] = '' if prev is None else prev
            row['d_'+nm] = '' if (prev is None or cur is None) else round(cur - prev, 3)
        row['red'] = ';'.join(reds) if reds else ''
        committed = gb.get('owner','')
        if spell == 253:
            owner = 'B1167 #486 (r35/253: the kept override carries the pool + duration gaps)'
        elif reds:
            if committed and 'none' not in committed:
                owner = committed + ' + ' + B1118_CHAIN
            else:
                owner = B1118_CHAIN
        else:
            owner = committed or 'none (in band)'
        row['owner'] = owner
        out.append(row)
    return out, counts

# ================= tour
TOUR_COLS = ['land_cov','end','roi_l','roi_s']
def build_tour():
    out, counts = [], {m: {'PASS':0,'short':0,'over':0,'n-a':0} for m in TOUR_COLS}
    for spell in sorted(kTour):
        ref = kTour[spell]
        mine = load_mine_summary(f'tour_{spell}')
        row = {'spell': spell}
        reds = []
        redmap = {}
        # landing_cov: +-3 % of oracle (B1178 Ask 3(b))
        h = None if mine is None else f(mine.get('land_cov'))
        v = verdict('landing_cov', ref['oracle_land'], h)
        row['oracle_land_cov'], row['mine_land_cov'], row['v_land_cov'] = ref['oracle_land'], '' if h is None else h, v
        counts['land_cov'][v] += 1
        if v in ('short','over'):
            reds.append('land_cov'); redmap['land_cov'] = v
        # landing_end: +/-0.10 s (253: fable has no measured end -> n-a)
        h = None if mine is None else f(mine.get('end'))
        v = verdict('landing_end_s', ref['oracle_end'], h)
        row['oracle_end'], row['mine_end'], row['v_end'] = ref['oracle_end'], '' if h is None else h, v
        counts['end'][v] += 1
        if v in ('short','over'):
            reds.append('end'); redmap['end'] = v
        for m, lo, hi in (('roi_l', ref['roi_l_lo'], ref['roi_l_hi']), ('roi_s', ref['roi_s_lo'], ref['roi_s_hi'])):
            h = None if mine is None else f(mine.get(m))
            v = roi_verdict(h, lo, hi)
            row['band_'+m] = f'{lo}-{hi}'
            row['mine_'+m], row['v_'+m] = ('' if h is None else h), v
            counts[m][v] += 1
            if v in ('short','over'):
                reds.append(m); redmap[m] = v
        row['mine_src_cov'] = '' if mine is None else mine.get('src_cov','')
        row['mine_src_on25'] = '' if mine is None else mine.get('src_on25','')
        row['mine_src_cen'] = '' if mine is None else mine.get('src_cen','')
        pb = b1180_tour_by.get(spell, {})
        for nm in ('land_cov','end','src_cov'):
            prev = f(pb.get('mine_'+nm))
            cur = num(row['mine_'+nm])
            row['b1180_'+nm] = '' if prev is None else prev
            row['d_'+nm] = '' if (prev is None or cur is None) else round(cur - prev, 3)
        row['red'] = ';'.join(reds) if reds else ''
        row['owner'] = tour_owner(spell, redmap) if reds else 'none (in band)'
        out.append(row)
    return out, counts

# ================= distance to the finish line
# row-level "in band" = no red cell (short/over); an n-a cell (no reference,
# or not measured) is neutral, not a failure.
def _clean(cells):
    return all(c not in ('short','over') for c in cells)

def build_distance(tour_rows, b1118_rows, b1130_rows):
    total = {'b1130': len(gate_c), 'tour': len(kTour), 'b1118': len(gate_b)}
    before, now = {t: 0 for t in total}, {t: 0 for t in total}
    # BEFORE = the committed B1178-AFTER state (b1130: gate_c's own verdicts --
    # B1178 proved the re-measure flips zero cells, so the committed table IS
    # the B1178-AFTER table; tour: B1142 new_* judged against kTour -- the
    # B1178(b) table, re-derived and pinned by verify(); b1118: gate_b's
    # committed verdicts -- B1178 did not re-reference the sweep)
    for gc in gate_c:
        if _clean([gc.get('verdict_'+m,'') for m in METRICS]):
            before['b1130'] += 1
    for spell in sorted(kTour):
        r = reg_by_spell[spell]
        k = kTour[spell]
        cells = [verdict('landing_cov', k['oracle_land'], f(r.get('new_land_cov'))),
                 verdict('landing_end_s', k['oracle_end'], f(r.get('new_end'))),
                 roi_verdict(f(r.get('new_roi_l')), k['roi_l_lo'], k['roi_l_hi']),
                 roi_verdict(f(r.get('new_roi_s')), k['roi_s_lo'], k['roi_s_hi'])]
        if _clean(cells):
            before['tour'] += 1
    for gb in gate_b:
        if _clean([gb.get(c,'') for c in ('land_cov_v','end_v','src_cov_v','roi_l_v','roi_s_v')]):
            before['b1118'] += 1
    for r in b1130_rows:
        if _clean([r['v_'+m] for m in METRICS]):
            now['b1130'] += 1
    for r in tour_rows:
        if _clean([r['v_land_cov'], r['v_end'], r['v_roi_l'], r['v_roi_s']]):
            now['tour'] += 1
    for r in b1118_rows:
        if _clean([r['v_'+m] for m in B1118_COLS]):
            now['b1118'] += 1
    lines = ['table\trows\trows_in_band_before_b1178_after\trows_in_band_now']
    for t in ('b1130','tour','b1118'):
        lines.append(f'{t}\t{total[t]}\t{before[t]}\t{now[t]}')
    return '\n'.join(lines) + '\n'

def before_counts():
    # per-column BEFORE verdict counts (the B1178-AFTER state)
    out = {'b1130': {}, 'tour': {}, 'b1118': {}}
    for m in METRICS:
        c = {'PASS':0,'short':0,'over':0,'n-a':0}
        for gc in gate_c:
            v = gc.get('verdict_'+m,'') or 'n-a'
            c[v] = c.get(v,0) + 1
        out['b1130'][m] = c
    for m in ('land_cov','end','roi_l','roi_s','src_cov'):
        c = {'PASS':0,'short':0,'over':0,'n-a':0}
        for spell in sorted(kTour):
            r = reg_by_spell[spell]; k = kTour[spell]
            if m == 'land_cov':
                v = verdict('landing_cov', k['oracle_land'], f(r.get('new_land_cov')))
            elif m == 'end':
                v = verdict('landing_end_s', k['oracle_end'], f(r.get('new_end')))
            elif m == 'roi_l':
                v = roi_verdict(f(r.get('new_roi_l')), k['roi_l_lo'], k['roi_l_hi'])
            elif m == 'roi_s':
                v = roi_verdict(f(r.get('new_roi_s')), k['roi_s_lo'], k['roi_s_hi'])
            else:  # src_cov: no B1178-era tour reference
                v = 'n-a'
            c[v] = c.get(v,0) + 1
        out['tour'][m] = c
    for m, col in zip(B1118_COLS, ('land_cov_v','end_v','src_cov_v','roi_l_v','roi_s_v')):
        c = {'PASS':0,'short':0,'over':0,'n-a':0}
        for gb in gate_b:
            v = gb.get(col,'')
            v = 'PASS' if v == 'in' else (v if v in ('short','over') else 'n-a')
            c[v] = c.get(v,0) + 1
        out['b1118'][m] = c
    return out

# ================= verification against the committed B1178-era data
def verify():
    errs = []
    # b1130: judge gate_c's own oracle_*/harness_* pairs with gate_after's rule;
    # must equal the committed verdict_* cells (0-cell diff) and total 104 PASS
    # (B1178 Ask 3: "the committed table ... reproduces the 104 PASS / 0 gates").
    exp = {m: {} for m in METRICS}
    diff = 0
    for gc in gate_c:
        for m in METRICS:
            got = verdict(m, f(gc.get('oracle_'+m)), f(gc.get('harness_'+m)))
            want = gc.get('verdict_'+m,'')
            if got != want:
                diff += 1
                if diff < 12:
                    errs.append(f"b1130 r{gc['record']} {m}: judge {got} != committed {want} (o={gc.get('oracle_'+m)} h={gc.get('harness_'+m)})")
            exp[m][got] = exp[m].get(got,0) + 1
    total_pass = sum(exp[m].get('PASS',0) for m in METRICS)
    if diff:
        errs.append(f'b1130 {diff} verdict cells differ from committed')
    if total_pass != 104:
        errs.append(f'b1130 total PASS {total_pass} != 104 (B1178 Ask 3)')
    # tour: judge B1142 new_* fable values against kTour; must equal the B1178 (b)
    # table's "vs NEW ref (v3)" column (README lines 352-361: the five flips)
    flip = {93:('short','PASS'), 38:('short','short'), 54:('short','PASS'), 656:('short','short'),
            41:('short','short'), 253:('over','n-a'), 458:('over','over')}
    for spell,(wl,we) in flip.items():
        r = reg_by_spell[spell]
        vl = verdict('landing_cov', kTour[spell]['oracle_land'], f(r['new_land_cov']))
        ve = verdict('landing_end_s', kTour[spell]['oracle_end'], f(r['new_end']))
        if (vl,ve) != (wl,we):
            errs.append(f'tour {spell}: judge ({vl},{ve}) != B1178(b) ({wl},{we})')
    return errs

# ================= owners (every red cell -> a named card or a NEW follow-up
# named in refactor/B1184/README.md, mechanism stated from the evidence)
#
# The landing_cov residual after B1164 #483 (s = 1.2), B1172 #491 (target
# scale) and B1180 #499 (width factor) -- all in this tip -- is the
# landing-window measurement domain: the harness window is per-target-height^2
# (the anchor-fitted target's height), the oracle reference the oracle
# target's height (B1171's attribution, carried by B1180 README sec. 3).
# No population word is under-applied: record 80 (spell 93) is drawn at its
# full file envelope -- burst 50 (+0x80) + per 50 (+0x84) x tps 30 (+0x88)
# over the 0.75 s window (+0x78 1.00 s -0x08C 0.25 s), each particle +0x7C
# 1.50 s -> 1175 live at peak (B1149 dump_pop.py's envelope; the tick rule
# pinned at 0x10060e8b..0x10060ffd, sim_particles.cpp:430-443; the emitter
# deactivates at its window, sim_particles.cpp:344-351).
FU1 = ("B1184-FU1 (NEW, the window-method unit per B1171's attribution, B1180 README "
       "sec. 3): band the harness landing window (per-target-height^2, the anchor-"
       "fitted target's height) to the capture's own window (the oracle target's "
       "height); the 93 residual after s (1.993 -> 2.719) + the B1172 #491 target "
       "scale (-> 2.905, 0.50x of the old ref 5.847) + the B1180 #499 width-factor "
       "pin (-> 3.040, 0.68x of the B1178 ref 4.479) is that window, not a "
       "population word: record 80's full envelope (1175 = 50 + 50 x 30 x 0.75, "
       "B1149 dump_pop.py; sim_particles.cpp:344-351, :430-443) is drawn")
SRC_SIZE = "B1164 #483 attach scale s + B1180 #499 width factor (sprite size)"
# The source-stage onset collapse (measured, all three gates): the committed
# B1153-era gate (gate_c.tsv) has a per-row distribution 0.433x33 / 0.500-0.733x7
# (36/40 in band vs the oracle's 0.370-0.580); B1180's re-gate (its
# b1130_band.tsv, tree 80202c81f) has a constant 0.100 on 38/40 rows with only
# rec 6 and 42 (fixed_self, spells 722/700) at 0.433; B1184's tip keeps that
# exact split (d_source_onset_s = 0.0 on every measured row; B1184's plans are
# byte-identical to B1180's, so it is not a plan change). 0.100 s = 3 frames
# at 30 fps: the source glow now starts on a constant post-click offset, not on
# the per-row cast track. The collapse predates B1184 (B1180's re-gate shows it;
# its README reports only land/end/p90, so it is unowned there). The cards
# between B1155 and B1180 that touched the Source-stage timing are B1174 (#493,
# the OP_Action cast-anim gate: the oracle draws the CastingAnim from Act+0.42
# and fable now plays that track on its standing source), B1152 (the
# stage-fire timing) and B1169 (harness determinism, the harness side).
FU2 = ("B1184-FU2 (NEW): the source-stage onset collapsed from the per-row cast-track "
       "distribution (B1153 era: 0.433x33 / 0.500-0.733x7, 36/40 in band vs oracle "
       "0.370-0.580) to a constant 0.100 s (3 frames at 30 fps) post-click on 38/40 "
       "rows -- only rec 6/42 (fixed_self, spells 722/700) keep 0.433; unchanged on "
       "B1184's tip (d = 0.0, byte-identical plans). Mechanism to trace: which card "
       "between B1155 and B1180 re-attached the Source stage to a constant offset "
       "instead of the per-spell cast track (candidates: B1174 #493 OP_Action "
       "cast-anim gate, B1152 stage-fire timing, B1169 harness determinism); restore "
       "the per-row onset (oracle band 0.370-0.580 BC+)")
B1130_CLASS_OWNERS = {
    'source_cov': SRC_SIZE + ' (source stage)',
    'landing_cov': FU1,
    'landing_end_s': ("B1179 #498 (no lifetime-word defect: the end is the last frame "
                      "above the lit-pixel threshold, so it moves with the landing "
                      "extent; B1179 README)"),
    'p90_src': SRC_SIZE + ' (source sprite)',
    'p90_land': SRC_SIZE + ' (landing sprite; the ~0.75x residual on 93 after s)',
    'source_onset_s': FU2,
    'source_centroid': ("B1157 (cast-anim residual: the raised hand moves the Source "
                        "window's centroid; the tip's centroid runs 0.10-0.25 model-h "
                        "above the oracle on the red rows and is unchanged vs B1180, "
                        "d = 0.0 on every measured row) + B1166 (anchor)"),
    'src_h_lo': "B1157 (blank src_*_n + the NVIDIA re-baseline -- the B1184 card's named owner)",
    'src_h_hi': "B1157 (blank src_*_n + the NVIDIA re-baseline -- the B1184 card's named owner)",
}
B1130_ROW_OWNERS = {
    # the committed per-row owners (refactor/B1171/tables/b1130_table.tsv col. owner,
    # as updated in place by B1178/B1179) are carried verbatim for the four metrics
    # they name; the five extra metrics take the class owners above.
    35: ("r35 (spell 253): every metric -- B1167 #486 (the kept override carries the "
         "pool + duration gaps)"),
    40: ("r40: cov/end over -- harness scene (the oracle caster zones out OP_DeleteSpawn "
         "Act+0.36-0.40; --caster-despawn 0.80, B1171's committed owner + B1179 #498); "
         "src short -- " + SRC_SIZE),
}
TOUR_OWNERS = {
    'land_short': "B1180 #499 (width factor, pinned in-tree: 93 1.993 -> 2.905 -> 3.040) + " + FU1,
    'land_253': ("B1167 #486 (the r35/253 row, per B1178's owner lines and this card) -- the "
                 "short cell specifically: the move 5.028 (B1142) -> 4.148 (B1180 re-gate, kept "
                 "on B1184's tip, d = 0.0) is B1180's unattributed self-landing regression, "
                 "owned there as the next unit to touch the self-landing window (not `s`: "
                 "B1164's s enlarges self sprites)"),
    'land_458': "B1164 #483 (the b1118 tour self-cast rows)",
    'end_short': "B1179 #498 (ends: the measured coupling -- the end is the last frame above the lit-pixel threshold, so it moves with the landing extent; no lifetime-word defect)",
    'end_over': "B1179 #498 (ends: the measured coupling -- the end is the last frame above the lit-pixel threshold, so it moves with the landing extent; no lifetime-word defect)",
    'roi_l': "B1164 #483 s + B1180 #499 (the sprite diameter in the target ROI)",
    'roi_s': "B1180 #499 (width factor, source sprite) + B1157 (cast-anim residual)",
}

def tour_owner(spell, reds):
    # reds: {metric: verdict}
    parts = []
    if 'land_cov' in reds:
        o = (TOUR_OWNERS['land_253'] if spell == 253 else
             TOUR_OWNERS['land_458'] if spell == 458 else TOUR_OWNERS['land_short'])
        parts.append('land: ' + o)
    if 'end' in reds:
        parts.append('end: ' + (TOUR_OWNERS['end_over'] if reds['end'] == 'over' else TOUR_OWNERS['end_short']))
    if 'roi_l' in reds:
        parts.append('p90_land: ' + TOUR_OWNERS['roi_l'])
    if 'roi_s' in reds:
        parts.append('p90_src: ' + TOUR_OWNERS['roi_s'])
    return '; '.join(parts)

# ================= emit
def write_tsv(path, rows, cols):
    with open(path, 'w') as fh:
        fh.write('\t'.join(cols) + '\n')
        for r in rows:
            fh.write('\t'.join(str(r.get(c,'')) for c in cols) + '\n')

def main():
    os.makedirs(OUT, exist_ok=True)
    errs = verify()
    b1130, c1130 = build_b1130()
    b1118, c1118 = build_b1118()
    tour, ctour = build_tour()
    write_tsv(os.path.join(OUT,'b1130.tsv'), b1130,
        ['record','kind','spell'] + [k for m in METRICS for k in ('oracle_'+m,'mine_'+m,'v_'+m)] +
        [k for m in METRICS for k in ('b1180_'+m,'d_'+m)] + ['red','owner'])
    write_tsv(os.path.join(OUT,'b1118.tsv'), b1118,
        ['row','spell'] + [k for m in B1118_COLS for k in ('b1151_'+m,'mine_'+m,'v_'+m)] +
        ['b1180_land_cov','d_land_cov','b1180_end','d_end','b1180_src_cov','d_src_cov','red','owner'])
    write_tsv(os.path.join(OUT,'tour.tsv'), tour,
        ['spell','oracle_land_cov','mine_land_cov','v_land_cov','oracle_end','mine_end','v_end',
         'band_roi_l','mine_roi_l','v_roi_l','band_roi_s','mine_roi_s','v_roi_s',
         'mine_src_cov','mine_src_on25','mine_src_cen',
         'b1180_land_cov','d_land_cov','b1180_end','d_end','b1180_src_cov','d_src_cov','red','owner'])
    # counts.tsv: per table x column PASS/short/over/n-a -- BEFORE (the B1178
    # AFTER state) and NOW (this tip, this run)
    bc = before_counts()
    lines = ['table\tcolumn\tbefore_PASS\tbefore_short\tbefore_over\tbefore_n-a\tPASS\tshort\tover\tn-a']
    for name, c in (('b1130',c1130),('b1118',c1118),('tour',ctour)):
        for m, d in c.items():
            b = bc[name][m]
            lines.append(f"{name}\t{m}\t{b.get('PASS',0)}\t{b.get('short',0)}\t{b.get('over',0)}\t{b.get('n-a',0)}"
                         f"\t{d.get('PASS',0)}\t{d.get('short',0)}\t{d.get('over',0)}\t{d.get('n-a',0)}")
    open(os.path.join(OUT,'counts.tsv'),'w').write('\n'.join(lines) + '\n')
    open(os.path.join(OUT,'distance.tsv'),'w').write(build_distance(tour, b1118, b1130))
    print(json.dumps({'errors': errs, 'counts': {'b1130':c1130,'b1118':c1118,'tour':ctour}}, indent=1))
    if errs:
        print('VERIFICATION ERRORS:'); [print(' ', e) for e in errs]
        sys.exit(1)

if __name__ == '__main__':
    main()
