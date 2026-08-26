# T277 — click off a buff (T264 F6)

Slot: slot1. x86. OWNS eqemu (noclobber) for a live click-off. Class A.
Territory: the buff-removal intent/handler T264 KEPT (its Review names
them; delist in `tests/test_reachability_sweep.cpp` when wired), the buff
bar panel's click path (static_panels / static_ui_input — T272 added the
spellbook click, follow that shape), the OP_BuffRemoveRequest sender,
CommandProcessor for a scriptable form, tests. Do NOT touch ui_renderer
(T275 slot2), spell scribe (T276 slot3).

## Gap
The buff bar draws buffs; nothing can remove one. eqemu:
`Handle_OP_BuffRemoveRequest` (zone/client_packet.cpp) with
`BuffRemoveRequest_Struct {slot_id, entity_id}` — verify, including which
buffs the server refuses to drop (song/detrimental/lore).

## Scope
1. Command first (T156 rule — a click is not scriptable): `/removebuff
   <slot|name>` resolving through the buff model the bar already reads.
2. Click: buff-bar rect click → the same handler body (T230/T239 one-body
   rule, T272's click plumbing). Note if the bar has no hit-test yet.
3. Refuse locally only what the client can know (empty slot, out of range);
   let the server refuse the rest.
4. Tests: request bytes for a given slot; the buff leaves the model on the
   server's confirmation, NOT optimistically. Red/green.
5. Live: Testcaster self-buffs (Minor Shielding from T272's staging),
   `/removebuff 0` → server drops it, buff bar empties, observer sees the
   appearance change if one is sent. Restore rows.

## Verification
Full ctest; visual gate if the panel changed; Review; evidence
review/T277/. Commit: `T277: buffs can be clicked off`.

---

## Review (executor, 2026-08-26, slot1, branch task/T277-buff-click-off off fable aa2bd783)

**Done. Evidence: `/mnt/projects/fable/review/T277/` (README.md there).**

### The wire truth -- the plan named the wrong opcode

The plan said `Handle_OP_BuffRemoveRequest` with
`BuffRemoveRequest_Struct {slot_id, entity_id}`. Checked against eqemu
rather than taken on trust (T273's rule), and it is **Underfoot and later**:

- `common/eq_packet_structs.h:559` carries the comment
  `// Underfoot & later struct.` immediately above `BuffRemoveRequest_Struct`.
- `D(OP_BuffRemoveRequest)` appears only in `rof_ops.h`, `rof2_ops.h`,
  `uf_ops.h`. `utils/patches/patch_Titanium.conf` has no such line at all,
  so a Titanium client sending it would be sending an unmapped opcode.
- `Handle_OP_BuffRemoveRequest`'s own first comment says it is for SoD pet
  buffs and Underfoot client+pet buffs.

**Titanium clicks a buff off with `OP_Buff` (0x6a53)** carrying the 32-byte
Titanium `SpellBuffPacket_Struct` (4 bytes shorter than the emu one --
Titanium's `SpellBuff_Struct` stops at `player_id`). `Client::Handle_OP_Buff`
(`zone/client_packet.cpp:4181`) reads exactly **one** field, `buff.spellid`:

| condition | server behaviour |
|---|---|
| `spid == 0xFFFF` or `spells[spid].good_effect == 0` | `QueuePacket(app)` -- our own bytes ECHOED BACK |
| otherwise | `BuffFadeBySpellID(spid)` -> `MakeBuffFadePacket` -> `OP_Buff` with `bufffade == 1` |

`entityid`, `slotid`, `bufffade` on the request are never read. Also worth
recording: the Titanium path does **not** check `spells[].no_remove` --
that guard exists only on the Underfoot+ `OP_BuffRemoveRequest` path. On
Titanium the whole refusal rule is "detrimental buffs stay".

**`bufffade` is 0 on the request, and that is the single most load-bearing
byte in the packet.** A refusal replays our own bytes into
`ZoneProcessBuff()`, which deletes a buff when it sees `bufffade == 1`. Had
the request carried 1, every refusal would have looked identical to a
success and silently desynced the bar from the server.
`BuffCancelPacket.BuffFadeIsZeroOnTheRequest` pins it; the live run then
observed the echo arriving with `fade=0` exactly as predicted.

### Scope, item by item

1. **Command first.** `/removebuff <slot#|spell name>` (alias `cancelbuff`),
   registered in `CommandProcessor` under "Spells" ->
   `ActionDispatcher::cancelBuff` -> `IActionHandler::cancelBuff` ->
   `EverQuest::RequestCancelBuff()`. The argument is passed through
   **unparsed**: slot-vs-name resolution needs the live buff model, which
   lives on the game side (`BuffManager`); `GameState` has no buff list, so
   a range check in `CommandProcessor` would only be a second, weaker copy
   of the one `RequestCancelBuff()` has to do anyway. Exact name match wins
   outright; otherwise a case-insensitive substring, with multi-match
   disambiguation (first five, each with its slot number).
2. **Click.** `hitTestBuffBar()` (new free function in `static_panels.cpp`,
   the `resolveMemorizeGem()` shape) + a hit-test block in
   `static_ui_input.cpp` pushing `BuffCancelIntent{slot}`.
   **The bar had NO hit-test before this unit** -- `renderBuffBar()` drew
   icons and nothing tested them. It is also **packed** (empty slots are
   skipped), so the on-screen column is not the slot; the hit-test walks the
   slots in the renderer's exact order and shares its `kBuffIcon`/`kBuffPad`
   constants, which were hoisted to file scope for the purpose.
   `processInput()` gained a `const BuffBarState&` parameter.
3. **Local refusals** are only what the client can know: slot >= 25, a
   1-based 0, an empty slot, an unmatched name, an empty argument. Nothing
   else -- detrimental and server-pinned buffs are the server's call and are
   sent so it can say no.
4. **Not optimistic.** The `BuffCancelIntent` arm no longer touches
   `BuffManager`; the buff leaves the model only on the server's
   `bufffade == 1`.

### The consumer arm was a live defect, like T272's

`eq_bridge.cpp`'s arm called `m_buff_manager->removeBuffBySlot(0, i.slot)`
and printed "Buff in slot N cancelled". `target_id == 0` **is** the player's
own list (checked -- the first draft of this Review guessed it was the
entity map and was wrong), so it really did erase the buff: locally,
optimistically, and **without sending a single byte**. The server kept the
buff ticking, the next `OP_Buff` or zone-in put it back, and it "succeeded"
on detrimental buffs the server refuses to drop. Nothing produced the
intent, so the line had never executed.
`review/T277/red_consumer_arm.txt` is that code failing the new consumer
tests (2 of 5).

### DEVIATION: a second gap the plan did not know about -- the buff bar could never fill

The first live attempt cast two buffs and the bar stayed empty. Root cause,
both halves:

- **eqemu never sends `OP_Buff` for a buff that is merely still running.**
  `Client::SendBuffDurationPacket` is called only when `buffs[i].UpdateClient`
  is set, which `Mob::AddBuff` does only for `level_override`/`hit_number`
  buffs or a duration extension, and `BuffProcess` only while buff timers are
  suspended. The real Titanium client learns about a newly applied buff from
  `OP_Action` plus its own local spell file; WillEQ's `SpellManager::handleAction`
  does not touch `BuffManager`.
- **The `PlayerProfile`'s 25 buff slots went into `BuffManager` and stopped
  there** -- nothing published `BuffUpdated`, so `BuffBarState` was never
  written and `renderBuffBar()` returned at its "no active buffs" guard
  every frame.

Net effect: **the buff bar was empty for the entire session, always**, and
the click gesture this unit delivers would have had nothing to hit. Fixed
minimally and in-territory (this is "the buff model the bar already reads"):
`ZoneProcessBuff()`'s publish body is extracted as
`EverQuest::PublishPlayerBuffUpdated()`, and the profile parse calls it for
every occupied slot via `PublishPlayerBuffs()` (which lives in
`eq_player_info.cpp`, deliberately, to keep `eq.cpp` from growing).
`BuffCancelConsumer.ProfileBuffsPublishTheSameEventTheBarConsumes` and
`.AnAppliedBuffReachesTheRendererAsBuffUpdated` pin both ends.

**NOT fixed, reported:** the OP_Action leg. A buff cast *during* a session
still does not reach `BuffManager` or the bar until a duration update, a
fade, or the next zone-in. That is the spell-landing path, not the buff-bar
click path, and it is a feature of its own.

### Red / green

| leg | red | green |
|---|---|---|
| consumer arm | `red_consumer_arm.txt` -- 2/5 fail, the buff is erased before the server is asked | 21/21 in `green_test_buff_click_off.txt` |
| click producer | `red_no_click_producer.txt` -- sweep: `NEW producer-less intents: BuffCancelIntent` | `green_reachability_sweep.txt`, 5/5 with it delisted |
| `/removebuff` | n/a (new command) | `green_command_processor_removebuff.txt`, 3/3 |

Honest limit: legs 1-3 (`hitTestBuffBar`, the click producer, the packet
builder) are new API and could not be compiled against the old tree, so
their red is "the symbol did not exist", not a measured failure. The two
that could be measured were.

### Live

Both directions proven, twice each, on a real server -- see
`review/T277/README.md` for the log excerpts and screenshots.

- `/removebuff 1` -> `OP_Buff` -> server `fade=1` -> buff gone from the
  client AND from `character_buffs` on the server.
- `/removebuff 2` (Weaken, detrimental) -> server echo with `fade=0` -> buff
  stays.
- Click at (16,360) -> `slot 0` -> Minor Shielding dropped.
- Click at the SAME pixel after the bar repacked -> `slot 1` -> Weaken,
  refused, icon still on screen. This is the packed-bar mapping working
  live, not just in the unit test.

`#castspell 288` on self reported success but applied no buff (only the
`effect_flag=0` action packet, `character_buffs` empty across a camp and a
cross-zone), so the two buffs were staged directly in `character_buffs` --
the table `ZoneDatabase::LoadBuffs` reads unconditionally on zone entry --
and deleted afterwards. The `#castspell` behaviour is an eqemu-side question
outside this unit; it is recorded here rather than chased.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest` | **3081/3082** -- sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | PASS |
| visual gate | **PASS**, goldens untouched. (First run failed on p95 CPU ceilings while two sibling slots were compiling on the same host; re-run quiet: PASS.) |
| `scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | 1 FAIL, `src/client/eq.cpp` 3934 lines -- **pre-existing** (3930 on `fable`; this unit adds 4). No new FAIL. |

eqemu lock taken atomically, released and deleted after a clean `down.sh`.
DB staged and restored (`db_before_and_staged.txt` / `db_restored.txt`).

