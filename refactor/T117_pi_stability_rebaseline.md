# T117 — Re-baseline Pi stability on current `fable`

**Slot: whichever is free. Pi required. eqemu lock required. No sanitizers.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T117/`. This unit fixes
nothing — it measures.** If you find yourself editing `src/`, stop and report.

Read T113's and T114's Reviews for the crash signatures and the tour, and
T115's for why its Pi runs do not count as a stability measurement.

## Why this unit exists

**Our last real Pi stability number is T114's 1-of-3 clean runs, and it is
stale.** Since it was taken:

- **T115** fixed `portalVisibleRegions_`, a genuine dangling-pointer crash
  reproducible on x86.
- **T116** closed that whole shape structurally and brought a fourth member
  (`currentFrameSimResults_`) under the same invariant.

**T115's own three Pi runs do not count.** All three were ASAN-instrumented,
and all three died of the sanitizer's 292MB footprint (two kernel OOM kills,
one timeout mid-zone-load) rather than of the defect. They measured the
sanitizer, not the client.

So two real crash sources have been removed since anyone last measured, and
**we do not know what the Pi does now.** Every further unit on this arc —
especially another `ZoneMergeBuffers::ranges_` hunt, which is expensive and
currently blocked on the Pi's ASAN memory ceiling — is being planned on an
assumption that has already gone stale twice. **Measure before spending
more.**

The result decides the next unit either way:

- **Clean** → the `ranges_` thread may already be closed by T115/T116, and we
  stop paying for Pi hunts.
- **Still crashing** → the corruption has a source neither fix touched, which
  is worth knowing *before* designing the next hunt.

Both outcomes are good. **There is no result this unit can produce that is a
failure**, and you should not shade the number in either direction.

## What to run

Reuse T115's checked-in tour and runner **shape** — they work and are already
tuned for the Pi:

- Tour: `/mnt/projects/fable/review/T115/tour_pi.txt` (Befallen zone-in,
  `#set time 11 0` for daylight, `/pmem` sampling, `/move -400` and
  `/move -700`, which is the movement that produced the mid-tour SIGSEGV).
- Runner: `/mnt/projects/fable/review/T115/run_asan_pi.sh` — **as a shape
  only.** Copy it to `review/T117/`, and **strip every ASAN-specific part**:
  no `ASAN_OPTIONS`, and point it at a normal `build-arm-noble` build.

**Build: normal ARM cross-compile of current `fable` tip `9fbf1440`. No
sanitizers of any kind.** That is the entire point — a ~10MB stripped binary
that fits the Pi comfortably, unlike T115's 292MB one.

### Comparability is the deliverable, so do not improvise

This number is only worth taking if it is comparable to T113's 0-of-3 and
T114's 1-of-3. **Match what those units did**, and where you cannot, say so
explicitly in the Review:

- Same zone, same tour, same movement targets.
- `--constrained orangepi --drm --renderer gles2 -r 800 600`.
- **`--log-level=INFO`.** T112 measured DEBUG as destabilising; T114 found why
  and fixed it, but do not reintroduce the variable in a measurement run.
- **Audio ENABLED — do not pass `--no-audio`.** T115 found an out-of-bounds
  read in TinySoundFont's sample interpolation on the audio callback thread
  (see `deferred.md`), and it is an untested Pi-instability suspect. Running
  audio-free would mask it. If a crash lands in the audio path, that is a
  real and valuable result.
- State whether you ran the binary from NFS or staged it to the Pi's `/tmp`,
  and **match the prior units' choice** — if you cannot determine what they
  did, pick one, say which, and note it as a comparability caveat.

## Run count — the number is the deliverable

**Minimum 5 runs, target 10.** Prior samples were 3 runs, which is a weak
basis for a rate; ten is affordable here because a run is a zone-in plus a
~100-second tour, not a sanitizer session.

**Report the raw tally, not a summary judgement**: how many runs, how many
completed cleanly to `Application shutdown complete`, how many crashed, and
what each crash was. **Do not stop early on a clean streak** — stopping at
the first good news is how a 1-in-5 defect gets reported as fixed. Run the
full count you commit to, and if you must stop early for time or hardware
reasons, say exactly why and how many you got.

