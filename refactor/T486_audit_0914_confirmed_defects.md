# T486 — the 2026-09-14 audit's confirmed latent defects and stale docs

Class A/B, **offline (no lock)**, slot3. Source: `review/AUDIT-0914/README.md`
(the audit of T449–T484). Each item below was confirmed against the tree at
0daacef4 by reading; confirm again with a red test before fixing. Fix all;
do not widen.

## Items

1. **UB in `ConstrainedTextureCache::clear()`** (`src/client/graphics/constrained_texture_cache.cpp:661-676`):
   protected entries are preserved across `lruOrder_.clear()` but their
   `CachedTexture::lruIterator` is never refreshed, so any later touch of a
   protected name (`getOrLoad` hit, `uploadTexture` hit, `registerTexture`,
   `getTexture`, `touch()`) erases a dangling list iterator. Fix in the
   preserve loop (`entry.lruIterator = std::prev(lruOrder_.end())` after the
   push_back). Red test: clear() then touch a protected name under ASan or
   with a list-iterator validity check.
2. **Pre-existing corpses are not corpses to three consumers** (class A,
   user-reachable: a cleric zoning in to rez a corpse it did not see die is
   refused client-side). `Entity::is_corpse` is only set by the witnessed
   death path and OP_MobRename; `ZoneProcessNewSpawn`
   (`src/client/entities/entities_component_updates.cpp:304`) stores
   `npc_type` but never sets `is_corpse` for `npc_type == 2`. Bare-flag
   readers: `src/client/spell/spell_cast_validation.cpp:86`,
   `src/client/bridge/bridge_component.cpp:634` (CorpseDecayStarted never
   published), `src/client/entities/entities_component_appearance.cpp:84`.
   Fix by ONE owner: set `is_corpse` from the NPC byte at spawn (and keep the
   predicate `IsCorpseByFields` consistent), so the three sites are right
   without touching them; extend the T456 ratchet so a bare `is_corpse` read
   that bypasses the owner is caught. Red test: spawn packet with NPC=2 →
   `/cast` a corpse-target spell → not InvalidTarget.
3. **Third copy of the death-animation ladder**: `EntityRenderer::markEntityAsCorpse()`
   (`src/client/graphics/entity_renderer.cpp:381-395`) — route through the
   shared ladder (keep `playThrough=true` for a witnessed death).
4. **Census misclassifies a continuation-line call as a definition**
   (`scripts/refactor/check_zero_callers.py` `classify_occurrence`): a lead of
   `identifier,` / `identifier &` / `identifier *` / `identifier <` before
   `foo(` is a USE. Today 0 rows are affected; the failure mode is a live
   caller reported dead and deleted by the next hygiene unit. Fix the rule,
   add the four probe shapes to the script's self-test, re-run the census and
   report any row that changes class.
5. **Two owners of the camera zoom notch**: `renderer_event_receiver.cpp:279-280`
   hard-codes ±2.0 beside `kCameraZoomStepUnits` — use the constant.
6. **`updateRigCamera()` runs twice per frame** (`renderer_frame_input.cpp:114`
   and `:931`): two BSP lookups + a PVS ray per frame on the Pi, and
   `ceilingLogCount` double-counts. Keep one call; pin with the frame-order
   golden if it exists for that TU.
7. **`chaseEyeAboveFeet_` never reset on identity change**
   (`renderer_camera_modes.cpp:57-68`): add it to the T467 identity reset.
