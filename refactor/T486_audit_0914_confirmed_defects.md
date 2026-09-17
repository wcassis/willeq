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

## Review (T486, slot3, branch `task/T486-audit-0914-defects`, rebased onto `fable` `c637518a`)

**Verdict: all nine items done, no STOP hit.** Every claim below is tagged
`[measured]` (a command was run in this worktree and its output read) or
`[assumed]` (reasoned from the code, not executed).

**Provenance.** A 2026-09-14 executor died mid-unit; its 34 uncommitted files
were committed verbatim as the branch tip and inherited by this session. The
branch was rebased onto `fable` `c637518a` (T492 llmbench corpus, T493 restore
fix) with **no conflicts** `[measured]`, and the WIP was then re-verified from
scratch rather than trusted: nothing here rests on the dead executor's own
Review, which has been replaced by this one. **Every one of its nine items was
kept; nothing was dropped** `[measured]` — each production fix was reverted in
place, its test rebuilt and run RED, and the fix restored (table below), and
the restored tree is byte-identical to the commit (`git diff HEAD` empty)
`[measured]`.

Two corrections to the inherited Review, which stated them wrongly:

- It said `review/AUDIT-0914/README.md`, the plan's named source, "does not
  exist". It **does** exist, at `/mnt/projects/fable/review/AUDIT-0914/README.md`
  `[measured]` — `review/` is untracked and lives beside the checkout, not
  inside a worktree (`git ls-files review` empty). It was read here, and it
  confirms the framing of items 2 and 9; the plan's item text remains the
  contract.
- Its `DocCitations` figures (1,361 ok / 52 skip) were from the pre-rebase tree.
  Current: **1,389 ok / 0 fail / 54 skip** over 14 docs and 1,443 citations
  `[measured]`.

### Per item

