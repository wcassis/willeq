# Testing campaign progress tracker

Owner: testing session (charter: `refactor/TESTING_CHARTER.md`).
The perf session reads this file; findings that belong to it are under
"Handoffs".

Last updated: 2026-07-28 (T02).

## Unit log

| Unit | Name | Status | Notes |
|------|------|--------|-------|
| T01 | Coverage survey | DONE (report-only) | Numbers + backlog below; awaiting Will's green-light on batch 1 |
| T02 | EventBus + state module unit tests | DONE | 127 new cases across `test_event_bus.cpp`, `test_state_modules.cpp`, `test_state_modules_secondary.cpp` (3-way split — see plan Review). All 11 files in `src/client/state/` now linked into a test binary. Full detail: `refactor/T02_state_event_bus.md` Review section. |
| T04 | Zone connection-flow sequencing | DONE (merged `2042490`) | 16 tests via the existing public `ReplayZonePacket` seam; all four gating prerequisites isolated. Found 2 real robustness bugs + 1 latent gating gap (below). Login/world still uncovered — needs a seam decision from Will. |
| T03b | Suite hygiene | DONE (merged `41fe719`) | Part A: Hotkey fixtures made parallel-safe (unique per-PID/per-test temp dirs) — verified 10/10 green under `-j8`, no `/tmp` residue. Part B: RDP segfault diagnosed as a real use-after-free in `RDPServer::stop()` and handed to the perf session, correctly NOT fixed. See `refactor/TESTING_HANDBACK.md` Round 3. |
| T03a | Unhardcode pcap zone port | DONE (merged `02d591d`) | 4 FAILs + 8 SKIPs → 12 PASS. Handshake-based port/IP discovery in `pcap_test_utils.h` + `WILLEQ_ZONE_PORT`/`WILLEQ_ZONE_IP` overrides. Compression assertions NOT weakened — 1017/1682 S→C packets genuinely carry the 0x5a marker once direction classification is fixed. |

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

## Decisions taken by Will (2026-07-28)

1. **Finding 1 (`EQT_GRAPHICS=OFF` broken) — perf session owns it.**
   Not a testing-session unit.
2. **Finding 2 (missing capture fixtures) — perf session owns it.**
   The testing session proceeds on synthetic/replay fixtures only and
   is not blocked on a recording.
3. Handoff delivered by Will relaying directly to the perf session,
   plus `refactor/TESTING_HANDBACK.md`. No `deferred.md` pointer was
   added (outside testing territory).

## Both handback items RESOLVED by the perf session (2026-07-28)

Landed while T02 was executing — fable moved 13 commits under it.

- **Finding 1 → DONE (P28, merged `e9d808f`)**: `eq.h`
  `EQT_HAS_GRAPHICS` guards declassified, 351 errors → 0.
  `EQT_GRAPHICS=OFF` compiles. **Backlog item 4 (login/world/zone
  connection flow) is now UNBLOCKED** — it was the only thing gating it.
- **Finding 2 → DONE (P29, merged `8fdcd24`)**: capture fixtures
  recorded and live at `/mnt/projects/fable/captures/` (NFS), reached
  via a `data/captures` symlink — **each worktree must create its own**
  (`ln -s /mnt/projects/fable/captures <worktree>/data/captures`); t1
  has one as of T02. session.sleq (2,943 records) + session.pcap (5,603
  pkts) + self-created golden. 10 tests skip→PASS.
- Also landed: P30 (desktop-GL CMake removed; the GLES2 config is now
  the ONLY tree) and a charter amendment rewriting the merge bar from
  "both trees green" to "the GLES2-config full suite green". P27 fixed
  the `RaceModelAnimationHeadlessTest` segfault that KICKOFF listed as
  a known GLES2-tree failure — it is no longer an expected failure.
- Process: the `.eqemu-lock` protocol was added to
  `EXECUTOR_PROTOCOL.md` as a direct result of this session flagging
  that P25's run used the server without a lock.

## Incoming — routed TO the testing session by the perf session

- **Hardcoded zone port 7000** (`refactor/deferred.md`, from P29):
  `tests/pcap_test_utils.h` plus three test files hardcode zone port
  7000, but the akk-stack image auto-boots ~15 static zones claiming
  7000–7011, so real captures land on 7012+ (befallen 7012, qeynos2
  7026 in the current fixture). One root cause behind **4 remaining
  FAILs + 8 skips**. Fix = `WILLEQ_ZONE_PORT` override or port
  autodiscovery from the pcap (SessionRequest/Response scan). Every
  future capture against this image hits the same thing. `tests/`
  territory — ours. Proposed as **T03a**, ahead of the action layer:
  it is small, it unblocks real protocol coverage, and it is the
  difference between the new fixtures paying off or not.

