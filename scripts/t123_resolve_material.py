#!/usr/bin/env python3
"""
scripts/t123_resolve_material.py — T123 extension of the T122 ring:
resolve each UAF TEXPARAMS entry to the SMaterial* the driver held for that
write.

Reuses scripts/t122_extract_ring.py's output format (now including the new
MATERIAL event kind, EventKind::MATERIAL=4 in t122_texprobe.h) and
scripts/t122_classify_ring.py's UAF/TYPE_CONFUSE/LEGIT classification logic.

For each TEXPARAMS entry, COpenGLES2Driver.cpp records a MATERIAL entry
(the real, non-copied SMaterial* -- see t122_texprobe.h's header comment)
on the SAME thread immediately before it, with nothing else in that
translation unit recording between them. So the nearest-preceding MATERIAL
entry with the same `tid`, in per-thread seq order, is that write's real
material address -- regardless of whether the call went through the
built-in path (applyMaterialState) or the custom-shader path
(applyCustomShaderState, where the TEXPARAMS entry's own call frame sees
only a local `baseMat` copy -- this is exactly why MATERIAL is NOT recorded
inside applyMaterialRenderState() itself).

Usage:
    python3 scripts/t123_resolve_material.py <ring.tsv>

Output: one line per UAF (and TYPE_CONFUSE_*) TEXPARAMS entry:
    seq=<N> tex_ptr=<0x..> class=<UAF|...> material_ptr=<0x..|UNRESOLVED>

`material_ptr` is what the next step (gdb against the core, matched against
mesh-buffer/node-override material addresses in known holder containers)
resolves to a specific owner.
"""
import sys
from collections import defaultdict


def classify(events, texparams):
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
        findings.append((seq, ptr, value, cls))
    return findings


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <ring.tsv>", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]

    events = defaultdict(list)          # tex ptr -> [(seq, kind), ...]
    texparams = []                      # [(seq, ptr, value), ...]
    material_by_tid = defaultdict(list)  # tid -> sorted [(seq, matptr), ...]

    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("seq"):
                continue
            seq, kind, ptr, tid, value = line.rstrip("\n").split("\t")
            seq = int(seq)
            if kind == "MATERIAL":
                material_by_tid[tid].append((seq, ptr))
                continue
            events[ptr].append((seq, kind))
            if kind == "TEXPARAMS":
                texparams.append((seq, ptr, value, tid))

    for ptr in events:
        events[ptr].sort()
    for tid in material_by_tid:
        material_by_tid[tid].sort()

    # texparams tuples carry tid too (4th element) -- classify() only wants
    # (seq, ptr, value), so unpack accordingly.
    findings = classify(events, [(s, p, v) for s, p, v, _ in texparams])
    tid_of = {(s, p): t for s, p, v, t in texparams}

    def resolve_material(seq, tid):
        candidates = material_by_tid.get(tid, [])
        best = None
        for mseq, mptr in candidates:
            if mseq < seq:
                best = mptr
            else:
                break
        return best if best is not None else "UNRESOLVED"

    non_legit = [f for f in findings if f[3] != "LEGIT"]
    print(f"Total TEXPARAMS: {len(findings)}, non-LEGIT: {len(non_legit)}")
    for seq, ptr, value, cls in non_legit:
        tid = tid_of.get((seq, ptr), "0x0")
        matptr = resolve_material(seq, tid)
        print(f"seq={seq} tex_ptr={ptr} class={cls} tid={tid} material_ptr={matptr}")


if __name__ == "__main__":
    main()
