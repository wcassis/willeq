# Local-model kanban cards: authoring and decomposition rules

These are RULES for whoever writes, splits or merges hermes kanban cards run by
local models (the dev, QA and review profiles on the willeq board): the
orchestrator, the solo-orchestrator cron, and any card generator. Each rule
cites the wave-7 retrospective event that motivated it as `RETRO-WAVE7 id N`,
the row id in `review/RETRO-WAVE7/README.md` (full rows in its
`failures.tsv`). The retrospective is the evidence; this file is the rule. Board
and issue mechanics are in `docs/issues.md`; CI behaviour in `docs/ci.md`.

## 1. Decomposition: one card, one kind of work

- **Prove-then-fix is TWO cards.** Card 1, PROVE: a red ctest, the mechanism
  it demonstrates, and a golden-impact census of the proposed change, all
  pushed, with NO fix. Card 2, FIX: the change, written against the mechanism
  card 1 proved. A card whose acceptance says "if it changes goldens, STOP"
  runs its golden census in card 1, so the STOP is decided before any fix
  exists. (RETRO-WAVE7 id 22: a one-card prove-then-fix hit the iteration cap
  with the design "designed, not yet implemented".)
- **A new tool or test AND a live or Pi leg is TWO cards.** Card 1 builds the
  tool and proves it (ctest green, pushed); card 2 runs the live/Pi leg
  against card 1's pushed head. (RETRO-WAVE7 id 1: five steps in one card,
  timed out with steps 1-2 done but uncommitted.)
- **Workers commit after every numbered step** (commit, not necessarily push),
  so a timeout or crash loses nothing. Card bodies number their steps so this
  is checkable. (RETRO-WAVE7 id 1.)
- Serialise cards that share a lane (eqemu lane, Pi, files) with
  `kanban link`; that held cleanly in wave 7 and stays the rule.

## 2. Card bodies: what the author checks before writing

- **Pre-answer the root question.** If the cause is already measured, the
  body states it with its evidence (file, version, line, command output).
  Bodies that did this finished in one dev run (RETRO-WAVE7 "What went well":
  B930, B339).
- **Name the LOCAL regression net**, not only the CI job. For any loader or
  model-builder change that is `ctest --test-dir build -R EntitySourcePruning`,
  byte-identical except the lines the body names as intended; other areas name
  their own golden or ctest. (RETRO-WAVE7 id 23: the card named the CI gate
  job, and the regression surfaced only in two red pipelines.)
- **Check a site-specific criterion before writing it.** Before "reject X at
  <site>", `git grep` the tests and the shipped assets for states that
  legitimately reach <site>. If any exist, state the criterion as the
  observable (for example "one FATAL line naming pack path and version") and
  leave the site to the worker. (RETRO-WAVE7 id 8.)
- **Counts in a body are measured or omitted.** Quote the measuring command's
  output (for example `scripts/refactor/check_write_only_fields.py --list`), or
  leave the number out. (RETRO-WAVE7 id 41: "twelve" members, the worker
  deleted 14.)
- **Golden changes are scoped.** The body names which goldens may change and
  why; any other golden line that moves is a regression, not a bless question
  (RETRO-WAVE7 ids 24, 25).

## 3. What workers must and must not do (stated in every body or template)

- **Never edit any CLAUDE.md.** CLAUDE.md files are protected in headless
  runs and the approval times out. Put the exact replacement lines under a
  `CLAUDE.md EDITS` heading in the completion summary; the orchestrator
  applies them before merging, or notes on the hand-off issue that they are
  owed. Never route around a protected-file or approval guard (python, sed,
  execute_code, terminal writes): a bypass is a FAIL. (RETRO-WAVE7 ids 7, 27,
  29, 30.)
- **Never create kanban cards.** Put follow-ups under a `FOLLOW-UP` heading
  in the summary; the orchestrator cuts them with the chain's model and
  workspace and links them as parents of the review card, so the review grades
  the follow-up too. (RETRO-WAVE7 ids 10, 11, 14, 29.)
