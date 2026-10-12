# B1028 (issue #348): wave-8 residue — what was done, what was left open

2026-10-12, worktree `kb-b1028-wave8-residue`, branch
`task/kb-b1028-wave8-residue`. Docs/deletion-only: no behaviour change, no
golden, no ratchet VALUE.

## What landed (one commit per card step)

| Step | Commit | Content |
|---|---|---|
| 1 | `c47f39cf` | `EntityManager::isPet` + `calculateDistance` deleted (zero callers, re-grepped on EntityManager receivers in src include tests tools benchmarks docker scripts); `check_zero_callers.py` docstring names the field-name-collision blind spot |
| 1b | `cee0f635` | cascade: the deleted method was `Entity::isPet`'s only reader — the field and its one write in `player_info.cpp` deleted with it |
| 2 | `7a5c15b1` | B602 README: the two-channel correction (B1027, #347), tip line cites, the real 14-TEST breakdown (2+2+2+1+2+1+2+2) |
| 3 | `14bee7f2` | B733 README timorous delta (+192 = 1,465,468 − 1,465,276 = 8 × 24 B), five call sites named, the five call-site comments re-pointed to `geometry_combiner.h`'s union rule, `tools/b733/*.cpp` (6 files) deleted |
| 4 | `d522c1cb` | BOARD B732 row + B733 README "The #69 (B732) row": the loader half is on fable (QA t_cbfd7f9a's censuses), the test pin / B732 / merge blockers stay open; #69 cross-note posted |
| 5 | `65050d9c` | BOARD B494 CameraController (11) struck (B533 MR !138 2026-10-11); B533 README "3 fields" → "6 fields (moveSpeed_, rotateSpeed_, mouseSensitivity_, yaw_, pitch_, orbitAngle_)" |

Bulk evidence (census outputs, inspect scripts, commit messages, the #69 note
text) is in `/home/user/kb-evidence/b1028-wave8-residue/` and attached to the
task.

## Verified at the head

- `ctest --test-dir build -L hygiene`: 29/29 (run after each commit).
- `check_zero_callers.py --check` and `check_write_only_fields.py --check`:
  exit 0 after the step-1/1b deletions.
- Every file:line cited in the README/BOARD edits re-read against the tree
  before push.
- `git diff origin/fable...HEAD` touches only the files named above; no
  `tests/goldens/`, no `src/client/graphics/CLAUDE.md`, no budget/baseline/
  ratchet VALUE.

## Not verified / left open

- CI: the push triggers the full pipeline (hygiene/build/test/gate); it must
  be green at the MR head. A red job's trace is read, not guessed.
- The #69 cross-note (work item 69, note 3948) states the B732 loader fix from
  B733's merged code; the 49 no-prefix skeletons' recovery is asserted from
  QA t_cbfd7f9a's censuses (2026-10-11, at the B733 tip), not re-measured here.
- B533 MR number (!138) verified by `glab mr view 138` and the merge commit
  `82deb60c` on origin/fable; the B494 row strike uses that citation.
- The B494 CameraController sentence in `src/client/graphics/CLAUDE.md:44`
  (the `CameraController` bullet) was NOT touched: chain `t_c54cec35` owns it
  (card instruction).
- The T479-branch text copied into BOARD.md is the historical record of
  `task/T479-skeleton-mesh-list-loaders` and was left verbatim; only the
  orchestrator note above it and the B732 row's dated annotation were updated.
