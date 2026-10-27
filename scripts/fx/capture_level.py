#!/usr/bin/env python3
"""B1171 (#490): the caster level an oracle capture ran at, the level a harness
run cast at, and the refusal of a comparison row where the two differ.

Will's rule: willeq copies the official client exactly; a harness that casts at
a level the oracle did not is measuring the wrong thing. So the harness sweeps
never hard-code a level and never take the record table's `level` column (that
is the CLASS level the spell is learned at, 1 for most records): they read the
`#level N` the capture itself ran, from the capture's own files.

Oracle side (a lane-5 capture directory, e.g. review/B1130/oracle/r51):
  plan.tsv   the capture plan: `command<TAB>#level 60<TAB>...`
  steps.tsv  the steps the driver EXECUTED (action / target columns)
The level is the last `#level N` (or eqemu's `#set level N`) command; plan and
executed steps must agree, and a capture with no such step is an error (the
character's database level is not read here: never inferred).

Harness side (a model_viewer --spell-test frames directory):
  timing.tsv's `caster_level` column (B1171's harness writes it on every packet
  row); a run from an older binary has no such column, and the level is the
  run's plan file column 6 (caster_level), which the caller passes. A '-' or
  missing plan level is unknown (the binary's --caster-level default is not
  assumed).

Comparison: `refuse(oracle, harness)` raises LevelMismatch when they differ or
either is unknown; `verdict_or_refusal()` is the table-cell form.

CLI:
  capture_level.py capture <capture_dir>                -> N
  capture_level.py b1130 <record> [--table T] [--oracle-root R]   -> N
  capture_level.py b1118 <spell> [--fx-table T] [--oracle-root R] -> N
  capture_level.py harness <frames_dir> [--plan P]      -> N (exit 4 if unknown)
  capture_level.py check <oracle_level> <harness_level> -> exit 3 when refused
"""
import argparse
import os
import re
import sys

REVIEW = '/mnt/projects/fable/review'
B1130_TABLE = REVIEW + '/B1130/record_table.tsv'
B1130_ORACLE = REVIEW + '/B1130/oracle'
B1118_FX_TABLE = REVIEW + '/B1118/spell_fx_table.tsv'
B1118_ROOT = REVIEW + '/B1118'

_LEVEL_RE = re.compile(r'^#(?:set )?level (\d+)\s*$')


class LevelError(Exception):
    """The level cannot be read (missing file / step, plan vs steps disagree)."""


class LevelMismatch(Exception):
    """A comparison row whose harness level differs from the oracle row's."""


def _rows(path):
    with open(path) as f:
        return [line.rstrip('\n').split('\t') for line in f if line.strip() and not line.startswith('#')]


def _levels(rows, action_col, text_col):
    out = []
    for r in rows:
        if len(r) > text_col and r[action_col] == 'command':
            m = _LEVEL_RE.match(r[text_col].strip())
            if m:
                out.append(int(m.group(1)))
    return out


def capture_level(capture_dir):
    """The `#level` a lane-5 capture ran at: plan.tsv and steps.tsv must agree."""
    plan = os.path.join(capture_dir, 'plan.tsv')
    steps = os.path.join(capture_dir, 'steps.tsv')
    for p in (plan, steps):
        if not os.path.isfile(p):
            raise LevelError(f'{p}: missing')
    planned = _levels(_rows(plan), 0, 1)
    srows = _rows(steps)
    if not srows or srows[0][:5] != ['step', 'begin_epoch', 'end_epoch', 'action', 'target']:
        raise LevelError(f'{steps}: not a capture steps.tsv (header {srows[0][:5] if srows else None})')
    ran = _levels(srows[1:], 3, 4)
    if not ran:
        raise LevelError(f'{steps}: no executed #level step (planned {planned})')
    if not planned or planned[-1] != ran[-1]:
        raise LevelError(f'{capture_dir}: plan #level {planned} != executed #level {ran}')
    return ran[-1]


def b1130_record_capture(record, table=B1130_TABLE):
    rows = _rows(table)
    hdr = rows[0]
    for r in rows[1:]:
        d = dict(zip(hdr, r))
        if d['record'] == str(record):
            return d['capture']
    raise LevelError(f'{table}: no record {record}')


def b1130_record_level(record, table=B1130_TABLE, oracle_root=B1130_ORACLE):
    """The level B1130 record `record`'s capture (column `capture`) ran at."""
    cap = b1130_record_capture(record, table)
    return capture_level(os.path.join(oracle_root, cap))


