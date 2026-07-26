# P08f — Composite coverage: teach the builder zone archives, generate the missing sheets

Approved by Will 2026-07-26 ("go ahead with the builder-extension unit").
Slot: wt/slot1, branch task/P08f-composite-coverage, cut from fable
(f29ad39). Tool + assets unit — NO production renderer changes (P08d's
runtime already consumes sheets generically, including nonzero uniform
variants). Composites are LIVE on the orangepi preset (P08d, blessed);
this unit raises effective coverage from ~10-21% of real zone rosters
toward all no-override NPCs.

## Motivation (P08d coverage audit)

The 210 existing sheets cover the 42 global player-race model defs × 5
variants. `entity_composite_builder` has a hardcoded global-only archive
list, so zone-native NPC models (the CAZ/INN/BEA-shaped majority of
befallen/qeynos2/gfaydark rosters) are structurally unbuildable →
~10-21% eligibility. P08c's measured entity win (33.1%/56.0% p95) scales
with coverage.

## Design

### A. Builder extension (tools/entity_composite_builder.cpp)

1. Add zone-archive support: `--zone <shortname>` opens the zone's
   character archives ({zone}_chr.s3d and any numbered/sibling variants
   the loader itself consults — mirror RaceModelLoader's archive search
   order rather than inventing one), enumerates the race models inside,
   and builds sheets per (model, gender, variant) exactly like the
   global path. `--all-zones` iterates docs/reference/
   pre_luclin_zones.json. Keep global behavior byte-identical.
2. Sheet naming/keying must match what the RUNTIME looks up
   (race_model_loader.cpp: `{modelname}_0_{textureVariant}` JSON +
   EQE1 entry) — verify against the runtime code, not assumption; if a
   zone model's name collides with a global model name but has different
   content, detect and report (do not silently overwrite global sheets).
3. **Animated-texture exclusion (the Z35-class builder gap, now
   enforced)**: detect materials with multi-frame/animated textures
   during sheet build and SKIP those models entirely (they would bake
   frame 0 and freeze). Emit them in the report as
   ineligible-by-animation; do not bake silently.
4. Variant enumeration for zone models: build the variants that actually
   exist in the archive textures (the {race3}{part2}{variant2}{page2}
   naming), not a fixed 0-4 sweep; report per-model variant sets.

### B. Generation + audit

5. Generate sheets for the three golden zones first (befallen, qeynos2,
   gfaydark), then `--all-zones` if runtime is reasonable (report time;
   it's offline x86 — hours are acceptable, days are not; if --all-zones
   is too slow, golden zones + a documented invocation for the rest).
   Output alongside the existing 210 at /mnt/projects/EverQuest_ETC1
   (RGBA .bmp + .eqe1 + .json per P08d's format expectations — match
   the existing sheets' exact format/layout conventions).
6. Structural audit of every NEW sheet (P08d's sheet_audit method,
   extended to the new set) — broken sheets deleted + reported, never
   left on NFS.
7. **Coverage re-audit**: recompute the eligibility table for the three
   golden-zone rosters (P08d's method) before vs after. Target: every
   no-override, non-animated-texture NPC model in those rosters has a
   sheet. Report the residual ineligible list with reasons (per-slot
   overrides / animated textures / unbuildable-other).

### C. Verification + evidence

8. Standard bar: build (the tool builds in the regular cmake build),
   ctest (known host exceptions), gles2-x86 build, visual gate PASS
   goldens untouched, ARM build, file-size check.
9. **Static render spot-checks** (the P08d bar, valid geometry only —
   NEVER --animate): pick 4-6 REPRESENTATIVE NEW zone models (mix of
   zones/body types incl. at least one non-humanoid if present), render
   composite vs multi-buffer via the benchmark's --composite-scenegraph
   route with close-up framing (P08d's no-overlap lesson: small entity
   counts, verify no co-location before comparing), pixel-diff each.
   Bar: pixel-identical (0 differing) per P08d precedent; any diff =
   investigate or mark that model's sheets ineligible + report. Contact
   sheet → benchmarks/contact_sheets/p08f_composite_coverage/ for
   Will's spot-review (no self-declared acceptability).
10. **Runtime adoption demonstration**: whatever cheap path exercises a
    zone-native model through the production composite lookup (benchmark
    entity placement if it can place zone models; otherwise a zone-load
    log capture showing the adoption INFO line counting the new
    eligibility on a golden-zone roster). Report the achieved adoption
    numbers.

## Out of scope

- Production renderer changes of any kind.
- Per-player/runtime baking; billboards; the --animate root-cause unit;
  fixing gles2_composite_audit's device bug (workaround sweep is fine).
- Baking animated-texture models (excluded by design, reported).
- Pi sessions (composites are read off NFS identically on x86; adoption
  proof on x86 suffices — the Pi check rides along with Will's next
  play session).

## Review

(executor fills in)