| # | Fix | Red proved here (how) → green |
|---|---|---|
| 1 | `constrained_texture_cache.cpp` `clear()`: the preserve loop re-points `entry.lruIterator = std::prev(lruOrder_.end())` after its `push_back` | **RED `[measured]`**: fix reverted to `fable`, test rebuilt and run — `'t486_protected_a' holds an iterator to the node carrying 't486_protected_c'` (and c→a). GREEN with the fix. No ASan on this host, so the assertion is the invariant (each entry's iterator must point at the live node carrying its own name), which the unfixed cache fails without dereferencing freed memory `[measured]`. Reachability: `protectTexture()` has five production callers — `ui_atlas`, `software_cursor_texture`, `item_icon_sheets`, `boids_manager`, `tumbleweed_manager` `[measured]`; the "every second zone load" frequency is `[assumed]` |
| 2 | ONE owner for `Entity::is_corpse`: `IsCorpseNpcType()` (`corpse_predicate.h/.cpp`, `== 2 \|\| == 3`; `IsCorpseByFields()` calls it), and both Spawn_Struct parsers set `entity.is_corpse = IsCorpseNpcType(entity.npc_type)` right after reading offset 83. The three bare readers were not touched | **RED `[measured]`**: both parser fixes reverted, tests rebuilt and run — `SpellManagerCorpseTarget.APreExistingCorpseIsACorpseTargetWithoutAWitnessedDeath` fails (`validateCast` ≠ Success for an NPC=2 spawn with no death), and the ratchet `CorpseRecognition.EverySpawnStructParserSetsTheCorpseFlagThroughTheOwner` fails. Both GREEN restored. Exactly two TUs read offset 83 (`entities_component.cpp:153`, `entities_component_updates.cpp:305`) and both are fixed `[measured]`. The three bare readers are at `spell_cast_validation.cpp:86` (into `BodyTypeSatisfiesTargetType`), `bridge_component.cpp:634`, `entities_component_appearance.cpp:84` `[measured]` |
| 3 | `ApplyCorpseDeathPose(visual, node, witnessed)`: `witnessed=true` stops the current animation and plays the ladder's pick once, play-through (the old `markEntityAsCorpse()` semantics); `EntityRenderer::markEntityAsCorpse()` calls it and its own ladder is deleted | **RED `[measured]`**: the three files reverted, test rebuilt and run — `CorpsePoseTest.AWitnessedDeathPlaysTheOneLadderThrough` fails (`corpsePositionAdjusted` false: the copy never set it) and `CorpseRecognition.TheDeathPoseLadderHasOneCopy` fails (2 copies). Both GREEN restored |
| 4 | `check_zero_callers.py` `classify_occurrence()`: a lead ending in `,` or a whitespace-bounded `&` / `*` / `<` (`CONTINUATION_OP`) is a USE. `--self-test` added: 22 probes, wired as the `ZeroCallersSelfTest` ctest | **RED `[measured]`**: `fable`'s classifier run over the same 22 probes scores **4 as DEF that are USEs** (`value, foo(x)`, `a & foo(x)`, `a * foo(x)`, `a < foo(x)`); the fixed one scores 0 wrong. The four definition shapes a fix must not hide (`Foo& bar(`, `Foo &bar(`, `Foo* bar(`, `Foo *bar(`, plus the nested-template return) are in the probe set and stay DEF `[measured]`. **Census re-run before/after (`--bucket all --json`), diffed row by row: zero 411 / tests-only 290 / tools-only 36 both times, 737 rows, 0 changed class** — STOP limit was 5 `[measured]` |
| 5 | `renderer_event_receiver.cpp`: the `CameraZoomIn/Out` arms use `∓kCameraZoomStepUnits` (includes `camera_rig.h`) | **RED `[measured]`**: `fable`'s source carries the literals the test forbids — `cameraZoomDelta_ = -2.0f` / `= 2.0f` at lines 279-280 — so `CameraModeWiringSource.TheZoomKeysUseTheOneZoomStepConstant`, which reads the working-tree source, fails on the unfixed tree. GREEN here |
| 6 | `renderer_frame_input.cpp`: the `updateRigCamera()` after `updatePlayerMovement()` is deleted; the remaining call moved OUT of `if (ctx_.doorManager)` so it runs once per frame after `carryPlayerOnLifts()` | **RED `[measured]`**: `fable`'s source has two `updateRigCamera()` calls (lines 114 and 931, the second inside the door block), so `TheRigCameraIsPlacedOncePerFrameAfterTheLiftCarry` fails on the unfixed tree. GREEN here. Safety of the move: `processFrameInput()` and `processFrameSimulation()` are both called unconditionally from `renderer_frame.cpp` (lines 114 and 218) with no `return` between them, so the one placement is reached on every frame the two placements used to be `[measured]`. No frame-order golden exists for this TU (`movement_order_g06` pins `renderer_player_physics.cpp`, `frame_render_order_g05` pins `renderer_frame_render.cpp`; neither moved) `[measured]`, so this source pin is the net |
| 7 | `PlayerCameraComponent::resetChaseEyeScale()`, called from the T467 reset block in `FrameComponent::syncUiPreferences()` | **RED `[measured]`**: the call site reverted, test rebuilt and run — `AThirdCharacterDoesNotInheritTheChaseEyeScale` fails, 9.0345 vs 6.023. GREEN restored. `AZoneLoadForTheSameCharacterKeepsItsChaseEyeScale` guards the zone case and is green on both sides `[measured]` |
| 8 | Docs — see the list below | `DocCitations` **1,389 ok / 0 fail / 54 skip**; `DocClaims` OK (13 docs, 206 live board entries); `DocCounts` OK ("every stated count matches the tree", hygiene ctest tests = 11); `CommentClaims` OK; `FileSizeCeiling` 1,235 files, 0 warn, 0 fail — all four run directly here `[measured]` |
| 9 | Board B780 addendum (no code change) | Reading only, as the plan says. Re-confirmed here: `particle_manager.cpp:718` builds each billboard vertex with `SColor(alpha, r, g, b)`, and `renderParticleEffects()` is called unconditionally at `renderer_frame_render.cpp:84` every frame `[measured]`. That the resulting pixels are R/B-swapped is inherited from T484's driver finding and is **not** re-measured here `[assumed]` |

**Item 8, what changed.** `docs/module_map.md`: rows added for
`corpse_death_pose.cpp`, `equipment_actor_geometry.cpp` (marked T485's
territory), `gles2_vertex_color.h`, `text_batch.cpp`, `ui_quad_batch.cpp`,
`chat_wrap_cache.cpp`; a new "Event bus" section with the seven
`include/client/state/event_*.h` rows; `tests/spell_wire_payloads.h` named in
the Test build section; row 44 corrected to **143 table entries / 39 `On*`
handlers / 2 ignored opcodes** — re-measured directly here (143 `{HC_OP_` keys
in `ZoneOpcodeTable()`, 39 `On*` declarations in `zone_component.h`, 2 entries
in `kIgnoredOpcodes`) `[measured]`; and the line counts of every TU this unit
touched, each re-measured with `wc -l` `[measured]`:
`renderer_frame_render.cpp` 752, `renderer_frame_input.cpp` 1,063,
`entities_component.cpp` 905, `entities_component_updates.cpp` 807,
`renderer_event_receiver.cpp` 408, `renderer_camera_modes.cpp` 269,
`entity_renderer.cpp` 544 (`renderer_zone_stream.cpp` is at
`src/client/graphics/zone_stream/`, 546). `CLAUDE.md` event-types line →
`event_types.h`. `tools/equipment_index_builder.cpp` and
`tools/hwcaps/ui_inventory_capture.cpp` say the `equipment_models` table was
dropped (T456, B480). `renderer_camera.cpp` `placeCameraForZoneIn` comment:
First Person since T471 — confirmed against
`kDefaultCameraMode = CameraMode::FirstPerson` `[measured]`. `camera_rig.h`
`kChaseRig`: the B721/B680 caveat. `docs/decisions.md`: a 2026-09-13
camera-set entry (F9 replaces Ctrl+F5, no Free mode, official set, `/camera`),
checked line by line against T461's "Will's ruling" section and against the
six-mode `CameraMode` enum, which has no Free `[measured]`.
`willeq_database.h`: the version text now matches the practice.

**Board.** B762 struck (CLOSED by T483 resumed, B770 option (a)); B588 carries
a "T485 (slot1, in flight) OWNS this" line; B730, B731, B732 un-struck and
re-marked "FIXED ON THE HELD BRANCH ONLY — unmerged" — confirmed here:
`task/T479-skeleton-mesh-list-loaders` exists and `fable` carries only T479's
plan and board commits, not its fix `[measured]`. B780 carries the item-9
addendum.

### Deviations (flagged)

- **A T160 pin reversed in place.** `tests/test_combat_manager_corpses.cpp`
  `PlayerCorpseSpawnedAtZoneInIsACorpse` asserted `EXPECT_FALSE(is_corpse)`
  with the reason "spawn parsing still does not write the flag". That is
  exactly the design item 2 changes, so it was the suite's one red; changed to
  `EXPECT_TRUE` with the T486 reason. The rest of the case (the predicate,
  `SetTarget`, the corpse-consider opcode) is unchanged and passes. Not a
  looting/targeting behaviour change: `CombatManager::IsCorpse()` already said
  corpse for that entity via the NPC byte `[measured]`.
- **Item 3's witnessed path sets `corpsePositionAdjusted = true` at once.** The
  two per-frame blocks that used to set it after the animation finished
  (`entity_interpolation.cpp`, `entity_updates.cpp`) therefore no longer tick
  `corpseTime` for a witnessed corpse. They only ever wrote the same `true` /
  `0.0f` the ladder writes, and `corpseYOffset` has no reader, so nothing drawn
  changes `[assumed]` — reasoned from the code, not rendered.
- **Item 6 moves the rig placement's cost from the `playerMovement` frame-timing
  bucket to `doorUpdate`.** Same work, one bucket over `[measured]`.
- **Item 4's self-test did not exist** ("add the four probe shapes to the
  script's self-test"): the script had none, so `--self-test` and its ctest are
  new.
- **Private-member access in two tests** (`#define private public`): item 1
  reads `lruOrder_` / `cache_[].lruIterator` (no public method exposes the
  invariant), item 7 plants `chaseEyeAboveFeet_` (only a built player model
  writes it in production).
- **No ccache on this host** (`/usr/bin/ccache` absent), so the compiler
  launcher the dispatch asked for could not be used; the build tree also
  carried stale ccache rules from an earlier configure, which failed the first
  build with `Error 127` until the reconfigure regenerated them `[measured]`.
- **One build failed with `/usr/bin/ld: final link failed: file truncated` on
  every link at `-j24`.** Not a code failure: `/mnt/projects` is an NFS volume
  at 97% (385G free) with other slots building concurrently; the identical
  build at `-j8` succeeded with exit 0 `[measured]`.

### Suite

`ctest -N`: **4,826 registered**. Full run `[measured]`:

- `ctest -j6 -E RDPServerFrameTest.UpdateFrameMultipleTimes` → **100% tests
  passed, 0 failed out of 4,825**, 246 s.
- That one test run alone → **passed** (0.03 s). It is the documented
  intermittent race and was excluded from the loaded run on the orchestrator's
  instruction, not because it failed here.
- Total: **4,826 of 4,826 green, zero failures, no waivers used.** This matches
  `CLAUDE.md`'s rule that the suite has NO expected failures since T392 (B215) —
  no exception was needed. The registered count is up from `BOARD.md`'s last
  recorded 4,056 at T392, as that line warns it would be; `ZeroCallersSelfTest`
  is this unit's one addition.

The `***Skipped` rows in the log (bridge ASan repro, DoorManagerGraphics,
`PfsArchiveContentTest.DdsFileValid`, some Pfs sound and spell-gem cases,
`StaticUiFrameCostTest`) are gtest skips on a missing artefact or display, not
failures, and they skip identically on `fable` `[assumed]` — not re-run on the
base tree here.

**Not done, and why.** Nothing in the plan needed a live run, so nothing is
blocked: item 2's user path is proved by replaying a Spawn_Struct with NPC=2
through the real parser and calling `validateCast`, not by a cleric on the
server. This unit took neither eqemu lane and no lock. The ARM build the Proof
section asks for was **not run** — an `arm-linux-gnueabihf-ar` from another
slot was active on this host throughout, and the cross tree is shared; it is
owed before merge `[measured: not run]`. No image golden moved (none was
touched; `visual_gate` is not in the default suite and was not run).

