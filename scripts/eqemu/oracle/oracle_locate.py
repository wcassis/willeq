#!/usr/bin/env python3
"""B1058 (#378): template location for the official-client oracle driver.

Runs on the DRIVER host (dev-u24 / hermes-ext). The oracle boxes have no
NumPy, Pillow or OpenCV, so the remote (`oracle_remote.sh`) only takes a
`shot` and clicks; this file finds the control in that shot.

Matching is OpenCV's TM_CCOEFF_NORMED on the 8-bit luma (`PIL.Image.convert
('L')`) of frame and template: the normalised cross-correlation of the
zero-mean template with the zero-mean frame window. It is invariant to an
affine change of brightness, so one label template matches the label white
(enabled), yellow (selected) or grey (disabled); the selected state is
verified separately by the driver (`oracle_remote.sh` selected_at). Two
implementations, chosen at runtime and named in every output line:

  cv2    cv2.matchTemplate(..., TM_CCOEFF_NORMED) on float32 luma.
  numpy  the same quantity in float64: an FFT cross-correlation of the frame
         with the zero-mean template (numerator) over sqrt(sum t'^2 *
         (S2 - S1^2/n)) with the window sums from integral images. A window
         with no variance scores 0. ORACLE_LOCATE_FORCE_NUMPY=1 forces it.

Coordinates are FRAME pixels of the PNG as Pillow reads it. The remote's
shot is `import -window root -crop 1280x800+0+30`, whose PNG keeps the
+0+30 page offset (ImageMagick honours it, Pillow ignores it), so a root
xdotool coordinate is (frame x + 0, frame y + 30): measured in
review/B1058/README.md "Coordinates".

Commands
--------
  locate <frame> <template.png> [--min-score S] [--roi x,y,w,h]
      -> "x y score path": the template CENTRE (left + w//2, top + h//2).
      Exit 0 when score >= S (default 0.9), 1 below it, 2 on error.
  find <frame> <name> [--templates DIR] [--roi x,y,w,h]
      Same, with the template file and min score from DIR/manifest.tsv.
  cut-sheet <sheet.tga> <x,y,w,h> <out.png> --name N --xml FILE:LINE [--note T]
  cut-frame <frame.png> <x,y,w,h> <out.png> --name N [--xml FILE:LINE] [--note T]
      Cut a template and write/replace its manifest.tsv row (beside out.png).
  broker --remote user@host --remote-dir DIR --out DIR [--templates DIR]
      The driver-side half of click-template / wait-template: one ssh
      exchange per locate (see docs/oracle_driver.md "Template clicks").
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATES = os.path.join(HERE, "templates")
MANIFEST_COLUMNS = ("name", "file", "kind", "source", "xml_ref", "rect",
                    "source_sha256", "template_sha256", "min_score", "note")
DEFAULT_MIN_SCORE = 0.9


# --- images -------------------------------------------------------------------

def load_luma(path):
    import numpy as np
    from PIL import Image
    with Image.open(path) as im:
        return np.asarray(im.convert("L"), dtype=np.uint8)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_rect(text):
    m = re.fullmatch(r"(\d+),(\d+),(\d+),(\d+)", text or "")
    if not m:
        raise ValueError(f"bad rect {text!r}, want x,y,w,h")
    x, y, w, h = map(int, m.groups())
    if w <= 0 or h <= 0:
        raise ValueError(f"empty rect {text!r}")
    return x, y, w, h


# --- matching -----------------------------------------------------------------

def have_cv2():
    if os.environ.get("ORACLE_LOCATE_FORCE_NUMPY") == "1":
        return False
    try:
        import cv2  # noqa: F401
        return True
    except ImportError:
        return False


def score_map_cv2(frame, tpl):
    import cv2
    import numpy as np
    return cv2.matchTemplate(frame.astype(np.float32), tpl.astype(np.float32),
                             cv2.TM_CCOEFF_NORMED).astype(np.float64)


def _fast_len(n):
    """Smallest 2^a 3^b 5^c >= n (pocketfft is slow on large prime factors)."""
    best = None
    p2 = 1
    while p2 < 2 * n:
        p3 = p2
        while p3 < 2 * n:
            p5 = p3
            while p5 < n:
                p5 *= 5
            if best is None or p5 < best:
                best = p5
            p3 *= 3
        p2 *= 2
    return best


class NumpyFrame:
    """Per-frame state the numpy path reuses across templates: the integral
    images and the frame spectrum for each padded FFT shape."""

    def __init__(self, frame):
        import numpy as np
        self.img = frame.astype(np.float64)
        H, W = self.img.shape
        self.c1 = np.zeros((H + 1, W + 1))
        self.c2 = np.zeros((H + 1, W + 1))
        self.c1[1:, 1:] = self.img.cumsum(0).cumsum(1)
        self.c2[1:, 1:] = (self.img * self.img).cumsum(0).cumsum(1)
        self.spectra = {}

    def spectrum(self, fshape):
        import numpy as np
        if fshape not in self.spectra:
            self.spectra[fshape] = np.fft.rfft2(self.img, s=fshape)
        return self.spectra[fshape]


def score_map_numpy(frame, tpl, cache=None):
    import numpy as np
    nf = cache if cache is not None else NumpyFrame(frame)
    H, W = nf.img.shape
    t = tpl.astype(np.float64)
    h, w = t.shape
    t = t - t.mean()
    tnorm2 = float((t * t).sum())
    if tnorm2 <= 0.0:
        raise ValueError("template has no variance")
    # Template sizes are bucketed to 32 px so one frame spectrum serves many
    # templates (the test locates ~40 templates in each corpus frame).
    fshape = (_fast_len(H + ((h + 31) // 32) * 32), _fast_len(W + ((w + 31) // 32) * 32))
    # Cross-correlation = convolution with the flipped template.
    spec = nf.spectrum(fshape) * np.fft.rfft2(t[::-1, ::-1], s=fshape)
    num = np.fft.irfft2(spec, s=fshape)[h - 1:H, w - 1:W]
    def window_sum(c):
        return c[h:, w:] - c[:-h, w:] - c[h:, :-w] + c[:-h, :-w]
    s1 = window_sum(nf.c1)
    s2 = window_sum(nf.c2)
    var = np.maximum(s2 - s1 * s1 / (h * w), 0.0)
    den = np.sqrt(var * tnorm2)
    out = np.zeros_like(num)
    ok = den > 1e-6 * tnorm2
    out[ok] = num[ok] / den[ok]
    return np.clip(out, -1.0, 1.0)


def score_map(frame, tpl, path=None, cache=None):
    if path is None:
        path = "cv2" if have_cv2() else "numpy"
    if tpl.shape[0] > frame.shape[0] or tpl.shape[1] > frame.shape[1]:
        raise ValueError("template larger than the search area")
    if path == "cv2":
        return score_map_cv2(frame, tpl), path
    return score_map_numpy(frame, tpl, cache), path


def peak(smap):
    import numpy as np
    idx = int(np.argmax(smap))
    y, x = divmod(idx, smap.shape[1])
    return x, y, float(smap[y, x])


def second_peak(smap, x, y, w, h):
    """Best score outside the winner's footprint (+/- one template size)."""
    import numpy as np
    m = smap.copy()
    m[max(0, y - h):y + h + 1, max(0, x - w):x + w + 1] = -np.inf
    if not np.isfinite(m).any():
        return None
    sx, sy, s = peak(m)
    return sx, sy, s


