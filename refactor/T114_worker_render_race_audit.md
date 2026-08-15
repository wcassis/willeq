# T114 — Find the whole class: races across the worker/render boundary

**Slot: whichever is free. x86 first — TSAN does not fit the Pi. Pi only if
Part 3 needs it. eqemu lock if a live session is required.**

**Territory: `tests/`, `tools/`, `refactor/`,
`/mnt/projects/fable/review/T114/`, plus whatever a confirmed race
implicates. Sanitizer build configuration is in scope.**

Read T113's Review first, then T111's and T112's.

## Why this unit exists — stop fixing them one at a time

Three crashes in three units, all the same shape:

- **T111**: dangling `regionPvsDepthMap_` pointer into a local
  `SimulationOutput` — fixed.
- **T113**: `RaceModelLoader::variantModels_`/`loadedModels_` accessed by the
  background prep worker and the main thread without the matching lock —
  fixed, and it explains T110's teardown abort.
- **T113, still open**: `ZoneMergeBuffers::ranges_` walked while already
  corrupted, crashing mid-play.

**All three are containers shared between the background prep worker and the
render thread without synchronisation.** T111 enabled sustained concurrent
background work for the first time and converted a latent class into live
crashes. At roughly one Pi session per crash, chasing them individually is the
slow way to find a class.

## Why TSAN on x86, not ASAN on the Pi

The defects are **data races**. ASAN finds memory corruption — the
*consequence*, at the point of damage. **TSAN finds the race itself**,
including instances that have not crashed yet, which is the point.

TSAN's memory overhead (roughly 5-10x) does not fit a 483MB Pi. x86 has the
headroom. **The races are in shared code** — they surface on the Pi because
four slow cores widen the windows, not because the code differs.

`libasan.so.8` is present on the Pi if Part 3 needs it, and P23 has precedent
for sanitizer work here (see `refactor/P23_particle_race_stability.md`'s
Review for the invocation it used — reuse anything applicable, including any
suppression file).

## Part 1 — Get TSAN running at all

This is the real risk. The races are between the prep worker and the **render**
thread, so graphics must be live — which means TSAN over llvmpipe, and
third-party noise from Irrlicht and Mesa.

- Build with `-fsanitize=thread` and get the client to run a zone-in.
- **Expect a suppression file** for third-party frames. Suppress by library,
  never by our own symbols — a suppression that hides our code defeats the
  unit.
- **State what you suppressed and why.** An unreviewable suppression list is
  how a sanitizer run becomes theatre.

**If TSAN cannot be made to run** — build failure, unusable noise, or it will
not complete a zone-in — say so precisely and fall back to Part 3. That is a
legitimate outcome, not a failure.

## Part 2 — The inventory is the deliverable

**Do not stop at the first race.** Produce a list of every unsynchronised
container or shared object crossing the worker/render boundary, each with:

- what it is, and which threads touch it
- the access pattern (write/read, read/read, iterate-while-mutated)
- whether TSAN flagged it or you found it by reading
- whether it has crashed in the wild yet

The known-open one — `ZoneMergeBuffers::ranges_` — should appear. If it does
not, that is important: it would mean TSAN is not reaching the path, and the
inventory is incomplete rather than clean.

Cross-check against the fixes T111 and T113 already made: **confirm they hold
under TSAN**, since both were reasoned rather than sanitizer-verified.

## Part 3 — Fix only what is unambiguous

Fix races that are clearly unsynchronised access to a shared container, using
the **existing per-container mutex pattern** T91, T111 and T113 all followed.
Additive locking only.

**STOP and report** if a fix requires changing the threading model, the
ownership of a subsystem, or lock ordering across subsystems. Deadlock
introduced while fixing a race is a worse outcome than the race.

If TSAN was unusable, use ASAN on the Pi to close `ZoneMergeBuffers::ranges_`
specifically — it fits (client 92.9MB, ~2x overhead, ~395MB headroom) and
`libasan.so.8` is installed.

## Verification — the honest bar

- x86 suite (current total 2266; sole pre-existing failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Then run the Pi tour.** T113 got 0 of 2 clean. **State how many consecutive
  clean Pi runs you observe** — that number, not the sanitizer output, is
  whether the platform is fixed. A clean TSAN report with a still-crashing Pi
  means the inventory missed something.
- Visual gate — goldens must not move from added locking.

Session discipline: binary freshness on both machines; **Pi cores go to NFS,
never `/tmp`** — T113 OOM'd the Pi writing a core to its 242MB tmpfs; restore
any sysctl you change; `--log-level=INFO` on the Pi, since T112 measured DEBUG
as destabilising; kill clients by real binary PID; leave the Pi pristine.

## What you may not do

- Do not suppress our own symbols in TSAN.
- Do not fix a race by removing the concurrency (e.g. making the worker
  synchronous). That would undo T111 and hide the class rather than close it.
- Do not claim the platform is stable on sanitizer output alone.

## Review

(executor fills: whether TSAN ran and what it cost to get there; the
suppression list and its justification; **the full inventory of shared
containers crossing the boundary**; whether `ZoneMergeBuffers::ranges_`
appeared; whether T111's and T113's fixes hold under TSAN; what was fixed and
why each qualified; **consecutive clean Pi runs**; what remains open;
deviations)

## Board

Record the inventory. It is the map of this defect class, and it should outlive
whichever individual races get fixed here.
