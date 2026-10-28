#!/usr/bin/env python3
"""B1178 (#497) FIX, finding 8 (RULED by the orchestrator): replace the stale
oracle-reference columns IN PLACE with this card's verified remeasure outputs.

No re-capture, no re-measure, no new values: the new values are the card's own
remeasure output (`record_table_remeasured.tsv`), already verified byte-identical
by the cold review (MR !332 note 8174). This script only PROPAGATES those
verified values into the three places the old values still sit.

Targets -- all oracle-reference side. Harness-side columns, goldens, manifests,
baselines and presets are NOT touched.

  1. refactor/B1157/b1130_table.tsv.after.tsv
       - the `oracle_p90_land` column (the 18 records the remeasure moved)
       - the derived `verdict_p90_land` column, recomputed with the band_table
         rules (refactor/B1155/scripts/band_table.py: p90_land = 15 % relative)
         from the NEW oracle value and the UNCHANGED harness value. A reference
         move that flips a verdict is an owned finding to print, not hide
         (refactor/B1178/scripts/gate_after.py, check #2) -- every flip is listed.
  2. /mnt/projects/fable/review/B1130/record_table.tsv  (the canonical oracle
     table, replaced in place) -- the four remeasured columns:
       target_centroid, sprite_p90_land, demand_landing, demand_overlap
  3. tests/spell_fx_population_oracle.h  (the consumer pin; a GENERATED header,
     updated by this committed script, not by hand) -- the `spriteP90Landing`
     field for the same 18 records.

Every changed cell is listed old -> new in the report file and on stdout. The
script validates that the input diff is EXACTLY the expected column/record set
and exits 1 on any unexpected diff, so it can never silently change a value it
was not told to. Old-value pre-checks compare numerically (the table and header
format the same number differently: 0.100 vs 0.1).

usage:
  python3 replace_b1130_refs.py [--after PATH] [--record-table PATH]
      [--remeasured PATH] [--test-header PATH] [--report PATH]

Defaults follow the B1087 absolute-path convention (the /mnt/projects evidence
root; the ThinkPad mounts it as live NFS, so running there applies the canonical
replacement to the real file).
"""
import argparse, math, re, sys


def repo_root():
    p = __file__
    return p[: p.index('refactor/B1178/scripts/')].rstrip('/')


ROOT = repo_root()


def label(path):
    """repo-relative for in-repo files (the report is committed, so it must not
    embed this worktree's absolute path); the canonical NFS path stays as-is."""
    if path.startswith(ROOT + '/'):
        return path[len(ROOT) + 1:]
    return path

# the four record_table.tsv columns the remeasure regenerated
REMEASURED_COLS = ['target_centroid', 'sprite_p90_land', 'demand_landing', 'demand_overlap']
# the record set the cold review verified as the sprite_p90_land movers
EXPECTED_P90_RECS = {1, 3, 7, 12, 13, 15, 16, 20, 24, 25, 26, 28, 29, 31, 33, 34, 35, 41}


def num(x):
    try:
        v = float(x)
        return v
    except (ValueError, TypeError):
        return None


def verdict_p90(o, h):
    """band_table.py rule for p90_land: 15 % relative band."""
    if o is None or h is None or math.isnan(h):
        return 'n-a'
    b = 0.15 * o
    if abs(h - o) <= b:
        return 'PASS'
    return 'short' if h < o - b else 'over'


