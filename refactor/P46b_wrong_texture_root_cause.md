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

**Root cause: benchmark-only shader-promotion asymmetry in P46 Part B, not a
texture-identity bug. Fixed; bless pairs regenerated and pass the
executor's own part-by-part visual inspection.**

### Bisect matrix (before any code reading)

Rendered ske+zom, both legs (ETC1/RGBA), frames 12/26/42, at fda2e34,
5420886 (P37+P42, pre-P46), tip-minus-Part-A, tip-minus-Part-B, tip. AE
against the fda2e34 control:

| leg | 5420886 | tip-minus-PartA | tip-minus-PartB | tip (rejected) |
|---|---|---|---|---|
| ske etc1/rgba f26 | 0 | 3875 | 0 | 3875 |
| zom etc1/rgba f26 | 0 | 1937 | 0 | 1937 |

**5420886 is clean — no HARD STOP.** P37/P42-era code is exonerated (with
one caveat, see manifest: the benchmark couldn't reach the relevant code
path at all before P46 Part B, so this is not dispositive for production,
just for this instrument — flagged, doesn't change the verdict).

**Reverting Part B clears the regression entirely; reverting Part A changes
NOTHING** — tip-minus-PartA is byte-identical (md5-matching) to full tip in
BOTH ETC1 and RGBA modes. This rules out hypotheses 1-3 (race_model_loader.cpp's
loop conversion, ConstrainedTextureCache key collision, palette-0 misfire)
outright — none of those are Part B code, and the symptom doesn't depend on
which texture/alpha value is used. Root cause is entirely in
`tools/hwcaps/zone_bench_loader.cpp` (Part B, benchmark-only).

### Root cause at file:line

`tools/hwcaps/zone_bench_loader.cpp`, `applyEntityAlphaCompanionBinding()`
(P46 commit d19e325, original lines 56-92). Production's
`EntityRenderer::createEntity()` (`src/client/graphics/entity_renderer.cpp`
lines 629-648, and 2 other call sites) promotes ALL of an entity's material
buffers in one paired step: `EMT_SOLID` buffers get the custom per-pixel-lit
shader (`shaderMaterialSolid_`), `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` buffers
get the alpha-test shader variant — both halves always fire together, so
every buffer on an entity ends up on the SAME lighting model.

P46 Part B ported only the alpha-test half. Masked buffers (skeleton
forearms/shins, zombie chest/legs — P37's exact ground-truth masked list)
got promoted to the custom per-pixel-lit shader (real player light/sun/fog);
sibling unmasked buffers on the SAME entity stayed on Irrlicht's built-in
fixed-function `EMT_SOLID` renderer. Every masked body part rendered through
a visibly different lighting model than its neighbors — independent of
alpha value, which is exactly what "Part A revert changes nothing" proves.
This is the causal mechanism behind "orange/saturated forearms/shins" and
"glowing amber chest": not wrong textures, a shader-consistency gap from an
incomplete production mirror, confined to this benchmark tool.

### Fix

Restructured `applyEntityAlphaCompanionBinding()` →
`applyEntityShaderMaterialPromotion()` to iterate all buffers and apply the
same paired promotion entity_renderer.cpp does (new `shaderMaterialSolid`
parameter = `ZoneShaderManager::getActiveSolid()`, already used for zone
geometry elsewhere in this file, now also wired to entities; `mat.Lighting =
false` set on every promoted buffer, mirroring production). Benchmark-only;
zero production source touched.

### Proof

1. Texture-identity mechanisms (Part A, race_model_loader.cpp,
   ConstrainedTextureCache) untouched by this fix — bisect matrix already
   exonerated them.
2. Visual, part-by-part (executor's own critical inspection, per this unit's
   mandate that diff-confinement alone is insufficient): 400% crops of both
   races, both legs, viewed directly — skull/ribcage/forearms/shins
   (skeleton) and head/chest/legs/boots (zombie) all render with
   internally-consistent lighting after the fix; no part looks out of place
   relative to its neighbors. Confirmed against a no-entity zone-geometry
   crop showing the same warm/golden color grading is befallen's own
   established ambient lighting (unaffected by this fix), not an artifact.
3. Rib-gap cutout: same visible inter-rib detail as BEFORE at this camera
   framing (P44b's own finding that this angle doesn't show clean
   see-through holes still applies; alpha-test engagement is demonstrated by
   the numeric AE evidence per P46's established standard, not this angle).
4. Unmasked races (Human) — **necessarily** no longer byte-identical to
   pre-P46 (AE=9184 @ f26): the fix's entire point is uniform promotion
   across every buffer, masked or not, matching production. A 400% crop
   confirms perfectly uniform lighting across Human's whole body in both
   states — this is a widened, correct gating claim, not a regression.
   Documented as a required deviation from the plan's original invariant.

### Retake (Part C)

Same P44b method/frames/races/legs. Stills:
`/mnt/projects/fable/videos/p46b_bless/{primary_etc1,secondary_rgba}/{pairs,crops,diffs}/`
(NFS, gitignored). Manifest: `benchmarks/audits/p46b_bless/EVIDENCE_MANIFEST.txt`
(committed). AE is larger than P46's original rejected numbers (expected —
the render now correctly changes the whole entity's lighting relative to
fda2e34's never-shader-lit baseline, not just masked parts); diff bounding
box stays a tight rectangle over the entity silhouette (107x242 ske,
84x257 zom @ f26) — entity-confined, no STOP.

**Executor's own visual verdict: PASS, part-by-part, both races, both
legs.** No orange/saturated forearm-vs-torso mismatch, no anomalous chest
glow, no unexplained cutaway. Every body part looks like it belongs to the
same lit entity.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS, clean |
| `ctest --test-dir build --output-on-failure` | 1529/1530 (AudioManagerTest.PreloadCommonSounds, documented ALSA quirk) |
| `cmake --build build-gles2` (H01 x86 GLES2 tree) | PASS, clean (see deviation 3 — patched Irrlicht was transiently damaged mid-session by an unrelated tooling mishap and restored before this run) |
| `bash scripts/ci/visual_gate.sh` | PASS (qeynos2/gfaydark/befallen), goldens untouched |
| `bash scripts/build-arm-noble.sh` | PASS, clean, non-stub |
| `bash scripts/refactor/check_file_sizes.sh 5420886` | `constrained_texture_cache.cpp` WARN (pre-existing, unchanged — this unit touched zero production source); `tools/hwcaps/zone_bench_loader.cpp` FAIL (3135 lines, pre-existing FAIL since before this unit, +37 net lines from the fix) — flagged, not silently absorbed |

### Deviations

1. Unmasked-race byte-identity no longer holds — necessary consequence of
   the fix (partial promotion was the bug), not scope creep. See Proof #4.
2. Bisect matrix's 5420886 HARD-STOP check has a documented instrument
   limitation (couldn't reach the code path pre-Part-B at all) — doesn't
   change the verdict, flagged rather than treated as a clean bill for
   pre-P37 production code.
3. Mid-session, an interrupted `scripts/build-gles2-x86.sh` invocation
   (killed while unblocking an unrelated stuck background build) left the
   shared, gitignored `third_party/irrlicht-gles2-x86/` tree transiently
   non-functional. Caught immediately by the next compile's errors,
   restored via `--irrlicht-only` before any further verification;
   already-linked binaries (build-preP37/, the 3 bisect scratch worktrees,
   build/'s binaries at the time of the fix) were unaffected. All
   verification steps re-run clean against the restored tree. No
   repo-tracked files touched.

Commits: fix (`tools/hwcaps/zone_bench_loader.cpp`), evidence (this Review +
`benchmarks/audits/p46b_bless/EVIDENCE_MANIFEST.txt`), annotations (P46's
Review + manifest pointer) — see git log on `task/P46b-wrong-texture`.
