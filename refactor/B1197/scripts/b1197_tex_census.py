#!/usr/bin/env python3
# B1197: spelab.tga is 256x256x32bpp uncompressed TGA (header says 32 bpp).
# Read it raw and census the drawn additive contribution (RGB x A/255).
import numpy as np

raw = open('/mnt/projects/EverQuestP1999/SpellEffects/spelab.tga', 'rb').read()
hdr = raw[:18]
w, h, depth = int.from_bytes(hdr[12:14], 'little'), int.from_bytes(hdr[14:16], 'little'), hdr[16]
print(f'header: {hdr.hex()}  w={w} h={h} depth={depth} file={len(raw)} expect={18 + w*h*4}')
data = np.frombuffer(raw[18:18 + w * h * 4], dtype=np.uint8).reshape(h, w, 4)
# TGA is BGRA per pixel, rows bottom-up
rgba = data[:, :, [2, 1, 0, 3]][::-1, :, :]
rgb = rgba[:, :, :3].astype(np.float32)
a = rgba[:, :, 3].astype(np.float32)
print(f'texels: {w}x{h}, alpha mean {a.mean():.1f}, A==255 frac {(a == 255).mean():.3f}')
mx = rgb.max(axis=2)
for thr in (0, 16, 80, 150, 220):
    m = mx > thr
    if m.any():
        print(f'  maxChan>{thr:4d}: {m.mean():8.4f} of texels  R/G/B = '
              f'{rgb[m, 0].mean():7.1f} / {rgb[m, 1].mean():7.1f} / {rgb[m, 2].mean():7.1f}')
# the drawn contribution (additive): texel RGB (the client's alpha = A channel here)
contrib = rgb * (a[:, :, None] / 255.0)
m = mx > 80  # the lit80-ish set
print('lit80 texels: R/G/B drawn contribution =', contrib[m].mean(axis=0).round(2) if m.any() else 'n/a')
# downsampled to 20.5 px (a 1.8 unit sprite at ~11.4 px/unit) - box average
small = cv2 = None
import cv2 as _cv2
s21 = _cv2.resize(rgba.astype(np.uint8), (21, 21), interpolation=_cv2.INTER_AREA)
r21 = s21[:, :, :3].astype(np.float32)
a21 = s21[:, :, 3].astype(np.float32)
c21 = r21 * (a21[:, :, None] / 255.0)
print('21x21 downsample: lit80 frac =', float((r21.max(axis=2) > 80).mean()),
      ' drawn R/G/B =', c21.mean(axis=(0, 1)).round(2))
s42 = _cv2.resize(rgba.astype(np.uint8), (42, 42), interpolation=_cv2.INTER_AREA)
r42 = s42[:, :, :3].astype(np.float32)
a42 = s42[:, :, 3].astype(np.float32)
c42 = r42 * (a42[:, :, None] / 255.0)
print('42x42 downsample: lit80 frac =', float((r42.max(axis=2) > 80).mean()),
      ' drawn R/G/B =', c42.mean(axis=(0, 1)).round(2))
# row/col profile: is it a vertical line? a disc? a glyph?
prof_rows = (mx > 80).sum(axis=1)
prof_cols = (mx > 80).sum(axis=0)
nz_r = np.nonzero(prof_rows > 0)[0]
nz_c = np.nonzero(prof_cols > 0)[0]
print(f'lit80 extent: rows {nz_r.min()}..{nz_r.max()} of 256, cols {nz_c.min()}..{nz_c.max()} of 256')
print(f'lit80 px total: {int((mx > 80).sum())}')
