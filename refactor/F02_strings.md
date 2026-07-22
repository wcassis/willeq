# F02 — Split static name/string tables into eq_strings.cpp

Move the static lookup/name-table cluster out of `src/client/eq.cpp`
(17,195 lines after F01). Pure move — no behavior change, no interface change,
no reformatting of moved code.

## Scope (line refs @ commit a0c02cd, unchanged by F01 — verify by name)

Move from eq.cpp to new `src/client/eq_strings.cpp`:
- The name/string/lookup functions in roughly lines 204–938: `FormatPrice`,
  `GetOpcodeName`, `GetStringMessage`, and the Get*Name family (class, race,
  deity, body type, equip slot, NPC type), `ChatTypeName`-style helpers,
  `LoadStringFiles`, plus the dump helpers `DumpEntityAppearance` /
  `DumpPacket` (~813–938). Enumerate the exact set by reading that range —
  move every function/method defined there EXCEPT anything that is not part
  of this cluster (the range ends where the ctor/lifecycle cluster begins).
- Some are free/static functions, some are `EverQuest::` methods — both move.
  Free functions used only within the moved cluster stay file-local in the
  new TU; if any is also referenced by remaining eq.cpp code, leave it in
  eq.cpp OR de-static it via a new `src/client/eq_internal.h` — document
  each case in the review.

## Steps

1. Read eq.cpp lines ~200–950; list every definition and where it's referenced
   (grep each name across src/ and include/).
2. Create `src/client/eq_strings.cpp`: brief header comment, minimal include
   block (this cluster is mostly tables — it should NOT need the full eq.cpp
   include set; prune compiler-driven).
3. Move definitions verbatim.
4. Add the new file to `CLIENT_CORE_SOURCES` in CMakeLists.txt next to
   eq_commands.cpp.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — fully green (baseline is empty;
  RDPServerFrameTest.UpdateFrameMultipleTimes may rarely segfault under
  parallel load — re-run to confirm it's the known flake before treating as
  a failure).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS, or
  substantive lines limited to documented de-static-ing.
- `bash scripts/refactor/check_file_sizes.sh --files src/client/eq_strings.cpp`
  — must be ok (<1,500 expected).
- eq.cpp shrinks by ~700+ lines.

## Review

**Cluster boundary (verified by reading eq.cpp lines 1-950 directly, commit
`a0c02cd` state unchanged before this branch):** lines 204-938 inclusive, ending
exactly at the blank line before `EverQuest::EverQuest(...)` (the ctor at old
line 939). `eqcrypt_block` (189-202), the `EXP_FOR_LEVEL` table (95-161),
`MAX_EXP_LEVEL`, and `CalculateExpProgress` (165-184) precede the cluster and
are NOT part of it (not in scope per the plan; left in eq.cpp untouched).

**Moved definitions (16, all verbatim, all `EverQuest::` methods):**
- `FormatPrice(int32_t)`
- `GetOpcodeName(uint16_t)`
- `GetStringMessage(uint32_t)`
- `GetFormattedStringMessage(uint32_t, const std::vector<std::string>&)`
- `LoadStringFiles(const std::string&)`
- `GetChatTypeName(uint32_t)`
- `GetClassName(uint32_t)`
- `GetRaceName(uint32_t)`
- `GetDeityName(uint32_t)`
- `GetBodyTypeName(uint8_t)`
- `GetEquipSlotName(int)`
- `GetNpcTypeName(uint8_t)`
- `DumpEntityAppearance(const std::string&) const`
- `DumpEntityAppearance(uint16_t) const`
- `DumpPacket(const std::string&, uint16_t, const EQ::Net::Packet&)`
- `DumpPacket(const std::string&, uint16_t, const uint8_t*, size_t)`

No free/static file-local functions were in this range (all 16 are class
methods declared in `include/client/eq.h`), so the "de-static via
eq_internal.h" fallback in the plan wasn't needed.

**Reference analysis:** grepped `src/` and `include/` for every moved name.
All external call sites (`eq_action_handler.cpp`, and callers throughout
eq.cpp itself, e.g. `GetOpcodeName` used by `DumpPacket`) go through the
`EverQuest::` methods declared in `eq.h`, which is unchanged — no header
edits were needed. No moved function is referenced via a free-function name
that would require exposure through a new internal header.

**Dependencies of the moved cluster:** `HC_OP_*` opcode enum (defined
directly in `eq.h`), `EQT::StringDatabase m_string_db` member (declared in
`eq.h` via `client/string_database.h`), `Entity` type + `m_entities` map
(declared in `eq.h`), `EverQuest::s_debug_level` (static member, declared in
`eq.h`), `LOG_ERROR`/`LOG_INFO`/`LOG_TRACE` + `MOD_MAIN`/`MOD_NET` (from
`common/logging.h`), `fmt::format` (pulled in transitively by
`common/logging.h`). New file's include block: `client/eq.h`,
`common/logging.h`, `common/packet_structs.h` (for `EQ::Net::Packet`),
plus `<iostream> <iomanip> <sstream> <algorithm> <cctype>` for the dump
helpers' stream/transform/isprint usage. This is a much smaller include set
than eq.cpp's full block — confirms the plan's expectation that this cluster
doesn't need eq.cpp's graphics/audio/inventory headers.

**Line counts:**
- `eq.cpp`: 17,195 → 16,460 lines (-735)
- `eq_strings.cpp`: 746 lines (new; 735 moved code lines + 11 lines of header
  comment/includes)
- Net change: +11 lines total (pure move, only the new include block added)

**Build/test verification:**
- `cmake --build build -j24`: clean, exit 0 (pre-existing `-Wpointer-arith`
  warnings in eq.cpp are unrelated to this move, not introduced by it)
- `ctest --test-dir build`: 1190/1192 passed on first run.
  - `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULTed under parallel
    load; re-ran in isolation and it passed — confirmed as the documented
    known flake, not a regression.
  - `AudioManagerTest.PreloadCommonSounds` failed both in the full run and
    re-run in isolation, with ALSA errors (`cannot find card '0'`,
    `Unknown PCM default`) — this sandbox has no audio hardware/ALSA device.
    This test exercises the audio subsystem only; `eq_strings.cpp`/`eq.cpp`
    changes here have zero relationship to audio code, and the failure mode
    (missing sound card) is purely an environment limitation, not a
    regression from this move. Not previously documented in
    `test_baseline.txt`; flagging here as an environment gap for the
    orchestrator rather than silently treating it as green.
- `python3 scripts/refactor/verify_move_only.py fable..HEAD`: PASS (see
  commit for final output; only the moved 735 lines plus 11 added
  include/header lines to the new TU, and the 3-line CMakeLists.txt
  addition — no de-static-ing was needed).
- `bash scripts/refactor/check_file_sizes.sh --files src/client/eq_strings.cpp`:
  ok (746 lines, under the 1,500-line ceiling).

**Deviations from plan:** none in scope/steps. One informational note: the
plan's fallback step ("de-static it via a new `src/client/eq_internal.h`")
was not needed since every moved definition is a header-declared
`EverQuest::` method, not a free function requiring exposure.
