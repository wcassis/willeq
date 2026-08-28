# T302 — One ranked class-A list; the audit's numbering demoted to provenance

**Trigger (Will, 2026-08-28).** The board's numbered class-A list is the
2026-08-27 audit's find list — its own heading says "NEWLY SURFACED CLASS-A
DEFECTS (**not previously on this board**)". An item earned a number by being NEW
THAT DAY, not by being severe. Consequence: the worst open defect in the tree
carries no number while twelve less severe ones do, and `bridgeQueue_` sat
mis-filed as a class-C decision for two weeks when it needed no decision at all.

**Deliverable: ONE consolidated, severity-ranked class-A section in
`refactor/deferred.md`**, with the audit's numbering demoted to a provenance note
("found by the 2026-08-27 audit") rather than structure.

## Rules

- **This is bookkeeping. Change no source.** If you find a new defect while
  reading, ADD it to the list with its file:line; do not fix it.
- **Never delete a section** (standing rule for this file). Entries move by being
  re-titled and cross-referenced, and the historical entries stay where they are
  with a pointer to the consolidated list. The file is a research record; its
  history has repeatedly been load-bearing.
- **Verify every entry against the tree at the current `fable` tip before listing
  it.** Do not inherit status from the board — two entries were found stale on
  2026-08-28 (`/target <substring>` closed by T244 `a6218d6f`;
  `connectRendererCallbacks()` deleted by T241). Mark anything you find already
  closed as closed in place, with the commit.
- Rank by **severity as a user or the process actually experiences it**: memory
  corruption and data loss first, then live user-reachable wrongness, then
  dead/unreachable code and bookkeeping. State the ranking criterion at the top of
  the section so the next reader can apply it rather than guess.
- For each entry: one line of description, `file:line`, one line of user impact,
  and whether it is **executor-ready** or **blocked on Will** (and on what).

## Known input (verify, do not trust)

The twelve numbered audit items, plus these six recorded elsewhere on the board —
their scattering is the whole reason for this unit:
- `bridgeQueue_` unsynchronised cross-thread vector — `deferred.md:116` and `:154`,
  `irrlicht_renderer.cpp:305+`. **Ranks first: it is the only open item that can
  corrupt the heap rather than merely behave wrongly.** Same shape for the
  two-word mouse-delta RMW and `keyWasPressed_[]`.
- Loot panel mouse-wheel plumbing, owed by T290 — `deferred.md:133`.
- Server-forced self position changes have no non-sending internal setter —
  `deferred.md:138`.
- `IOutputRenderer::updateCurrentTargetHP` family caller-less — `:112`, `:134`.
- Shrink/grow (SPA 89) decoded, dispatched, dropped — `:57`,
  `spell_effects.cpp:883-891`.
- `collisionZOffset` uses `bboxHeight/2` not origin-to-feet — `:59`,
  `entity_renderer.cpp:602`.

Also fold in the T301 items if they are not already listed, and check whether the
stale Index at `deferred.md:105+` should be struck rather than left to mislead —
the `## STATUS` block wins and the Index is a snapshot at `e70e3347`.

## Out of scope

The first-person arc (T296-T301) is tracked separately; list its open defects like
any other, but do not re-plan that work.
