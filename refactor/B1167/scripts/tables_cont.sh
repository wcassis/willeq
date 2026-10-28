#!/usr/bin/env bash
# B1167 continuation (the ruling leg): the before (branch tip eff3de46, face-on
# actors) / after (this tip, the capture's actor headings applied) gate tables
# -> refactor/B1167/contintables/.
#
#   b1130_table.tsv     B1166's compare (before/after/oracle/b1157 + verdicts)
#                       joined with B1171's level-60 after columns
#                       (b1171_b1130_table.tsv, copied from origin/fable's
#                       refactor/B1171/tables/b1130_table.tsv);
#   b1118_table.tsv     B1167's compare (tour-7 + 40 records vs B1157 + lane 5);
#   owners_cont.tsv     one row per record x moved axis: direction vs the
#                       oracle (b1130) / lane 5 or B1157 (b1118) and the owner.
#
# inputs: /home/user/kb-evidence/kb-b1167-record104/sweeps_{before,after}H
# (beforeH frames = the eff3de46 model_viewer, afterH = this tip).
set -eu
EV=/home/user/kb-evidence/kb-b1167-record104
T=refactor/B1167/contintables
mkdir -p "$T"
python3 -P refactor/B1166/scripts/compare_b1130.py "$EV/sweeps_beforeH" "$EV/sweeps_afterH" "$T/b1130_table.tsv" > /dev/null
python3 -I refactor/B1167/scripts/compare_b1118.py "$EV/sweeps_beforeH" "$EV/sweeps_afterH" "$T/b1118_table.tsv" > /dev/null
git show origin/fable:refactor/B1171/tables/b1130_table.tsv > "$T/b1171_b1130_table.tsv"
python3 -I - "$T" <<'PY'
import sys, re
T = sys.argv[1]
def load(p, delim='\t'):
    rows = [l.rstrip('\n').split(delim) for l in open(p) if l.strip()]
    return rows[0], {r[0]: dict(zip(rows[0], r)) for r in rows[1:]}
def f(x):
    try: return float(x)
    except Exception: return None
h30, b30 = load(f'{T}/b1130_table.tsv')
_, b171 = load(f'{T}/b1171_b1130_table.tsv')
AX = ['landing_cov', 'landing_end_s', 'p90_land', 'source_cov']
with open(f'{T}/b1130_table.tsv', 'w') as o:
    cols = h30 + [f'b1171_{a}' for a in AX]
    o.write('\t'.join(cols) + '\n')
    for rec, r in b30.items():
        ref = b171.get(rec, {})
        o.write('\t'.join([r.get(c, '') for c in h30] + [ref.get(f'after_{a}', 'n/a') for a in AX]) + '\n')
h18, b18 = load(f'{T}/b1118_table.tsv')
own = ['record', 'kind', 'axis', 'before', 'after', 'oracle_or_b1157', 'b1171_or_lane5', 'direction', 'owner']
rows = []
for rec, r in b30.items():
    o171 = b171.get(rec, {})
    keymap = {'landing_cov': 'cov', 'landing_end_s': 'end', 'p90_land': 'p90', 'source_cov': 'src'}
    frags = dict(re.findall(r'(\w+ \w+): ([^;]+?)(?:;|$)', o171.get('owner', '')))
    for ax in AX:
        b, a, o = f(r[f'before_{ax}']), f(r[f'after_{ax}']), f(r[f'oracle_{ax}'])
        if b is None or a is None or abs(a - b) <= 0.001: continue
        near = o is not None and abs(a - o) < abs(b - o)
        d = 'nearer' if near else 'farther'
        if near:
            owner = 'B1167 continuation (the capture headings, this MR)'
        else:
            frag = frags.get(keymap[ax], '').strip()
            owner = (f'moved away by the heading; residual {ax}: '
                     f'{frag if frag else "unowned -- the row owner"}')
        rows.append([rec, r['kind'], ax, r[f'before_{ax}'], r[f'after_{ax}'],
                     r[f'oracle_{ax}'], r[f'b1171_{ax}'], d, owner])
for tag, r in b18.items():
    if r['moved'] == '-': continue
    for ax in ('land', 'end', 'src'):
        if ax not in r['moved'].split(','): continue
        b, a = f(r[f'{ax}_before']), f(r[f'{ax}_after'])
        if b is None or a is None: continue
        l5, b57 = (f(r[f'lane5_{ax}']) if f'lane5_{ax}' in r and r[f'lane5_{ax}'] else None), f(r[f'{ax}_b1157'])
        near = (l5 is not None and abs(a - l5) < abs(b - l5)) or \
               (l5 is None and b57 is not None and abs(a - b57) < abs(b - b57))
        d = 'nearer' if near else 'farther'
        owner = 'B1167 continuation (the capture headings, this MR)' if near else \
                f'moved away by the heading; residual {ax}: unowned -- the row owner'
        rows.append([tag, 'b1118', ax, r[f'{ax}_before'], r[f'{ax}_after'],
                     r[f'{ax}_b1157'], r.get(f'lane5_{ax}', ''), d, owner])
with open(f'{T}/owners_cont.tsv', 'w') as o:
    o.write('\t'.join(own) + '\n')
    for row in rows:
        o.write('\t'.join(row) + '\n')
print(f'owners: {len(rows)} rows, nearer {sum(r[7] == "nearer" for r in rows)}')
PY
echo "=== tables in $T:"
ls -la "$T"
