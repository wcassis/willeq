#!/usr/bin/env python3
"""B1164 (#483) fix leg (review note 7941 finding 3): one b1130 row's landing_cov / landing_end /
source_cov across several renders (B1157's own measure dir, dev5's before, and the attribution
runs E1..E4), each read from the run's b1130_rows.tsv.
usage: python3 -I attrib_table.py <tag> <label>=<measure_root> [...]
(<measure_root>/<tag>/b1130_rows.tsv; B1157's tags are b1157_<kind>_r<n>, given as label=dir:prefix)"""
import sys

tag = sys.argv[1]
print('tag\trun\tlanding_cov\tlanding_end_s\tsource_cov\tlanding_onset_s')
for arg in sys.argv[2:]:
    lab, root = arg.split('=', 1)
    t = tag
    if ':' in root:
        root, pre = root.split(':')
        t = tag.replace('b1130_', pre + '_', 1)
    try:
        rows = [l.rstrip('\n').split('\t') for l in open(f'{root}/{t}/b1130_rows.tsv')]
    except OSError:
        print(f'{tag}\t{lab}\t(missing)')
        continue
    h, v = rows[0], rows[-1]
    d = dict(zip(h, v))
    print(f"{tag}\t{lab}\t{d['landing_cov']}\t{d['landing_end_s']}\t{d['source_cov']}\t{d['landing_onset_s']}")
