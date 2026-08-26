# T278 — manual tradeskill combine + close container (T264 F7)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live combine. Class A.
Territory: the combine/close-container intents + handler hops T264 KEPT
(delist in `tests/test_reachability_sweep.cpp` when wired), the
`SendTradeSkillCombine` / `SendCloseContainer` senders (T264 noted both are
reached only via kept blocked arms), CommandProcessor `/recipe*` family,
`EverQuest::m_active_tradeskill_object_id` (T232's note: this is where the
open container lives), tests. Do NOT touch buff bar (T277 slot1), scribe
(T276 slot3).

## Gap
`/clickobject` opens a forge/oven (T232 confirmed
`TradeskillContainerOpened/Closed` are published), `/recipesearch` and
`/recipecombine` exist — but T264 found the manual combine and the close
hop have no producer. Establish exactly which part is unreachable before
writing code (the recipe commands may already cover part of it).

## Scope
1. Trace the real path: `/recipecombine` → which sender → does it reach
   `SendTradeSkillCombine`? If it does, the F7 gap is only the container
   CLOSE and the mouse gesture — say so and scope down.
2. Wire what is actually missing, command-first (T156 rule). Verify the
   wire form against eqemu (`Handle_OP_TradeSkillCombine`,
   `Handle_OP_ClickObjectAction` close path) — T273's lesson: check field
   ids in `SerializeItem`/the struct, do not trust member names.
3. Tests: combine request bytes with a container + items; close request;
   the container model clears on the server's confirmation. Red/green.
4. Live: `#summonitem` a Tailoring/Baking kit or use a world container
   (`/clickobject` on a forge in befallen/qeynos2 — T268's item staging
   recipe), put components in, combine, DB shows the product; close the
   container. Restore rows.

## Verification
Full ctest; Review; evidence review/T278/. Commit:
`T278: manual tradeskill combine and container close reachable`.

---

# Review (T278, slot2, 2026-08-26)

Branch `task/T278-tradeskill-combine` off fable `83567499` (the plan commit).

## 1. The trace, first (Scope 1) — what was unreachable vs already working

| hop | before T278 |
|---|---|
| `/recipesearch`, `/recipedetails`, `/recipecombine`, `/recipefavorites` | **already working** (T149) |
| `/recipecombine` -> `SendTradeSkillCombine` | **NO** — it goes to `SendRecipeAutoCombine` (OP_RecipeAutoCombine, a *recipe id*, server picks the components). Different opcode, different feature. |
| `SendTradeSkillCombine` (OP_TradeSkillCombine, the MANUAL combine) | **unreachable** — only caller was the `TradeskillCombineIntent` arm in `eq_bridge.cpp`, and nothing pushed that intent |
| `SendCloseContainer` (OP_ClickObjectAction open=0) | **unreachable** — same shape, via `TradeskillCloseIntent` |
| `/clickobject` -> container opens, `TradeskillContainerOpened` published | **already working** (T144/T232) |
| `InventoryManager::clearWorldContainerSlots()` | **unreachable** — zero callers (see 3 below) |

So the F7 gap was exactly the two hops the plan named, plus one this unit found.

## 2. What was wired (Scope 2), and the wire forms checked against eqemu

`/combine` and `/closecontainer`, command-first (T156):
`CommandProcessor` -> `ActionDispatcher` -> `EqActionHandler` ->
`EverQuest::RequestTradeskillCombine(int16_t)` / `RequestCloseTradeskillContainer()`
-> the kept intents -> `ProcessBridgeIntents()` -> the two senders. Same shape as
T273's `/read`; the intents are delisted from `kBlockedIntents`.

