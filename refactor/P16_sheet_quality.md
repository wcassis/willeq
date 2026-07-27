# P16 — Composite sheet quality: kill the atlas-seam artifact + unroll discipline, regenerate, deploy

Approved by Will 2026-07-27 ("go ahead with all three").
Slot: wt/slot3, branch task/P16-sheet-quality, cut from fable tip.
Needs NO Pi and NO server (offline tool + x86 benchmark evidence).
NOTE: slot3's build dirs are months stale — first builds will be long;
run them foreground with timeouts and patience.

## Motivation

Two confirmed artifacts of entity_composite_builder's sheet layout:
(a) the atlas-seam diff — visible as dark patches at tile boundaries in
close-ups (P08d round-5b, on garbled geometry) and as the P12b Z35
verdict (17-29% px at LOW magnitude, mean ~0.1-2/255, localized to a
seam, under animation) — classic bilinear-bleed-at-tile-boundary;
(b) 3.25x average unroll inflation (up to 6.06x), P13's sheet
economics. Same tool, one unit. Sheets are LIVE on the Pi preset —
deployment is gated on evidence.

## Design

1. **Seam fix (builder-side preferred)**: gutter/padding texels around
   each tile (duplicate edge texels into the pad) so bilinear sampling
   at tile edges never reads a neighbor; adjust the JSON UV metadata
   accordingly (runtime lookup/keying UNCHANGED — verify against
   race_model_loader.cpp, do not touch runtime code unless a UV inset
   is strictly required, and justify if so).
2. **Unroll discipline**: cap/reduce pre-tiled repetition where the
   inflation factor is extreme (P13 documented up to 6.06x) — e.g.
   tile only to the actual max UV extent used by the geometry, or cap
   repeats and rely on the pad for edge correctness. Target: meaningful
   sheet-byte reduction (P13 ranked this ~50-65%) WITHOUT visual
   regression.
3. Regenerate ALL sheets → a STAGING dir on NFS (not the live dir).
4. **Evidence before deployment** (x86 benchmark, P12b's aligned
   animated method + P08d's static close-up method, against staged
   sheets via --eqe1-path pointed at staging):
   - seam diff vs multibuffer: gone or materially reduced (quantified
     vs P12b's 17-29% baseline);
   - static close-ups clean (no new artifacts, no visible tiling
     errors on large tiled surfaces — check a robed model and a large
     tiled-texture body part specifically);
   - structural audit (P08f method) 100% clean;
   - total bytes: before/after per sheet + aggregate.
5. **Deploy**: back up the live sheet set to a dated NFS dir, swap the
   staged set into /mnt/projects/EverQuest_ETC1, record both paths.
   (Composites are live on the Pi preset — the swap is a deployment;
   rollback = restore the backup dir.)
6. Contact sheet for Will → benchmarks/contact_sheets/p16_sheet_quality/
   (before/after close-ups incl. the seam site; no self-declared
   acceptability — but deployment proceeds on the quantified evidence
   per Will's parallel-batch approval, with the backup as the safety).

## Verification
- Standard bar; the evidence set of design 4; audit JSONs →
  benchmarks/audits/p16_sheet_quality/.

## Out of scope
- Runtime loader changes (unless a justified UV inset); per-player
  baking; foliage alpha (different system); the WOL collision.

## Review
(executor fills in)