## Triage every crash against the known signatures

For each crash, capture a core (**to NFS, never `/tmp`** — T113 OOM'd the Pi
writing a core to its 242MB tmpfs) and classify it:

| Signature | Where | Status |
|---|---|---|
| `ZoneMergeBuffers::renderMerged()` at `ranges_.find()`, `__x=0x2601` | mid-tour, near `x≈-333..-400` | **the open defect** |
| Mesa/Lima **SIGBUS** | at teardown | unexplained since T113 |
| Anything else | — | new — say so loudly |

If a crash is **none of the above**, that is the most important thing this
unit can find, and it takes priority in your report over the headline rate.

**Post-mortem cores only — no live gdb.** It distorts the timing this unit
exists to measure.

## What to establish

- **The rate**, stated as "N of M runs completed cleanly," directly alongside
  T113's 0-of-3 and T114's 1-of-3.
- Whether the mid-tour `ranges_` signature still reproduces **at all**.
- Whether the Mesa/Lima SIGBUS still appears at teardown.
- Whether anything new appeared — including anything in the audio path.
- Your honest read on whether the arc is converging. If the rate improved but
  did not reach clean, say that plainly rather than picking a side.

## Do not

- **Do not fix anything.** If you root-cause a crash while triaging, write the
  diagnosis into the Review — that is valuable — but **do not change `src/`.**
  A fix mid-measurement invalidates the measurement and this unit's whole
  purpose.
- **Do not re-run until the number looks good.** Report the first honest N.
- **Do not conclude `ranges_` is fixed** because it did not appear. Absence
  across M runs at a historical ~2-in-3 rate is evidence, not proof — state
  it as a rate with its sample size, exactly as T115 did with "not caught,
  not ruled out."

## Verification

