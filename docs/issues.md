# Work items live in GitLab issues

Will's 2026-09-17 ruling (B848, issue #4): a GitLab issue is the durable
record for one work item; a hermes kanban card is an execution LEG of it;
the MR that lands the fix closes the issue. `refactor/BOARD.md` remains the
board of record for classes B and P until they migrate too; class A and C
entries there carry a `→ issue #N` pointer to their issue. The live A/C count
is not quoted here (it drifts with every new classed entry) --
`scripts/gitlab/board_to_issues.py --dry-run` prints it and its red
cross-check asserts the parser's count matches a format-agnostic recount of
the same bullets, so a format variant that drops an entry fails the run before
it can be left without an issue.

## Idempotency (B849)

The script is the one way in, and re-running it never creates a second issue
for a work item, in EITHER direction:

- it dedups against issues in **all states** -- an issue titled `B###:` that
  is OPEN or CLOSED both stop a re-create (before B849 it read `state=opened`
  only, and closing a migrated issue then re-running the script created it
  again);
- it honours the entry's own `→ issue #N` pointer, so a re-create is stopped
  even if the issue listing could not be read;
- an unstruck entry whose own header says the work is done (`fixed in <unit>`,
  `CLOSED by <unit>`, `FIXED by …`) is printed as an `ANOMALY:` line and NOT
  created -- the check reaches no further than the header line and the first
  sentence; verify the fix on `fable`, close the record, strike the entry.

Its regression net is the `BoardToIssuesHygiene` ctest test (label `hygiene`,
`scripts/gitlab/test_board_to_issues.py`, no network).

## Labels

- `class::A` live defect, `class::B` unreached or hygiene, `class::C` needs
  Will's decision, `class::P` process — the class letter from BOARD.md.
- `status::triage|ready|running|review|blocked|done` — mirrors the kanban
  columns; move it as the work moves. Every issue the migration script
  creates carries `status::triage` from the start (B849);
  `--label-missing-status` backfills the label on open `board-migrated`
  issues that carry no `status::` label at all.
- `lane::offline` no outside dependency, `lane::vm` needs the eqemu VM /
  the scenario lane / its lock, `lane::pi` needs the Orange Pi,
  `lane::oracle` needs the official client's own behaviour as ground truth.
- `kanban` — an execution leg exists or is expected on the hermes board.
- `board-migrated` — the entry started life in `refactor/BOARD.md`.

## Conventions

- One issue per work item; title `B###: <first sentence>`. The issue body is
  the entry's full text — amendments land in the issue, not the board entry.
- A kanban card's completion summary names the issue; the MR description
  starts `Closes #<issue>`, so GitLab closes it on merge.
- File a new work item by opening an issue with a `class::` and `lane::`
  label. To turn it into work, create the kanban card (or ask for one)
  referencing the issue.
- Migrated A/C entries in `refactor/BOARD.md` are struck when their issue is
  closed — the strike names the closing unit and (for a fixed defect) the
  evidence on `fable`, and the board keeps the struck entry as history
  (B849: a closed issue behind an unstruck entry is a skip, never a
  re-create).
