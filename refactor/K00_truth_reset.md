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

## Review — Leg B

Branch `task/K00-legB-enforce`, base tip `3808ebe9` (confirmed at checkout).

### B1 — `check_file_sizes.sh --all`

Since `--all` did not exist before this unit, "before" is the first run of
the new mode against the unmodified allowlist (i.e. against the tip with the
seeded no-grow ceilings, before any code change); this is also the pre-unit
tree-wide state the plan asked to record, since no file under `src/`,
`include/`, `tests/` was touched by this unit.

- `bash scripts/refactor/check_file_sizes.sh -C <slot> --all`
- **796 files checked: 796 ok, 0 warn, 0 fail. Exit 0.**
- WARN list: **empty.** No file besides the two allowlisted headers is over
  1,500 lines anywhere under `src/`, `include/`, `tests/` — confirms J34's
  claim for `.cpp` and extends it to `.h`/`.hpp` and the `tests/` tree.
- Allowlisted files, both `ok` (within their seeded ceiling, not WARNed):
  - `include/client/eq.h`: 2025 lines (allowlisted max 2025)
  - `include/client/graphics/irrlicht_renderer.h`: 1914 lines (allowlisted
    max 1914)
- No file over 3,000 — the STOP condition on a >3,000 finding did not fire.
- Ratchet correctness verified by temporarily lowering `eq.h`'s allowlisted
  max to 2000 (below its real 2025) and re-running `--all`: reproduced
  `FAIL include/client/eq.h: 2025 lines (> allowlisted max 2000 — no-grow
  ratchet exceeded; ...)`, exit 1, `795 ok, 0 warn, 1 fail`. Allowlist file
  restored immediately after (`git diff` on it shows the seeded values only).
- Existing modes reconfirmed unchanged: diff-mode against `fable` from the
  slot on its task branch (no `src/`/`include/`/`tests/` changes in this
  unit) prints `no C++ files to check ... nothing was checked, this is not a
  pass`, exit 0 (T37 behaviour intact); `--files <path>` still WARNs
  `include/client/eq.h` at 2025 lines exactly as before.

### B2 — `check_doc_citations.py`

Run against the tip before Leg A has merged (per the plan, `BOARD.md` is
expected absent):

- `python3 scripts/refactor/check_doc_citations.py --repo <slot>`
- **13 live docs checked, 176 citations found: 163 ok, 13 fail. Exit 1.**
- `NOTE refactor/BOARD.md not found — skipped (Leg A may not have merged
  yet)` — the documented skip-missing-input path fired correctly.
