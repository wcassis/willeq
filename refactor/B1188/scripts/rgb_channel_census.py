#!/usr/bin/env python3
"""B1188 FIX (MR !342 note 8515, finding 6): RGB-channel census of the
storage chain for the spelcf pillar sprite — where does the rendered
flame's blue come from?

Three inputs, same pixel region, per-channel means over the bright set
(alpha > 155, the flame core; and over max(R,G,B) > 150):

  --source  the official client's 64x64 source sprite (spelcf.png);
  --new     this worktree's shipped 1:1 atlas (data/textures/
            spell_sprite_atlas.bmp) with the sprite's 2x2-cell (64x64)
            region at the B1188 layout's slot;
  --old     the pre-B1188 blob (git show ac8c26bc6:data/textures/...)'s
            32x32 box cell (what the B1191 fresh-render tree stored).

If the stored pixels carry low B on the bright set, the rendered B shift is
NOT a storage bug; if they carry high B, the builder is named here.
BMP decode is pure stdlib (the 100-byte-DIB writer format); the PNG side
goes through cv2 IMREAD_UNCHANGED (the B1191 scripts' own dependency).

Usage (from the MR worktree):
  python3 -I refactor/B1188/scripts/rgb_channel_census.py --repo .
"""
import argparse
import struct
import subprocess
import sys


def read_bmp32(data):
    if data[:2] != b'BM':
        raise ValueError('not a BMP')
    off = struct.unpack_from('<I', data, 10)[0]
    dib_size = struct.unpack_from('<I', data, 14)[0]
    w, h, _planes, bpp, comp = struct.unpack_from('<iihHi', data, 18)
    if (bpp & 0xFFFF) != 32 or comp not in (0, 3):
        raise ValueError(f'unsupported BMP: bpp={bpp} comp={comp}')
    topdown = h < 0
    h = abs(h)
    stride = w * 4
    rows = []
    for i in range(h):
        y = i if topdown else (h - 1 - i)
        rows.append(data[off + y * stride: off + y * stride + stride])
    return w, h, rows


def read_png_rgba(path):
    """RGBA-8 PNG -> rows in BMP byte order (B,G,R,A). Decoded with cv2
    (IMREAD_UNCHANGED, BGRA) rather than a hand-rolled decoder: the 1:1
    storage is pinned byte-exact against this file, and a decode bug here
    would masquerade as a storage defect."""
    import cv2
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img is None or img.shape[2] != 4:
        raise ValueError(f'cannot read RGBA PNG {path}')
    h, w = img.shape[:2]
    rows = [img[y].tobytes() for y in range(h)]  # BGRA already
    return w, h, rows


def census(rows, w, h, tag):
    brg = [(rows[y][x * 4], rows[y][x * 4 + 1], rows[y][x * 4 + 2], rows[y][x * 4 + 3])
           for y in range(h) for x in range(w)]  # B,G,R,A (BMP order)
    def mean(sel):
        px = [p for p in brg if sel(p)]
        if not px:
            return f'{tag}: n=0'
        n = len(px)
        r = sum(p[2] for p in px) / n
        g = sum(p[1] for p in px) / n
        b = sum(p[0] for p in px) / n
        a = sum(p[3] for p in px) / n
        return f'{tag}: n={n} R={r:.1f} G={g:.1f} B={b:.1f} A={a:.1f} B/R={b / max(r, 1):.3f}'
    print(mean(lambda p: p[3] > 155))
    print(mean(lambda p: max(p[0], p[1], p[2]) > 150 and p[3] > 155))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.')
    ap.add_argument('--source', default='/mnt/projects/EverQuestP1999/SpellEffects/spelcf.png')
    ap.add_argument('--sprite', type=int, default=22)
    args = ap.parse_args()

    sw, sh, srows = read_png_rgba(args.source)
    assert (sw, sh) == (64, 64)
    census(srows, sw, sh, 'source64')

    # New 1:1 atlas: B1188 layout = 8x8 grid of 4x4-cell (128x128 px) blocks;
    # sprite s owns block (col s%8, row s/8), its 64x64 stored frame (kStored-
    # FramePx[s]) at the block's top-left (spell_sprite_atlas.h blockSlotCell).
    blob_new = open(args.repo + '/data/textures/spell_sprite_atlas.bmp', 'rb').read()
    aw, ah, arows = read_bmp32(blob_new)
    assert (aw, ah) == (1024, 1024)
    slot = args.sprite
    bx, by = (slot % 8) * 128, (slot // 8) * 128
    new_rows = [arows[by + y][bx * 4:(bx + 64) * 4] for y in range(64)]
    census(new_rows, 64, 64, 'new1x1')
    # byte-exact RGB check of the 1:1 storage against the source
    mism = sum(1 for y in range(64) for x in range(64)
               if srows[y][x * 4:(x + 1) * 4] != new_rows[y][x * 4:(x + 1) * 4])
    print(f'new1x1 vs source: mismatched px (all 4 bytes) = {mism}/4096')

    # Old 2:1 cell from the ac8c26 blob.
    blob_old = subprocess.run(['git', '-C', args.repo, 'show',
                               'ac8c26bc6:data/textures/spell_sprite_atlas.bmp'],
                              check=True, capture_output=True).stdout
    ow, oh, orows = read_bmp32(blob_old)
    assert (ow, oh) == (1024, 1024)
    cell = args.sprite * 16
    ox, oy = (cell % 32) * 32, (cell // 32) * 32
    old_rows = [orows[oy + y][ox * 4:(ox + 32) * 4] for y in range(32)]
    census(old_rows, 32, 32, 'oldbox32')


if __name__ == '__main__':
    main()
