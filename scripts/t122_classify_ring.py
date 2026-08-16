#!/usr/bin/env python3
"""
scripts/t122_classify_ring.py — classify a T122 ring TSV
(scripts/t122_extract_ring.py's output) against the plan's three questions.

Usage:
    python3 scripts/t122_classify_ring.py <ring.tsv>

For every TEXPARAMS entry, walks the CREATE/DESTROY history of the same
pointer (`ptr` column) and classifies it:

  LEGIT              - the pointer's most recent CREATE (before this write)
                        is more recent than its most recent DESTROY (or it
                        was never destroyed yet) -> the object is live.
                        cacheTexParams() writing here is legitimate.
  UAF                - the pointer's most recent event before this write is
                        a DESTROY with no intervening CREATE -> a
                        use-after-free write through cacheTexParams()
                        (T122 plan question 1).
  TYPE_CONFUSE_NEVER  - the pointer never appears in CREATE at all, ever in
                        this run -> it was never a COGLES2Texture; whatever
                        material.getTexture(0) returned is some other kind
                        of object (T122 plan question 2).
  TYPE_CONFUSE_LATER  - the pointer does get a CREATE eventually, but only
                        AFTER this TEXPARAMS write (in ring seq order) ->
                        also type confusion / wrong pointer, reported
                        separately because it is an even stronger signal
                        than TYPE_CONFUSE_NEVER (the address hadn't even
                        become a texture yet when written).

If TEXPARAMS entries are 100% LEGIT, cacheTexParams() is exonerated for that
run (T122 plan question 3) — say so plainly, don't stretch a UAF/type-
confusion narrative onto legitimate data.
"""
import sys
from collections import defaultdict


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <ring.tsv>", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]

    events = defaultdict(list)  # ptr -> [(seq, kind), ...]
    texparams = []  # [(seq, ptr, value), ...]
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("seq"):
                continue
            seq, kind, ptr, tid, value = line.rstrip("\n").split("\t")
            seq = int(seq)
            events[ptr].append((seq, kind))
            if kind == "TEXPARAMS":
                texparams.append((seq, ptr, value))

    for ptr in events:
        events[ptr].sort()

    counts = defaultdict(int)
    findings = []
    for seq, ptr, value in texparams:
        hist = events[ptr]
        prior = [e for e in hist if e[0] < seq]
        any_create_before = any(k == "CREATE" for _, k in prior)
        any_create_ever = any(k == "CREATE" for _, k in hist)
        if not any_create_ever:
            cls = "TYPE_CONFUSE_NEVER"
        elif not any_create_before:
            cls = "TYPE_CONFUSE_LATER"
        else:
            last_create_seq = max(s for s, k in prior if k == "CREATE")
            last_destroy_seq = max([s for s, k in prior if k == "DESTROY"], default=-1)
            cls = "UAF" if last_destroy_seq > last_create_seq else "LEGIT"
        counts[cls] += 1
        if cls != "LEGIT":
            findings.append((seq, ptr, value, cls, prior[-5:]))

    print(f"Total TEXPARAMS entries: {len(texparams)}")
    for cls, n in sorted(counts.items()):
        print(f"  {cls}: {n}")
    print()
    print(f"Non-legit findings ({len(findings)} total, up to 20 shown):")
    for seq, ptr, value, cls, ctx in findings[:20]:
        print(f"  seq={seq} ptr={ptr} value={value} class={cls} recent_history={ctx}")


if __name__ == "__main__":
    main()
