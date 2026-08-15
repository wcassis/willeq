# T118 — Find the heap corruptor behind the Pi's four crash signatures

**Slot: whichever is free. Start on x86 — do NOT start on the Pi.** eqemu lock
required. Pi only if the x86 legs come back clean.

**Territory: sanitizer/build configuration, `tools/`, `scripts/`,
`refactor/`, `/mnt/projects/fable/review/T118/`. Production `src/` changes
only under "If you find it" below.**

Read T117's Review first (the four signatures and the 2-of-10 rate), then
T115's (what x86 ASAN did and did not cover).

## The target

T117 established that the Pi is **not** stabilizing (2 of 10 clean), and — the
real finding — that **four independent victim containers were corrupted in the
same 10 runs with no shared write site**:

| Victim | When |
|---|---|
| `ZoneMergeBuffers::ranges_` (`__x=0x2601`) | mid-tour, at `/move -700` |
| `SkyData`'s texture map | teardown |
| `RaceModelLoader::vertexMappingForAnimation_` | teardown |
| glibc `_int_malloc` on an innocent `std::string` | **before** the hazard box |
| Mesa/Lima SIGBUS | teardown |

Unrelated containers, plus a crash inside malloc's own internals far from any
of them, is the signature of **one wild or out-of-bounds write**, not four
bugs. Three individually-correct lifetime fixes (T111, T115, T116) did not
move the rate, which is consistent.

**Your target is the write, not any victim.** Do not spend this unit on
`ranges_`.

## The decisive fact this unit starts from

**x86 ASAN has never run in anything close to the Pi's configuration.** T115's
and T116's ASAN runs were clean — 3 of 3 each, *including* teardown via
`/camp` — but `review/T115/run_asan_x86.sh` shows they ran with:

- **no `--constrained orangepi`** — so the entire constrained path
  (`ConstrainedTextureCache` and its LRU eviction, the `entityLoadRadius=400`
  gate, the constrained presets) was never instrumented;
- **no `--renderer gles2`** — a different renderer path than the Pi's;
- **`--no-audio`** — added as a workaround for the TinySoundFont overflow,
  which silently removed the whole audio subsystem from ASAN coverage from run
  2 onward.

So "x86 ASAN is clean" is a much weaker statement than it looked. **It means
clean on a substantially different program.** Closing that gap is cheap,
fast, and the obvious first move.

## Do these in order, and stop as soon as ASAN names the write

### Leg 1 — x86 ASAN in the Pi's actual configuration (cheap, do this first)

Rerun the existing x86 ASAN tour with the Pi's flags:
`--constrained orangepi --renderer gles2 -r 800 600`, **audio ENABLED**, same
15-step tour including `/camp` so teardown is covered.

Expect the TinySoundFont overflow (`tsf.h:1295`, see `deferred.md`) to fire
early. **Do not respond by disabling audio** — that is what hid this path
before. Instead use `ASAN_OPTIONS=halt_on_error=0` (and/or
`-fsanitize-recover=address`) so the run continues past it and reports
everything else, or apply the known one-line `nextPos` clamp locally as an
**uncommitted** test-only patch and say so. Either is fine; hiding the
subsystem is not.

### Leg 2 — 32-bit x86 ASAN (`-m32`)

The Pi is **32-bit ARM**; every x86 run so far has been 64-bit. A size
computation that truncates or overflows at 32 bits — or a struct-layout or
alignment assumption that only holds at 64 — would be invisible on x86-64 and
fatal on the Pi, and that is a strong shape for a wild write.

`-m32` gives you 32-bit `size_t` and pointer arithmetic **on a fast machine
with full ASAN**, which is far cheaper than fighting the Pi. Build i386 ASAN,
run the same tour. If the toolchain or a dependency will not go 32-bit,
report that as a blocker with what specifically failed — do not burn the unit
on it.

### Leg 3 — only if 1 and 2 are clean: make ASAN fit the Pi

