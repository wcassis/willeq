# F06 — Split chat and text-message clusters into eq_chat.cpp

Move the chat pipeline out of `src/client/eq.cpp` (11,932 lines).
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; positions shifted heavily by F01–F07.
Enumerate by NAME.)

Move to new `src/client/eq_chat.cpp`:
1. **Chat cluster** (orig 7288–7593, ~11 methods): channel-message send and
   process (`ZoneSendChannelMessage`, `ZoneProcessChannelMessage`),
   `SendChatMessage`, the `AddChat*` family (system/emote messages into the
   chat buffer), hotbar-pending chat helpers, `ProcessChatInput`.
2. **Text messages cluster** (orig 12432–12666, ~3 methods): the
   colored/formatted/simple text processors (`ZoneProcessColoredText` — noted
   as deliberately left behind by F07 for this unit — plus
   FormattedMessage/SimpleMessage processors).

Enumerate by grep + boundary reading; anything in-range that is clearly not
chat/text (judgment) stays put, documented.

## Steps

1. Locate both clusters; list every `EverQuest::` definition found.
2. Create `src/client/eq_chat.cpp`: header comment + pruned includes.
3. Move verbatim; preserve any #ifdef guards exactly.
4. File-local static helpers: usual rule (move if exclusive, else leave +
   document).
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_chat.cpp` — ok.
- eq.cpp shrinks by ~500+ lines.

## Review

**Branch:** `task/F06-chat`, commit `369a5b1`.

**Located clusters** (line numbers as found on this branch, pre-move):

1. Chat cluster, eq.cpp lines 4414–4689 (before UCS block, which turned out
   to be entirely inside a `/* ... */` comment — no live UCS definitions
   exist in eq.cpp today, so nothing there needed handling):
   - `EverQuest::ZoneSendChannelMessage`
   - `EverQuest::ZoneProcessChannelMessage`
   - `EverQuest::SendChatMessage`
   - `EverQuest::AddChatSystemMessage`
   - `EverQuest::AddChatCombatMessage`
   - `EverQuest::AddChatMissMessage`
   - `EverQuest::AddPendingHotbarButton`
   - `EverQuest::GetPendingHotbarButtons`
   - `EverQuest::ClearPendingHotbarButtons`
   - `EverQuest::GetPendingHotbarButtonCount`
   - `EverQuest::ProcessChatInput`

   (11 methods — the 4 pending-hotbar-button helpers were interleaved
   between `AddChatMissMessage` and `ProcessChatInput` under their own
   "Hotbar Button Management" banner comment; moved together per the plan's
   "hotbar-pending chat helpers" note.)

2. Text-messages cluster, eq.cpp lines 6515–6748 (immediately after
   `ZoneProcessMoveDoor`, confirming F07's note that it left
   `ZoneProcessColoredText` in place for this unit):
   - `EverQuest::ZoneProcessColoredText`
   - `EverQuest::ZoneProcessFormattedMessage`
   - `EverQuest::ZoneProcessSimpleMessage`

**Helper analysis:** No file-local (anonymous-namespace/static) helpers are
exclusive to either cluster — all supporting state (`m_bridge`, `m_string_db`,
`m_combat_manager`, `m_entities`, `s_debug_level`, `m_pending_hotbar_buttons`,
`m_command_registry`) and free functions (`GetStringMessage`,
`GetFormattedStringMessage`, `eqt::parseFormattedMessageArgs`) are class
members or externally-declared, so nothing needed relocating or de-static-ing.

**Line counts:** eq.cpp 11,932 → 11,422 lines (−510). New `src/client/eq_chat.cpp`:
538 lines (510 moved + 28 header/include boilerplate).

**Necessary-include deviation:** `eq_chat.cpp` added `#include "client/combat.h"`,
which eq.cpp did not need explicitly (it pulled in the full `CombatManager`
definition transitively through its much larger include list). Without it,
`ZoneProcessSimpleMessage`'s use of `m_combat_manager->GetTargetId()` fails
against the forward declaration in `eq.h`. Pure include addition, no logic
change — flagged per plan step 4/verification note.

**Verifier summary:**
- `cmake --build build -j24`: clean (after adding the `combat.h` include).
- `ctest --test-dir build`: 1191/1192 passed; sole failure
  `AudioManagerTest.PreloadCommonSounds` is the documented pre-existing ALSA
  expected-fail in `refactor/test_baseline.txt`, unrelated to this unit.
- `verify_move_only.py ebbf816..369a5b1`: PASS, move-only modulo boilerplate
  (CMakeLists.txt change reported separately, as expected for a new TU).
- `check_file_sizes.sh --files eq_chat.cpp`: ok, 538 lines.

**Deviations:** none beyond the documented `combat.h` include above.
