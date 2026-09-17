# T494 — B796: `/equip`'s slot numbers are Titanium's, not the server's

Class A. **Lane: LOCAL eqemu** (`export EQEMU_HOST=local` before sourcing
`scripts/eqemu/env.sh`; client config host `10.0.30.23`; `scripts/scenario.sh`).
The VM at 172.16.200.99 is free but not needed.

**Orchestrator's decision (class C, taken 2026-09-17 while Will is away, reversible):
user-facing slot numbers ARE `inventory.slot_id` (server/RoF2 numbering).** Reason:
the only people who type slot numbers are developers and rigs reading the DB and
server logs, both of which print RoF2 slots; the official client never shows a
number, so no player convention is broken. If Will prefers Titanium numbering at
the user boundary, the conversion moves to the OUT side only and the tests flip
back — record either way in `docs/decisions.md`.

## What the off-by-one actually is

- [measured] **fable's inventory model is TITANIUM-numbered and the DB is
  ROF2-numbered; nothing in fable converts between them.**
  `/mnt/projects/akk-stack/code/common/emu_constants.h:155-156` makes
  `EQ::invslot` an alias of `RoF2::invslot`, and
  `/mnt/projects/akk-stack/code/common/patches/rof2_limits.h:144-157` gives that
  namespace `slotWaist=20, slotPowerSource=21, slotAmmo=22, slotGeneral1=23 …
  slotGeneral8=30, slotCursor=33`. `inventory.slot_id` is in THAT numbering.
  `include/client/inventory/inventory_constants.h:10-46` is the Titanium set:
  `SLOT_AMMO=21`, `GENERAL_BEGIN=22`, `GENERAL_END=29`, `CURSOR_SLOT=30`.
- [measured] The conversion exists only on the SERVER:
  `/mnt/projects/akk-stack/code/common/patches/titanium.cpp:3576`
  `ServerToTitaniumSlot()` and `:3666` `TitaniumToServerSlot()` — identity ≤ 20,
  **+1 for ammo (21↔22) and for general1-8 (22-29 ↔ 23-30)**, +3 for the cursor
  (30↔33), identity for bank/trade/world, and a large remap for general bags
  (Titanium 251-330 ↔ server `invbag::GENERAL_BAGS_BEGIN`=4010 + 200·bag + idx,
  `emu_constants.h:223` + `rof2_limits.h:225`).
- [measured] **fable's wire is already CORRECT, so there is no `+1`/`-1` to find
  in fable — the defect is the absence of one at the user boundary.** Inbound:
  the server serialises the already-translated Titanium slot into instance field
  2 (`titanium.cpp:400`, `:1135`, `:3340`) and
  `src/client/inventory/inventory_item_parser.cpp:173` takes it verbatim.
  Outbound: `src/client/merchant/merchant.cpp:178-181` writes the model's slot
  verbatim and `titanium.cpp:2993-2994` decodes it with `TitaniumToServerSlot`.
  The one surviving fragment of the mapping in fable is the magic
  `SERVER_CURSOR_SLOT = 33` (`inventory_constants.h:46`), special-cased at
  `inventory_item_parser.cpp:145`, `inventory_manager.cpp:32/44/56/160/182/218`,
  `graphics/ui/cursor_attachment.cpp:29`, `graphics/ui/static_ui_input.cpp:884`.
- [measured] **The exact user-boundary functions.** IN:
  `ParseEquipSlot()` — `src/client/commands/commands_component.cpp:298`, called
  at `:748` (`/equip <from> <to>`) and `:855` (`/read`/`/scribe` numeric form);
  `ParseMerchantSlot()` via `cmdSell` (`command_processor_world.cpp:349-366`);
  `cmdCombine` (`command_processor_world.cpp:178-183`, which also PRINTS
  `GENERAL_BEGIN`-`GENERAL_END` = "22-29"). OUT: `describeSlot()` —
  `include/client/inventory/inventory_constants.h:809`, the single formatter that
  produced the board's `"… cannot go in Primary (13)"`. All of them pass/print
  raw model (Titanium) numbers.
- [measured] **Only the slash path is affected; there is no inventory-window
  drag path.** Every `MoveItemIntent` in the tree is pushed from
  `inventory_cursor.cpp`/`inventory_manager.cpp`; `static_ui_input.cpp` pushes no
  item-move intent at all. Reachable paths: typed `/equip|/read|/scribe|/sell|
  /combine`, the same via `--script` (`docs/decisions.md:485-496`), and the
  llmbench timelines.