T115's Pi ASAN failed 3 of 3 by OOM/timeout on a **292MB binary**. **Do not
repeat that invocation.** The reason it was 292MB is almost certainly **DWARF,
not ASAN instrumentation** — ASAN roughly doubles code size, while `-g` on a
C++ binary this size dominates everything.

So: build ASAN with `-g1` or `-gsplit-dwarf`, **deploy a stripped binary**,
keep the symbols on the NFS host, and **symbolize the report offline**
(`ASAN_OPTIONS=symbolize=0`, then `llvm-symbolizer`/`addr2line` against the
unstripped copy). Also trim runtime cost: `quarantine_size_mb=0`,
`malloc_context_size=5`, `detect_leaks=0`, `redzone` small.

**Report the deployed binary size before and after.** If it does not come down
to something the Pi can hold, say so plainly with the numbers — that is a real
result and it kills a line of attack we keep assuming is available.

### Leg 4 — cheap Pi-native probe, can run alongside

`MALLOC_CHECK_=3` and `MALLOC_PERTURB_=<nonzero>` cost almost nothing, fit the
Pi trivially, and catch double-frees and some overflows **at `free()` time**
with an abort. Weaker than ASAN and it will not name the writing line, but it
can confirm heap corruption and roughly bracket *when* it happens. Worth a few
runs regardless of how the other legs go.

## Axes that x86 can never cover — name them, don't pretend otherwise

If every x86 leg is clean, the corruptor lives in something x86 cannot
exercise. Say which of these remain, so the next unit starts from a real list:

- **Lima/Mali GLES2 driver and DRM/KMS** — x86 uses llvmpipe surfaceless.
  Note the teardown SIGBUS lands *inside* `libgallium`, and SIGBUS is
  characteristically an **alignment** fault — ARM is stricter than x86 here.
- **Native ETC1** compressed textures on Mali.
- **Actual ARM codegen and alignment rules.**
- **Real memory pressure** — 512MB with ~93MB resident, where an overwrite is
  far likelier to land on something live than on x86's slack.

## If you find it

**Fix it only if the fix is genuinely narrow and obviously safe** — the write
is off by one, the buffer is the wrong size, the cast is wrong. Then verify on
the Pi with T117's exact 10-run method and report the new rate against 2-of-10.

**Otherwise the diagnosis IS the deliverable.** A precise, evidenced
"here is the write, here is why, the fix is larger than this unit" is a
success. T115's honest negative result is what made T117 possible.

**Do not paper over**: no clamping a symptom, no guard that hides a faulting
access, no disabling a subsystem to make the crash go away.

## STOP conditions

- The fix would change buffer ownership, the zone merge/rebuild lifecycle, or
  GL teardown order — those are their own units.
- You find **more than one** independent corrupting write. Report both rather
  than half-fixing; that changes the shape of the whole arc.
- A leg needs the Pi for more than verification. Say what and why.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile, **if** you
  changed production code. If you changed none, say so and skip.
- Any Pi rate claim must use **T117's exact method** — same tour, same flags,
  10 runs, stated as N of 10. **Do not compare a 3-run sample to T117's 10-run
  number**; T117's whole lesson is that small samples read as more stable than
  reality.
- Pi rules if you get there: cores and logs to **NFS never `/tmp`**,
  `--log-level=INFO`, no live gdb, teardown by real binary PID (`pgrep -x`),
  `down.sh`, lock deleted and confirmed gone.

## Review

(executor fills: what each leg covered and found; **the ASAN report verbatim**
with allocation/free stacks if one fired; whether the Pi-config x86 run
changed the picture versus T115's weaker configuration; the `-m32` result or
why it was blocked; deployed ASAN binary size before/after for Leg 3; the
corrupting write and its thread if found, or precisely what was ruled out;
which x86-uncoverable axes remain; deviations)

## Board

Say what remains of the Pi arc. If the write is found and fixed, the Pi rate
against 2-of-10 is the headline. If not, the deliverable is a **narrowed** list
of where it can still be hiding — that is worth as much, provided the
elimination is real and evidenced rather than assumed.
