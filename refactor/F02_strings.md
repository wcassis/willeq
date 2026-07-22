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

(executor fills in)
