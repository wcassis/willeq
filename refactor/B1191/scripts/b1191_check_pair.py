#!/usr/bin/env python3
"""Check frame dims + per-channel for a record pair. usage: check_pair.py <wil_dir> <orc_dir> <wil_glob> <orc_glob> <tail_w> <tail_o>"""
import os, sys, glob
import numpy as np, cv2
wd, od, wg, og, tw, to = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6])
def files(d,g): return sorted(glob.glob(os.path.join(d,g)), key=lambda p:int(''.join(c for c in os.path.basename(p) if c.isdigit())))
def load(p): return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
wf, of = files(wd,wg), files(od,og)
wi, oi = load(wf[0]), load(of[0])
print(f'willeq frame {wi.shape[1]}x{wi.shape[0]}  oracle frame {oi.shape[1]}x{oi.shape[0]}')
def peak(d,g,tail):
    fs=files(d,g); imgs=[load(p) for p in fs]
    base=np.median(np.stack([i for i in imgs[-tail:]]),axis=0)
    best=(0,-1)
    for i,im in enumerate(imgs):
        n=int((np.clip(im-base,0,255).max(axis=2)>80).sum())
        if n>best[1]: best=(i,n)
    i=best[0]; df=np.clip(imgs[i]-base,0,255); mx=df.max(axis=2)
    out=[f'  peak f{i}']
    for nm,thr in (('lit80',80),('px150',150)):
        m=mx>thr
        if m.sum()>50:
            mm=df[m].mean(axis=0); out.append(f'{nm}: px={int(m.sum())} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f} G/R={mm[1]/mm[0]:.3f}')
    return ' | '.join(out)
print('WILLEQ :', peak(wd,wg,tw))
print('ORACLE :', peak(od,og,to))
