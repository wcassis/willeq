#!/usr/bin/env python3
"""B1059 (#379): glyph-atlas text reader for official-client frames.

Runs on the DRIVER host (dev-u24 / hermes-ext), like oracle_locate.py: the
oracle boxes have no NumPy, Pillow or OpenCV.

Measured (review/B1059/README.md "Measurement"): one string in one control
state over one background is the same binarised bitmap sequence in every
frame, on every rig (lanes 1, 3, 4 llvmpipe, lane 5 Quadro, the dev-u24 Xvfb
rig) and at both window sizes (1280x800, 1024x768); what changes a glyph's
bitmap is its colour state and the background under its anti-aliased edge,
not the rig. One atlas therefore serves every measured rig, holding each
character's measured variants; glyphs/rigs.tsv names the rigs. A rig not listed there has no atlas, and the
callers keep their legacy check (exit 3).

Segmentation (identical on both paths)
--------------------------------------
  1. luma = Pillow convert('L') of the crop (as oracle_locate.py).
  2. mask = luma > med + FRAC * (max - med), med/max over the crop; a crop
     whose max - med < MIN_CONTRAST holds no text (read = "").
  3. 8-connected components of the mask (cv2.connectedComponents, or a numpy
     flood fill); components whose column ranges overlap are one CLUSTER
     (the dot of an 'i' sits over its stem).

Kerning / overlap rule
----------------------
The text is anti-aliased, so at the threshold one glyph can fall apart into
two clusters (the diagonals of 'M', 'N', 'W' are below it) and two touching
glyphs can join into one ("rd", "rt"). A TOKEN is therefore a run of 1..3
adjacent clusters, and an atlas entry's text is one character or a run of
touching characters (learned only where the label alignment pins it). The
reader parses the cluster sequence into tokens left to right (dynamic
programming over every split); every token must match an atlas entry. If the
complete parses disagree on the text, the read is UNKNOWN; if one exists that
uses only exact matches, parses needing NCC are not considered.

Matching
--------
  exact  the token's binary bitmap (h, w, bits) hashes to an atlas key.
  ncc    else TM_CCOEFF_NORMED (cv2 matchTemplate, or the numpy formula) of
         each atlas bitmap (0/1) over the token bitmap padded by one empty
         pixel, for entries within 1 px of its size; the best text must score
         >= ncc_min and beat any other text by >= ncc_margin. ncc_min is
         DERIVED by `build` (atlas.tsv meta): the midpoint between the worst
         score of two bitmaps of the SAME text and the best score of two
         DIFFERENT texts. Measured on the titanium atlas there is no gap
         (same-text worst 0.64, different-text best 1.00: a stem 'l' sits
         inside 'i' at a 1 px offset), so ncc_min=off and every read is EXACT.

Identical bitmaps (measured: 'I' and 'l' are one bitmap at the same height in
this font) form an AMBIGUOUS class; the read prints it as "[Il]" and
`--expect` accepts either member. Spaces: a gap of >= space_gap empty columns
between two tokens (the midpoint between the widest gap inside a word and the
narrowest between words over the labelled crops). An edit field's caret (one
solid column taller than every glyph, at the right end) is dropped with
--edit.

Commands
--------
  read <frame> <x,y,w,h> [--rig R] [--font F] [--edit] [--expect TEXT]
      -> "text<TAB>score<TAB>path" (score = the worst token score, 1.0 for
      exact). Exit 0 read (and, with --expect, it matches); 1 UNKNOWN; 4 read
      but differs from --expect; 3 the rig has no atlas; 2 error.
      ORACLE_LOCATE_FORCE_NUMPY=1 forces the numpy path.
  build <crops.tsv> --rigs lane1,lane5,... [--atlas A]
      crops.tsv (review/B1059/tools/label_crops.py): frame, rect, truth, font,
      rig, edit, use, truth_source; writes glyphs/<A>/atlas.png + atlas.tsv
      (every row cites the frame + rect it was cut from).
"""

import argparse
import hashlib
import os
import re
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
GLYPHS = os.path.join(HERE, "glyphs")

