# T265 — audit every race_models.json row the way T262 audited three (+ race 330)

Slot: slot1. x86. NO eqemu (data-level; the pixel checks are owed to a
later eqemu unit). Class A. Territory: data/race_models.json source,
tools/willeq_db_builder race_models path, data/willeq.db (rebuild with
T246/T262-style proof), tests (`WilleqDatabaseShippedContentTest`). Do NOT
touch the EQE1 resolver code, inventory, or anything T264 (slot3) may
delete — T264 is a reachability sweep across src/; keep to data + builder.

## Method (T262's comparison, applied to ALL rows)
For every race id in race_models.json (and every id eqemu's `Race::` enum
can send for Titanium — the board's T213 note lists the 209 client model
codes): (1) does the row exist; (2) is `s3d_file` non-empty and does that
archive exist in BOTH the S3D tree (/mnt/projects/EverQuestP1999) and the
EQE1 tree; (3) does the archive contain the model code's mesh (T262's
`s3d_dump` scan); (4) do all referenced textures resolve through T207's
index against the T199 candidate set for at least one zone where the race
spawns (spawn tables in the eqemu DB dump if available offline; else the
zone list in `also_in`). Produce a table: race | code | s3d_file | mesh
found | textures resolved | verdict.

## Fixes
- Empty/wrong `s3d_file` → fix at the source like T262 (the archive that
  holds mesh + textures).
- Race 330 (the old "placeholder cube in gfaydark" item): decide by the
  table — T215 found it has had a row since 35c51be2; verify the row
  resolves; if it does, close the item with the evidence.
- Races with NO possible model (post-Titanium, like T213's 415/457) →
  alias to the nearest model as T213 did, listed explicitly.
Rebuild willeq.db with the diff proof; extend `WilleqDatabaseShippedContentTest`
to pin "every race row's s3d_file exists and contains its code" so this
cannot regress silently.

## Verification
Full ctest; Review with the table; evidence review/T265/. Commit:
`T265: every race model row verified against the archives -- <n> fixed`.
