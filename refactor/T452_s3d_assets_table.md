# T452 — the armor-texture lookup reads a table the shipped db never had (B468)

Class A (unconfirmed live), slot2, no server. Read B468 (T450's find:
`getAssetLocation()` / `getAssetsInArchive()` select from `s3d_assets`,
created only under `--s3d-index`, which nothing passes; reached from
`RaceModelLoader::getArmorTexture()` at `mesh_building.cpp:210` and
`:273`), T450's §Review (the db pin recipe `scripts/build_willeq_db.sh`,
`DbReproducible`, the input table in `main()`), the EQE1 asset rule
(all-or-nothing; where armor textures actually live in the pack — read
the pack builder and `equipment_textures` table), and the armor-texture
callers.

1. Measure offline: with the shipped db, what does `getArmorTexture()`
   return for a real armor material on a real race (a headless test on
   the pack + db)? If the EQE1 pack already resolves armor textures by
   another route and the `s3d_assets` query is a dead fallback, delete
   the query, the `--s3d-index` mode and the table code (stale doc =
   defect), and pin. If the query is the live route and it fails, the
   builder generates the index from the inputs it already has (no new
   external file), the db is rebuilt and re-pinned under T450's recipe
   (one commit: db + sha256), and a test proves the lookup succeeds.
2. Either way: a pre-fix failing test on the exact call
   `getArmorTexture()` makes, against the shipped db.
3. Docs: the builder header, CLAUDE.md's db table list, module_map.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
`DbReproducible` green with the new pin if rebuilt, visual gate
standalone + ARM (if armor textures start resolving, an image golden
MAY move — that is a STOP with the contact sheet, not a bless), file
sizes, doc nets. Strike B468; new items B469+ (T451/T431 allocate from
B468+ — say which you used).

STOP: an image golden moves.