8. **Stale docs (first-class defects):** `docs/module_map.md` has no rows for
   `corpse_death_pose.cpp`, `equipment_actor_geometry.cpp` is T485's,
   `gles2_vertex_color.h`, `text_batch`, `ui_quad_batch`, `chat_wrap_cache`,
   `spell_wire_payloads.h`, the six T451 event headers; row 44 says 3 ignored
   opcodes / 38 handlers (tree: 2 / 39); `renderer_frame_render.cpp` row says
   667 lines (752); `renderer_zone_stream.cpp` 545 (546). `CLAUDE.md:669`
   points at `event_bus.h` for the enum (it is `event_types.h`).
   `tools/equipment_index_builder.cpp:3-5` and
   `tools/hwcaps/ui_inventory_capture.cpp:278` still name the dropped
   `equipment_models` table. `renderer_camera.cpp:547` says the default camera
   is Chase (First Person since T471). `camera_rig.h:107-125` presents the
   Chase fit as settled — add the B721/B680 caveat. `docs/decisions.md` has no
   2026-09-13/14 entry for Will's camera ruling (F9 replaces Ctrl+F5, no Free
   mode, official set, /camera) — add it. `include/client/willeq_database.h:17-22`
   says bump the version on every schema change; two DDL changes kept v7 —
   make the text match the practice (digest is the pin).
   Board: strike B762 (implemented by T483); B588 is T485's; the 0daacef4
   board says B730–B732 CLOSED by T479 while T479 is unmerged — mark them
   "fixed on the held branch".
9. **B780 addendum (no code change):** the driver-wide R/B vertex-colour
   swap also affects `ParticleManager::render()` billboards
   (`particle_manager.cpp:718-731`, drawn every frame at
   `renderer_frame_render.cpp:288`), tumbleweeds, placeholder doors — the
   board says "not rendered" for the particles, which is false. Amend the
   entry; the decision stays Will's.

## Proof

Standard verification bar, builds strictly in sequence; goldens for touched
TUs updated in place per G-correction 32 and said so; ARM build; file sizes;
`DocCitations`/`DocCounts` green. No image golden may move (none of this is
visual); STOP if one does.

File territory: the files named above only. **Not** held items (T485,
slot1), not the loaders (T479, slot2), not the spell cast-end state machine
(a later unit).

STOP: item 2's fix changes any looting/targeting behaviour beyond the three
sites; item 4's re-run changes the class of more than five rows.

## Review (T486, slot3, branch `task/T486-audit-0914-defects` from `a5c4f1f5`)

**Verdict: all nine items done, no STOP hit.** Every code item has a test that
was RED on the unfixed tree and GREEN after (table below); the census re-run
changed the class of **0** rows (STOP limit 5); item 2 changed no
looting/targeting behaviour beyond the three named readers (one T160 pin that
asserted the OLD design was reversed in place, see deviations). No image golden
moved. `review/AUDIT-0914/README.md`, which the plan names as the source, does
not exist (`review/AUDIT-0914/` holds `analysis/`, `pi/`, `scripts/`); the plan
file's item text was taken as the contract and each item re-confirmed by reading.

### Per item

