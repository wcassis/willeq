#!/usr/bin/env python3
"""T499 -- find the official Titanium client's "Tip of the Day" dialog in
oracle screenshots.

Why
---
Board B795 (found by T492 Leg H): the official client pops a modal "Tip of the
Day" dialog after entering the world and nothing in the oracle rig dismissed it
before T492 Leg I. Its [TipWindow] rect in the character UI ini is
(664,234) 360x300, which sits over the target window and the right-hand third
of the scene. Any camera-fit / held-item / screenshot-diff score computed from
such a frame is contaminated in that region. This tool sweeps every archived
oracle frame and says, per frame, whether the dialog is present.

Two independent signals, BOTH required for a `dialog` verdict
-------------------------------------------------------------
(a) OCR -- tesseract over the dialog rect, matched against
    /Tip of the Day|Show Another Tip|Don't show any tips/i.  Those three
    strings are the title bar, the button and the checkbox; all three are
    inside the rect.
(b) Pixel -- the dialog's title bar is a fixed 17-row x 359-column bitmap
    (rounded gold ends, the engraved "Tip of the Day" caption, the beveled
    top/bottom border rows).  It is byte-identical in every frame that shows
    the dialog, because it is a UI bitmap, not scene content.  The template is
    carried in TITLEBAR_TEMPLATE_PNG (extracted from the known-positive frame)
    and matched anywhere in the frame, so the detector does NOT depend on the
    ini rect being the same in every unit's rig.

Coordinate mapping [measured, T499]
-----------------------------------
The rig runs the client at 1024x768 with the X window at root (-1, 30).
Two kinds of grab exist in the archive:

  * "window" grabs, 1023x738 -- these are client coordinates, so the ini rect
    applies as-is.  MEASURED on the known positive
    review/T492/llmbench-legH/players/p1/frames/
        19:24:01.004_login_camera_untouched_window.png :
    the title-bar top border row is y=229 and the dialog spans x=664..1022
    (the 360th column is clipped off by the 1023-wide grab).  The ini's y=234
    is the client area below the title bar; the title bar itself starts 5 rows
    higher.  Rect used: x [664, 1024), y [229, 534).

  * "root" grabs, 1024x768 -- the whole X root, so window coords shift by
    (-1, +30).  MEASURED on review/T492/llmbench-legH/players/p1/.camto_probe.png
    (1024x768): the same title-bar top border row is at y=259 = 229+30 and the
    same modal-colour run starts at x=663 = 664-1.  Rect used:
    x [663, 1023), y [259, 564).

For any other frame size the default rect is derived from the 1024x768 root
mapping scaled by (w/1024, h/768); the pixel template match is
position-independent anyway, and when it fires the OCR is taken at the MATCHED
position rather than at the default rect.

Verdicts
--------
  dialog      -- both signals fired
  clear       -- neither signal fired
  unreadable  -- exactly one signal fired (ambiguous: a human must look), or
                 the frame could not be opened / OCR could not be run.

Usage
-----
  find_tip_dialog.py --self-test          validate on the known positive and
                                          a known negative, then exit
  find_tip_dialog.py --out FILE [ROOTS..] sweep and write the TSV
"""

import argparse
import base64
import io
import os
import re
import subprocess
import sys
import tempfile
from collections import OrderedDict

import numpy as np
from PIL import Image

# --- geometry ----------------------------------------------------------------

INI_RECT = (664, 234, 360, 300)          # [TipWindow] in the character UI ini
WINDOW_RECT = (664, 229, 360, 305)       # measured, client/window coordinates
WINDOW_SIZE = (1023, 738)
ROOT_SIZE = (1024, 768)
ROOT_OFFSET = (-1, 30)                   # measured: root = window + this

TITLEBAR_H = 17                          # rows 229..245 in window coords
TITLEBAR_TOP_COLOUR = (140, 130, 113)    # measured top-border row colour
TITLEBAR_MIN_RUN = 300                   # px of that colour on the top row
TEMPLATE_MATCH_MIN = 0.95                # fraction of masked template px equal