def load_tsv(path):
    lines = open(path).read().split('\n')
    while lines and lines[-1] == '':
        lines.pop()
    hdr = lines[0].split('\t')
    rows = [ln.split('\t') for ln in lines[1:]]
    return hdr, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--after', default=f'{ROOT}/refactor/B1157/b1130_table.tsv.after.tsv')
    ap.add_argument('--record-table', default='/mnt/projects/fable/review/B1130/record_table.tsv')
    ap.add_argument('--remeasured',
                    default='/mnt/projects/fable/review/B1178/worker/b1130_remeasure/record_table_remeasured.tsv')
    ap.add_argument('--test-header', default=f'{ROOT}/tests/spell_fx_population_oracle.h')
    ap.add_argument('--report', default=f'{ROOT}/refactor/B1178/tables/b1130_refs_replacement.tsv')
    a = ap.parse_args()

    report = []  # (file, record, column, old, new)

    # ---- load the remeasured table (source of the new values) ----
    mh, mrows = load_tsv(a.remeasured)
    mrec = {r[0]: dict(zip(mh, r)) for r in mrows}
    # ---- load the canonical table (the in-place replacement target) ----
    ch, crows = load_tsv(a.record_table)
    crec = {r[0]: dict(zip(ch, r)) for r in crows}
    assert ch == mh, f'record_table header mismatch:\n{ch}\n{mh}'

    # ---- validate the diff is EXACTLY the expected column/record set ----
    changed_recs = {}
    for r in crows:
        rec = r[0]
        m = mrec[rec]
        diffs = {}
        for i, col in enumerate(ch):
            if col == 'record':
                continue
            if r[i] != m[col]:
                diffs[col] = (r[i], m[col])
        if diffs:
            changed_recs[rec] = diffs
    diff_cols = {col for d in changed_recs.values() for col in d}
    diff_p90_recs = {int(rec) for rec, d in changed_recs.items() if 'sprite_p90_land' in d}
    if diff_cols != set(REMEASURED_COLS):
        sys.exit(f'FATAL: remeasure diff columns {sorted(diff_cols)} != expected {sorted(REMEASURED_COLS)}')
    if diff_p90_recs != EXPECTED_P90_RECS:
        sys.exit(f'FATAL: sprite_p90_land records {sorted(diff_p90_recs)} != expected {sorted(EXPECTED_P90_RECS)}')
    percol = {c: sum(1 for d in changed_recs.values() if c in d) for c in sorted(REMEASURED_COLS)}
    print(f'validated remeasure diff: {percol} cells across {len(changed_recs)} records')

    # ---- 1. replace the canonical record_table.tsv in place ----
    new_crows = []
    for r in crows:
        r = list(r)
        m = mrec[r[0]]
        for i, col in enumerate(ch):
            if col in REMEASURED_COLS and r[i] != m[col]:
                report.append((label(a.record_table), r[0], col, r[i], m[col]))
                r[i] = m[col]
        new_crows.append(r)
    with open(a.record_table, 'w') as fh:
        fh.write('\t'.join(ch) + '\n' + '\n'.join('\t'.join(r) for r in new_crows) + '\n')

    # ---- 2. after.tsv: oracle_p90_land column + recomputed verdict_p90_land ----
    # data rows carry record + kind first; the 27 metric columns follow, so the
    # p90_land triple (oracle, harness, verdict) sits at data-cell indexes 14,15,16.
    alines = open(a.after).read().split('\n')
    while alines and alines[-1] == '':
        alines.pop()
    oI, hI, vI = 14, 15, 16
    new_alines = [alines[0]]
    flips = []
    for ln in alines[1:]:
        c = ln.split('\t')
        rec = c[0]
        if int(rec) in EXPECTED_P90_RECS:
            old_o = c[oI]
            if num(old_o) != num(crec[rec]['sprite_p90_land']):
                sys.exit(f'FATAL: after.tsv rec {rec} oracle_p90_land {old_o} != canonical '
                         f'{crec[rec]["sprite_p90_land"]}')
            new_p = mrec[rec]['sprite_p90_land']
            c[oI] = new_p
            old_v = c[vI]
            new_v = verdict_p90(num(new_p), num(c[hI]))
            c[vI] = new_v
            report.append((label(a.after), rec, 'oracle_p90_land', old_o, new_p))
            if old_v != new_v:
                flips.append((rec, old_v, new_v))
                report.append((label(a.after), rec, 'verdict_p90_land', old_v, new_v))
        new_alines.append('\t'.join(c))
    with open(a.after, 'w') as fh:
        fh.write('\n'.join(new_alines) + '\n')

    # ---- 3. test header: spriteP90Landing field (last float) for the 18 records ----
    tl = open(a.test_header).read().split('\n')
    rowre = re.compile(r'^(\s*\{\d+, [^,]+, [^,]+, [^,]+, [^,]+,) ([^,}]+)(\},\s*)$')
    newtl = []
    for ln in tl:
        m = rowre.match(ln)
        if m:
            rec = int(ln.split('{', 1)[1].split(',', 1)[0])
            if rec in EXPECTED_P90_RECS:
                old = m.group(2).strip()             # verbatim (e.g. "0.112f") for the report
                old_num = old.rstrip('f')            # numeric form for the pre-check
                if num(old_num) != num(crec[str(rec)]['sprite_p90_land']):
                    sys.exit(f'FATAL: test header rec {rec} spriteP90Landing {old} != canonical '
                             f'{crec[str(rec)]["sprite_p90_land"]}')
                # the generator writes f'{float(value)}f' -- match that format
                new = f'{num(mrec[str(rec)]["sprite_p90_land"])}f'
                ln = m.group(1) + ' ' + new + m.group(3)
                report.append((label(a.test_header), rec, 'spriteP90Landing', old, new))
        newtl.append(ln)
    with open(a.test_header, 'w') as fh:
        fh.write('\n'.join(newtl))

    # ---- report ----
    rep_lines = ['file\trecord\tcolumn\told\tnew']
    for f, rec, col, o, n in report:
        rep_lines.append(f'{f}\t{rec}\t{col}\t{o}\t{n}')
    with open(a.report, 'w') as fh:
        fh.write('\n'.join(rep_lines) + '\n')

    n_rt = sum(1 for r in report if r[0] == label(a.record_table))
    n_at = sum(1 for r in report if r[0] == label(a.after))
    n_th = sum(1 for r in report if r[0] == label(a.test_header))
    print(f'report: {len(report)} cells -> {a.report}')
    print(f'  record_table.tsv : {n_rt} cells')
    print(f'  after.tsv        : {n_at} cells ({len(EXPECTED_P90_RECS)} oracle_p90_land + {len(flips)} verdict_p90_land)')
    print(f'  test header      : {n_th} cells (spriteP90Landing)')
    print(f'  verdict_p90_land flips (owned findings, gate_after.py check #2): {flips if flips else "none"}')
    print('\nold -> new, every changed cell:')
    for f, rec, col, o, n in report:
        print(f'  {f.split("/")[-1]}  rec {rec:<3} {col:18} {o}  ->  {n}')


if __name__ == '__main__':
    main()
