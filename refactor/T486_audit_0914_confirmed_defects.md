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
