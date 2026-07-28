# T05 — Login/world replay seam + truncated-packet guards

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**This is the testing campaign's first unit that modifies base-layer
source.** Will authorized it explicitly (2026-07-28), widening the
charter's "minimal seam" latitude for this specific work, with Batch R's
existing zone seam as the named template. That authorization is
narrow — see Territory.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/testing_progress.md` ("Decision needed from Will",
now granted) and `refactor/T04_zone_connection_flow.md`.

## Territory — read carefully, this is wider than previous units

**ALLOWED to modify:**
- `include/client/eq.h` — two new public methods + two private
  dispatcher declarations.
- `src/client/eq_login_world.cpp` — dispatcher extraction, null guards.
- `src/client/eq.cpp` — the two truncated-packet guards ONLY
  (`ZoneProcessTimeOfDay` lives in `eq_entities.cpp`, see Part B).
- `src/client/eq_entities.cpp` — the `ZoneProcessTimeOfDay` guard ONLY.
- `tests/**`.

**NOT allowed**, still: `src/client/graphics/**`, anything else under
`src/`, opportunistic cleanups, reformatting, or "while I was in here"
changes. Every source hunk must be traceable to Part A or Part B below.
If you find another bug, report it — do not fix it.

## Part A — login/world replay seam

### What exists (the template — mirror it, do not invent)

Zone works because Batch R did **two** things:

1. Split the opcode switch out of `ZoneOnPacketRecv` (`eq.cpp:717`)
   into `DispatchZonePacket` (`eq.cpp:749`).
2. Guarded **every** zone sender against a null connection, e.g.
   `eq.cpp:1627`:

```cpp
    DumpPacket("C->S", HC_OP_ZoneEntry, p);
    if (m_zone_connection) {
        m_zone_connection->QueuePacket(p);
    } else {
        ++m_replay_suppressed_sends;
    }
```

There are 11 such guards in `eq.cpp`. The public seam is
`ReplayZonePacket` (`eq.h:697`), which builds a `DynamicPacket` with the
2-byte opcode prefix and calls the dispatcher.

### What to do

**A1. Extract the dispatchers.** `LoginOnPacketRecv`
(`eq_login_world.cpp:70`) and `WorldOnPacketRecv` (`:319`) both follow
the same shape: log → `GetUInt16(0)` → `DumpPacket` → session-recorder
record → `switch`. Move **only the `switch`** into new private
`DispatchLoginPacket(const EQ::Net::Packet&)` /
`DispatchWorldPacket(...)`, and have `*OnPacketRecv` call them.

> **CRITICAL — the session-recorder call must STAY in `*OnPacketRecv`,
> above the dispatcher call.** It must NOT move into the extracted
> dispatcher. Zone does it this way deliberately; the comment at
> `eq.cpp:725` says why: replayed packets enter via
> `Replay*Packet → Dispatch*Packet` and must never be re-recorded. Get
> this wrong and every replay corrupts the session log, and the
> recording workflow that produced our fixtures silently breaks.

`DumpPacket` and the opcode read may stay in `*OnPacketRecv` (matching
zone) — the point is that the *dispatch* is separately callable.

**A2. Guard the senders.** 11 sender functions in
`eq_login_world.cpp` make **12** unguarded `QueuePacket` calls on
`m_login_connection` / `m_world_connection` (e.g.
`LoginSendSessionReady` at `:109`). Apply the exact zone pattern above,
reusing the existing `m_replay_suppressed_sends` counter and its
existing public accessor — do not add a second counter.

This is not optional and not reducible by picking "safe" opcodes:
`LoginOnPacketRecv`'s **first** case (`HC_OP_ChatMessage`) calls
`LoginSendLogin()`.

**A3. Add the public seam.** In `eq.h`, mirroring `ReplayZonePacket`'s
declaration and doc comment exactly:

```cpp
void ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size);
void ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size);
```

**A4. Smoke-test the seam only.** Add a *small* test proving the seam
works: replay one login packet and one world packet, assert an
observable state change and a `GetReplaySuppressedSendCount()` delta,
and assert an unknown opcode is ignored safely.

**Do NOT write thorough login/world flow coverage in this unit.** That
is deliberately deferred to a follow-up so this diff stays reviewable —
a base-layer refactor and a large new test suite should not land
together. If you find yourself writing a tenth flow test, stop.

## Part B — truncated-packet guards

Both bugs were found by T04 and are currently pinned as
`BUG_`-prefixed tests in `tests/test_zone_connection_flow.cpp`.

**B1. `ZoneProcessPlayerProfile`** (`src/client/eq.cpp:1999`) already
warns `"PlayerProfile packet too small"` and then **falls through and
reads anyway**. Add the missing `return;` after the warning.

**B2. `ZoneProcessTimeOfDay`** (`src/client/eq_entities.cpp:484`) has
no guard at all; it reads `GetUInt8(2)`…`GetUInt16(6)`, so it needs a
total length of at least 8. Add a length check that logs a warning and
returns, matching B1's shape.

### Verified safe — I checked these against the real capture

I confirmed against `/mnt/projects/fable/captures/session.sleq` that
neither guard will skip a legitimate packet:

| handler | real payload size in capture | guard threshold | fires on real traffic? |
|---------|------------------------------|-----------------|------------------------|
| PlayerProfile (`0x75df`, 2 records) | 19592 | warns below 19588 | **no** |
| TimeOfDay (`0x1580`, 2 records) | 8 | needs ≥ 6 | **no** |

So `return`-ing on the short path cannot regress a working case. If
your own measurement disagrees with this table, **STOP and report** —
do not proceed on the assumption that I am right.

**B3. Flip the pinned tests.** The two `BUG_`-prefixed tests currently
`EXPECT_THROW`. Rewrite them to assert the fixed behavior: the handler
returns without throwing, and observable state is unchanged. Drop the
`BUG_` prefix and name them for the behavior they now guarantee.

## STOP conditions

- The dispatcher extraction cannot be made behavior-preserving → STOP
  and report; do not "improve" the flow while extracting.
- Any real-capture packet would be skipped by a new guard → STOP.
- You find another bug → report it, do not fix it.
- A change would touch a file not listed in Territory → STOP.

## Acceptance criteria

1. Both dispatchers extracted; session-recorder calls verifiably still
   in `*OnPacketRecv` (quote the final code in your Review).
2. All 12 `QueuePacket` call sites guarded. State the count you found;
   if it is not 12, say so and explain.
3. `ReplayLoginPacket`/`ReplayWorldPacket` public in `eq.h`; seam
   smoke-test passes.
4. Part B: both guards added, both pinned tests flipped and passing.
5. **`test_session_replay` passes, including the golden comparison.**
   This is the behavior-preservation oracle for Part A — if the golden
   changes, the refactor was not behavior-preserving. **A golden
   mismatch is a STOP, not a re-bless.** Never re-bless a golden.
6. Full suite: 3 consecutive runs, documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` is the known T03b use-after-free, not a
   flake — name it, do not call the run clean).
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
8. **`bash <slot>/scripts/build-arm-noble.sh` must pass.** This unit
   touches shared source, which is exactly the case the charter's
   verification bar reserves the ARM build for. Previous tests-only
   units skipped it; this one may not.

Visual gate does not apply (no rendering change) — say so explicitly
rather than silently omitting it.

## Environment notes

- GLES2 config is the only tree; the documented relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a`
  is absent: `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.

## Review

Per the protocol, and additionally:
- Quote the final `LoginOnPacketRecv`/`WorldOnPacketRecv` bodies so the
  recorder placement can be checked without reading the whole diff.
- List every guarded call site.
- State explicitly that the replay golden matched.
- Confirm the ARM build result.

This unit changes shipped client behavior on the receive path. If any
part of it is uncertain, say so plainly — I will re-verify the diff
myself either way.