- The 13 FAILs (recorded here, not fixed — Leg A's territory):
  1. `CLAUDE.md:183` cites `review/T64/README.md` — does not exist in the
     repo tree. This is a **structural false positive**: `review/` is the
     sanctioned outside-checkout path for capture media
     (`EXECUTOR_PROTOCOL.md`), not part of the repo, so any citation into it
     will always FAIL here. Flagging for Leg A/orchestrator awareness, not a
     doc bug.
  2. `refactor/EXECUTOR_PROTOCOL.md:230` cites `review/T64/README.md` — same
     structural false positive as #1.
  3. `src/client/graphics/CLAUDE.md:129` cites
     `include/client/graphics/constrained_mesh_cache.h` — deleted T67, the
     doc's own prose says so two sentences earlier.
  4-5. Same line cites `src/client/graphics/constrained_mesh_cache.cpp`
     twice (once in prose, once inside a `git show` command) — same T67
     deletion, genuinely stale.
  6. `src/client/graphics/CLAUDE.md:249` cites `./scripts/gdb_zone_crash.sh`
     — does not exist.
  7. `docs/decisions.md:524` cites
     `include/client/graphics/ui/loot_window.h` — does not exist.
  8. `docs/decisions.md:525` cites `src/client/graphics/ui/loot_window.cpp`
     — does not exist.
  9. `docs/decisions.md:526` cites `refactor/D06b_publish_loot_vendor.md` —
     does not exist.
  10. `docs/decisions.md:758` cites `world/zoneserver.cpp:423` — this is a
      citation into `/mnt/projects/akk-stack/code` (a sibling project, named
      explicitly two lines above in the source text), not this repo. A
      second structural false-positive category: an intentional
      cross-project citation will always FAIL a repo-relative resolver.
  11. `docs/decisions.md:28` (this unit's own new B4 entry) cites
      `refactor/BOARD.md` — expected, Leg A has not merged; will resolve
      once it does.
  12. `refactor/K_component_extraction.md:70` cites
      `include/client/client_context.h` — does not exist (component not yet
      extracted).
  13. `refactor/K_component_extraction.md:147` cites
      `refactor/K01_guild_component.md` — a prospective future unit plan,
      not yet written.
- Deviation flagged for the orchestrator/Leg A: two FAIL categories
  (`review/...` paths, and the one intentional cross-project citation in
  `docs/decisions.md:758`) are **not** doc staleness — they are citations
  the checker cannot resolve by design (outside-repo by convention).
  `check_doc_citations.py` was left implementing the literal spec (repo-root
  resolution only) rather than special-casing `review/` or absolute
  cross-project paths, since the plan's territory for B2 did not ask for an
  exception list and inventing one would be guessing at Leg A's intent for
  how such citations should read. Recorded here as a finding for Will/the
  orchestrator to rule on (e.g. an explicit exemption comment convention, or
  leave the doc author to route around it).

### B3 — ctest registration

`tests/cmake/hygiene.cmake` (new) adds `FileSizeCeiling` (label `hygiene`)
running `check_file_sizes.sh -C ${CMAKE_SOURCE_DIR} --all`, and
`DocCitations` (label `hygiene`) running `check_doc_citations.py --repo
${CMAKE_SOURCE_DIR}`. Included from `tests/CMakeLists.txt` via
`INCLUDE(cmake/hygiene.cmake)`, appended after the existing
`INCLUDE(cmake/packets_skills.cmake)` line, following the file's own stated
rule ("ADD A NEW TEST TARGET TO THE FRAGMENT FOR ITS DOMAIN"). No `add_test`
of a script pre-existed under `tests/cmake/`; `tests/cmake/eqe1.cmake` (script
tests `check_sender_reachability`, `check_build_tree_orphans`) supplied the
closest precedent for `ADD_TEST(... COMMAND python3/bash <script>)`, and
`GTEST_DISCOVER_TESTS(... PROPERTIES LABELS "live")` (three sites, e.g.
`tests/cmake/zone_integration.cmake:118`) supplied the label-property
convention, applied here via `SET_TESTS_PROPERTIES(<name> PROPERTIES LABELS
"hygiene")` since these are plain `ADD_TEST` targets, not
`GTEST_DISCOVER_TESTS`.

`ctest --test-dir <slot>/build -R 'FileSizeCeiling|DocCitations'
--output-on-failure`:

```
1/2 Test #3499: FileSizeCeiling ..................   Passed    6.66 sec
2/2 Test #3500: DocCitations .....................***Failed    0.59 sec
50% tests passed, 1 tests failed out of 2
```

`DocCitations` failing here is the expected pre-Leg-A-merge state the plan
names explicitly (B3: "DocCitations may FAIL before Leg A merges ... the
orchestrator sequences the merge so Leg A lands first"); its output is the
same 13-FAIL list itemized under B2 above, plus the `BOARD.md` skip note.

### Full verification bar

1. `cmake --build <slot>/build -j24` — **exit 0.**
2. `ctest --test-dir <slot>/build -j6 --output-on-failure` — **3498/3500
   passed.** The two failures are exactly the two expected: `AudioManagerTest
   .PreloadCommonSounds` (documented ALSA host quirk) and `DocCitations`
   (expected pre-Leg-A-merge FAIL, see B2/B3 above). No other regression.
   Total test time 118.65s. Steps 3-5 (build-gles2-x86.sh,
   build-arm-noble.sh, visual gate) not run per the plan's narrowed
   verification bar for Leg B (no code under `src/` changed).

### Deviations

- None from the territory list: touched exactly
  `scripts/refactor/check_file_sizes.sh` (modified, new `--all` mode added,
  existing modes' behaviour reconfirmed unchanged),
  `scripts/refactor/file_size_allowlist.txt` (new, two seeded entries at
  measured current line counts), `scripts/refactor/check_doc_citations.py`
  (new), `tests/cmake/hygiene.cmake` (new, one file), `tests/CMakeLists.txt`
  (one include line), `docs/decisions.md` (one new dated entry). `git status
  --porcelain` at finish: exactly those six paths.
- `check_doc_citations.py`'s citation-with-line extension set was widened
  slightly beyond the plan's bare-path list (`.cpp|.h|.py|.sh|.md`) to also
  accept `.hpp` and `.cmake` for the *with-line-number* form only — the
  bare-path-no-line FAIL rule stayed exactly as specified. Called out in
  case Will wants the with-line set narrowed to match exactly; nothing in
  the live docs currently exercises the difference (no `.hpp:NNN` or
  `.cmake:NNN` citation was found in this scan).
- No STOP condition fired: no file over 3,000 lines under `--all`.
