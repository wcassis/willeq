# Executor Prompt Template — Batch F unit

Orchestrator: fill placeholders, spawn as a Sonnet agent. One unit per agent.

---

You are executing a mechanical refactoring unit in the WillEQ codebase.
Work ONLY inside the worktree at `{SLOT_PATH}`. Do not touch
`/mnt/projects/fable/main` or any sibling slot.

Setup:
1. In `{SLOT_PATH}`: `git checkout -b task/{UNIT_ID}-{UNIT_NAME} fable`
2. Read the unit plan: `{SLOT_PATH}/refactor/{UNIT_FILE}` — it lists the exact
   methods to move, target file(s), and acceptance criteria. Also read
   `docs/decisions.md` (standing rules) and the relevant rows of
   `docs/module_map.md`.

Rules:
- Execute the plan EXACTLY. No scope creep, no drive-by fixes, no reformatting
  of moved code. Method bodies move verbatim.
- Headers are untouched unless the plan says otherwise. If a file-local
  `static` helper must become visible to moved code, note it in the review
  section — the verifier will flag it and that is expected.
- Add each new .cpp file to CMakeLists.txt.
- Shell discipline: never chain commands with `&&` or `;`; never `cd` — pass
  paths as arguments; never pipe build output through grep/tail.
- NEVER end your turn waiting on a background task or monitor — this stalls
  the whole pipeline until the orchestrator manually resumes you (it has
  happened repeatedly). Run long commands in the FOREGROUND with explicit
  timeouts (600000 ms) and re-run on timeout (builds resume incrementally).
  If you must wait on a condition, use a bounded until-loop inside a single
  foreground call. If you catch yourself about to say "waiting for the
  background task" — don't; check its output file directly and continue.

Verification (all must pass before you finish):
1. `cmake --build {SLOT_PATH}/build -j24` — clean build, do not pipe/filter output
2. `ctest --test-dir {SLOT_PATH}/build --output-on-failure`
3. `python3 {SLOT_PATH}/scripts/refactor/verify_move_only.py fable..HEAD --repo {SLOT_PATH}`
4. `bash {SLOT_PATH}/scripts/refactor/check_file_sizes.sh fable`

Finish:
- Write the review section at the bottom of the unit plan file: each plan step
  checked, deviations listed, verifier output summarized (paste substantive
  lines if any, with justification).
- Commit on the task branch with message "{UNIT_ID}: {UNIT_TITLE}" and the
  standard Co-Authored-By trailer. Do NOT merge, do NOT push, do NOT delete
  the branch.
- Your final report: unit ID, branch name, commit hash, build/test/verifier
  results (pass/fail each), and the full list of deviations. If you could not
  complete a step, say exactly where you stopped and why — do not improvise
  around a blocking problem.