def b1118_spell_capture(spell, fx_table=B1118_FX_TABLE):
    """The B1118 oracle leg a tour spell's oracle rows come from (the `video`
    column of spell_fx_table.tsv, e.g. oracle_druid/video.mp4); every oracle
    row of the spell must name the same leg."""
    rows = _rows(fx_table)
    hdr = rows[0]
    legs = set()
    for r in rows[1:]:
        d = dict(zip(hdr, r))
        if d.get('spell') == str(spell) and d.get('client') == 'oracle':
            legs.add(d['video'].split('/')[0])
    if len(legs) != 1:
        raise LevelError(f'{fx_table}: spell {spell} oracle legs {sorted(legs)} (want exactly one)')
    return legs.pop()


def b1118_spell_level(spell, fx_table=B1118_FX_TABLE, root=B1118_ROOT):
    return capture_level(os.path.join(root, b1118_spell_capture(spell, fx_table)))


def plan_level(plan):
    """The caster_level of a harness plan (every cast row must agree); None = unknown."""
    rows = _rows(plan)
    levels = set()
    for r in rows:
        if r and r[0] == 't_begin_s':
            continue
        lv = r[5] if len(r) > 5 else ''
        levels.add(None if lv in ('', '-') else int(lv))
    if len(levels) != 1:
        raise LevelError(f'{plan}: caster levels {sorted(levels, key=str)} (want exactly one)')
    return levels.pop()


def harness_level(frames_dir, plan=None):
    """(level, source) of a harness run: timing.tsv's caster_level column, else
    the plan's column 6. level None = unknown."""
    timing = os.path.join(frames_dir, 'timing.tsv')
    if not os.path.isfile(timing):
        raise LevelError(f'{timing}: missing')
    with open(timing) as f:
        rows = [line.rstrip('\n').split('\t') for line in f]
    if not rows:
        raise LevelError(f'{timing}: empty (run unfinished?)')
    hdr = rows[0]
    if 'caster_level' in hdr:
        i, ev = hdr.index('caster_level'), hdr.index('event')
        levels = {int(r[i]) for r in rows[1:] if len(r) > i and r[ev] != '-'}
        if len(levels) != 1:
            raise LevelError(f'{timing}: caster levels {sorted(levels)} (want exactly one)')
        return levels.pop(), 'timing.tsv'
    if plan is None:
        return None, 'unknown (no caster_level column, no plan)'
    return plan_level(plan), 'plan ' + os.path.basename(plan)


def refuse(oracle_level, harness_level_):
    """Raise LevelMismatch unless both levels are known and equal."""
    if oracle_level is None or harness_level_ is None:
        raise LevelMismatch(f'level unknown: oracle {oracle_level}, harness {harness_level_}')
    if int(oracle_level) != int(harness_level_):
        raise LevelMismatch(f'harness level {harness_level_} != oracle level {oracle_level}')


def verdict_or_refusal(oracle_level, harness_level_, verdict):
    """The table cell: the verdict, or REFUSED(L<h>!=<o>) for a mismatched row."""
    try:
        refuse(oracle_level, harness_level_)
    except LevelMismatch:
        return f'REFUSED(L{harness_level_}!={oracle_level})'
    return verdict


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('capture'); s.add_argument('dir')
    s = sub.add_parser('b1130'); s.add_argument('record')
    s.add_argument('--table', default=B1130_TABLE); s.add_argument('--oracle-root', default=B1130_ORACLE)
    s = sub.add_parser('b1118'); s.add_argument('spell')
    s.add_argument('--fx-table', default=B1118_FX_TABLE); s.add_argument('--oracle-root', default=B1118_ROOT)
    s = sub.add_parser('harness'); s.add_argument('frames'); s.add_argument('--plan')
    s = sub.add_parser('check'); s.add_argument('oracle'); s.add_argument('harness')
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'capture':
            print(capture_level(a.dir))
        elif a.cmd == 'b1130':
            print(b1130_record_level(a.record, a.table, a.oracle_root))
        elif a.cmd == 'b1118':
            print(b1118_spell_level(a.spell, a.fx_table, a.oracle_root))
        elif a.cmd == 'harness':
            lv, src = harness_level(a.frames, a.plan)
            if lv is None:
                print(f'unknown: {src}', file=sys.stderr)
                return 4
            print(lv)
        elif a.cmd == 'check':
            refuse(int(a.oracle), int(a.harness))
            print(f'level {a.harness} == oracle {a.oracle}')
    except LevelMismatch as e:
        print(f'REFUSED: {e}', file=sys.stderr)
        return 3
    except (LevelError, OSError, ValueError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
