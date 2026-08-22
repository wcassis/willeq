# T175 — a build-tree hygiene gate: fail when a bin/ binary has no CMake target

**Slot: any. x86. No Pi, no server, no display** — a pure check over a
configured build tree. Acceptance is measured by planting a fake orphan and
watching the gate bite.

**Territory:** `scripts/ci/check_build_tree_orphans.py` (new),
`tests/CMakeLists.txt` (one `ADD_TEST` + `SKIP_RETURN_CODE`), and this doc.

## The recurrence risk

On 2026-08-14 the build tree carried four STALE binaries whose CMake targets
**and** source files had both been deleted — `test_gl_points`,
`test_eq_commands_attack`, `test_eq_commands_assist`, `test_eq_commands_vendor`.
They still ran and still passed under `ctest`, exercising code that had been
gone for a week. They were removed by hand, and nothing stopped it happening
again.

The mechanism is unglamorous: **cmake never prunes `bin/`.** Delete a target
from `CMakeLists.txt`, reconfigure, rebuild — cmake stops building that target,
but the executable it last produced stays in `build/bin` untouched. A leftover
binary is not "an old test still passing"; it is a binary pinned to a source
tree that no longer exists. It cannot be rebuilt, so it can never fail on a real
regression, and it silently pads the green count. `ctest` will happily keep
running it because `GTEST_DISCOVER_TESTS` registered it before the target was
deleted — or, worse, because someone registered it by hand and forgot.

## The check

`scripts/ci/check_build_tree_orphans.py` compares the files actually present in
`build/bin` against the set of artifacts the **current** CMake configuration
says it produces there. Any regular file in `bin/` that no target claims is an
orphan; the script names it and exits non-zero.

It is a pure check — no server, no display, no network. It inspects whichever
build tree it is pointed at (argv[1], else `<repo>/build`); the ctest entry
passes `${CMAKE_BINARY_DIR}` so it always examines the exact tree ctest runs in.
When there is no configured tree (`CMakeCache.txt` absent) or no `bin/` yet, it
prints a clear message and exits **125**, which the `SKIP_RETURN_CODE 125`
property turns into a clean ctest SKIP — the same convention `visual_gate` and
`test_eqe1_totalestimate_accounting` already use in this file. Exit 1 is an
orphan; exit 2 is a genuine environment error (never a silent pass).

Exit-code convention mirrors `check_sender_reachability.py` (T168): 0 clean, 1
the defect, 2 environment error — with 125=SKIP added because, unlike a pure
source scan, this check's input (a configured build tree) may legitimately be
absent.

## Why the CMake file API, and not the alternatives

The authoritative target list comes from CMake's **file API** (`codemodel-v2`,
under `build/.cmake/api/`). Each target's reply records its on-disk
`artifacts[].path`; every artifact whose path is under `bin/` is "claimed", by
basename. The three candidates the unit brief named, and why this one won:

- **`cmake --build … --target help`** lists target *names* only, in a
  generator-specific text format, mixed with utility/phony targets, and with **no
  artifact path**. It cannot tell you which file a target lands in `bin/` once
  `OUTPUT_NAME` or a per-config suffix is in play, and it cannot distinguish an
  executable target from a custom command. Name-matching against it would work by
  luck in *this* repo (no `OUTPUT_NAME` anywhere today) and break the day one
  appears.
- **`CMakeCache.txt`** has no target list at all — it is cache variables.
- **The file API** is the versioned, generator-independent, machine-readable
  contract CMake maintains precisely for tooling. It maps each target to its
  exact artifact path. CMake regenerates the reply on every reconfigure, so it
  tracks the current `CMakeLists.txt` whenever the tree is built — and the
  deleted-target case this gate exists for regenerates the reply *without* that
  target, leaving its abandoned binary correctly unclaimed. That is the whole
  ballgame, so the file API is the robust choice.

**Bootstrapping the reply.** The file API only writes a reply if a query exists.
The script writes the stateless query file
(`.cmake/api/v1/query/codemodel-v2`, idempotent) so every future reconfigure
emits a reply, and if a tree was configured before the query existed and has no
reply yet, it runs `cmake <build>` once (local, offline, no compile) to produce
one, then reads it. Both paths were exercised (see below).

**Authority is the current configuration.** The claimed set reflects this
tree's present config. A binary left from a since-disabled feature — say a
graphics test after a reconfigure to `-DEQT_GRAPHICS=OFF` dropped its target —
is reported. That is intended: at that point it *is* a stale binary in the tree,
the same hygiene concern the gate exists for. (In this repo the graphics test
executables are defined as targets regardless of `EQT_GRAPHICS`, "which is what
keeps them compiling" per `CMakeLists.txt`, so an `EQT_GRAPHICS=OFF` configure
still claims all 112 bin artifacts and does not false-positive on them.)

## Acceptance — measured proof it bites

Build tree configured with `-DEQT_GRAPHICS=OFF` (no display on this box);
`test_captured_packets` built into `bin/`. Codemodel reports **112** claimed
bin artifacts.

| step | command | result |
|---|---|---|
| clean | `python3 check_build_tree_orphans.py` | `PASS`, exit **0** |
| plant orphan | `cp bin/test_captured_packets bin/test_bogus_orphan` then run | `FAIL … test_bogus_orphan`, exit **1** |
| remove orphan | `rm bin/test_bogus_orphan` then run | `PASS`, exit **0** |
| no build tree | run against a nonexistent dir | `SKIP … no CMakeCache.txt`, exit **125** |
| no reply yet | `rm -rf …/reply` then run | reconfigures, then `PASS`, exit **0** |

Through ctest itself:

- `ctest -R check_build_tree_orphans` on the clean tree — **Passed**, 1/1.
- Same with `test_bogus_orphan` planted — **Failed**, 1/1, the output naming
  `test_bogus_orphan`. Orphan then removed; tree clean.

The failing-with-orphan / passing-clean pair is the proof the gate bites.

## Constraints honoured

- **File mode:** the script is mode 755 (matching
  `scripts/ci/check_sender_reachability.py`), the doc 644 — verified with
  `git ls-files -s` after adding, per the NFS file-mode trap.
- **Build:** configured `-DEQT_GRAPHICS=OFF`; built `test_captured_packets`
  (one small GTest binary) to give `bin/` real contents to inspect. The gate
  itself needs only the file API reply, not a full build.

## Deviations

- The worktree was handed to me checked out at an old ancestor of `fable`
  (`7a8162a9`), not at the stated fork point `eef57da0`, so the T168 precedent
  script and the `refactor/` docs were absent. I branched
  `task/T175-build-tree-hygiene` from `fable` (`eef57da0`) — the intended base,
  which carries `check_sender_reachability.py` and `T173_corpse_targeting.md` —
  and did the work there. No scope change.
