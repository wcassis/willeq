#!/usr/bin/env python3
"""B1188 FIX (MR !342 note 8515, finding 1): re-derive the storage-mechanism
numbers from the ACTUAL old shipped atlas blob, not from a description of it.

Inputs
  --repo   git repo to read the old blob from (default: this checkout)
  --blob   git object for the pre-B1188 shipped atlas
           (default: ac8c26bc6:data/textures/spell_sprite_atlas.bmp -- the
           B1186 tip, the tree the B1188 gate's before-side is judged against)
  --source the official client's own 64x64 source sprite
           (default: /mnt/projects/EverQuestP1999/SpellEffects/spelcf.png)
  --sprite sprite ordinal (default: 22, spelcf in the ac8c26bc kFiles order)

What it does
  1. pulls the old 1024x1024 atlas BMP out of the git blob,
  2. extracts sprite 22's stored frame: the ac8c26bc layout is 32x32 px cells
     on a 32x32 grid, each sprite reserves 16 cells (cell = sprite*16+frame),
     so spelcf (sprite 22) lives in cell 352 at pixel origin (0, 352);
  3. re-derives the builder's own 2x2 box rule (tools/spell_sprite_atlas_builder.cpp
     blitCell at ac8c26bc: colour = alpha-weighted mean, alpha = sumA/4) from
     the source and verifies the stored cell byte-for-byte against it;
  4. reports, for the SOURCE 64x64 and the STORED 32x32 alpha channel:
     peak, count(alpha>150), p99, mean, integral (sum), count(alpha>80)
     ("lit80") and count(alpha>128) -- at native scale and after a 288x288
     bilinear upscale (the pillar's drawn size, README sec. 7), with the
     stored/source ratios.

Pure stdlib (BMP + PNG decode, bilinear upscale); no numpy/cv2, runs anywhere
python3 does. The reviewer's numbers for comparison (note 8515): peak 251,
72 px > 150, p99 243 in the stored cell; mean alpha 28.2 both sides; lit80
~1.03x and >128 ~0.96x at 288 px scale.

Usage (from the MR's worktree):
  python3 -I refactor/B1188/scripts/old_cell_stats.py \
      --repo . --blob ac8c26bc6:data/textures/spell_sprite_atlas.bmp \
      --source /mnt/projects/EverQuestP1999/SpellEffects/spelcf.png
"""
import argparse
import math
import struct
import subprocess
import sys
import zlib


# ---------------- image decoders (stdlib only) ----------------

def read_bmp32(data):
    """32bpp BMP (BI_RGB or BI_BITFIELDS) -> (w, h, rows top-down, 4 bytes/px BGRA)."""
    if data[:2] != b'BM':
        raise ValueError('not a BMP')
    off = struct.unpack_from('<I', data, 10)[0]
    # DIB header: size(4) w(4) h(4) planes(2) bpp(2) comp(4) ... the shipped
    # writer (bmp32_writer) emits a 100-byte header (size field 100), so do
    # not assume the 40-byte BITMAPINFOHEADER.
    dib_size = struct.unpack_from('<I', data, 14)[0]
    w, h, _planes, bpp, comp = struct.unpack_from('<iihHi', data, 18)
    if (bpp & 0xFFFF) != 32 or comp not in (0, 3):
        raise ValueError(f'unsupported BMP: bpp={bpp} comp={comp}')
    if off + h * w * 4 > len(data):
        raise ValueError('BMP pixel data truncated')
    topdown = h < 0
    h = abs(h)
    stride = w * 4  # 32bpp rows are 4-byte aligned by construction
    rows = []
    for i in range(h):
        y = i if topdown else (h - 1 - i)
        rows.append(data[off + y * stride: off + y * stride + stride])
    return w, h, rows


