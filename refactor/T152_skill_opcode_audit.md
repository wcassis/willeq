# T152 — Audit all seventeen skill opcodes against their eqemu handlers

**Slot: slot1. x86. No eqemu lock needed (source verification). No Pi.**

**Territory: `src/client/skill/`, `include/client/skill/`, `tests/`,
`refactor/`. Do not touch `src/client/eq_player_info.cpp` or
`src/client/combat.cpp` beyond reading them** — if a fix reaches into either,
report the blast radius instead of following it.

Read the T151 Review first (`refactor/T151_zero_caller_triage.md`) — this unit
exists because of its closing finding — then the T146/T148 sections of
`refactor/deferred.md` for the standard of evidence this arc expects.

## The finding this unit chases

`SkillManager::sendSkillUsePacket()` (`src/client/skill/skill_manager.cpp:432`)
sends **`packet.Resize(0)`** — a zero-length body — for every skill with a
dedicated opcode. `getSkillOpcode()` (`:442`) maps **seventeen** skills to one:

`ApplyPoison`(6) `BindWound`(9) `Disarm`(16) `DisarmTraps`(17) `FeignDeath`(25)
`Forage`(27) `Hide`(29) `Mend`(32) `SenseHeading`(40) `Sneak`(42)
`PickPocket`(48) `Track`(53) `Fishing`(55) `SenseTraps`(62) `Begging`(67)
`InstillDoubt`(71) `Taunt`(73)

T151 verified exactly one — `ApplyPoison` — and found it **broken**: eqemu's
`DECODE(OP_ApplyPoison)` uses `DECODE_LENGTH_EXACT` and the handler needs
`inventorySlot`, so a zero-length packet is discarded **before the handler, with
no "Wrong size" line at all**. The `DECODE_LENGTH_EXACT` failure rewrites the
opcode to `OP_Unknown` silently. **A skill can be completely dead on the wire
while the client cheerfully reports using it and the server log says nothing.**

## Orchestrator's pre-scan — a STARTING POINT, NOT A FINDING

I ran a coarse scan of eqemu's handlers. **Treat it as a hypothesis to test, not
a result to confirm.** It looks only at the first ~12 lines of each handler and
can be wrong in both directions:

| gate found | opcodes |
|---|---|
| explicit size check | `ApplyPoison` (confirmed broken), `Disarm`, `PickPocket`, `Taunt` |
| no size check, no buffer reference in the opening lines | `DisarmTraps`, `FeignDeath`, `Forage`, `Mend`, `SenseHeading`, `Sneak`, `Track`, `Fishing`, `SenseTraps`, `InstillDoubt` |
| no size check but the handler DOES reference the buffer | `Hide`, `Begging` |
| handler not found under that name | `BindWound` — it is `Handle_OP_Bind_Wound` (underscore), `zone/client_packet.cpp:4013` |

**So the likely damage is roughly four or five of seventeen, NOT sixteen.** Many
EQ skills genuinely are empty-packet activations and a zero-length body is
correct for them. **Do not go looking for sixteen bugs.** Equally, do not assume
the ten "no gate" handlers are fine — a handler with no size check can still
read a field off an empty buffer, which is a different and worse defect than a
clean rejection.

## Method — per opcode, all seventeen

1. Find the real handler (mind `Handle_OP_Bind_Wound`'s underscore, and check
   for others).
2. Determine what the handler **actually requires**: an explicit size gate, a
   `DECODE`/`ENCODE` in `common/patches/titanium.cpp` (the arbiter rule — if one
   exists, `Titanium::structs::` is the wire format), or a bare read off
   `app->pBuffer`.
3. Decide: does a **zero-length body** reach the handler and do the right thing?
4. Classify: **CORRECT-AS-EMPTY** / **BROKEN-REJECTED** (gated, silently
   dropped) / **BROKEN-GARBAGE** (accepted but reads off an empty buffer) /
   **NEEDS-CAPTURE**.

**Also check every one for a second route.** T151 found `ApplyPoison` had a
correct dead sender competing with the broken live path. **`Taunt` is a known
second-route case**: `CombatManager::Taunt()` (`src/client/combat.cpp:739`)
sends `HC_OP_Taunt` **with** a body while skill 73 sends it empty, and eqemu
gates on `ClientTarget_Struct`. Work out which path is right and whether they
conflict. Look for the same shape elsewhere.

