# K00 — Truth reset: one board, enforced ceilings, docs that say what the tree says

**Why this unit exists (Will, 2026-09-03).** Stale documentation is corrupting
task scoping and dependency tracking. `refactor/deferred.md` is 10,409 lines
with an EMPTY `## STATUS` section that `CLAUDE.md` tells every reader to start
at; its index admits it is "two removes from current"; its item numbers run
35, 44, 45, 36. `refactor/` holds 477 markdown files and 160,000 lines.
`check_file_sizes.sh` checks only files changed vs `fable`, so nothing enforces
the ceiling once a unit merges. Executors (and the orchestrator's own memory)
have been scoping off all of this. This unit replaces it with something a
small model can read in one sitting and a ctest that keeps it honest.

Standing context: this project is private and single-user (Will). There is no
user base, no production, no deployment. Do not write any of those words into
any doc this unit touches.

Two legs, non-overlapping territory, run in parallel slots.

---

## Leg A — the board (slot1, branch `task/K00-legA-board`)

**Territory:** `refactor/BOARD.md` (new), `refactor/deferred.md` (moves),
`refactor/progress.md` (moves), `refactor/testing_progress.md` (moves),
`refactor/completed/` (additions), `CLAUDE.md`, `docs/CLAUDE.md`,
`docs/module_map.md`, `include/client/state/CLAUDE.md`,
`include/client/bridge/CLAUDE.md`, `src/client/input/CLAUDE.md`,
`src/client/graphics/CLAUDE.md`, `include/client/graphics/ui/CLAUDE.md`,
`README.md`. Nothing under `src/`, `include/`, `tests/`, `scripts/`.

### A1. Build `refactor/BOARD.md`

One entry per OPEN item, nothing else. Format, one item per bullet:

```
- **B001** (A) `bridge/renderer_event_receiver.cpp` `RendererEventReceiver` —
  one-sentence defect. Closes when: one sentence. Prov: deferred item 32.
```

- Stable IDs `B001..`, assigned in severity order A → B → C → process. The old
  deferred item number goes in `Prov:` and nowhere else.
- Anchor by **file and function/class name, never a line number** (J21 found
  line citations rot within a unit).
- Class letters (define them at the top of the board): A live defect, B
  implemented-but-unreached / hygiene with a code change, C needs Will's
  design decision, P process/doc).
- Source every candidate from: "⚠ THE ONE CLASS-A LIST" (items 1–45 and the
  addenda), "SERVER-SENT PLACEABLES AND DOORS ARE UNMEASURED", the "⚠ AUDIT
  2026-08-27" bullets, every non-struck entry further down, and the "Handoffs
  to the perf session" section of `refactor/testing_progress.md`.
- **An item is on the board only if you confirmed it against the tree at your
  branch tip.** For each candidate run a grep or read that shows the defect /
  gap still exists, and record that command and its one-line result in the
  Review table (NOT on the board). If the anchor is gone or the behaviour is
  already fixed, it is closed: list it under "Closed by this audit" in the
  Review with the commit that closed it if `git log -S` finds one.
- Items Will has explicitly ruled on are not "awaiting Will". Check
  `docs/decisions.md` and the J tracker's "WILL'S RULING" before writing
  class C.
- Add these items, which the 2026-09-03 survey established and no doc carries:
  - (P) Component extraction of `class EverQuest` and `class IrrlichtRenderer`
    is the open structural programme — `refactor/K_component_extraction.md`.
  - (B) `src/client/eq_ucs.cpp` is 320 lines of commented-out code; T297
    proved UCS is not needed by Titanium; delete it and the UCS connect path
    (deferred item 4 becomes a delete, not a fix).
  - (B) Eight vestigial `irrlicht_renderer.h` includes in game-logic TUs, the
    `InitGraphics(IrrlichtRenderer*, …)` parameter, and
    `door_manager.cpp`'s `EverQuest::GetDebugLevel()` — the S2 leftovers.
  - (P) The eight 546–880-line methods listed under "Optional, NOT part of
    this batch — the Batch-G follow-up" in `refactor/J_file_size_reclamation.md`.
- Top of the board: a 6-line header — tip hash, date, suite count from the
  last merged unit's Review (name it), and the sentence "Items are confirmed
  against the tree at the tip above; nothing here is quoted from an older
  board."
- Target: under 250 lines. If it is longer, you have copied prose instead of
  writing one sentence.

### A2. Archive, do not delete

- `git mv refactor/deferred.md refactor/completed/deferred_archive_20260903.md`
- `git mv refactor/progress.md refactor/completed/progress_archive_20260903.md`
- `git mv refactor/testing_progress.md refactor/completed/testing_progress_archive_20260903.md`
- Prepend each with a 3-line banner: archived 2026-09-03 by K00; superseded by
  `refactor/BOARD.md`; findings here are historical, do not scope off them.
- Grep the whole tree (`*.md`, `*.py`, `*.sh`, `*.cmake`, `*.cpp`, `*.h`) for
  `deferred.md`, `testing_progress.md`, `progress.md`. Repoint every
  reference in a LIVE doc (the files in your territory) to `BOARD.md`. If any
  script or test reads one of these files as text, STOP and report — that is
  outside your territory.

### A3. Rewrite the orientation

