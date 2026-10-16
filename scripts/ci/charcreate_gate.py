#!/usr/bin/env python3
"""B888-CC4 (GitLab #204) — the character select + creation visual gate.

Re-runs the willeq side of the gate (tools/hwcaps/charcreate_capture, the
device-free state machines driven through the PRODUCTION render path into a
headless 1280x800 GLES2 frame), then scores every gate state per control:

  * IoU — the control's detected edge pixels inside its XML-derived rect
    (char_select_layout.h / char_create_layout.h, quoted in controls.py, the
    same numbers the layout headers compute at 1280x800). A rect whose edges
    moved or vanished scores below the 0.9 bar. This is willeq-internal
    (frame vs the header rect), so it runs with or without a reference.
  * OCR — tesseract the label region and check the client's own label landed
    (the state machine resolved the eqstr to real text, not "eqstr <id>").
  * reference IoU — when the official-client reference still for the state is
    present, the gate ratchets the FULL-FRAME edge IoU (blessed still vs the
    fresh willeq render) against the state's blessed baseline in
    reference_manifest.tsv, floor = baseline - REF_REGRESSION_TOL. Until the
    reference set is blessed (tests/goldens/charcreate/ is a BLESS, Will's
    sign-off) these are reported NOT-BLESSED and are a coverage gap, not a
    failure. A blessed still without a manifest row fails the gate (the
    reference leg must never run inert).

The per-state table is printed and written as a ledger TSV in the visual_gate
shape. The gate FAILS on: a willeq state that does not render, a required
control that did not render, a PRESENT required control whose edge density is
below IOU_BAR (its border regressed to a flat fill; presence alone does not
see it), or a reference-IoU regression below the blessed floor. OCR label
mismatches are FINDINGS (reported in the ledger), never failures. A missing
reference still does not fail the gate (it is named NOT-BLESSED) — the
reference set is attached as goldens_proposed and blessed separately.

Usage:
  charcreate_gate.py [--capture DIR] [--ref-dir DIR] [--labels FILE]
                     [--willeq-states DIR] [--capture-bin BIN]
                     [--ref-manifest FILE] [--bless-required]
                     [--bless-record FILE]
  Default: run tools/hwcaps/charcreate_capture into a temp dir, score it.
  --bless-record FILE: bless-time helper — after scoring, write
  reference_manifest.tsv (state<TAB>frame-edge-IoU) for every armed state.
  That file is the blessed set's baseline: with it beside the stills the
  reference leg arms; without it the gate fails.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import charcreate_gate_controls as controls  # noqa: E402  the XML-derived rects

PROJECT_DIR = Path(__file__).resolve().parents[2]
BLESS_GOLDENS = PROJECT_DIR / "tests" / "goldens" / "charcreate"

# The per-control edge-density bar for a REQUIRED control: the share of the
# rect's bright pixels that carry an edge (a bright pixel with a dark
# 4-neighbour). Provenance, measured at this gate's render (llvmpipe, this
# host is the CI runner): a control that drew correctly reads 0.981..1.000
# (the 0.981 floor is the stat up/down glyph borders on the five creation
# states, the README's scores table), while a control whose edges regressed
# to a solid fill reads ~0.14 (QA's break_2.py: create_initial help flattened
# to a solid bright fill -> 0.142). 0.9 is the floor of the first regime: a
# required control at or above it drew its border; below it, the border is
# gone even though the rect still has bright pixels, so presence alone does
# not see it.
IOU_BAR = 0.9

# The reference leg is ratcheted at frame level, not per rect: for each
# blessed state the manifest records the full-frame edge IoU between the
# blessed still and the FRESH willeq render at bless time, and the gate fails
# if the armed value drops below (baseline - REF_REGRESSION_TOL). This is the
# one number an 8,8 px-shifted still measurably hurts (select_one: 0.0823
# unshifted against a 0.0723 floor; the shifted still reads 0.0021, the
# acceptance proof's break A) while staying immune to per-rect bless-time
# artifacts (a blessed rect that measured 0.4197 because a 3D viewport's art
# bled in is not a control the gate can ratchet). REF_REGRESSION_TOL is
# headroom for re-render jitter, not a quality claim.
REF_REGRESSION_TOL = 0.01


def frame_edge_iou(gray_a, gray_b):
    """Full-frame edge IoU: the overlap of the two frames' edge sets (the same
    edge definition edge_fraction uses, bright pixel with a dark 4-neighbour,
    taken over the whole frame). One number per (reference, willeq render)
    pair, what the manifest records and the gate ratchets."""
    import numpy as np
    def edges(gray):
        p = np.zeros((gray.shape[0] + 4, gray.shape[1] + 4), dtype=bool)
        p[2:-2, 2:-2] = gray > 96
        interior = p.copy()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            interior &= np.roll(np.roll(p, dx, axis=1), dy, axis=0)
        return (p & ~interior)[2:-2, 2:-2]
    ea, eb = edges(gray_a), edges(gray_b)
    union = int((ea | eb).sum())
    return int((ea & eb).sum()) / union if union else 1.0


# The blessed set's frame-level baseline. TSV, one row per state:
# state<TAB>frame_iou, '#' comments. A blessed state without a row is a
# failure (the reference leg would be inert for it).
def load_reference_manifest(path):
    base = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        state, frame_iou = line.split("\t")[:2]
        base[state] = float(frame_iou)
    return base


def load_grayscale(path):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(path).convert("L"))


def edge_fraction(gray, x0, y0, x1, y1):
    """Edge density inside the rect: bright pixels that touch a dark
    4-neighbour. Returns (edge_frac, bright_frac) so a rect with NO bright
    pixels at all (a control that vanished) is distinguishable from one with
    bright pixels but no edges (a flat fill). A rect entirely outside the
    image reads as (0.0, 0.0)."""
    import numpy as np
    h, w = gray.shape
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x1), min(h, y1)
    if x1 <= x0 or y1 <= y0:
        return 0.0, 0.0
    sub = gray[y0:y1, x0:x1]
    bright = sub > 96
    nb = int(bright.sum())
    if nb == 0:
        return 0.0, 0.0
    # A real control edge: a bright pixel that has at least one dark 4-neighbour
    # (a border, a text stroke). np.roll WRAPS (the left column compares to the
    # right), which on a small crop makes every bright pixel look "interior" and
    # the ratio read 0 -- so pad the crop with a dark ring first, then drop the
    # pad. The interior of a flat bright fill has all-bright neighbours and is
    # not an edge.
    pad = 2
    p = np.zeros((sub.shape[0] + 2 * pad, sub.shape[1] + 2 * pad), dtype=bool)
    p[pad:pad + sub.shape[0], pad:pad + sub.shape[1]] = bright
    interior = p.copy()
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        interior &= np.roll(np.roll(p, dx, axis=1), dy, axis=0)
    edge = p & ~interior
    edge = edge[pad:pad + sub.shape[0], pad:pad + sub.shape[1]]
    return float(edge.sum()) / nb, float(nb) / sub.size


def has_tesseract():
    return shutil.which("tesseract") is not None


def ocr(gray, x0, y0, x1, y1):
    """tesseract the rect (cropped, 2x upscaled, thresholded) in image space.
    Returns the best-effort lowercased text, or '' on no tesseract / no text."""
    if not has_tesseract():
        return ""
    import numpy as np
    from PIL import Image
    h, w = gray.shape
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x1), min(h, y1)
    if x1 <= x0 or y1 <= y0:
        return ""
    crop = Image.fromarray(gray[y0:y1, x0:x1])
    # the stock client's small white-on-dark type needs a clean binary to
    # survive tesseract: 2x nearest, then a mid threshold.
    crop = crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST)
    arr = np.asarray(crop)
    bw = np.where(arr > 128, 0, 255).astype("uint8")  # invert: black text on white
    p = Path(tempfile.mktemp(suffix=".png"))
    Image.fromarray(bw).save(p)
    try:
        out = subprocess.run(["tesseract", str(p), "stdout", "--psm", "7"],
                             capture_output=True, text=True, timeout=30)
        return " ".join(out.stdout.split()).lower()
    except Exception:
        return ""
    finally:
        p.unlink(missing_ok=True)


def run_capture(out_dir, cap_bin, extra_args):
    cmd = [str(cap_bin), str(out_dir)] + list(extra_args)
    # The capture tool's stdout mixes the tool's own "<state>\t<path>" lines with
    # Irrlicht/driver noise that is not always valid UTF-8 (and, when piped,
    # stdout is fully-buffered and the state names can interleave). The PNG file
    # NAME is the authoritative state name (the tool writes <out_dir>/<state>.png
    # and the state is the file stem), so glob the output dir rather than trust
    # stdout. Decode the noise leniently for the error path only.
    proc = subprocess.run(cmd, capture_output=True, timeout=300)
    err = proc.stderr.decode("utf-8", "replace")
    if proc.returncode != 0:
        out = proc.stdout.decode("utf-8", "replace")
        raise RuntimeError(f"charcreate_capture exited {proc.returncode}:\n{out}\n{err}")
    states = {p.stem: p for p in sorted(Path(out_dir).glob("*.png"))}
    return states


def score_state(name, willeq_png, ref_png, labels, ref_labels, gray_cache={},
                ref_manifest=None):
    """Score one gate state. Returns a dict of per-control rows + an overall
    ok flag. `labels` is {control: expected substring}; a None labels value
    means the state has no OCR labels to check. `ref_manifest`, when the
    reference leg is armed (a manifest TSV alongside the reference set), is
    the {state: frame-edge-IoU} baseline to ratchet against (see
    REF_REGRESSION_TOL)."""
    import numpy as np
    gray = load_grayscale(willeq_png)
    ref_gray = load_grayscale(ref_png) if ref_png and ref_png.exists() else None
    # the reference leg: a reference frame with NO manifest is a failure, not
    # report-only (an inert reference leg is exactly the claimed-but-unenforced
    # defect this ratchet exists to close); with a manifest, the frame-level
    # edge IoU against the fresh willeq render ratchets against the blessed
    # baseline - REF_REGRESSION_TOL.
    ref_armed = ref_gray is not None and ref_manifest is not None
    ref_frame_iou = frame_edge_iou(ref_gray, gray) if ref_gray is not None else None
    ref_base = (ref_manifest or {}).get(name)
    ref_floor = (
        ref_base - REF_REGRESSION_TOL
        if ref_armed and ref_base is not None else None)
    ref_fail = (
        ref_gray is not None
        and (ref_manifest is None
             or ref_base is None
             or (ref_frame_iou is not None
                 and ref_floor is not None
                 and ref_frame_iou < ref_floor)))

    rows = []
    ok = True
    required = set(controls.required_controls(name))
    for ctrl, rect in controls.state_controls(name).items():
        w_edge, w_bright = edge_fraction(gray, *rect)
        ref_edge = edge_fraction(ref_gray, *rect)[0] if ref_gray is not None else None
        # a real control has bright pixels in its rect; if it has none the
        # control is missing (regardless of edge ratio)
        present = w_bright > 0.002
        # the per-control edge bar: a PRESENT required control still owes its
        # border. Presence alone (bright > 0.2%) does not see a control whose
        # edges regressed to a solid fill (break_2.py: 0.142, still "present").
        below_bar = present and ctrl in required and w_edge < IOU_BAR
        rows.append({
            "state": name, "control": ctrl,
            "required": int(ctrl in required),
            "iou": round(w_edge, 3),
            "bright": round(w_bright, 4),
            "ref_iou": (round(ref_edge, 4) if ref_edge is not None else ""),
            "present": int(present),
            "below_bar": int(below_bar),
            "ref_present": int((ref_edge or 0) > 0.002) if ref_gray is not None else None,
        })
        if not present and ctrl in required:
            ok = False
        if below_bar:
            ok = False
    if ref_fail:
        ok = False
    # OCR labels: reported, not gated. The stock client's small antialiased
    # type is not OCR-reliable at this crop (measured: psm7 on a 2x-4x upscale
    # reads "Character Select" as 'ssr ee se tt'/'Be ele'), and the card's rule
    # is that a mismatch is a FINDING, not a reason to loosen the bar -- so the
    # label read is recorded in the ledger and named in the table, and a
    # state machine that failed to resolve an eqstr (it drew "eqstr <id>")
    # shows up there as a finding for a later card.
    ocr_rows = []
    if labels:
        for ctrl, expect in labels.items():
            lr = controls.label_rect(name, ctrl)
            got = ocr(gray, *lr) if lr else ""
            ocr_rows.append({"state": name, "control": ctrl,
                             "expected": expect, "got": got[:40],
                             "ok": int(bool(expect and got and expect.lower() in got))})
    return {"rows": rows, "ocr": ocr_rows, "ok": ok,
            "ref_blessed": ref_gray is not None,
            "ref_armed": ref_armed,
            "ref_frame_iou": (round(ref_frame_iou, 4) if ref_frame_iou is not None else ""),
            "ref_floor": (round(ref_floor, 4) if ref_floor is not None else ""),
            "ref_fail": int(ref_fail)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture-dir", default=None, help="reuse these willeq state PNGs")
    ap.add_argument("--willeq-states", default=None, help="dir of willeq state PNGs (or capture-dir)")
    ap.add_argument("--ref-dir", default=None, help="official-client reference stills dir")
    ap.add_argument("--ref-manifest", default=None,
                    help="reference_manifest.tsv (state<TAB>frame_iou) -- the "
                         "blessed set's frame-level baseline. Default: "
                         "reference_manifest.tsv beside the reference set (or "
                         "one level up, the goldens root). A reference frame "
                         "without its manifest fails the gate (the reference "
                         "leg must be armed, not inert).")
    ap.add_argument("--labels", default=None, help="labels.tsv (state<TAB>control<TAB>label)")
    ap.add_argument("--ref-labels", default=None, help="reference labels.tsv")
    ap.add_argument("--capture-bin", default=None, help="charcreate_capture binary")
    ap.add_argument("--ledger", default=None, help="write the ledger TSV here")
    ap.add_argument("--bless-required", action="store_true",
                    help="the ctest row's mode: if the blessed reference set "
                         "(tests/goldens/charcreate/) is absent, SKIP with 125 "
                         "('reference set not blessed') instead of running the "
                         "willeq-internal gate. Until Will blesses it, CI stays "
                         "green; the table is still printed for the README.")
    ap.add_argument("--bless-record", default=None, metavar="FILE",
                    help="bless-time helper: after scoring, write "
                         "reference_manifest.tsv (state<TAB>frame-edge-IoU) for "
                         "every armed state to FILE. That file is the reference "
                         "leg's baseline: keep it beside the blessed stills, or "
                         "the gate fails on the missing/short manifest.")
    args = ap.parse_args()

    # --bless-required is the ctest row's mode: the official-client reference
    # set is a BLESS (Will's sign-off; the proposed stills live in
    # /home/user/kb-evidence/kb-cc4-charcreate-oracle-gate/goldens_proposed/).
    # Until it is blessed into tests/goldens/charcreate/, skip with 125 BEFORE
    # the expensive willeq capture, so CI stays green.
    if args.bless_required and not BLESS_GOLDENS.exists():
        print(f"SKIP: reference set not blessed -- {BLESS_GOLDENS} is absent. "
              f"The official-client stills are proposed as a bless under "
              f"/home/user/kb-evidence/kb-cc4-charcreate-oracle-gate/"
              f"goldens_proposed/; this row activates once they are blessed into "
              f"tests/goldens/charcreate/. Run without --bless-required for the "
              f"willeq-internal per-control gate (it runs on all states, no "
              f"reference needed).")
        return 125  # ctest SKIP_RETURN_CODE

    cap_bin = args.capture_bin
    if not cap_bin:
        for cand in ("build/bin/charcreate_capture", "build-gles2/bin/charcreate_capture"):
            if (PROJECT_DIR / cand).exists():
                cap_bin = str(PROJECT_DIR / cand)
                break
    if not cap_bin or not Path(cap_bin).exists():
        print(f"FAIL: charcreate_capture not found (looked for {cap_bin}). "
              f"Build it: cmake --build build -j3 --target charcreate_capture")
        return 2

    labels = controls.DEFAULT_LABELS  # willeq's own labels
    if args.labels:
        labels = controls.load_labels(Path(args.labels), labels)
    ref_labels = controls.DEFAULT_LABELS
    if args.ref_labels:
        ref_labels = controls.load_labels(Path(args.ref_labels), ref_labels)

    ref_dir = Path(args.ref_dir) if args.ref_dir else None
    # blessed goldens win over a --ref-dir
    if BLESS_GOLDENS.exists():
        ref_dir = BLESS_GOLDENS
    # the reference leg's baseline lives beside the reference set (or one
    # level up, the goldens root, when the stills sit in a references/ subdir);
    # a reference frame without it fails the gate (score_state), so a set
    # that ships stills but no baseline can never run inert.
    ref_manifest = None
    if ref_dir is not None:
        if args.ref_manifest:
            cands = [Path(args.ref_manifest)]
        else:
            cands = [ref_dir / "reference_manifest.tsv",
                     ref_dir.parent / "reference_manifest.tsv"]
        for manifest in cands:
            if manifest.exists():
                ref_manifest = load_reference_manifest(manifest)
                break

    # run (or reuse) the willeq capture
    if args.willeq_states or args.capture_dir:
        states_dir = Path(args.willeq_states or args.capture_dir)
        states = {p.stem: p for p in sorted(states_dir.glob("*.png"))}
    else:
        with tempfile.TemporaryDirectory(prefix="cc4_capture_") as td:
            states = run_capture(td, cap_bin, [])
            # the ledger + scores are printed before the dir is torn down
            return _score_and_report(states, ref_dir, ref_manifest,
                                     labels, ref_labels, args.ledger,
                                     args.bless_record)

    return _score_and_report(states, ref_dir, ref_manifest,
                             labels, ref_labels, args.ledger,
                             args.bless_record)


def _score_and_report(states, ref_dir, ref_manifest, labels, ref_labels,
                      ledger_path, bless_record=None):
    import numpy as np
    failures = []
    all_rows, all_ocr = [], []
    state_results = {}
    print(f"{'state':22} {'controls':>8} {'present':>8} {'minIoU':>7} {'ocrOk':>7} {'ref':>10}  ok")
    blessed = 0
    for name in controls.GATE_STATES:
        w_png = states.get(name)
        if not w_png or not w_png.exists():
            print(f"{name:22}  MISSING WILLEQ STATE (not rendered)")
            failures.append(f"{name}: willeq state missing (capture did not render it)")
            continue
        ref_png = (ref_dir / f"{name}.png") if ref_dir else None
        result = score_state(name, w_png, ref_png, labels.get(name),
                             ref_labels.get(name), ref_manifest=ref_manifest)
        state_results[name] = result
        all_rows += result["rows"]
        all_ocr += result["ocr"]
        present = sum(1 for r in result["rows"] if r["present"])
        total = len(result["rows"])
        req_iou = [r["iou"] for r in result["rows"] if r["required"]]
        min_iou = min(req_iou) if req_iou else 0.0
        ocr_total = len(result["ocr"])
        ocr_ok = sum(1 for r in result["ocr"] if r["ok"])
        if result["ref_blessed"]:
            blessed += 1
        if result["ref_armed"]:
            ref_txt = f"ARMED {result['ref_frame_iou']:.4f}"
        else:
            ref_txt = "blessed" if result["ref_blessed"] else "NOT-BLESSED"
        print(f"{name:22} {total:>8} {present:>8} {min_iou:>7.3f} "
              f"{(str(ocr_ok) + '/' + str(ocr_total)) if ocr_total else '-':>7} {ref_txt:>10}  {'OK' if result['ok'] else 'FAIL'}")
        if not result["ok"]:
            for r in result["rows"]:
                if not r["present"] and r["required"]:
                    failures.append(f"{name}: control {r['control']} has no pixels in its rect "
                                    f"({controls.state_controls(name)[r['control']]}) — it did not render")
                if r["below_bar"]:
                    failures.append(f"{name}: required control {r['control']} edge density "
                                    f"{r['iou']:.3f} is below the {IOU_BAR:.1f} bar "
                                    f"(its border regressed; the rect still has bright pixels, "
                                    f"so presence alone does not see it)")
            if result["ref_fail"]:
                if ref_manifest is None:
                    failures.append(f"{name}: reference frame present but reference_manifest.tsv "
                                    f"is missing from {ref_dir} — the reference leg would run "
                                    f"unarmed; bless it with its baseline")
                elif (ref_manifest or {}).get(name) is None:
                    failures.append(f"{name}: reference frame present but reference_manifest.tsv "
                                    f"has no baseline row for it — the reference leg would run "
                                    f"unarmed for this state; record it with --bless-record")
                else:
                    failures.append(f"{name}: reference-IoU regression — frame edge IoU "
                                    f"{result['ref_frame_iou']:.4f} fell below the blessed "
                                    f"baseline floor {result['ref_floor']:.4f}")

    # ledger TSV (visual_gate shape: one header, tab-separated)
    if ledger_path:
        ledger_path = Path(ledger_path)
        with open(ledger_path, "w") as f:
            f.write("state\tcontrol\trequired\tiou\tbright\tref_iou\tpresent\tbelow_bar\tref_present\n")
            for r in all_rows:
                f.write(f"{r['state']}\t{r['control']}\t{r['required']}\t{r['iou']}\t{r['bright']}\t"
                        f"{r['ref_iou'] if r['ref_iou'] != '' else '-'}\t"
                        f"{r['present']}\t{r['below_bar']}\t"
                        f"{r['ref_present'] if r['ref_present'] is not None else '-'}\n")
            f.write("\n# ocr\nstate\tcontrol\texpected\tgot\tok\n")
            for r in all_ocr:
                f.write(f"{r['state']}\t{r['control']}\t{r['expected']}\t{r['got']}\t{r['ok']}\n")
        print(f"ledger: {ledger_path}")

    if not has_tesseract():
        print("NOTE: tesseract not installed — OCR labels reported blank (IoU still gated).")
    if blessed == 0 and ref_dir is not None:
        print(f"NOTE: no reference stills found under {ref_dir} — reference IoU is a coverage "
              f"gap (NOT-BLESSED), not a failure, until Will blesses tests/goldens/charcreate/.")
    elif blessed == 0:
        print("NOTE: no reference set supplied (--ref-dir / tests/goldens/charcreate absent) — "
              "reference IoU NOT-BLESSED, a coverage gap, not a failure.")

    # bless-record: the blessed set's frame-level baseline, one row per ARMED
    # state (a blessed still present in the reference set), measured at bless
    # time against the fresh willeq render. Written AFTER scoring so a bless
    # can never ship a baseline the gate itself measured failing.
    if bless_record:
        armed = [n for n in state_results
                 if state_results[n]["ref_blessed"]
                 and state_results[n]["ref_frame_iou"] != ""]
        missing = ([p.stem for p in ref_dir.glob("*.png")
                    if p.stem in set(controls.GATE_STATES)]
                   if ref_dir else [])
        unrecorded = sorted(set(missing) - set(armed))
        if unrecorded:
            print(f"NOTE: --bless-record skipped: blessed stills without an armed "
                  f"frame IoU: {', '.join(unrecorded)}")
        else:
            bless_record = Path(bless_record)
            with open(bless_record, "w") as f:
                f.write("# reference_manifest.tsv -- B888-CC4 reference leg baseline\n")
                f.write("# state<TAB>frame_edge_iou (blessed still vs fresh willeq "
                        "render at bless time; gate floor = baseline - "
                        f"{REF_REGRESSION_TOL})\n")
                for n in controls.GATE_STATES:
                    if n in armed:
                        f.write(f"{n}\t{state_results[n]['ref_frame_iou']:.4f}\n")
            print(f"bless-record: {bless_record}")

    if failures:
        print("\n=== CHARCREATE GATE: FAIL ===")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\n=== CHARCREATE GATE: PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