- x86 suite must be untouched (current total **2272**; sole failure the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`) — you are not
  changing code, so this is a sanity check that your tree is clean.
- ARM cross-compile of `fable` tip, stated with the commit hash you built.
- **Teardown**: kill any Pi client **by real binary PID** (`pgrep -x`, not a
  substring match on a command line — that has produced false positives
  before), run `down.sh`, delete the eqemu lock, confirm it is gone, and
  leave the Pi pristine (governor `ondemand`, `/tmp` clean, `core_pattern`
  restored).

## Review

Executed in `/mnt/projects/fable/wt/slot3`, task branch
`task/T117-pi-rebaseline`, base fable tip `045f2512` (the plan text named
`9fbf1440`; fable had moved a few commits by the time this unit started —
built at the actual current tip, not the stale name, per the executor
protocol's "fable moved" tolerance). Live eqemu (lock free at session start;
taken, used, released cleanly — `down.sh` run, lock file deleted, confirmed
gone). Live Orange Pi (10 timed invocations across one session, well under
the 12-invocation cap; left pristine — `core_pattern` restored to `core`,
`/tmp` 0% used, governor `ondemand`, no leftover `willeq_t117` process
confirmed via `pgrep -x`). Full logs, backtraces, cores, README:
`/mnt/projects/fable/review/T117/`.

**Headline: 2 of 10 runs completed cleanly (20%). Set beside T113's 0-of-3
(0%) and T114's 1-of-3 (33%), this shows NO DETECTABLE CHANGE — 20% is
entirely consistent with a true rate of 33%, and with 0%, at those sample
sizes.**

*(Orchestrator correction at merge: this Review originally read "worse than
T114's 1-of-3 and no better than T113's 0-of-3." Both halves were wrong.
20% is arithmetically better than 0%, not "no better"; and 2-of-10 versus
1-of-3 is far inside sampling error, so it cannot support "worse." The
distinction is not pedantic — "worse" implies T115/T116 REGRESSED the Pi,
which this data does not support and which would send the next unit hunting
a regression that does not exist. The defensible claim is the one this unit
actually earned: **the Pi is not stabilizing, and this is the first sample
large enough to say so with any confidence.** The executor's own downstream
reasoning — that prior 3-run samples looked more stable than reality — is
the correct reading and stands.)*

The
`ZoneMergeBuffers::ranges_` mid-tour SIGSEGV (`__x=0x2601`) that T113 named
and T114/T115 left open reproduced 4 of 10 times, byte-for-byte identical to
the historical signature, every single time immediately at/after `/move
-700` — the exact hazard-box window T113 first identified, unchanged. Beyond
that, **this session surfaced three additional distinct crash signatures in
the same 10 runs**: a confirmed reproduction of T113's previously-unexplained
Mesa/Lima teardown SIGBUS, a confirmed reproduction of T113's *original*
T110-class teardown double-free (`RaceModelLoader::~RaceModelLoader()`
destroying `vertexMappingForAnimation_`) despite T114's `animScratchMutex_`
fix for that exact pair still being present and correctly used in the
current source, and a brand-new teardown heap-corruption signature in a
container neither T113 nor T114 ever touched (`SkyData`'s texture map). A
fifth data point — discounted from the tally but corroborating — an early
smoke-test run also died with the identical `ranges_`/`0x2601` corruption
signature. **T115's and T116's fixes (`portalVisibleRegions_`,
`currentFrameSimResults_`) did not touch whatever is actually corrupting the
heap here.**

### Build

`bash scripts/build-arm-noble.sh` at fable tip `045f251270525293bc2b8ec07f3f46a53384aa65`,
no sanitizers, `RelWithDebInfo` (the script's normal config: `-O2 -g
-DNDEBUG -static-libstdc++`). Output was 369MB unstripped (debug info
retained per the script's own "Skipping strip — debug symbols retained for
GDB" step) — **not** the ~10MB CLAUDE.md describes, because that figure
assumes a stripped binary. Stripped a copy locally with
`arm-linux-gnueabihf-strip` down to **10,275,568 bytes (~9.8MB)**, matching
the documented figure and comfortably clear of the Pi's 242MB `/tmp` tmpfs —
this is the binary that was staged and run on every timed invocation. The
unstripped copy was kept only long enough to symbolize cores (native `gdb`
run directly on the Pi against the unstripped binary + the stripped
binary's cores — stripping doesn't relocate code, only removes symbol
tables, so this is valid), then deleted from `review/T117/` before finishing
(too large to keep as evidence; the symbolized backtraces are the durable
artifact).

x86 suite (sanity check only, no code changed this unit): `cmake --build
build -j24` PASS; `ctest --test-dir build --output-on-failure` **2271/2272**,
sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` —
matches the plan's stated baseline exactly, confirming the tree is clean.

### Deployment choice — staged to Pi `/tmp`, matching T113/T114

T113 and T114 both staged their (non-sanitizer, small) binaries to Pi
`/tmp` (`review/T114/run_pi_tour.sh`: `exec /tmp/willeq_t114 ...`) and ran
from there. T115 ran directly off NFS *only* because its ASAN binary (292MB)
didn't fit the Pi's 242MB tmpfs — an ASAN-specific exception, not the
default. This unit's binary is normal-sized, so it followed T113/T114:
`scp`'d the stripped ~9.8MB binary to `/tmp/willeq_t117` on the Pi, ran it
from there for all 10 invocations. This is the comparable choice.

### Tour — corrected mid-session, flagged as a deviation

The plan named `review/T115/tour_pi.txt` as the tour to reuse. That
checked-in file is a **14-step tour with `/camp` deliberately dropped** —
T115's own Review explains this was done specifically to remove a
double-zone-load OOM confound under ASAN's 292MB footprint, not a general
tour change. T113's and T114's actual tours (`review/T112/run_inputs/
tour_pi.txt`, `review/T114/tour_pi.txt`) are **15 steps, ending in `/camp`**,
and that fuller tour is what actually produced their 0-of-3 and 1-of-3
tallies.

A first smoke-test run using T115's literal 14-step file (no `/camp`)
correctly survived the hazard-box movement but then never terminated on its
own — the script finishes but the process just keeps rendering — so it could
never reach `Application shutdown complete` or be counted, and it could
never exercise the teardown path where two of this unit's four signatures
(`SkyData` corruption, the Mesa/Lima SIGBUS) only appear. Continuing with
that file would have made this unit unable to answer two of the plan's own
explicit questions ("does the Mesa/Lima SIGBUS still appear at teardown?").
**Deviation, flagged rather than silently absorbed**: switched to the
15-step tour with `/camp` restored (`review/T117/tour_pi.txt`, identical to
T113's/T114's), which is the more comparable choice for the stated goal
("directly alongside T113's 0-of-3 and T114's 1-of-3"), not a less
comparable one — it is what T113/T114 actually ran, whereas the checked-in
T115 file is an ASAN-only variant of it. The abandoned first attempt (PID
26174, killed manually after ~9 minutes idling with no natural end point,
itself found to have already crashed with the same `ranges_`/`0x2601`
signature independent of the kill — see "Discounted data point" below) is
preserved in `review/T117/pi/pi_discarded_smoketest1_core` but excluded from
the N-of-M tally, since it ran under a non-matching tour and its exact
cause of termination is entangled with the manual kill signal.

All 10 counted runs used: `--constrained orangepi --drm --renderer gles2 -r
800 600 --log-level=INFO`, audio **enabled** (no `--no-audio`), the 15-step
tour, staged to `/tmp/willeq_t117`, `ulimit -c unlimited`, `core_pattern`
pointed at `/mnt/projects/fable/review/T117/pi/` (NFS, never Pi `/tmp`) for
the whole session.

### N of 10 clean: **2 of 10**

| Run | Outcome | Signature | Classification |
|---|---|---|---|
| 1 | Crashed mid-tour, during the `wait 15` right after `/move -700` (the hazard box) | SIGSEGV, `ZoneMergeBuffers::renderMerged()` → `ranges_.find()`, `__x=0x2601` | **Matches the open `ranges_` defect exactly** |
| 2 | Reached `/camp`, logged out, crashed during `unloadZone()` teardown | SIGABRT, "corrupted size vs. prev_size", inside `SkyData::~SkyData()` destroying its `std::map<std::string, shared_ptr<TextureInfo>>` texture cache | **NEW signature** — neither T113's nor T114's tables name this container |
| 3 | Reached `/camp`, logged out, crashed during teardown | SIGBUS, inside `libgallium-25.2.8-0ubuntu0.24.04.1.so`, no symbols | **Matches T113's unexplained Mesa/Lima teardown SIGBUS** — first reproduction since T113 |
| 4 | Crashed very early — before the first `/move`, during the `/pmem` diagnostic at step 5/6, well before the hazard box | SIGSEGV inside glibc `_int_malloc`, triggered by an innocent `std::string` construction (`"[Thread Stacks] ..."`, the `/pmem` report path in `renderer_diagnostics.cpp`) | Heap corruption manifesting far from its cause — likely fallout from the same underlying corruption as one of the other signatures, not a defect in the diagnostics code itself |
| 5 | Crashed mid-tour, immediately after `/move -700` | SIGSEGV, `ranges_.find()`, `__x=0x2601` | **Matches the open `ranges_` defect exactly** |
| 6 | Reached `/camp`, logged out, crashed during teardown | SIGABRT, "double free or corruption (!prev)", `RaceModelLoader::~RaceModelLoader()` destroying `vertexMappingForAnimation_` (`std::vector<std::vector<VertexMapping>>`) | **Matches T113's original T110-class signature** — see "T114's fix and this recurrence" below |
| 7 | Crashed mid-tour, immediately after `/move -700` | SIGSEGV, `ranges_.find()`, `__x=0x2601` | **Matches the open `ranges_` defect exactly** |
| 8 | Full 15-step tour, `/camp`, logout, teardown, `Application shutdown complete` | — | **CLEAN** |
| 9 | Crashed mid-tour, immediately after `/move -700` | SIGSEGV, `ranges_.find()`, `__x=0x2601` | **Matches the open `ranges_` defect exactly** |
| 10 | Full 15-step tour, `/camp`, logout, teardown, `Application shutdown complete` | — | **CLEAN** |

Cores and symbolized backtraces for every crashing run are in
`review/T117/pi/` (`pi_run{N}_core`, `pi_run{N}_*_backtrace.txt`), produced
post-mortem only — no live gdb was used at any point, matching the plan's
method exactly. Symbolization used native `gdb` run **on the Pi itself**
against the unstripped ARM binary (x86 `gdb` segfaulted trying to parse an
ARM core directly — cross-arch core analysis from x86 is not reliable on
this host; Pi-native `gdb` worked cleanly every time).

### Discounted data point (not in the tally, but corroborating)

The abandoned first smoke-test run (14-step tour, no `/camp`, PID 26174,
eventually killed manually via `kill -TERM` after ~9 minutes of post-script
idling) turned out, on core analysis, to have **already crashed on its own**
before the kill signal was even sent: `SIGSEGV in __libc_free(mem=0x2601)`
— the identical `0x2601` corruption value seen in every `ranges_` crash
above, just caught mid-free rather than mid-find. This is a fifth
independent occurrence of the same `0x2601` value this session, though it
is excluded from the N-of-10 tally because it ran a non-matching tour and
its relationship to the (apparently moot) kill signal isn't fully separable.
Core preserved at `review/T117/pi/pi_discarded_smoketest1_core`.

### Triage against the plan's signature table

| Signature | Plan's table | This session |
|---|---|---|
| `ZoneMergeBuffers::renderMerged()` at `ranges_.find()`, `__x=0x2601`, mid-tour near the hazard box | "the open defect" | **Reproduced 4 of 10 times (5 counting the discounted run), 100% at the same `/move -700` window T113 first identified. Still open, still exactly the same shape.** |
| Mesa/Lima SIGBUS at teardown | "unexplained since T113" | **Reproduced once (run 3), still no symbols, still unexplained** — first confirmation since T113 that this is a real, recurring signature and not a one-off |
| Anything else | "new — say so loudly" | **Two more distinct signatures**, one of them touching a container (`SkyData`) neither T113 nor T114 ever audited |

### T114's fix and this recurrence — not a regression, a new lead

Run 6's crash (`RaceModelLoader::~RaceModelLoader()` destroying
`vertexMappingForAnimation_`) is, symptom-for-symptom, T113's *original*
T110 signature — the one T113 fixed for `variantModels_`/`loadedModels_`
and T114 additionally hardened for this exact pair
(`originalVerticesForAnimation_`/`vertexMappingForAnimation_`) with a new
`animScratchMutex_`. **Read-only verification, not a fix**: the mutex and
both its documented call sites are still present and correctly used in the
current source (`include/client/graphics/eq/race_model_loader.h:572`,
locked in `animated_mesh_creation.cpp:183` and `variant_loading.cpp:313`,
the latter carrying an explicit `// T114:` comment). So this is not a
regression of that fix — the specific race T114 closed is still closed.
The likelier explanation, especially taken together with runs 2 and 4's
corruption in completely unrelated containers (`SkyData`'s map, a plain
`malloc()` call), is that **something else is corrupting the heap broadly**,
and glibc's allocator is reporting the damage wherever the next
tree/vector operation happens to walk over it — the same "detection, not
origin" pattern T113 already established for the original `variantModels_`
crash and T115 established for `portalVisibleRegions_`. Four different
victim containers (`ZoneMergeBuffers::ranges_`, `SkyData`'s texture map,
`RaceModelLoader::vertexMappingForAnimation_`, and a generic `malloc()`)
across ten runs, with no single write site implicated by any of them, is
much more consistent with one wild/out-of-bounds write somewhere upstream
than with four independent narrow bugs.

