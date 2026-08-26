# T244 — cast refusals name the reason; /target substring picks the nearest match

Slot: slot3. x86. OWNS eqemu briefly (noclobber) if a live check is cheap;
unit tests are the acceptance. Class A (T240 findings). Territory:
src/client/spell/spell_manager.cpp `beginCastFromGem` result switch +
eq_strings lookups, src/client/state/entity_manager.cpp
`findEntityByName` (or wherever the /target ranking T173 built lives —
grep command_processor cmdTarget), tests. Do NOT touch eq_chat / cmdEmote
(T243 slot2), graphics/benchmark (T242 slot1).

## 1. Cast refusal messages
`beginCastFromGem()`'s result switch collapses OutOfRange / NoLineOfSight /
InvalidTarget (and any other distinct enumerators) into "Cannot cast
spell". The official client prints distinct strings — find them in
eqstr_us.txt (e.g. "Your target is out of range, get closer!", "You cannot
see your target.", "You must first select a target for this spell!") and
map each result to the right one. Test each branch. Red/green.

## 2. /target substring ranking
`/target <substring>` returns the LOWEST spawn id among matches, not the
nearest. Rank matches by distance from the player (keep T173's
living-over-corpse ordering as the primary key, distance secondary). Test:
two matches, farther one has the lower id → nearest wins; corpse vs living
still prefers living. Red/green.

## Verification
Full ctest; Review; evidence review/T244/. Commit:
`T244: cast refusals say why; /target prefers the nearest match`.

## Review (executor, 2026-08-26, slot3, off fable 6d784175)

Evidence: /mnt/projects/fable/review/T244/ (README.md indexes every file).

### 1. Cast refusal messages
- The "result switch" the plan names is NOT in `spell_manager.cpp` -- it is
  `EqActionHandler::beginCastFromGem()` (src/client/eq_action_handler.cpp,
  T240 finding 1) plus a second, already-drifted copy in the
  `CastSpellIntent` arm of `EverQuest::ProcessBridgeIntents()`
  (src/client/eq_bridge.cpp). Both now read ONE table, new header
  `include/client/spell/cast_refusal.h` (`EQ::castRefusalStringId()`), and
  render it through `EverQuest::GetStringMessage()` (string DB first, its
  hardcoded fallback table second). Deviation flagged: eq_action_handler.cpp
  and eq_bridge.cpp were edited; neither is T242/T243 territory.
- eqstr_us.txt ids used (each verified against
  /mnt/projects/EverQuestP1999/eqstr_us.txt): OutOfRange 100, NoLineOfSight
  108, Fizzle 173, Stunned 175, NotEnoughMana 199, Silenced 207, GemCooldown
  236, InvalidTarget 251 ("That spell can not affect this target NPC."),
  NoPet 255, ComponentMissing 272, AlreadyCasting 12442. Success /
  Interrupted / Feared / NotMemorized / SpellNotReady / InvalidSpell have no
  official refusal string (id 0) and keep local wording. 214 ("You must
  first select a target for this spell!") is NOT emitted: SpellManager never
  refuses a targetless cast locally (validateCast skips range/creature
  checks at target 0 and the server answers) -- adding a client-side
  no-target refusal would change cast semantics, out of scope.
- New enumerator `CastResult::NoPet = 16` (spell_constants.h; appended, wire
  values of the others unchanged): pet-only spell with no pet was folded
  into `InvalidTarget` alongside creature-type mismatch, and the two need
  different strings (255 vs 251). Both `beginCastFromGem`/`beginCastById`
  return it.
- eq_strings.cpp fallback table gained 175 and 12442 (the only two ids the
  path can emit that it lacked); `GetStringMessage()` moved to public in
  eq.h (EqActionHandler is not a friend; T180 precedent for GetClassName).
- Red/green (`red_cast.txt` / `green_cast.txt`, pre-T244 sources stashed,
  the three tests needing the new API removed for the red build): RED
  `CastSpell_TargetOutOfRange_SaysOutOfRange` ("Cannot cast spell"),
  `CastSpell_NoMana_SaysInsufficientMana` ("Insufficient mana"),
  `CastSpell_AnimalSpellOnHumanoid_SaysCannotAffectTarget` ("Cannot cast
  spell") -> GREEN 12/12 incl. `CastSpell_PetSpellWithoutPet_SaysNoPet`,
  `CastRefusalStringIds_MatchEqstr`, `CastRefusalStrings_ResolveWithoutStringDb`.
  Stunned / Silenced / Feared / NoLineOfSight / ComponentMissing are NOT
  exercised end-to-end: `checkPlayerState()`, `checkLineOfSight()`,
  `checkReagents()` are `return true` stubs (pre-existing TODOs), so those
  results are unreachable today; their ids are pinned by the table test.

### 2. /target substring ranking
- `EntityManager::findEntityByName(name, px, py, pz)` overload
  (entity_manager.h/.cpp, shared private body `findEntityByNameRanked`).
  Rule: T173 tier is PRIMARY (exact-living < exact-corpse <
  substring-living < substring-corpse); squared distance from (px,py,pz) is
  SECONDARY; equal distance falls to lowest spawn id. The no-position
  overload is unchanged (id order per tier, early-out on exact living);
  its four other callers (action_dispatcher.cpp) are untouched.
  `CommandProcessor::cmdTarget` passes `m_state.player()` x/y/z.
- Red/green: `red_target.txt` -- `Target_TwoLivingMatches_NearestWinsOverLowerId`
  answered spawn 10 (lower id, 500 units away) on pre-T244 code; GREEN 24/24
  Target_* incl. `Target_NearerCorpseStillLosesToFartherLiving`. Four
  EntityManager tests (`green_entity_manager.txt`): nearest wins, flips with
  viewpoint; tier beats distance (corpse at feet loses; exact beats nearer
  substring); equal distance -> lowest id; no-position form still id-ordered.

### Verification
- Build: `cmake --build slot3/build -j24` clean (`build_full.txt`).
- ctest (`ctest_full.txt`): 2888/2890 -- AudioManagerTest.PreloadCommonSounds
  (documented ALSA quirk) and `check_build_tree_orphans`, which flagged a
  STALE `bin/test_eq_action_handler_transforms` left in slot3's build tree
  from before T240 deleted its sources (not this unit's change; cmake never
  prunes bin/). Deleted the binary; re-run passes (`ctest_orphans_rerun.txt`).
  Net: every test passes except the ALSA one.
- check_file_sizes (`check_file_sizes.txt`): 0 FAIL on this unit's 13 files
  (4 pre-existing WARNs). The vs-fable run also lists 2 FAILs in
  tools/hwcaps/* -- fable moved to aae1ae65 (T242 merged) after this branch
  was cut from 6d784175, so those are T242's files, not touched here.
- Not run (plan's bar is "Full ctest"): build-gles2-x86.sh, visual gate,
  build-arm-noble.sh. No live eqemu check -- unit tests are the acceptance.