FRAC = 0.5           # mask threshold: med + FRAC * (max - med) (measured, README)
MIN_CONTRAST = 40    # max - med below this: no text in the crop
MAX_GROUP = 3        # clusters per token
PAD = 2              # padding of a token's grey patch (kept for --explain dumps)
UNKNOWN = "UNKNOWN"
ATLAS_COLUMNS = ("text", "font", "key", "h", "w", "dy", "atlas_x", "atlas_y", "seen",
                 "source", "source_sha256", "crop", "glyph_rect", "truth_source")
NO_DESCENDER = set("ABCDEFGHIJKLMNOPRSTUVWXYZabcdefhiklmnorstuvwxz0123456789!?.:%#&*+=<>/\\'\"^~")


# --- images -------------------------------------------------------------------

def have_cv2():
    if os.environ.get("ORACLE_LOCATE_FORCE_NUMPY") == "1":
        return False
    try:
        import cv2  # noqa: F401
        return True
    except ImportError:
        return False


def default_path():
    return "cv2" if have_cv2() else "numpy"


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
    m = re.fullmatch(r"(-?\d+),(-?\d+),(\d+),(\d+)", text or "")
    if not m:
        raise ValueError(f"bad rect {text!r}, want x,y,w,h")
    x, y, w, h = map(int, m.groups())
    if w <= 0 or h <= 0 or x < 0 or y < 0:
        raise ValueError(f"empty or negative rect {text!r}")
    return x, y, w, h


def crop(luma, rect):
    x, y, w, h = rect
    H, W = luma.shape
    if x + w > W or y + h > H:
        raise ValueError(f"rect {rect} outside the {W}x{H} frame")
    return luma[y:y + h, x:x + w]


def text_mask(sub, frac=FRAC):
    import numpy as np
    med, mx = float(np.median(sub)), float(sub.max())
    if mx - med < MIN_CONTRAST:
        return np.zeros(sub.shape, dtype=bool)
    return sub > med + frac * (mx - med)


def glyph_key(bits):
    import numpy as np
    h, w = bits.shape
    packed = np.packbits(bits.astype(np.uint8), axis=None).tobytes()
    return hashlib.sha256(f"{h}x{w}:".encode() + packed).hexdigest()[:16]


# --- connected components -----------------------------------------------------

def components_cv2(mask):
    import cv2
    import numpy as np
    n, labels = cv2.connectedComponents(mask.astype(np.uint8), connectivity=8)
    return labels, n - 1


def components_numpy(mask):
    import numpy as np
    H, W = mask.shape
    labels = np.zeros((H, W), dtype=np.int32)
    n = 0
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys.tolist(), xs.tolist()):
        if labels[y0, x0]:
            continue
        n += 1
        labels[y0, x0] = n
        stack = [(y0, x0)]
        while stack:
            y, x = stack.pop()
            for yy in (y - 1, y, y + 1):
                if 0 <= yy < H:
                    for xx in (x - 1, x, x + 1):
                        if 0 <= xx < W and mask[yy, xx] and not labels[yy, xx]:
                            labels[yy, xx] = n
                            stack.append((yy, xx))
    return labels, n


class Token:
    """1..MAX_GROUP adjacent clusters: bbox (crop coords), bits, grey."""
    __slots__ = ("x0", "x1", "y0", "y1", "bits", "grey", "padded", "n")

    def __init__(self, x0, x1, y0, y1, bits, grey, padded, n=1):
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.bits, self.grey, self.padded, self.n = bits, grey, padded, n

    @property
    def key(self):
        return glyph_key(self.bits)


def segment(luma, rect, path=None, frac=FRAC):
    """-> ([cluster labels as (x0, x1, y0, y1, ids)], labels, sub, path)."""
    import numpy as np
    path = path or default_path()
    sub = crop(luma, rect)
    mask = text_mask(sub, frac)
    labels, n = (components_cv2 if path == "cv2" else components_numpy)(mask)
    boxes = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(labels == i)
        boxes.append([int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max()), [i]])
    boxes.sort(key=lambda b: (b[0], b[2]))
    merged = []
    for b in boxes:
        if merged and b[0] <= merged[-1][1]:
            m = merged[-1]
            m[1] = max(m[1], b[1]); m[2] = min(m[2], b[2]); m[3] = max(m[3], b[3])
            m[4].extend(b[4])
        else:
            merged.append(b)
    return merged, labels, sub, path


