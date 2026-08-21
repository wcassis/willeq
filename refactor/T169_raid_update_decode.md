# T169 — `ZoneProcessRaidUpdate`: decode the raid packet correctly

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi (offline).** Orchestrator
executed directly.

**Territory: `include/client/raid_update.h`, `src/client/raid_update.cpp`,
`src/client/eq_entities.cpp` (`ZoneProcessRaidUpdate` only),
`include/client/eq.h` (raid opcodes only), `src/client/eq_strings.cpp` (one
line), `tests/test_raid_update_decode.cpp`, CMake, `refactor/`.**

## What was wrong

`ZoneProcessRaidUpdate` read a single `uint32` action and switched on six
hand-guessed values. **Four of the six disagreed with eqemu's own enum**
(`common/raid.h`), and the body was never parsed at all, so no message could
name anyone:

| action | what we said | what it is |
|---|---|---|
| 0 | "You have joined the raid." | `raidAdd` — **any** member joined; the same packet goes to the whole raid, so this announced the player joining once per member |
| 2 | "member joined" (log) | `raidMemberNameChange` |
| 3 | "The raid leader has changed." | `raidRemove1` — a member **leaving** |
| 6 | "The raid has disbanded." | `raidMembers` — the full roster. **The real disband is 5, which was unhandled** |
| 10 | "lock state update" (log) | `raidNoRaid` — "you are not in a raid". Lock/unlock are **17/18**, both unhandled |
| 20 | "Unknown raid action" | `raidSetLeader`, **which is how a raid INVITE reaches the invited client**. An invited player was told nothing whatsoever. |

Only action 1 was right.

## The part that could not be guessed: there is no single raid struct

The body shape **depends on the action**, and a decoder that reads
`parameter` at a fixed offset — the obvious implementation — is wrong for four
of them:

- **action 0** → Titanium `RaidAddMember_Struct` (140 B): general + `_class`,
  `level`, `isGroupLeader`.
- **action 8** (`raidCreate`) → emu `RaidCreate_Struct` (72 B):
  `action@0, leader_name@4, leader_id@68`. It arrives here at all because
  **`OP_RaidJoin` and `OP_RaidUpdate` are the same Titanium wire value,
  `0x1f21`** (`patch_Titanium.conf:441` and `:443`), and `titanium.cpp` has no
  `ENCODE(OP_RaidJoin)`, so it goes out raw.
- **action 10** → eqemu's **internal** `RaidGeneral_Struct` (140 B), sent
  unencoded by `ENCODE(OP_RaidUpdate)`'s `case raidNoRaid:
  dest->QueuePacket(inapp)`. The internal struct has an extra `unknown1@68`,
  so its names and parameter sit **4 bytes later** than every other action's.
  Only `action` is at a common offset — so the decoder reads nothing else for
  it rather than reporting a misaligned name.
- **actions 14 / 30** → `RaidLeadershipUpdate_Struct`: general-compatible for
  the two names, but **offset 132 is leadership AA data, not `parameter`**.
- **35 / 36** → general + a trailing motd/note string.
- everything else → Titanium `RaidGeneral_Struct` (136 B):
  `action@0, player_name@4, leader_name@68, parameter@132`.

**Note the 68-vs-72 trap.** eqemu's internal struct puts `leader_name` at 72;
Titanium's puts it at 68. Using the internal layout for a Titanium packet
reads the tail of `player_name` plus four bytes of `leader_name`, which for
ordinary short names yields an **empty string** — a silent failure, not a
visible one. There is a test for exactly that.

## What was done

Decoding moved to `DecodeRaidUpdate()` in its own TU — the same reasoning as
T167: `ZoneProcessRaidUpdate` is private and cannot be tested, and the shape
table is the part that rots. The action enum is **eqemu's names verbatim**
(`raidAdd`, `raidRemove2`, …) on purpose: renaming them to something more
readable is how a mapping drifts away from its authority.

The handler is now only rendering, and it says true things: joins and leaves
name the member (and distinguish "You" from everyone else — without that
comparison the client announces the player's own join once per raid member),
disband is 5, lock/unlock are 17/18, `raidNoRaid` says "You are not in a
raid", and **an invite now tells the player who invited them**.

Known-but-unhandled and genuinely-unknown actions are different facts and now
read differently in the log, via `RaidActionName()`.

**SCOPE, stated plainly: this is the decode, not a raid feature.** There is no
roster, no accept path, no `/raid` commands. Action 6's member list is logged
as received and **not parsed** — announcing a roster this unit has not decoded
would be the same class of guess it exists to remove. The invite message says
out loud that the client cannot accept yet, because accepting needs an
`OP_RaidCommand` sender with `action = 1 (RaidCommandAcceptInvite)`,
`player_name` = the inviter, `leader_name` = us — which T168 deliberately did
not leave lying around broken.

## Two opcode constants, one fixed and one reported

- **`HC_OP_RaidJoin = 0x3c24` DELETED.** `0x3c24` maps to nothing in
  `patch_Titanium.conf`; the real `OP_RaidJoin` is `0x1f21`, the same value as
  `OP_RaidUpdate`, so it cannot be given its true value without colliding. It
  had no sender and no dispatch arm. The collision fact now sits on
  `HC_OP_RaidUpdate`, where it explains why action 8 arrives there.
- **`HC_OP_MarkRaidNPC = 0x5191` — found, NOT fixed** (outside this unit's
  scope). `0x5191` does not appear in `patch_Titanium.conf`; eqemu's
  `OP_MarkNPC` is `0x5483` (`:528`). No sender, no dispatch arm, referenced
  only by `eq_strings.cpp`. Annotated in place for whoever owns it next.

## Verification

- **15 new tests**, all against eqemu's source as the authority: the action
  codes, both name offsets, the 64-byte-name boundary, the add-member tail,
  and each of the three non-general shapes. The two that matter most are
  `LeadershipActionsDoNotReportAParameter` and
  `NoRaidIsRecognisedAndReportsNoMisalignedNames` — both pin decisions a
  reviewer would wave through.
- Suite **2619/2620**, sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`.
- Build clean; `build-gles2-x86.sh` clean; **visual gate PASS**, goldens
  untouched; `build-arm-noble.sh` clean; `check_file_sizes.sh` no new FAILs.
- `check_sender_reachability` (T168's gate) still passes.

## Not verified

**Not wire-verified.** Every shape here is read from eqemu source, not from a
capture. A live probe needs **two accounts in a raid** — `Testmelee` and
`Oraclerogue` both exist (see the character memory), so it is runnable when
the server is up: `#raid` an invite from one to the other and confirm the
invite message names the inviter, then add/remove/disband and confirm each
message. Until then the decode is source-verified only, which is exactly the
status T148 taught us not to confuse with tested.

## What this unblocks

The raid family is now scopeable. A raid unit needs, in order: the action-6
roster parse, an `OP_RaidCommand` sender (body recorded on `HC_OP_RaidInvite`
by T168), and `/raid` commands on top. None of that is blocked on decoding any
more.