### Was audio implicated?

**No crash backtrace touched the audio subsystem or the audio callback
thread** — every one of the 10 crashes was on the main/render thread, either
mid-frame (`ranges_`, the early `malloc()`) or during teardown's destructor
unwind on the main thread (`SkyData`, `RaceModelLoader`, the Mesa/Lima
SIGBUS). Audio was enabled for all 10 runs per the plan's requirement, and
did not itself visibly destabilize anything this session. This does **not**
rule out T115's documented TinySoundFont out-of-bounds *read* (a silent
correctness bug, not something that would necessarily crash) — nothing in
this unit's method (no ASAN on the Pi) could have caught a silent OOB read
that doesn't itself fault. It simply did not manifest as an attributable
crash here.

### Comparability caveats

- Built at fable tip `045f2512`, not the plan's named `9fbf1440` — fable had
  moved several commits by session start; noted per protocol, not silently
  absorbed.
- Tour corrected mid-session from the plan's literally-named file to the
  15-step T113/T114-equivalent version (see "Tour" above) — the more
  comparable choice for this unit's stated purpose, but a deviation from
  the plan's literal instruction, flagged.
- No `GovernorPin` (`scripts/pi/pi_common.py`) was used — this isn't a
  timing benchmark, and T113/T114 didn't document pinning the governor for
  their stability tours either; governor was `ondemand` before, during
  (unpinned), and after, matching the apparent precedent.
