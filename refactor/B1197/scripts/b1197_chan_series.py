#!/usr/bin/env python3
"""B1197: extended per-channel / per-frame / ROI table for a record pair
(willeq harness frames vs oracle video frames), both sides, one method.

Extends B1191's b1191_check_pair.py (branch task/kb-b1191-green-channel,
refactor/B1191/scripts/b1191_check_pair.py) with:
  * per-frame series across the landing window (not only the peak): per
    channel means AND per-channel 16-bin histograms over lit80 and px>150
    (the sets, thresholds and per-side tails are B1191's: lit80 = max channel
    diff vs the tail-median baseline > 80; px>150 > 150; the baseline is the
    median of the side's own last <tail> frames);
  * the blue-channel ratio and the pixel-count ratio (peak + window);
  * a per-frame time-aligned willeq/oracle G and B ratio over the window
    (aligned by each side's own lit80-peak frame);
  * the ROI split: core box (the b1130 gate's 209x209 window) / halo (the
    418x418 box minus core) / ground (the rest of the frame), each box
    centred on the side's own px>150 peak-frame centroid -- where the
    oracle's extra pixels live.

usage: b1197_chan_series.py <wil_dir> <orc_dir> <wil_glob> <orc_glob> <tail_w> <tail_o> [tsv_out]
       defaults follow B1191's committed runs: 'frame_*.png' / 'f*.png',
       tails 30 / 38 (willeq tail 30 for every record; oracle tail 38 for the
       ~201-frame b1130 window).

The tsv_out (optional) carries the full per-frame histograms: one row per
(side, frame, set) with n, R/G/B means and the three 16-bin histograms.
"""
import os
import sys
import glob

import numpy as np
import cv2

wd, od, wg, og, tw, to = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6])
tsv_out = sys.argv[7] if len(sys.argv) > 7 else None
FPS_W, FPS_O = 30.0, 25.0  # harness 30 fps, oracle video 25 fps (B1130 capture)
WINDOW_MIN_N = 50  # B1191's: a frame/set with <= 50 px is not tabled


def files(d, g):
    return sorted(glob.glob(os.path.join(d, g)),
                  key=lambda p: int(''.join(c for c in os.path.basename(p) if c.isdigit())))


def load(p):
    img = cv2.imread(p, cv2.IMREAD_COLOR)
    assert img is not None, f'unreadable frame {p}'
    return img[:, :, ::-1].astype(np.float32)


def analyze(d, g, tail, fps):
    fs = files(d, g)
    frames = [load(p) for p in fs]
    base = np.median(np.stack(frames[-tail:]), axis=0)
    rows = []
    for i, im in enumerate(frames):
        df = np.clip(im - base, 0, 255)
        mx = df.max(axis=2)
        rec = {'i': i, 't': i / fps, 'df': df}
        for nm, thr in (('lit80', 80), ('px150', 150)):
            m = mx > thr
            rec[nm] = {
                'n': int(m.sum()),
                'mask': m,
                'mean': df[m].mean(axis=0) if m.any() else np.zeros(3),
                'hist': [np.histogram(df[m, c], bins=16, range=(0, 255))[0].astype(int)
                         for c in range(3)] if m.any() else [np.zeros(16, dtype=int) for _ in range(3)],
            }
        rows.append(rec)
    return base, rows


def window_of(rows):
    win = [r for r in rows if r['lit80']['n'] >= WINDOW_MIN_N]
    return win


def window_stats(rows, nm):
    """aggregate over the landing window: n + weighted channel means + histograms"""
    win = window_of(rows)
    n_px = sum(r[nm]['n'] for r in win)
    tot = np.array([sum(r[nm]['n'] * r[nm]['mean'][c] for r in win) for c in range(3)])
    m = tot / max(n_px, 1)
    hist = [np.array([sum(int(r[nm]['hist'][c][b]) for r in win) for b in range(16)])
            for c in range(3)]
    return n_px, m, hist, win