def make_token(clusters, labels, sub):
    import numpy as np
    x0 = min(c[0] for c in clusters); x1 = max(c[1] for c in clusters)
    y0 = min(c[2] for c in clusters); y1 = max(c[3] for c in clusters)
    ids = [i for c in clusters for i in c[4]]
    bits = np.isin(labels[y0:y1 + 1, x0:x1 + 1], ids)
    grey = sub[y0:y1 + 1, x0:x1 + 1]
    H, W = sub.shape
    padded = np.full((y1 - y0 + 1 + 2 * PAD, x1 - x0 + 1 + 2 * PAD), float(np.median(sub)))
    py0, px0 = max(0, y0 - PAD), max(0, x0 - PAD)
    py1, px1 = min(H, y1 + PAD + 1), min(W, x1 + PAD + 1)
    padded[py0 - (y0 - PAD):py1 - (y0 - PAD), px0 - (x0 - PAD):px1 - (x0 - PAD)] = sub[py0:py1, px0:px1]
    return Token(x0, x1, y0, y1, bits, grey, padded, len(clusters))


def drop_caret(clusters):
    """--edit: an edit field's caret is one solid column at the right end,
    taller than every other cluster; drop it (measured: 13 rows vs 10-11)."""
    if len(clusters) >= 1:
        c = clusters[-1]
        h = c[3] - c[2] + 1
        others = [o[3] - o[2] + 1 for o in clusters[:-1]]
        if c[0] == c[1] and (not others or h > max(others) + 1):
            return clusters[:-1]
    return clusters


# --- NCC ----------------------------------------------------------------------

def ncc_best(image, tpl, path):
    """Max TM_CCOEFF_NORMED of tpl over image (image >= tpl in both dims)."""
    import numpy as np
    if path == "cv2":
        import cv2
        r = cv2.matchTemplate(image.astype(np.float32), tpl.astype(np.float32), cv2.TM_CCOEFF_NORMED)
        return float(r.max())
    t = tpl.astype(np.float64)
    t = t - t.mean()
    tn = float((t * t).sum())
    if tn <= 0:
        return 0.0
    img = image.astype(np.float64)
    h, w = t.shape
    best = -1.0
    for y in range(img.shape[0] - h + 1):
        for x in range(img.shape[1] - w + 1):
            win = img[y:y + h, x:x + w]
            wz = win - win.mean()
            den = np.sqrt(tn * float((wz * wz).sum()))
            if den > 1e-6 * tn:
                best = max(best, float((wz * t).sum() / den))
    return max(-1.0, min(1.0, best))


# --- atlas --------------------------------------------------------------------

def rig_atlas(rig, atlas_dir=GLYPHS):
    """glyphs/rigs.tsv: rig -> atlas name (measured identical rigs share one)."""
    path = os.path.join(atlas_dir, "rigs.tsv")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            cells = line.rstrip("\n").split("\t")
            if cells[0] == rig:
                return cells[1]
    return None


class Atlas:
    def __init__(self, name, atlas_dir=GLYPHS):
        import numpy as np
        from PIL import Image
        d = os.path.join(atlas_dir, name)
        tsv = os.path.join(d, "atlas.tsv")
        if not os.path.exists(tsv):
            raise FileNotFoundError(f"no atlas {name!r} ({tsv})")
        self.name, self.meta, self.entries = name, {}, []
        header = None
        for line in open(tsv, encoding="utf-8"):
            line = line.rstrip("\n")
            m = re.match(r"#\s*meta\s+(\S+)=(\S*)", line)
            if m:
                self.meta[m.group(1)] = m.group(2)
                continue
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if header is None:
                header = cells
                continue
            self.entries.append(dict(zip(header, cells)))
        png = os.path.join(d, "atlas.png")
        if sha256_file(png) != self.meta.get("atlas_png_sha256"):
            raise ValueError(f"{png} differs from the sha256 in atlas.tsv")
        with Image.open(png) as im:
            sheet = np.asarray(im.convert("L"))
        self.by_key = defaultdict(list)
        for e in self.entries:
            x, y, h, w = int(e["atlas_x"]), int(e["atlas_y"]), int(e["h"]), int(e["w"])
            e["bits"] = sheet[y:y + h, x:x + w] > 127
            e["dy"] = int(e["dy"])
            e["texts"] = e["text"].split("|")
            if glyph_key(e["bits"]) != e["key"]:
                raise ValueError(f"atlas glyph {e['text']!r} at {x},{y} does not hash to {e['key']}")
            self.by_key[e["key"]].append(e)
        self.frac = float(self.meta.get("frac", FRAC))
        self.ncc_min = float(self.meta["ncc_min"]) if self.meta.get("ncc_min", "off") != "off" else None
        self.ncc_margin = float(self.meta.get("ncc_margin", "0.05"))
        self.space_gap = {k[len("space_gap."):]: int(v) for k, v in self.meta.items()
                          if k.startswith("space_gap.")}