def locate_arrays(frame, tpl, roi=None, path=None, cache=None, want_map=False):
    """-> dict(left, top, cx, cy, score, path, second) in FRAME pixels.
    `cache` is a NumpyFrame for this exact (un-ROI'd) frame, numpy path only."""
    ox = oy = 0
    if roi:
        rx, ry, rw, rh = roi
        frame = frame[ry:ry + rh, rx:rx + rw]
        ox, oy = rx, ry
        cache = None
    smap, used = score_map(frame, tpl, path, cache)
    x, y, s = peak(smap)
    h, w = tpl.shape
    sec = second_peak(smap, x, y, w, h)
    out = {"left": x + ox, "top": y + oy, "cx": x + ox + w // 2,
           "cy": y + oy + h // 2, "score": s, "path": used,
           "second": None if sec is None else sec[2]}
    if want_map:
        out["map"] = smap
    return out


# --- manifest -----------------------------------------------------------------

def read_manifest(templates=TEMPLATES):
    path = os.path.join(templates, "manifest.tsv")
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path, encoding="utf-8") as fh:
        header = None
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if header is None:
                header = cells
                continue
            rows[cells[0]] = dict(zip(header, cells))
    return rows


def write_manifest(rows, templates):
    path = os.path.join(templates, "manifest.tsv")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write("# B1058 template library: one row per template. kind=sheet cites the\n"
                 "# UI sheet + the EQUI_*.xml line naming the sub-rect; kind=frame cites the\n"
                 "# capture it was cut from. rect is x,y,w,h in that source. min_score is the\n"
                 "# midpoint of the positive/negative score gap (review/B1058/README.md);\n"
                 "# '-' = not yet derived, 'rejected' = no gap (never used by the driver).\n")
        fh.write("\t".join(MANIFEST_COLUMNS) + "\n")
        for name in sorted(rows):
            fh.write("\t".join(str(rows[name].get(c, "-")) for c in MANIFEST_COLUMNS) + "\n")
    os.replace(tmp, path)


