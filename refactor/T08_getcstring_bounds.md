# T08 — Bound `Packet::GetCString`'s terminator scan

**Executed inline by the orchestrator** (slot t1, branch
`task/T08-getcstring-bounds`). Authorized by Will 2026-07-28.

Base-layer source in `src/common/net/`, which every network caller
inherits — 35 `GetCString` call sites across `eq.cpp`,
`eq_login_world.cpp`, `eq_merchant.cpp` and `ucs.cpp`.

## The bug

`src/common/net/packet.cpp:270` bounds-checked only `offset + 1`, then
built a `std::string` from the raw pointer:

```cpp
char *str = ((char*)Data() + offset);
return std::string(str);           // reads to the first NUL, wherever it is
```

For a string field that is not NUL-terminated *inside the packet*, that
reads past the end of the buffer. Unlike the `GetUInt*` accessors —
which bounds-check and throw — this is a genuine out-of-bounds **read**,
reachable from the network receive path.

### Proof, not inference

A standalone repro compiled with `-fsanitize=address`, over an
exactly-sized 4-byte heap buffer containing `"abcd"` and no terminator:

```
==ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 5 at 0x502000000014
    #0 strlen
    #1 std::char_traits<char>::length(char const*)
    #2 std::basic_string<...>::basic_string(char const*, ...)
    #3 OldGetCString
```

The same repro using the new logic exits 0 and returns `"abcd"`.

An exactly-sized heap buffer matters: with a `DynamicPacket`, heap slack
after the buffer would usually contain a zero byte and mask the
over-read, which is why this survived so long and why the new tests use
`StaticPacket` over exact-size buffers.

## The fix

Bound the terminator scan to the packet, treating the end of the packet
as a terminator when none is found:

```cpp
const char* base = static_cast<const char*>(Data()) + offset;
const size_t available = Length() - offset;
const void* terminator = memchr(base, '\0', available);
const size_t length = terminator
    ? static_cast<size_t>(static_cast<const char*>(terminator) - base)
    : available;
return std::string(base, length);
```

**Why end-of-packet rather than throwing.** This is reached from the
network receive path, where an uncaught exception kills the client —
the exact failure mode T04/T05/T07 have been closing. Degrading to a
bounded string keeps malformed input safe *and* non-fatal. The
`offset + 1` bounds check is unchanged, so a genuinely out-of-range
offset still throws.

Behavior is **identical** for any string terminated inside the packet,
which is every well-formed case. Confirmed empirically: the
session-replay golden matched with `session.golden` md5 unchanged, so
the real recorded traffic parses bit-identically.

The chained-offset idiom in `ucs.cpp`
(`GetCString(2 + from.length() + 1)`) stays safe: an unterminated field
now returns to end-of-packet, so the next offset exceeds `Length()` and
the existing bounds check throws rather than reading further.

## Tests

Seven cases added to `tests/test_packet.cpp`, all using `StaticPacket`
over exactly-sized buffers so the packet end is a hard boundary:
unterminated at offset 0 and at an offset, embedded terminator (trailing
bytes must not leak), terminator at the final byte, empty string,
`offset == Length()-1`, and out-of-range offsets still throwing.

## Verification

| step | result |
|------|--------|
| ASan repro, old logic | heap-buffer-overflow READ of size 5 — bug confirmed real |
| ASan repro, new logic | clean, exit 0 |
| `test_packet` | 24/24 pass |
| Session-replay golden | matched; `session.golden` md5 unchanged (`4d55b40a…`) |
| Full suite ×3 | 1407 tests; `AudioManagerTest.PreloadCommonSounds` all 3 (documented host ALSA); `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULT in run 1 only — the known T03b use-after-free, not this diff |
| ARM build | passes |

Visual gate: not applicable (no rendering change).

Scratch ASan directory removed.
