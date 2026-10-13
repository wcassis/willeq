#!/usr/bin/env python3
"""Conservative first-repeat check on retained FacePick matrix stills.

The server candidate bounds are deliberately not inputs.  A count is reported
only when the first frame's model crop matches a later frame substantially
better than every intervening frame.  Ambiguous rows stay UNMEASURED.
"""

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image


def distance(a: Path, b: Path) -> float:
    # Exclude the static picker; compare the same central model region.
    box = (470, 220, 780, 580)
    with Image.open(a) as image:
        first = np.asarray(image.convert("RGB").crop(box), dtype=np.int16)
    with Image.open(b) as image:
        second = np.asarray(image.convert("RGB").crop(box), dtype=np.int16)
    return float(np.abs(first - second).mean())


def first_repeat(directory: Path):
    initial = directory / "00.jpg"
    if not initial.is_file():
        return None, "missing 00"
    later = sorted(p for p in directory.glob("[0-9][0-9].jpg") if p.stem != "00")
    if len(later) < 2:
        return None, "too few stills"
    scores = [(int(p.stem), distance(initial, p)) for p in later]
    # JPEG, camera drift and lighting change the pixels.  Find the FIRST
    # strong return toward frame zero, not the lowest score in the sweep.
    prior = scores[0][1]
    for index, score in scores[1:]:
        if score < 0.5 * prior and score < 4.0:
            return index, f"first repeat {index}={score:.2f}; prior min={prior:.2f}"
        prior = min(prior, score)
    return None, "no unambiguous repeated still"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--table", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    human, detail = first_repeat(args.matrix / "human-male" / "face")
    assert human == 8, f"Human-male face self-test expected 8, got {human}: {detail}"
    with args.table.open(newline="") as stream:
        rows = list(csv.DictReader((line for line in stream if not line.startswith("#")), delimiter="\t"))
    with args.report.open("w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow(("race", "gender", "picker", "detected_count", "detail"))
        for row in rows:
            source = row["source"]
            pair = source.split("facepick-matrix4/")[-1].split("/")[0]
            if row.get("visible_luclin", row["visible"]) == "1":
                count, detail = first_repeat(args.matrix / pair / row["picker"])
            else:
                count, detail = None, "hidden"
            writer.writerow((row["race"], row["gender"], row["picker"],
                             count if count is not None else "UNMEASURED", detail))
    print(f"Human-male face self-test PASS: {human}; report={args.report}")


if __name__ == "__main__":
    main()
