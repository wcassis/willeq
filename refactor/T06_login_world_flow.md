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

Per the protocol, plus:
- The precise list of behaviors you could **not** assert for lack of a
  public accessor. Be specific — this is the evidence for the next
  latitude request, and a vague list is worthless.
- Exactly what `LoginProcessLoginResponse` does on a short packet.
- Whether any recorded-capture record failed to replay, and which.
