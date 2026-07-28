# Testing campaign progress tracker

Owner: testing session (charter: `refactor/TESTING_CHARTER.md`).
The perf session reads this file; findings that belong to it are under
"Handoffs".

Last updated: 2026-07-28 (T01).

## Unit log

| Unit | Name | Status | Notes |
|------|------|--------|-------|
| T01 | Coverage survey | DONE (report-only) | Numbers + backlog below; awaiting Will's green-light on batch 1 |

## T01 — how the numbers were produced

Scratch configure (not committed, `build-*/` is gitignored):

```
cmake -S . -B build-cov -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0 -g" \
  -DCMAKE_C_FLAGS="--coverage -O0 -g" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
```

`gcovr 7.0` (apt-installed on this host; no lcov present), filtered to
`src/` + `include/`, excluding `graphics/` and `third_party/`.

Rationale for `EQT_GRAPHICS=OFF` rather than the desktop tree: desktop
GL is being eliminated (`refactor/desktop_gl_elimination_plan.md`,
`docs/decisions.md` 2026-07-28), so baseline numbers should not be
anchored to a dying configure. Base layers are supposed to link no
Irrlicht at all, which the charter names as the preferred shape.

**That configure does not build.** See Finding 1 — it is itself the
survey's headline result.

Test run: 782 tests, 4 failures, 50 skipped, 8.59s wall.
Failures: `test_titanium_opcodes_NOT_BUILT`, `test_eq_client_NOT_BUILT`,
`test_session_replay_NOT_BUILT` (all consequences of Finding 1) and
`AudioManagerTest.PreloadCommonSounds` (documented host ALSA exception).

## Headline findings

### Finding 1 — `EQT_GRAPHICS=OFF` does not compile (blocks everything)

351 compile errors across 12 client TUs. Header members are removed by
`#ifdef EQT_GRAPHICS` but their uses in the `.cpp` files are not
guarded. Representative errors:

| count | error |
|-------|-------|
| 75 | `'Graphics' is not a member of 'EQT'` |
| 52 | `'m_quit_requested' was not declared in this scope` |
| 17 | `'eqt::ui' has not been declared` |
| 11 | `base operand of '->' is not a pointer` |

Failing TUs: `eq.cpp`, `eq_bridge.cpp`, `eq_chat.cpp`,
`eq_combat_actions.cpp`, `eq_entities.cpp`, `eq_merchant.cpp`,
`eq_movement.cpp`, `eq_strings.cpp`, `eq_zone.cpp`,
`eq_action_handler.cpp`, `combat.cpp`, `application.cpp`.

The CMake option is advertised in the root `CMakeLists.txt` and the
runtime `--no-graphics` flag is documented in `CLAUDE.md`, but the
compile-time headless build is dead. **This is the single biggest
obstacle to base-layer testing**: it is why 21k lines of `eq*.cpp` and
12k lines of other client logic cannot be linked into a test binary
that doesn't drag in the renderer.

This is base-layer SOURCE work well beyond a "minimal-diff testability
seam", so per the charter it is filed here rather than fixed. It needs
Will's decision (see "Decisions needed").

### Finding 2 — no capture fixtures exist, so the protocol tests are inert

`data/captures/` does not exist in this checkout. Every pcap and
session-replay test therefore skips (50 skipped tests). The direct
consequence: **`src/common/net/daybreak_connection.cpp` — 1195 lines,
the reliable-UDP core of the whole client — measures 0.0% line
coverage.** The tests that would cover it are written and building;
they have no data to run on.

### Finding 3 — coverage is concentrated almost entirely in audio

Audio is 61.9% covered and accounts for over half of all covered lines.
Every other base layer is at or near zero. The suite looks large (782
tests) because audio and asset-loading tests are numerous, not because
the protocol/state/game-logic layers are tested.

## Per-layer coverage

"Instrumented" = compiled into at least one test binary. Files in no
test binary at all are counted separately, because their 0% is not even
in the denominator — the instrumented percentages **overstate** reality.

| Layer | Instr. lines | Covered | Line % | Branch % | Uninstrumented |
|-------|-------------:|--------:|-------:|---------:|----------------|
| net protocol | 1544 | 182 | 11.8% | 3.2% | — |
| common util | 644 | 137 | 21.3% | 8.7% | 3 files / 12 lines |
| common other | 207 | 0 | 0.0% | 0.0% | — |
| state/EventBus | 1261 | 0 | **0.0%** | 0.0% | — |
| bridge | 63 | 0 | 0.0% | 0.0% | 1 file / 876 lines |
| spell | 451 | 82 | 18.2% | 6.3% | 3 files / 2879 lines |
| skill | — | — | none | — | 1 file / 572 lines |
| input | 434 | 0 | 0.0% | 0.0% | 2 files / 1126 lines |
| audio | 2924 | 1809 | 61.9% | 24.8% | — |
| output | 424 | 0 | 0.0% | 0.0% | — |
| action | 1618 | 0 | **0.0%** | 0.0% | — |
| eq core (`eq*.cpp`) | — | — | none | — | **18 files / 21351 lines** |
| client other | 3730 | 980 | 26.3% | 11.9% | 7 files / 6263 lines |
| **TOTAL (instrumented)** | **13300** | **3190** | **24.0%** | **9.7%** | **~33k lines uninstrumented** |

### Largest zero-coverage instrumented files

