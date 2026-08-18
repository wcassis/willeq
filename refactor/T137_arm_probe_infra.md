# T137 — Make the T122 texture probe usable on ARM

**Slot: whichever is free. x86 host only. NO eqemu lock. NO Pi.**

**Territory: `docker/Dockerfile.arm-noble`, `scripts/build-arm-noble.sh`,
`scripts/t122_extract_ring.py`, `docker/irrlicht-drm/t122_texprobe.h`
(assertions only), `tests/`, `refactor/`,
`/mnt/projects/fable/review/T137/`.**

**Stay out of T136's territory** — `src/client/eq_commands.cpp`,
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.*`, `include/client/eq.h`. You should need none
of them.

Read T122's Review (the probe's design and constraints) and T125's (the leg
this unblocks).

## Why

T125 could not take a ring-count measurement on the Pi — the sharper
instrument for every Pi question this project has — because of two
infrastructure gaps, documented then and confirmed still present:

1. **The ARM build never threads the probe flag through.**
   `docker/Dockerfile.arm-noble` and `scripts/build-arm-noble.sh` contain
   **zero** references to `WILLEQ_T122_TEXPROBE`. The x86 path has it
   (`scripts/build-gles2-x86.sh --t122-texprobe`); ARM has nothing.
2. **The extractor is 64-bit only.** `scripts/t122_extract_ring.py` hardcodes
   `ENTRY_FMT_64 = "<Q8QIIIB3x"` with `assert ENTRY_SIZE_64 == 88`, and uses it
   unconditionally at `:111`, `:117-118`. An ARM ring cannot be parsed at all.

Neither needs the server or the hardware, which is why this runs while T136
holds the eqemu lock.

## What to build

### 1. Thread the probe flag through the ARM build

Mirror what `scripts/build-gles2-x86.sh --t122-texprobe` does. Note
`docker/Dockerfile.arm-noble` maintains its **own** copy-list for
`docker/irrlicht-drm/*.h`, separate from the x86 script — T122 already had to
fix a missing header there, so check both halves.

The flag must stay **off by default**: a normal ARM build must contain no probe
symbols, exactly as the x86 default build does.

### 2. Teach the extractor 32-bit

`Entry` is `const void* ptr; const void* retaddr[8]; uint32_t seq, tid, value;
uint8_t kind; uint8_t pad[3];`

- 64-bit: 8 + 64 + 12 + 4 = **88** (matches the existing assert).
- 32-bit: 4 + 32 + 12 + 4 = **52**, i.e. `"<I8IIIIB3x"` — **derive and verify
  this, do not take my arithmetic on faith.**

**Detect the width from the ELF rather than adding a flag.** The binary and
core already say which they are (ELF class byte, or `readelf -h`). A flag the
caller can get wrong is a worse interface than a fact already present in the
input — and a silently mis-parsed ring produces plausible garbage, which is the
worst possible failure mode for a diagnostic.

**Add a size assertion for both widths** so a future `Entry` change breaks
loudly instead of silently shifting every field. The header is the source of
truth; consider asserting there too.

## Verification — and be honest about the boundary

Fully verifiable here, no server or hardware:

- **ARM cross-compile with the flag on**, and **without** it — confirm probe
  symbols present in one and **absent** in the other (`nm`, as T122 did).
- **Extractor against a synthesised 32-bit ring**: craft a buffer with known
  values, parse it, assert the fields come back exactly. Do the same for 64-bit
  as a regression guard — the existing path must not break.
- Confirm the 64-bit path still parses a **real** ring: `review/T122/` and
  `review/T126/` have checked-in captures. A refactor that quietly breaks the
  working path would be worse than the gap it fixes.

**Not verifiable here, and say so plainly in the Review**: an end-to-end
capture on the Pi needs eqemu, which T136 holds. **Do not claim ARM ring
extraction works end-to-end** — claim exactly what you tested: the build
produces an instrumented binary, and the extractor parses a 32-bit ring of the
expected layout. The end-to-end leg is the next Pi unit's first step.

## Do not

- **Do not run the Pi or take the eqemu lock.** If you think you need either,
  STOP and say why — the whole point of this unit is that it doesn't.
- **Do not change the probe's runtime behaviour.** T122's constraints
  (lock-free, allocation-free, no syscalls on the record path, off by default)
  are load-bearing and were validated by an acceptance gate. Assertions and
  build plumbing only.
- **No papering over**: no width guess that "looks right", no parse that
  succeeds on garbage.

## Verification bar

- x86 suite (current total **2352**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) — you are not touching `src/`, so
  this is a cleanliness check.
- ARM cross-compile, both with and without the flag.
- If you add Python tests, say how they run (the suite is ctest; a standalone
  script invoked from the Review is acceptable if wiring it in is
  disproportionate — say which you chose).

## Review

(executor fills: the ARM flag plumbing and the `nm` evidence for both
configurations; the 32-bit layout as **derived**, with how you verified it; the
ELF-width detection mechanism; the synthesised-ring test results for both
widths; confirmation the 64-bit path still parses a checked-in real capture;
**what remains untested and why**; deviations)

## Board

State plainly that ARM ring extraction is **infrastructure-ready but not
end-to-end verified**, and that the first Pi unit to take the lock should run
the capture as its first step. That closes T125's open leg properly rather than
leaving a half-claim on the board.
