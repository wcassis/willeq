# T262 — zone_characters table has no reader (reachability #14, T261)

Slot: slot1. x86. NO eqemu (a benchmark/zone load with the EQE1 request
log is the evidence). Class A. Territory: src/client/willeq_database.*
`getZoneCharacters`, tools/willeq_db_builder.cpp zone_chars path, the
EQE1 archive resolver (`openArchiveContaining` / `eqe1_texture_index` /
renderer_effects archive selection — T207/T199/T214 territory), tests.
Do NOT touch the zone-load snapshot / region lookup (T260 slot2),
inventory (T259 slot3).

## Finding
`WilleqDatabase::getZoneCharacters()` is the only reader of
`zone_characters` (2188 rows: zone_name, model_code, source_file) and has
ZERO callers. The table was meant to tell the loader which `*_chr` archive
holds a zone's character models (the `source_file` hint). Today T199's
spawn-driven archive selection + T207's name→archive index decide this at
runtime, and T214's audit found ZERO NOT-FOUND — so the table may be
redundant.

## Scope — decide by evidence
1. Compare: for each zone, the set of archives T207's index would open for
   that zone's `zone_characters` model codes vs. the `source_file` hints.
   If the index already resolves every code (expected after T214's
   NOT-FOUND=0), the table is redundant → DELETE table, builder path,
   reader, tests, and the `*_chr.txt` source parsing if nothing else uses
   it. Record the comparison.
2. If any code resolves ONLY via the hint, wire `getZoneCharacters()` into
   the resolver as a pre-open hint for the current zone (rule R1/R2 in
   deferred.md STATUS — it must not open archives nothing requests), test
   it, and show the EQE1 request log before/after for that zone.

## Verification
Full ctest; visual gate (loader touched); Review; evidence review/T262/.
Commit: `T262: zone_characters table deleted (index resolves every code)`
or `T262: zone_characters hints wired into the EQE1 resolver`.
