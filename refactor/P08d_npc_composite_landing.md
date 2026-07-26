# P08d — Land eligibility-gated NPC composite entities in production

Approved by Will 2026-07-26 (picked from the deck; strategy set in the
2026-07-25 discussion: composites for NPCs without per-slot overrides,
players/override-NPCs keep multi-buffer; per-player baking deferred).
Slot: wt/slot1, branch task/P08d-npc-composites, cut from fable (9589d37).
PRODUCTION landing unit — merges only after Will blesses the quality
evidence. P08c measured the win: 33.1% p95 (befallen) / 56.0% (qeynos2)
at gameplay load; production pipeline exists dormant (210 pre-baked
sheets at /mnt/projects/EverQuest_ETC1/).

## Design

### A. Ground truth first (read before coding)

1. **How per-slot texture overrides actually flow today**: WearChange /
   equipment appearance from server → game state → bridge events →
   EntityRenderer. Determine what production CURRENTLY renders for
   per-slot armor textures (there is a months-old
   fix-npc-and-equipment-textures branch — read its diff for context but
   do NOT merge it). If per-slot overrides are not visually applied
   today, say so — the eligibility gate may reduce to "NPCs eligible,
   players not" with a documented TODO for when overrides land.
2. **Composite selection plumbing**: RaceModelLoader::preloadModelData /
   getAnimatedMeshForRace key composites per (race, gender,
   textureVariant) and CACHE PER MODEL. The gate must be per-ENTITY.
   Map where the per-entity mesh choice happens and design the cleanest
   split: eligible entities get the composite mesh, ineligible entities
   get the existing multi-buffer mesh (both cacheable side by side; no
   regression for ineligible entities — their path must be BYTE-UNTOUCHED).
3. **Variant coverage audit**: enumerate the texture variants actually
   present in the model data for the golden zones' spawn rosters (and
   the tour-character zones) vs the 210 sheets on disk. Missing sheets:
   generate with the existing entity_composite_builder (offline, x86) —
   report what was generated; if a variant can't be built, that
   race/variant is simply ineligible (report the list).

### B. Implementation (production, flag-gated)

4. Eligibility gate per design A: composite only for entities with a
   uniform texture variant and no per-slot overrides (as defined by
   ground truth A1). Players ALWAYS ineligible in this unit.
5. Config: `enableCompositeEntities` stays the master switch;
   `compositeEntityPath` comes from config (verify how the Pi deployment
   should point at the sheets — NFS path vs local copy; document the
   recommended willeq.json/preset arrangement). The orangepi preset flip
   to enabled happens in a SEPARATE final commit so the merge can carry
   or drop it per Will's bless.
6. Logging: one INFO line per zone load summarizing composite adoption
   (N eligible / M total entities, sheets hit/missed) — module-filtered.

### C. Verification + quality evidence (the bless package)

7. Standard bar: build, ctest (known host exceptions), gles2-x86 build,
   visual gate PASS goldens untouched (gate renders no entities), ARM
   build, file-size check.
8. **gles2_composite_audit full sweep** (all race/gender/variant combos
   available) with the production sheets — catalogue renders, flag any
   visually broken sheet (those become ineligible + reported, not
   shipped broken).
9. **Animation verification on the PRODUCTION path** (the Z35-era risk):
   composite-meshed NPCs playing walk/attack/idle animations — model
   viewer or the audit tool's animated mode or an x86 app session,
   whichever exercises EQAnimatedMesh + composite together; capture
   frames at several animation phases for 3-4 representative races.
   Explicitly include at least one model with ANIMATED TEXTURES if any
   exists in the eligible set (the Z35 "animated texture fix" class) —
   document behavior.
10. **Side-by-side quality evidence**: composite vs multi-buffer renders
    of the same NPCs (near + mid distance), assembled as a contact
    sheet → benchmarks/contact_sheets/p08d_composite_landing/. Expect
    near-identical (same texels, repacked); quantify diffs. NO
    self-declared acceptability — Will blesses.
11. **Perf spot-check** (cheap, x86 or Pi if budget allows): the P08c
    benchmark numbers stand as the perf case; optionally confirm the
    production path's draw-count reduction via the zone-load INFO line +
    a short instrumented run. Do not re-run the full P08c matrix.

## Out of scope

- Per-player runtime composite baking (deferred design unit).
- Billboards/impostors (P08e). Zone merge landing (separate unit).
- Merging or fixing the fix-npc-and-equipment-textures branch.
- Debugging broken sheets beyond marking them ineligible.
- Flipping the preset before Will's bless (separate commit, see B5).

## Review

(executor fills in)