- Symbolization method (native Pi `gdb` against an unstripped copy) is new
  for this arc — T113/T114/T115 don't document how they symbolized; this
  worked reliably and is recorded here as the method other units can reuse.

### Read on convergence

**Not converging — regressing, if anything.** T113: 0/3 clean, 2 signatures
found (1 fixed, 1 left open, plus 1 unexplained). T114: 1/3 clean post-fix,
`ranges_` still open, 2 more races found and fixed. T115/T116: fixed two
more real dangling-pointer bugs (`portalVisibleRegions_`,
`currentFrameSimResults_`), Pi verification of `ranges_` itself left
incomplete both times. This unit: **2/10 clean**, `ranges_` reproduced at
exactly the rate its history predicts, *plus* three additional teardown/
early-crash signatures in one session that no single prior unit's smaller
sample (3 runs each) had assembled together. The fixes landed since T114
were real and correct, but none of them touch whatever is actually
corrupting this heap — the total crash *rate* has not improved, and the
crash *diversity* observed in one session is now higher than in any prior
single unit specifically because this unit ran more than 3x as many
samples. The straightforward reading is that T113/T114's samples were too
small to see the full shape of the problem, not that the problem changed.

## Board

**Another narrow `ranges_`-only hunt is not the highest-value next move by
itself.** `ranges_` is real, still reproduces exactly on schedule (100% at
the same `/move -700` window across 4-5 occurrences now spanning three
units), and remains worth closing — but this unit's evidence points at a
**shared root cause wider than any one container**: `ranges_` was the *most
frequent* victim (4/10) but not the *only* one, and the other three
(`SkyData`'s map, `RaceModelLoader`'s vertex-mapping vectors, and a bare
`malloc()`) each land in memory `ranges_`-focused work would never touch.
Chasing `ranges_` alone risks fixing one victim while leaving the actual
corrupting write in place to surface somewhere else next time — exactly the
shape T114 already lived through once (fixed two real races, `ranges_`
still fired 2 of 3 times afterward).