def entry_texts(entries, font):
    return sorted({t for e in entries if font is None or e["font"] == font for t in e["texts"]})


def bits_ncc(tok_bits, e_bits, path):
    """TM_CCOEFF_NORMED of an atlas bitmap over the token bitmap padded by
    one empty pixel each side (a 1 px size or offset difference is searched),
    on 0/1 floats."""
    import numpy as np
    h, w = tok_bits.shape
    img = np.zeros((h + 2, w + 2))
    img[1:-1, 1:-1] = tok_bits
    eh, ew = e_bits.shape
    if eh > h + 2 or ew > w + 2:
        return -1.0
    return ncc_best(img, e_bits.astype(np.float64), path)


def match_token(tok, atlas, font, path, allow_ncc=True):
    """-> (texts, how, score, entries): texts = the candidate texts (>1 =
    identical bitmaps of different characters: an ambiguous class)."""
    cands = [e for e in atlas.by_key.get(tok.key, []) if font is None or e["font"] == font]
    if cands:
        return entry_texts(cands, font), "exact", 1.0, cands
    if not allow_ncc or atlas.ncc_min is None:
        return [], "unknown", 0.0, []
    h, w = tok.bits.shape
    best = {}
    for e in atlas.entries:
        if font is not None and e["font"] != font:
            continue
        eh, ew = e["bits"].shape
        if abs(eh - h) > 1 or abs(ew - w) > 1:
            continue
        sc = bits_ncc(tok.bits, e["bits"], path)
        if sc > best.get(e["text"], (-2, None))[0]:
            best[e["text"]] = (sc, e)
    if not best:
        return [], "unknown", 0.0, []
    ranked = sorted(best.items(), key=lambda kv: -kv[1][0])
    top_t, (top_s, top_e) = ranked[0]
    rival = ranked[1][1][0] if len(ranked) > 1 else -1.0
    if top_s >= atlas.ncc_min and top_s - rival >= atlas.ncc_margin:
        return list(top_e["texts"]), "ncc", top_s, [top_e]
    return [], "unknown", top_s, []


def render_class(texts):
    if len(texts) == 1:
        return texts[0]
    if all(len(t) == 1 for t in texts):
        return "[" + "".join(texts) + "]"
    return "[" + "|".join(texts) + "]"


def parse(clusters, labels, sub, atlas, font, path, allow_ncc):
    """All complete token parses -> {rendered text: (score, tokens)}."""
    n = len(clusters)
    memo = {}

    def tok(i, k):
        if (i, k) not in memo:
            t = make_token(clusters[i:i + k], labels, sub)
            memo[(i, k)] = (t, match_token(t, atlas, font, path, allow_ncc))
        return memo[(i, k)]

    # states[i] = list of (pieces, score, tokens) reaching cluster i (capped)
    states = {0: [([], 1.0, [])]}
    for i in range(n):
        for pieces, score, toks in states.get(i, []):
            for k in range(1, MAX_GROUP + 1):
                if i + k > n:
                    break
                t, (texts, how, s, cands) = tok(i, k)
                if not texts:
                    continue
                nxt = states.setdefault(i + k, [])
                if len(nxt) < 64:
                    nxt.append((pieces + [(texts, how, s, cands, t)], min(score, s), toks + [t]))
    out = {}
    for pieces, score, toks in states.get(n, []):
        out.setdefault(tuple(render_class(p[0]) for p in pieces), (score, pieces))
    return out


