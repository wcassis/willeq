# G02 — Split RegisterCommands into per-domain registration methods

Decompose the 2,568-line `eq_commands.cpp` (dominated by the single
`RegisterCommands` method of sequential slash-command lambda registrations)
into per-domain `Register*Commands()` private methods across 2–3 TUs, each
file under the 1,500-line warn ceiling.

## Design

- Command registrations are sequential, independent
  `m_command_registry->...` calls with lambda bodies — partition them by
  domain. Survey the actual command set first, then group into roughly:
  gameplay/chat/social commands; movement/navigation/zone commands;
  debug/diagnostic/renderer commands (match reality over this guess —
  put the real grouping in the review).
- New private methods declared in eq.h (e.g. `RegisterGameplayCommands()`,
  `RegisterDebugCommands()`, …); `RegisterCommands()` becomes a short driver
  calling them in the ORIGINAL registration order (order can matter for
  duplicate names/aliases — preserve it exactly; if a domain grouping would
  reorder registrations, document why it is safe or keep order-critical
  entries in the driver's sequence).
- Lambda bodies move verbatim. The `#ifdef EQT_HAS_GRAPHICS` guard wrapping
  the current file must be preserved on every new file/method.
- Files: keep `eq_commands.cpp` (driver + first domain), add
  `eq_commands_<domain>.cpp` as needed. runPmemDiagnostics /
  runLoadDiagnostics stay where they are.
- Add new files to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Steps

1. Analysis first (in review BEFORE editing): list every registered command
   name in order, with its assigned domain group.
2. Declare the Register*Commands methods in eq.h (private, guarded like
   RegisterCommands).
3. Split verbatim; driver preserves order.
4. CMakeLists update.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — expected:
  additions limited to method signatures/declarations/driver calls and
  per-file boilerplate. Paste output in review for orchestrator sign-off.
- `bash scripts/refactor/check_file_sizes.sh --files <all eq_commands*.cpp>`
  — every file under 1,500 (ok, no WARN).

## Review

(executor fills in)