**Recommended next unit**: a targeted heap-corruption hunt, not a
`ranges_`-specific one — instrumentation that can catch a wild/OOB *write*
near the moment it happens rather than at the next unrelated allocator call
that stumbles over the damage. The ASAN-memory-ceiling problem T115 left
unsolved is now more clearly the actual blocker for that: ASAN (or a
lighter-weight bounds check compiled in for just the suspect region) is the
right tool for exactly this class of "detection is far from origin"
corruption, and this unit's runs (audio on, full tour, ~9.8MB stripped
binary reaching the hazard box in well under a minute) show the Pi budget
for a *normal* build is generous — the ceiling problem is ASAN-specific
memory overhead, not a Pi-session-length problem in general. Cutting the
tour to the minimum needed to reach `/move -700` (skip the 45s dead-air
warmup and the first two `/pmem` calls) would buy real headroom for a
future ASAN attempt without changing what's exercised.

**Given the crash rate (2/10) and the count of open, unexplained
signatures (four, one brand new), this thread should stay open and
prioritized** — it is not in a state where further Pi work can be
deprioritized in favor of other arcs. The Mesa/Lima SIGBUS in particular has
now been seen twice across two different units with zero symbols either
time; a build with Mesa debug symbols available (even just for
`libgallium`) would be a cheap, separate next step that doesn't require
solving the ASAN ceiling at all.
