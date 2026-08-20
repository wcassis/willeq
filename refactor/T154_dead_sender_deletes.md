# T154 — Batch B: delete four dead senders

**Slot: slot2. x86. NO eqemu lock. No Pi.**

**Territory: `src/client/eq_player_info.cpp` (the three bodies named below ONLY),
`src/client/eq.cpp` (the `SendPetitionDelete` body ONLY),
`include/client/eq.h` (these four declarations ONLY), `tests/`, `refactor/`.**

**T153 (slot1) runs in parallel and rewrites `EverQuest::SendConsiderCorpse` at
`eq_player_info.cpp:1241` — the function DIRECTLY ABOVE `SendConfirmDelete`, which
you are deleting.** Delete exactly the function and its declaration; do not
reflow, re-comment or move anything around them. T153 touches no file you own
except `eq_player_info.cpp`, and only that one adjacent body.

This unit comes from T151's triage (`refactor/T151_zero_caller_triage.md`,
"Proposed batching" → Unit B, with the per-sender justifications in
"DELETE justifications — what would be lost"). **Read those justifications; they
are the content of this unit's commit message.**

## The four

| sender | site | why it goes |
|---|---|---|
| `SendConfirmDelete` | `eq_player_info.cpp:1253`, `eq.h:1046` | eqemu's `Handle_OP_ConfirmDelete` is `{ return; }` in full — ours prints "Your corpse has been summoned to your bind point", which is a **lie to the user** |
| `SendPetitionDelete` | `eq.cpp:3844`, `eq.h:1122` | no petition feature exists; nothing can produce a petition id |
| `SendPopupResponse` | `eq_player_info.cpp:1662`, `eq.h:1071` | no popup feature exists; nothing can produce a popup id |
| `SendSaveOnZoneReq` | `eq_player_info.cpp:1646`, `eq.h:1070` | eqemu already calls `SaveCharacterData()` at `zoning.cpp:533` before the handoff — the request is redundant, not missing |

This is the trap class T147 cleared out of `packet_structs.h`: code that looks
implemented, has never run, and would mislead the next reader who assumes it works.

## The work

1. **Re-verify zero callers yourself** across `src/`, `include/`, `tests/`,
   `tools/` before deleting each one. T151's sweep was five days ago and T148–T152
   have landed since. **If any of the four has acquired a caller, STOP and report
   it** — do not delete around it.
2. Delete the four definitions and their four `eq.h` declarations.
3. **Leave the structs alone.** `PopupResponse_Struct`'s comment records a real
   T145 wire verification worth keeping. Same for anything else in
   `packet_structs.h`.
4. **Leave the opcodes alone** (`HC_OP_ConfirmDelete`, `HC_OP_PetitionDelete`,
   `HC_OP_PopupResponse`, `HC_OP_SaveOnZoneReq`) and their `eq_strings.cpp` name
   entries — the opcode table is a protocol reference, not a call graph.
5. **One commit**, with T151's justification for each of the four quoted into the
   message so the next reader does not re-derive it.
6. Remove any test that exists only to exercise a deleted sender; report each one
   removed. Do not weaken a test that covers something else.

## Also in scope — one comment, no code

T151's second hazard: **`SendControlBoat` files a `POSSIBLE_HACK` player-event
report against our own player if it is ever called** (`Handle_OP_ControlBoat`
resolves `boatId` via `entity_list.GetMob()`, and any hit that is not a
controllable boat logs the report; `m_boat_id` is only ever set by the
already-broken `SendBoardBoat`). Boats are deferred indefinitely, so the sender
stays — **but add a comment on `SendControlBoat` recording the hazard**, citing
the eqemu handler. T151: "that comment is worth more than the three senders are."
Do NOT delete or wire any boat sender.

## Acceptance

Deletion-only unit. The bar is: the tree builds, the suite is unchanged except for
tests that only covered deleted code, and nothing else moved. **No behaviour
change is claimed and none should appear.**

## Verification

Full protocol bar. Suite baseline **2531/2532** at tip `c123c7b4`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Report the new
count and account for every test that disappears.

Also run `bash scripts/refactor/check_file_sizes.sh fable` — files only shrink here,
but run it.

## STOP conditions

- Any of the four has acquired a production caller since T151.
- A deletion cascades past the four bodies and their declarations (it should not;
  if it does, something else depends on them and that is the finding).

## Review

(executor fills this in)