## The fix, for whatever turns out broken

Prefer **routing the skill path through an existing correct sender** over adding
a new one — that is T151's recommendation for `ApplyPoison` and it avoids the
duplicated-then-drifted defect this programme keeps hitting (T147 spent a whole
unit deleting the last generation of it). Where no correct sender exists, build
the body in `sendSkillUsePacket` from the struct eqemu actually requires.

**`SendApplyPoison` is a live, correct, zero-caller sender. Do not delete it —
route skill 6 through it.**

## Acceptance

**No eqemu lock, so nothing here is wire-verified** — this is a source audit
plus tests, and the Review must say so in those words. Do not write "skills work
now." The wire leg is a separate owed unit; enumerate the probes it should run,
one per opcode you changed.

Where a fix changes the bytes on the wire, add a serialization test asserting
the exact body length and content against the eqemu struct — `offsetof`-based,
per T147's `tests/captured_packets.h` convention, not a bare `sizeof`.

## Verification

Full protocol bar. Suite baseline 2524 at tip `4a73e3ab`, sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A fix requires changing `combat.cpp`'s or `eq_player_info.cpp`'s senders
  rather than the skill path — report the blast radius, do not follow it.
- An opcode where only a capture can settle whether an empty body is accepted —
  record it, do not take the lock.
- More than about six turn out broken: stop and report before fixing, because
  that would mean the pre-scan is badly wrong and the batch needs re-scoping.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T152-skill-opcode-audit`, off `fable` tip `c75349f3`. **No eqemu lock was
taken and no server was started.**

### NOTHING HERE IS WIRE-VERIFIED

**Nothing in this unit is wire-verified.** This is a source audit against
`/mnt/projects/claude/EQEmu/` plus unit tests. Skills are not proven to work;
five send paths that provably could not work now send the bytes the handler
reads, and that is the whole claim. The wire leg is enumerated below and is
owed.

### Headline: five of seventeen were broken, not sixteen — and two of the five
### are worse than "silently dropped"

The pre-scan's headline number was right (it predicted "four or five"; the
answer is five) but **its bucketing was wrong in three places**, and the two
mistakes that mattered both hid the more dangerous defect class.

**`Handle_OP_Bind_Wound` and `Handle_OP_PickPocket` log a size mismatch and then
DO NOT RETURN.** Both fall straight through to a cast of `app->pBuffer` and a
field read:

```
zone/client_packet.cpp:4013  Handle_OP_Bind_Wound
    if (app->size != sizeof(BindWound_Struct)) {
        LogError("Size mismatch for Bind wound packet");
        DumpPacket(app);           <-- no return
    }
    ...
    BindWound_Struct* bind_in = (BindWound_Struct*)app->pBuffer;
    Mob* bindmob = entity_list.GetMob(bind_in->to);      <-- reads the buffer
