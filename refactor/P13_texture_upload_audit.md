# P13 — Zone-in texture upload audit: what are we actually loading, and why does 15MB of 1999 assets evict a 64MB budget?

Approved by Will 2026-07-27 ("go ahead with P13").
Slot: wt/slot1, branch task/P13-texture-audit, cut from fable (827719a).
AUDIT unit — instrument, measure, reconstruct, recommend. REPORT-ONLY:
no loading-path fixes land here (they get their own unit(s) from the
ranked list); instrumentation must be flag/env-gated and additive.

## Motivation (Will, 2026-07-27)

All assets are original 1999/2000 launch data. qeynos2's three archives
are 9.3MB on disk / ~15.4MB extracted (148 zone + 472 chr textures —
7.1MB as RGBA for ALL of chr). A 1999 machine ran this zone in 4MB of
texture memory. Yet P10b measured 71 ConstrainedTextureCache evictions
during a qeynos2 zone-in under the 64MB orangepi budget. Something
loads far more than the era's referenced set. Candidates:
1. **Composite sheets (our own P08d flip)**: 479 sheets avg 2.2MB RGBA,
   182 at >=2048px (8-16MB each); the builder's tile-unrolling
   multiplies pixels vs source textures; ~184 entities across many
   races → sheets alone may exceed 64MB on the RGBA path.
2. **Wholesale archive loading (Will's historical suspicion)**: are we
   uploading only referenced textures, or everything an opened S3D
   contains? ("In the past, every s3d that was opened was being
   cached.")
3. **Double-loading**: do composite-eligible races upload their
   per-part textures AND their sheet?

## Design

### A. Instrumentation (flag/env-gated, additive)

1. A texture-upload LEDGER: every driver texture creation during zone
   load + entity spawn-in records name, dimensions, bytes-as-uploaded,
   format (RGBA/565/4444/ETC1), source (which archive/loader/callsite:
   zone regions, objects, entity per-part, composite sheet, sky,
   detail, UI, placeholder), and — where cheap — what references it.
   Build on P04's LOAD-MEM-AUDIT pattern and the existing /pmem
   bookkeeping; ledger dumps to a file at a marker (zone-in complete).
2. Eviction tracing: each eviction logs victim name/bytes/category and
   whether it is RE-uploaded later in the same session (thrash count).

### B. Measurement runs (x86, eqemu harness, production orangepi preset)

3. qeynos2 zone-in, composites ON (the shipped config): full ledger +
   eviction reconstruction of the 71-eviction scenario.
4. qeynos2 zone-in, composites OFF: the differential isolates the
   sheets' contribution exactly.
5. befallen zone-in, composites ON: the no-eviction contrast case.
6. (Code-level, no run needed) Determine which upload format each
   category ACTUALLY takes on x86 vs Pi: where eqe1/ETC1 engages, what
   the preset's enableCompressedTextures=false actually controls, and
   what the sheets upload as on each platform.

### C. Analysis deliverables

7. **The ledger tables**: totals by category for each run; top-20
   largest uploads; uploads-never-referenced list (the wholesale
   symptom); double-load list (per-part textures uploaded for
   composite-eligible races).
8. **Sheet economics**: per-sheet uploaded bytes; unroll inflation
   factor (sheet pixels ÷ sum of distinct source texture pixels) for
   the qeynos2-relevant sheets; total sheet bytes vs total everything-
   else.
9. **The era-honest arithmetic**: what qeynos2 SHOULD cost
   (referenced-only set, at RGBA and at ETC1) vs what it DOES cost —
   the direct answer to Will's question.
10. **Eviction reconstruction**: what was evicted, in what order, what
    thrashed.
11. **Ranked fix list** with projected savings each (candidates to
    evaluate, not presume: ETC1/eqe1 sheets on all paths per the P03
    decision; lazy per-visible-race sheet loading; skip per-part
    uploads for composite-eligible races; builder sheet-size/unroll
    discipline; referenced-only archive loading if wholesale loading is
    confirmed; budget right-sizing LAST — the budget is probably fine,
    the load is wrong).

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Instrumentation off-by-default proof (ledger flag off = byte-
  identical behavior; a spot render compare suffices).
- Server use sanctioned; clean shutdown; configs restored.
- Ledgers/analysis → benchmarks/audits/p13_texture_ledger/ (committed;
  they're small text/JSON).

## Out of scope

- Landing any loading-path fix (ranked list only).
- Pi sessions (format differences established at code level).
- The merge, animation, foliage-alpha, pop-in threads.

## Review

(executor fills in)
