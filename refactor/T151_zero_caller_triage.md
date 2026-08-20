# T151 — Triage the remaining eleven zero-caller senders

**Slot: slot1. x86. No eqemu lock needed (source verification). No Pi.**

**This is a DECISION unit, not a wiring unit.** Its deliverable is a
per-sender recommendation with evidence, committed to this file's Review, that
Will can act on. **Do NOT wire any command. Do NOT delete any sender.** Both
are follow-up work whose shape this unit decides.

**Territory: `refactor/` only, plus `tests/` if and only if you add a test that
records a finding. No production code changes.** If you believe a fix is
urgent, write it up — do not apply it.

Read, in order: the T146, T147, T148, T149 and T150 sections of
`refactor/deferred.md` (this arc's accumulated findings, including the
orchestrator's T150 correction about the skill system), then T148's Review (the
strongest evidence standard in the arc, and the source of the framing below).

## The framing that must drive every recommendation

**A zero-caller sender is not "correct but unreached". It is UNVERIFIED.**
Nothing has ever exercised it, so nothing has ever contradicted it. Measured
rate in this arc: **T148 found four defects in nine senders; T149 and T150 each
found one more.** Roughly **one defect per two senders.**

So "wire it" is never the default answer, and neither is "delete it". For each
of the eleven the question is: *is there a working feature here at all, and is
it worth the unit it would cost?*

**"Delete" is a live and respectable outcome.** A dead sender nobody wants is
the same trap class T147 spent a whole unit removing from `packet_structs.h` —
it looks authoritative, it compiles, and the next reader assumes it works.

## The eleven

Verified on tip `9e6f59f0`: 11 of 97 sender-shaped `EverQuest::Send*` methods
have zero production callers (down from 28 before T148/T149/T150).

| # | sender | file | opcode | grouping |
|---|---|---|---|---|
| 1 | `SendApplyPoison` | `eq_player_info.cpp:698` | `HC_OP_ApplyPoison` | misc player |
| 2 | `SendBoardBoat` | `eq_player_info.cpp:1366` | `HC_OP_BoardBoat` | boats |
| 3 | `SendLeaveBoat` | `eq_player_info.cpp` | `HC_OP_LeaveBoat` | boats |
| 4 | `SendControlBoat` | `eq_player_info.cpp:1406` | `HC_OP_ControlBoat` | boats |
| 5 | `SendConsiderCorpse` | `eq_player_info.cpp` | `HC_OP_ConsiderCorpse` | corpse |
| 6 | `SendConfirmDelete` | `eq_player_info.cpp` | `HC_OP_ConfirmDelete` | corpse |
| 7 | `SendPetitionDelete` | `eq_player_info.cpp` | `HC_OP_PetitionDelete` | petition |
| 8 | `SendFaceChange` | `eq_player_info.cpp:1842` | `HC_OP_FaceChange` | misc player |
| 9 | `SendPopupResponse` | `eq_player_info.cpp:1662` | `HC_OP_PopupResponse` | popup |
| 10 | `SendRaidInvite` | `eq_group.cpp` | `HC_OP_RaidInvite` | raid |
| 11 | `SendSaveOnZoneReq` | `eq_player_info.cpp` | `HC_OP_SaveOnZoneReq` | zone |

## Three questions already known to need answering — do not assume

### (a) `SendApplyPoison` may be the CORRECT one and the live path the broken one

`HC_OP_ApplyPoison` has **two** send routes: this dead sender
(`eq_player_info.cpp:698`), and the **live, hotbar-reachable** skill path —
`SkillManager::getSkillOpcode()` maps skill 6 to it
(`src/client/skill/skill_manager.cpp:447`) and `sendSkillUsePacket()` sends it
with a **zero-length body** (`:432`).

The obvious reading is "delete the duplicate". **Check that reading before
adopting it.** If `Handle_OP_ApplyPoison` needs the poison item slot and the
skill path sends an empty body, then **the live path is the broken one and the
dead sender is correct** — and the right answer is to route the skill path
through `SendApplyPoison`, not to delete it. Read eqemu's handler and settle it.

**Then generalise the check to all eleven.** The zero-caller sweep that produced
this list cannot see the skill system, because it routes to `QueuePacket`
without any named `Send*` method. Cross-check every one of the eleven against
`getSkillOpcode()` and against a plain grep for its opcode constant.

### (b) `SendSaveOnZoneReq` probably wants automatic wiring, not a command

It sends a pre-zone save. That is not something a user types — it is something
the zone-change path should do. **If the correct answer is "call it from the
zoning sequence", say so**; it is a different and smaller job than registering a
command, and it does not belong in a command-wiring batch. Check whether the
server requires it, or whether zoning already saves server-side and the sender
is redundant.

### (c) Boats may be untestable here

T146 could not find a boat in the test zones. Before recommending a unit for
the three boat senders, establish whether a boat is reachable on this server at
all. **A recommendation to wire something that cannot be tested is not a
recommendation, it is a deferral** — label it as one.

Note `SendControlBoat`'s signature was changed to `SendControlBoat(bool)` by
T145 after finding `ControlBoat_Struct` was 20 bytes against the wire's 8.
That fix is **structurally applied and never wire-verified.**

## Method — per sender

1. **Verify it against eqemu source.** Its struct, its size, and the handler's
   gate (`DECODE_LENGTH_EXACT`, an `app->size !=` check, or nothing). Use the
   arbiter rule: if the opcode has an `ENCODE`/`DECODE` in
   `common/patches/titanium.cpp`, `Titanium::structs::` is the wire format;
   otherwise the base `eq_packet_structs.h` struct goes raw. **Expect to find
   defects — the rate says roughly five of these eleven are broken.**
2. **Establish whether the server-side feature exists and works** on this
   eqemu, and whether Titanium-era clients used it.
3. **Check for a second route** (see (a)) and for an existing command that
   overlaps or collides — `/face` already means the movement face,
   `/assist` already means single-target assist. Both cost earlier units real
   time.
4. **Recommend one of: WIRE / FIX-THEN-WIRE / AUTO-CALL / DEFER / DELETE**, with
   a one-line cost estimate and the evidence behind it.

## Deliverable

Fill this plan's Review with:

- A **per-sender table**: sender, opcode, struct verdict vs eqemu, second route
  y/n, collision y/n, recommendation, rough cost.
- For every **DELETE**: what would be lost, and confirmation nothing references
  it — deletion recommendations must be as evidenced as wiring ones.
- For every **FIX-THEN-WIRE**: the defect, and whether it is wire-observable or
  needs a capture.
- A **proposed batching** of whatever survives into units, in priority order,
  with the boats question resolved either way.
- An explicit list of anything **only a capture can settle** — do not guess, and
  do not take the eqemu lock to chase one; record it.

**Recommend, do not hedge.** A table of "possibly, depending" is not a decision
aid. Where you are genuinely uncertain, say what would settle it and how cheap
that is.

## Verification

Build + ctest to prove the tree is untouched (this unit should change no
production code). Full bar is not required for a documentation-only unit — say
which steps you ran and why the others do not apply.

## STOP conditions

- A finding that some sender is actively harmful if ever called — flag it loudly
  and immediately; do not bury it in a table row.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T151-zero-caller-triage`,
off `fable` tip `eeb6f6be`. **No eqemu lock taken, no server started, no production
code changed.** Method throughout: source verification against
`/mnt/projects/claude/EQEmu/` plus grep over this tree.

### STOP CONDITION HIT — two senders are actively harmful if ever called. Read this first.

**1. `SendControlBoat` can get the player's account flagged for hacking.**
`Handle_OP_ControlBoat` (`zone/client_packet.cpp:5421`) resolves `boatId` through
`entity_list.GetMob()`. If that id resolves to *any* mob that is not a controllable
boat, it does **not** bail quietly — it writes a player-event log entry:

```
RecordPlayerEventLog(PlayerEvent::POSSIBLE_HACK,
    PlayerEvent::PossibleHackEvent{.message = "OP_Control Boat was sent against {} which is of race {}"});
```

`m_boat_id` is set **only** by `SendBoardBoat`, which is itself broken (finding 2
below) — so the id it carries is whatever the eventual caller passed, and any
caller that guesses or reuses a spawn id sends a hack report to the server's
player-event log against the account. A wiring unit that "just registers
`/controlboat`" gets the tester flagged. **Do not wire `SendControlBoat` before
the boat entity model exists.**

**2. `SendConfirmDelete` lies to the user about an effect that cannot happen.**
`Handle_OP_ConfirmDelete` (`:5087`) is, in full, `{ return; }` — a no-op on this
eqemu. Our sender nevertheless prints
`"Your corpse has been summoned to your bind point."` unconditionally on queue.
That message is wrong twice over: nothing happened server-side, and corpse
summoning is not what this opcode ever did. Same class as
`SendBoardBoat`'s `"You board the boat."` (finding 2). Not dangerous to the
server; dangerous to a tester's trust in the client.

### Answers to the three questions the plan named

**(a) `SendApplyPoison` is the CORRECT route. The live hotbar skill path is the
broken one.** Settled from source, no capture needed.

* `titanium.cpp:2475` `DECODE(OP_ApplyPoison)` uses
  `DECODE_LENGTH_EXACT(structs::ApplyPoison_Struct)`. The struct is
  `uint32 inventorySlot; uint32 success;` — **8 bytes**, identical in
  `titanium_structs.h:3327` and `eq_packet_structs.h:5272`.
* `Handle_OP_ApplyPoison` (`:3016`) then re-checks
  `app->size != sizeof(ApplyPoison_Struct)` and immediately uses
  `GetInv().GetItem(ApplyPoisonData->inventorySlot)`. The slot is the entire
  point of the packet — without it there is no poison to apply.
* `SkillManager::sendSkillUsePacket()` (`skill_manager.cpp:432`) does
  `packet.Resize(0)` for every non-`CombatAbility` skill, and
  `getSkillOpcode()` maps skill 6 → `HC_OP_ApplyPoison` (`:447`). A zero-length
  body fails `DECODE_LENGTH_EXACT` before the handler is ever reached; the
  opcode is rewritten to `OP_Unknown` and dropped.
* `SendApplyPoison` (`eq_player_info.cpp:685`) writes exactly the 8 bytes the
  handler wants, at the right offsets, and `ZoneProcessApplyPoison` (`:705`)
  already parses the server's echo.

**So the dead sender is right and the reachable path is dead on arrival.** The
correct fix is to route skill 6 through `SendApplyPoison` with the real
inventory slot — *not* to delete the duplicate. This is the arc's inversion
case: the thing that looked like duplicated-then-drifted is the only correct
copy.

**Generalised second-route check — done for all eleven.** `getSkillOpcode()`
(`skill_manager.cpp:442-467`) maps skills 6, 9, 16, 17, 25, 27, 29, 32, 40, 42,
48, 53, 55, 62, 67, 71, 73. Of our eleven opcodes **only `HC_OP_ApplyPoison`
appears**. A plain grep for every one of the eleven `HC_OP_*` constants across
`src/`, `include/`, `tests/`, `tools/` (excluding the `eq_strings.cpp` name
table) returns, for the other ten, exactly two hits each: the enum declaration
in `eq.h` and the `PutUInt16` inside the dead sender. **There is no third route
anywhere.** All eleven opcode *values* were also checked against
`utils/patches/patch_Titanium.conf` and all eleven are correct.

**(b) `SendSaveOnZoneReq` should be DELETED, not auto-called.** The plan
guessed auto-wiring; the source says the packet is redundant.

* `Handle_OP_SaveOnZoneReq` (`:13668`) is a one-line forward to
  `Handle_OP_Save`, which is `Save(); return;`.
* The zone-change path **already saves, unconditionally, before the handoff**:
  `zoning.cpp:533` calls `SaveCharacterData()` after copying position/heading/
  zone into `m_pp`, in `Client::ZonePC`, on every zone.
* Disconnect also saves (`client.cpp:783`, `Save(2)`).
* A manual save is *already reachable* from this client: `/save` is registered
  (`command_processor.cpp:572`) and routes to `SendSave`.

So auto-calling it from our zoning sequence would send a packet whose only
effect duplicates work the server does anyway one function later, and wiring it
to a command duplicates `/save`. Nothing is lost by deleting it.

**(c) Boats: DEFER, and the reason is not "no boat to test on".** The reachability
question turns out not to be the binding constraint, so it did not need the DB.

`Mob::IsControllableBoat()` (`mob.cpp:7036`) is true only for race `Boat` (141)
and `Rowboat` (502) — the small player-steered boats, **not** the classic transit
ships (`PirateShip` 551, `GhostShip2` 552, `Boat2`), which are `IsBoat()` and are
ridden as moving platforms with no opcode exchange at all. And the steering
channel is not `OP_ControlBoat`: `Handle_OP_ClientUpdate` (`:4857`) accepts an
`OP_ClientUpdate` whose `spawn_id` is the *boat's* entity id and moves the boat
from it. So a working boat needs, in this client: a controllable-boat entity
class, a "we are riding entity N" camera/physics attachment, and a position-update
path that emits updates on behalf of a foreign spawn id. **Three senders are the
smallest part of that feature.** Whether a rowboat spawns in a zone we can load is
a real question too — it needs a DB read (`spawn2`/`npc_types` for race 141/502)
that this unit was not authorised to make — but it is moot: even standing next to
one we could not board it, because `SendBoardBoat` is broken by design (below) and
there is no ride model.

**This is a deferral, labelled as such.** It is not a recommendation to wire.

### Per-sender table

Struct verdict is against eqemu source using T145's arbiter rule (an
`ENCODE`/`DECODE` in `common/patches/titanium.cpp` makes `Titanium::structs::`
authoritative; otherwise the base `eq_packet_structs.h` struct goes raw). Both
eqemu headers are `#pragma pack(1)`, so all sizes below are byte-exact.

| # | sender | opcode | struct verdict vs eqemu | 2nd route | collision | recommendation | cost |
|---|---|---|---|---|---|---|---|
| 1 | `SendApplyPoison` | `0x0c2c` | **CORRECT** — 8 bytes, `DECODE_LENGTH_EXACT(8)`, offsets match | **YES** — skill 6, and **the second route is the broken one** | none (no `/poison`) | **FIX-THEN-WIRE** — fix `sendSkillUsePacket` to call this with the slot | M |
| 2 | `SendBoardBoat` | `0x4298` | **WRONG — wrong data type entirely.** Handler wants a **boat name string**, `5 < size <= 64`; we send a 4-byte entity id | no | none | **DEFER** (boats) | — |
| 3 | `SendLeaveBoat` | `0x67c9` | **body ignored** — handler reads nothing, any size works | no | none | **DEFER** (boats) | — |
| 4 | `SendControlBoat` | `0x2c81` | **CORRECT** — 8 bytes (`uint32 boatId; bool; char[3]`), T145's fix verified against source | no | none | **DEFER** (boats) — ⚠ see STOP condition | — |
| 5 | `SendConsiderCorpse` | `0x773f` | **WRONG — 4 bytes sent, handler demands `sizeof(Consider_Struct)` = 28.** Silently dropped (`LogDebug`, no error) | no | `/consider` exists (living target) | **FIX-THEN-WIRE** — best value in the list | **S** |
| 6 | `SendConfirmDelete` | `0x3838` | **moot** — `Handle_OP_ConfirmDelete` is `{ return; }` | no | none | **DELETE** — ⚠ see STOP condition | S |
| 7 | `SendPetitionDelete` | `0x5692` | **WRONG — 4 bytes sent, handler demands `sizeof(PetitionUpdate_Struct)` = 184.** And that struct is the one T147 declared un-wire-credible (contains a `time_t`) | no | `/petition` exists (submit) | **DELETE** | S |
| 8 | `SendFaceChange` | `0x0f8e` | **CORRECT** — 7 bytes, matches `Titanium::structs::FaceChange_Struct`, `DECODE_LENGTH_EXACT` | no | **`/face` is taken** (movement face, `command_processor.cpp:324`) — T146 already burned on this | **WIRE** — the one clean wire in the list | **S** |
| 9 | `SendPopupResponse` | `0x3816` | **CORRECT** — 8 bytes, `popup_id` at offset 4 matching `PopupResponse_Struct.popupid` | no | none | **DELETE** — nothing to respond *to* | S |
| 10 | `SendRaidInvite` | `0x5891` | **WRONG — 64-byte bare name; handler is `Handle_OP_RaidCommand`, `DECODE_LENGTH_ATLEAST(structs::RaidGeneral_Struct)` = 136, dispatching on an `action` field we never send** | no | `/invite` is taken (group) | **FIX-THEN-WIRE**, but see the raid finding — it drags a receive-side rewrite with it | L |
| 11 | `SendSaveOnZoneReq` | `0x1540` | body ignored; handler forwards to `Handle_OP_Save` | no | `/save` exists | **DELETE** — server already saves on zone (`zoning.cpp:533`) | S |

**Tally: 5 broken of 11** (2, 5, 7, 10 broken outright; 1 correct-but-its-live-
partner-is-broken). **The arc's one-defect-per-two rate held exactly.** Two
correct-and-harmless (4, 8), one correct-but-pointless (9), two no-op-by-design
(3, 11), one no-op server-side (6).

### DELETE justifications — what would be lost

**`SendConfirmDelete` (#6).** Lost: nothing. `Handle_OP_ConfirmDelete` is an
empty `return;` on this eqemu, so no reachable behaviour exists to preserve at
any packet size. References: declaration `eq.h:1046`, definition
`eq_player_info.cpp:1253`, zero callers, no test, and `HC_OP_ConfirmDelete`
appears nowhere else but its enum entry. **Delete the false
`AddChatSystemMessage` with it.** If a future server implements the opcode, the
4-byte body would still be a guess needing a capture — restoring from git
history would restore the guess.

**`SendPetitionDelete` (#7).** Lost: the *idea* of a GM deleting a petition from
the client. Nothing else — there is no petition list receive path in this client
(`grep Petition src/client/*.cpp` finds only `SendPetition` (submit, live,
T135-verified) and this sender), so there is no petition id to pass. To be
correct it would have to send a 184-byte `PetitionUpdate_Struct`, and T147
already recorded that struct as **unresolvable without a capture** because
eqemu's version carries a `time_t`. So this is a sender that is wrong today,
cannot be made right from source, and has no id source and no consumer.
Cleanest delete in the set. Note for the record that `OP_PetitionResolve`
forwards to the same handler, so the shape is at least real — it is just not
recoverable from source.

**`SendPopupResponse` (#9).** Lost: the send half of a dialog exchange whose
receive half does not exist. `Mob::SetFaceAppearance`-style broadcast aside, the
server initiates popups with `OP_OnLevelMessage`, and **this client has no
handler, no opcode constant, and no popup UI** — `grep -i popup src/client`
returns only the *inventory/skills* UI popups, which are unrelated local
windows. There is therefore no popup id to respond with, ever. The struct itself
is correct (T145 fixed it) and that verification is preserved in
`packet_structs.h`'s comment; only the unreachable sender goes. **If popups are
ever implemented, this sender is four lines to rewrite** — that is what makes
deleting it cheap rather than lossy.

**`SendSaveOnZoneReq` (#11).** Lost: nothing — see (b). The server saves at
`zoning.cpp:533` before the zone handoff, saves again on disconnect, and `/save`
already exists for a manual save.

### FIX-THEN-WIRE defects — wire-observable, or capture-only?

**#1 `SendApplyPoison` / the skill path.** Defect: `sendSkillUsePacket` sends a
zero-length body for a packet that must be 8 bytes and must carry an inventory
slot. **Wire-observable**, no capture needed: today the server logs nothing at
all (the `DECODE_LENGTH_EXACT` failure drops the packet before
`Handle_OP_ApplyPoison`, so there is not even a "Wrong size" line — the *absence*
of any server-side trace is itself the evidence, and matches the T148 lesson that
silence proves nothing). After the fix the observable effects are strong and
external: the server **deletes the poison item from inventory** (`DeleteItemInInventory`,
which happens on success *and* failure), may print `POISON_TOO_HIGH`, and echoes
`OP_ApplyPoison` with `success`. Requires a Rogue with a poison item — the
handler no-ops for any other class. **This is the one leg whose acceptance needs a
character we may not have; check for a rogue test character before scoping it.**

**#5 `SendConsiderCorpse`.** Defect: 4 bytes where 28 are required. **Wire-observable
and cheap to fix** — the correct body already exists and is proven live in this
client: `CombatManager::ConsiderTarget()` (`combat.cpp:355`) builds exactly the
28-byte `Consider_Struct` for `HC_OP_Consider`. The corpse variant needs the same
body with the corpse's entity id in `targetid`. Observable effect is a chat line
the server generates and we already display: `CORPSE_DECAY_TIME_MINUTE` /
`CORPSE_DECAY_NOW` for an NPC corpse, rez-timer strings for a player corpse.
No capture needed.

**#10 `SendRaidInvite`.** Defect: sends a bare 64-byte name; the wire format is
`Titanium::structs::RaidGeneral_Struct` — 136 bytes, `uint32 action` @0,
`char player_name[64]` @4, `char leader_name[64]` @68, `uint32 parameter` @132 —
with `action = RaidCommandInvite = 3` (`common/raid.h:52`). Note the **base**
`RaidGeneral_Struct` is 140 bytes and has an extra `unknown1` at offset 68;
`titanium.cpp:3072` has `DECODE(OP_RaidInvite)`, so per the arbiter rule the
136-byte Titanium layout is the wire format and the base layout must not be
copied. **Wire-observable**: on a valid invite the server sends the invitee an
`OP_RaidUpdate` with `action = 20` (`client_packet.cpp:12356-12365`), so a second
client proves it at the T148 bar.

### Bonus defect found on the receive side — `ZoneProcessRaidUpdate` decodes four of six actions wrongly

Not one of the eleven, found while establishing whether #10 has an observable
effect. `eq_entities.cpp:1288` carries an invented action table. Against
`common/raid.h:21-44`:

| action | our client says | eqemu actually means |
|---|---|---|
| 0 | "You have joined the raid." | `raidAdd` — *a* member was added |
| 1 | member left | `raidRemove2` — near enough |
| 2 | member joined | `raidMemberNameChange` — **wrong** |
| 3 | "The raid leader has changed." | `raidRemove1` — **wrong** |
| 6 | "The raid has disbanded." | `raidMembers` (member list, len 395+) — **wrong**; disband is 5 |
| 10 | "raid lock state update" (ignored) | `raidNoRaid` — **wrong**; lock is 17 |

And **20 — the one action an invite actually produces — falls through to
`default: "Unknown raid action"`.** So even after #10 is fixed, the invitee shows
the user nothing. Fixing the sender without this table produces a wire success
and a user-visible nothing.

### Also worth recording

* **`SendBoardBoat` is not a size bug, it is a type bug.** `Handle_OP_BoardBoat`
  (`:4133`) `memcpy`s the body as a **name** and does `entity_list.GetMob(boatname)`.
  Our `SendBoardBoat(uint32_t boat_id)` has the wrong *signature*. Anyone
  "fixing the size" by padding to 8 bytes makes it worse. It also sets
  `m_is_on_boat = true` and prints `"You board the boat."` before any server
  acknowledgement — a second false-confirmation message, and the state it
  corrupts is the gate that `SendLeaveBoat`/`SendControlBoat` read.
* **`SendFaceChange`'s effect cannot be seen by a second client on Titanium.**
  `Handle_OP_FaceChange` (`:6387`) calls `SetFaceAppearance(*fc, true)`, which
  broadcasts `OP_SetFace` (`mob.cpp:3791`). **`OP_SetFace` has no entry in
  `utils/patches/patch_Titanium.conf`** — it has an `ENCODE` (`titanium.cpp:1916`)
  but no opcode number, so it cannot be delivered to a Titanium client. The
  observable effects that *do* exist are still two independent authorities:
  the server's `FACE_ACCEPTED` message to the sender, and `Save()` writing
  `haircolor`/`hairstyle`/`face`/`beard` to `character_data` — the T144
  outside-the-client authority. That is enough for acceptance; plan for it
  rather than expecting a visual on a second client.
* **`/face` collision, again.** T146 already lost time to this: `/face` is the
  movement face (`command_processor.cpp:324`). Name the appearance command
  something else — `/appearance` reads naturally and collides with nothing in
  either registry.

### Proposed batching, in priority order

**Unit A — "the two cheap correct ones" (S).** `SendConsiderCorpse` fixed to a
28-byte `Consider_Struct` and folded into the existing `/consider` (route to the
corpse opcode when the target is a corpse — the client already distinguishes
corpses, `application.cpp:1623`), plus `/appearance` wired to `SendFaceChange`.
Both have observable effects with no new subsystem; both structs are settled
from source. Highest value per hour in the whole list.

**Unit B — "the deletes" (S).** Remove `SendConfirmDelete`, `SendPetitionDelete`,
`SendPopupResponse`, `SendSaveOnZoneReq` and their `eq.h` declarations. Same trap
class T147 cleared from `packet_structs.h`. Do it in one commit with the
justifications above quoted into it, so the next reader sees why rather than
re-deriving. Leave the *structs* alone — `PopupResponse_Struct`'s comment records
a real T145 verification worth keeping.

**Unit C — "poison" (M).** Route `SkillManager` skill 6 through `SendApplyPoison`
with a real inventory slot. This is a fix to a **live** path, not a wiring job,
so it ranks above the raid work. Gate: needs a Rogue character with a poison
item. Worth checking whether other zero-length skill sends have the same defect —
`sendSkillUsePacket` sends `Resize(0)` for **sixteen** opcodes, and this unit
verified only one of them. **That audit is a candidate unit in its own right and
is probably worth more than anything else on this page.**

**Unit D — "raid" (L).** `SendRaidInvite` rebuilt to the 136-byte Titanium
`RaidGeneral_Struct` with `action = 3`, **plus** the `ZoneProcessRaidUpdate`
action table corrected against `common/raid.h`, **plus** an accept path
(`action = RaidCommandAcceptInvite = 1`) — without accept, an invite can be sent
and seen but never completed. Two clients required. This is a feature unit, not
a wiring unit; scope it as one.

**Deferred indefinitely — boats.** `SendBoardBoat`, `SendLeaveBoat`,
`SendControlBoat`. Not scheduled. If they are ever picked up, the unit starts
with the entity/ride model, not with the senders, and `SendBoardBoat`'s signature
changes from `uint32_t` to `const std::string&`. **Until then, leave a comment on
`SendControlBoat` recording the `POSSIBLE_HACK` hazard** so nobody wires it
casually — that comment is worth more than the three senders are.

### What only a live capture (or a DB read) can settle

Recorded, not chased. Nothing above depends on any of these.

1. **Whether any zone we can load spawns a controllable boat** (race 141 or 502).
   Needs a `spawn2`/`npc_types` DB read. Moot while boats are deferred.
2. **Whether the `OP_ApplyPoison` echo's `success` field is what we parse.**
   `ZoneProcessApplyPoison` reads it at offset 6 and the struct says it belongs
   there; the round trip has never been observed. Closes for free the first time
   Unit C runs — no separate capture needed.
3. **Whether `PetitionUpdate_Struct` is 184 bytes on this server's wire.**
   T147's open item, unchanged. Only matters if petitions are ever implemented;
   deleting `SendPetitionDelete` does not depend on it.
4. **Whether `OP_SetFace` is genuinely undeliverable to Titanium**, or merely
   unmapped in the copy of `patch_Titanium.conf` under
   `/mnt/projects/claude/EQEmu/`. Confirmed absent from the conf; a second client
   in Unit A would show it either way as a side effect, so no dedicated capture.

### Verification

Documentation-only unit; the plan narrows the bar to "prove the tree is untouched".

| step | result |
|---|---|
| `git status --porcelain` before building | **empty** — no production file touched at any point |
| 1. `cmake --build build -j8` | **PASS** (`-j8` used directly; T148 recorded `-j24` discovery timeouts in this slot) |
| 2. `ctest --output-on-failure` | **2524/2525**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `scripts/build-gles2-x86.sh` | **not run** — no source changed, so it can only re-prove step 1 |
| 4. Visual gate | **not run** — no renderer, shader, asset or golden touched; `git status tests/goldens` empty |
| 5. `scripts/build-arm-noble.sh` | **not run** — no source changed; there is no arch-specific surface in a Markdown file |
| 6. `check_file_sizes.sh fable` | **not run** — the only changed file is `refactor/T151_zero_caller_triage.md`, which the script does not police |

### Deviations

1. **The plan's guess on (b) was wrong and is contradicted here.** It expected
   `SendSaveOnZoneReq` to want automatic wiring from the zoning sequence; the
   server already saves at `zoning.cpp:533`, so the recommendation is DELETE.
   Flagged rather than quietly answered.
2. **Boats were resolved without the reachability question.** The plan asked
   whether a boat exists on this server; that needs a DB read this unit was not
   authorised to make, and it turned out not to be the binding constraint —
   the missing ride/steering model is. The DB question is recorded above as
   unsettled rather than guessed.
3. **One finding outside the eleven** — `ZoneProcessRaidUpdate`'s action table —
   is reported because Unit D is unscopeable without it.
4. **A second, larger finding outside the eleven** is flagged in Unit C: the
   zero-length-body defect proven for skill 6 is structural to
   `sendSkillUsePacket`, which sends `Resize(0)` for sixteen opcodes. Only
   `OP_ApplyPoison` was verified here. The other fifteen are unaudited and are
   exactly the same UNVERIFIED class as the eleven — reachable, but never
   contradicted.
