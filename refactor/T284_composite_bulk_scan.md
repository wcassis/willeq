# T284 — the composite scan bulk-opens every pack in the tree (T281 finding; bears on decision #3)

Slot: slot2. x86 for the fix and proof; **Pi measurement is NOT in this
unit** (see below). NO eqemu. Class A. Territory: `loadZoneStep0_EQE1`'s
composite bulk-open and `RaceModelLoader::preloadModelData`'s composite
path (renderer_zone_load* / race_model_loader.cpp), the index-driven open
(`EQE1TextureIndex::resolve` + `openArchiveContaining` — T207/T266), tests.
Do NOT touch inventory/world containers (T282 slot3), practice points /
loot UI (T283 slot1).

## Finding (T281 Review)
`loadZoneStep0_EQE1` opens EVERY `.eqe1` in the composite directory. With
the composite dir pointed at the shipped ETC1 tree that is **1506 packs /
4.8 GB of mmap** — precisely T195/T197's address-space exhaustion, and the
stated reason Will turned composite entities OFF on the orangepi (T198,
decision #3). T281 has now made the index know the 498 sheet packs, so a
name → one pack open is finally possible.

## Scope
1. Replace the bulk scan with the index-driven open: a composite sheet
   name resolves through `EQE1TextureIndex` to its owning pack, which is
   opened on demand (T266's late-open path already does exactly this for
   the entity case — reuse it, do not fork a second implementation).
   Rule R1/R2 still governs: never open a pack nothing asked for.
2. Measure on x86 with `EQT_EQE1_REQUEST_LOG_DIR`: packs opened and bytes
   mapped, composites ON, before vs after. That number is the deliverable.
3. Tests: a composite name resolves and opens exactly one pack; a name in
   no pack still takes the documented FATAL (decision #2). Red/green.
4. Visual: with composites ON on x86, entities still render correctly
   (before/after crops; the composite atlas is what T191/T192 chased).

## STOP / hand-back
Do **not** change the orangepi preset or re-enable composites on the Pi —
that is Will's decision #3. Deliver the x86 numbers and say plainly whether
the address-space argument still holds, then hand it back.

## Verification
Full ctest; visual gate; Review with the before/after mapped-bytes table;
evidence review/T284/. Commit:
`T284: composite sheets open by index, not by scanning the tree`.