def template_entry(name, templates=TEMPLATES):
    rows = read_manifest(templates)
    if name not in rows:
        raise KeyError(f"unknown template {name!r} (not in {templates}/manifest.tsv)")
    row = rows[name]
    if row["min_score"] in ("-", "rejected"):
        raise KeyError(f"template {name!r} has min_score {row['min_score']}; not usable")
    return os.path.join(templates, row["file"]), float(row["min_score"])


def cut(kind, source, rect, out, name, xml_ref, note, keep_source_rel=True):
    from PIL import Image
    x, y, w, h = rect
    with Image.open(source) as im:
        if x + w > im.width or y + h > im.height:
            raise ValueError(f"rect {rect} outside {im.width}x{im.height} {source}")
        piece = im.convert("RGBA" if kind == "sheet" else "RGB").crop((x, y, x + w, y + h))
        piece.save(out)
    import numpy as np
    if float(np.asarray(piece.convert("L"), dtype=np.float64).std()) == 0.0:
        os.unlink(out)
        raise ValueError("cut piece is flat: TM_CCOEFF_NORMED is undefined for it")
    templates = os.path.dirname(os.path.abspath(out))
    rows = read_manifest(templates)
    old = rows.get(name, {})
    rows[name] = {
        "name": name, "file": os.path.basename(out), "kind": kind,
        "source": os.path.abspath(source), "xml_ref": xml_ref or "-",
        "rect": f"{x},{y},{w},{h}", "source_sha256": sha256_file(source),
        "template_sha256": sha256_file(out),
        "min_score": old.get("min_score", "-"), "note": note or "-",
    }
    write_manifest(rows, templates)


# --- broker (driver side of click-template / wait-template) -------------------

ANSWER_RE = re.compile(r"^\d+ (-?\d+ -?\d+ -?\d+\.\d+ (cv2|numpy) [01]|ERR)$")


def broker(remote, remote_dir, out, templates, idle_limit, ssh_opts):
    os.makedirs(out, exist_ok=True)
    log_path = os.path.join(out, "locate.tsv")
    if not os.path.exists(log_path):
        with open(log_path, "w") as fh:
            fh.write("seq\ttemplate\tframe_x\tframe_y\troot_x\troot_y\tscore\t"
                     "min_score\tsecond\tpath\tfound\tlocate_ms\n")
    seq, answer, idle, failures = 0, "", 0, 0
    while True:
        cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", *ssh_opts,
               remote, f"bash ~/eq-oracle/uigap-scripts/oracle_remote.sh broker "
                       f"'{remote_dir}' {seq} '{answer}'"]
        try:
            res = subprocess.run(cmd, capture_output=True, timeout=90)
        except subprocess.TimeoutExpired:
            res = None
        if res is None or res.returncode != 0:
            failures += 1
            sys.stderr.write(f"broker: ssh exchange failed (seq {seq}, "
                             f"rc {None if res is None else res.returncode})\n")
            if failures >= 5:
                return 65
            time.sleep(2)
            continue
        failures = 0
        answer = ""
        first, _, rest = res.stdout.partition(b"\n")
        if first == b"IDLE":
            idle += 1
            if idle_limit and idle >= idle_limit:
                return 0
            continue
        idle = 0
        m = re.fullmatch(rb"REQ\t(\d+)\t([A-Za-z0-9_-]+)", first)
        if not m or int(m.group(1)) != seq:
            sys.stderr.write(f"broker: bad reply {first[:80]!r}\n")
            return 65
        name = m.group(2).decode()
        shot = os.path.join(out, f"shot-{seq:03d}.png")
        with open(shot, "wb") as fh:
            fh.write(rest)
        t0 = time.monotonic()
        try:
            tpl_path, min_score = template_entry(name, templates)
            r = locate_arrays(load_luma(shot), load_luma(tpl_path))
            found = 1 if r["score"] >= min_score else 0
            answer = f"{seq} {r['cx']} {r['cy']} {r['score']:.4f} {r['path']} {found}"
            row = [seq, name, r["cx"], r["cy"], r["cx"], r["cy"] + 30,
                   f"{r['score']:.4f}", f"{min_score:.4f}",
                   "-" if r["second"] is None else f"{r['second']:.4f}",
                   r["path"], found]
        except Exception as exc:  # unknown template, unreadable shot
            sys.stderr.write(f"broker: seq {seq} {name}: {exc}\n")
            answer = f"{seq} ERR"
            row = [seq, name, "-", "-", "-", "-", "-", "-", "-", "-", "ERR"]
        row.append(int((time.monotonic() - t0) * 1000))
        with open(log_path, "a") as fh:
            fh.write("\t".join(map(str, row)) + "\n")
        assert ANSWER_RE.match(answer), answer
        seq += 1


