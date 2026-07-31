# P46d — Complete P46 Part A: mask-aware cache keying + ordering audit + the round-5 pair

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Production change: this
completes the P46 Part A fix Will already authorized — P46c proved
Part A is load-order-dependent (a latent defect), which was never the
intent of the approved fix.

## Background (read first)

refactor/P46c_neutral_pair_stop.md — full root cause with file:line:
`ConstrainedTextureCache::getOrLoad()` (constrained_texture_cache.cpp
:111-196) keys by texture name only; `applyPaletteMask` is honored
only on the first decode of a name. Any unmasked-first load of a
masked texture's name (benchmark static preload via
zone_geometry.cpp:249 today; potentially production paths) poisons
the cache and silently defeats the masking. Also read
refactor/P46_masked_alpha_decode.md and P46b/P46c manifests.

## Work

A. **Fix the keying**: make the cache mask-aware — key on
   (name, applyPaletteMask) or equivalent variant separation. Watch
   the interaction points: P13b's releaseRawTextureData deferred-name
   skip list, P15/P17's eviction/clear paths, the eviction listeners
   (P10b/P17 guards) — anything keyed by name that must now handle
   the variant dimension. Memory cost is bounded (only WLD-masked
   textures ever request the masked variant — dozens, not thousands).
B. **Production ordering audit**: enumerate every production path
   that can load an entity-masked texture name (race_model_loader,
   equipment_model_loader, composite builder, zone builder, UI/misc
   getOrLoad callers) and determine whether ANY unmasked-first
   ordering can occur in the live client (P46c only proved it for the
   benchmark's preload). With the Part-A fix keyed correctly this
   becomes moot for correctness, but document what the exposure WAS —
   it determines whether the pre-fix window shipped wrong rendering.
C. **Re-verify the discard**: rerun P46c's discard-fire check (RGBA
   mode, --no-entity-shader-promotion, tip vs fda2e34) — it must now
   fire, with mask-shaped diffs.
D. **Capture the round-5 neutral pair** per P46c's design, now
   unblocked: RGBA mode, promotion OFF both legs, deterministic
   frames 12/26/42, orbit-radius 8 / height-offset 6, SKE + ZOM,
   400% torso crops. ACCEPTANCE ABSOLUTE (round 5): pixel-identical
   everywhere except genuine mask cutouts — assert numerically AND
   verify with your own eyes part-by-part; any other delta = STOP.
   Stills → /mnt/projects/fable/videos/p46d_bless/; manifest →
   benchmarks/audits/p46d_bless/ (committed). Note in the manifest:
   pair shows P37+P46A jointly, neutral instrument lighting, one
   variable only.
E. Update deferred.md (bless entry + retire the P46c stop item);
   annotate P46/P46b manifests' RGBA-leg numbers as
   lighting-delta-only (per P46c's finding) if P46c's annotations
   didn't already.

## Rules

- Full bar: GLES2 ctest baseline (~1614/1615 class; known ALSA
  exception, RDP race), visual gate PASS goldens untouched, ARM
  cross-build clean, file sizes (pre-existing zone_bench_loader FAIL
  — flag, don't absorb). Unmasked textures and zone rendering must
  stay byte-identical (md5 evidence).
- Separate commits: cache fix / audit memo / evidence / annotations.
- STOP if the keying fix cascades beyond ~6 files or destabilizes
  eviction machinery (those guards have deadlock history — P10b).

## Review

(executor fills in)
