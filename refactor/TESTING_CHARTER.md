# Testing Campaign Charter

Authorized by Will 2026-07-28: a dedicated Claude session runs a
test-coverage campaign over WillEQ's base layers, in parallel with the
perf/renderer campaign. This charter is the contract between the two
sessions; both treat it as law. Changes to it require Will.

## Mission

Raise real test coverage of the base layers — network protocol,
login/world/zone connection flow, game state (EventBus, state modules),
config parsing, remaining tools — with deterministic, fast, honest
tests. Coverage numbers guide priorities; passing tests that assert
real behavior are the deliverable, not coverage theater.

## Session mechanics

- Home checkout: `/mnt/projects/fable/testing` (worktree, branch
  `testing-home`; fast-forward it from fable via `git merge fable`
  when starting work). Executor slots: `/mnt/projects/fable/wt/t1`,
  `/mnt/projects/fable/wt/t2` (worktrees of the same repo).
  Slots `wt/slot1..3` belong to the perf session — never use them.
- Unit naming `T##`; branches `task/T##-<name>`. Plan files
  `refactor/T##_<name>.md`, same structure as P-units.
- Executors follow `refactor/EXECUTOR_PROTOCOL.md` exactly (checkout-
  first, shell discipline, no monitor-parking, finish protocol). Where
  that file says slot1..3, read your own t1/t2.
- Committing to fable (plans and merges) happens via the canonical
  checkout `/mnt/projects/fable/main`: verify `git -C
  /mnt/projects/fable/main status --short` is CLEAN first (if dirty,
  the other session is mid-operation — wait, never fix up), then do
  the atomic add/commit or `merge --no-ff`, and leave it clean.
  Always operate on the current fable tip; both sessions already
  tolerate "fable moved" with non-FF merges.

## Territory

- ALLOWED to create/modify: `tests/**`, test-only CMake target blocks,
  `tools/` test fixtures, `benchmarks/` test data, this charter's
  progress tracker (`refactor/testing_progress.md`).
- Base-layer SOURCE (src/common/**, non-graphics src/client modules,
  include/ equivalents): read freely; modify ONLY for a testability
  seam that is minimal-diff, behavior-preserving, and flagged
  prominently in the unit Review. When in doubt, don't — file it in
  the progress tracker as "needs seam" instead.
- OFF-LIMITS entirely: `src/client/graphics/**`,
  `include/client/graphics/**`, `docker/**` (driver), goldens
  (`tests/goldens/**`), `config/constrained_presets.json`, anything
  named as in-flight in an uncommitted-unit plan on fable or in
  `refactor/deferred.md`'s active items. The perf session owns those.
- The Pi is NEVER yours. eqemu: prefer the synthetic/replay fixtures
  (session-replay oracle `docs/plans/session_replay_oracle.md`,
  pcap tests, recorded captures) — they exist precisely so protocol
  tests don't need a live server. If a unit genuinely needs live
  eqemu, take the lock first: create
  `/mnt/projects/fable/.eqemu-lock` containing your session name +
  date; remove it at server shutdown. If the lock exists, wait. The
  perf session honors the same lock.

## Test quality bar

- Deterministic and fast (target <5s per test binary where feasible);
  anything soak/stress-shaped goes in `tools/` as a manual harness
  (precedent: `tools/particle_race_stress.cpp`), not ctest.
- No new flaky tests: 3 consecutive full-suite green runs before a
  unit merges (known documented exceptions excepted:
  AudioManagerTest.PreloadCommonSounds [host ALSA],
  RDPServerFrameTest.* [parallel-run flake family]). A test you can't
  make deterministic gets redesigned or dropped, not retried in CI.
- Tests assert behavior, not implementation details, so base-layer
  refactors don't shed tests. Binary protocol layouts (packet structs)
  are the exception — layout IS the behavior there.
- Every new test binary must build in BOTH trees (desktop `build/` and
  GLES2 `EQT_GLES2_IRRLICHT_DIR` tree) or link no Irrlicht at all
  (preferred for base layers). The GLES2-only migration
  (refactor/desktop_gl_elimination_plan.md) is in progress — don't add
  new desktop-GL dependencies.
- Verification bar for merges: full ctest both trees green (minus
  documented exceptions) + the 3x-consecutive rule above. Graphics
  bar steps (visual gate, ARM build) only when a unit touches
  something they cover — for tests/-only diffs, desktop+GLES2 ctest
  is the bar; run `bash scripts/build-arm-noble.sh` only if shared
  source was touched (seam case).

## Coordination

- Progress lives in `refactor/testing_progress.md` (create in T01):
  per-layer coverage numbers, unit log, gap backlog, "needs seam"
  list. The perf session reads it; it reads `refactor/deferred.md`
  before every batch to refresh the off-limits set.
- Findings that belong to the perf campaign (a graphics bug, a perf
  smell) go in the progress tracker under "handoffs", not fixed.
- Will is the shared boss: report to him the same way — outcome first,
  honest failures, no self-declared acceptability on anything visual
  (you shouldn't have visual work at all; if you somehow do, STOP).

## First unit (T01) — coverage survey

Stand up a coverage build (gcov/lcov or llvm-cov on a scratch
configure; do NOT commit build-system changes unless cleanly optional
behind a flag), measure line/branch coverage per base layer
(src/common/net, packet structs, login/world/zone connection flow,
state/EventBus, config parsing, spell/skill/combat non-graphics logic),
and produce `refactor/testing_progress.md` with the numbers, a ranked
gap backlog (thinnest+highest-risk first), and the proposed first
batch of T-units. Report-only; the backlog is Will's to green-light.