# The caption is NOT part of the signature.  MEASURED T499: the same window
# appears with several captions -- "Tip of the Day" at login, and the
# context-sensitive "Consider Tip window", "Death Tips", "Inventory Tips",
# "Looting Tips", "Sitting Tips", "Keyboard Options Tips" -- at the same rect.
# Diffing the title bar of the login positive against T478's "Consider Tip
# window" positive, the bar differs ONLY in columns 0-10, 120-238 and 347-358
# (the two rounded end caps, which are alpha-blended over the scene, and the
# centred caption text).  Everything else is byte-identical chrome.  The
# template is matched over the invariant columns only; identifying WHICH tip
# window it is, is the OCR's job.
TITLEBAR_SKIP_COLS = ((0, 11), (120, 239), (347, 359))

OCR_RE = re.compile(r"Tip of the Day|Show Another Tip|Don'?t show any tips"
                    r"|Don'?t show this tip again|Tip window",
                    re.IGNORECASE)

# The title-bar template is not embedded as a blob: it is lifted from the
# known-positive frame, which is in the repo's review/ tree.  If that frame is
# missing the tool refuses to run rather than sweep with half a detector.
KNOWN_POSITIVE = ("review/T492/llmbench-legH/players/p1/frames/"
                  "19:24:01.004_login_camera_untouched_window.png")
KNOWN_POSITIVE_ROOT = "review/T492/llmbench-legH/players/p1/.camto_probe.png"
KNOWN_VARIANT = ("review/T478/oracle/"
                 "T478_oracle_held_C_after_attack_chat_ui.png")
KNOWN_NEGATIVE = "review/T478/oracle/T478_oracle_h160_login_camera_noui_window.png"


