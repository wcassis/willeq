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

(executor fills in)
