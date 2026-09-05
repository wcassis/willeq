# T379 — login and world sends through PacketSender (B146)

Class B, slot3, no server. Read the B146 board text, T376's §Review
(the census and the STOP that boarded this), `include/client/packet_sender.h`
and `src/client/packet_sender.cpp` (how `SafeQueueZone` feeds the T268
seam and the R01 recorder), and `src/client/login_world/login_world_component.cpp`
(the 12 direct sends: 4 on `m_login_connection`, 8 on `m_world_connection`).

1. Give `PacketSender` per-stream entry points `QueueLogin(...)` and
   `QueueWorld(...)` with the same three-step body as `SafeQueueZone`
   (T268 seam → recorder with `SessionStream::Login`/`World`,
   ClientToServer → the connection's queue), each null-safe (a missing
   connection logs and returns; no dereference). If `PacketSender` does not
   hold the login/world connections, give it what `login_world_component`
   holds (read how the zone connection reaches it — same route).
2. Route the 12 sends; delete wrappers that guard nothing else.
3. Tests, pre-fix failing: (a) each stream's sender with no connection
   returns without crashing; (b) a recorded session of a login handshake
   contains the client's side (`OP_SessionReady`/login request, world
   `OP_SendLoginInfo`/`OP_EnterWorld` — whichever fable sends; name them
   from the code) — T376's `RecordedSession_*` shape.
4. Census pin: extend `tests/test_direct_sender_census.cpp` if T378 has
   landed it on your tip (check `git log fable`), else add one for this TU
   alone — no `->QueuePacket(` outside `PacketSender` in
   `login_world_component.cpp`.
5. Strike B146.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, the login/world
goldens if any (grep `tests/goldens` for login), file sizes. No graphics.

STOP: the login stream's sends happen before `PacketSender` exists (then
report the construction order and route only the world sends).

## Review

Executed 2026-09-05 in `wt/slot3`, branch `task/T379-login-world-senders` off
`fable` at `c0e5bfa3` (verified at checkout). Two code commits plus this doc
commit. **No STOP condition hit** — see "The STOP that did not fire" below.
No graphics, no server, no re-bless, goldens untouched.

| commit | subject |
| --- | --- |
| `e9ee0220` | PacketSender gains per-stream QueueLogin/QueueWorld entry points |
| `f08369db` | route the 12 login/world sends through PacketSender (+ tests) |
| (this one) | B146 struck, this Review |

### The STOP that did not fire

The plan's STOP was "the login stream's sends happen before `PacketSender`
exists". They do not. `eqt::PacketSender` is a **member** of `EverQuest`,
constructed in its initialiser list; `ConnectToLogin()` is called from
`Application` long after the `EverQuest` constructor has returned. So both
streams were routable and all 12 sends went through.

There is a real ordering constraint one level down, and it is what shaped the
binding: `PacketSender` is constructed in the initialiser list, but the login
and world connections belong to `LoginWorldComponent`, which
`EverQuest`'s constructor allocates in its **body**. A reference member
(the shape `m_zone_connection` uses) cannot be bound to a member of an object
that does not exist yet. Hence `BindLoginWorldStreams(&LoginConnection(),
&WorldConnection())`, called from the constructor body right before
`SetCredentials`, storing two `std::shared_ptr<DaybreakConnection>*`. The
component is heap-allocated (`std::unique_ptr`) and never moved, so the
addresses are stable for the client's life, and both entry points are null-safe
on the pointer as well as on the pointee.

### The entry points

`QueueLogin(EQ::Net::Packet&)` / `QueueWorld(EQ::Net::Packet&)`, both
returning `bool`, both delegating to one private `QueueOnStream(slot, stream,
module, name, p)`. The body is `SafeQueueZone`'s three steps —

1. `Test_RecordQueuedPacket` (T268 seam), **before every gate**;
2. null check -> `LOG_WARN` + `NoteSuppressedSend()` + `return false`;
3. `m_session_recorder->record(SessionStream::Login|World, ClientToServer, ...)`,
   then `(*slot)->QueuePacket(p)`.

Four pieces of `SafeQueueZone` were deliberately **not** carried over, each
because it is zone-specific rather than because it was inconvenient:

