#!/usr/bin/env python3
# B1192 (#511) -- extract the measured LOOT row of B1183's +0x251 table.
#
# The oracle column is READ from the client's memory per step (WILL'S RULE):
# b1183_poll.py polls the caster's local spawn +0x251 (WR lines) and walks
# the spawn list for non-local spawns (XNEW/XWR lines, +0x244 corpse flag).
# The loot gate 28061/28062 writes 105 to the USE TARGET's +0x251 -- the
# corpse's byte moves, the looter (local) holds 100 (B1183, b1183_poll.py
# header). So the row is the XWR transition INTO 105 on a flagged corpse.
#
# Usage: extract_loot_row.py <poll.log> <fable_step_table.tsv> [timeline.tsv]
# Output on stdout: the row (TSV) + the cited log lines; exit 1 if the
# transition is absent (an absent transition is a finding, not a fallback).
import re
import sys

poll, fable = sys.argv[1], sys.argv[2]
timeline = sys.argv[3] if len(sys.argv) > 3 else None

xwr = []
xnew = []
wr_local = []
init = []
for ln in open(poll):
    m = re.match(r'XWR t=(\S+) (\d+) -> (\d+)  spawn=0x([0-9a-f]+) flag=(\d+)', ln)
    if m:
        xwr.append(m.groups()); continue
    m = re.match(r'XNEW t=(\S+) spawn=0x([0-9a-f]+) flag=(\d+) byte=(\d+)', ln)
    if m:
        xnew.append(m.groups()); continue
    m = re.match(r'WR t=(\S+) (\S+) -> (\d+)  spawn=0x([0-9a-f]+)', ln)
    if m:
        wr_local.append(m.groups()); continue
    m = re.match(r'INIT byte \+0x251 = (\d+)', ln)
    if m:
        init.append(m.group(1))

# The corpse: an XNEW or XWR spawn with flag 2 (the death handler sets
# +0x244 = 2). Its transition INTO 105 is the loot gate.
corpse_trans = [t for t in xwr if t[4] == '2' and t[2] == '105']
if not corpse_trans:
    print('NO-LOOT-TRANSITION: no XWR into 105 on a flagged corpse', file=sys.stderr)
    print('xnew: %r' % xnew, file=sys.stderr)
    print('xwr:  %r' % xwr, file=sys.stderr)
    sys.exit(1)
t = corpse_trans[-1]  # the last one is the measured loot open
oracle_byte = t[2]

# The looter (local): must hold 100 -- no local WR away from 100.
local_strays = [w for w in wr_local if w[2] != '100']
if local_strays:
    print('LOOTER-NOT-100: %r' % local_strays, file=sys.stderr)
    sys.exit(1)

fable_loot = None
for ln in open(fable):
    p = ln.rstrip('\n').split('\t')
    if len(p) >= 3 and p[0] == 'loot':
        fable_loot = p[2]
if fable_loot is None:
    print('no loot row in %s' % fable, file=sys.stderr)
    sys.exit(1)

use_t = ''
if timeline:
    for ln in open(timeline):
        p = ln.rstrip('\n').split('\t')
        if len(p) >= 3 and p[2] == 'use_click':
            use_t = p[1]

print('step\tfeed\tfable\toracle\tevidence')
print('loot\tloot open (28061)\t%s\t%s\tXWR t=%s %s -> %s spawn=0x%s flag=%s; looter INIT %s, no local WR off 100%s' % (
    fable_loot, oracle_byte, t[0], t[1], t[2], t[3], t[4], '/'.join(init),
    ' (use_click t=%s)' % use_t if use_t else ''))
print()
print('cited poll.log lines:')
for ln in open(poll):
    if ln.startswith(('XNEW', 'XWR', 'INIT byte', 'END ')) or re.match(r'WR t=', ln):
        print(ln.rstrip('\n'))