```

`common/base_packet.cpp:23-38` allocates `pBuffer` **only when `len > 0`**; for
a zero-length body it stays `nullptr`. `Client::HandlePacket`
(`client_packet.cpp:444`) has no size guard of any kind, and neither opcode has
a `DECODE` in `common/patches/titanium.cpp`, so nothing between the socket and
the handler can rescue it. On this reading **a zero-length `OP_Bind_Wound`
dereferences a null pointer inside the zone process.** `Handle_OP_Bind_Wound`
has no skill gate at all, so any character could trigger it; `PickPocket` is
behind `HasSkill(SkillPickPockets)` and a recovery timer, so it needs a rogue.

That is a client-triggerable server fault, not a dropped packet, and this
client's hotbar could send it (skill 9 → `sendSkillUsePacket` → `Resize(0)`).
It is fixed here by giving both opcodes their real body. **It is not proven by
observation** — see the wire probes below, and note that probe 2 is the one that
must be run carefully, on a server nobody else is using.

Where the pre-scan was wrong:

| pre-scan said | reality |
|---|---|
| `Hide`, `Begging` "no size check but the handler DOES reference the buffer" | **Both are fine.** `Handle_OP_Hide`'s buffer read is inside an `if (app->size == 4)` branch for post-Titanium clients that answer `OP_CancelSneakHide`; size 0 skips it and falls into the correct path. `Handle_OP_Begging` touches `app` not at all — it works off `GetTarget()`. |
| `PickPocket` "explicit size check" | The check exists but **does not return**, which is the BROKEN-GARBAGE case, not the clean-rejection case. |
| `BindWound` "handler not found under that name" | Found at `Handle_OP_Bind_Wound` (`:4013`) as the plan said — and it is the worst of the seventeen. |

The one thing the coarse scan could not have seen is exactly the thing that
mattered: whether the size check *returns*.

### Per-opcode table — all seventeen

Handlers are in `/mnt/projects/claude/EQEmu/zone/client_packet.cpp` unless
noted. Arbiter rule applied: `grep DECODE(OP_…) common/patches/titanium.cpp`
returns **`OP_ApplyPoison` only** among the seventeen (`OP_Track` has an
`ENCODE`, server→client, which does not gate our send). So for the other
sixteen the base `common/eq_packet_structs.h` struct goes on the wire raw.

| opcode | skill | handler | what it actually requires | verdict | second route | fix |
|---|---|---|---|---|---|---|
| `ApplyPoison` 0x0c2c | 6 | `:3016` | `DECODE_LENGTH_EXACT(structs::ApplyPoison_Struct)` = 8 at `titanium.cpp:2475`, **then** `app->size != sizeof(...)` again, then `GetInv().GetItem(inventorySlot)` | **BROKEN-REJECTED** | **YES** — `EverQuest::SendApplyPoison` (`eq_player_info.cpp:685`), correct, zero callers | **routed** through `SendApplyPoison` |
| `BindWound` 0x601d | 9 | `Handle_OP_Bind_Wound` `:4013` | `BindWound_Struct` = 8; size check **does not return**; reads `bind_in->to` | **BROKEN-GARBAGE** (null-`pBuffer` deref) | **YES** — `EverQuest::SendBindWound` (`eq_player_info.cpp:1790`), correct, live from `/bandage` | **routed** through `SendBindWound` |
| `Disarm` 0x17d9 | 16 | `:5749` | `Disarm_Struct` = 16, gated **with** return; needs `source`==own id, `target`, `skill`==server's Disarm value | **BROKEN-REJECTED** | no | **body built** (`MakeDisarmBody`) |
| `DisarmTraps` 0x1241 | 17 | `:5862` | nothing — timers, `FindNearbyTrap` | CORRECT-AS-EMPTY | no | none |
| `FeignDeath` 0x7489 | 25 | `:6415` | nothing | CORRECT-AS-EMPTY | no | none |
| `Forage` 0x4796 | 27 | `:6493` | nothing — timer, horse rule, `ForageItem()` | CORRECT-AS-EMPTY | no | none |
| `Hide` 0x4312 | 29 | `:8874` | nothing at size 0; the only `app` read is inside `if (app->size == 4)` | CORRECT-AS-EMPTY | no | none |
| `Mend` 0x14ef | 32 | `:10374` | nothing | CORRECT-AS-EMPTY | no | none |
| `SenseHeading` 0x05ac | 40 | `:13688` | nothing | CORRECT-AS-EMPTY | no | none |
| `Sneak` 0x74e1 | 42 | `:14587` | nothing | CORRECT-AS-EMPTY | no | none |
| `PickPocket` 0x2ad8 | 48 | `:11913` | `PickPocket_Struct` = 18; size check **does not return**; reads `pick_in->to` | **BROKEN-GARBAGE** (null-`pBuffer` deref, rogue-gated) | no | **body built** (`MakePickPocketBody`) |
| `Track` 0x5d11 | 53 | `:15266` | nothing — `MakeTrackPacket(this)` | CORRECT-AS-EMPTY | no | none |
| `Fishing` 0x0b36 | 55 | `:6472` | nothing | CORRECT-AS-EMPTY | no | none |
| `SenseTraps` 0x5666 | 62 | `:13710` | nothing | CORRECT-AS-EMPTY | no | none |
| `Begging` 0x13e7 | 67 | `:3937` | nothing — works off `GetTarget()` | CORRECT-AS-EMPTY | no | none |
| `InstillDoubt` 0x389e | 71 | `:9095` | nothing; eqemu's own comment: `//packet is empty as of 12/14/04` | CORRECT-AS-EMPTY | no | none |
| `Taunt` 0x5e48 | 73 | `:15215` | `ClientTarget_Struct` = 4, gated **with** return | **BROKEN-REJECTED** | **YES** — `CombatManager::Taunt` (`combat.cpp:715`), correct, live from `/taunt` | **routed** through `CombatManager::Taunt` |