| Lines | File |
|------:|------|
| 1195 | `src/common/net/daybreak_connection.cpp` |
| 713 | `src/client/action/command_processor.cpp` |
| 689 | `src/client/action/action_dispatcher.cpp` |
| 349 | `src/client/pathfinder_nav_mesh.cpp` |
| 348 | `src/client/output/console_renderer.cpp` |
| 317 | `src/client/input/console_input_handler.cpp` |
| 314 | `src/client/zone_lines.cpp` |
| 288 | `src/client/spell/buff_manager.cpp` |
| 277 | `src/client/trade_manager.cpp` |
| 239 | `src/client/state/player_state.cpp` |
| 234 | `src/client/ucs.cpp` |
| 216 | `src/client/action/input_action_bridge.cpp` |
| 212 | `src/client/state/entity_manager.cpp` |
| 207 | `src/common/performance_metrics.cpp` |
| 201 | `src/client/pathfinder_waypoint.cpp` |
| 165 | `src/client/state/spell_state.cpp` |

Also notable: `hc_map.cpp` 4.1% (24/579), `strings.cpp` 19.5%,
`packet.cpp` 24.3%.

### Uninstrumented (in no test binary)

- `eq*.cpp` — all 18 files, 21351 lines (largest: `eq.cpp` 3689,
  `eq_entities.cpp` 2438, `eq_player_info.cpp` 2013, `eq_movement.cpp`
  1985, `eq_bridge.cpp` 1843, `eq_merchant.cpp` 1561)
- `application.cpp` 1963, `inventory_manager.cpp` 1952, `combat.cpp` 1864
- `spell_manager.cpp` 1317, `spell_effects.cpp` 929,
  `spell_type_processor.cpp` 633
- `hotkey_manager.cpp` 855, `irrlicht_bridge.cpp` 876 (perf territory),
  `skill_manager.cpp` 572

## Ranked gap backlog (thinnest + highest risk first)

1. **Daybreak reliable-UDP protocol** — 1195 lines at 0%. Sequencing,
   fragmentation, CRC, compression, retransmit. Highest blast radius in
   the codebase; currently guarded only by tests that never run.
2. **EventBus + state modules** — 1261 lines at 0%. The spine of the
   threading model; every bridge event flows through it. Cheap to test
   (no I/O, no graphics).
3. **Action layer** (`action_dispatcher`, `command_processor`,
   `input_action_bridge`) — 1618 lines at 0%. Pure logic, slash-command
   parsing and dispatch. Cheap and high-value.
4. **Login/world/zone connection flow** — inside `eq*.cpp`, entirely
   uninstrumented. The highest-risk area (zone-load hangs are a known
   recurring failure) and blocked behind Finding 1.
5. **Packet structs + opcodes** — `test_packet_structs` runs, but
   `test_titanium_opcodes` doesn't even build under this configure.
   Layout is behavior here; regressions are silent and catastrophic.
6. **`hc_map.cpp` 4.1%, `zone_lines.cpp` 0%** — map/zone-line parsing,
   file-format code with real edge cases and available fixtures.
7. **Spell/skill/combat non-graphics logic** — ~5.4k lines, mostly
   uninstrumented. Rich behavior, but needs seams to separate from the
   renderer.
8. **`strings.cpp` 19.5%, `packet.cpp` 24.3%** — small, easy top-ups.

## Proposed first batch of T-units (NOT started — awaiting green-light)

| Unit | Scope | Territory | Why first |
|------|-------|-----------|-----------|
| T02 | EventBus + state modules unit tests | `tests/` only | Zero coverage, zero dependencies, no seams needed. Proves the executor pattern on an easy target. |
| T03 | Action layer: `command_processor`, `action_dispatcher`, `input_action_bridge` | `tests/` only | 1618 lines at 0%, pure logic, no graphics link. |
| T04 | Daybreak protocol tests on synthetic fixtures | `tests/` + a generated fixture under `tests/` | Highest-risk layer. Deliberately synthetic so it does not depend on a capture that doesn't exist. |

Each is `tests/`-only, needs no base-layer source change, and links no
Irrlicht — so the merge bar is desktop+GLES2 ctest with no seam risk.

Deferred until Will rules on Finding 1: everything touching `eq*.cpp`
(the connection flow, backlog item 4), which is the largest and
highest-risk gap but is currently untestable in isolation.

## Decisions needed from Will

1. **Finding 1 — who fixes `EQT_GRAPHICS=OFF`?** Options: (a) the
   testing session takes it as a unit, accepting that it is a large
   base-layer source change outside the charter's "minimal seam" rule;
   (b) it goes to the perf session, which already owns the graphics
   elimination plan; (c) it stays broken and base-layer tests are built
   against the GLES2 tree instead, accepting that every test binary
   drags in the renderer. My recommendation is (b) — it is a graphics
   #ifdef problem and the perf session owns that boundary — but it
   blocks backlog item 4 until done.
2. **Finding 2 — should a capture be recorded?** A single recorded
   session (`--record-session`) would activate ~50 currently-inert
   tests and give `daybreak_connection.cpp` real coverage. That needs
   live eqemu, which is currently in use by another session. Worth
   scheduling, or stick to synthetic fixtures (T04 as proposed)?

## Needs seam

Nothing yet — no source modification has been attempted.

## Handoffs to the perf session

- Finding 1 (`EQT_GRAPHICS=OFF` broken, 351 errors, 12 TUs) is a
  graphics-boundary defect. Pending Will's ruling in decision 1 above.
- `src/client/bridge/irrlicht_bridge.cpp` (876 lines) is in no test
  binary; perf territory, noted not touched.
