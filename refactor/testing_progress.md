# Testing campaign progress tracker

Owner: testing session (charter: `refactor/TESTING_CHARTER.md`).
The perf session reads this file; findings that belong to it are under
"Handoffs".

Last updated: 2026-07-31 (T18; T17 in flight in t1, T19 planned).

## Unit log

| Unit | Name | Status | Notes |
|------|------|--------|-------|
| T18 | Zone entities + chat tests | DONE (merged `0439de6`) | First direct coverage of `eq_entities.cpp` (2445 lines) + `eq_chat.cpp`. Real capture: 2788 zone records replay to a 184-entity table, pinned exactly (incl. player spawn 261). Synthetic: Spawn_Struct bitfield decode (INVERSEXY, sign extension, 11-bit heading), batch semantics, delete/health handlers, bridge events, all 8 chat channel mappings, /reply tracking. **BUG pinned, needs Will:** exactly-150-byte ChannelMessage → uncaught `std::out_of_range` on the receive path (section below). Suite failures: ALSA quirk + pre-existing deferred RDP race (1-in-5 isolated repro, perf territory). Executed inline in t2. |
| T16 | Spell-cast off-by-one fix | DONE (merged `3253126`) | Closes the T15 finding, go-ahead from Will. `processSpellCasts()` now converts the 0-based request to the dispatcher's 1-based contract (`+1`, matching the hotbar path). Gem 1 castable again; `/cast N` casts gem N. Both `BUG_` pins re-pointed (gem 0→`castSpell(1)`, gems 4/7→5/8). Executed inline. |
| T15 | `InputActionBridge` tests | DONE (merged `61e5d17`) | The T12 follow-on; the 391-line bridge now has 33 tests (routing, gating, draining, mouse-look, chat mapping) via a new `FakeInputHandler` in the shared fakes. **BUG pinned, not fixed (needs Will):** spell-cast gem off-by-one — see section below. Also pinned: `"raid"` unmapped (falls to Say), empty-target tells vanish, `setInvertMouseY()` inert, `processContinuousInput()` dead (makes `setTurnSpeed()` a no-op knob). Executed inline. |
| T14 | Character-select list storage | DONE (merged `4414a92`) | Closes the T06/T09 open question, go-ahead from Will. Occupied slots stored (`CharacterSelectEntry`: slot/name/level/class/race/zone) behind `GetCharacterSelectList()`, rebuilt per delivery; first-match index semantics preserved but the loop now parses all ten slots instead of returning at the match. **Finding:** eqemu fills empty Titanium slots with literal name `<none>` (other fields zero) — hidden until now by the early return; filtered ('<' is not legal in names). Real-capture list pinned exactly (3 entries); synthetic decode, sentinel/empty skip, parse-past-match, redelivery-rebuild tests. Executed inline. |
| T13 | `OP_SetChatServer` 4-field parse | DONE (merged `9d456c1`) | Closes the T09 finding, go-ahead from Will. Parser rewritten to the real eqemu format (`host,port,shortname.charname,<type><mail_key:08X>`); connection-type char stored (new accessor) for the future UCS implementation; receive path guarded (field count, empty field 4, non-numeric AND out-of-uint16-range port — `stoi` alone doesn't catch 70000). BUG_ pin re-pointed at fixed behavior + `OP_SetChatServer2` 'b' case. Executed inline; interrupted by the 07-30 VM crash and recovered from the t1 working tree. |
| T01 | Coverage survey | DONE (report-only) | Numbers + backlog below; awaiting Will's green-light on batch 1 |
| T02 | EventBus + state module unit tests | DONE | 127 new cases across `test_event_bus.cpp`, `test_state_modules.cpp`, `test_state_modules_secondary.cpp` (3-way split — see plan Review). All 11 files in `src/client/state/` now linked into a test binary. Full detail: `refactor/T02_state_event_bus.md` Review section. |
| T12 | Action layer (dispatcher + commands) | DONE (merged `15af1d9`) | 68 tests over 2609 lines that were at 0%. No mis-wiring found (verified independently). Guard split pinned: 12 checkHandler-only vs 81 zone-gated. `InputActionBridge` (391 lines) not reached — follow-on. |
| T11 | Split `test_wld.cpp` | DONE (merged `2e8f8c5`) | 3897 lines → 9 TUs (247–545) + a 108-line fixture header. Test set verified identical: 103 → 103, empty diff. No file in `tests/` now exceeds 1500 lines. |
| T10 | WLD mutation kill rate | DONE (merged `bd60ee3`) | Measured baseline **0/8**, strengthened to **4/8**. Confirms the benchmark session's signal. Found `resolveTextureName` is dead code and the per-mesh header bbox fields are read by nothing. |
| T09 | Login/world accessors | DONE (merged `b531b97`) | 11 read-only accessors + 9 tests asserting login/world **state** for the first time. Found that chat-server info is silently discarded on this server (4-field payload vs a 5-field parser). Character-list contents correctly NOT exposed — would need new storage. |
| T08 | `GetCString` bounds | DONE (merged `b2fee49`) | Real OOB read fixed in `common/net`, inherited by 35 call sites. Confirmed with ASan (heap-buffer-overflow READ of size 5) rather than inferred. Golden md5 unchanged, so real traffic parses identically. 7 new tests. |
| T07 | Login guards | DONE (merged `b6b3502`) | Executed inline by the orchestrator. `LoginDisableReconnect` null-guarded (closes T05's sweep gap); `LoginProcessLoginResponse` underflow + undersized-buffer guarded. Whole recorded login stream now replays; no death test remains. Corrects the orchestrator's wrong fixture-count claim from T06. |
| T06 | Login/world flow coverage | DONE (merged `bae9c83`) | 18 tests incl. real-capture replay of both streams. Found a replay-path segfault in `LoginDisableReconnect` (not live-reachable) that exposed an incomplete guard sweep in T05. `LoginProcessLoginResponse` underflow confirmed as `bad_alloc`, not worse. |
| T05 | Login/world seam + packet guards | DONE (merged `6f2e434`) | **First source-touching unit.** Dispatchers extracted, 12 QueuePacket sites guarded, `ReplayLoginPacket`/`ReplayWorldPacket` public. Both T04 truncated-packet crashes FIXED. Golden matched, ARM build green. Login/world flow coverage deferred to T06. |
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

### 1. Truncated-packet crashes — **FIXED in T05** (`6f2e434`)

Both guards landed: a `return` after `ZoneProcessPlayerProfile`'s
existing warning, and a length guard in `ZoneProcessTimeOfDay`.
Thresholds were verified against the real capture first (PlayerProfile
records are 19592 bytes vs the 19588 guard; TimeOfDay 8 vs a need of 6),
so neither can skip legitimate traffic. The pinned `BUG_` tests are
flipped to assert the fixed behavior. Original writeup below for the
record.

### 1a. Original finding (historical)

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

## Login/world replay seam — GRANTED and BUILT (T05, `6f2e434`)

Will authorized the seam 2026-07-28; T05 delivered it. Both
`ReplayLoginPacket` and `ReplayWorldPacket` are now public in `eq.h`,
mirroring `ReplayZonePacket`. All three network surfaces now have a
replay seam, so **no further seam work is needed** — see the survey
below, which remains accurate.

**Known limitation, recorded deliberately:** the session-replay golden
exercises only the ZONE stream, so it did not independently validate
login/world behavior preservation. That rests on line-by-line review
(switch bodies moved verbatim, recorder placement confirmed in both
functions, guards additive-only so the live path is bit-identical when a
connection exists). If login/world behavior ever looks suspect, this is
the seam of least evidence.

**Next: T06 — thorough login/world flow coverage.** T05 shipped only a
5-test seam smoke suite so the base-layer refactor stayed reviewable.
The recorded login/world streams (5998 and 9000, ~1511 packets) are now
reachable and still unused by any state-level test.

## Original decision writeup (historical) — a login/world replay seam

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

## T06 findings

### 1. `LoginDisableReconnect` segfaults in replay mode (pinned, not fixed)

`eq_login_world.cpp:310` dereferences `m_login_connection_manager`
(`->OnConnectionStateChange`) and `m_login_connection` (`->Close`) with
**no null guard**, and `LoginProcessServerPlayResponse` reaches it from
the ordinary `HC_OP_PlayEverquestResponse` opcode.

**Not live-reachable.** In production both members are set
(`LoginOnNewConnection`, `ConnectToLogin`) before any packet can
arrive. This is a replay/test-mode crash. Pinned with `EXPECT_DEATH`;
the one record is excluded from the capture replay.

**It exposed an incomplete sweep in T05, and that is the plan's fault
(mine).** T05 scoped its guard sweep by *"`QueuePacket` call sites"* —
12 of them. `LoginDisableReconnect` dereferences via `->Close()` and
`->OnConnectionStateChange()`, so the sweep missed it **by
construction**. The correct predicate was "connection dereferences
reachable from the dispatch path".

Re-checked with the right predicate — 16 dereference sites in
`eq_login_world.cpp`:

| site | guarded? | seam-reachable? |
|------|----------|-----------------|
| 12 `QueuePacket` sites (T05) | yes | yes |
| `:59` `m_login_connection_manager->Connect` | no | no (status-change handler) |
| `:340` `m_world_connection_manager->Connect` | no | no (status-change handler) |
| `:312-313` `LoginDisableReconnect` | **no** | **yes** |

So T05's guards are correct as far as they go and the live path is
unaffected — but one seam-reachable hole remained. Fixing it is a
2-line guard in the same shape; **needs Will's authorization**, as
T05's was single-use.

### 2. `LoginProcessLoginResponse` underflow — confirmed, not worse than predicted

`Length() < 12` underflows `p.Length() - 12` to ~1.8e19;
`new char[]` throws `std::bad_alloc` cleanly. No corruption, no
sanitizer report. `Length() == 12` exactly throws `std::out_of_range`
from the subsequent bounds-checked read. Still worth a guard, but it is
not the memory-safety problem the shape suggested.

### 3. RDP start/stop family has another member

`RDPServerLifecycleTest.MultipleStartStop` failed in 1 of 3
orchestrator runs — an assertion failure with **no** kernel oops, so a
different manifestation from the `RDPServerFrameTest` GP fault, but the
same start/stop lifecycle. Further corroboration that
`RDPServer::stop()` is racy. Still the perf session's to fix.

### 4. Behaviors that could NOT be asserted (evidence for an accessor request)

No public accessor exists for: character-list contents and selected
index, server-list field contents, `m_dbid`, `m_key`, `m_world_ready`,
`m_enter_world_sent` (only inferable indirectly from send-count
deltas), UCS/chat-server parsed fields, the `WorldSendClientAuth`
zoning flag, and `m_zone_server_host`/`m_zone_server_port`.

Deep login/world state assertions remain impossible without read-only
accessors. Not requested yet — the sequencing and robustness coverage
landed first so any request comes with this concrete list behind it.

### Fixture note — RETRACTED, the orchestrator was wrong

This section previously claimed the T06 executor's fixture count was
incorrect, asserting 10 login S→C records including six zero-length
`0x0000`. **That claim was wrong and is withdrawn. The executor's count
of 4 was right.**

`SessionLogFileHeader` is **112 bytes** (`static_assert`,
`session_log.h:53`) — `magic`/`version`/`reserved`/`startTimestampUs`
plus `character[64]` and `zone[32]`. The orchestrator's ad-hoc parser
assumed a 16-byte header, so it began 96 bytes early and misread the
header's zeroed name fields as 96/16 = **exactly six** phantom `0x0000`
size-0 records.

Correct parse: **2943 records, 4 login S→C** — `0x16` (17B), `0x17`
(90B), `0x18` (60B), `0x21` (20B). No `0x0000` records exist.

Both the wrong and the right parse ended exactly at EOF, which is why
the orchestrator's sanity check gave false confidence: the phantom
records consumed precisely the 96 bytes that had been skipped.

Process lesson: the T06 plan explicitly told the executor to use the
`session_log.h` structs rather than hand-roll a parser — and the
orchestrator then overrode their correct result using exactly the
hand-rolled parser the plan had warned against. Corrected in T07
(`b6b3502`); the T06 merge commit message still carries the wrong
claim and cannot be rewritten now that it is merged.

## Needs seam

- ~~Login/world replay seam~~ — **BUILT in T05**.
- ~~`LoginDisableReconnect` null guard, `LoginProcessLoginResponse`
  length guard~~ — **DONE in T07** (`b6b3502`).
- **Open, needs Will:** read-only accessors for login/world state, if
  deep state assertions are wanted (T06 finding 4 lists exactly which).
  Deliberately not requested yet — the sequencing coverage may prove
  sufficient.

## T12 observations (action layer)

### The guard split is real behavior, now pinned

**12** dispatcher methods are `checkHandler()`-only and work while
disconnected: `stopMoving`, `stopAllMovement`, `stopFollow`,
`clearTarget`, `disableAutoAttack`, `interruptCast`, `openReplyToTell`,
`declineGroupInvite`, `cancelCamp`, `closeSpellbook`, `cancelTrade`,
`setPathfinding`. The other **81** require a zone connection.

That mostly tracks the stop/cancel/decline/interrupt naming. **One
asymmetry worth a look:** `setRoleplay` requires a zone connection while
its apparent sibling `setPathfinding` does not. Not obviously wrong —
pathfinding is arguably a client-side toggle — but the pair reads
symmetric and behaves differently, so it is worth a glance from whoever
owns that code.

`openReplyToTell` is guarded but never touches `m_handler` on success —
a UI stub. Pinned separately so a future implementation doesn't silently
change its contract.

### No mis-wiring — and how that null result was verified

A "no bugs found" result is only worth something if the search was real,
so the orchestrator re-checked it mechanically rather than accepting it:

- Of 91 dispatcher methods, **78** delegate to the identically-named
  handler method.
- All **13** that differ are legitimate: `faceLocation`/`faceEntity`
  compute a heading then call `setHeading`; `interactNearest` is a
  composite (`targetEntity` + `hailTarget` + `clickNearestDoor`);
  `toggleAFK` calls `setAFK`; and 9 `pet*` wrappers call
  `sendPetCommand`.
- The pet family is the one place name-matching is blind to a
  copy-paste error, so its command IDs were extracted directly:
  **0, 2, 4, 5, 6, 12, 15, 24, 28 — all distinct.**

### The no-handler sweep is a regression guard, not a finding

All 91 methods were already guarded before T12 (checked while planning).
The sweep passes and is recorded as protection against a future method
92 added without a guard — not as a discovered bug.

## Still open — `InputActionBridge`

391 lines at 0%. The plan's explicitly lowest-priority item; T12's first
three sections filled the unit and it stopped rather than rushing.
Cheap follow-on when wanted.

## Trap worth knowing — a stale BOOTSTRAP, not just stale artifacts

Found while reviewing T11. Symptom: `gles2_zone_benchmark` fails to link
with `undefined reference to irr::video::g_glCounters`, so
`cmake --build` returns non-zero even though nothing in the diff is
related.

Cause: `g_glCounters` is defined in
`docker/irrlicht-drm/COpenGLES2Driver.cpp`, which
`scripts/build-gles2-x86.sh` **copies into**
`third_party/irrlicht-gles2-x86/`. A worktree bootstrapped before the
perf session added the GL call counters has a `third_party` **source
tree** that predates the definition — so neither the archive nor the
copied source contains it. Re-running
`bash scripts/build-gles2-x86.sh --irrlicht-only` fixes it; the build
then goes clean.

This is the perf session's documented stale-artifact trap one level
deeper: their note covers stale *build artifacts*, this is a stale
*copied source tree*.

**Diagnostic caution:** a `git stash` test cannot tell "pre-existing repo
breakage" apart from "stale local artifact" — both reproduce without your
diff. T11's executor correctly concluded the failure wasn't theirs but
attributed it to the repo. If an unresolved Irrlicht symbol appears,
re-bootstrap before believing the repo is broken.

## T10 — mutation kill rate is a better signal than coverage (confirmed)

The benchmark session's intel was right. Measured on `wld_loader.cpp`,
the most-covered file in the repo (952 lines):

| | kill rate |
|---|---|
| before T10 | **0 of 8** |
| after T10 | **4 of 8** |

Our 0/8 baseline is *worse* than their reported 1/8 — recorded as
measured, not reconciled. Our mutation set simply landed entirely
inside the gap.

**Cause, for the record:** `WldIntegrationTest.WldLoader_ParseZone`
drove the entire parser and asserted only that it returned `true` and
produced ≥1 geometry. The file's other 296 assertions are real but sit
on synthetic fragment structs, so coverage came from 5 integration
tests while assertions came from the other 78.

**Still surviving after T10**, with reasons rather than a round number:
winding order (M2), per-mesh header bbox (M5), per-triangle
`textureIndex` (M7), zone-line coordinate slices (M8).

### Two dead-code findings (not fixed — `src/client/graphics/`)

- `WldLoader::resolveTextureName` (`wld_loader.cpp:612`) is **declared,
  defined, and called from nowhere** in `src/`, `include/` or `tests/`.
  Independently confirmed.
- The **per-mesh header bounding-box fields are read by nothing** in
  `src/` or `tests/` — which is why M5 is unkillable. That is a sharper
  statement than "untested": the data is dead-end, so no assertion can
  legitimately depend on it without inventing a consumer.

Both are graphics-territory; handed over rather than fixed.

### Practice note for future T-units

Kill-rate spot checks are cheap and worth running on any file whose
coverage looks reassuring. The technique — mutate, build the single
test target, run, revert — costs a few minutes per mutation, and the
plan-level guardrail that made it safe was requiring `git status` to
show no `src/` modification at commit time.

## T09 finding — chat-server info is silently discarded (needs Will)

`WorldProcessSetChatServer` (`eq_login_world.cpp:743`) requires **≥5**
comma-separated fields, documented as
`"host,port,server.character,connection_type,mail_key"`.

Independently verified against the capture — all four real records
carry exactly **four**:

```
['10.0.30.23', '7778', 'WillEQ Private Server.Testchar', 'B48FB5F37']
```

No `connection_type`, and the mail key sits at index **3**, not 4. So
`m_ucs_host` / `m_ucs_port` / `m_mail_key` are **never populated**
against this server; the handler takes its else branch and warns every
time.

**Severity is lower than it first looks:** nothing consumes those
fields today. `ConnectToUCS` is commented out (both the call site at
`eq_login_world.cpp:644` and the whole UCS block in `eq.cpp`), and the
separate `UCSConnection` class is never connected. So this is a latent
bug that bites whenever UCS is implemented, not a live breakage — which
is also why it went unnoticed.

### ANSWERED (2026-07-30) — our parser is wrong; the server is right

Will asked what the eqemu source does for a Titanium-era client. Checked
`/mnt/projects/akk-stack/code/world/client.cpp:1023` — the server builds
the string with:

```cpp
buffer = fmt::format("{},{},{}.{},{}{:08X}",
    ucs_addr, config->GetUCSPort(), config->ShortName, GetCharName(),
    static_cast<char>(connection_type), mail_key);
```

That is **4 comma-separated fields**, and the format string is
**era-independent** — the version `switch` above it only selects the
`connection_type` *character*, not the field layout. So no client era
gets 5 fields.

Field 4 is `<connection_type><mail_key:08X>` **concatenated with no
separator**. For Titanium (`emu_versions.h:124-125`):

- `ucsTitaniumChat = 'B'` → sent on `OP_SetChatServer`
- `ucsTitaniumMail = 'b'` → sent on `OP_SetChatServer2`

Which matches the capture exactly: `B48FB5F37` and `b48FB5F37` — one
type character plus 8 hex digits of mail key.

**So `WorldProcessSetChatServer` is simply wrong.** It treats connection
type and mail key as separate fields 3 and 4, requires `>= 5`, and reads
`parts[4]`. The correct parse is:

```cpp
if (parts.size() >= 4) {
    m_ucs_host = parts[0];
    m_ucs_port = std::stoi(parts[1]);
    // parts[2] is shortname.charname
    // parts[3] is <connection_type><mail_key>
    m_mail_key = parts[3].substr(1);      // drop the type character
}
```

The connection-type character is currently discarded entirely; a real
UCS implementation will need it to distinguish the chat and mail
connections, so it is worth storing rather than dropping.

**Status: FIXED in T13 (merged `9d456c1`, 2026-07-30), go-ahead from
Will.** The parser now implements the 4-field parse above, plus: the
connection-type character is stored (`m_ucs_connection_type`,
`GetChatConnectionType()`) rather than discarded, and the receive path
is guarded against malformed input — too few fields, empty field 4
(`substr(1)` would throw), non-numeric port (`stoi` throws), and
out-of-uint16-range port (`stoi` does NOT throw on e.g. 70000; without
an explicit range check it silently truncated through the cast). The
BUG_ pin and the synthetic 5-field contrast case were re-pointed as
prescribed; real-capture tests now assert 'B'/`OP_SetChatServer` and
'b'/`OP_SetChatServer2` both populate correctly.

## T18 finding — ChannelMessage exact-size crash (needs Will)

`ZoneProcessChannelMessage` (`eq_chat.cpp:90`) guards `p.Length() < 150`
then reads the message with `GetCString(150)`. A packet of **exactly
150 bytes** passes the guard, and the post-T08 bounds-checked
`GetCString` throws `std::out_of_range` — on the zone network receive
path, where nothing catches, so the client terminates.

Reachability: real servers always append a message (min real payload is
151 bytes), so this is malformed/hostile input only — but it is the
same crash class T05/T07 closed on the login path, and the zone stream
is the least-trusted input surface. Fix is a one-character guard change
(`< 151`), or a bounds-checked message read.

Pinned as `BUG_ChannelMessage_ExactMinimumSizeThrowsOutOfRange`
(`tests/test_zone_chat.cpp`); re-point at no-throw + empty-message when
the fix lands. Found by the T08 exact-size-allocation discipline — the
same probe with heap slack would have passed silently.

## T15 finding — spell-cast gem off-by-one — FIXED in T16 (merged `3253126`)

`InputActionBridge::processSpellCasts()` forwards
`SpellCastRequest.gemSlot` unchanged into `ActionDispatcher::castSpell`.
The contract mismatch:

- Both real producers are **0-based**: `ConsoleInputHandler` pushes
  `gem - 1` for `/cast 1..8` (`console_input_handler.cpp:411`);
  `GraphicsInputHandler` accepts gem clicks `0..7`
  (`graphics_input_handler.cpp:191`). The field is documented "0-7 for
  gem slots 1-8" (`input_handler.h:85`).
- The dispatcher is **1-based**: `castSpell` validates 1-12
  (`action_dispatcher.cpp:308`) and the real handler does
  `gemSlot - 1` into the `SpellSlot` enum (`eq_action_handler.cpp:285`).

Live effect: `/cast 1` and clicking spell gem 1 fail validation
("Invalid gem slot"); `/cast N` for N≥2 casts gem N−1. The hotbar path
(`processHotbarRequests`, `slot + 1`) is correct against the same
dispatcher — the asymmetry between two paths feeding the same call is
the tell.

**Fixed in T16** (go-ahead from Will, 2026-07-30): the bridge converts
with `+1` in `processSpellCasts()`, matching the hotbar path. Both
`BUG_` pins re-pointed at correct behavior (gem 0 → `castSpell(1)`;
gems 4/7 → 5/8, pinning mid and top of the producers' range). The
alternative (making the producers 1-based) was rejected — it touches
three files and the documented field contract.

## Character-list storage — RESOLVED in T14 (merged `4414a92`)

T06 asked for character-list contents; T09 correctly did not build it,
since exposing it means adding a member and populating it. Will
green-lit the storage 2026-07-30 (rationale: name/level/class/race/zone
is exactly what a Titanium character-select UI displays, so the state
is product-plausible, not test-only). `GetCharacterSelectList()` now
returns the occupied slots.

The unit also produced a real protocol finding: eqemu marks empty
Titanium slots with the literal name `<none>` (remaining fields zero).
The old loop's early return at the configured character's slot had
hidden this — nothing had ever parsed past the match. The handler now
filters the sentinel; without it the list carries seven phantom
entries on a 3-character account.

## `Packet::GetCString` OOB read — FIXED in T08 (`b2fee49`)

`src/common/net/packet.cpp` bounds-checked only `offset + 1` and then
built a `std::string` from the raw pointer, reading to the first NUL
wherever it fell — past the end of the buffer for any unterminated
string field. 35 call sites inherited it.

**Confirmed with AddressSanitizer, not inferred:** over an
exactly-sized 4-byte heap buffer with no terminator, the old logic
reports `heap-buffer-overflow READ of size 5` via `strlen` inside the
`std::string` constructor. New logic clean.

Why it survived: with a `DynamicPacket`, heap slack after the buffer
usually contains a zero byte and masks the over-read. It only shows on
an exactly-sized allocation — which is why T08's tests use
`StaticPacket` over exact-size buffers, and a lesson for any future
buffer-bounds test in this codebase.

Fix: `memchr` bounded by the packet, end-of-packet treated as a
terminator when none is found. Chosen over throwing because this is the
receive path, where an uncaught exception kills the client. Behavior
identical for well-formed data — the session-replay golden matched with
`session.golden` md5 unchanged.

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
- ~~**T02 merge verification finding**: canonical configure command
  broken (relative `EQT_GLES2_IRRLICHT_DIR` fails the `EXISTS`
  check)~~ — **CLOSED, fixed by `6b53e0d` + `8d13e8c` 2026-07-28**
  ("testing handback round 2"; the WARNING-visibility tweak in the
  second commit was itself testing-session review feedback, so the
  fix round-tripped through this channel — the entry just never got
  struck through afterwards, sorry). `CMakeLists.txt` now absolutizes
  the value against `CMAKE_SOURCE_DIR` before the `EXISTS` check, and
  falls back (with a WARNING) to the in-tree default when a
  wrongly-resolved path lacks the library. Perf-orchestrator
  re-verified 2026-07-30: the documented relative form configures
  clean from the source root against current fable tip. Original
  report kept below for the record:
  The canonical
  configure command in the root `CLAUDE.md` "Build Commands" did not
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
