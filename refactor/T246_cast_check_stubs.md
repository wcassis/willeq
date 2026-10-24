# T246 — SpellManager's cast pre-checks are `return true` stubs (T244 finding)

> **SUPERSEDED in part by B1140 (#460, refactor/B1140/README.md):** the local reagent refusal (`checkReagents`/`countCarriedItem`, and T251's named 433 for it) was never measured on the oracle and is REMOVED. The official client sends OP_CastSpell with the component carried nowhere (review/B1140/oracle/O1_wiz); the server says 272 + 433 when the cast finishes.

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for a live check of the
reagent refusal. Class A (player state, reagents) / B (line of sight).
Territory: src/client/spell/spell_manager.cpp `checkPlayerState`,
`checkReagents`, `checkLineOfSight` (+ header), the state they need
(read-only: PlayerState flags, InventoryManager item lookup, buff list),
tests. Do NOT touch combat.cpp / eq.cpp update thread (T245 slot1),
eq_chat / cmdEmote (T243 slot2).

## Defect
`checkPlayerState()`, `checkReagents()`, `checkLineOfSight()` all
`return true` (pre-existing TODOs, `spell_manager.cpp:612-625` per the
board), so T244's Stunned / Silenced / Feared / ComponentMissing /
NoLineOfSight refusals are unreachable and the server rejects instead
(with a round trip and, for reagents, no local message).

## Scope
1. `checkPlayerState`: stunned / silenced / feared / mezzed from whatever
   the client already tracks (grep for `m_is_stunned`, AT_STUN /
   SpawnAppearance handlers, buff effect ids for silence/fear/mez in
   `SpellData::effects`). Implement what exists; for anything the client
   does NOT track, leave that sub-check permissive and list it — do not
   invent state.
2. `checkReagents`: spell components from `SpellData` (component ids +
   counts — verify the fields are populated by willeq.db), look them up via
   InventoryManager; refuse with ComponentMissing when absent. Note the
   official client's "You are missing <item>" string if an id exists.
3. `checkLineOfSight`: ATTRIBUTE ONLY unless there is an existing LoS query
   (grep renderer/collision for a ray test the game thread can call
   without touching the renderer — `hasLineOfSight`, `raycast`). If none is
   callable from the game thread, keep permissive and document.
4. Tests for 1 and 2 (real EverQuest harness with willeq.db, T240's
   test_eq_action_handler_cast.cpp pattern): stunned → refusal string 175;
   missing reagent → 272 and NO OP_CastSpell sent; reagent present → cast
   proceeds. Red/green.
5. Live (optional if the lock is free): a spell with a reagent the
   character lacks → local refusal, nothing sent.

## Verification
Full ctest; Review; evidence review/T246/. Commit:
`T246: cast pre-checks are real for player state and reagents; line of sight <attributed|implemented>`.

## Review (executor, 2026-08-26, slot3, off fable e72a761f)

Evidence: /mnt/projects/fable/review/T246/ (README.md indexes every file).

### Which sub-checks are real, which stay permissive

| Check | Real | Permissive (and why) |
|---|---|---|
| `checkPlayerState` | An active PLAYER BUFF (BuffManager, fed by PlayerProfile + OP_Buff via `setPlayerBuff(s)`) whose spell carries SE_Stun (21) or SE_Mez (31) -> `Stunned` (175); SE_Silence (96) -> `Silenced` (207); SE_Fear (23) -> `Feared` (no official string; the handler prints its "Cannot cast spell"). Returns a `CastResult` now, not a bool, so the three reasons are distinct. | The instantaneous **OP_Stun** packet: `eq.cpp:1450` logs it and stores nothing, and a duration-less stun never enters the buff list. eq.cpp is T245's territory, so no stun flag was added -- the server still refuses those. Nothing else (mez/fear/silence) has a non-buff signal the client receives. |
| `checkReagents` | Every consumed component (`SpellData::reagent_id/reagent_count`, now populated -- see below) must be carried in >= count across worn 0-21 + general 22-29 + bag contents 251-330 (eqemu's invWhereWorn/invWherePersonal set; stacks sum `quantity`, non-stackables count 1). Refuses with `ComponentMissing` (272) and sends nothing. | `noexpend_reagent_id` (fields 66-69, the held-not-consumed focus: 98 spells) is carried in SpellData/DB as plumbing but NOT enforced; a component listed with count 0 (e.g. 78 Immolate: 10307 x0) is not enforced. Both left to the server rather than risk a local refusal of a cast it accepts. Bank/shared-bank/trade/cursor do not count (matches eqemu). |
| `checkLineOfSight` | IMPLEMENTED, not just attributed: there IS a game-thread LoS query -- `HCMap::CheckLOS()` on `EverQuest::m_zone_map` (the same eqemu `.map` the server runs `CheckLosFN` on; eq_zone.cpp reads it on the game thread already). New `EverQuest::GetZoneMap()` (shared_ptr copy, P35-safe) + `GetSize()`. Ray from the caster's eye (ground `m_z` + size/2 to model centre, + size/2 * 0.9, size 6 default -- eqemu's HEAD_POSITION) to the target's (`Entity::z` is already model-centre). Refuses with `NoLineOfSight` (108). | No map loaded (`m_zone_map` null / not `IsLoaded()`) or target not in `m_entities` -> permissive. |

### Reagent data: the plan's "verify the fields are populated" -- they were NOT
`SpellData::reagent_id/reagent_count` existed but nothing ever wrote them:
`willeq.db` had no reagent columns (`spells` was 86 columns after T68), so
`requiresReagents()` was false for all 8066 spells and `checkReagents` could
never have refused anything. Fixed the T68 way (DEVIATION from the listed
territory, flagged: `tools/willeq_db_builder.cpp`, `include/client/
willeq_database.h`, `src/client/willeq_database.cpp`, `data/willeq.db`):
- Builder writes 12 new columns from spells_us.txt fields 58-61 (`reagent_id_1..4`),
  62-65 (`reagent_count_1..4`), 66-69 (`noexpend_reagent_1..4`), verbatim
  (`-1` = none; the file says count 1 even beside a -1 id).
- `kWilleqDbExpectedVersion` 3 -> 4 (T56 mechanism; `test_willeq_database`
  re-verified both fatal directions at the new number).
- `SpellInfo` carries the raw values; `SpellDatabase::queryAndCache()`
  normalises at the consumer boundary: id -1 -> 0, count -> 0 unless a real
  id, noexpend -1 -> 0. New `SpellData::noexpend_reagent_id`.
- **Regeneration proof** (`db_regeneration_proof.txt`): rows 8066 -> 8066;
  all 86 pre-existing `spells` columns byte-identical across ALL rows (dump +
  diff, 0 lines); every other table dumped and diffed, 0 differing lines;
  383 rows with a reagent in both file and DB; spot rows 3/5/78/351/390/439
  match the raw file field-for-field. File 3,883,008 -> 4,050,944 bytes.
  Built with `willeq_db_builder --eq-path /mnt/projects/EverQuestP1999
  --data-path data --config-path config` (no s3d index, as before -- the
  db has no such table).
- Official string note (plan item 2): eqstr 272 is "You are missing some
  required components." and **433 "You are missing %1."** is the per-item
  form (eqemu MISSING_SPELL_COMP_ITEM). The refusal table still emits 272;
  wiring 433 with the item name needs an item-name source (the client has
  no items table) and the T244 message sites -- not this unit. Also noted:
  `eq_strings.cpp`'s fallback for 272 says "...required spell components."
  (one word more than eqstr); the DB string wins when loaded. Untouched.

### Red / green (measured)
RED = the new tests built and run against the pre-T246 sources AND the
pre-T246 willeq.db (`git stash` of src/include/tools/data; two red-only
shims because two symbols do not exist there: `GetZoneMap()` ->
`m_zone_map`, and the `noexpend_reagent_id` assertions dropped).
- `red_cast.txt`: FAILED `CastSpell_StunBuffOnPlayer_SaysStunned`,
  `CastSpell_SilenceBuffOnPlayer_SaysSilenced`,
  `CastSpell_WallBetween_SaysCannotSeeTarget` (each cast proceeded and put
  an OP_CastSpell on the wire). The four reagent tests SKIPPED on red --
  the pre-T246 DB has no spell that `requiresReagents()`, which is itself
  the defect ("no self-target reagent spell in scanned range").
- `red_spell_database.txt`: FAILED `ReagentsRoundTripSingleComponentWithCount`,
  `ReagentsRoundTripTwoComponents`, `ZeroCountComponentAndNoexpendCarriedVerbatim`
  (all reagent fields 0). `NoReagentMarkerLandsAsZero` passes on both, by
  construction.
- GREEN: `green_cast.txt` 21/21 (12 prior + 9 new), `green_spell_database.txt`
  25/25, `green_spell_manager.txt`, `green_willeq_database.txt` 5/5.
- New cast tests pin: stunned -> 175 text + nothing sent; silenced -> 207;
  an ordinary buff does NOT block (control); missing reagent -> 272 text
  + no OP_CastSpell; reagent in a BAG slot -> cast proceeds; Bone Walk
  (2 x 13073): one chip refused, a second chip in another slot adds up and
  casts; reagent only in the bank -> refused; a V1 wall map between caster
  and target -> 108 + nothing sent, same map with a target on the caster's
  side -> cast (and the map's own CheckLOS is asserted first, so a cast-path
  bug cannot hide behind a map bug); no map -> permissive.
  Spells the scans pick on this DB: 22 Force Shock (stun), 840 Mesmerizing
  Breath (silence), 58 Elementalkin: Earth (10015 x1), 351 Bone Walk.

### Verification
- Build: `cmake --build slot3/build -j24` clean (`build_full.txt`).
- ctest (`ctest_full.txt`): **2902/2903** -- the one failure is
  `AudioManagerTest.PreloadCommonSounds` (documented ALSA quirk).
- check_file_sizes (`check_file_sizes.txt`): 0 FAIL. The three checks went
  into a NEW TU `src/client/spell/spell_cast_checks.cpp` (146 lines, added
  to CMakeLists) so spell_manager.cpp stays at 1388 (it had crossed 1500
  with them inline). WARNs: eq.h (pre-existing, +5 lines), and
  willeq_db_builder.cpp 1482 -> 1502 (crosses the 1500 WARN line by the
  12 reagent binds; not a FAIL, and splitting the builder is not this unit).
- Live leg (plan item 5, optional): NOT RUN -- `/mnt/projects/fable/.eqemu-lock`
  is held by T245 (`T245 2026-08-26`); never overwrote it.
- Not run (plan's bar is "Full ctest"): build-gles2-x86.sh, visual gate,
  build-arm-noble.sh.

### Deviations
- Territory: builder / willeq_database.{h,cpp} / data/willeq.db (reasoned
  above -- without them item 2 is unimplementable); `include/client/eq.h`
  gained two one-line const accessors (`GetZoneMap`, `GetSize`); eq.cpp,
  combat.cpp, eq_chat untouched. `CMakeLists.txt` +1 line for the new TU.
- `checkPlayerState()` signature: `bool` -> `CastResult` (private; only
  caller is `validateCast`).
