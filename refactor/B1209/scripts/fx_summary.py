#!/usr/bin/env python3
"""B1209 (#528): summarize fx_measure.py's frames.tsv into onset/peak/end.

usage: fx_summary.py <frames.tsv>

fx_measure.py (review/B1122/scripts/fx_measure.py) writes one row per video
frame: frame, epoch, t_bc, t_act, n_px, blobs, R, G, B, hue, sat, bbox,
centroid. A frame is LIT when n_px > the env FX_LIT (default 50) -- additive
sprites only ever brighten over the pre-cast baseline, so n_px is the lit
count. Prints and writes <frames.tsv>.summary.tsv:
  total_frames, frames_with_fx, lit_threshold, first/last/peak frame +
  t_act, peak_n_px, peak_hue, peak_bbox, peak_centroid, baseline_n_px_max.
"""
import sys, os
TH = int(os.environ.get('FX_LIT', '50'))
fn = sys.argv[1]
hdr = open(fn).readline().rstrip('\n').split('\t')
ci = {h: i for i, h in enumerate(hdr)}
all_lines = [l.rstrip('\n').split('\t') for l in open(fn) if l.strip()]
rows = all_lines[1:]
# fx_measure.py's frames.tsv columns: frame, epoch, t_bc, t_act, n_px, blobs,
# R, G, B, hue, sat, bbox, centroid. A frame is LIT when n_px > TH.
F, E, TB, TA, N = 'frame', 'epoch', 't_bc', 't_act', 'n_px'
lit = [r for r in rows if int(r[ci[N]]) > TH]
def get(r, col):
    return r[ci[col]]
out = {
    'total_frames': len(rows),
    'frames_with_fx': len(lit),
    'lit_threshold': TH,
}
if lit:
    peak = max(lit, key=lambda r: int(r[ci[N]]))
    out.update({
        'first_lit_frame': get(lit[0], F),
        'first_lit_t_act': get(lit[0], TA),
        'last_lit_frame': get(lit[-1], F),
        'last_lit_t_act': get(lit[-1], TA),
        'peak_frame': get(peak, F),
        'peak_t_act': get(peak, TA),
        'peak_n_px': get(peak, N),
        'peak_hue': get(peak, 'hue'),
        'peak_bbox': get(peak, 'bbox_x0,y0,x1,y1'),
        'peak_centroid': get(peak, 'centroid'),
        'baseline_n_px_max': max(int(r[ci[N]]) for r in rows if float(r[ci[TA]]) < 0),
    })
outfn = fn + '.summary.tsv'
outf = open(outfn, 'w')
outf.write('fact\tvalue\n')
for k in sorted(out):
    outf.write('%s\t%s\n' % (k, out[k]))
outf.close()
print(open(outfn).read())