| # | Fix | Test (red -> green) |
|---|---|---|
| 1 | `src/client/graphics/constrained_texture_cache.cpp` `clear()`: the preserve loop now re-points `entry.lruIterator = std::prev(lruOrder_.end())` after its `push_back` | `test_constrained_texture_cache.cpp` `ConstrainedTextureCacheTest.ProtectedEntriesSurviveClearWithValidLruIterators` -- RED: `'t486_protected_a' holds an iterator to the node carrying 't486_protected_c'` (and c -> a); GREEN after. No ASan/valgrind on the host, so the red is the invariant (each entry's iterator must point at the live node carrying its name), which the pre-fix cache fails without dereferencing freed memory |
| 2 | ONE owner for `Entity::is_corpse`: `IsCorpseNpcType()` (`corpse_predicate.h/.cpp`, `== 2 \|\| == 3`; `IsCorpseByFields()` now calls it) and both Spawn_Struct parsers set `entity.is_corpse = IsCorpseNpcType(entity.npc_type)` right after reading offset 83 (`entities_component.cpp` ZoneSpawns, `entities_component_updates.cpp` NewSpawn). The three bare readers were not touched | `test_spell_body_type_targeting.cpp` `SpellManagerCorpseTarget.APreExistingCorpseIsACorpseTargetWithoutAWitnessedDeath` -- RED: `is_corpse` false and `validateCast` = `InvalidTarget` (05) for an NPC=2 spawn with no death; GREEN. Ratchet extension in `test_corpse_death_pose.cpp`: `EverySpawnStructParserSetsTheCorpseFlagThroughTheOwner` (RED: both parsers listed), `NoProductionSiteDerivesCorpsenessFromTheNpcByteInline` (guard, green both sides), `TheNpcByteRuleIsOneFunction` |
| 3 | `ApplyCorpseDeathPose(visual, node, witnessed=false)`: `witnessed=true` stops the current animation and plays the ladder's pick once, play-through (the old `markEntityAsCorpse()` semantics); `EntityRenderer::markEntityAsCorpse()` calls it and its own ladder is deleted | `CorpsePoseTest.AWitnessedDeathPlaysTheOneLadderThrough` -- RED: `corpsePositionAdjusted` false (the copy never set it); GREEN (d05, Playing, playThrough, frame 0). `CorpseRecognition.TheDeathPoseLadderHasOneCopy` -- RED: 2 copies (`entity_renderer.cpp`, `corpse_death_pose.cpp`); GREEN: 1 |
| 4 | `check_zero_callers.py` `classify_occurrence()`: a lead ending in `,` or a whitespace-bounded `&` / `*` / `<` (`CONTINUATION_OP`) is a USE. `--self-test` added: 22 probes (`CLASSIFY_PROBES`: the four new shapes, T458's stream/punctuation shapes, the definition shapes a fix must not hide -- `Foo& bar(`, `Foo &bar(`, `Foo* bar(`, `std::vector<std::pair<...>> getBankItems(`). New ctest `ZeroCallersSelfTest` (`tests/cmake/hygiene.cmake`) | Pre-fix classifier (from `fable`) on the same probes: **4 of 22 fail** (the four continuation shapes score DEF); fixed: 0 of 22. First cut (`\s[&*<]\s*$`) scored `Foo &bar(` / `Foo *bar(` as USE -- the self-test caught it, hence the both-sides bound. Census re-run before/after (`--bucket all --json`): zero 411 / tests-only 290 / tools-only 36 both times, **0 rows changed class** |
| 5 | `renderer_event_receiver.cpp`: `CameraZoomIn/Out` arms use `-kCameraZoomStepUnits` / `kCameraZoomStepUnits` (includes `camera_rig.h`) | `test_camera_mode_wiring.cpp` `CameraModeWiringSource.TheZoomKeysUseTheOneZoomStepConstant` -- RED (literal `2.0f` found); GREEN |
| 6 | `renderer_frame_input.cpp`: the `updateRigCamera()` right after `updatePlayerMovement()` is deleted; the remaining call moved OUT of `if (ctx_.doorManager)` so it runs once per frame after `carryPlayerOnLifts()` (with or without doors). Comments in `renderer_player_physics.cpp`, `renderer_player_lift.cpp`, `test_movement_order.cpp` updated | `CameraModeWiringSource.TheRigCameraIsPlacedOncePerFrameAfterTheLiftCarry` -- RED: 2 calls, first before the carry, second inside the door block; GREEN. No frame-order golden exists for this TU (`movement_order_g06` pins `renderer_player_physics.cpp`, `frame_render_order_g05` pins `renderer_frame_render.cpp`; neither moved), so this source pin is the net |
| 7 | `PlayerCameraComponent::resetChaseEyeScale()` (`renderer_camera_modes.cpp`, declared beside `setUserCameraRigs()`), called from the T467 reset block in `FrameComponent::syncUiPreferences()` | `test_ui_preferences_identity.cpp` `AThirdCharacterDoesNotInheritTheChaseEyeScale` -- RED: 9.0345 vs 6.023; GREEN. `AZoneLoadForTheSameCharacterKeepsItsChaseEyeScale` guards the zone case (green both sides) |
| 8 | Docs: `docs/module_map.md` -- rows added for `corpse_death_pose.cpp`, `equipment_actor_geometry.cpp` (marked T485's territory), `gles2_vertex_color.h`, `text_batch.cpp`, `ui_quad_batch.cpp`, `chat_wrap_cache.cpp`; a new "Event bus" section with the seven `include/client/state/event_*.h` rows; `tests/spell_wire_payloads.h` named in the Test build section; row 44 now 143 table entries / 39 `On*` handlers / 2 ignored opcodes (measured); `renderer_frame_render.cpp` 752, `renderer_zone_stream.cpp` 546, and the counts of every TU this unit touched (`renderer_frame_input.cpp` 1063, `entities_component.cpp` 905, `_updates` 807, `renderer_event_receiver.cpp` 408, `renderer_frame_preferences.cpp` 157, `renderer_player_physics.cpp` 997, `renderer_player_lift.cpp` 130, `renderer_camera_modes.cpp` 269, `entity_renderer.cpp` 544); hygiene count 11 (DocCounts caught the new test). `CLAUDE.md` event-types line -> `event_types.h`. `tools/equipment_index_builder.cpp` header and `tools/hwcaps/ui_inventory_capture.cpp` comment say the table was dropped (T456, B480). `renderer_camera.cpp` placeCameraForZoneIn comment: First Person since T471. `camera_rig.h` `kChaseRig`: B721/B680 caveat. `docs/decisions.md`: 2026-09-13 camera-set entry (F9 replaces Ctrl+F5, no Free mode, official set, `/camera`). `willeq_database.h`: version bumps on a change the CLIENT reads; T454/T456's drops kept v7; the input digest + DbReproducible pin the file | `DocCitations` 1361 ok / 0 fail / 52 skip; `DocCounts` OK; `DocClaims` OK; `CommentClaims` OK (all run directly and under ctest) |
| 9 | Board B780 addendum: `ParticleManager::render()` billboards ARE drawn every frame (`renderer_frame_render.cpp` `renderParticleEffects()`, gated on `isEnabled()` and `zoneReady`), the tumbleweed mesh bakes two browns per vertex, the placeholder door prism bakes `colorFromName()` -- "not rendered" corrected for all three; decision stays Will's | reading only, as the plan says |

Board (item 8's second half): B762 struck (CLOSED by T483 resumed, B770
option (a)); B588 carries a "T485 (slot1, in flight) OWNS this" line; B730,
B731, B732 un-struck and re-marked "FIXED ON THE HELD BRANCH ONLY -- unmerged".

### Deviations (flagged)

- **A T160 pin reversed in place.** `tests/test_combat_manager_corpses.cpp`
  `CombatManagerCorpseRecognition.PlayerCorpseSpawnedAtZoneInIsACorpse`
  asserted `EXPECT_FALSE(entities.at(30).is_corpse)` with the reason "spawn
  parsing still does not write the flag -- recognition is the predicate's
  job". That is exactly the design item 2 changes, so the full suite's one red
  was this test. Changed to `EXPECT_TRUE` with the T486 reason; the rest of the
  case (the predicate, `SetTarget`, the corpse-consider opcode) is unchanged
  and still passes. Not a looting/targeting behaviour change: `CombatManager::
  IsCorpse()` already said corpse for that entity via the NPC byte.
- **Item 3's witnessed path now sets `corpsePositionAdjusted = true` at once**
  (the ladder's bookkeeping). The two per-frame blocks that used to set it
  after the animation finished (`entity_interpolation.cpp`, `entity_updates.cpp`)
  therefore no longer tick `corpseTime` for a witnessed corpse; they only ever
  wrote the same `true` / `0.0f` the ladder writes, and `corpseYOffset` has no
  reader, so nothing drawn changes.
- **Item 6 moves the rig placement's cost from the `playerMovement` frame-timing
  bucket to `doorUpdate`** (it now sits after the door block). Same work, one
  bucket over.
- **Item 4's self-test did not exist** ("add the four probe shapes to the
  script's self-test"): the script had none. `--self-test` and its ctest are
  new.
- Private-member access in two tests (`#define private public`, the
  `test_spell_body_type_targeting.cpp` idiom): item 1 reads `lruOrder_` /
  `cache_[].lruIterator` (no public method exposes the invariant), item 7
  plants `chaseEyeAboveFeet_` (only a built player model writes it in
  production).
