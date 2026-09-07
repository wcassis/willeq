# T456 — one corpse predicate (B478), the async build copy pinned (B477), the equipment_models table (B476)

Class A/B, slot2, no server. Read B476–B478 (T454's finds: corpse
recognition is derived FIVE times and the copies disagree — three use a
loose `name.find("corpse")` with no id/npc_type check, which would
freeze a live NPC named "a corpse beetle" in a death pose; the async
entity-build path duplicates the pose ladder and nothing asserts it;
`equipment_models`, 1,000 rows, has no reader), T454's §Review (the
wire truth: `Spawn_Struct.NPC == 2` for every corpse, cited at
`/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`; the death pose
`d05` last frame held; `EquipmentModelLoader::buildEquipmentIndex()`
indexes the archives live), T450/T454's db recipe.

1. B478: one predicate (`eqt::entities::isCorpse(const Entity&)` or the
   existing best copy) keyed on `npc_type == 2`, used by all five sites;
   the name heuristic is deleted. Pre-fix failing test: a live NPC named
   "a corpse beetle" (npc_type 1) is NOT a corpse at every site; a
   player corpse (npc_type 2, name "…'s corpse") IS.
2. B477: the async build path's pose ladder shares the sync path's code
   (one function) and is pinned by the same test through both paths.
3. B476: delete the `equipment_models` writer and table, rebuild and
   re-pin the db (one commit: db + sha256; row diff = the table), the
   converse net's exemption removed so it pins zero exemptions.
4. Docs/nets green; CLAUDE.md table list re-measured.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
`DbReproducible` green, visual gate standalone + ARM (no golden should
move — STOP with the sheet if one does), file sizes. Strike B476–B478;
new items B479+ (T449/T455 allocate from B474+/B476+ — collisions are
renumbered at merge; say which you used).

STOP: an image golden moves.