def assemble(pieces, atlas, font):
    """Join token texts; a gap of >= the font's space_gap empty columns
    between two tokens is a space (the font of the left token's entry)."""
    text = ""
    prev = None
    for texts, how, s, cands, t in pieces:
        if prev is not None:
            gap_min = atlas.space_gap.get(font or prev[0][0]["font"])
            if gap_min is not None and t.x0 - prev[1].x1 - 1 >= gap_min:
                text += " "
        text += render_class(texts)
        prev = (cands, t)
    return text


def read_crop(luma, rect, atlas, font=None, path=None, edit=False):
    """-> dict(text, score, path, how=Counter, parses=n)."""
    clusters, labels, sub, used = segment(luma, rect, path, atlas.frac)
    if edit:
        clusters = drop_caret(clusters)
    if not clusters:
        return {"text": "", "score": 1.0, "path": used, "how": Counter(), "parses": 1}
    for allow_ncc in (False, True):
        parses = parse(clusters, labels, sub, atlas, font, used, allow_ncc)
        if parses:
            break
    texts = {assemble(p, atlas, font) for _, p in parses.values()}
    if len(texts) != 1:
        return {"text": UNKNOWN, "score": 0.0, "path": used, "how": Counter(),
                "parses": len(texts), "candidates": sorted(texts)}
    score, pieces = max(parses.values(), key=lambda v: v[0])
    return {"text": texts.pop(), "score": score, "path": used,
            "how": Counter(p[1] for p in pieces), "parses": len(parses),
            "tokens": [(render_class(p[0]), p[4].n) for p in pieces]}


def text_matches(read, expect):
    """Class-aware equality: '[Il]' in the read accepts 'I' or 'l'."""
    if read == UNKNOWN:
        return False
    pat = ""
    i = 0
    while i < len(read):
        if read[i] == "[":
            j = read.index("]", i)
            body = read[i + 1:j]
            alts = body.split("|") if "|" in body else list(body)
            pat += "(?:" + "|".join(re.escape(a) for a in alts) + ")"
            i = j + 1
        else:
            pat += re.escape(read[i])
            i += 1
    return re.fullmatch(pat, expect) is not None


# --- build --------------------------------------------------------------------

def read_crops_tsv(path):
    rows, header = [], None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if header is None:
            header = cells
            continue
        rows.append(dict(zip(header, cells)))
    return rows


def resolve(frame):
    if os.path.isabs(frame):
        return frame
    return os.path.join(os.environ.get("ORACLE_GLYPHS_REVIEW", "/mnt/projects/fable/review"), frame)


def _baseline(toks, texts):
    b = Counter(t.y1 for t, x in zip(toks, texts) if len(x) == 1 and x in NO_DESCENDER)
    return b.most_common(1)[0][0] if b else max(t.y1 for t in toks)


class Seg:
    """One labelled crop, segmented once."""
    __slots__ = ("r", "clusters", "labels", "sub", "chars", "words", "memo", "how")

    def __init__(self, r, clusters, labels, sub):
        self.r, self.clusters, self.labels, self.sub = r, clusters, labels, sub
        self.words = r["truth"].split(" ")
        self.chars = [c for w in self.words for c in w]
        self.memo = {}
        self.how = None

    def token(self, i, k):
        if (i, k) not in self.memo:
            self.memo[(i, k)] = make_token(self.clusters[i:i + k], self.labels, self.sub)
        return self.memo[(i, k)]


