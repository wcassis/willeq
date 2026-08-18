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

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T137-arm-probe`,
branched from commit `2f02f68b` (this plan's own commit, on top of T136's
in-flight branch). No eqemu, no Pi touched. Full evidence in
`/mnt/projects/fable/review/T137/` (README indexes every file referenced
below).

### 1. ARM flag plumbing + `nm` evidence, both configurations

Mirrored `scripts/build-gles2-x86.sh --t122-texprobe`'s pattern exactly:

- `docker/Dockerfile.arm-noble` gets a new `ARG ENABLE_T122_TEXPROBE=OFF`
  (declared right next to `ENABLE_GRAPHICS`/`ENABLE_GLES2`, scoped to the
  `deps` stage where Irrlicht is cross-compiled — probe symbols live
  entirely inside the Irrlicht static lib, `src/` never references
  `WILLEQ_T122_TEXPROBE`, confirmed by grep before writing this). The
  Irrlicht `make staticlib` invocation gets a new `CXXFLAGS="$PROBE_CXXFLAGS"`
  var, computed the same way the x86 script computes it
  (`-g -fno-omit-frame-pointer -DWILLEQ_T122_TEXPROBE` when the arg is
  `ON`, empty otherwise) — same `+=`-into-`NDEBUG=1`'s-`-O3` mechanism T122
  already validated on x86.
- `scripts/build-arm-noble.sh` gets a new `--t122-texprobe` flag, threaded
  through as `--build-arg "ENABLE_T122_TEXPROBE=${ENABLE_T122_TEXPROBE}"`.
- The Dockerfile's own copy-list for `docker/irrlicht-drm/*.h` (the
  footgun the plan named — "T122 already had to fix a missing header
  there") already includes `t122_texprobe.h`'s `COPY` + `cp` lines from
  T122's own fix; checked, unchanged, no further action needed there.

**`nm` evidence** (`review/T137/arm_nm/`, both from the FINAL build after
every fix below — not earlier superseded runs):

| build | `grep -i t122` on full `nm` | BuildID |
|---|---|---|
| plain `bash scripts/build-arm-noble.sh` | 0 matches (grep exits 1) | `5b5c6e8db0c90aafc06df17dc862c2f3118ad1dc` |
| `bash scripts/build-arm-noble.sh --t122-texprobe` | 10 matches: `g_t122Ring`, `t122Record`, `g_t122RingIndex`, `t122RecordTexParamsWithCallChain`, `kRingCapacity`×2, `kCallChainDepth`×2, `kRingMask`×2 | `bc9c96b8e07ae8ef9e014e9ca54dbf9b3b86e85e` |

The plain-build BuildID above is identical to the very first plain ARM
build run in this unit, before any probe build had touched the worktree —
strong evidence the fix (below) produces a genuinely clean rebuild, not a
coincidentally-symbol-free one.

**Two real bugs found and fixed while getting to that clean table** (both
outside the plan's literal "assertions only" territory note for
`t122_texprobe.h`, but both load-bearing for "make the probe usable on
ARM" at all — flagged here as deviations, not absorbed silently):

1. **ARM compile break**: `t122_texprobe.h`'s `e.tid =
   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()))`
   failed to compile on armhf: `error: invalid cast from type 'pthread_t'
   {aka 'long unsigned int'} to type 'uintptr_t' {aka 'unsigned int'}`.
   This reinterpret_cast only ever compiled on x86_64 because `pthread_t`
   and `uintptr_t` are the identical typedef there (`unsigned long`),
   making it a degenerate no-op; on armhf they're different non-pointer
   integral types (`unsigned long` vs `unsigned int`), and reinterpret_cast
   between two different non-pointer integral types is ill-formed per the
   standard. Fixed to `static_cast<uint32_t>(static_cast<uintptr_t>(pthread_self()))`
   — a portable arithmetic narrowing conversion, bit-identical result on
   both architectures, no behavior change (both sites, CREATE/DESTROY's
   `t122Record` and TEXPARAMS's `t122RecordTexParamsWithCallChain`).
2. **Stale persistent build cache**: `build-arm-noble.sh` mounts a
   host-persistent `build-arm-noble/cache/` volume for incremental
   `cmake`/`make` builds across `docker run` invocations. Found by direct
   observation: a probe-ON build followed immediately by a plain rebuild
   reused the probe-ON link output byte-for-byte (`nm` still showed all 10
   t122 symbols in the supposedly-plain binary, same file size). Make's
   generated `link.txt`/`build.make` DO list the external
   `/opt/noble-sysroot/usr/lib/libIrrlicht.a` as a prerequisite of
   `bin/willeq`, but the incremental relink did not reliably fire across
   the flag toggle in this Docker+persistent-volume setup (root cause not
   fully pinned down — plausibly layer/volume timestamp interaction — but
   the failure mode itself was reproduced twice, deliberately, and is
   unambiguous). Since "off by default, zero probe symbols in a normal
   build" is a load-bearing T122 contract, silently serving a stale
   probe-tainted link would be a real regression. Fixed with a marker file
   (`$BUILD_CACHE/.t122_texprobe_state`): if the flag's value differs from
   the last recorded run, the whole cache is wiped before building,
   forcing one full rebuild in that case only. **Verified the fix actually
   works, not just added**: toggled OFF→ON→OFF three times after the fix,
   confirmed each toggle logs "T122 texprobe flag changed ... wiping stale
   incremental build cache" and triggers a genuine full recompile (~80s,
   not the ~15s a cache hit takes), and confirmed the resulting `nm` output
   is correct in every case (table above is the last OFF/ON pair from that
   sequence).

### 2. The 32-bit layout — derived, then verified two ways

`Entry` (`docker/irrlicht-drm/t122_texprobe.h`): `const void* ptr; const
void* retaddr[8]; uint32_t seq, tid, value; uint8_t kind, pad[3];`.

Derivation on a 32-bit target (4-byte pointers, natural 4-byte alignment
throughout, no padding needed before `pad[3]` since the four `uint32_t`-ish
fields already align on 4): `ptr` (4) + `retaddr[8]` (8×4=32) + `seq/tid/value`
(3×4=12) + `kind` (1) + `pad[3]` (3, to round the struct back up to a
multiple of its 4-byte alignment) = **4 + 32 + 12 + 4 = 52 bytes**,
`"<I8IIIIB3x"` in Python struct notation.

Verified two independent ways, neither of which is "trust the arithmetic":

1. `python3 -c "import struct; print(struct.calcsize('<I8IIIIB3x'))"` →
   `52`, matching by-hand computation exactly (also re-derived the 64-bit
   figure the same way: `struct.calcsize('<Q8QIIIB3x')` → `88`, matching
   the existing, previously-shipped assert).
2. A `static_assert(sizeof(Entry) == (sizeof(void*) == 8 ? 88u : 52u), ...)`
   added directly in `t122_texprobe.h` (source-of-truth, width-generic —
   keyed off `sizeof(void*)` so the same line is correct whichever
   architecture compiles it, no `#ifdef` needed) — this is a REAL compiler
   check, not a script-side assumption, and it passed on both the x86_64
   probe build and the ARM `--t122-texprobe` cross-compile (both built
   successfully in this unit; a size mismatch would have failed the build,
   not just a Python script). Also fixed a stale comment above `Entry`
   that said "68 on armhf" — leftover from before T126 extended `retaddr`
   to 8 entries; nothing read that number, but it would have misled the
   next person to touch this file, so corrected to 52 alongside the
   static_assert that now makes drift like this loud instead of silent.

### 3. ELF-width detection mechanism

`scripts/t122_extract_ring.py`'s new `detect_pointer_width(binary_path)`
reads the target binary's own 16-byte ELF `e_ident` header directly (no
gdb, no `readelf` subprocess) and checks the ELF magic (`\x7fELF`) and the
`EI_CLASS` byte at offset 4 (`1`=`ELFCLASS32`, `2`=`ELFCLASS64`) — a fact
the file already states, per the plan's design point, so there is no flag
for a caller to get wrong. Raises `ElfWidthError` (not a silent
best-guess) on a bad magic number or an unrecognized class byte.
`entry_format_for_width(width)` maps the detected width to the matching
`(struct_format, size)` pair, also raising `ElfWidthError` for anything
else. `main()` calls `detect_pointer_width(args.binary)` before any gdb
work and prints the detected width, so a run's own output states which
layout it used.

### 4. Synthesised-ring test results, both widths

`scripts/tests/test_t122_extract_ring.py` (new, standalone — see "Test
wiring" below for why it's not a ctest target): 29/29 checks pass
(`review/T137/synthesized_ring_test_output.txt`, full output). Covers:

- `detect_pointer_width()` against hand-crafted ELF headers for both
  classes, a bad-magic file, and an unrecognized-class-byte file (all four
  correctly accepted/rejected).
- `detect_pointer_width()` against the REAL binaries this checkout had on
  hand: `build-gles2/bin/willeq` (x86_64) correctly detected as 64,
  `build-arm-noble/bin/willeq` (armhf) correctly detected as 32 — not just
  synthesised headers.
- `entry_format_for_width()` returns the right `(format, size)` for both
  widths, raises for anything else.
- The core claim: `parse_ring_bytes()` round-tripped against a hand-packed
  synthetic ring, separately for 64-bit (`ENTRY_FMT_64`/88 bytes) and
  32-bit (`ENTRY_FMT_32`/52 bytes). Each test places three known entries
  (CREATE, TEXPARAMS with a full non-zero 8-deep retaddr chain including
  the `0x2601` `GL_LINEAR` value T118/T122 identified, DESTROY) at
  out-of-order ring slots among otherwise-all-zero slots, and asserts: the
  5 never-written slots are correctly skipped, the 3 real entries come
  back in `seq` order (not slot order — proving the sort, not just the
  parse), and every field (ptr, tid, value, all 8 retaddr entries) round-
  trips exactly. This is both the "does 32-bit actually parse" proof and a
  regression guard that pulling the parse loop out of `main()` into a
  reusable `parse_ring_bytes()` function (needed for this test to exist at
  all) did not change 64-bit behavior.

### 5. The 64-bit-path-against-a-real-checked-in-capture leg — attempted, fell short, documented why

The plan asked to confirm the 64-bit path still parses a real checked-in
capture from `review/T122/` or `review/T126/`. **This could not be fully
completed**, and the reason is itself worth recording: the exact probe
binary that produced those checked-in cores (`review/T122/probe_gate/cores/`)
was never checked in — only the core dumps and their already-extracted
TSVs were preserved, and the worktree that built it (`wt/slot2`, per the
core's own recorded exec path) has long since been rebuilt for other work
and no longer contains it.

What WAS verified (`review/T137/x86_real_capture_reverify/`, detailed in
its README section): rebuilt the probe binary fresh in this worktree
(`bash scripts/build-gles2-x86.sh --t122-texprobe`, confirmed via `nm` to
carry the same probe symbol set T122's Review documented), then ran the
refactored `t122_extract_ring.py` against it and `review/T122/probe_gate/cores/run1_core.willeq`.
It correctly detected 64-bit from the ELF header, then failed cleanly at
the gdb memory-dump step ("Cannot access memory") rather than producing a
plausible-but-wrong parse. Diagnosed why: reconstructed the crashed
process's actual PIE load bias from the core's own `AT_PHDR` auxv entry
(`0x60c50cc06040`) minus this checkout's `e_phoff` (64) = `0x60c50cc06000`
— which exactly matches what `info proc mappings` independently reports as
the mapped base for that core's `willeq` — confirming the bias-reconstruction
mechanism itself is sound. But the crashed binary's mapped image only
spans ~22MB, while this checkout's freshly-rebuilt binary places
`g_t122RingIndex` roughly 45MB in: the source tree has grown enough since
that session that the two binaries' static layouts don't correspond. This
is a binary/core content mismatch (an evidence-preservation gap from
T122's session, not a bug introduced or left by this unit), not a defect
in the extraction logic — and the extractor's honest failure on a
genuinely mismatched pair is the correct behavior, not a bug to paper
over.

Given that, the 64-bit path's correctness rests on: (a) the byte-identical
`ENTRY_FMT_64`/`ENTRY_SIZE_64` constants (unchanged from T122/T126's
shipped, previously-validated values), (b) the synthesised-ring regression
test above proving the refactored `parse_ring_bytes()` behaves identically
to the pre-T137 inline logic, and (c) the gdb/ELF mechanics being
demonstrated working (symbol resolution attempted correctly, bias math
verified correct) against the real core — just not the full round trip to
recovered rows, for the reason above.

### What remains untested, and why

- **End-to-end Pi capture** (probe-build binary staged to real hardware,
  crashed under a real zone-crossing tour, core pulled and extracted) —
  explicitly out of scope per the plan (needs eqemu, which T136 holds, and
  the Pi, which this unit was told not to touch). This is the next Pi
  unit's first step, per the plan's Board instruction.
- **The exact T122/T126 checked-in cores, round-tripped to recovered
  rows** — see section 5 above. Not a scope exclusion, an evidence gap:
  redoing it would need either a preserved matching binary (not checked
  in) or a fresh eqemu-driven crash (out of scope here).
- **A real ARM core, extracted** — no Pi run happened, so there is no ARM
  core to test the 32-bit extraction path against real (as opposed to
  synthesised) data. The 32-bit `Entry` layout and the parsing logic are
  both verified (compiler-checked `static_assert` + synthesised-ring
  round-trip), but nothing here proves gdb can successfully cross-resolve
  an armhf core's symbols the way it does for x86_64 — that combination is
  untested. Flagging this explicitly since a genuinely different failure
  mode (e.g. needing `gdb-multiarch` or a target-aware gdb build) could
  exist there and hasn't been exercised.

### Verification bar

- `cmake --build build -j24` (main GLES2 tests-on tree): succeeded (first
  attempt hit the same host-load `gtest_discover_tests` timeout flake
  T125/T126 already documented on an unrelated test, `test_zone_connection_flow`;
  re-ran with no source changes, succeeded — matches T125's own precedent
  exactly).
- `ctest --test-dir build --output-on-failure`: **2351/2352**, sole
  failure the documented `AudioManagerTest.PreloadCommonSounds` ALSA host
  quirk — matches the stated baseline exactly.
- `bash scripts/build-gles2-x86.sh` (plain, no flag): succeeded, `nm |
  grep -i t122` on the resulting binary: 0 matches — confirms the default
  x86 build is unaffected by any of this unit's changes.
- ARM cross-compile, both configurations: see section 1 above.
- `bash scripts/refactor/check_file_sizes.sh fable` from this slot/branch:
  12 files reported changed vs. `fable`, but only ONE (`docker/irrlicht-drm/t122_texprobe.h`,
  264 lines, `ok`) is actually this unit's change — the other 11
  (`eq.h`, `action_dispatcher.*`, `command_processor.*`, etc., several
  `WARN`, zero `FAIL`) are T136's in-flight Merchant-migration commits,
  inherited because this branch was rooted on T136's branch tip (where the
  T137 plan commit `2f02f68b` happens to live) — not files this unit
  touched. This unit's own touched files outside `.h`/`.cpp`
  (`scripts/build-arm-noble.sh`, `docker/Dockerfile.arm-noble`,
  `scripts/t122_extract_ring.py`, the new
  `scripts/tests/test_t122_extract_ring.py`) are outside this script's
  scope by design (it checks C++ translation-unit sizes). Zero new FAILs
  either way.

### Test wiring: standalone script, not ctest

`scripts/tests/test_t122_extract_ring.py` is invoked directly
(`python3 scripts/tests/test_t122_extract_ring.py`), not wired into the
CMake-driven ctest suite. The suite builds and runs C++ binaries; adding a
Python test runner to it for one offline diagnostic script would be
disproportionate to what's being tested, and mirrors
`t122_extract_ring.py`'s own existing precedent as a standalone,
Review-invocable script rather than a ctest target.

### Deviations from the plan

1. Two small fixes to `t122_texprobe.h` beyond "assertions only": the
   `pthread_self()` cast portability fix and the stale "68 on armhf"
   comment correction (section 1 and 2 above). Both are required for the
   ARM build to compile/behave correctly at all — not optional polish —
   and neither changes the probe's runtime recording behavior (same
   values recorded, same lock-free/allocation-free/syscall-free
   properties, still off by default). Flagged here per the plan's own
   "deviations flagged, never silently absorbed" instruction rather than
   read as silently exceeding the stated territory.
2. A second, unplanned bug fix in `build-arm-noble.sh` (the persistent
   build-cache staleness across a flag toggle) — found only because this
   unit tested BOTH configurations back-to-back, which the plan's
   verification bar required anyway. Without this fix, the "off by
   default" claim the plan explicitly cares about would have been false
   under exactly the conditions this unit's own testing exercised.
3. The "confirm the 64-bit path still parses a real checked-in capture"
   verification leg was attempted but not completed, for reasons outside
   this unit's control (the matching binary was never preserved). Reported
   plainly per the plan's own honesty-boundary instruction rather than
   claimed as done or silently dropped — see section 5.

## Board

State plainly that ARM ring extraction is **infrastructure-ready but not
end-to-end verified**, and that the first Pi unit to take the lock should run
the capture as its first step. That closes T125's open leg properly rather than
leaving a half-claim on the board.

**T137 closed. ARM ring extraction is infrastructure-ready, not end-to-end
verified.** `scripts/build-arm-noble.sh --t122-texprobe` now produces an
ARM binary carrying the T122 texture-lifecycle probe (confirmed via `nm`,
both with the flag and — after fixing a real build-cache staleness bug —
confirmed clean without it), and `scripts/t122_extract_ring.py` now parses
either a 32-bit or 64-bit ring, auto-detected from the target binary's ELF
header, with the 32-bit layout independently derived and verified
(`struct.calcsize` + a compiled `static_assert` on both real architectures)
and both widths' parsing logic regression-tested against a synthesised
ring. **The first Pi unit to take the eqemu lock should run the probe
capture as its literal first step**: stage a
`build-arm-noble.sh --t122-texprobe` binary, reproduce T125's crash
condition (or any zone-crossing tour), pull the resulting core, and run
`t122_extract_ring.py` against it — that is the one leg this unit could not
touch (no eqemu, no Pi, by design) and the one thing that would make this
infrastructure's "usable on ARM" claim end-to-end rather than
build-and-parse-only. Two bugs found and fixed along the way (an ARM-only
compile break in `t122_texprobe.h`'s thread-id cast, and a stale
Docker build-cache that could silently serve a probe-tainted "plain"
binary) are both documented in the Review with before/after evidence —
worth a glance since the second one specifically undermines the "off by
default" guarantee this whole probe design depends on if left unfixed.
