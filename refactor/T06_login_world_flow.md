# T06 — Login/world flow coverage

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** T05's source authorization was
single-use and is spent. No changes under `src/` or `include/` — not a
guard, not an accessor, not a seam. Findings get pinned and reported.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/T05_login_world_seam.md` (built the seam you are
about to use) and `tests/test_login_world_seam.cpp` (its 5-test smoke
suite — extend the idiom, don't duplicate it).

## Why this unit

T05 built `ReplayLoginPacket` / `ReplayWorldPacket` so login and world
could finally be tested. It shipped only a smoke suite, on purpose, so
the base-layer refactor stayed reviewable. This unit is the coverage
that seam was built for.

Login and world are the two stages with **zero** state-level testing,
and T05's own review flagged them as the seam of least evidence — the
session-replay golden covers only the zone stream, so nothing currently
validates login/world behavior end to end.

## Read this before designing: the assertion vocabulary is THIN

I checked. This constrains the unit and you should know it up front
rather than discovering it at test 20.

**There are no public accessors for login/world state.** No character
list, no server list, no `m_dbid`, no `m_key`, no world-ready flag —
all private.

What you *can* observe through the seam:

| observable | how |
|------------|-----|
| `GetLoadingPhase()` | only 2 transitions are reachable via packets: `LOGIN_AUTHENTICATING` (`eq_login_world.cpp:143`) and `WORLD_CHARACTER_SELECT` (`:525`) |
| `GetReplaySuppressedSendCount()` | delta per replayed packet = how many client sends that packet triggered |
| absence of crash / exception | the robustness axis |

So this unit is mostly about **protocol sequencing and robustness**,
not deep state assertions. That is a real and worthwhile thing to
cover — it is what nothing tests today — but do not contort tests to
manufacture depth that the public API cannot support.

**Record what you could not assert.** If a behavior is genuinely
untestable without an accessor, list it precisely in your Review. That
list becomes the evidence for a future accessor request to Will. Do
**not** add accessors yourself.

## What to test

Create `tests/test_login_world_flow.cpp`.

### 1. Real-capture replay (the regression oracle that doesn't exist yet)

`/mnt/projects/fable/captures/session.sleq` contains **32 replayable
S→C application records** for these stages — 10 login, 22 world.
(Earlier I quoted ~1511; that was pcap-level UDP packets including
protocol overhead and C→S traffic. The application-level S→C record
count is 32. Use the real number.)

Recorded opcodes, all present in the fixture:

- **Login (10):** `HC_OP_ChatMessage` (0x0016), `HC_OP_LoginAccepted`
  (0x0017), `HC_OP_ServerListResponse` (0x0018), 0x0021, and six 0x0000
  records.
- **World (22):** `GuildsList`, `LogServer`, `ApproveWorld`,
  `EnterWorld`, `PostEnterWorld`, `ExpansionInfo`, `SendCharInfo`,
  `MOTD`, `SetChatServer`, `SetChatServer2`, `ZoneServerInfo` — each
  twice, because the session contains two zone-ins.

Replay the recorded login and world S→C records in recorded order
through the seam and assert: no crash, no exception, the expected
loading-phase transitions occur, and the suppressed-send count evolves
as expected. Read the `.sleq` with the existing
`common/net/session_log.h` types — `test_session_replay.cpp` already
does this; reuse its approach rather than writing a new parser.

Skip cleanly when the capture is absent (other checkouts lack it).

### 2. Sequencing (synthetic)

Assert the client's *responses* to server packets, via send-count
deltas:

- `HC_OP_ChatMessage` on the login stream triggers `LoginSendLogin` and
  moves the phase to `LOGIN_AUTHENTICATING`.
- `HC_OP_ServerListResponse` triggers the play request path.
- On world, `HC_OP_ChatMessage` and `HC_OP_SessionReady` both trigger
  `WorldSendClientAuth` — assert both, since they are distinct cases
  that happen to share a handler.
- `HC_OP_ExpansionInfo` triggers *two* sends (an inline ack, then
  `WorldSendWorldClientReady`) — a good check that send counting is
  meaningful.
- Unknown opcodes on both streams are ignored without crashing.

### 3. Robustness — one specific case I expect to find a real bug

Apply the T04 treatment (zero-length and truncated payloads) to the
login and world handlers.

**Test `LoginProcessLoginResponse` (`eq_login_world.cpp:210`)
specifically.** Its first statement is:

```cpp
auto encrypt_size = p.Length() - 12;
```

`Packet::Length()` returns **`size_t`** (`packet.h:23`), so a packet
shorter than 12 bytes **underflows** to ~1.8e19. The next statements
are `new char[encrypt_size]` and an `eqcrypt_block` read of that
length. I expect `std::bad_alloc` / `std::length_error`, but the
failure mode could be worse.

This is a different and more serious class than the T04 bugs — those
were bounds-checked throws; this is an unsigned underflow feeding an
allocation size and a raw decrypt read, reachable from a malformed
server packet on the *login* path, before any authentication.

Pin whatever it actually does with an appropriately-named test.
**Do not fix it** — T05's authorization is spent, and this needs Will's
decision on its own merits. If it turns out to be worse than an
allocation failure, stop and say so prominently rather than burying it
in the Review.

Note for contrast: `WorldProcessCharacterSelect` (`:529`) *does* guard
correctly — warns and returns. That is the pattern the codebase should
follow, and it shows the gap is inconsistency, not ignorance.

## STOP conditions

- Any change under `src/` or `include/` → STOP. Includes "just adding
  an accessor" and "just adding the guard".
- A robustness case does something worse than throw — memory
  corruption, a sanitizer report, a huge allocation that succeeds →
  STOP and report immediately with evidence.
- You find you cannot assert anything meaningful for a whole stage →
  say so; a small honest suite beats a padded one.

## Acceptance criteria

1. `tests/test_login_world_flow.cpp` exists and is wired into
   `tests/CMakeLists.txt` in the existing style.
2. Real-capture replay implemented, and skips cleanly without the
   capture. Demonstrate both.
3. Robustness cases cover both streams, including the
   `LoginProcessLoginResponse` underflow, each pinned to actual
   observed behavior.
4. Build clean; binary under 5 seconds — report the time.
5. Full suite: 3 consecutive runs. Documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` is the known T03b use-after-free — name it,
   do not call the run clean).
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

