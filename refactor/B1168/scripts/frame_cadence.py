#!/usr/bin/env python3
"""B1168 (#487) ask 2: the oracle client's frame cadence in the B1152/B1159 captures.

The capture video is a fixed 30 fps screen recording (ffprobe r_frame_rate=30/1).
When the client renders slower than 30 fps, the recorder repeats the last
presented image, so consecutive video frames are (near-)identical. This script
reads every video frame in [first, last], computes the mean absolute
difference (grayscale, whole 1280x800 frame) to the previous video frame and
counts "repeat" frames (mean abs diff < THRESH). A run of k repeats after a
change means one client frame lasted k+1 video frames.

usage: python3 -P frame_cadence.py <video.mp4> <first_frame> <last_frame> <out.tsv>
Writes per-frame rows (frame, t_video = frame/30, mad, new_image 0/1) and
prints the summary: client-frame durations (in video frames) histogram and
the mean client frame interval in seconds.
"""
import sys
from collections import Counter

import cv2
import numpy as np

THRESH = 0.005  # grey levels; measured gap: repeats <= 0.0015, changed frames >= 0.018 (M1_38 window)


def main():
    video, first, last, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    cap = cv2.VideoCapture(video)
    cap.set(cv2.CAP_PROP_POS_FRAMES, first - 1)
    ok, prev = cap.read()
    if not ok:
        raise SystemExit("cannot read frame %d" % (first - 1))
    prev = cv2.cvtColor(prev, cv2.COLOR_BGR2GRAY).astype(np.int16)
    rows = []
    for f in range(first, last + 1):
        ok, img = cap.read()
        if not ok:
            break
        g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.int16)
        mad = float(np.abs(g - prev).mean())
        rows.append((f, f / 30.0, mad, 0 if mad < THRESH else 1))
        prev = g
    with open(out, "w") as fh:
        fh.write("frame\tt_video\tmad\tnew_image\n")
        for r in rows:
            fh.write("%d\t%.3f\t%.4f\t%d\n" % r)
    # durations: video frames between consecutive new images
    news = [r[0] for r in rows if r[3]]
    durs = [b - a for a, b in zip(news, news[1:])]
    hist = Counter(durs)
    n = len(rows)
    print("frames %d..%d: %d video frames, %d new images, %d repeats" %
          (first, last, n, len(news), n - len(news)))
    print("client-frame duration histogram (video frames: count):",
          dict(sorted(hist.items())))
    if durs:
        print("mean client frame interval %.4f s (%.1f fps); min mad of new %.3f; max mad of repeat %s" % (
            sum(durs) / len(durs) / 30.0, 30.0 * len(durs) / sum(durs),
            min(r[2] for r in rows if r[3]),
            ("%.4f" % max(r[2] for r in rows if not r[3])) if n - len(news) else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
