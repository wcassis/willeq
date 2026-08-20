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

(executor fills)
