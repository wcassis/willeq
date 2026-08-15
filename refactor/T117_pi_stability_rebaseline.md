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

(executor fills: the exact build hash and config; deployment choice and
whether it matched prior units; **N of M clean**, with the per-run outcome
table; every crash classified against the signature table with its core; any
new signature; whether audio was implicated; comparability caveats; your read
on convergence; deviations)

## Board

State what this means for the next unit on the Pi arc — specifically whether
another `ranges_` hunt is still justified, and whether the ASAN-memory-ceiling
problem still needs solving. If the rate is clean, say what would have to be
true for `ranges_` to be genuinely closed versus merely quiet, and what it
would cost to establish that.