**Tally: 12 CORRECT-AS-EMPTY, 3 BROKEN-REJECTED, 2 BROKEN-GARBAGE, 0
NEEDS-CAPTURE.** Five broken against the pre-scan's predicted four or five. The
STOP threshold ("more than about six") was not reached, so the fixes were
applied rather than reported back.

### Second-route sweep — the result, and the Taunt question settled

A grep for every one of the seventeen `HC_OP_*` constants across `src/`,
`include/`, `tests/`, `tools/` (excluding the `eq_strings.cpp` name table)
found **exactly three second routes**, and all three are on broken opcodes:

* `HC_OP_ApplyPoison` → `eq_player_info.cpp:698` (`SendApplyPoison`, zero
  callers — T151's inversion case)
* `HC_OP_BindWound` → `eq_player_info.cpp:1793` (`SendBindWound`, **live**, called
  from `EqActionHandler::bandage` `:1350`)
* `HC_OP_Taunt` → `combat.cpp:739` (`CombatManager::Taunt`, **live**, called from
  `EqActionHandler::taunt` `:390`)

The other fourteen have exactly two hits each — the `eq.h` enum entry and
`getSkillOpcode()`. The receive-side `case HC_OP_*` labels in `eq.cpp:1574-1638`
are inbound handling, not senders.

**Taunt, settled: `combat.cpp` is right and the skill path was wrong, and they
never conflicted — the skill path simply never arrived.** `CombatManager::Taunt`
sends a 4-byte `Target_Struct` (`packet_structs.h:628` already records it as
eqemu's `ClientTarget_Struct`, `titanium_structs.h:985`), which is exactly what
`Handle_OP_Taunt`'s `app->size != sizeof(ClientTarget_Struct)` gate demands. The
skill-73 path sent 0 bytes and hit the gate's `return`. Two senders for one
opcode where one is dead on arrival is not a conflict, it is the
duplicated-then-drifted shape mid-drift; skill 73 now calls
`CombatManager::Taunt(target_id)` and **no second Taunt sender was added**.

The same reasoning applied to the other two: `SendApplyPoison` was **not**
deleted (it was the correct copy all along), and `SendBindWound` was reused
rather than reimplemented. Only `Disarm` and `PickPocket`, which had no sender
anywhere, got new body-building code.

### What changed

* **`include/client/skill/skill_packet_bodies.h`** (new, 144 lines) — offset-named
  builders for the only two bodies that had no existing sender:
  `MakeDisarmBody()` (16 bytes) and `MakePickPocketBody()` (18 bytes), plus
  `kItemTypePoison`. Header-only and dependency-free so the test can pin it
  without linking `willeq_core`.
* **`src/client/skill/skill_manager.cpp`** — `sendSkillUsePacket()` now delegates
  the five bodied opcodes to a new `sendBodiedSkillPacket()` and keeps the
  `Resize(0)` path for the other twelve, with the audit named in the comment so
  the next reader does not have to re-derive which twelve. New
  `findPoisonInventorySlot()`.
* **`include/client/skill/skill_manager.h`** — two private declarations.
* **`tests/test_skill_packet_bodies.cpp`** (new, 7 tests) + `tests/CMakeLists.txt`.

**Targeting decisions, for the record.** Skill 9 mirrors
`EqActionHandler::bandage()` exactly (target, else current target, else self) so
there is one bind-wound targeting rule in the client, not two. Skills 16/48
refuse to send with no target rather than sending id 0 — `entity_list.GetMob(0)`
is a lookup miss server-side and, for Disarm, a `source` that is not our own id
is what triggers eqemu's `POSSIBLE_HACK` player-event log (`:5775`), so
`source` is always `GetMySpawnID()`.

**The poison slot.** `SendApplyPoison` needs an inventory slot that
`sendSkillUsePacket` never had. `findPoisonInventorySlot()` scans general slots
22-29 then general bags 251-330 for `ItemInstance::skillType ==
kItemTypePoison (42)`, and tells the user "You do not have any poison to apply."
rather than sending a slot it made up. Two things make this correct rather than
lucky, and both are worth recording:

1. **The slot numbering needs no translation.** `DECODE(OP_ApplyPoison)`
   (`titanium.cpp:2475`) runs `TitaniumToServerSlot(eq->inventorySlot)` before
   the handler sees it, so the Titanium-native ids this client already uses are
   exactly what belongs on the wire.
2. **`ItemInstance::skillType` is a misnomer that happens to carry the field we
   need.** `inventory_manager.cpp:284` parses serialized item field 56, and
   `titanium.cpp:3429` writes field 56 as `item->ItemType` — the full
   `EQ::item::ItemType` enum, not a weapon skill. The name has never caused
   trouble because for weapons the ItemType values *are* the weapon skill ids
   (`ItemType1HSlash = 0` … `ItemType2HPiercing = 35`), which is exactly how
   `eq_bridge.cpp:1449` uses it — so this is a naming defect, not a data
   defect, and it is left alone rather than renamed inside a skill unit.

### Evidence

Every verdict above is a source read, quoted by file and line. The load-bearing
ones:

| claim | evidence |
|---|---|
| only `OP_ApplyPoison` is DECODE-gated | `grep "DECODE(OP_\|ENCODE(OP_" common/patches/titanium.cpp` filtered to the seventeen returns 3 hits: `ENCODE(OP_ApplyPoison)` `:186`, `ENCODE(OP_Track)` `:2049`, `DECODE(OP_ApplyPoison)` `:2475` |
| a `DECODE_LENGTH_EXACT` miss is near-silent | `common/patches/ss_define.h:166-171` — logs to `LogNetcode` (a category, typically off), rewrites the opcode to `OP_Unknown`, returns. **Refines T151:** there IS a log line, but it is a Netcode-category line, not an error, and the handler's own "Wrong size" `LogError` never runs |
| zero-length ⇒ `pBuffer == nullptr` | `common/base_packet.cpp:30` `if (len>0) { … pBuffer = new unsigned char[len]; }` |
| nothing guards size before dispatch | `Client::HandlePacket`, `client_packet.cpp:444-513` — logs, checks `client_state`, calls the handler |
| BindWound / PickPocket fall through their own size check | `client_packet.cpp:4013-4020` and `:11913-11932` — quoted above |
| Taunt's gate is 4 bytes and combat.cpp sends 4 | `client_packet.cpp:15217`; `ClientTarget_Struct` = one `uint32` (`eq_packet_structs.h:1149`); `combat.cpp:735-739` `packet.Resize(4); packet.PutUInt32(0, actual_target);` |
| Disarm compares the skill value | `client_packet.cpp:5804` `disarm->skill == GetSkill(EQ::skills::SkillDisarm)` |
| PickPocket reads only `to` | `client_packet.cpp:11932` `entity_list.GetMob(pick_in->to)`; the remaining fields are the response payload |

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j8` | **PASS** (`-j8`; T148 recorded `-j24` discovery timeouts in this slot) |
| 2. `ctest --output-on-failure` | **PASS — 2531/2532**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. The 7 new `SkillPacketBodies.*` tests ran and passed. Suite is 2532 vs the plan's 2524 baseline: +7 from this unit, +1 already on the tip |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| 4. `scripts/ci/visual_gate.sh` | **PASS** — `=== VISUAL GATE: PASS ===`; `git status tests/goldens` empty, no re-bless |
| 5. `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| 6. `check_file_sizes.sh fable` | **PASS** — 4 files checked, 4 ok, 0 warn, 0 fail (run from this slot on the task branch, with the new files staged so they are actually checked) |

### Wire probes the follow-up unit owes — one per opcode changed

None of these were run. All five need the eqemu lock. Probes 1 and 4 need a
Rogue; probe 3 needs a class with Disarm.

1. **`OP_ApplyPoison` (skill 6).** Rogue holding a poison item. Fire skill 6 from
   the hotbar. Expect: the item **disappears from inventory** (eqemu deletes it
   on success *and* failure, `:3066`), and an `OP_ApplyPoison` echo arrives that
   `ZoneProcessApplyPoison` parses. This simultaneously closes T151's open item
   2 (whether `success` really sits at offset 6 of the echo) at no extra cost.
   Also assert the slot round-trips: the echo's `inventorySlot` is
   `ServerToTitaniumSlot`-encoded (`titanium.cpp:2483`), so it should equal the
   slot we sent.
2. **`OP_Bind_Wound` (skill 9). RUN THIS ONE DELIBERATELY.** The claim under test
   is that the *old* code could fault the zone process. Probe the **new** path
   first: fire skill 9 with a damaged target, expect the bind-wound message and
   HP change, and expect the zone to stay up. Only then, if the null-deref claim
   is to be confirmed at all, do it on a server nobody else is using and expect
   to restart it. **Do not confirm it casually.**
3. **`OP_Disarm` (skill 16).** Warrior/monk/rogue with Disarm, NPC target in
   range, in combat. Expect aggro on the target whether or not the disarm lands
   (eqemu adds to the hate list before rolling, `:5808`). A wrong `skill` field
   is indistinguishable from a miss except that it yields `DISARM_NO_TARGET`
   ("You cannot disarm that.") — watch for that string specifically, it is the
   tell that the skill value we send disagrees with the server's.
4. **`OP_PickPocket` (skill 48).** Rogue, humanoid NPC within 20 units. Expect a
   pickpocket response. **Assert on the server's player-event log too**: firing
   twice inside the recovery timer, or from out of range, files a
   `POSSIBLE_HACK` entry (`:11929`, `:11951`), so a clean probe should produce
   exactly zero of those.
5. **`OP_Taunt` (skill 73).** NPC target, combat zone, within
   `RuleI(Skills, MaximumTauntDistance)`. Expect a hate-list change. This one
   also proves the routing rather than the bytes — `/taunt` already worked, so
   the assertion is that the **hotbar skill** now produces the same server
   effect as the command.

Probes 3 and 4 are the only two whose *bytes* are unproven; 1, 2 and 5 exercise
senders that were already correct and only needed a caller.

### Deviations, flagged

1. **The pre-scan's bucketing was wrong in three places and is contradicted
   above**, not quietly corrected: `Hide` and `Begging` were listed as buffer-
   readers and are clean; `PickPocket` was listed as cleanly gated and is not.
   The headline count ("four or five") was right; the reasoning behind it was
   not, and the difference is the whole finding.
2. **The serialization test's authority is eqemu SOURCE, not captured wire
   bytes.** The plan asks for offset-named assertions "per T147's
   `tests/captured_packets.h` convention" — that convention pins *observed*
   bytes, and there is no capture for `OP_Disarm` or `OP_PickPocket` because
   this client has never sent either with a body. The test is offset-named and
   field-by-field as asked, and it does not use a bare `sizeof`, but it is a
   weaker authority than a capture and the file's docstring says so in those
   words. It is not added to `captured_packets.h`, so that header keeps meaning
   only one thing.
3. **A defect was found and NOT fixed, because it is out of territory.**
   `ItemInstance::skillType` (`item_instance.h:149`) is documented as "Weapon
   skill type" and actually carries eqemu's full `ItemType`. Harmless today
   (see above) and this unit depends on the real meaning, but the comment and
   the `WeaponSkillType` enum next to it (`inventory_constants.h:379`) are
   misleading to the next reader. Renaming it touches `eq_bridge.cpp` and the
   inventory headers — reported, not followed.
4. **Skill 9 is not marked `requires_target`** in `skill_data.h`, unlike 16/48/73.
   That is arguably correct (self-bandaging is normal) and the new code handles
   target 0 by falling back to self, so no flag was changed. Recorded because it
   looks like an omission and the next reader will wonder.
5. **Shell-discipline slip.** Two intermediate compile checks piped build output
   through `grep`/`tail`, which the protocol forbids. All six graded
   verification steps were re-run clean, foreground, redirected to files rather
   than piped, and the results above come from those runs — but the slip
   happened and is recorded rather than omitted.
6. **The null-deref finding is a source reading, not an observation.** It is
   stated as such throughout. Probe 2 is the only thing that can settle it and
   it was deliberately not run.

### For `refactor/deferred.md`

* The five wire probes above are one owed unit, not five — they share a session.
* `ItemInstance::skillType` misnaming (deviation 3).
* The reachability pattern's count does not move: none of these five were
  "implemented but unreached". They were **reached and wrong**, which is the
  class T151 named UNVERIFIED. Of the seventeen reachable skill opcodes,
  five had never once produced their intended server effect.

