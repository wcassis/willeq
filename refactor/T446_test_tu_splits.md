# T446 — split the three test TUs riding the 1,500 ceiling (structural)

Class B (structural: file size + functional separation outrank minor
edits — Will 2026-09-03), slot2, no server. The three largest test TUs:
`tests/test_eq_commands_equip.cpp` (1,492), `tests/test_zone_connection_flow.cpp`
(1,367), `tests/test_combat_manager.cpp` (1,316). Read `CLAUDE.md`'s
test-layout rules, `tests/cmake/*.cmake` (how a test target is
registered), Batch G's golden-pinned list discipline, and each TU's
fixture/section structure.

1. Split each TU by functional group into 2–3 TUs with a shared fixture
   header where one exists (e.g. `test_eq_commands_equip_{wear,swap,
   validation}.cpp`; `test_zone_connection_flow_{login,zone_in,
   teardown}.cpp`; `test_combat_manager_{attack,skills,death}.cpp` —
   name by what the cases actually cover, not this guess). Every case
   keeps its name and body; the case COUNT before and after is identical
   (list both; ctest totals prove it). No behaviour change.
2. Register the new targets; delete the old TU; any doc/net naming the
   old file (`module_map`, `DocClaims`, the T444 COVERAGE table if it
   covers tests/) learns the new names.
3. Largest resulting TU under 1,000.

## Proof

Build (background), `ctest -j6` (no expected reds; the total rises by
zero cases — say the per-target case counts), file sizes, DocClaims/
DocCounts/DocCitations green. No golden changes. No board strikes (no
board item); new items B461+ if you find something while reading (T445
in slot1 allocates from B459+; say which you used).

STOP: a case count changes.

## Review

**Branch** `task/T446-test-tu-splits`, base `7f1f3ca9` (fable tip). Slot2,
`build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86`. No server, no eqemu lock taken.

### Shape of the split: one target, three sources

Separate BINARIES were rejected on a hard constraint, not on taste. Every
case had to keep its name, and a case name here is `Suite.Case` — so three
binaries all carrying suite `EqCommandsEquipTest` (or `CombatManagerLoot`, or
`ZoneRequestPhaseGating`) would register the SAME ctest test name three
times. The only split that keeps every name is T165's
`test_command_processor` precedent: ONE target, several sources, plus a
shared header. That also costs no extra link of `willeq_core` and no extra
ctest discovery pass.

For `test_eq_commands_equip` the fixture therefore had to move into a NAMED
namespace (`eqt::tests`): GTest requires every test in a suite to share one
fixture TYPE, and an anonymous-namespace fixture in a header gives each TU
its own type and aborts at startup. The free helpers stay anonymous — a
per-TU copy of a plain function is correct and free. The other two families
use `TEST`, not `TEST_F`, so they needed no named type.

### The split table

| old TU | lines | cases | new TUs | lines | cases |
|---|---|---|---|---|---|
| `tests/test_eq_commands_equip.cpp` | 1,492 | 57 | `tests/eq_commands_equip_test_fixture.h` (shared fixture + `Seed*()` helpers) | 121 | — |
| | | | `tests/test_eq_commands_equip_moves.cpp` — parsing, slot validation, happy-path intent fields, T71 local-model sync, T77 legality | 357 | 20 |
| | | | `tests/test_eq_commands_equip_swaps.cpp` — name form, list form, T236 swap pre-flight, T249/T250 cursor swaps | 432 | 20 |
| | | | `tests/test_eq_commands_equip_bags.cpp` — T252/T255/T259 bag CONTENTS | 654 | 17 |
| `tests/test_zone_connection_flow.cpp` | 1,367 | 35 | `tests/zone_connection_flow_test_fixture.h` (packet builders, `RecordingBridge`) | 167 | — |
| | | | `tests/test_zone_connection_flow_handshake.cpp` — phase gating, dispatcher robustness, observable post-handshake state | 371 | 17 |
| | | | `tests/test_zone_connection_flow_snapshot.cpp` — `FullStateSnapshot` | 413 | 3 |
| | | | `tests/test_zone_connection_flow_identity.cpp` — guild id, GM mode, BSP region | 458 | 15 |
| `tests/test_combat_manager.cpp` | 1,316 | 66 | `tests/combat_manager_test_fixture.h` (seam + the T17 private-access preamble) | 59 | — |
| | | | `tests/test_combat_manager_targeting.cpp` — targeting, auto-attack, auto-fire, cycling | 421 | 26 |
| | | | `tests/test_combat_manager_state.cpp` — the self-paced tick, consider, state machine, config | 267 | 15 |
| | | | `tests/test_combat_manager_corpses.cpp` — loot state machine, consider-opcode, corpse naming/recognition | 619 | 25 |

