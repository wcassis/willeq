# Work items live in GitLab issues

Will's 2026-09-17 ruling (B848, issue #4): a GitLab issue is the durable
record for one work item; a hermes kanban card is an execution LEG of it;
the MR that lands the fix closes the issue. `refactor/BOARD.md` remains the
board of record for classes B and P until they migrate too; class A and C
entries there carry a `→ issue #N` pointer to their issue.

## Labels

- `class::A` live defect, `class::B` unreached or hygiene, `class::C` needs
  Will's decision, `class::P` process — the class letter from BOARD.md.
- `status::triage|ready|running|review|blocked|done` — mirrors the kanban
  columns; move it as the work moves.
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
- Migrated A/C entries in `refactor/BOARD.md` are NOT struck when their
  issue is closed — the issue is the record, the board keeps history.
