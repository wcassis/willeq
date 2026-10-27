#!/usr/bin/env python3
"""B1170 (#489): frame identity of the re-gate renders, before vs after.

usage (ON THE THINKPAD, stdlib only): python3 -I regate_identity.py <before_dir> <after_dir> <out.tsv>
For every run directory present on both sides: frames per side, frames whose
PNG bytes differ (md5), the first and last differing frame index, and the
first OP_Action / B1152 / B1170 log facts of each side's run.log.
"""
import hashlib
import os
import re
import sys


def md5(p):
    with open(p, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def frames(d):
    return {n: os.path.join(d, n) for n in os.listdir(d) if n.startswith('frame_') and n.endswith('.png')}


def facts(d):
    p = os.path.join(d, 'run.log')
    if not os.path.exists(p):
        return '-'
    t = open(p, errors='replace').read()
    return 'b1152_missile=%d b1170_missile=%d deferred=%d' % (
        len(re.findall(r'B1152 missile', t)), len(re.findall(r'B1170 missile', t)),
        len(re.findall(r'deferring to the in-flight missile', t)))


def main():
    a, b, out = sys.argv[1:4]
    rows = ['run\tframes_before\tframes_after\tdiffering\tfirst\tlast\tbefore_log\tafter_log']
    for run in sorted(set(os.listdir(a)) & set(os.listdir(b))):
        da, db = os.path.join(a, run), os.path.join(b, run)
        if not os.path.isdir(da) or run == 'plans':
            continue
        fa, fb = frames(da), frames(db)
        diff = sorted(int(n[6:11]) for n in set(fa) & set(fb) if md5(fa[n]) != md5(fb[n]))
        rows.append('%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s' % (
            run, len(fa), len(fb), len(diff), diff[0] if diff else '-', diff[-1] if diff else '-',
            facts(da), facts(db)))
    open(out, 'w').write('\n'.join(rows) + '\n')
    print('\n'.join(rows))


if __name__ == '__main__':
    main()