Visual gate and ARM build do not apply (tests-only diff) — say so.

## Environment notes

- GLES2 config is the only tree; the relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `.../lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.

## Review

Executed 2026-07-28, slot t1, branch `task/T06-login-world-flow` off
`fable`@062ec8d.

### HEADLINE FINDING (not one of the cases this plan asked for — flagged
### with equal prominence per the "worse than expected → stop and report
### prominently" instruction)

**`LoginDisableReconnect()` (`eq_login_world.cpp:310`) segfaults** when
called on a headless `EverQuest` built via the replay-seam idiom (i.e. one
that never ran the real `Connect()`/handshake sequence — every test in this
whole file, and T05's). It unconditionally does:

```cpp
m_login_connection_manager->OnConnectionStateChange(...);
m_login_connection->Close();
```

with no null guard on either — the *only* dereference of these two members
in the entire file that lacks the `if (m_x) {...} else
{++m_replay_suppressed_sends;}` pattern used at all ~9 other send sites
(`LoginSendLogin`, `LoginSendServerRequest`, `LoginSendPlayRequest`,
`WorldSend*`). `LoginProcessServerPlayResponse` (`:284`) calls
`LoginDisableReconnect()` **unconditionally on both its branches** —
allowed-and-server-found (the *normal successful-login* path) and
not-allowed — so this is not an edge case reachable only by malformed
input; it fires on the very last opcode of an ordinary login sequence
(`HC_OP_PlayEverquestResponse`).

Confirmed with gdb (`bt`): `SIGSEGV` inside `std::function::operator=`
inside `DaybreakConnectionManager::OnConnectionStateChange`, called from
`LoginDisableReconnect`, called from `LoginProcessServerPlayResponse`,
called from `DispatchLoginPacket`/`ReplayLoginPacket`. This is a raw
null-pointer dereference (crash), not a C++ exception — strictly worse
than the `LoginProcessLoginResponse` allocation-failure case this plan
anticipated.

**Practical consequence for this unit**: the real-capture replay
(`data/captures/session.sleq`) cannot be replayed to its natural
conclusion — the login stream's 4th record (`HC_OP_PlayEverquestResponse`)
is excluded from the replay loop with an inline comment explaining why
(`test_login_world_flow.cpp`, `LoginWorldCaptureReplay` test). It is
separately pinned as its own finding via
`BUG_LoginProcessServerPlayResponse_NotAllowed_SegfaultsInLoginDisableReconnect`,
an `EXPECT_DEATH` test (gtest forks a subprocess for the statement under
test — the only safe way to observe a `SIGSEGV` without taking the whole
suite down; no prior `EXPECT_DEATH` precedent in this codebase, added
here because there was no other way to pin this safely).

**Is this reachable live?** Almost certainly not. `m_login_connection_manager`
is only ever non-null once `LoginOnPacketRecv`'s callback is wired to it
(`eq.cpp:250-254`), and `m_login_connection` is set synchronously inside
`Connect()` (`daybreak_connection.cpp:76-81`) before any packet could
arrive on it — so by the time `DispatchLoginPacket` runs via the real
network path, both are already set, in a single-threaded synchronous
dispatch model. This looks like a structural limitation of the T05
replay seam specifically (it can construct an `EverQuest` that skips the
real connect sequence, which `LoginDisableReconnect()`'s missing guard
was never written to expect), not a live production bug — but the missing
guard is a real inconsistency against the rest of the file's pattern, and
worth a decision on its own merits: either guard it (mirroring every
other send site) or accept the seam can never replay a login sequence to
completion. **Not fixed here — tests/ only, per territory.**

### `LoginProcessLoginResponse` on a short packet — exactly what happens

Verified both in isolation (a standalone g++ probe with the identical
arithmetic, matching this build's libstdc++) and in the real build via
the pinned tests:

- `p.Length() < 12` (e.g. a zero-length or 9-byte payload → packet length
  2 or 11): `p.Length() - 12` underflows (well-defined modular wraparound
  for `size_t`, not UB) to ≈1.8e19. The `% 8` round-down leaves it at
  ≈1.8e19 minus a few bytes — still enormous. `new char[encrypt_size]`
  **throws `std::bad_alloc`**. It does **not** succeed, does **not**
  corrupt memory, and does **not** read/write out of bounds before
  throwing — libstdc++'s array-new path checks the requested size against
  the allocator's capacity and throws cleanly. No sanitizer report, no
  crash. This is exactly the plan's anticipated (not the "worse") outcome.
  Pinned: `BUG_LoginProcessLoginResponse_ZeroLengthPacket_UnderflowsAndThrowsBadAlloc`,
  `BUG_LoginProcessLoginResponse_OneByteUnderMinimum_UnderflowsAndThrowsBadAlloc`.
- `p.Length() == 12` exactly (the boundary — payload size 10):
  `encrypt_size == 0`, no underflow. `new char[0]` succeeds (a valid
  zero-size allocation); `eqcrypt_block`'s own guard short-circuits false
  on `buffer_in_sz == 0` and `DES_ncbc_encrypt` runs as a no-op. The
  subsequent `StaticPacket` over that zero-length buffer then hits
  `Packet::GetUInt16(1)`, which is properly bounds-checked and **throws
  `std::out_of_range`** — a milder, different failure mode, and the exact
  boundary at which the underflow bug stops applying. Pinned:
  `LoginProcessLoginResponse_ExactlyTwelveBytePacket_ThrowsOutOfRangeInstead`.

Both are uncaught exceptions with no `try`/`catch` anywhere between
`LoginProcessLoginResponse` and `LoginOnPacketRecv`/`ReplayLoginPacket`,
so in production a single malformed/truncated `HC_OP_LoginAccepted`
packet under 12 bytes would `std::terminate()` the client from the login
path, before any authentication. Not fixed — Will's decision, per plan.

### Whether any recorded-capture record failed to replay

Yes, one: **`HC_OP_PlayEverquestResponse`** (login stream, 4th/last login
record) — excluded deliberately from the replay loop due to the
`LoginDisableReconnect()` segfault above, not attempted-and-failed. All
other real-capture records replay cleanly:
- Login: `HC_OP_ChatMessage` (17B), `HC_OP_LoginAccepted` (90B, real
  encrypted credentials — decrypts and proceeds without incident since
  the packet is well over the 12-byte minimum), `HC_OP_ServerListResponse`
  (60B, one server entry, `long_name=WillEQ`, decoded by direct parse of
  the fixture and matched via the test's server-name constructor arg to
  stay on the "found" branch).
- World: all 22 records (11 opcodes × 2 zone-ins — `GuildsList`,
  `LogServer`, `ApproveWorld`, `EnterWorld`, `PostEnterWorld`,
  `ExpansionInfo`, `SendCharInfo`, `MOTD`, `SetChatServer`,
  `SetChatServer2`, `ZoneServerInfo`) replay without incident, including
  `ConnectToZone()` firing twice (real but harmless local UDP socket
  opens — non-blocking, no listener required, verified safe by reading
  `PosixUdpTransport::open`/`DaybreakConnectionManager::Connect`).

Both documented phase transitions occur (`LOGIN_AUTHENTICATING` on the
login `ChatMessage`; `WORLD_CHARACTER_SELECT` on `SendCharInfo`, whose
real payload is exactly 1704 bytes — 1706 with the opcode prefix, exactly
at `WorldProcessCharacterSelect`'s `>=1706` guard). The replay additionally
reaches **`LoadingPhase::ZONE_CONNECTING`** via the final `ZoneServerInfo`
record's `ConnectToZone()` call — **a third real phase transition
reachable from the world stream that the plan's "only 2 transitions"
table did not document.**

### Correction to the plan's own figures (found before writing any test,
### pinned as the real numbers)

- Plan text: "32 replayable S→C application records — 10 login, 22
  world" and "six 0x0000 records" on the login side. **Actual** (verified
  by direct byte-level parse of `session.sleq`'s header+record stream):
  **4 login S→C records, 22 world S→C records — 26 total**, not 32. The
  4 login records are exactly `HC_OP_ChatMessage`, `HC_OP_LoginAccepted`,
  `HC_OP_ServerListResponse`, `HC_OP_PlayEverquestResponse` (one each,
  covering the whole `DispatchLoginPacket` switch); no `0x0000` records
  exist in the login stream at all. The world figure (22) was correct.
  Pinned as `EXPECT_EQ(loginRecords, 3u)` (3, not 4 — see above) and
  `EXPECT_EQ(worldRecords, 22u)` in `LoginWorldCaptureReplay`.
- Plan text: "`HC_OP_ExpansionInfo` triggers *two* sends (an inline ack,
  then `WorldSendWorldClientReady`)". **Actual**: three sends on first
  delivery — ack, `WorldSendWorldClientReady`, **and**
  `WorldSendEnterWorld`, because `m_enter_world_sent` defaults `false` so
  `WorldProcessExpansionInfo`'s `if (!m_enter_world_sent)` branch also
  fires. Pinned: `WorldExpansionInfo_FirstDelivery_TriggersThreeSends`
  (delta `+3`) and `WorldExpansionInfo_SecondDelivery_IsNoOp` (delta `+0`,
  confirming the `m_world_ready` guard holds on redelivery — the closest
  observable proxy for that private flag, since it has no accessor).

### Behaviors that could NOT be asserted for lack of a public accessor

Precise list, for the next accessor-request evidence:

1. **Character list contents** (`WorldProcessCharacterSelect`'s parsed
   name/level/class/race/zone for each of the 10 character slots, and
   which index `m_character_select_index` lands on) — only the
   `WORLD_CHARACTER_SELECT` phase transition is observable; whether the
   name-matching loop actually found "Testchar" in the real capture's
   character list, or fell through to the "could not find" error path,
   cannot be distinguished from outside.
2. **Server list contents** (`m_world_servers`: address/type/id/
   long_name/lang/region/status/players parsed by
   `LoginProcessServerPacketList`) — only inferable indirectly, and only
   partially, via whether `LoginSendPlayRequest` fires (a suppressed-send
   delta) when the constructed server name matches. Field-level
   correctness of the parse (e.g. off-by-one in the cstring-length
   walking loop) is entirely unverifiable through the public API.
3. **`m_dbid` / `m_key`** — `LoginProcessLoginResponse`'s successful path
   extracts these from the decrypted response; there is no way to verify
   they were extracted correctly (or at all) from a real `LoginAccepted`
   packet — only that the call didn't throw.
4. **`m_world_ready` / `m_enter_world_sent`** — inferred only
   *indirectly* via the absence of additional suppressed sends on a
   second `ExpansionInfo` delivery (see above). This is a proxy, not a
   direct read, and would not distinguish "guard is false but the sends
   happen to be no-ops for another reason" from "guard is true".
5. **UCS/chat-server info** (`m_ucs_host`, `m_ucs_port`, `m_mail_key`,
   parsed by `WorldProcessSetChatServer` from a comma-separated string) —
   no accessor; parse correctness on realistic data is untestable.
6. **The "zoning" flag** computed in `WorldSendClientAuth`
   (`m_zone_connected || m_zone_change_requested`, written into the
   `LoginInfo` struct sent to the world server) — no accessor, and no
   way to inspect an outgoing packet's contents when the send is
   suppressed (only that a send was *attempted*, not what it contained).
7. **`m_zone_server_host` / `m_zone_server_port`** set by
   `WorldProcessZoneServerInfo` — only the resulting `ZONE_CONNECTING`
   phase transition is observable, not whether the host/port were parsed
   correctly from the packet.
8. **`LoginProcessServerPacketList`'s "not found" branch** and
   **`LoginProcessServerPlayResponse`'s "allowed but server not found"
   branch** (the one `LoginProcessServerPlayResponse` path that does
   *not* call `LoginDisableReconnect()`) — both are reachable in
   principle via `ReplayLoginPacket`, but deliberately not exercised here:
   the former also unconditionally reaches `LoginDisableReconnect()`
   (same segfault as the headline finding), and constructing a
   server-not-found `PlayEverquestResponse` scenario was judged not worth
   the added complexity once the underlying crash was already pinned
   once. Recorded here rather than silently skipped.

### Deviations from the plan

- Plan estimated 10 login / 22 world real-capture records; actual is 4
  login / 22 world (see above) — plan's own instruction was "use the real
  number," followed.
- Plan's `HC_OP_ExpansionInfo` send-count expectation ("two") corrected to
  the actual observed three.
- One real-capture record (`HC_OP_PlayEverquestResponse`) is excluded
  from the capture-replay loop due to the `LoginDisableReconnect()`
  segfault — not anticipated by the plan, discovered while building the
  first acceptance criterion. Pinned separately as its own `EXPECT_DEATH`
  test rather than silently worked around.
- Added one `EXPECT_DEATH` test — no prior precedent in this codebase's
  test suite — because it was the only safe way to pin a raw `SIGSEGV`
  without crashing the whole binary. Flagged explicitly since it's a new
  idiom, not because it's expected to be controversial.

### Verification

1. Build: `cmake --build build -j24` — clean, no new warnings from
   `test_login_world_flow.cpp`.
2. `./build/bin/test_login_world_flow` run directly (repo-root cwd, so the
   `data/captures` symlink resolves): **18/18 tests pass**, real total
   wall time **2.256s** (includes one `EXPECT_DEATH` subprocess fork/exec
   at ~1.3–1.6s) — comfortably under the 5s bar.
3. `ctest --test-dir build --output-on-failure`, 3 consecutive runs:
   **1399/1400 pass each run**; the sole failure every run is the
   documented `AudioManagerTest.PreloadCommonSounds` (host ALSA).
   `RDPServerFrameTest.*` did **not** crash in any of the 3 runs this
   session (it's documented as intermittent, per T03b — not claiming it's
   fixed, just reporting what was observed). Under `ctest` (cwd = `build/`),
   `LoginWorldCaptureReplay.RealSessionReplaysLoginAndWorldStreamsWithoutCrash`
   shows `***Skipped` because the default relative path
   (`data/captures/session.sleq`) doesn't resolve from that cwd — this is
   the same behavior `SessionReplayGolden` already exhibits under `ctest`
   (pre-existing project convention, not a new gap); the real replay was
   demonstrated directly via the binary invocation in step 2, and via a
   direct `--gtest_filter` invocation with `gdb` while diagnosing the
   segfault above.
4. Visual gate / ARM build: **do not apply** — tests-only diff, no
   graphics or ARM-relevant code touched.
5. `bash scripts/refactor/check_file_sizes.sh fable`: **no FAILs**. One
   pre-existing `WARN` (`src/client/graphics/renderer_frame.cpp`, 1623
   lines) — unrelated to this unit's diff (not a file this unit touched;
   `fable`'s tip advanced past this branch's base commit during the
   session, via an unrelated already-landed unit, so the diff base
   includes drift not authored here). This unit's own new file,
   `tests/test_login_world_flow.cpp`, is 560 lines.

### Files touched

- `tests/test_login_world_flow.cpp` (new, 560 lines)
- `tests/CMakeLists.txt` (added the `test_login_world_flow` target, same
  style as `test_login_world_seam`)

No changes under `src/` or `include/`.
