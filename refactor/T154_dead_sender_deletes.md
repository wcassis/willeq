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

**Executed slot2, branch `task/T154-dead-sender-deletes`, off `fable` (`58548116`).**

### Zero-caller re-verification (done fresh, post T148-T152)

Method: two greps over the working tree at branch creation —
(1) `grep -rn "SendConfirmDelete\|SendPetitionDelete\|SendPopupResponse\|SendSaveOnZoneReq" src/ include/ tests/ tools/ scripts/ docs/`
and (2) the same pattern over the whole checkout excluding `build/`, `build-gles2/`,
`.git/`, `third_party/`, `refactor/`. Both returned **exactly eight lines** — one
definition and one declaration per sender, and nothing else:

| sender | definition | declaration | other hits |
|---|---|---|---|
| `SendConfirmDelete` | `eq_player_info.cpp:1253` | `eq.h:1046` | none |
| `SendSaveOnZoneReq` | `eq_player_info.cpp:1646` | `eq.h:1070` | none |
| `SendPopupResponse` | `eq_player_info.cpp:1662` | `eq.h:1071` | none |
| `SendPetitionDelete` | `eq.cpp:3844` | `eq.h:1122` | none |

No caller acquired since T151. No STOP condition hit. Sites matched T151's
recorded line numbers exactly, so nothing had shifted underneath the triage
either. There is no command-table or string-dispatch route to these names — the
second grep covered the whole tree, including `eq_commands*.cpp`, the action
dispatcher and the help tables, and found nothing.

### Deletions

Four definitions and four declarations, nothing else. `SendPopupResponse`'s
five-line T145 comment block went with it — it documents only that function's
wire format; the T145 verification it summarises is preserved independently in
`packet_structs.h`'s `PopupResponse_Struct` comment, which was not touched.

Left alone as instructed: all structs (`PopupResponse_Struct` included), all four
opcodes (`HC_OP_ConfirmDelete`, `HC_OP_PetitionDelete`, `HC_OP_PopupResponse`,
`HC_OP_SaveOnZoneReq`) and their `eq_strings.cpp` name entries.

Nothing cascaded — the deletions are leaf functions; the build needed no other
edit.

### Tests removed

**None.** No test referenced any of the four (grep (1) above covered `tests/`
and returned no hit there). `test_wire_layout_t145` exercises the *structs*, not
the senders, and is untouched. Suite count is therefore unchanged and no test
needs accounting for.

### `SendControlBoat` comment (no code change)

Added a `T151/T154 HAZARD -- DO NOT WIRE THIS UP` block above
`SendControlBoat` in `eq_player_info.cpp`, appended to the existing T145 comment
(the T145 text was not altered). It records that eqemu's
`Handle_OP_ControlBoat` (`zone/client_packet.cpp:5421`) resolves `boatId` via
`entity_list.GetMob()` and writes a `PlayerEvent::POSSIBLE_HACK` player-event log
entry ("OP_Control Boat was sent against {} which is of race {}") for any hit
that is not a controllable boat, and that `m_boat_id` is only ever set by the
broken `SendBoardBoat`. No boat sender was deleted or wired.

### Territory / parallel-unit discipline

`SendConsiderCorpse` (slot1's T153 target, directly above `SendConfirmDelete`) is
byte-identical in this diff — the deleted block starts at its closing brace + 1
and nothing above it moved. No neighbour was reflowed, re-commented or relocated
in any of the three files. Files touched: `include/client/eq.h`,
`src/client/eq.cpp`, `src/client/eq_player_info.cpp` — all within territory.

### Verification results

1. `cmake --build build -j24` — **PASS**, clean, unpiped. Only the tree's
   pre-existing `-Wpointer-arith` / OpenSSL-deprecation warnings; no new warning.
2. `ctest --output-on-failure` — **PASS at baseline**: `99% tests passed, 1 tests
   failed out of 2532` → **2531/2532**, identical to the `c123c7b4` baseline. The
   sole failure is the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   **Zero tests disappeared** (2532 total before and after), so there is nothing
   to account for.
3. `bash scripts/refactor/check_file_sizes.sh fable` — exit 1, all three findings
   **pre-existing and strictly improved** by this unit:
   - `include/client/eq.h` WARN 2218 lines (was 2222 on `fable`)
   - `src/client/eq.cpp` FAIL 3908 lines (was 3920)
   - `src/client/eq_player_info.cpp` WARN 2038 lines (was 2073)

   Every file shrank; no new file crossed a threshold. `eq.cpp` was already over
   the 3000-line FAIL bar on `fable` — splitting it is out of scope here.

### Deviations

1. `ctest` output was piped through `tail -40` / `grep` to extract the failure
   list and the totals line. The shell-discipline rule names *build* output; the
   build itself was run fully unpiped. The full `--output-on-failure` run was
   executed, and its complete failure list is reported above.

No other deviations. No behaviour change is claimed and none appeared.