57 = 20+20+17. 35 = 17+3+15. 66 = 26+15+25. Largest resulting TU **654**
lines, well under the plan's 1,000 bar.

Two helper functions were HOISTED out of the middle of the equip TU into the
shared header because they are used on both sides of a split boundary:
`SeedNamedItem()` (was inside the T77 legality section) and `SeedBag()` (was
inside the T250 section). Their bodies and comments moved verbatim. No test
case body was touched anywhere in this unit — every `.cpp` body is a
contiguous line range of its original.

### Case counts are identical, and so are the NAMES

The plan's STOP condition is a count change. Counts were checked from the
binaries, and then the stronger claim was checked too: the sorted
`Suite.Case` set from `--gtest_list_tests` after the split `diff`s clean
against the sorted `TEST(_F)?(Suite, Case` set extracted from the deleted
files at `7f1f3ca9`.

| target | cases before | cases after | name set |
|---|---|---|---|
| `test_eq_commands_equip` | 57 | 57 | IDENTICAL |
| `test_zone_connection_flow` | 35 | 35 | IDENTICAL |
| `test_combat_manager` | 66 | 66 | IDENTICAL |

`ctest -N -R '^EqCommandsEquipTest\.'` registers 57, matching.

### cmake registrations changed

- `tests/cmake/commands.cmake` — `test_combat_manager` and
  `test_eq_commands_equip` each take three sources instead of one.
- `tests/cmake/zone_integration.cmake` — `test_zone_connection_flow` likewise.

No target was added or removed, so no `GTEST_DISCOVER_TESTS` call, link line
or include-directory guard changed.

### Docs and nets repointed

`refactor/BOARD.md`'s M00 "largest survivors" paragraph cited two of the three
deleted files by path — a **DocCitations FAIL** the moment they were removed.
Rewritten to name the ceiling measurement historically and point at this
plan, with the new largest test TU (654). Nineteen further cross-reference
comments in `src/`, `include/`, `tests/` and `tests/cmake/` named a deleted
file; each learned either the family glob (`test_combat_manager_*.cpp`) or
the specific new home (the T23 seam note and the `#define private public`
technique now live in `tests/combat_manager_test_fixture.h`;
`test_zone_connection_flow.cpp`'s `RecordingBridge` and T43 item-string
builder now live in `tests/zone_connection_flow_test_fixture.h`).
`docs/module_map.md` and `README.md` named none of the three.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background, exit file) | **exit 0** — no exit 143, so no truncation scan was owed |
| `ctest --test-dir build -j6 --output-on-failure` | **4642/4642, 0 failed**, 361 s |
| ctest total before / after | 4642 / 4642 — unchanged, as the identical per-target name sets require |
| `check_file_sizes.sh fable` | 28 files checked, **28 ok, 0 warn, 0 fail** |
| `DocCitations` | 1157 citations, **0 fail** (2 fail before the BOARD.md rewrite) |
| `DocClaims` | OK |
| `DocCounts` | OK (see the deviation below) |
| goldens | untouched — no golden file is in the diff |

### Deviation / finding: DocCounts was ALREADY RED at `7f1f3ca9`

The dispatch said there are no expected reds on this tip. There was one, in
`DocCounts`, and it is not mine:

```
FAIL  docs/module_map.md does not name src/client/graphics/diagnostics/scene_node_census.cpp
```

`b28a4fd2` (T443) split `sceneNodeCensus()` into its own 48-line TU and never
added it to the map. This unit touches neither file, so the red would have
followed the branch to the tip. Fixed here rather than boarded, since it is a
two-line doc edit and leaving a known red for a later unit to re-discover is
exactly the failure mode the "stale docs are a first-class defect" ruling
targets: `docs/module_map.md` now lists the TU in the DiagnosticsComponent
row and carries a line-count row for it. Flagged rather than absorbed —
whoever merges T443's sibling work should know the map was one TU short.
It also means **`ctest -j6` on `7f1f3ca9` itself is 4641/4642, not clean**;
the 4642/4642 above is with that fix in.

No new board items were opened (B461+ unused): the one defect found was
closed in place, and nothing else surfaced while reading the three TUs.