class Builder:
    """Learn atlas entries from labelled crops.

    Pass A: a crop with one cluster per character is assigned 1:1.
    Votes: every bitmap's texts are counted over the assigned crops. A bitmap
    whose minority text has >= CLASS_SHARE of the votes and >= 3 crops is an
    ambiguous class (identical glyphs, e.g. I/l); otherwise the minority
    crops are MISALIGNED (a split glyph and a merged pair in one crop keep
    the count) and lose their assignment. Repeat until stable.
    Pass B: the other crops are aligned by dynamic programming: every token
    that is a known bitmap must carry one of its texts; up to MAX_NEW tokens
    may be new (1..3 clusters <-> 1..3 characters); the alignment must be
    UNIQUE. Then the votes again. Finally every assigned crop is read back
    (exact parse) and must give its truth.
    """
    CLASS_SHARE = 0.25
    MAX_NEW = 3

    def __init__(self, path=None, frac=FRAC):
        self.path = path or default_path()
        self.frac = frac
        self.log = []
        self.assign = {}            # seg index -> (toks, texts)
        self.resolved = {}          # (font, key) -> [texts]

    def votes(self, segs):
        v = defaultdict(Counter)
        for i, (toks, texts) in self.assign.items():
            for t, tx in zip(toks, texts):
                v[(segs[i].r["font"], t.key)][tx] += 1
        return v

    def settle(self, segs):
        while True:
            v = self.votes(segs)
            self.resolved = {}
            for k, c in v.items():
                (t1, n1), = c.most_common(1)
                members = [t1] + [t for t, n in c.items() if t != t1 and len(t) == 1 and len(t1) == 1
                                  and n >= 3 and n >= self.CLASS_SHARE * n1]
                self.resolved[k] = sorted(members)
            bad = [i for i, (toks, texts) in self.assign.items()
                   if any(tx not in self.resolved[(segs[i].r["font"], t.key)] for t, tx in zip(toks, texts))]
            if not bad:
                return
            for i in bad:
                self.log.append(f"misaligned (minority vote, re-queued): {segs[i].r['frame']} "
                                f"{segs[i].r['rect']} {segs[i].r['truth']!r}")
                del self.assign[i]

    def align(self, seg, known):
        """Every solution with the fewest new tokens; unique -> accepted."""
        n, m = len(seg.clusters), len(seg.chars)
        font = seg.r["font"]
        sols = []
        best = [self.MAX_NEW + 1]

        def rec(i, j, toks, texts, new):
            if new > best[0] or len(sols) > 64:
                return
            if i == n and j == m:
                if new < best[0]:
                    best[0] = new
                    sols.clear()
                sols.append((list(toks), list(texts)))
                return
            for k in range(1, MAX_GROUP + 1):
                if i + k > n:
                    break
                t = seg.token(i, k)
                cand = known.get((font, t.key))
                if cand is not None:
                    for alt in cand:
                        if "".join(seg.chars[j:j + len(alt)]) == alt:
                            rec(i + k, j + len(alt), toks + [t], texts + [alt], new)
                elif new < self.MAX_NEW:
                    for L in range(1, MAX_GROUP + 1):
                        if j + L <= m and (k, L) != (MAX_GROUP, MAX_GROUP):
                            rec(i + k, j + L, toks + [t], texts + ["".join(seg.chars[j:j + L])], new + 1)
        rec(0, 0, [], [], 0)
        # among the fewest-new solutions, the ones whose new tokens carry the
        # fewest characters (the most characters pinned by known bitmaps)
        def new_chars(sv):
            return sum(len(tx) for t, tx in zip(*sv) if known.get((font, t.key)) is None)
        if not sols:
            return None
        least = min(new_chars(sv) for sv in sols)
        sols = [sv for sv in sols if new_chars(sv) == least]
        texts = {tuple(tx) + tuple(t.key for t in tk) for tk, tx in sols}
        return sols[0] if len(texts) == 1 else None

    def run(self, rows):
        segs = []
        frames = {}
        for r in rows:
            if r["frame"] not in frames:
                frames[r["frame"]] = load_luma(resolve(r["frame"]))
            clusters, labels, sub, _ = segment(frames[r["frame"]],
                                               parse_rect(r["rect"]), self.path, self.frac)
            if r.get("edit") == "1":
                clusters = drop_caret(clusters)
            segs.append(Seg(r, clusters, labels, sub))
        for i, s in enumerate(segs):
            if s.clusters and len(s.clusters) == len(s.chars):
                self.assign[i] = ([s.token(j, 1) for j in range(len(s.clusters))], list(s.chars))
                s.how = "1:1"
        self.settle(segs)
        progress = True
        while progress:
            progress = False
            known = dict(self.resolved)
            for i, s in enumerate(segs):
                if i in self.assign or not s.clusters:
                    continue
                sol = self.align(s, known)
                if sol is not None:
                    self.assign[i] = sol
                    s.how = "dp"
                    progress = True
            self.settle(segs)
        self.segs = segs
        self.entries = {}
        self.gaps = defaultdict(lambda: ([], []))
        for i, (toks, texts) in sorted(self.assign.items()):
            s = segs[i]
            r = s.r
            base = _baseline(toks, texts)
            x, y, _, _ = parse_rect(r["rect"])
            bounds, pos = set(), 0
            for w in s.words[:-1]:
                pos += len(w)
                bounds.add(pos)
            pos = 0
            for j, (t, tx) in enumerate(zip(toks, texts)):
                if j:
                    (self.gaps[r["font"]][1] if pos in bounds else self.gaps[r["font"]][0]).append(
                        t.x0 - toks[j - 1].x1 - 1)
                pos += len(tx)
                k = (r["font"], t.key)
                e = self.entries.get(k)
                if e is None:
                    self.entries[k] = {"text": "|".join(self.resolved[k]), "texts": self.resolved[k],
                                       "font": r["font"], "key": t.key, "bits": t.bits,
                                       "dy": t.y1 - base, "seen": 1, "source": r["frame"],
                                       "src": resolve(r["frame"]), "crop": r["rect"],
                                       "truth_source": r["truth_source"],
                                       "glyph_rect": f"{x + t.x0},{y + t.y0},{t.x1 - t.x0 + 1},{t.y1 - t.y0 + 1}"}
                else:
                    e["seen"] += 1
        for i, s in enumerate(segs):
            if i not in self.assign:
                self.log.append(f"unaligned: {s.r['frame']} {s.r['rect']} {s.r['truth']!r} "
                                f"({len(s.clusters)} clusters, {len(s.chars)} characters)")
        return [s for i, s in enumerate(segs) if i not in self.assign]

    def derive_ncc(self):
        """ncc_min = midpoint between the worst score of two bitmaps of the
        SAME text and the best score of two bitmaps of DIFFERENT texts (same
        font, within 1 px of size); no gap -> NCC off."""
        ents = list(self.entries.values())
        same, diff = [], []
        for i, a in enumerate(ents):
            for b in ents[i + 1:]:
                if a["font"] != b["font"]:
                    continue
                (ah, aw), (bh, bw) = a["bits"].shape, b["bits"].shape
                if abs(ah - bh) > 1 or abs(aw - bw) > 1:
                    continue
                sc = max(bits_ncc(b["bits"], a["bits"], self.path), bits_ncc(a["bits"], b["bits"], self.path))
                rec = (sc, a["text"], b["text"], a["glyph_rect"], a["source"], b["glyph_rect"], b["source"])
                (same if set(a["texts"]) & set(b["texts"]) else diff).append(rec)
        self.ncc_pairs = (sorted(same), sorted(diff, reverse=True))
        lo = min(same)[0] if same else None
        hi = max(diff)[0] if diff else None
        if lo is not None and hi is not None and lo > hi:
            return round((lo + hi) / 2, 4), lo, hi
        return None, lo, hi