- [measured] **The T492 experiment's seed is itself wrong.**
  `scripts/llmbench/record.sh:229-231` inserts `slot_id=21` for characters 67/68
  — RoF2 slot 21 is `slotPowerSource`, which `ServerToTitaniumSlot(21)` maps to
  `SLOT_INVALID`, so the dagger is not serialised to a Titanium client at all;
  `timeline_legH.tsv:29,31` / `timeline_legI.tsv:29,31` then send `/equip 21 13`,
  and Titanium 21 is the AMMO slot = DB 22 — exactly the Shuriken Testcaster was
  refused. [assumed] Testbuddy's reported SUCCESS is NOT explained by this
  (his Titanium 21 should have been empty); that half must be re-measured before
  B796 is struck.

## Tasks

1. **Red test first, offline.** New `tests/test_inventory_slot_convention.cpp`
   in the `test_eq_commands_equip` target (fixture
   `tests/eq_commands_equip_test_fixture.h`). Seed a fake inventory through the
   parser exactly as the wire would: an item at DB 21 (must be INVISIBLE), a
   dagger at DB **23**, a Shuriken at DB **22**; run `RequestEquip("23 13")`;
   assert the `MoveItemIntent` drained from `TestBridge` is
   `{fromSlot=22,toSlot=13}` (the wire form) and the system message names the
   Dagger, not the Shuriken. Add the board's exact case: item at DB 21 →
   `/equip 21 13` must answer "nothing there / that slot is not sent to this
   client", never move DB 22. Both are RED today.
2. **Fix at the root, not in the command.** Add the two conversions to
   `include/client/inventory/inventory_constants.h` as
   `serverSlotToClient(int16_t)` / `clientSlotToServer(int16_t)`, ported field
   for field from `titanium.cpp:3576/:3666` with that citation, and derive
   `SERVER_CURSOR_SLOT` from them instead of the literal 33. Then apply them at
   exactly two seams, both in the inventory layer:
   - IN: one `parseUserSlot(const std::string&, int16_t&)` next to
     `describeSlot()`, which parses AND converts. Delete `ParseEquipSlot`
     (`commands_component.cpp:298`) and re-point `:748` and `:855`; re-point
     `ParseMerchantSlot`'s slot arm (`cmdSell`) and `cmdCombine`.
   - OUT: `describeSlot()` (`inventory_constants.h:809`) prints
     `clientSlotToServer(slotId)`, so `/equip list`, every refusal and the
     `/equip 22 30`-style hint text in `inventory_cursor.cpp:713-717` round-trip
     with what a user can type. `cmdCombine`'s usage string becomes 23-30.
   [assumed] The bag half of the table (Titanium 251-330 ↔ 4010+) is the one
   range not confirmed against real data — before committing it, dump
   `SELECT slot_id FROM inventory WHERE character_id=68` on the lane and check a
   bagged row against `getBagContentsSlots()`. If it disagrees, ship possessions
   +bank/trade/world only and refuse (not silently pass through) bag numbers.
3. **Flip what encodes the old convention.** [measured] 48 numeric `/equip`
   call sites across `tests/test_eq_commands_equip_{moves,swaps,bags}.cpp` and
   `tests/test_command_processor_world.cpp:370,393`. Only the TYPED argument and
   the expected message text flip (22→23, 30→33, 2000/3000/4000 unchanged); the
   parser-seeded wire strings and every model-side expectation
   (`MoveItemIntent{22,13}`, `test_inventory_model_view.cpp`) must NOT flip —
   the split is the point of the test pass. Flip the timelines too:
   `record.sh:229-231` seeds DB **23**, `make_timeline_legH.py:97` and
   `timeline_leg{H,I}.tsv` become `/equip 23 13`, and the two parked
   `E_heldB_p2_*` steps lose their `expect=unlabelled`.
4. **Live proof on the LOCAL lane, observed not asserted.** New scenario
   `equip_observed` (`src/client/scenario/`, registered in
   `scenario_registry.cpp`): the OBSERVER logs in, waits for the peer
   (`OtherPlayerCount()`), sends nothing, and asserts the peer's
   `Entity::equipment[7]` went 0 → the dagger's IT model — filled ONLY by
   `EntitiesComponent::ZoneProcessWearChange()`
   (`entities_component_appearance.cpp:409-440`), i.e. the server's appearance
   packet, not the actor's own belief. This needs one new accessor on
   `IScenarioWorld` (`include/client/scenario/scenario.h:47-64`), e.g.
   `PeerEquipmentMaterial(name, materialSlot)`. The peer is the second process
   `scripts/scenario.sh` already launches for `two_client_survive`
   (`scenario.sh:156-176`); it runs `/equip 23 13` from its own scenario.
   Seed DB 23 before the run, verify exit 0 and the note line, keep the logs in
   `review/T494/`.