| dropped | why |
| --- | --- |
| `stream` / `reliable` arguments | all 12 call sites sent reliably on stream 0 — `DaybreakConnection::QueuePacket`'s defaults |
| the async hop off `m_main_thread_id` | the login/world handshakes run on the thread that owns their managers, and the 12 bodies called `QueuePacket()` synchronously; adding the hop would be a behaviour change, not a port |
| the zero-stats "connection appears invalid" guard | it is `stats.recv==0 && stats.sent==0 && m_game_state.world().isZoneConnected()` — `isZoneConnected()` is false for the whole login and world handshake by definition, so the guard could never fire. **This is why no test harness in this unit needed the `conn->m_stats.sent_packets = 1` line T346/T360/T376 all had to add.** |
| the catch-and-reset | `SafeQueueZone` clears `m_zone_connection` and marks the zone disconnected on an exception; login and world have their own reconnect paths (`LoginOnStatusChange*` / `WorldOnStatusChange*`) and must not be torn down from inside a send |

### The 12 sites routed

All in `src/client/login_world/login_world_component.cpp`. Each replaced the
same five lines (`if (conn) { conn->QueuePacket(p); } else {
ctx_.net.NoteSuppressedSend(); }`) with one call, so the null guard and the
counter bump moved into the entry point rather than disappearing.

| # | function | opcode | new call |
| --- | --- | --- | --- |
| 1 | `LoginSendSessionReady` | `HC_OP_SessionReady` | `ctx_.net.QueueLogin(p)` |
| 2 | `LoginSendLogin` | `HC_OP_Login` | `ctx_.net.QueueLogin(p)` |
| 3 | `LoginSendServerRequest` | `HC_OP_ServerListRequest` | `ctx_.net.QueueLogin(p)` |
| 4 | `LoginSendPlayRequest` | `HC_OP_PlayEverquestRequest` | `ctx_.net.QueueLogin(p)` |
| 5 | `WorldSendClientAuth` | `HC_OP_SendLoginInfo` | `ctx_.net.QueueWorld(p)` |
| 6 | `WorldSendEnterWorld` | `HC_OP_EnterWorld` | `ctx_.net.QueueWorld(p)` |
| 7 | `WorldSendApproveWorld` | `HC_OP_ApproveWorld` | `ctx_.net.QueueWorld(p)` |
| 8 | `WorldSendWorldClientCRC` | `HC_OP_World_Client_CRC1` | `ctx_.net.QueueWorld(p)` |
| 9 | `WorldSendWorldClientCRC` | `HC_OP_World_Client_CRC2` | `ctx_.net.QueueWorld(p)` |
| 10 | `WorldSendWorldClientReady` | `HC_OP_WorldClientReady` | `ctx_.net.QueueWorld(p)` |
| 11 | `WorldSendWorldComplete` | `HC_OP_WorldComplete` | `ctx_.net.QueueWorld(p)` |
| 12 | `WorldProcessExpansionInfo` (inline ack) | `HC_OP_AckPacket` | `ctx_.net.QueueWorld(ack)` |

**No wrapper was deletable.** Every one of these 12 functions builds its own
packet body — none was a pure forwarder guarding nothing else — so the plan's
step 2 clause about deleting wrappers has no subject here.

Comment-filtered `->QueuePacket(` count in the TU after the routing: **0**.
(The raw grep hits 1, in the new file-header comment that quotes the old
shape; the census test below filters `//` lines, which is why it is green.)

### Tests

`tests/test_login_world_senders_packet_sender.cpp` (new target in
`tests/cmake/action_handlers.cmake`, `WILLEQ_SOURCE_DIR` defined for the
census case), 7 cases, **three** observers: the replay-suppression counter,
the T268 ordered-capture seam, and a real `SessionRecorder` written to a temp
`.sleq` and read back off disk — per stream, since the whole point of B146 is
the two streams nothing ever wrote.

Pre-fix state = the routing commit's `login_world_component.cpp` hunk reverted
via an exported patch + `git apply -R` (never `git stash`), entry points left
in place, then that one target rebuilt.

| test | pre-fix | post-fix |
| --- | --- | --- |
| `LoginWorldSendersPacketSender.NoLoginConnection_SendersReturnWithoutCrashing` | PASS | PASS |
| `…NoWorldConnection_SendersReturnWithoutCrashing` | PASS | PASS |
| `…UnboundStreams_QueueLoginAndQueueWorldRefuse` | PASS | PASS |
| `…OrderedCaptureSeam_SeesTheLoginAndWorldHandshake` | **FAIL** — seam vector empty; expected the 11-opcode sequence, got `{}` | PASS |
| `…RecordedSession_ContainsTheClientLoginHandshake` | **FAIL** — 0 of 4 (`OP_SessionReady`, `OP_Login`, `OP_ServerListRequest`, `OP_PlayEverquestRequest`) in the `.sleq` | PASS |
| `…RecordedSession_ContainsTheClientWorldHandshake` | **FAIL** — 0 of 7 (`OP_SendLoginInfo`, `OP_ApproveWorld`, `OP_World_Client_CRC1`, `OP_World_Client_CRC2`, `OP_WorldClientReady`, `OP_EnterWorld`, `OP_WorldComplete`) | PASS |
| `…CensusPin_NoDirectQueuePacketInTheLoginWorldTU` | **FAIL** — first offender at line 190 | PASS |

