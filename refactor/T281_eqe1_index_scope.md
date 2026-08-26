# T281 — data/eqe1_texture_index.json is scope-short: 1008 archives vs the tree's 1506 (T279 hand-back)

Slot: slot2. x86. NO eqemu. Class A. Territory: tools/eqe1_texture_index_builder.cpp,
data/eqe1_texture_index.json, the resolver's use of it
(`EQE1TextureIndex::resolve` — T266 added late-open on top of it), tests.
Do NOT touch inventory (T282 slot3), trainer (T280 slot1).

## Finding (T279 Review)
The committed index covers **1008 archives / 50,447 names**; the live tree
(`/mnt/projects/EverQuest_ETC1`, now complete after T279) has **1506 /
50,972**. The index predates the ~498 equipment-sheet packs P16/P48 added.
T266's late-open resolves a name through this index, so a name that lives
ONLY in an unindexed pack cannot be late-opened — it takes the
all-or-nothing FATAL instead (decision #2). That is the same crash class
T266 just fixed, hiding behind a stale data file.

## Scope
1. Rebuild the index over the whole tree; commit it with the T262/T274-style
   diff proof (archives/names before vs after, and a spot check that an
   existing name's owner list is unchanged, not just longer).
2. Establish whether any of the 525 newly-indexed names were previously
   FATAL-able: cross-check the new names against what the resolver could
   already reach (candidate sets + the old index). Report the count — if it
   is non-zero, that is a live crash that T266's fix could not reach.
3. Guard it: a test that the shipped index's archive count equals the pack
   count in the configured EQE1 dir (skip cleanly when the dir is absent,
   `test_eqe1_texture_index`'s precedent), so a future pack addition cannot
   silently outrun the index again.
4. Note the index's size/load cost before and after (it is read at startup).

## Verification
Full ctest; visual gate; Review; evidence review/T281/. Commit:
`T281: the EQE1 name index covers every shipped pack`.