# --- CLI ----------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("locate")
    p.add_argument("frame")
    p.add_argument("template")
    p.add_argument("--min-score", type=float, default=DEFAULT_MIN_SCORE)
    p.add_argument("--roi")
    p.add_argument("--verbose", action="store_true")
    p = sub.add_parser("find")
    p.add_argument("frame")
    p.add_argument("name")
    p.add_argument("--templates", default=TEMPLATES)
    p.add_argument("--roi")
    p.add_argument("--verbose", action="store_true")
    for kind in ("cut-sheet", "cut-frame"):
        p = sub.add_parser(kind)
        p.add_argument("source")
        p.add_argument("rect")
        p.add_argument("out")
        p.add_argument("--name", required=True)
        p.add_argument("--note", default="")
        p.add_argument("--xml", required=(kind == "cut-sheet"),
                       help="EQUI_*.xml FILE:LINE naming the sub-rect (sheet) or the label (frame)")
    p = sub.add_parser("broker")
    p.add_argument("--remote", required=True)
    p.add_argument("--remote-dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--templates", default=TEMPLATES)
    p.add_argument("--idle-limit", type=int, default=0)
    p.add_argument("--ssh-opt", action="append", default=[])
    a = ap.parse_args(argv)
    try:
        if a.cmd in ("locate", "find"):
            if a.cmd == "find":
                tpl_path, min_score = template_entry(a.name, a.templates)
            else:
                tpl_path, min_score = a.template, a.min_score
            roi = parse_rect(a.roi) if a.roi else None
            r = locate_arrays(load_luma(a.frame), load_luma(tpl_path), roi)
            print(f"{r['cx']} {r['cy']} {r['score']:.4f} {r['path']}")
            if a.verbose:
                sec = "-" if r["second"] is None else f"{r['second']:.4f}"
                print(f"# left={r['left']} top={r['top']} min_score={min_score} "
                      f"second={sec}", file=sys.stderr)
            return 0 if r["score"] >= min_score else 1
        if a.cmd in ("cut-sheet", "cut-frame"):
            if not re.fullmatch(r"[A-Za-z0-9_-]+", a.name):
                raise ValueError(f"bad template name {a.name!r}")
            xml_ref = getattr(a, "xml", None)
            if xml_ref and not re.fullmatch(r"EQ\w+\.xml:\d+", xml_ref):
                raise ValueError(f"--xml wants EQUI_<x>.xml:<line>, got {xml_ref!r}")
            cut(a.cmd.split("-")[1], a.source, parse_rect(a.rect), a.out, a.name,
                xml_ref, a.note)
            return 0
        if a.cmd == "broker":
            return broker(a.remote, a.remote_dir, a.out, a.templates, a.idle_limit,
                          a.ssh_opt)
    except (OSError, ValueError, KeyError) as exc:
        print(f"oracle_locate: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