- `CLAUDE.md`: replace the "Orientation" and "Process requirement" sections.
  Order of reading for a new agent: `refactor/BOARD.md` →
  `docs/module_map.md` → `docs/decisions.md` → the unit plan. Remove every
  mention of a `## STATUS` block, of `F_file_split.md`, and the stale suite
  count sentence in "Build Commands" (replace with "run it; the sole expected
  failure is the ALSA quirk"). Add one paragraph: Batch J is complete (every
  `.cpp` under 1,500; `eq.h` and `irrlicht_renderer.h` carry a no-grow
  allowlist entry pending `refactor/K_component_extraction.md`). Keep every
  build/bootstrap command verbatim.
- `README.md`: its newest date is 2026-07-28. Do not rewrite it. Add a
  dated "Status" paragraph at the top pointing at `BOARD.md` and
  `module_map.md`, and delete any sentence you can show is false at the tip
  (list each deletion in the Review with the evidence).
- Each subdirectory `CLAUDE.md`: read it against the tree. Fix or delete
  sentences that are false (counts, file names, "TODO" claims). List each
  change in the Review with the evidence. Do not add prose.
- `docs/module_map.md`: add a row block for the flat `src/client/eq_*.cpp`
  family table from `refactor/K_component_extraction.md` §Families so the two
  agree.

### STOP conditions (Leg A)

- Any candidate item whose confirmation needs a build or a live run: board
  it as "unconfirmed, needs <what>" rather than guessing, and say so in the
  Review.
- Do not fix any item. Do not touch code.
- If the board exceeds 400 lines, stop and report the count; something is
  wrong with the method.

---

## Leg B — enforcement (slot2, branch `task/K00-legB-enforce`)

**Territory:** `scripts/refactor/check_file_sizes.sh`,
`scripts/refactor/file_size_allowlist.txt` (new),
`scripts/refactor/check_doc_citations.py` (new), `tests/cmake/` (one new
file), `tests/CMakeLists.txt` (one include line), `docs/decisions.md` (one
new dated decision entry). Nothing else.

### B1. `check_file_sizes.sh --all`

- New mode `--all`: check every `*.cpp`, `*.h`, `*.hpp` under `src/`,
  `include/`, `tests/` (exclude `third_party/`, `build*/`). Existing modes
  unchanged.
- Allowlist file `scripts/refactor/file_size_allowlist.txt`, format
  `path<TAB>max_lines<TAB>reason`. Seed with exactly two entries at their
  CURRENT line counts (measure, do not copy: `wc -l`):
  `include/client/eq.h` and `include/client/graphics/irrlicht_renderer.h`,
  reason "single class; shrinks under refactor/K_component_extraction.md; may
  not grow".
- Semantics under `--all`: FAIL if any file is over 3,000; FAIL if an
  allowlisted file exceeds its `max_lines` (that is the no-grow ratchet);
  WARN for any other file over 1,500; exit non-zero on any FAIL; print the
  count of files checked (never a silent pass, keep the T37 rule).
- Measure first: run `--all` on the tip before you change anything and record
  the WARN list in the Review. Expected: `eq.h`, `irrlicht_renderer.h`
  allowlisted; nothing else over 1,500 (J34 claims every `.cpp` is under; a
  test file may not be — if one is, WARN, do not FAIL, and list it).

### B2. `check_doc_citations.py`

- Walks ONLY the live docs: root `CLAUDE.md`, every `*/CLAUDE.md`,
  `docs/*.md` (not `docs/reference/`), `refactor/BOARD.md`,
  `refactor/EXECUTOR_PROTOCOL.md`, `refactor/K_*.md`. Not `refactor/completed/`,
  not the T/J/P plan files.
- Finds every `path/to/file.ext:NNN` citation. Reports: file does not exist
  (FAIL); line NNN beyond the file's length (FAIL); otherwise OK. Prints counts.
- Also FAIL on a citation of a path that does not exist even without a line
  number, when the path contains a `/` and ends in `.cpp|.h|.py|.sh|.md`.
- Run it on the tip first; record what it finds in the Review. Do not fix the
  docs it flags — that is Leg A's territory. If Leg A has not merged when you
  run it, `BOARD.md` will not exist; skip missing inputs with a printed note.

### B3. Register both in ctest

- One new `tests/cmake/hygiene.cmake` adding two tests: `FileSizeCeiling`
  runs `check_file_sizes.sh --all`; `DocCitations` runs
  `check_doc_citations.py`. Label both `hygiene`. Include it from
  `tests/CMakeLists.txt` the same way the other `tests/cmake/*.cmake` files
  are included (grep for the pattern; do not cite a line number).
- Confirm with `ctest --test-dir <slot>/build -R 'FileSizeCeiling|DocCitations'
  --output-on-failure` that both run and pass (DocCitations may FAIL before
  Leg A merges — if so, record the failing citations and mark the test with
  the `hygiene` label only; the orchestrator sequences the merge so Leg A
  lands first).

### B4. Decision entry

- Append to `docs/decisions.md`, dated 2026-09-03: the ceiling is enforced
  tree-wide by ctest; single-class headers are allowlisted with a no-grow
  maximum that each K unit ratchets down; live docs must have resolvable
  citations. Five lines.

### STOP conditions (Leg B)

- If `--all` finds a file over 3,000 lines, STOP and report — nothing should
  be, and it means the J close-out is wrong.
- Do not edit any file outside the territory to make a test pass.

---

## Verification bar

Protocol bar, narrowed: Leg A is docs-only — build + ctest need not be re-run
but `git diff --stat` must show no `src/`, `include/`, `tests/`, `scripts/`
change. Leg B: full bar steps 1, 2 and 6 (using the new `--all`); steps 3–5
not needed (no code under `src/` changes).

## Review

(filled by each executor: per-item confirmation table for Leg A; before/after
checker output for Leg B; deviations.)