def repo_root():
    """The directory that holds review/.

    review/ lives OUTSIDE the checkout (see the project's review-media-path
    rule: /mnt/projects/fable/review/<UNIT>/), so walk up from this file until
    a directory with a review/ subdirectory is found.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    d = here
    while True:
        if os.path.isdir(os.path.join(d, "review")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return os.path.abspath(os.path.join(here, "..", ".."))
        d = parent


def load_template(review_base):
    path = os.path.join(review_base, KNOWN_POSITIVE)
    if not os.path.exists(path):
        sys.exit("known-positive frame missing, cannot build template: " + path)
    im = np.array(Image.open(path).convert("RGB"))
    x, y, w, _h = WINDOW_RECT
    band = im[y:y + TITLEBAR_H, x:min(x + w, im.shape[1])].astype(np.int16)
    keep = np.ones(band.shape[1], dtype=bool)
    for a, b in TITLEBAR_SKIP_COLS:
        keep[a:min(b, band.shape[1])] = False
    return band, keep


def default_rect(size):
    w, h = size
    if (w, h) == WINDOW_SIZE:
        x, y, rw, rh = WINDOW_RECT
    elif (w, h) == ROOT_SIZE:
        x0, y0, rw, rh = WINDOW_RECT
        x, y = x0 + ROOT_OFFSET[0], y0 + ROOT_OFFSET[1]
    else:
        x0, y0, rw, rh = WINDOW_RECT
        sx, sy = w / float(ROOT_SIZE[0]), h / float(ROOT_SIZE[1])
        x = int(round((x0 + ROOT_OFFSET[0]) * sx))
        y = int(round((y0 + ROOT_OFFSET[1]) * sy))
        rw, rh = int(round(rw * sx)), int(round(rh * sy))
    x2, y2 = min(x + rw, w), min(y + rh, h)
    x, y = max(x, 0), max(y, 0)
    if x2 - x < 40 or y2 - y < 40:
        return None
    return (x, y, x2, y2)


# --- signal (b): the title-bar bitmap ---------------------------------------

def pixel_signal(im, template_pair):
    """Search the whole frame for the title-bar template.

    Returns (hit, (x, y) or None, best_fraction).  Candidates are found cheaply
    by looking for a long horizontal run of the top-border colour, then the
    full 17-row template is compared at each candidate.
    """
    template, keep = template_pair
    h, w, _ = im.shape
    th, tw = template.shape[:2]
    if h < th or w < tw:
        return False, None, 0.0
    mask = np.all(im == np.array(TITLEBAR_TOP_COLOUR, dtype=im.dtype), axis=2)
    rows = np.where(mask.sum(axis=1) >= TITLEBAR_MIN_RUN)[0]
    best, best_pos = 0.0, None
    for r in rows:
        if r + th > h:
            continue
        cols = np.where(mask[r])[0]
        # candidate x origins: each run start, shifted back by the template's
        # own offset from the run start (15 px, measured), plus a small search.
        starts = [cols[0]] + [c for i, c in enumerate(cols[1:], 1)
                              if c > cols[i - 1] + 1]
        cands = set()
        for s in starts:
            for d in range(-20, 21):
                x = s - 15 + d
                if 0 <= x <= w - tw:
                    cands.add(x)
        for x in sorted(cands):
            patch = im[r:r + th, x:x + tw].astype(np.int16)
            eq = np.all(np.abs(patch - template) <= 2, axis=2)
            frac = float(np.mean(eq[:, keep]))
            if frac > best:
                best, best_pos = frac, (x, r)
    return best >= TEMPLATE_MATCH_MIN, best_pos, best


# --- signal (a): OCR ---------------------------------------------------------

def ocr_signal(im, rect):
    if rect is None:
        return None, ""
    x, y, x2, y2 = rect
    crop = Image.fromarray(im[y:y2, x:x2])
    # upscale 2x: the dialog's text is 8px and tesseract is unreliable below 20px
    crop = crop.resize((crop.width * 2, crop.height * 2), Image.LANCZOS)
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        tmp = f.name
    try:
        crop.save(tmp)
        p = subprocess.run(["tesseract", tmp, "stdout", "--psm", "6"],
                           capture_output=True, text=True, timeout=120)
        if p.returncode != 0:
            return None, ""
        text = " ".join(p.stdout.split())
        return bool(OCR_RE.search(text)), text
    except (OSError, subprocess.SubprocessError):
        return None, ""
    finally:
        os.unlink(tmp)


def classify(path, template_pair):
    try:
        im = np.array(Image.open(path).convert("RGB"))
    except Exception as exc:                                  # noqa: BLE001
        return dict(size="?", ocr="err", pixel="err", verdict="unreadable",
                    note=str(exc)[:60])
    h, w, _ = im.shape
    pix, pos, frac = pixel_signal(im, template_pair)
    rect = default_rect((w, h))
    if pix and pos is not None:
        x, y = pos
        rect = (x, y, min(x + WINDOW_RECT[2], w),
                min(y + WINDOW_RECT[3], h))
    ocr, text = ocr_signal(im, rect)
    if ocr is None:
        verdict = "unreadable"
        ocr_s = "err"
    else:
        ocr_s = "yes" if ocr else "no"
        if ocr and pix:
            verdict = "dialog"
        elif not ocr and not pix:
            verdict = "clear"
        else:
            verdict = "unreadable"
    note = ""
    if verdict != "clear":
        note = text[:90]
    return dict(size="%dx%d" % (w, h), ocr=ocr_s,
                pixel="yes" if pix else "no",
                verdict=verdict, frac="%.3f" % frac, note=note)


_TEMPLATE = None


def _init_worker(template):
    global _TEMPLATE
    _TEMPLATE = template


def _work(path):
    return classify(path, _TEMPLATE)


def unit_of(path, review_base):
    rel = os.path.relpath(path, os.path.join(review_base, "review"))
    return rel.split(os.sep)[0]


def self_test(review_base, template):
    # KNOWN_VARIANT is a second positive with a different caption: it must also
    # come out `dialog`, which is what the column mask buys.
    print("== detector validation ==")
    ok = True
    for label, rel, expect in (
            ("known-positive (window grab)", KNOWN_POSITIVE, "dialog"),
            ("known-positive (root grab)", KNOWN_POSITIVE_ROOT, "dialog"),
            ("known-positive (caption variant)", KNOWN_VARIANT, "dialog"),
            ("known-negative (T478 _noui_)", KNOWN_NEGATIVE, "clear")):
        p = os.path.join(review_base, rel)
        if not os.path.exists(p):
            print("  MISSING  %-30s %s" % (label, rel))
            ok = False
            continue
        r = classify(p, template)
        good = r["verdict"] == expect
        ok = ok and good
        print("  %-4s %-30s expect=%-6s got=%-10s ocr=%-3s pixel=%-3s frac=%s"
              % ("PASS" if good else "FAIL", label, expect, r["verdict"],
                 r["ocr"], r["pixel"], r["frac"]))
        if r["note"]:
            print("       ocr text: %s" % r["note"])
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--base", default=None,
                    help="directory holding review/ (default: nearest "
                         "ancestor of this script that has one)")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("roots", nargs="*")
    a = ap.parse_args()
    base = a.base or repo_root()
    template = load_template(base)

    if a.self_test:
        sys.exit(0 if self_test(base, template) else 1)

    roots = a.roots
    if not roots:
        import glob
        roots = sorted(
            d for d in glob.glob(os.path.join(base, "review", "T*", "**"),
                                 recursive=True)
            if os.path.isdir(d) and os.path.basename(d) == "oracle")
        roots += sorted(glob.glob(os.path.join(
            base, "review", "T492", "llmbench*", "players", "p1", "frames")))
        roots += sorted(glob.glob(os.path.join(
            base, "review", "T492", "llmbench*", "players", "p1")))
        # T492 Leg F's F9 cycle: 22 official grabs that are scoring INPUTS
        # (they validated the MARGIN criterion at min ratio 4.90), so they
        # belong in the corpus even though they are not under an oracle/ dir.
        roots += sorted(glob.glob(os.path.join(base, "review", "T492",
                                               "f9_cycle")))

    files = []
    seen = set()
    for r in roots:
        for dirpath, _dirs, names in os.walk(r):
            for n in sorted(names):
                if n.lower().endswith(".png"):
                    p = os.path.join(dirpath, n)
                    if p not in seen:
                        seen.add(p)
                        files.append(p)
    files.sort()

    if not self_test(base, template):
        sys.exit("detector validation failed; refusing to sweep")
    print("\n== sweep: %d frames ==" % len(files))

    rows = []
    if a.jobs > 1:
        from multiprocessing import Pool
        with Pool(a.jobs, initializer=_init_worker, initargs=(template,)) as pool:
            for i, r in enumerate(pool.imap(_work, files, chunksize=4)):
                rows.append((os.path.relpath(files[i], base),
                             unit_of(files[i], base), r))
                if (i + 1) % 100 == 0:
                    print("  %d/%d" % (i + 1, len(files)), file=sys.stderr)
    else:
        for i, p in enumerate(files):
            r = classify(p, template)
            rows.append((os.path.relpath(p, base), unit_of(p, base), r))
            if (i + 1) % 100 == 0:
                print("  %d/%d" % (i + 1, len(files)), file=sys.stderr)

    out = a.out or os.path.join(base, "review", "T499", "tip_dialog_audit.tsv")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("frame\tunit\tsize\tocr_hit\tpixel_hit\tverdict\t"
                "template_frac\tocr_text\n")
        for rel, unit, r in rows:
            f.write("\t".join([rel, unit, r["size"], r["ocr"], r["pixel"],
                               r["verdict"], r["frac"], r["note"]]) + "\n")

        per = OrderedDict()
        for _rel, unit, r in rows:
            d = per.setdefault(unit, dict(n=0, dialog=0, clear=0, unreadable=0))
            d["n"] += 1
            d[r["verdict"]] += 1
        f.write("\n# per-unit summary\n")
        f.write("# unit\tframes\tdialog\tclear\tunreadable\n")
        for unit, d in per.items():
            f.write("# %s\t%d\t%d\t%d\t%d\n"
                    % (unit, d["n"], d["dialog"], d["clear"], d["unreadable"]))
        tot = dict(n=len(rows),
                   dialog=sum(1 for _a, _b, r in rows if r["verdict"] == "dialog"),
                   clear=sum(1 for _a, _b, r in rows if r["verdict"] == "clear"),
                   unreadable=sum(1 for _a, _b, r in rows
                                  if r["verdict"] == "unreadable"))
        f.write("# TOTAL\t%d\t%d\t%d\t%d\n"
                % (tot["n"], tot["dialog"], tot["clear"], tot["unreadable"]))
    print("wrote %s: %d frames, %d dialog, %d clear, %d unreadable"
          % (out, tot["n"], tot["dialog"], tot["clear"], tot["unreadable"]))


if __name__ == "__main__":
    main()