Pre-fix binary run: **exit 1, 3 passed / 4 failed.** Post-fix: **exit 0,
7 passed.**

**The three null cases are green in both states, on purpose, and that is the
point of them here.** Unlike T376's B140 sites these senders were already
null-guarded, so there is no crash to fix; what the cases pin is that the
routing did not *lose* the guard or the accounting. They assert the exact
suppressed-send delta (4 on the login stream, 7 on the world stream — six
calls, seven packets, because `WorldSendWorldClientCRC` sends CRC1 and CRC2),
so a routing that dropped the `NoteSuppressedSend()` contract would go red.
The third reaches the other null axis by unbinding the slots
(`BindLoginWorldStreams(nullptr, nullptr)`) and asserting both entry points
return false — a case that cannot exist at all before the entry points do, and
which does not compile against `c0e5bfa3`.

Bodies are asserted, not just opcodes: the login-stream records carry the
sequence counter 2, 3, 4, 5 in that order (it initialises to 2, not 0 — see
the surprise below), `OP_EnterWorld` carries `"Testchar"` at payload offset 0,
and `OP_SendLoginInfo` carries the account id `"4242"` as decimal text.

**Login/world goldens: none affected.** `grep -ril login tests/goldens/`
returns two files, and neither is a login/world *packet* golden:
`tests/goldens/application_init_order_g04.txt` is G04's statement-text list for
`application*.cpp` and its four "login" hits are the loading-screen strings and
`LoginWorld().ConnectToLogin()` on the Application side, a TU this unit does not
touch; `tests/goldens/contact_sheet.html` is the visual-gate contact sheet. No
golden names a login or world opcode, and none needed a re-bless — the full
`ctest -j6` below includes every golden test, all green.

**Census pin scope, per plan step 4:** `git log fable` at this tip has no
T378 commit and `tests/test_direct_sender_census.cpp` does not exist, so the
pin is the single case above, scoped to this TU alone, as the plan's "else"
branch directs.

### Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` (GLES2 tree) | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3980 / 3981**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` (waived ALSA quirk) |
| goldens | `git status --porcelain tests/goldens/` **empty** |
| `check_file_sizes.sh fable` | 5 files checked, **5 ok / 0 warn / 0 fail** (largest touched: `src/client/core/everquest.cpp` 1107; new test file 369; `login_world_component.cpp` 968) |
| `DocCitations` / `DocClaims` / `DocCounts` / `FileSizeCeiling` | all PASS (inside the `hygiene` label, 4 tests) |
| visual gate / `build-gles2` / ARM | not run — the plan narrows the bar ("No graphics") |

Suite 3968 (T376's final) -> 3981: +7 from this unit, the rest from `fable`
moving on between the two units.

### Surprises worth carrying forward

1. **`m_login_sequence` initialises to 2, not 0** (`login_world_component.h`).
   The first draft of the recorded-session case asserted 0..3 and went red
   post-fix with 2..5 — which briefly looked like a routing defect and was
   not. Worth knowing before anyone else writes an assertion over the login
   sequence field.
2. **The zero-stats guard that has bitten four consecutive units does not
   exist on these streams.** T346, T360 and T376 each had to add
   `conn->m_stats.sent_packets = 1` to a harness because `SafeQueueZone`
   refuses a never-round-tripped `FakeUdpTransport` connection. `QueueLogin` /
   `QueueWorld` deliberately do not carry that guard (it is gated on
   `isZoneConnected()`, false throughout the handshake), so the harness here
   needs no such line. Anyone porting a zone harness to these streams should
   not copy it in.
3. **One exit-143 build**, on the first full build of the unit — same shape
   T376 recorded, the static link against a 322-member `libwilleq_core.a`.
   Post-143 sweep run per protocol: no `build*/lib/st??????` ar temps, both
   archives present and non-empty by member count (`libwilleq_core.a` **322**,
   `libEtcLib.a` **15**), 273 binaries in `build/bin` scanned for ELF magic and
   a 10 KB floor, **bad=0**; gmake had printed its own `Deleting file` lines
   for the killed targets, which were the only casualties. The relaunched
   build produced them, EXIT=0. Nothing in the log matched `error:` — every
   failure line was `Terminated`.
4. **A new test target needs an explicit `cmake -S . -B build` reconfigure**
   in this tree before `--build --target <name>` will find it; the first
   attempt died with "No rule to make target", not a compile error. Cheap, but
   it reads like a broken cmake edit for a minute.
