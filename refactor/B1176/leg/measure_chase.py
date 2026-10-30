#!/usr/bin/env python3
"""B1176 chase-frame drawn-height measurer (column profile).

For a clean chase frame where the subject A stands on grass under sky, scan a
band of columns across A's torso x-range and find:
  head_row = first row (from top) that is FIGURE (not sky)
  feet_row = last row (from top) that is FIGURE before a continuous run of GROUND starts
drawn_px = feet_row - head_row

Classification per pixel (BGR):
  sky   : bright and blue-dominant  (B > R and max>140) OR very bright
  ground: green-dominant            (G >= R and G >= B-4 and max>50)
  figure: everything else (dark, R-dominant, maroon, boots...)

We require a MIN_RUN of consecutive ground rows to call the feet (so a single
dark boot pixel on grass doesn't end the figure early, and the chat window
which is dark text on dark is excluded by the x-band).
"""
import sys
import numpy as np
import cv2


def classify(bgr):
    B = bgr[..., 0].astype(np.int16)
    G = bgr[..., 1].astype(np.int16)
    R = bgr[..., 2].astype(np.int16)
    mx = np.maximum(np.maximum(R, G), B)
    sky = (B > R) & (mx > 130)
    ground = (G >= R) & (G >= B - 6) & (mx > 45)
    fig = ~(sky | ground)
    return fig, sky, ground, (B, G, R, mx)


def column_figure_mask(img, x0, x1):
    """Per-row: fraction of columns in [x0,x1) that are FIGURE."""
    fig, sky, ground, _ = classify(img)
    band = fig[:, x0:x1]
    frac = band.mean(axis=1)
    return frac, fig, sky, ground


def measure(img, x0, x1, min_ground_run=6, fig_frac=0.25, ground_frac=0.5,
            search_top=150, search_bot=None, max_gap=4):
    """Walk UP from the feet.

    feet = lowest row (above a continuous ground run) that is FIGURE.
    head = walk up from feet while rows are FIGURE (allowing gaps <= max_gap);
           the top of that connected run. This ignores background objects
           (trees/walls) that sit ABOVE the character, separated by a gap.
    """
    H, W = img.shape[:2]
    if search_bot is None:
        search_bot = H - 60  # avoid bottom UI
    frac, fig, sky, ground = column_figure_mask(img, x0, x1)
    is_fig = frac >= fig_frac
    is_gnd = (ground[:, x0:x1].mean(axis=1)) >= ground_frac

    # feet: lowest figure row that has a continuous ground run just below it
    feet = None
    for r in range(search_bot - 1, search_top, -1):
        if is_fig[r]:
            # check the rows below are ground (a continuous run)
            g = 0
            for q in range(r + 1, min(r + min_ground_run + 4, H)):
                if is_gnd[q]:
                    g += 1
                else:
                    break
            if g >= min_ground_run:
                feet = r
                break
    if feet is None:
        # fallback: lowest figure row overall
        fig_rows = [r for r in range(search_top, search_bot) if is_fig[r]]
        if not fig_rows:
            return None
        feet = max(fig_rows)

    # head: walk up from feet through figure rows (allow small gaps)
    head = feet
    gap = 0
    r = feet - 1
    while r >= search_top:
        if is_fig[r]:
            head = r
            gap = 0
        else:
            gap += 1
            if gap > max_gap:
                break
        r -= 1
    return {"head": int(head), "feet": int(feet), "px": int(feet - head),
            "x0": int(x0), "x1": int(x1)}


def main():
    if len(sys.argv) < 4:
        print("usage: measure_chase.py <frame> <x0> <x1> [min_ground_run]")
        sys.exit(2)
    path, x0, x1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    mgr = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    img = cv2.imread(path)
    if img is None:
        print("cannot read", path)
        sys.exit(1)
    res = measure(img, x0, x1, min_ground_run=mgr)
    import json
    print(json.dumps({"frame": path, **res} if res else {"frame": path, "error": "no figure"}))
    # write an overlay crop for verification
    if res:
        crop = img[max(0, res["head"]-30):res["feet"]+30, max(0, x0-40):min(img.shape[1], x1+40)].copy()
        cv2.line(crop, (0, 30), (crop.shape[1], 30), (0, 0, 255), 1)          # head
        cv2.line(crop, (0, 30 + res["px"]), (crop.shape[1], 30 + res["px"]), (0, 255, 0), 1)  # feet
        out = path.replace(".png", "_chase_meas.png")
        cv2.imwrite(out, crop)
        print("overlay:", out)


if __name__ == "__main__":
    main()
