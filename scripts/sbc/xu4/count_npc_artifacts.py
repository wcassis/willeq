#!/usr/bin/env python3
"""Count image-only B1051 artifact proxies in a fixed lower-middle crop.

These are not entity detections: UI panels, masonry, water, and shadows can
trigger them. A nonzero count needs a matching visible NPC before it is a
reproduction of issue #371. The TSV preserves each frame for cold review.
"""

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage


def components(mask, min_area, max_area, min_height, max_width):
    labels, count = ndimage.label(mask)
    boxes = ndimage.find_objects(labels)
    found = []
    for number in range(1, count + 1):
        bounds = boxes[number - 1]
        if bounds is None:
            continue
        y, x = bounds
        height, width = y.stop - y.start, x.stop - x.start
        area = np.count_nonzero(labels[bounds] == number)
        if min_area <= area <= max_area and height >= min_height and width <= max_width:
            found.append((x.start, y.start, x.stop, y.stop))
    return found


def overlap(a, b):
    width = max(0, min(a[2], b[2]) - max(a[0], b[0]))
    height = max(0, min(a[3], b[3]) - max(a[1], b[1]))
    intersection = width * height
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - intersection
    return intersection / union if union else 0.0


def count_frame(path):
    rgb = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    height, width = rgb.shape[:2]
    x0, x1 = int(width * .20), int(width * .80)
    y0, y1 = int(height * .30), int(height * .80)
    crop = rgb[y0:y1, x0:x1]
    grey = crop @ np.array([.2126, .7152, .0722], dtype=np.float32)
    chroma = crop.max(axis=2) - crop.min(axis=2)
    dark = (grey < 25) & (chroma < 22)
    black_boxes = components(dark, 160, 18000, 18, 140)
    black_boxes = [b for b in black_boxes if (b[3] - b[1]) >= (b[2] - b[0]) * 1.2]
    mean = ndimage.uniform_filter(grey, size=7)
    variance = ndimage.uniform_filter(grey * grey, size=7) - mean * mean
    noisy = variance > 1800
    noise_boxes = components(noisy, 100, 18000, 8, 180)
    return black_boxes, noise_boxes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    frames = sorted(args.frames.glob("frame_*.png"))
    if not frames:
        parser.error("no frames found")
    previous = []
    with args.output.open("w", newline="") as out:
        writer = csv.writer(out, delimiter="\t")
        writer.writerow(("frame", "dark_blobs", "persistent_dark_blobs", "high_variance_patches"))
        for frame in frames:
            black, noise = count_frame(frame)
            persistent = sum(any(overlap(a, b) >= .3 for b in previous) for a in black)
            writer.writerow((frame.name, len(black), persistent, len(noise)))
            previous = black
    print(f"{len(frames)} frames scored -> {args.output}")


if __name__ == "__main__":
    main()