def write_atlas(b, name, atlas_dir, rigs, ncc, note=""):
    import numpy as np
    from PIL import Image
    d = os.path.join(atlas_dir, name)
    os.makedirs(d, exist_ok=True)
    entries = sorted(b.entries.values(), key=lambda e: (e["font"], e["text"], e["key"]))
    W, x, y, row_h = 256, 0, 0, 0
    for e in entries:
        h, w = e["bits"].shape
        if x + w > W:
            x, y, row_h = 0, y + row_h + 1, 0
        e["atlas_x"], e["atlas_y"] = x, y
        x += w + 1
        row_h = max(row_h, h)
    bits = np.zeros((max(1, y + row_h), W), dtype=np.uint8)
    for e in entries:
        h, w = e["bits"].shape
        bits[e["atlas_y"]:e["atlas_y"] + h, e["atlas_x"]:e["atlas_x"] + w] = e["bits"] * 255
    png = os.path.join(d, "atlas.png")
    Image.fromarray(bits).save(png)
    ncc_min, worst_same, best_diff = ncc
    fmt = lambda v: "-" if v is None else f"{v:.4f}"  # noqa: E731
    with open(os.path.join(d, "atlas.tsv"), "w", encoding="utf-8") as fh:
        fh.write(f"# B1059 glyph atlas {name!r}: one row per distinct glyph bitmap, built by\n"
                 f"# `oracle_glyphs.py build` from labelled crops (truth_source: the log/plan/pcap/\n"
                 f"# DB/XML side the label came from, never the image); source + glyph_rect cite\n"
                 f"# the frame pixels each bitmap was cut from. Built from rigs: {','.join(rigs)}.\n"
                 f"# text 'a|b' = identical bitmaps of different characters (an ambiguous class);\n"
                 f"# a multi-character text = touching glyphs (one cluster). {note}\n")
        fh.write(f"# meta frac={b.frac}\n# meta atlas_png_sha256={sha256_file(png)}\n")
        fh.write(f"# meta ncc_min={'off' if ncc_min is None else ncc_min}\n# meta ncc_margin=0.05\n")
        fh.write(f"# ncc over the atlas: worst between bitmaps of one text {fmt(worst_same)}, "
                 f"best between different texts {fmt(best_diff)}\n")
        for font, (intra, inter) in sorted(b.gaps.items()):
            if intra and inter and max(intra) < min(inter):
                fh.write(f"# meta space_gap.{font}={(max(intra) + min(inter) + 1) // 2}\n")
            fh.write(f"# font {font}: gap inside words max {max(intra) if intra else '-'}, "
                     f"between words min {min(inter) if inter else '-'}\n")
        fh.write("\t".join(ATLAS_COLUMNS) + "\n")
        for e in entries:
            h, w = e["bits"].shape
            fh.write("\t".join(str(v) for v in (
                e["text"], e["font"], e["key"], h, w, e["dy"], e["atlas_x"], e["atlas_y"], e["seen"],
                e["source"], sha256_file(e["src"]), e["crop"], e["glyph_rect"],
                e["truth_source"])) + "\n")


