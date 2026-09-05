# T372 — survey of the two never-audited branches (B026)

Class P, slot3, read-only on the branches (no checkout of them into the
slot — use `git log`, `git diff`, `git show` against `fable`). No server.

Two branches predate the lettered programme and have never been audited:
`task/eqcpp-zoneprocess-track-b-remaining` and
`task/eqemu-zone-line-transition-scenario`. Their merge-base with `fable`
is old; "300 commits ahead" is mostly fable's own history moving on.

1. For each branch: merge-base, commits unique to the branch
   (`git log --oneline fable..<branch>`), files touched unique to the
   branch (`git diff --stat <merge-base> <branch>`), and for every file
   whether it still exists on `fable` at the same path (Batch K/L moved
   most of the client).
2. Classify each unique commit into: (a) already present on `fable` by
   another route (find the commit or unit that did it — cherry-pick
   detection with `git cherry` is a start, reading is the proof);
   (b) superseded (the code it touched no longer exists or was rewritten
   by a lettered unit — name the unit); (c) still relevant — a behaviour
   or test `fable` lacks. For (c) describe the behaviour in one sentence
   and where it would land on the current tree.
3. Recommendation per branch: DELETE (all a/b), or a list of (c) items to
   port as board entries (B130+; class by the usual rule). Do not port
   anything in this unit; do not delete the branches (the orchestrator
   does that on the ruling).
4. Review: the two tables. Strike B026 if both branches classify fully.

## Proof

Docs-only commit (the plan's Review + board). `ctest -R 'Doc'` for the
hygiene tests.

STOP: none expected.