def side_table(rows, base, label, tail):
    print(f'\n===== {label} (tail {tail}) =====')
    pk = max(rows, key=lambda r: r['lit80']['n'])
    print(f'peak frame f{pk["i"]} (t={pk["t"]:.2f}s)')
    for nm in ('lit80', 'px150'):
        r = pk[nm]
        if r['n'] > WINDOW_MIN_N:
            mm = r['mean']
            print(f'  {nm}: px={r["n"]} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f} '
                  f'G/R={mm[1]/max(mm[0], 1e-9):.3f} B/G={mm[2]/max(mm[1], 1e-9):.3f}')
        else:
            print(f'  {nm}: px={r["n"]} (too few for stats)')
    win = window_of(rows)
    if not win:
        print('  no landing window (no frame with lit80 n >= 50)')
        return None
    print(f'landing window: f{win[0]["i"]}..f{win[-1]["i"]} ({len(win)} frames, '
          f't={win[0]["t"]:.2f}..{win[-1]["t"]:.2f}s)')
    for nm in ('lit80', 'px150'):
        n_px, m, hist, _ = window_stats(rows, nm)
        print(f'  {nm} window: n={n_px} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f} '
              f'G/R={m[1]/max(m[0], 1e-9):.3f} B/G={m[2]/max(m[1], 1e-9):.3f}')
        for c, cn in zip(range(3), 'RGB'):
            print(f'    {cn} hist16 (window): ' + ' '.join(str(int(v)) for v in hist[c]))
    return pk, win


def roi_split(rows, pk, label):
    # core = 209x209 (the b1130 gate window), centred on the peak frame's px>150
    # centroid; halo = 418x418 minus core; ground = the rest of the frame.
    m = pk['px150']['mask']
    H, W = m.shape
    if pk['px150']['n'] < WINDOW_MIN_N:
        print(f'  {label}: ROI skipped (peak px150 n={pk["px150"]["n"]})')
        return
    ys, xs = np.nonzero(m)
    cy, cx = int(ys.mean()), int(xs.mean())
    core = np.zeros((H, W), bool)
    core[max(0, cy - 104):min(H, cy + 105), max(0, cx - 104):min(W, cx + 105)] = True
    big = np.zeros((H, W), bool)
    big[max(0, cy - 208):min(H, cy + 209), max(0, cx - 208):min(W, cx + 209)] = True
    win = window_of(rows)
    print(f'  {label}: peak px150 centroid ({cx}, {cy}) on {W}x{H}; '
          f'core 209x209, halo 418x418-minus-core, ground the rest')
    for nm in ('lit80', 'px150'):
        for nm2, msk in (('core', core), ('halo', big & ~core), ('ground', ~big)):
            n_pk = int((pk[nm]['mask'] & msk).sum())
            # window: n + weighted per-pixel means (recomputed per ROI)
            n_win = 0
            tot = np.zeros(3)
            for r in win:
                mm = r[nm]['mask'] & msk
                n = int(mm.sum())
                if n:
                    n_win += n
                    df = r['df']
                    tot += df[mm].sum(axis=0)
            mw = tot / max(n_win, 1)
            print(f'  {nm}: {nm2:8s} peak px={n_pk:7d} | window px={n_win:8d} '
                  f'R={mw[0]:6.1f} G={mw[1]:6.1f} B={mw[2]:6.1f}')


def tsv_write(rows, side, fh):
    for r in rows:
        for nm in ('lit80', 'px150'):
            d = r[nm]
            line = [side, str(r['i']), f'{r["t"]:.4f}', nm, str(d['n']),
                    f'{d["mean"][0]:.3f}', f'{d["mean"][1]:.3f}', f'{d["mean"][2]:.3f}']
            for c in range(3):
                line += [str(int(v)) for v in d['hist'][c]]
            fh.write('\t'.join(line) + '\n')