def read_png_rgba(path):
    """Minimal RGBA-8 non-interlaced PNG -> (w, h, rows, 4 bytes/px RGBA)."""
    with open(path, 'rb') as fh:
        raw = fh.read()
    if raw[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos, w, h, bitd, ctype = 8, 0, 0, 0, 0
    while pos < len(raw):
        (ln,) = struct.unpack_from('>I', raw, pos)
        typ = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bitd, ctype, comp, filt, inter = struct.unpack('>IIBBBBB', body)
            if inter != 0:
                raise ValueError('interlaced PNG not supported')
        elif typ == b'IDAT':
            pass
        elif typ == b'IEND':
            break
        pos += 12 + ln
    if (bitd, ctype) != (8, 6):
        raise ValueError(f'only RGBA-8 supported (got bitd={bitd} ctype={ctype})')
    scan = zlib.decompress(b''.join(_png_idats(raw)))
    stride = w * 4
    prev = bytearray(stride)
    out = []
    p = 0
    for y in range(h):
        f = scan[p]
        p += 1
        line = bytearray(scan[p:p + stride])
        p += stride
        if f == 1:  # Sub
            for x in range(4, stride):
                line[x] = (line[x] + line[x - 4]) & 0xFF
        elif f == 2:  # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif f == 3:  # Average
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif f == 4:  # Paeth
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out.append(bytes(line))
        prev = line
    return w, h, out


def _png_idats(raw):
    pos = 8
    while pos < len(raw):
        (ln,) = struct.unpack_from('>I', raw, pos)
        if raw[pos + 4:pos + 8] == b'IDAT':
            yield raw[pos + 8:pos + 8 + ln]
        pos += 12 + ln


def alpha_of(rows, w, h, bgra=True):
    """rows -> flat list of the alpha channel, top-down."""
    ai = 3 if bgra else 3  # BGRA and RGBA both carry alpha last
    return [rows[y][x * 4 + ai] for y in range(h) for x in range(w)]


def bilinear_up(rows, w, h, target):
    """Square bilinear resize of the ALPHA channel to target x target (0..255 floats)."""
    a = alpha_of(rows, w, h)
    out = []
    scale = (h - 1) / (target - 1) if h > 1 else 0.0
    for ty in range(target):
        fy = ty * scale
        y0 = min(int(fy), h - 1)
        y1 = min(y0 + 1, h - 1)
        dy = fy - y0
        for tx in range(target):
            fx = tx * scale
            x0 = min(int(fx), w - 1)
            x1 = min(x0 + 1, w - 1)
            dx = fx - x0
            top = a[y0 * w + x0] * (1 - dx) + a[y0 * w + x1] * dx
            bot = a[y1 * w + x0] * (1 - dx) + a[y1 * w + x1] * dx
            out.append(top * (1 - dy) + bot * dy)
    return out


# ---------------- stats ----------------

def p99(vals):
    s = sorted(vals)
    if not s:
        return None
    return s[max(0, math.ceil(0.99 * len(s)) - 1)]


def stats(vals, tag):
    n = len(vals)
    if not n:
        print(f'{tag}\t(none)')
        return
    s = sum(vals)
    print(f'{tag}\tpeak={max(vals)}\tpx_gt150={sum(1 for v in vals if v > 150)}'
          f'\tp99={p99(vals)}\tmean={s / n:.2f}\tintegral={s}'
          f'\tlit80={sum(1 for v in vals if v > 80)}\tband128={sum(1 for v in vals if v > 128)}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.')
    ap.add_argument('--blob', default='ac8c26bc6:data/textures/spell_sprite_atlas.bmp')
    ap.add_argument('--source', default='/mnt/projects/EverQuestP1999/SpellEffects/spelcf.png')
    ap.add_argument('--sprite', type=int, default=22)
    args = ap.parse_args()

    blob = subprocess.run(['git', '-C', args.repo, 'show', args.blob],
                          check=True, capture_output=True).stdout
    aw, ah, arows = read_bmp32(blob)
    assert (aw, ah) == (1024, 1024), f'old atlas is {aw}x{ah}, expected 1024x1024'

    # ac8c26bc layout: 32x32 px cells, sprite reserves 16, cell origin (cell%32)*32.
    cell = args.sprite * 16
    ox, oy = (cell % 32) * 32, (cell // 32) * 32
    stored_rows = [arows[oy + y][ox * 4:(ox + 32) * 4] for y in range(32)]
    stored = alpha_of(stored_rows, 32, 32)

    sw, sh, srows = read_png_rgba(args.source)
    assert (sw, sh) == (64, 64), f'source is {sw}x{sh}, expected 64x64'
    source = alpha_of(srows, 64, 64)

    # Re-derive the builder's 2x2 box from the source (colour alpha-weighted,
    # alpha = sumA/4) and check it against the stored cell byte-for-byte.
    box = [0] * 1024
    for yy in range(32):
        for xx in range(32):
            # the builder's 2x2 block: source rows 2*yy, 2*yy+1; cols 2*xx, 2*xx+1
            a00 = srows[2 * yy][2 * xx * 4 + 3]
            a01 = srows[2 * yy][(2 * xx + 1) * 4 + 3]
            a10 = srows[2 * yy + 1][2 * xx * 4 + 3]
            a11 = srows[2 * yy + 1][(2 * xx + 1) * 4 + 3]
            box[yy * 32 + xx] = (a00 + a01 + a10 + a11) // 4  # sumA / texels, integer
    maxdiff = max(abs(box[i] - stored[i]) for i in range(1024))
    mism = sum(1 for i in range(1024) if box[i] != stored[i])
    print(f'stored cell = builder 2x2 box of source: max|alpha diff|={maxdiff} '
          f'mismatched px={mism}/1024')

    print(f'{"side":10s}{"peak":>8s}{"px>150":>9s}{"p99":>7s}{"mean":>8s}'
          f'{"integral":>11s}{"lit80":>8s}{"band128":>10s}')
    stats(source, 'source64')
    stats(stored, 'stored32')
    # integral conservation: each stored px carries 4 source px, so the stored
    # integral x4 must equal the source integral (truncation aside); the mean
    # alpha is the per-unit-area figure both sides.
    print(f'integral conserved: stored*4/source = {4 * sum(stored) / sum(source):.4f} '
          f'({4 * sum(stored)} vs {sum(source)}); mean alpha {sum(source) / 4096:.2f} vs '
          f'{sum(stored) / 1024:.2f}')

    # Drawn size: the 288 px pillar of README sec. 7. Bilinear upscale of each
    # side's alpha, same thresholds, stored/source ratios.
    u_src = bilinear_up(srows, 64, 64, 288)
    u_sto = bilinear_up(stored_rows, 32, 32, 288)

    def counts(u):
        return (sum(1 for v in u if v > 80), sum(1 for v in u if v > 128),
                sum(1 for v in u if v > 150), max(u))

    cs, cb, cp, cpeak = counts(u_src)
    ds, db, dp, dpeak = counts(u_sto)
    print(f'@288px source: lit80={cs} band128={cb} px>150={cp} peak={cpeak:.1f}')
    print(f'@288px stored: lit80={ds} band128={db} px>150={dp} peak={dpeak:.1f}')
    print(f'@288px stored/source: lit80={ds / cs:.3f} band128={db / cb:.3f} '
          f'px>150={dp / cp:.3f}')
    print('ratios >1 mean the STORED sprite is brighter at that band at drawn size')


if __name__ == '__main__':
    sys.exit(main())
