# P13b — Fix the confirmed double-load: per-part textures become lazy when a composite sheet hits

Authorized in advance by Will 2026-07-27 ("go ahead with the top fix
from the ranked list"). Slot: wt/slot1, branch task/P13b-perpart-lazy,
cut from fable (post-P13 tip). P13's top-ranked fix (surgical,
confirmed waste); includes a BOUNDED side-quest on the unreproduced
71-eviction scenario.

## Motivation (P13 findings)

CONFIRMED double-load: RaceModelLoader::preloadModelData's per-part
decode/upload loop is unconditional — entity_perpart uploads were
identical (268) with composites on or off, so every composite sheet is
pure net addition (+3.1-3.7MB in qeynos2) with zero savings realized.
The whole point of sheets is to REPLACE per-part textures for eligible
entities.

**Correctness constraint (the reason this is lazy, not a blanket
skip)**: eligibility is per-ENTITY (P08d gate: players and
per-slot-override NPCs use the multi-buffer path and NEED per-part
textures) — a race can have both eligible and ineligible entities in
one zone. Per-part textures for a race must therefore load ON DEMAND
(first time an ineligible entity of that race actually builds a
multi-buffer mesh / needs the texture-name swap), not never.

## Design

1. In preloadModelData (and any other unconditional per-part
   decode/upload site P13's ledger callsites identify —
   loadZoneStep8c_VariantTextures / 8d_EquipmentPrep included): when a
   composite sheet HIT occurs for a (race, gender, variant), DEFER the
   per-part decode/upload — record enough metadata to do it later
   (archive/name list), do not touch the GPU.
2. On-demand path: the multi-buffer mesh build (and the equipment
   texture-name swap machinery) triggers the deferred per-part
   load for that race when an ineligible entity needs it. Thread-
   correctness per the existing loading/background-pool rules (uploads
   via the same paths they use today — no new GL-on-render-thread).
3. Composite MISS (no sheet): behavior byte-identical to today (eager
   per-part), so races without sheets are unaffected.
4. Ledger-verified outcome (the P13 instrumentation is the acceptance
   harness): re-run the P13 qeynos2 composites-ON scenario —
   entity_perpart uploads should DROP for fully-eligible races (report
   before/after counts and bytes; expected: several MB saved and
   per-part uploads only for races with ineligible entities present);
   composites-OFF run byte-identical to P13's (271-line diff of
   ledgers acceptable only in ordering).
5. Visual safety: an eligible-entity render and an ineligible-entity
   render (equipment-wearing NPC or player) both correct post-change —
   spot captures; the P08d contact-sheet method at 2 poses.

## Side-quest (bounded): reproduce the 71-eviction scenario

6. P13's controlled single-zone-in runs (15.68MB, 0 evictions) did NOT
   reproduce P10b's 71 evictions. Two experiments MAX, with the ledger
   on:
   a. Multi-zone session in ONE process (character in befallen → zone
      to qeynos2, or login-flow default): does cross-zone texture
      accumulation (no full teardown between zones) drive eviction?
   b. Replicate P10b's exact session shape (its Review/README documents
      the repro configs — same preset copies, same entry path).
   Whatever reproduces (or doesn't): record the ledger + eviction
   trace, update the deferred item; do NOT chase further fixes here.

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Design-4 ledger before/after tables; design-5 visual spot checks.
- Sanctioned server flow; clean shutdown; configs/character restored.

## Out of scope

- Ranked fixes #2 (builder unroll discipline) and #3 (lazy sheet
  loading) — separate units if the data keeps pointing at them.
- Eviction fixes beyond the bounded repro attempt.
- ETC1-sheets-everywhere; budget changes.

## Review

(executor fills in)
