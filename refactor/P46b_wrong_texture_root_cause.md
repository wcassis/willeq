# P46b — P46 AFTER-renders show WRONG TEXTURES on body parts: root cause + fix

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Will REJECTED the P46
bless pair (round 3 of the P37 visual-evidence saga): "BEFORE looks
correct, AFTER has the wrong texture for some skeleton body parts and
some zombie body parts." Orchestrator confirmed on the crops:
skeleton forearms/shins render orange/saturated; zombie chest shows a
glowing amber texture that doesn't belong; zombie skirt region partly
cut away. Will's eye has been right every round — treat his
description as ground truth.

## The key clue

The ETC1 (primary) and RGBA (secondary) legs show IDENTICAL AE counts
and near-identical diff boxes (see
benchmarks/audits/p46_bless/EVIDENCE_MANIFEST.txt) — the same visual
wrongness through two different texture-delivery pipelines. Suspects
must be code shared by both legs.

## Ranked hypotheses (verify, don't assume)

1. **Part A's loop conversion in race_model_loader.cpp (e55ba17)**:
   the T05 per-part decode loop was converted from a range-for over
   textureNames() to an indexed loop so textureMasked(i) could be
   consulted. If the index↔texture↔part correspondence slipped
   (parallel-array mismatch, off-by-one, skip/continue reordering),
   parts get the wrong texture in BOTH modes. Check against the
   pre-P46 loop's exact pairing; diff the resulting per-part
   texture-name assignment (log both, same race) old-vs-new.
2. **Cache-key collision in ConstrainedTextureCache**: getOrLoad now
   takes applyPaletteMask but the cache may key on name only — the
   same texture name loaded masked-first then unmasked (or vice
   versa, incl. across races sharing texture names) returns the
   wrong variant. Check the key; check whether zombie/skeleton share
   any texture names with differently-masked usage.
3. **Palette-0 masking misfire**: EQ's convention may not be
   "palette index 0" for every masked texture class (the zombie
   skirt cutaway could be legitimate black texels at index 0 being
   discarded). Compare against eqe1_builder's ACTUAL behavior —
   including any preconditions it applies before treating index 0 as
   transparent — and against what the live ETC1 companions contain
   for the affected textures.
4. Part B's benchmark binding (d19e325) — only if 1-3 exonerate
   shared code (unlikely: RGBA leg shows the symptom and takes Part
   B's RGBA branch, which only switches shader variant).

## Work

A. **Bisect matrix first** (same invocation/framing as P46's
   evidence, both legs, ske+zom): fda2e34 (pre-P37 control), 5420886
   (P37+P42, pre-P46 — does the wrongness predate P46?), tip with
   Part A reverted, tip with Part B reverted, tip. This attributes
   the symptom to a commit before any code reading. If the wrongness
   already exists at 5420886, P37/P42 are implicated and the scope
   escalates — STOP and report before fixing (production
   implications for the Pi tier).
B. Root-cause at file:line, fix, and prove the fix: AFTER renders
   must show (a) correct textures on every body part (compare
   against BEFORE for non-masked regions — byte-identical except
   genuine mask cutouts), (b) the rib-gap cutout still working,
   (c) unmasked races still md5-identical to pre-P46.
C. Regenerate the bless pairs (P44b method verbatim, both legs,
   ske+zom, same frames) from the FIXED tip. View your own crops
   critically before delivering: every body part must look RIGHT,
   not just diff-confined.
D. Annotate P46's Review + manifest: evidence superseded (rejected
   round noted), pointer to this unit.

## Rules

- Full bar (GLES2 ctest baseline ~1529/1530, visual gate PASS
  goldens untouched, ARM clean, file sizes). Separate commits: fix
  vs evidence vs annotations.
- If the bisect implicates pre-P46 commits (P37/P42 era): STOP after
  the matrix with the attribution memo — that decision is above this
  unit.
- Diff-confinement is NO LONGER sufficient evidence — P46's pairs
  passed it while being wrong. The new bar is per-part texture
  correctness verified by name-level logging AND by eye.

## Review

(executor fills in)