Wire forms verified against eqemu source, not member names (T273's rule):

- **OP_TradeSkillCombine (0x0b40)**. `Handle_OP_TradeSkillCombine` is
  `DECODE_LENGTH_EXACT(structs::NewCombine_Struct)` — titanium
  `{ int16 container_slot; int16 guildtribute_slot; }`, 4 bytes, which matches
  `EQT::NewCombine_Struct` and what `SendTradeSkillCombine` writes.
  `TitaniumToServerSlot` maps 1000 -> 1000, so
  `SLOT_TRADESKILL_EXPERIMENT_COMBINE` is correct on the wire unchanged
  (`common/patches/sof_limits.h:150`, reused by titanium via `emu_constants.h`).
- **OP_ClickObjectAction (0x2eae)**. `Handle_OP_ClickObjectAction` requires
  exactly `sizeof(ClickObjectAction_Struct)` = 92 (no Titanium translation
  exists for this opcode) and treats `open == 0` as `Object::Close()`; any
  other value is logged as "Unsupported action". `SendCloseContainer` already
  wrote all of that correctly.

**Deviation from the plan's Scope, flagged:** `/combine` also takes an
optional **carried-container slot** (`/combine <general slot 22-29>`). This is
not scope creep for its own sake — `container_slot` is the *only* thing that
distinguishes the two container kinds on the wire, and
`Object::HandleCombine` branches on `container_slot == 1000` vs
`user_inv.GetItem(container_slot)`. T149's "some_id" caveat does **not** apply
to this opcode: `some_id` is a `HandleCombine` local computed server-side, and
appears on the wire only for the `/recipe*` family. Without the carried form
there is no way to prove a combine end-to-end live at all — see 4.

## 3. A defect found and fixed on the way: OP_ClearObject was misread

`ZoneProcessClearObject` read the first four payload bytes as a world-object
`drop_id` and erased `m_world_objects[that id]`. **OP_ClearObject has no such
field.** eqemu's `ClearObject_Struct` is `{ uint8 Clear; uint8 Unknown001[7] }`
and all three senders set `Clear = 1`, so the "object id" was always the
literal **1** — any ground spawn with `drop_id == 1` was silently deleted from
the client's world-object map.

What the packet means is "the world container's CONTENTS are gone": eqemu
queues it from `Object::Close()` and from the world-container branches of
`Object::HandleCombine()` / `HandleAugmentation()` after they `Clear()` the
container. It is **not** a close notification — after a combine the window
stays open with empty slots — so the active container id is deliberately left
alone. The handler now empties the world container slots (4000-4009) and
announces each one, which finally gives
`InventoryManager::clearWorldContainerSlots()` (zero callers since it was
written) its caller.

Also worth recording, because it silently shapes the close path: **eqemu never
echoes `ClickObjectAction open=0` back.** `Object::Close()` answers with
OP_ClearObject plus two OP_FinishWindow. The client therefore clears
`m_active_tradeskill_object_id` when it *asks* to close, which
`SendCloseContainer` already did.

## 4. Tests (Scope 3) — measured red/green

New `tests/test_tradeskill_combine.cpp`, 11 cases, real `EverQuest` over a real
`DaybreakConnection`/`FakeUdpTransport` (the T273/T234 idiom, so the **bytes**
are asserted, not just the call).

- **RED** (`review/T278/red_test_tradeskill_combine.txt`): the same file built
  against the pre-T278 production sources — **1 passed, 8 FAILED** (the file
  had 9 cases at that point; the two carried-container cases were added after
  the red measurement, with the argument). The one pass is the control
  (`CombineDoesNotCloseTheContainer`, trivially true when `/combine` does
  nothing). The red run also *demonstrates* the OP_ClearObject bug:
  `ClearObjectDoesNotDeleteTheWorldObjectWithDropIdOne` fails with
  "OP_ClearObject deleted an unrelated ground spawn".
- **GREEN** (`review/T278/green_test_tradeskill_combine.txt`): 11/11.
- Full suite (`review/T278/ctest_full.txt`): **3074/3075**; the one failure is
  the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds`.

## 5. Live (Scope 4) — eqemu, qeynos2

Lock taken by `noclobber` after a bounded wait (T277 held it; 7 retries).
`up.sh qeynos2`, three client sessions, `down.sh`, lock deleted.

**Session `live_combine.log` — the WORLD container (forge drop_id 202):**

| step | evidence |
|---|---|
| no container open | `You must have a tradeskill container open.` / `You do not have a tradeskill container open.` — no packet |
| `/clickobject 202` | `Opening tradeskill container: drop_id=202 type=17` |
| `/recipesearch Boots` | server answered with the forge's recipe list, incl. `Boots Of Ro (ID: 3815)` — the server agrees the open container is type 17 |
| `/combine 4321` | refused, usage names `/recipecombine` |
| `/combine` | `Sending tradeskill combine for container slot 1000`, server answered `string_id=334 'You cannot combine these items in this container type!'` + the 2-byte OP_TradeSkillCombine ack — **the packet parsed and reached `Object::HandleCombine` -> `GetTradeRecipe`** |
| `/closecontainer` | `Closing tradeskill container 202`, then the server's OP_ClearObject -> the new handler ran (`ClearObject: emptying world container slots`) |
| `/combine` after close | refuses again |

The forge was **empty**, which is why 334 came back — and finding out why is a
result in itself (see 6).

**Session `live_kit2.log` — a CARRIED container, the full product:**

| step | evidence |
|---|---|
| staged (`live_kit.log`) | `#summonitem 17909` (Forge, bagType=17 bagSlots=10 — the client's item parser reads both), moved to general slot 29; `#summonitem 12304` + `12305` moved into its bag slots 321/322 |
| `/combine 4321` | refused (not a general slot), usage names `/recipecombine` |
| `/combine 22` | `Scaled Curskin (slot 22) is not a container.` — local refusal, no packet |
| `/combine 29` | `Sending tradeskill combine for container slot 29` -> 2-byte ack -> **`You have fashioned the items together to create something new!`** -> `Boots of Ro` (item 3156) on the cursor |

**DB before/after/restored** (`db_before.txt`, `db_after_run.txt`,
`db_restored.txt`, writes + restores in `db_writes.txt`): after the combine the
DB held `slot 33 -> 3156 Boots of Ro` and both components were gone.
`diff db_before.txt db_restored.txt` is **EMPTY**.

## 6. Owed / found, not fixed

1. **The client cannot put items INTO a world container.**
   `InventoryManager::isValidSlot()` does not accept the world slots
   (4000-4009), so `/equip <from> 4000` is refused before any packet. The
   world-container form of `/combine` is therefore reachable and correct on the
   wire but not yet *useful* end-to-end from the client alone. This is a
   separate hop in inventory territory (not this unit's) and is the natural
   next unit for F7.
2. **eqemu wipes a world container on the first open by a new user.**
   `Object::HandleClick` calls `ItemInstance::ClearByFlags(byFlagSet,
   byFlagSet)` when `user != last_user`, and that function's `switch` falls
   through from `byFlagSet` into `byFlagNotSet` — so both arms run and **every**
   item is deleted regardless of its NoDrop/NoRent flags (the function carries
   a `// TODO: This needs work...`). This is why pre-filling `object_contents`
   in the DB (WRITE 1) could not stage a world combine. Server-side, not ours
   to fix; recorded so the next unit does not repeat the experiment.
3. Inventory *bag* containers are now combinable, but the `/recipe*` family
   still only addresses world containers (their wire form carries
   `object_type`/`some_id`, which T149 scoped to world objects).

## 7. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2-config tree) | PASS |
| `ctest --test-dir build --output-on-failure` | **3074/3075** — only `AudioManagerTest.PreloadCommonSounds` (documented ALSA quirk). `ctest_full.txt` |
| `scripts/build-gles2-x86.sh` | PASS (`build_gles2_x86.log`) |
| Visual gate | **PASS**, goldens untouched (`visual_gate.log`) |
| `scripts/build-arm-noble.sh` | PASS (`build_arm_noble.log`) |
| `scripts/refactor/check_file_sizes.sh fable` | 1 FAIL = `src/client/eq.cpp` 3930 lines, **pre-existing and untouched by this unit**. `check_file_sizes.txt` |

**Deviation:** `include/client/action/action_dispatcher.h` crossed the 1500-line
WARN ceiling for the first time (1496 -> 1510) by taking two new virtuals and
their documentation. Reported rather than hidden by shrinking the comments; the
file is a candidate for a split, and `command_processor.cpp` at 2968 is now
within 32 lines of the 3000 FAIL ceiling.
