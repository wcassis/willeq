#!/usr/bin/env python3
"""B1155: the 40-record sweep table, BANDED against review/B1130/record_table.tsv.

usage: band_table.py <evidence_dir> <out.tsv>

Reads <evidence_dir>/measure/<kind>_r<rec>/b1130_rows.tsv (harness_measure_b1130.py's
output, one row per record) and the B1130 oracle table, and writes one row per
record with, for each of the nine card columns, the oracle value, the harness
value, the band, and the verdict (PASS / short / over / n-a):

  source_cov, landing_cov      B1130 value +- 3 %          (the B1142 method: percent
                                                            deviation from the reference,
                                                            review/B1142/README.md "every
                                                            land_cov within -0.9..+2.6 %"
                                                            and its gate/regression_table.tsv
                                                            pct columns)
  landing_end_s                B1130 value +- 0.10 s       (B1149's verdict method:
                                                            "within +/- 0.1 s of the lane")
  p90_src, p90_land            B1130 value +- 15 %         (the B1142 method: the
                                                            roi_*_band columns of
                                                            gate/regression_table.tsv are
                                                            the oracle roi p90 +- 15 %)
  source_onset_s               B1130 value +- 0.10 s       (B1131/B1132)
  source_centroid, src_h_lo,
  src_h_hi                     B1130 value +- 0.15         (B1131/B1132)

Oracle columns read from record_table.tsv: source_cov, landing_cov, landing_end,
source_onset, source_centroid ("fixed X / close Y" -> X), source_extent
("h lo..hi" -> lo and hi), sprite_p90_src, sprite_p90_land ("-" -> n/a)."""
import sys, os, re, math
EV, out = sys.argv[1], sys.argv[2]
TABLE = '/mnt/projects/fable/review/B1130/record_table.tsv'

def f(x):
    try: return float(x)
    except (ValueError, TypeError): return None

oracle = {}
for line in open(TABLE):
    c = line.rstrip('\n').split('\t')
    if c[0] == 'record': continue
    rec = int(c[0])
    m = re.search(r'h ([\d.]+)\.\.([\d.]+)', c[17])   # source_extent ("h 0.10..1.86")
    mc = re.search(r'fixed ([\d.]+)', c[18])          # source_centroid ("fixed 0.55 / close 0.5")
    oracle[rec] = dict(
        spell_id=int(c[5]),    # the EQ spell id (the harness casts it; the record is B1130's index)
        kind='fixed_npc' if 'fixed_npc' in c[11] else 'fixed_self',  # captures (col 11)
        landing_end_s=f(c[22]),      # landing_end_s (METRICS name, not "landing_end")
        source_onset_s=f(c[14]),     # source_onset_s (METRICS name, not "source_onset")
        source_cov=f(c[16]),       # source_cov
        source_centroid=f(mc.group(1)) if mc else None,
        src_h_lo=f(m.group(1)) if m else None, src_h_hi=f(m.group(2)) if m else None,
        landing_cov=f(c[23]),      # landing_cov
        p90_src=f(c[26]),          # sprite_p90_src
        p90_land=f(c[27]))         # sprite_p90_land

def band(metric, o):
    if metric in ('source_cov', 'landing_cov'): return 0.03 * o          # relative
    if metric in ('p90_src', 'p90_land'):      return 0.15 * o          # relative
    if metric == 'landing_end_s':              return 0.10              # absolute
    if metric == 'source_onset_s':             return 0.10              # absolute
    return 0.15                                                        # absolute (centroid, lo, hi)

METRICS = ['source_cov', 'landing_cov', 'landing_end_s', 'p90_src', 'p90_land',
           'source_onset_s', 'source_centroid', 'src_h_lo', 'src_h_hi']

def verdict(metric, o, h):
    if o is None or h is None or math.isnan(h): return 'n-a'
    b = band(metric, o)
    if abs(h - o) <= b: return 'PASS'
    return 'short' if h < o - b else 'over'

rows = []
for rec in sorted(oracle):
    o = oracle[rec]
    tag = f"{o['kind']}_r{rec}"
    mroot = os.path.join(EV, 'measure', tag, 'b1130_rows.tsv')
    h = {}
    if os.path.exists(mroot):
        for line in open(mroot):
            c = line.rstrip('\n').split('\t')
            if c[0] == 'spell' or not c[0].isdigit(): continue
            if int(c[0]) == o['spell_id']:   # the harness cast the EQ spell id, not the record index
                h = dict(source_onset_s=f(c[1]), source_cov=f(c[2]), source_centroid=f(c[3]),
                         src_h_lo=f(c[4]), src_h_hi=f(c[5]),
                         landing_onset_s=f(c[6]), landing_end_s=f(c[7]), landing_cov=f(c[8]),
                         p90_src=f(c[9]), p90_land=f(c[10]))
    row = [rec, o['kind']]
    for mname in METRICS:
        ov, hv = o.get(mname), h.get(mname) if h else None
        row += [ov, hv, verdict(mname, ov, hv)]
    rows.append(row)

with open(out, 'w') as f:
    for mname in METRICS:
        f.write(f"oracle_{mname}\tharness_{mname}\tverdict_{mname}\t")
    f.write('\n')
    for row in rows:
        f.write('\t'.join('-' if x is None else (f'{x:.3f}' if isinstance(x, float) else str(x)) for x in row) + '\n')
print(open(out).read())
