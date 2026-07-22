# F01 — Split RegisterCommands into eq_commands.cpp

Pilot unit of Batch F. Move the slash-command registration cluster out of
`src/client/eq.cpp` (19,676 lines) into a new TU. Pure move — no behavior
change, no interface change, no reformatting of moved code.

## Scope (line refs @ commit a0c02cd — verify by method name, not line number)

Move these `EverQuest` method definitions from eq.cpp to
`src/client/eq_commands.cpp`:
- `RegisterCommands` (~lines 7709–10075, ~2,366 lines of lambda registrations)
- `runPmem` and `runLoadDiagnostics` (~7594–7709, the two diagnostic helpers
  in the same cluster — confirm exact names/signatures by reading that range)

## Steps

1. Read the cluster boundaries in eq.cpp (grep for `::RegisterCommands`,
   `::runPmem`, `::runLoadDiagnostics`; find where each definition ends).
2. Create `src/client/eq_commands.cpp`. Top of file: a brief comment noting it
   holds EverQuest slash-command registration, then the include block —
   start from the includes eq.cpp has and prune to what the moved code needs
   (compiler-driven; boilerplate include changes are allowed by the verifier).
3. Move the method definitions verbatim (cut from eq.cpp, paste unchanged).
4. If moved code references file-local helpers from eq.cpp (`static` functions
   or anonymous-namespace symbols):
   - used ONLY by moved code → move them too (list each in the review);
   - used by BOTH files → leave in eq.cpp, remove `static`/move the
     declaration into a new `src/client/eq_internal.h` shared by both TUs.
     List each such symbol in the review — the verifier will flag these
     lines and they need orchestrator sign-off.
5. Add `src/client/eq_commands.cpp` to the same source list as eq.cpp in
   CMakeLists.txt.

## Verification

- Build clean: `cmake --build build -j24` (use a 600000 ms timeout; if it
  still times out, re-run — it resumes incrementally).
- `ctest --test-dir build` — zero NEW failures vs `refactor/test_baseline.txt`
  (pre-existing failures listed there are expected).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS, or
  substantive lines limited to the de-static-ed helpers documented in review.
- `bash scripts/refactor/check_file_sizes.sh fable` — eq_commands.cpp will
  WARN (>1,500); that is accepted for this unit. It must not FAIL (>3,000).
- eq.cpp shrinks by roughly 2,400+ lines.

## Review

(executor fills in)
