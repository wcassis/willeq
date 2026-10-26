#!/usr/bin/env python3
"""B1152: regenerate the missile-capture strips and print the per-cell gap-box census.

usage:
  strip_regen.py <video.mp4> <frames csv (6)> <out.png> [existing-strip.png]

- extracts each frame F as 1280x800 RGB with
    ffmpeg -v error -y -i <video> -vf "select=eq(n,F)" -frames:v 1
    -f rawvideo -pix_fmt rgb24 <out>
  (decode from the start; this is the extraction the committed strips were
  made from - cells 1-5 of both strips are byte-identical to it, M1 cell 6
  within 56 px);
- assembles a 3x2 tile of native 1280x800 cells, 4px black border, 3856x1612
  (cell (c,r) at x=4+c*1284, y=4+r*804; reading order left-to-right, top row
  then bottom row) and writes <out.png> via
    ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 3856x1612 -i strip.rgb out.png;
- prints the per-cell census in the deep-gap box x[660,760) x y[440,620):
  pixels with grayscale int(0.299R + 0.587G + 0.114B + 0.5) >= 180, for the
  fresh raw frame and for the strip cell;
- with an existing strip, also prints per-cell differing-pixel counts against
  the fresh extraction (0 = byte-identical cell).
"""
import os, subprocess, sys, tempfile

Wf, Hf = 1280, 800
CW, CH, BW = Wf + 4, Hf + 4, 4          # cell pitch: 1284 x 804
SW, SH = 3*CW + BW, 2*CH + BW           # strip: 3856 x 1612
X0, X1, Y0, Y1 = 660, 760, 440, 620      # the deep-gap box (half-open)
THR = 180

def extract_rgb(video, n, tmpdir):
    out = os.path.join(tmpdir, 'f_%d.rgb' % n)
    cmd = ['ffmpeg', '-v', 'error', '-y', '-i', video,
           '-vf', 'select=eq(n\\,%d)' % n, '-frames:v', '1',
           '-f', 'rawvideo', '-pix_fmt', 'rgb24', out]
    subprocess.run(cmd, check=True)
    with open(out, 'rb') as f:
        b = f.read()
    assert len(b) == Wf*Hf*3, (n, len(b))
    return b

def gray(r, g, b):
    return int(0.299*r + 0.587*g + 0.114*b + 0.5)

def census(buf, ox, oy, W):
    n = 0
    for y in range(Y0, Y1):
        for x in range(X0, X1):
            i = ((oy + y)*W + (ox + x))*3
            if gray(buf[i], buf[i+1], buf[i+2]) >= THR:
                n += 1
    return n

def diff_px(a, W, ax, ay, b):
    n = 0
    for y in range(Hf):
        for x in range(Wf):
            i = ((ay + y)*W + (ax + x))*3
            j = (y*Wf + x)*3
            if a[i] != b[j] or a[i+1] != b[j+1] or a[i+2] != b[j+2]:
                n += 1
    return n

def png_to_rgb(existing, tmpdir):
    out = os.path.join(tmpdir, 'existing.rgb')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', existing,
                    '-f', 'rawvideo', '-pix_fmt', 'rgb24', out], check=True)
    with open(out, 'rb') as f:
        b = f.read()
    assert len(b) == SW*SH*3, len(b)
    return b

def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    video, framelist, out_png = sys.argv[1], sys.argv[2], sys.argv[3]
    existing = sys.argv[4] if len(sys.argv) == 5 else None
    frames = [int(f) for f in framelist.split(',')]
    assert len(frames) == 6, framelist

    tmpdir = tempfile.mkdtemp(prefix='b1152_strip_')
    raws = [extract_rgb(video, f, tmpdir) for f in frames]

    strip = bytearray(SW*SH*3)  # black
    for k, buf in enumerate(raws):
        c, r = k % 3, k // 3
        ox, oy = BW + c*CW, BW + r*CH
        for y in range(Hf):
            dst = ((oy + y)*SW + ox)*3
            src = y*Wf*3
            strip[dst:dst + Wf*3] = buf[src:src + Wf*3]

    rgb = os.path.join(tmpdir, 'strip.rgb')
    with open(rgb, 'wb') as f:
        f.write(bytes(strip))
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo',
                    '-pix_fmt', 'rgb24', '-s', '%dx%d' % (SW, SH),
                    '-i', rgb, out_png], check=True)
    print('wrote %s (cells 1-6 = frames %s)' % (out_png, ', '.join(map(str, frames))))

    E = png_to_rgb(existing, tmpdir) if existing else None

    for k, f in enumerate(frames):
        c, r = k % 3, k // 3
        ox, oy = BW + c*CW, BW + r*CH
        line = 'cell %d frame %d: raw_census=%d cell_census=%d' % (
            k+1, f, census(raws[k], 0, 0, Wf), census(strip, ox, oy, SW))
        if E is not None:
            line += ' diff_px_vs_existing=%d' % diff_px(E, SW, ox, oy, raws[k])
        print(line)

if __name__ == '__main__':
    main()