5. **Board.** Strike B796 with the measured mapping and the `record.sh` seed
   defect; open a new entry for the Testbuddy half if task 1/4 cannot reproduce
   it; open one for `LootItemIntent`'s corpse-slot numbering, which has the same
   Titanium/server split (`titanium.cpp:3643` `ServerToTitaniumCorpseSlot`) and
   is NOT in this unit's scope.

## Rules
- Lock: `eqemu_lock_take T494` on this host's `/mnt/projects/.eqemu-lock`;
  exit 64 = stop. Release on every path. `down.sh` at the end.
- Restore through `scripts/eqemu/restore_lib.sh` only (T493); never re-inline.
- No Testchar (T489). No edits to the shared eq-oracle client copy.
- One convention, stated once: user-facing slot numbers ARE `inventory.slot_id`.
  Document it in the `/equip` usage string, `CLAUDE.md`, `docs/decisions.md`,
  and the header comment at `inventory_constants.h:10`.
- Commit code, tests, timelines, board, plan Review. Corpus output to
  `review/T494/` with a README.

## Review

Branch `task/T494-equip-slot-convention`, base `9a99d54c` on fable `8c6e39cf`.
Corpus, logs and the reproduction recipe: `/mnt/projects/fable/review/T494/`.

### What did not work, first

- [measured] **The plan's task-4 observable does not exist on this server.**
  The first `equip_observed` design waited for an `OP_WearChange` on the peer's
  primary after the equip and got NOTHING in 150 s -- while the `inventory`
  row had demonstrably moved `23 -> 13` (polled in the database during the
  run, `review/T494/inventory_row_poll_during_run.log`). Cause, read off the
  server: `zone/inventory.cpp:2205` takes the material slot from
  `Client::SlotConvert2()`, whose switch (`zone/client.cpp:4128-4154`) has
  cases for the seven ARMOR slots only, so `slotPrimary`(13) falls through to
  its `uint8 slot2 = 0` default; the packet therefore describes the HEAD,
  whose material did not change, and `zone/mob_appearance.cpp:420-428`'s
  dedupe drops it. The weapon materials ARE correct in the SPAWN packet
  (`Mob::SendWearChangeAndLighting` runs to `weaponPrimary`=7 -- the observer
  logged a burst of nine for that character at its spawn). So the scenario now
  has the actor equip and EXIT, and the harness logs the same character
  straight back in; the proof is the fresh spawn packet, which is still
  entirely the server's statement. Boarded **B809**.
- [measured] **The plan's `scripts/scenario.sh has a lane guard` is wrong.**
  It did not have one; `scripts/eqemu/restore_selftest.sh:85-131` did. T494
  gave `scenario.sh` the same lane-following config default and the same
  refusal.
- [measured] **The plan's "the two parked `E_heldB_p2_*` steps lose their
  `expect=unlabelled`" does not apply.** Those steps carry
  `expect=discrepancy:B725` at this tip, not `expect=unlabelled`, and B725 is a
  held-item RENDERING discrepancy untouched by slot numbering. The parking
  stands; only the `/equip` argument and the `moved from slot N` prose changed.
- [measured] **Three ctest cases fail in a fresh worktree for an environmental
  reason** (`data/eq_client` is a gitignored symlink that only `main` has).
  Boarded **B812**; making the symlink turns them green and the suite is
  4842/4842.
- [measured] **`git diff --stat scripts/` dumps 700 KB** because
  `benchmarks/contact_sheets/*.html` embeds base64 PNGs; scope any `grep -rn`
  over the tree away from `benchmarks/`.

### The red

[measured] At `86bf335f`, `tests/test_inventory_slot_convention.cpp` seeds the
model the way the wire does -- the DB slot run through a literal transcription
of `titanium.cpp:3576`, deliberately NOT through any production helper -- and:

```
EquipByDbSlot_AddressesTheRowAtThatSlotId_NotItsNeighbour
  /equip 23 13  ->  "Nothing to move: slot 23 is empty."   (no MoveItemIntent)
EquipDbSlot21_PowerSource_Refused_DoesNotReachDbSlot22
  /equip 21 13  ->  "Shuriken cannot go in Primary (13) -- wrong slot type
                     for this item."
```

The second is B796's live message verbatim. Full output:
`review/T494/red_before_fix.txt`.

