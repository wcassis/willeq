# M00 — The 1,500-line ceiling holds tree-wide: retire the allowlist

Both single-class headers are now under the soft limit that Batch J said no
mechanical split could reach (`eq.h` 2,025 → 899 by Batch K;
`irrlicht_renderer.h` 1,914 → 1,343 by Batch L). The allowlist that
carried them (K00 Leg B) has nothing left to carry. Slot2; no code change.

## Steps

1. `scripts/refactor/file_size_allowlist.txt`: delete both entries (leave
   the file present and EMPTY with a comment line saying what it is for,
   so the checker's `--all` mode still parses it).
2. `scripts/refactor/check_file_sizes.sh`: under `--all`, a file over 1,500
   that is not allowlisted is a **FAIL**, no longer a WARN (the soft limit
   becomes the enforced limit; the 3,000 hard cap stays as a second line).
   Keep the "nothing was checked" rule. Update the header comment and
   `docs/decisions.md` (a dated entry: the ceiling is 1,500 for every
   `.cpp`/`.h`/`.hpp` under `src/`, `include/`, `tests/`, enforced by
   ctest; a future single-class file that must exceed it gets an allowlist
   entry with a reason and a no-grow max, exactly as before).
3. Run `--all` and paste the output: 0 FAIL, 0 WARN expected. If a test
   file is between 1,500 and 3,000 (the survey flagged
   `tests/test_eq_commands_equip.cpp` at 1,485 in the morning), report it
   — it is close; do not split it in this unit.
4. `CLAUDE.md` §"Process requirement": the Batch J paragraph becomes
   "Batches J, K, L and G are complete; every source file is under 1,500
   lines and ctest enforces it" with a pointer to the three batch docs'
   close-outs. `refactor/BOARD.md`: strike nothing; add a P entry closed
   in place recording the milestone with the numbers.
5. ctest: `FileSizeCeiling` and `DocCitations` pass; full suite once.

Territory: the two scripts, `docs/decisions.md`, `CLAUDE.md`,
`refactor/BOARD.md`. Visual gate + ARM skipped with evidence.
