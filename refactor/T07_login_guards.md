# T07 — LoginDisableReconnect + LoginProcessLoginResponse guards

**Executed inline by the orchestrator** (slot t1, branch
`task/T07-login-guards`), not dispatched. The change is ~20 lines and
the verification tail (3× full suite + ARM build) is the bulk of the
work, which the orchestrator performs during review regardless — a
dispatch round-trip would have added coordination cost for no quality
gain. Recorded here so the unit log stays complete.

Authorized by Will 2026-07-28 (items 1 and 2 from the T06 findings).

## Scope

Base-layer source, `src/client/eq_login_world.cpp` only, plus the
`tests/` updates required to flip T06's pinned expectations.

## What changed

### 1. `LoginDisableReconnect` (T06 finding 1 — the segfault)

Both `m_login_connection_manager->OnConnectionStateChange(...)` and
`m_login_connection->Close()` are now null-guarded, matching the
pattern used by every other connection use in the file.

Not live-reachable (both members are always set in production before a
packet can arrive), but reachable through the replay seam from
`HC_OP_PlayEverquestResponse`, where T06 pinned it as a SIGSEGV.

This closes the hole left by T05, whose guard sweep was scoped by
"`QueuePacket` call sites" and therefore missed a site that dereferences
via `->Close()`.

### 2. `LoginProcessLoginResponse` (T06 finding 2 — the underflow)

Two guards:

- `p.Length() < 12` → warn and return. Previously this underflowed
  `p.Length() - 12` to ~1.8e19 and fed it to `new char[]`
  (`std::bad_alloc`).
- `encrypt_size < 13` after the round-down → warn and return. The
  decrypted buffer is read at offsets 1 (`uint16`), 8 (`uint32`) and 12
  (cstring), so it needs 13 bytes; previously a 12-byte packet produced
  a zero-length buffer and threw `std::out_of_range` out of the network
  callback.

Cannot reject legitimate traffic: the real `LoginAccepted` record in
`session.sleq` is a 90-byte payload decrypting to 80 bytes.

Deliberately **not** fixed: `Packet::GetCString` bounds-checks only
`offset + 1` and then reads to the first NUL, so it can run past the
buffer end. That is a `common/net` issue affecting many callers, not
this handler — filed in the tracker, not touched here.

### 3. Test updates (`tests/test_login_world_flow.cpp`)

- The two `BUG_*_UnderflowsAndThrowsBadAlloc` tests now assert
  `EXPECT_NO_THROW`, renamed to `*_ReturnsSafely`.
- `*_ExactlyTwelveBytePacket_ThrowsOutOfRangeInstead` replaced by
  `*_ExactlyHeaderLength_ReturnsSafely`.
- The `EXPECT_DEATH` test is replaced by its inverse: the same packet
  that killed the process must now be handled cleanly. **No death test
  remains in the suite.**
- The `HC_OP_PlayEverquestResponse` exclusion in the capture-replay loop
  is removed, so the entire recorded login stream now replays.
  `loginRecords` expectation 3 → 4.

## A correction the orchestrator owes the record

T06's executor reported the fixture holds **4** login S→C records and
no `0x0000` records. When reviewing T06 I contradicted that, asserting
10 records including six zero-length `0x0000`, and wrote the
contradiction into the T06 merge commit and the tracker.

**The executor was right and I was wrong.** `SessionLogFileHeader` is
**112 bytes** (`static_assert`, `session_log.h:53`), not the 16 my
ad-hoc parser assumed. Starting at offset 16 misreads the header's
zeroed `character[64]`/`zone[32]` tail as 96/16 = **exactly six**
phantom `0x0000` size-0 records. Re-parsed correctly: 2943 records,
4 login S→C records (`0x16`, `0x17`, `0x18`, `0x21`), no `0x0000`.

Both parses ended exactly at EOF, which is why my sanity check gave
false confidence — the phantom records consumed precisely the 96 bytes
I had skipped.

Two lessons, both mine: the T06 plan told the executor to use the
`session_log.h` structs rather than hand-roll a parser, and I then
overrode their correct result using exactly the hand-rolled parser I
had warned against; and "the parse ends at EOF" is a weak check that
can survive a constant offset error.

## Verification

| step | result |
|------|--------|
| Build | clean, 0 errors |
| `test_login_world_flow` (real capture) | 18/18 pass |
| Session-replay golden | matched; `session.golden` md5 unchanged (`4d55b40a…`) |
| Full suite ×3 | 1400 tests, only `AudioManagerTest.PreloadCommonSounds` (documented host ALSA) each run |
| ARM build | passes, produces ELF 32-bit ARM `willeq` |
| Live-path behavior | unchanged: both guards only add branches taken when a connection is null, which cannot happen in production |

Visual gate: not applicable (no rendering change).