# --- CLI ----------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("read")
    p.add_argument("frame")
    p.add_argument("rect")
    p.add_argument("--rig", default=os.environ.get("ORACLE_GLYPHS_RIG", "lane5"))
    p.add_argument("--font")
    p.add_argument("--edit", action="store_true")
    p.add_argument("--expect")
    p.add_argument("--atlas-dir", default=GLYPHS)
    p.add_argument("--explain", action="store_true")
    p = sub.add_parser("build")
    p.add_argument("crops")
    p.add_argument("--atlas", default="titanium")
    p.add_argument("--rigs", required=True)
    p.add_argument("--atlas-dir", default=GLYPHS)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "read":
            name = rig_atlas(a.rig, a.atlas_dir)
            if name is None:
                print(f"oracle_glyphs: no atlas for rig {a.rig!r} (glyphs/rigs.tsv)", file=sys.stderr)
                return 3
            atlas = Atlas(name, a.atlas_dir)
            r = read_crop(load_luma(a.frame), parse_rect(a.rect), atlas, a.font, edit=a.edit)
            print(f"{r['text']}\t{r['score']:.4f}\t{r['path']}")
            if a.explain:
                print(f"# how {dict(r['how'])} parses {r['parses']} {r.get('candidates', '')}",
                      file=sys.stderr)
            if r["text"] == UNKNOWN:
                return 1
            if a.expect is not None and not text_matches(r["text"], a.expect):
                return 4
            return 0
        if a.cmd == "build":
            rigs = a.rigs.split(",")
            rows = [r for r in read_crops_tsv(a.crops) if r["rig"] in rigs and r.get("use", "build") == "build"]
            b = Builder()
            pending = b.run(rows)
            ncc = b.derive_ncc()
            for msg in b.log:
                print(f"build: {msg}", file=sys.stderr)
            write_atlas(b, a.atlas, a.atlas_dir, rigs, ncc)
            print(f"{a.atlas}: {len(b.entries)} glyph bitmaps, "
                  f"{len({t for e in b.entries.values() for t in e['texts']})} texts, "
                  f"{len(rows) - len(pending)}/{len(rows)} crops aligned, ncc_min {ncc[0]} "
                  f"(same-text worst {ncc[1]}, different-text best {ncc[2]})")
            return 0
    except (OSError, ValueError, KeyError) as exc:
        print(f"oracle_glyphs: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
