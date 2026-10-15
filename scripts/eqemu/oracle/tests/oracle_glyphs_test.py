#!/usr/bin/env python3
"""B1059 (#379): the glyph-atlas reader against pinned, labelled crops.

glyph_corpus.tsv (review/B1059/tools/make_test_table.py) lists crops of
archived official-client frames whose TRUTH comes from the pcap / server DB /
plan / UI XML side, never from the image: the held-out ones (B1057's lane-5
run, T492's official client; the atlas build never saw those frames) plus the
in-sample dialog and window-title crops.

  expect=read     the reader returns exactly the truth (the measured I/l
                  class '[Il]' accepts either letter);
  expect=unknown  the crop holds a cluster no atlas key covers (`why`), and
                  the reader returns UNKNOWN -- never a guess;
  any other text is a WRONG read and fails.

Also asserted: the numpy and cv2 paths give the same segmentation and the
same read on every crop (with cv2 importable and ORACLE_LOCATE_FORCE_NUMPY
unset; =1 runs numpy alone); the kerning/overlap rule is exercised (a split
glyph read from >1 cluster, a run of touching glyphs read as one entry); the
edit caret rule and the class matcher; every frame's sha256; the atlas
sha256s (Atlas() refuses a drifted atlas.png).

Exit 125 (ctest SKIP) when NumPy/Pillow or the review corpus is absent (the CI
runners have neither; review/B1058/README.md). --report FILE writes the crop
table with the tesseract read beside each (tesseract 5, the verify_labels.py
recipe: gray, x5, threshold 50 %, psm 7), for the record only.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE = os.path.dirname(HERE)
sys.path.insert(0, ORACLE)
TABLE = os.path.join(HERE, "glyph_corpus.tsv")
MIN_READ = 50
MIN_UNKNOWN = 5


def skip(msg):
    print(f"SKIP: {msg}")
    sys.exit(125)


def tesseract(luma, rect):
    from PIL import Image
    x, y, w, h = rect
    im = Image.fromarray(luma[y:y + h, x:x + w]).resize((w * 5, h * 5))
    im = im.point(lambda v: 255 if v >= 128 else 0)
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as fh:
        tmp = fh.name
    try:
        im.save(tmp)
        p = subprocess.run(["tesseract", tmp, "-", "--psm", "7"], capture_output=True, text=True, timeout=60)
        return " ".join(p.stdout.split())
    except (OSError, subprocess.SubprocessError):
        return "(tesseract unavailable)"
    finally:
        os.unlink(tmp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report")
    a = ap.parse_args()
    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError as exc:
        skip(f"{exc}: the reader needs NumPy + Pillow on the driver host")
    import oracle_glyphs as og
    review = os.environ.get("ORACLE_GLYPHS_REVIEW", "/mnt/projects/fable/review")
    if not os.path.isdir(os.path.join(review, "B1058", "corpus")):
        skip(f"review corpus not present under {review}")
    paths = ["numpy"] + (["cv2"] if og.have_cv2() else [])
    print(f"paths: {' + '.join(paths)}"
          f"{' (ORACLE_LOCATE_FORCE_NUMPY=1)' if os.environ.get('ORACLE_LOCATE_FORCE_NUMPY') == '1' else ''}")
    fail = []

    # unit rules
    if not og.text_matches("Iksar".replace("I", "[Il]"), "Iksar") or \
            not og.text_matches("Ha[Il]f", "Half") or og.text_matches("Ha[Il]f", "Hatf") or \
            og.text_matches(og.UNKNOWN, og.UNKNOWN):
        fail.append("text_matches: class rule broken")
    caret = [[0, 3, 2, 11, [1]], [5, 9, 2, 11, [2]], [12, 12, 0, 12, [3]]]
    if og.drop_caret(caret) != caret[:2] or og.drop_caret(caret[:2]) != caret[:2]:
        fail.append("drop_caret: an edit caret (1 column, taller than every glyph) is not dropped, "
                    "or a glyph is")
    if og.rig_atlas("lane2") is not None:
        fail.append("rigs.tsv names lane2, which no measurement covered")
    try:
        atlas = og.Atlas(og.rig_atlas("lane5"))
    except (OSError, ValueError, TypeError) as exc:
        print(f"FAIL: atlas: {exc}")
        return 1

    rows = og.read_crops_tsv(TABLE)
    frames = {}
    n_read = n_unknown = multi_cluster = multi_char = classes = 0
    report = []
    for r in rows:
        f = og.resolve(r["frame"])
        if f not in frames:
            with open(f, "rb") as fh:
                if hashlib.sha256(fh.read()).hexdigest() != r["frame_sha256"]:
                    fail.append(f"{r['frame']}: frame bytes differ from the pinned sha256")
            frames[f] = og.load_luma(f)
        luma, rect = frames[f], og.parse_rect(r["rect"])
        res = {}
        for p in paths:
            res[p] = og.read_crop(luma, rect, atlas, None, p, edit=r["edit"] == "1")
        if "cv2" in paths:
            s_np = og.segment(luma, rect, "numpy")[0]
            s_cv = og.segment(luma, rect, "cv2")[0]
            if [c[:4] for c in s_np] != [c[:4] for c in s_cv]:
                fail.append(f"{r['frame']} {r['rect']}: numpy and cv2 segment differently")
            if res["numpy"]["text"] != res["cv2"]["text"]:
                fail.append(f"{r['frame']} {r['rect']}: numpy read {res['numpy']['text']!r}, "
                            f"cv2 {res['cv2']['text']!r}")
        got = res["numpy"]
        if r["expect"] == "read":
            n_read += 1
            if not og.text_matches(got["text"], r["truth"]):
                fail.append(f"{r['frame']} {r['rect']}: read {got['text']!r}, truth {r['truth']!r}")
            for t, n in got.get("tokens", []):
                multi_cluster += n > 1
                multi_char += len(t) > 1 and not t.startswith("[")
                classes += t.startswith("[")
        else:
            n_unknown += 1
            if got["text"] != og.UNKNOWN:
                fail.append(f"{r['frame']} {r['rect']}: expected UNKNOWN ({r['why']}), read {got['text']!r}")
        if a.report:
            report.append((r, got, tesseract(luma, rect)))
    if n_read < MIN_READ:
        fail.append(f"only {n_read} expect=read crops (< {MIN_READ})")
    if n_unknown < MIN_UNKNOWN:
        fail.append(f"only {n_unknown} expect=unknown crops (< {MIN_UNKNOWN})")
    if not multi_cluster:
        fail.append("no read used a glyph split over several clusters: the overlap rule is untested")
    if not multi_char:
        fail.append("no read used a run of touching glyphs: the kerning rule is untested")

    if a.report:
        with open(a.report, "w") as fh:
            fh.write(f"paths: {' + '.join(paths)}; atlas {atlas.name} ({len(atlas.entries)} bitmaps)\n\n")
            fh.write("| # | truth | atlas read | tesseract read | expect | held out | rig | frame | rect | truth source |\n"
                     "|---|---|---|---|---|---|---|---|---|---|\n")
            for i, (r, got, tess) in enumerate(report, 1):
                cell = lambda v: (v or "(empty)").replace("|", "\\|")  # noqa: E731
                fh.write(f"| {i} | {cell(r['truth'])} | {cell(got['text'])} | {cell(tess)} | {r['expect']} | "
                         f"{r['held_out']} | {r['rig']} | {r['frame']} | {r['rect']} | {cell(r['truth_source'])} |\n")
            exact_t = sum(1 for r, g, t in report if r["expect"] == "read" and t == r["truth"])
            fh.write(f"\ntesseract exact on the expect=read crops: {exact_t} of {n_read}\n")
    if fail:
        print("\n".join("FAIL: " + m for m in fail[:200]))
        print(f"{len(fail)} failure(s)")
        return 1
    print(f"PASS: {n_read} crops read exactly, {n_unknown} UNKNOWN as pinned, 0 wrong; "
          f"{multi_cluster} split-glyph and {multi_char} touching-run tokens, {classes} I/l class reads; "
          f"{len(frames)} frames on {' + '.join(paths)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
