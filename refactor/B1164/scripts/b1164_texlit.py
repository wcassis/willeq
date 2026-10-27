#!/usr/bin/env python3
"""B1164: lit fraction of record 80's landing textures as an additive SRCALPHA,ONE copy draws them
(max-RGB x alpha), at the instrument thresholds 45 and 80. usage: b1164_texlit.py [client SpellEffects dir]"""
import sys, cv2, numpy as np
D = sys.argv[1] if len(sys.argv) > 1 else '/mnt/projects/EverQuestP1999/SpellEffects'
for n in ['spelcc.png', 'spelcf.png', 'spelcm.png', 'spelcb.png']:
    im = cv2.imread(f'{D}/{n}', cv2.IMREAD_UNCHANGED)
    v = im[:, :, :3].max(axis=2).astype(float) * im[:, :, 3] / 255.0
    print(f'{n} {im.shape[1]}x{im.shape[0]} colour*alpha >45 {(v > 45).mean():.3f} >80 {(v > 80).mean():.3f}')