### The fix

[measured] `serverSlotToClient()` / `clientSlotToServer()` in
`include/client/inventory/inventory_constants.h`, ported field for field from
`titanium.cpp:3576/:3666` with the constants from
`{titanium,rof2}_limits.h`/`emu_constants.h`. `SERVER_CURSOR_SLOT` is now
derived from `clientSlotToServer(CURSOR_SLOT)` and `static_assert`ed, not the
literal 33.

[measured] Applied at exactly two seams: `parseUserSlot()` IN (replacing
`ParseEquipSlot`, which is deleted; re-pointed `/equip`'s slot form,
`/read`/`/scribe`'s numeric form, `/sell`'s slot and `/combine`'s container
slot -- `/buy` is NOT converted, its `<slot>` is an index into the merchant's
list) and `describeSlot()` OUT. `parseUserSlot()` returns three outcomes so a
typo and an unreachable-but-real slot get different answers, and it ROUND-TRIPS
through both directions, refusing anything that does not survive -- which is
what makes bank 2016-2023, general bags 9/10 and container offsets above 9
refusals rather than silent slides.

[measured] The wire and the model did not move: `MoveItemIntent{22,13}`, the
parser's verbatim take of the server's slot, and every model-side test
expectation are unchanged. The live run's own trace shows the split:
`Moving item: slot 23 -> Primary (13)` next to
`Sending MoveItem packet: 22 -> 13`.

### The `[assumed]` bag range, now measured

[measured] The plan asked for a bagged row to be checked against
`getBagContentsSlots()`. There was none to check: the whole `inventory` table
is `MIN(slot_id)=1, MAX(slot_id)=29` over 154 rows, **zero** rows at
`slot_id >= 4010` -- every Backpack\* in the database is empty
(`review/T494/bag_range_db_check.txt`). So one was made: a Dagger\* seeded at
`inventory.slot_id = 5210` (= 4010 + 200x6 + 0, bag 6 being the Backpack\* at
DB slot 29) came back out of `/equip list` as

```
  Dagger* (slot 5210) -> Primary (13)
```

i.e. the server sent it as Titanium 311, it landed in
`getBagContentsSlots(28)[0]` = 311, and `describeSlot()` printed 5210. The
general-bags half round-trips against real data. The OTHER bag families
(cursor, bank, shared bank, trade) remain `[assumed]` -- they are ported from
the same server function and pinned by the round-trip test, but no real row
exists to check.

### The live proof

[measured] `scripts/scenario.sh equip_observed --unit T494 --inventory-chars
"67,68" --seed-sql ...` on the LOCAL lane, twice, exit **0** both times. The
observer sends nothing; its own log line:

```
OBSERVED: the peer's material slot 7 went 0 -> 5 (all nine
  [0,0,0,0,0,0,0,5,48]) 57.6138s after it first appeared -- read off the
  SERVER's packets for another character, never this client's own belief
```

5 is Dagger\*'s `idfile` IT5. `snapshot restored BYTE-IDENTICALLY` and the
inventory rows for 67/68 were put back on both runs; `down.sh` ran; the lock
was released on every path.

### B796's success half, explained

[measured] B796 recorded that Testbuddy "equipped the dagger and the pair
verified". He did not: `scripts/llmbench/timeline_legH.tsv:27` records, from
the inventory table at compile time, that Testbuddy was ALREADY holding
`13=9997 Dagger* (IT5)` before the `/equip` step ran, and the local database
still shows `68 | 13 | 9997` today. The seed at `slot_id=21` was invisible to
both clients, so BOTH commands were about the ammo slot; only Testcaster had
anything there to complain about. The verification shape that cannot tell
those apart is boarded **B811**, and `equip_observed` refuses outright when the
peer starts out already holding something.

### Suite

[measured] `ctest --test-dir build -j8`: **4842/4842**, zero failures, after
creating the gitignored `data/eq_client` symlink (B812). `visual_gate` was
green; it did not need a rerun.

### Every `[assumed]` left standing

- [assumed] The cursor-bag, bank-bag, shared-bank-bag and trade-bag ranges of
  the conversion table. Ported from the same server function as the general
  bags and pinned by the round-trip property test, but no row in any of those
  ranges exists in the database to measure against.
- [assumed] B809's user-visible consequence (a party member who draws a weapon
  mid-session stays empty-handed on your screen until they respawn) follows
  from the measurement but was not itself watched on screen.
- [assumed] That the official client behaves the same way as fable does under
  B809. It talks to the same server and the same packet is suppressed, but it
  was not run.
