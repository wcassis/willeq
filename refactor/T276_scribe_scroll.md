# T276 — scribe a spell scroll into the spellbook (T264 F5)

Slot: slot3. x86. OWNS eqemu (noclobber) for a live scribe. Class A.
Territory: the `ScribeSpellIntent` arm and its handler hop (T264's Review
names them; `tests/test_reachability_sweep.cpp` still lists it blocked —
delist when wired), the OP_MemorizeSpell/scribe sender (eqemu uses the SAME
opcode with a different `scribing` field — verify), CommandProcessor for
the command form, the spellbook popup click path T272 built (reuse it),
tests. Do NOT touch race data/getters (T274 slot1), ui_renderer (T275 slot2).

## Gap
A spell scroll in inventory can be scribed into the spellbook. eqemu:
`Handle_OP_MemorizeSpell` with `scribing=1` writes `character_spells`;
T272's live run already observed `MemorizeSpell response: ... scribing=1`
on the memorize path, so the reply shape is known.

## Scope
1. Read eqemu's `Handle_OP_MemorizeSpell` (zone/client_packet.cpp) for the
   scribe branch: which slot the scroll must be in, what `scribing` value,
   what it validates (class/level/already-known), and what it sends back.
2. Command first (T156 rule): `/scribe <slot|name>` — resolve the scroll
   via `InventoryManager` (T230's `findItemSlotByItemId` pattern), refuse
   locally with the official string when the item is not a scroll or the
   class/level is wrong (only where the client can know), send otherwise.
3. Mouse: if the spellbook popup or inventory click can carry it, route to
   the same handler body (T230/T239 one-body rule). If no gesture exists,
   say so — the intent stays for the UI leg.
4. Tests: scribe request bytes for a scroll in a slot; refusals send
   nothing; the reply adds the spell to the book model. Red/green.
5. Live: `#summonitem` a scroll the character can use (Testcaster is the
   wizard — see review/T272/ for its staging), `/scribe`, `character_spells`
   gains the row, the spellbook popup lists it, then `/mem` + `/cast` it.
   Restore rows.

## Verification
Full ctest; visual gate if the popup changed; Review; evidence review/T276/.
Commit: `T276: spell scrolls can be scribed`.

---

## Review (executor, 2026-08-26)

Branch `task/T276-scribe-scroll` (slot3), off fable `f42be1db`.
Evidence: `/mnt/projects/fable/review/T276/` (README.md indexes every file).

### 1. The scribe wire truth -- verified in eqemu, not assumed

Read from `/mnt/projects/akk-stack/code`:

| claim | source | verdict |
|---|---|---|
| scribe uses the SAME opcode as memorize | `zone/client_packet.cpp:300` -> `Handle_OP_MemorizeSpell` -> `Client::OPMemorizeSpell` (`zone/client_process.cpp:1145`) | confirmed; `HC_OP_MemorizeSpell` both ways |
| `scribing` for a scroll is **0**, not 1 | `zone/client.h:100-106` -- `memSpellScribing = 0`, `memSpellMemorize = 1`, `memSpellForget = 2` | **the plan's "different `scribing` field" is 0.** T272's live `scribing=1` was the MEMORIZE echo |
| struct = `slot \| spell_id \| scribing \| reduction`, 16 bytes | `common/eq_packet_structs.h:415`, identical `common/patches/titanium_structs.h:407` | our 18-byte packet (opcode + 16) matches; `OPMemorizeSpell` rejects any other size outright |
| `slot` is a SPELLBOOK slot when scribing | `ScribeSpell(m->spell_id, m->slot)`, guarded `slot >= EQ::spells::SPELLBOOK_SIZE` | 0..399, not a gem |
| **the scroll must be on the CURSOR** | `m_inv[EQ::invslot::slotCursor]`, then `item->Scroll.Effect == m->spell_id` | the single fact the client code did not honour -- see below |
| class/level checked server-side | `GetLevel() < spells[id].classes[GetClass()-1]` -> `SPELL_LEVEL_TO_LOW` (12048) | client can and now does refuse first |
| race/class equip check under a rule | `RuleB(Character, RestrictSpellScribing)` -> `CANNOT_USE_ITEM` (181) | server-only; the client cannot know the rule's state |
| success echoes `OP_MemorizeSpell` with scribing=0 and DELETES the scroll | `ScribeSpell(...)` -> `MemorizeSpell(slot, id, memSpellScribing)`; `DeleteItemInInventory(slotCursor, 1, true)` | the echo handler already existed; the deletion was not handled |

Item field mapping re-verified per T273's lesson: counting `ob << '|'`
emissions through `titanium.cpp SerializeItem` and anchoring on T273's
proven `Book`=100 / `BookType`=101 / `Filename`=102 gives a constant offset
of 12, so `Scroll.Effect` (emission 166) is field **154** and `Scroll.Type`
is **155** -- exactly what `TitaniumItemParser` already reads. **No item
field fix was needed this time.**

### 2. What was actually broken

1. **No producer.** `ScribeSpellIntent` was on `kBlockedIntents` (T264 F5).
2. **The consumer arm could never have worked.** `ScribeSpellFromScroll()`
   carried the comment *"the scroll was already moved to cursor when
   Ctrl+clicked (`InventoryManager::setSpellForScribe` sends MoveItem to
   cursor)"* -- and `setSpellForScribe()` has **zero callers** anywhere in
   the tree. Nothing had ever put the scroll on the cursor, so every
   OP_MemorizeSpell this function could have sent would have been answered
   with *"Scribing a spell without an Item Instance on your cursor?"*.
   This is the T272 class again: an arm that compiles, reads plausibly, and
   is wrong, invisible because nothing produced its intent.
3. **`ScribeSpellIntent::bookSlot` was `uint8_t`** against a 400-slot
   spellbook. `ScribeSpellFromScroll()` and `SpellManager::scribeSpell()`
   both take `uint16_t`; the intent was the sole narrowing point on the
   route, so slots 256..399 would have wrapped and silently overwritten a
   known spell.
4. **The spent scroll was never removed from the client's cursor.** The
   server confirms its own deletion with `OP_DeleteItem`, whose handler
   (`InventoryManager::processDeleteItemResponse`) is a comment saying it
   trusts client-side deletion and does nothing. Left alone, the scroll
   would stay on the cursor forever and block the next `/scribe`.
5. **`m_class`/`m_level` instead of `GetClass()`/`GetLevel()`.** The
   refusal read the raw duplicate members rather than
   `m_game_state.player()`. Both are written by the PlayerProfile handler
   today, so it was not a live bug -- but it is the wrong copy and it made
   the checks untestable without replaying a profile packet.

### 3. Design decisions

- **The move-to-cursor lives in `ScribeSpellFromScroll()`, and calls
  `SendMoveItem()` directly.** Routing it through `MoveItemIntent` (which
  is what `InventoryManager::pickupItem()` does) would have queued the
  MoveItem **after** the OP_MemorizeSpell, because both are drained by the
  same `ProcessBridgeIntents()` pass -- the server would have found an empty
  cursor. `InventoryManager::setSpellForScribe()` is still called for the
  local model + pending-scribe bookkeeping (it gains its first caller);
  only the packet is sent directly. Pinned by
  `MoveItemToCursorPrecedesTheMemorizeSpell`.
- **One refusal table.** `EverQuest::ScribeRefusalMessage()` (eq_merchant.cpp)
  is shared by `/scribe`'s pre-flight and `ScribeSpellFromScroll()`'s
  last-line guard -- the T244 `castRefusalMessage()` idiom. The pre-flight
  matters: a refusal must happen BEFORE the scroll is moved onto the cursor.
- **Official strings.** Level -> eqstr_us.txt 12048 (eqemu
  `SPELL_LEVEL_TO_LOW`), wrong class -> 181 (`CANNOT_USE_ITEM`) -- the
  strings the server itself would have sent. 12048 was missing from
  `GetStringMessage()`'s built-in fallback table and was added.
- **Gate on the scroll's spell id, not `ItemInstance::isSpellScroll()`.**
  That helper also returns true for a bare `"Spell:"` name prefix with no
  effect; the server compares `Scroll.Effect` to the spell id, so an item
  without one can never be scribed.
- **The destination book slot is chosen, not typed.** The stock client picks
  it and a wrong slot silently overwrites a known spell, so
  `SpellManager::findFreeSpellbookSlot()` returns the first slot holding
  `SPELL_UNKNOWN` **or 0** (the PlayerProfile spellbook uses both sentinels
  -- eq.cpp's own scribed-count loop skips both).
- **`ResolveCarriedItem()` extracted.** T273's `/read` copied /equip's
  slot-or-name rule; `/scribe` would have been the third copy. It is one
  function in eq_commands.cpp now and `/read` calls it -- behaviour
  unchanged, `test_read_book` still 12/12.
- **No mouse gesture was wired, and here is why.** The inventory popup's
  click path in `static_ui_input.cpp` only sets `hoveredSlot`; its state
  (`InventoryPanelState` -> `SlotDisplayInfo`) carries `hasItem`,
  `itemName`, `iconId`, `quantity` and **no item id and no scroll effect**.
  A ctrl+click could not tell a scroll from a sword, let alone name the
  spell. Wiring it means feeding item identity into the panel -- new
  plumbing, not this unit's territory. The intent now has a producer either
  way, so it leaves `kBlockedIntents`; the UI leg stays open (T48 class).

### 4. Red / green (measured, each RED a real reversion + rebuild)

| RED probe | result |
|---|---|
| a. `/scribe` registration removed | `test_scribe_scroll` **16 of 21 FAIL** (every command test) |
| b. + the `pushIntent` removed (true pre-T276 state) | `test_reachability_sweep` FAIL: `NEW producer-less intents (not on the documented list): ScribeSpellIntent` |
| c. `scribing = 1` instead of 0 | FAIL `RequestBytesMatchMemorizeSpellStructWithScribingZero` (`u32(8)` is 1, want 0) |
| d. pre-T276 producer (nothing puts the scroll on the cursor) | **5 FAIL** -- no OP_MoveItem, and the arm's cursor guard then refuses |
| e. `consumeScribedScroll()` call removed | FAIL `EchoWithScribingZeroAddsTheSpellAndConsumesTheScroll` |
| f. `bookSlot` back to `uint8_t` | FAIL `SlotsBeyond255AreReachable` |
| **GREEN** | **21/21** |

A test-authoring trap worth recording: `FindScribeIntent(h.drainIntents())`
binds a const-ref parameter to a temporary and returns a pointer into it,
which dangles at the end of the full expression. It read plausible values
(5, then 6, then 5) and cost one debugging cycle. Every call site now binds
the vector to a named local, and the helper carries the warning.

A second one: the tests deliberately do **not** `#define private public`
around `spell_manager.h`. Making every member public can change member
ORDER relative to the library's own TUs, so poking `m_spellbook` directly
wrote into the wrong offsets. All spellbook fixtures go through the public
`setSpellbook()`/`scribeSpell()` API.

### 5. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --output-on-failure` | **3076/3077**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| Visual gate | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | **no NEW FAIL** -- `eq.cpp` 3930 -> 3940 (already over 3000 on fable) and `entity_renderer.cpp` 3340 (T274/T275's, unchanged); 8 pre-existing WARNs |


### 6. Live (nektulos, Testcaster = wizard lvl 1)

Two runs, both exit 0, full logs in `review/T276/`. **No DB staging was
needed**: Testcaster already carried `Spell: Minor Shielding` (item 15288,
`scrolleffect` 288) and `Spell: Blast of Cold` (15372, 372) in server
inventory slots 25 and 26, and its `character_spells` was empty.

**Run 1** (`live_run1_filtered.log`, script `t276_tour.txt`) -- surfaceless
EGL, `--script`, no display needed:

| step | observed |
|---|---|
| `/scribe Tattered Note` | `Tattered Note (slot 26) is not a spell scroll.` -- **no packet sent** |
| `/scribe Minor Shielding` | `InventoryManager: Set spell 288 from slot 24 for scribing` -> `Picked up 'Spell: Minor Shielding' from slot 24` -> `SendMoveItem called: fromSlot=24 toSlot=30` -> `Sending MemorizeSpell: spell=288 book_slot=0 scribing=0` -- **the move goes first, exactly as designed** |
| server reply | `MemorizeSpell response: slot=0 spell_id=288 scribing=0` -> `You have learned Minor Shielding!` -> `Successfully scribed spell Minor Shielding (288) to slot 0` |
| scroll | `scroll 'Spell: Minor Shielding' consumed by the server after scribing`, then the server's own `MoveItem: from=30 to=-1` arrives |
| `/scribe Minor Shielding` again | `No carried item matches 'Minor Shielding'.` -- **proof the cursor really was cleared**; `findItemsByNameSubstring` searches the cursor too |
| `/scribe Blast of Cold` | `Sending MemorizeSpell: spell=372 book_slot=1 scribing=0` -> `slot=1 spell_id=372 scribing=0` -> `You have learned Blast of Cold!` -- **book slot 1, the next free one** |
| `/mem 1 Minor Shielding` | `Starting memorization of 'Minor Shielding' in gem 1` -> `Memorized spell 'Minor Shielding' in gem 1` |
| `/cast 1` | `Casting Spell #288...` -> `beginCastFromGem: gem=1 state=1` -> `Casting complete.` |

**Server DB, before -> after** (`db_before.txt` / `db_after_run1.txt`):

| table | before | after |
|---|---|---|
| `character_spells` (id 67) | **empty** | `(67, 0, 288)`, `(67, 1, 372)` |
| `inventory` (id 67) | slots 25 = 15288, 26 = 15372 | both rows **gone** (server consumed the scrolls) |
| `character_memmed_spells` | `(0,54) (1,54)` | `(0,288) (1,54)` (the `/mem`) |
| `character_data` 65 + 67 | zone 36/-77.9232/21.9086/3/190 and 25/-259/-1201/-5.75/0 | unchanged |

**Run 2** (`live_run2_filtered.log`, `t276_drive_run2.sh`) -- Xvfb + `--egl-x11`
+ xdotool `ctrl+b`, purely to photograph the result:
`t276_2_spellbook_scribed_1024x768.png` shows the spellbook popup listing
**`L1 Minor Shielding`** and **`L1 Blast of Cold`** -- the two spells `/scribe`
put there -- with gem A1 holding `Mino` from run 1's `/mem`.

**Restore.** `character_spells` rows deleted, `character_memmed_spells` slot 0
back to 54, both `inventory` rows re-inserted with their original
`item_id`/`charges`/`color`/`instnodrop`. `db_restored.txt` diffs
**byte-identical** against `db_before.txt`. Testchar (65) was never touched.
Lock taken atomically (`set -o noclobber`), released and deleted after a
clean `down.sh`.

### 7. Deviations

1. **The plan's premise that eqemu "uses the SAME opcode with a different
   `scribing` field -- verify" resolved to `scribing = 0`, not 1.** T272's
   live `scribing=1` was the memorize echo. Verified in `zone/client.h`, and
   pinned both ways (`RequestBytesMatchMemorizeSpellStructWithScribingZero`
   and `AMemorizeEchoDoesNotScribeAnything`).
2. **The unit is wider than "add a producer", and deliberately so.** The
   cursor precondition (item 2 above) meant the existing consumer arm could
   never have worked; a producer alone would have shipped a `/scribe` that
   always got "Scribing a spell without an Item Instance on your cursor?".
   The `uint8_t` narrowing and the un-consumed cursor scroll are in the same
   class. All four are inside the plan's named territory ("the
   `ScribeSpellIntent` arm and its handler hop"), flagged here rather than
   absorbed.
3. **Scope item 3 (mouse) is reported, not done** -- the plan's own
   escape hatch ("If no gesture exists, say so"). Reason in section 3.
4. **`ResolveCarriedItem()` touches `/read` (T273, this same slot's previous
   unit).** Deduplication, not behaviour change; `test_read_book` is still
   12/12 and is in the evidence directory as `green_read_book.txt`.
5. **Two test-authoring traps cost real debugging time** and are recorded in
   section 4 because they will recur: a dangling pointer into a temporary
   vector, and `#define private public` changing member order relative to
   the library.
6. **`include/client/graphics/ui/CLAUDE.md` was NOT edited** -- same call
   T273 made, and for the same reason (parallel units in the other slots own
   that file's neighbourhood). `/scribe` is discoverable through its
   registered help text (`/help`, category Inventory). Worth one
   consolidated doc line when these units merge.
