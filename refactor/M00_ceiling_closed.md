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

---

## Review

Executed in `wt/slot2` on `task/M00-ceiling-closed`, base `fable` = `668801ab`
(verified at checkout). No file under `src/`, `include/` or `tests/` changed —
`git diff --name-only $(git merge-base fable HEAD)` is exactly the five files of
the declared territory, and the same diff filtered to
`*.cpp *.h *.hpp *.cmake CMakeLists.txt` is EMPTY.

### Step 3 — `--all`, before and after

**Before**, at `668801ab` (unchanged tree):

```
checking 866 file(s) (every *.cpp/*.h/*.hpp under src/, include/, tests/ in <slot2> (excludes third_party/, build*/))
ok    include/client/eq.h: 899 lines (allowlisted, max 899; single class; shrinks under refactor/K_component_extraction.md; may not grow)
ok    include/client/graphics/irrlicht_renderer.h: 1343 lines (allowlisted, max 1343; single class; shrinks under refactor/L_renderer_component_extraction.md; may not grow)
checked 866 file(s): 866 ok, 0 warn, 0 fail
```
(exit 0; the two lines above are the only non-plain `ok` lines in the run.)

**After** the change — the allowlist is empty, so neither header gets a special
line any more; every one of the 866 files passes on its own line count:

```
checking 866 file(s) (every *.cpp/*.h/*.hpp under src/, include/, tests/ in <slot2> (excludes third_party/, build*/))
checked 866 file(s): 866 ok, 0 warn, 0 fail
```
(exit 0; zero `FAIL` lines, zero `WARN` lines, zero `allowlisted` lines.)

**0 FAIL, 0 WARN as predicted.** Nothing in the tree is between 1,500 and
3,000 lines — the plan's step-3 contingency does not fire.

### Files near the ceiling (reported, NOT split in this unit)

| file | lines | headroom |
|---|---|---|
| `tests/test_eq_commands_equip.cpp` | 1,492 | **8** |
| `src/client/graphics/door_manager.cpp` | 1,394 | 106 |
| `tests/test_zone_connection_flow.cpp` | 1,367 | 133 |
| `src/client/spell/spell_manager.cpp` | 1,360 | 140 |
| `include/client/graphics/irrlicht_renderer.h` | 1,343 | 157 |
| `src/client/graphics/eq/s3d_loader.cpp` | 1,322 | 178 |
| `tests/test_combat_manager.cpp` | 1,304 | 196 |

`tests/test_eq_commands_equip.cpp` is the file that will trip the gate first,
and 8 lines is roughly one test case. It grew from the 1,485 the K00 morning
survey recorded; whoever adds the next equip test should expect to split it
rather than append. Not this unit's territory.

### The new FAIL rule, proved

The rule change is only meaningful if a file between 1,500 and 3,000 now fails
where it previously warned, so it was planted and measured rather than
reasoned about.

`tests/test_eq_commands_equip.cpp` (1,492, the closest file) was copied to
`tests/m00_planted_oversize.cpp` and 200 blank lines appended — 1,692 lines:
over the 1,500 ceiling, comfortably UNDER the 3,000 hard cap, so only the new
rule can catch it.

```
checking 867 file(s) (…)
FAIL  tests/m00_planted_oversize.cpp: 1692 lines (> 1500 — the enforced ceiling; split it, or add an entry to scripts/refactor/file_size_allowlist.txt with a reason and a no-grow max)
checked 867 file(s): 866 ok, 0 warn, 1 fail
```
**exit 1.** Under the old script this same file printed `WARN` and exited 0.

The escape hatch was proved in the same session: with one temporary line
`tests/m00_planted_oversize.cpp<TAB>1700<TAB>planted M00 proof; escape hatch check`
appended to the allowlist, the identical file passes —

```
ok    tests/m00_planted_oversize.cpp: 1692 lines (allowlisted, max 1700; planted M00 proof; escape hatch check)
checked 867 file(s): 867 ok, 0 warn, 0 fail
```
**exit 0.** So the K00 no-grow ratchet is intact; emptying the list did not
remove the mechanism, only its two occupants.

**Both were then removed and the tree proved unchanged:**
`git status --porcelain` is byte-identical before and after the plant (the same
five modified territory files, no untracked file), and re-running `--all`
produces output byte-identical to the "after" run above (`diff` clean).

### Steps 1, 2 and 4 — what changed

1. `scripts/refactor/file_size_allowlist.txt`: both entries deleted. The file
   stays present, comment-only, and `--all` still parses it (the parser skips
   `#` lines and blanks — proved by the passing runs above). The comment states
   the format and that an entry needs a reason and a no-grow max.
2. `scripts/refactor/check_file_sizes.sh`: under `--all`, over-1,500-and-not-
   allowlisted moved from the `WARN` branch to a `FAIL` branch (`status=1`,
   counted in `n_fail`). The 3,000 hard cap is checked FIRST and unchanged, so
   it still catches allowlisted files too. The T37 "nothing was checked" rule
   is untouched — none of its three routes were edited. The diff/`--files`
   modes keep WARN (they do not consult the allowlist, so a future allowlisted
   file would spuriously fail there); their WARN text now says out loud that
   it is a FAIL under `--all`, which is what ctest runs. Header comment and
   `--all` usage text rewritten.
3. `docs/decisions.md`: new dated entry, 2026-09-04, at the top (newest first)
   — 1,500 is the enforced ceiling for every `*.cpp`/`*.h`/`*.hpp` under
   `src/`, `include/`, `tests/`, enforced by ctest; the allowlist is the
   reviewable exception mechanism, unchanged in shape.
4. `CLAUDE.md` §"Process requirement": the Batch J paragraph replaced with the
   J/K/L/G completion statement, the two headers' final numbers, the FAIL rule,
   and pointers to the four batch docs. `refactor/BOARD.md`: nothing struck; a
   new P entry **B053**, closed in place, records the milestone with the
   numbers and the largest survivors.

⚠ One stale sentence was left deliberately, per "strike nothing": **B024**'s
closer still says "Both headers hold a no-grow allowlist entry that each K unit
ratchets down". That is no longer true. B053 names it; whoever closes B024
should drop the sentence.

### Step 5 — ctest

- `ctest -R "FileSizeCeiling|DocCitations" --output-on-failure`: **2/2 passed**
  (`FileSizeCeiling` 9.75 s, `DocCitations` 1.28 s), exit 0. `DocCitations`
  passing is the check on this unit's own doc edits — every path cited in the
  new decisions entry, the new `CLAUDE.md` paragraph and B053 resolves.
- Full suite, `-j6`: **3759 of 3760 passed**. The sole failure is the known ALSA
  host quirk `AudioManagerTest.PreloadCommonSounds`. (Suite total is 3760 here
  vs the 3498 in `refactor/BOARD.md`'s header — the board's count predates the
  K/L/G units.)

### Bar steps skipped, with evidence

Steps 1, 3, 4 and 5 of the protocol bar (build, `build-gles2-x86.sh`, visual
gate, `build-arm-noble.sh`) were skipped: `git diff --name-only` vs the merge
base lists five files, all `.md`, `.sh` and `.txt`, and the same diff filtered
to `*.cpp *.h *.hpp *.cmake CMakeLists.txt` returns NOTHING. No compiled
artefact and no rendered pixel can differ. The ctest runs above used the
pre-existing GLES2-config `build/` tree unmodified.