- **Never rebuild the shared Irrlicht prefix** (the main checkout's
  `third_party/irrlicht-gles2-x86` that worker worktrees configure against).
  Wave setup rebuilds it from origin/fable at wave start and after any merge
  touching `docker/irrlicht-drm/`. A worker that finds it stale (a missing
  symbol at link) comments on its card and builds a worktree-local prefix
  instead: `bash scripts/build-gles2-x86.sh --irrlicht-only` from its own
  worktree (the script writes under that checkout's own `third_party/`), then
  configures with `-DEQT_GLES2_IRRLICHT_DIR` pointing at it.
  (RETRO-WAVE7 ids 17, 28, 34.)
- **Evidence rules.** Every derived number shows its arithmetic inline
  (`8 B = 280 - 272`); a number quoted from another unit is re-derived from
  that unit's raw file; test counts come only from the head's CI test-job
  trace, with the job id. (RETRO-WAVE7 ids 14, 19, 35.)
- **Citations carry anchors.** Cite a line as `` `<file>:LINE` (`token`) ``
  and re-measure after the last commit; `DocCitations` fails an anchor that is
  not on its line (`docs/ci.md`, "Anchored citations"; RETRO-WAVE7 ids 5, 37).
- **Before every push**: `ctest --test-dir build -L hygiene` all green, and
  every line removed in `git diff origin/fable...HEAD` is one the worker meant
  to remove. (RETRO-WAVE7 ids 2, 3, 18.)
- **Shas are copied, never retyped**: from `git rev-parse HEAD` output.
  (RETRO-WAVE7 ids 4, 9.)
- **Board verbs**: finish with `kanban complete`; a FAIL is `complete` with
  `Verdict: FAIL` as the first summary line; `block` only for a Will ruling or
  bless; never `request-review`. (RETRO-WAVE7 ids 33, 38.)
- **One plain command per terminal call**; multi-step logic goes in a script
  file that is run with `bash <file>`. (RETRO-WAVE7 id 42.)

## 4. Verdicts and merges

- **QA and review verdicts name the FULL 40-character head sha** they graded,
  copied from `git rev-parse HEAD`.
- **Merge only on PASS at the current head.** The latest QA and review
  verdicts in the chain, including every follow-up and fix card, are PASS and
  name the CURRENT head sha. If the head moved after the review's sha, cut a
  narrow re-review on the delta first. A review or re-review card that is
  running or blocked means no merge. (RETRO-WAVE7 ids 11, 31, 40: three of the
  wave's nine merges landed a head no reviewer graded.)
- Fix-chain dev cards carry the chain's original `--model` / `--provider`
  (RETRO-WAVE7 id 21).
- A worker crash reporting "session storage was busy" is hermes runtime
  contention, not a card fault: unblock with a comment; it does not count
  toward max-retries (RETRO-WAVE7 ids 12, 13).

## 5. The retrospective loop (standing process)

After EVERY wave, before the next one is dispatched:

1. **Classify every event.** Every block, crash, timeout, FAIL verdict,
   retry, red pipeline at a pushed head and extra fix/follow-up card gets one
   row with its evidence (card id, run, pipeline/job, log line) and ONE cause
   bucket: DECOMPOSITION, CARD-BODY, TEMPLATE, SOLO-PROMPT, WORKER-ENV,
   REPO-RULE, INFRA, MODEL-LIMIT, LEGITIMATE (a correct block, e.g. a bless),
   or BENIGN (not counted, e.g. a pipeline cancelled by a newer push). The
   wave-7 table is the format: `review/RETRO-WAVE7/failures.tsv`.
2. **Fix the cause where it lives**, not the instance: the card template or
   generator, the solo-orchestrator prompt, this file, CLAUDE.md, a hygiene
   check, CI config, or hermes config. MODEL-LIMIT rows get a rule that makes
   the miss mechanically checkable (an inline-arithmetic rule, a sha-copy
   rule), not a note to "be careful".
3. **Record the counts** per bucket, plus the wall-clock cost of the largest
   items, in the wave's retrospective README, so the next wave is measured
   against them. A bucket that does not fall after its fix landed means the
   fix was wrong; say so in the next retrospective.
4. Keep the "what went well" list too; a rule that worked is not removed
   because it is no longer firing.

Write-ups go to `review/RETRO-WAVE<N>/` (README plus `failures.tsv`), outside
the checkout, per the review-media convention in `refactor/EXECUTOR_PROTOCOL.md`.