def main():
    tsv_fh = open(tsv_out, 'w') if tsv_out else None
    if tsv_fh:
        tsv_fh.write('side\tframe\tt_s\tset\tn\tR_mean\tG_mean\tB_mean\t'
                     + '\t'.join(f'{cn}bin{b}' for cn in 'RGB' for b in range(16)) + '\n')
    nw, no = len(files(wd, wg)), len(files(od, og))
    print(f'willeq dir={wd} ({nw} frames)  oracle dir={od} ({no} frames)')
    base_w, rows_w = analyze(wd, wg, tw, FPS_W)
    base_o, rows_o = analyze(od, og, to, FPS_O)
    print(f'willeq tail-{tw} median R/G/B = ' + ' '.join(f'{v:.1f}' for v in base_w.mean(axis=(0, 1))))
    print(f'oracle tail-{to} median R/G/B = ' + ' '.join(f'{v:.1f}' for v in base_o.mean(axis=(0, 1))))
    out_w = side_table(rows_w, base_w, 'WILLEQ', tw)
    out_o = side_table(rows_o, base_o, 'ORACLE', to)
    if tsv_fh:
        tsv_write(rows_w, 'willeq', tsv_fh)
        tsv_write(rows_o, 'oracle', tsv_fh)
        tsv_fh.close()
    if not (out_w and out_o):
        return
    pk_w, win_w = out_w
    pk_o, win_o = out_o

    print('\n===== peak-frame ratios (willeq / oracle) =====')
    for nm in ('lit80', 'px150'):
        nw8, no8 = pk_w[nm]['n'], pk_o[nm]['n']
        mw, mo = pk_w[nm]['mean'], pk_o[nm]['mean']
        print(f'  {nm}: px {nw8}/{no8} = {nw8/max(no8, 1):.3f}x | '
              f'R {mw[0]:.1f}/{mo[0]:.1f} = {mw[0]/max(mo[0], 1e-9):.3f} | '
              f'G {mw[1]:.1f}/{mo[1]:.1f} = {mw[1]/max(mo[1], 1e-9):.3f} | '
              f'B {mw[2]:.1f}/{mo[2]:.1f} = {mw[2]/max(mo[2], 1e-9):.3f}')

    print('\n===== window ratios (willeq / oracle) =====')
    for nm in ('lit80', 'px150'):
        n1, m1, _, _ = window_stats(rows_w, nm)
        n2, m2, _, _ = window_stats(rows_o, nm)
        print(f'  {nm}: px {n1}/{n2} = {n1/max(n2, 1):.3f}x | '
              f'R {m1[0]:.1f}/{m2[0]:.1f} = {m1[0]/max(m2[0], 1e-9):.3f} | '
              f'G {m1[1]:.1f}/{m2[1]:.1f} = {m1[1]/max(m2[1], 1e-9):.3f} | '
              f'B {m1[2]:.1f}/{m2[2]:.1f} = {m1[2]/max(m2[2], 1e-9):.3f}')

    dt = pk_w['t'] - pk_o['t']
    print(f'\n===== per-frame series, time-aligned (willeq t - oracle t = {dt:+.2f}s) =====')
    print('frame  t_w   t_o | lit80 n_w  n_o  ratio | px150 n_w  n_o  ratio | G_w  G_o ratio | B_w  B_o ratio')
    for r in win_w:
        to_t = r['t'] - dt
        cand = [q for q in win_o if abs(q['t'] - to_t) < 0.5 / FPS_O]
        if not cand:
            continue
        q = min(cand, key=lambda q: abs(q['t'] - to_t))
        nw8, no8 = r['lit80']['n'], q['lit80']['n']
        nw, no = r['px150']['n'], q['px150']['n']
        gw, go = r['px150']['mean'][1], q['px150']['mean'][1]
        bw, bo = r['px150']['mean'][2], q['px150']['mean'][2]
        print(f'f{r["i"]:3d} {r["t"]:6.2f} {q["t"]:6.2f} | {nw8:7d} {no8:7d} {nw8/max(no8, 1):5.2f} | '
              f'{nw:7d} {no:7d} {nw/max(no, 1):5.2f} | {gw:5.1f} {go:5.1f} {gw/max(go, 1e-9):5.2f} | '
              f'{bw:5.1f} {bo:5.1f} {bw/max(bo, 1e-9):5.2f}')

    print('\n===== ROI split (each side, its own peak centroid) =====')
    roi_split(rows_w, pk_w, 'WILLEQ')
    roi_split(rows_o, pk_o, 'ORACLE')
    print('\noracle-minus-willeq window px per ROI (each side, its own centroid box):')
    def roi_counts(rows):
        pk = max(rows, key=lambda r: r['lit80']['n'])
        m = pk['px150']['mask']
        H, W = m.shape
        ys, xs = np.nonzero(m)
        cy, cx = int(ys.mean()), int(xs.mean())
        core = np.zeros((H, W), bool)
        core[max(0, cy - 104):min(H, cy + 105), max(0, cx - 104):min(W, cx + 105)] = True
        big = np.zeros((H, W), bool)
        big[max(0, cy - 208):min(H, cy + 209), max(0, cx - 208):min(W, cx + 209)] = True
        win = window_of(rows)
        return {nm3: sum(int((r[nm]['mask'] & msk).sum()) for r in win)
                for nm3, msk in (('core', core), ('halo', big & ~core), ('ground', ~big))}

    for nm in ('lit80', 'px150'):
        rw, ro = roi_counts(rows_w), roi_counts(rows_o)
        print(f'  {nm}: core {rw["core"]}/{ro["core"]} (delta {ro["core"] - rw["core"]:+d}) | '
              f'halo {rw["halo"]}/{ro["halo"]} (delta {ro["halo"] - rw["halo"]:+d}) | '
              f'ground {rw["ground"]}/{ro["ground"]} (delta {ro["ground"] - rw["ground"]:+d}) '
              f'(willeq/oracle, window px)')


if __name__ == '__main__':
    main()