## Suite hygiene — a THIRD flake family, undocumented (found in T03a review)

The charter's documented exceptions are `AudioManagerTest.
PreloadCommonSounds` (host ALSA) and `RDPServerFrameTest.*`
(parallel-run). There is a third, and it is ours:

**`HotkeyJsonLoadTest.WarnsOnInvalidActionName` and
`HotkeyResetReloadTest.Reload_ReloadsFromLastPath` are parallel-unsafe.**

Evidence (t1, GLES2 tree, current tip):
- `ctest -R Hotkey` serially → 47/47 pass.
- `ctest -R Hotkey -j8` → 1 failure. **No other test binary is running**,
  so this is the Hotkey tests interfering with each other, not
  cross-suite contention. They almost certainly share a config file
  path on disk.
- Unrelated to T03a: that diff touches only pcap test files, and the
  reproduction above runs no pcap binary at all.

They do not fail on every full run — they surfaced only when the test
set changed (`ctest -j8 -E "Pcap|Daybreak"`), which is exactly the
profile of an order/timing-dependent shared-resource bug.

Also worth recording honestly: `RDPServerFrameTest.
UpdateFrameMultipleTimes` SEGFAULTed on **3 of 3** full `-j8` runs
during T03a verification (it passes isolated; the T03a executor saw it
fail 1 of 3). The documented "intermittent" characterization understates
its current rate. It is a documented exception so it did not block the
merge, but a segfault reproducing 3/3 deserves a real diagnosis rather
than a standing waiver.

**RESOLVED by T03b** (merged `41fe719`):

- **Hotkey family — FIXED.** All four fixtures now build a unique temp
  dir from PID + suite + test name. Verified 10 consecutive green
  `ctest -R Hotkey -j8` runs, serial still green, no `/tmp` residue.
  This family is no longer a flake and must **not** be added to the
  documented-exception list.
- **RDP family — NOT a flake. It is a product bug.** T03b traced it to
  a use-after-free in `RDPServer::stop()`: the listener is `Close()`d
  while `listenerThread()` may already hold fetched handles and be
  entering `WaitForMultipleObjects`, and the thread is only joined
  *after* that close. 12 kernel GP faults, all at the identical
  libwinpr3 offset (`0xB3EB` → `WaitForMultipleObjectsEx+0x16b`), and
  it reproduces in more than one test of the family — so it is the
  shared start/stop lifecycle, not any single test.
  Handed to the perf session (`TESTING_HANDBACK.md` Round 3); the fix
  is in `src/client/graphics/**`, off-limits to us.

**Consequence for our own bar:** the `RDPServerFrameTest.*` entry in the
charter and `EXECUTOR_PROTOCOL.md` is mislabelled. It is a known
*product* bug, not a test flake, and the "intermittent" wording caused
every session to wave a real crash through. Until the perf session
lands a fix, T-unit reviews should treat an RDP segfault as an expected
manifestation of that known bug — and should say so explicitly rather
than counting the run as clean.

## T04 findings (zone flow — pinned as tests, NOT fixed)

All three verified against source by the orchestrator during review.

### 1. Truncated-packet crashes in the zone receive path (REAL, actionable)

- `ZoneProcessTimeOfDay` (`src/client/eq_entities.cpp:484`) has **no
  length guard at all** — reads `GetUInt8(2)`…`GetUInt16(6)`
  unconditionally.
- `ZoneProcessPlayerProfile` (`src/client/eq.cpp:1999`) logs
  `"PlayerProfile packet too small"` and then **falls through and reads
  anyway** — the guard warns but does not `return`.

`EQ::Net::Packet::GetUInt*` throws `std::out_of_range` rather than
reading out of bounds (`src/common/net/packet.cpp:207`), so this is an
exception rather than memory corruption. But **the zone receive path
has no exception handling**: `ZoneOnPacketRecv` (`eq.cpp:717`) calls
`DispatchZonePacket` bare, and the `try`/`catch` blocks at
`eq.cpp:3480-3516` guard `SafeQueueZonePacket` — the *send* path. So a
truncated packet propagates out of the network callback and takes the
client down.

Not hypothetical: `CLAUDE.md` documents packet loss during zone-load
bursts as a known failure mode, and truncation is what that produces.

Pinned as `DispatcherRobustness.BUG_TimeOfDay_ZeroLengthPayload_*` and
`BUG_PlayerProfile_TruncatedPayload_*`. **Fix is base-layer source —
needs Will's go-ahead** (a `return` after the PlayerProfile warning and
a length guard on TimeOfDay would be a ~4-line change, but it is not
`tests/` territory).

### 2. `CheckZoneRequestPhaseComplete` is evaluated on 3 of its 4 inputs (latent)

The gate needs `m_new_zone_received` + three counters. Three of those
are set by handlers that then call `CheckZoneRequestPhaseComplete()`
(`eq_player_info.cpp:54,62,71,80`). `ZoneProcessNewZone` sets
`m_new_zone_received` and **never calls it**.

The T04 executor reported this as "a genuine zone-load hang". **That
overstates it, corrected during review:** `ZoneProcessNewZone` is
itself what *sends* the AA/tribute/guild requests (`eq.cpp:1965+`), so
their responses cannot arrive before it; and the counters are reset on
zone change (`eq_zone.cpp:323-325`), which closes the re-zone path too.
The dangerous ordering should not arise in the real protocol.

It remains worth a one-line defensive call — correctness currently
rests on request/response causality rather than on the gate itself —
but it is **not** a demonstrated production hang and should not be
reported as one.

## Decision needed from Will — a login/world replay seam

T04 covered the zone half of backlog item 4. The login and world stages
remain at zero coverage and **cannot be reached without a source
change**: every handler is private and needs a live connection
(`LoginOnPacketRecv` eq.h:1146, `LoginProcessLoginResponse` 1152,
`WorldOnPacketRecv` 1168, `WorldProcessCharacterSelect` 1178).

### Full survey of network surfaces (done 2026-07-28, answering "how many more spots?")

The gap is **bounded at two**, not open-ended. Live packet-receive
entry points:

| surface | entry point | seam? |
|---------|-------------|-------|
| Zone | `ZoneOnPacketRecv` (eq.cpp:717) | **yes** — `DispatchZonePacket` + public `ReplayZonePacket` (Batch R) |
| Login | `LoginOnPacketRecv` (eq_login_world.cpp:70) | no |
| World | `WorldOnPacketRecv` (eq_login_world.cpp:319) | no |
| UCS | `UCSOnPacketRecv` (eq.cpp:2808) | **dead code** — the whole block from eq.cpp:2753 is inside `/* */`, and the only caller (`eq_login_world.cpp:644`) is commented out. The separate `UCSConnection` class in `src/client/ucs.cpp` (234 lines, 0% coverage) is likewise never connected. Not a live surface; do not build a seam for it. |

### What the work actually is (bigger than "add two methods")

The zone seam works because Batch R did **two** things, and only the
first is obvious:

1. Split the opcode switch out of `ZoneOnPacketRecv` into
   `DispatchZonePacket`, so a test can enter at the switch.
2. Retrofitted **every zone sender** with a null-connection guard that
   no-ops and bumps `m_replay_suppressed_sends` (11 such guards in
   `eq.cpp`). Without this, replaying a packet whose handler sends
   would dereference a null connection.

Login/world have neither. Their switches are inline in `*OnPacketRecv`,
and their senders call `m_login_connection->QueuePacket(p)` /
`m_world_connection->QueuePacket(p)` **unguarded** — e.g.
`LoginSendSessionReady` (eq_login_world.cpp:109). And this is not
avoidable by picking safe opcodes: `LoginOnPacketRecv`'s very first
case (`HC_OP_ChatMessage`) calls `LoginSendLogin()`.

Measured scope: **11 sender functions, 12 unguarded `QueuePacket` call
sites, 2 dispatcher extractions, 2 new public methods** — across
`eq_login_world.cpp` and `eq.h`. That is almost exactly the size of
what Batch R already did once for zone (11 guards), so the precedent
covers the shape and the risk, but it is not a 10-line diff.

### Territory: this is probably NOT a perf-session item

`include/client/eq.h` and `src/client/eq_login_world.cpp` are
**base-layer, non-graphics client code** — the charter's "read freely,
modify only for a testability seam" zone, i.e. ours. The perf session
owns `src/client/graphics/**`, docker, goldens, presets. P28 went to
them because it was a graphics-`#ifdef` problem; this is not.

So the blocker is not ownership, it is the charter's "minimal-diff"
wording versus a 12-call-site change. That is a latitude question for
Will, not a handoff.

**Separately — and this one genuinely has no owner:** the
truncated-packet crashes (finding 1 above) are a *behavior* fix, not a
testability seam, so they fall outside the seam clause entirely
regardless of how the latitude question is settled. Base-layer,
non-graphics, ~4 lines. Someone has to be assigned it.

Until resolved, login/world stay uncovered and the recorded
`session.pcap` login/world streams (5998 and 9000, ~1511 packets) go
unused by any state-level test.

## Needs seam

- **Login/world replay seam** — see the decision item above. This is the
  only thing blocking the remaining half of backlog item 4.

## T02 findings (state-layer bugs, documented in tests, NOT fixed)

Full detail: `refactor/T02_state_event_bus.md` Review section.

1. `CombatState::fireTargetChangedEvent()` / `fireCombatEvent()`
   (`src/client/state/combat_state.cpp`) are empty stubs — `setTarget()`,
   `clearTarget()`, `setAutoAttacking()` never actually publish anything
   on the `EventBus` despite `GameEventType::TargetChanged` /
   `CombatEvent` existing for exactly this. Pinned in
   `CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent`.
2. `InventoryState::fireInventoryChangedEvent()` always reports
   `hasItem=true`/`itemId=0` regardless of the actual slot state passed
   in (source comment acknowledges this). Pinned in
   `InventoryStateTest.InventorySlotChangedEventAlwaysReportsHasItemTrue`.
3. `WorldState::fireZoneChangedEvent()` reads position from the
   pending-zone fields, not from `setZone()`/`setZoneName()` args —
   calling `setZone()` alone publishes position (0,0,0). Pinned in
   `WorldStateTest.ZoneChangedEventPositionComesFromPendingZoneNotSetZoneArgs`.
4. `EventBus` listener lifetime is a genuine, unexercised UB hazard (a
   listener capturing `this` of an object destroyed mid-dispatch) — not
   tested per the plan's instruction (UB, cannot be asserted safely),
   noted here as an observation only.

## Handoffs to the perf session

Full detail: **`refactor/TESTING_HANDBACK.md`**.

- ~~Finding 1 (`EQT_GRAPHICS=OFF`)~~ — **CLOSED, fixed by P28**.
- ~~Finding 2 (no capture fixtures)~~ — **CLOSED, fixed by P29**.
- `src/client/bridge/irrlicht_bridge.cpp` (876 lines) is in no test
  binary; perf territory, noted not touched.
- ~~**T02 finding**: GLES2-x86 tree X11 link failures~~ — **CLOSED.**
  The executor reported `test_graphics`, `test_asset_loading`,
  `test_door_loading` and five tools failing to link with undefined
  Xlib references. On re-verification by the orchestrator against the
  current fable tip, a full unscoped `cmake --build build -j24` of the
  GLES2 config completes with **0 errors** and all targets link. P30
  removed the desktop-GL/X11 paths that caused it. Recorded as closed
  rather than deleted so the perf session doesn't chase a stale report.
- **NEW (orchestrator, T02 merge verification)**: the canonical
  configure command in the root `CLAUDE.md` "Build Commands" does not
  work as written. It documents
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` (relative),
  but `CMakeLists.txt:94` tests that path with `EXISTS`, which CMake
  only defines for absolute paths — so configure fails the
  patched-Irrlicht check and dies with the "requires a locally-built
  patched GLES2 Irrlicht" FATAL_ERROR even when the library is present
  and correctly built. An absolute
  `-DEQT_GLES2_IRRLICHT_DIR=<abs>/third_party/irrlicht-gles2-x86`
  configures fine. The T02 executor hit this independently and worked
  around it. Since P30 made this the only tree and this is the
  documented entry point for every future checkout, it is worth a
  one-line fix (make the path absolute in `CMakeLists.txt`, or correct
  the doc). Root `CMakeLists.txt` + `CLAUDE.md` are outside testing
  territory, hence handed over rather than fixed.
- Coordination gap: nothing in the perf session's docs points at this
  tracker, so the handoff channel is write-only on the testing side.
  See the closing section of `TESTING_HANDBACK.md`.
