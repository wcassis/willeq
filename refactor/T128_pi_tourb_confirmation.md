# T128 — Pi Tour B confirmation, and the arena tradeoff measured

**Slot: whichever is free. Pi required. eqemu lock required. No sanitizers.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T128/`. This unit fixes
nothing — it measures and triages.** If you find yourself editing `src/`,
stop and report.

Read T125's Review first (Tour A's 10-of-10, Tour B's OOM kills), then T127's
(what was fixed and the arena tradeoff).

## Two questions, one Pi session

**1. Does Tour B now complete?** T125 could not run a Pi zone-crossing tour at
all — every attempt was killed by the kernel OOM killer, confirmed from
`dmesg`. T127 then cut x86 crossing growth from **+306.8 MB to +42.0 MB** with
a flat per-crossing profile. **This is the confirmation on the hardware that
matters, and it closes the one leg T125 left open.**

**2. Does `mallopt(M_ARENA_MAX, 1)` cost anything?** T127's fix serializes
every thread's allocation onto one arena lock. That tradeoff was reasoned but
**never measured** (see the T127 orchestrator-review entry in `deferred.md`).
**Measure it from these same runs — it needs no extra invocations.**

## Build

`fable` tip **`2f10b8a3`** (T127 merged). Normal ARM cross-compile, **no
sanitizers**. State the hash you built and the stripped binary size.

## Tour B — the primary result

T125's reduced single-crossing recipe and its full crossing variant are both
preserved in `review/T125/`. Use the **fullest variant that T125 could not
complete**, so this is a like-for-like answer to the question it left open.
Same flags as Tour A: `--constrained orangepi --drm --renderer gles2 -r 800
600 --log-level=INFO`, audio enabled.

**Report N of 10 completed cleanly**, and for every failure say whether it was
**OOM** (`dmesg`, not inferred from exit code — T125 initially misread
`EXIT=137` as a timeout) or a **crash**, with the signature.

**Sample RSS across the tour** via the existing `/pmem` output, including
T127's new `[Allocator]` line, and report **MB per crossing** against T127's
x86 figures (+36.1 MB on the first crossing, 0.0–0.1 MB on each subsequent
one). The question is whether the flat profile holds on ARM.

## The arena measurement — from the same runs

- **Frame cost**: use T112's methodology and compare against its recorded Pi
  frame-time baseline. A single-arena lock contended by the game, render,
  audio, `EntityPrepWorker`, `SimulationWorker`, GPU-upload and loading
  threads would show up here.
- **Audio**: the concrete worry is the **real-time audio callback** blocking on
  an arena lock the `LoadingThread` holds during zone load. Watch for
  underrun/xrun warnings in the logs across each crossing, and say plainly
  whether any appeared. If the logs carry no such signal, say that instead of
  implying silence is evidence.

**If the arena change costs something measurable, say so plainly.** `M_ARENA_MAX=2`
or `4` is the available middle ground — but **do not change it in this unit**;
report the measurement and let that be a decision.

## Triage — one new possibility to watch

T127 added four `drop()` calls to node factories. **An over-release would
manifest as a new crash signature**, not as OOM. If you see a crash that does
not match anything in the table below, treat T127's `drop()` changes as a
prime suspect and say so.

| Signature | Status |
|---|---|
| kernel **OOM** | what T125 hit; the thing this unit tests |
| `ZoneMergeBuffers::ranges_` / `__x=0x2601` | closed by T126; a reappearance would be major |
| `SkyData` teardown corruption | same family |
| `RaceModelLoader` double-free | T110-class, separate |
| Mesa/Lima **SIGBUS** | never explained |
| **anything new** | **suspect T127's `drop()` additions first** |

## Discipline

- **Respect the Pi invocation cap: 12 per session.** T125 used 20 with no
  mid-session reboot and flagged it as a breach. If you need more, **stop and
  report** rather than continuing. Budget accordingly: Tour B's 10 runs plus
  build/smoke leaves almost nothing spare.
- **Do not fix anything.** Diagnosis into the Review; no `src/` changes.
- **Do not stop early on a clean streak.** Run the full count.
- **Report rates with denominators.**
- Cores and logs to **NFS, never `/tmp`**. `--log-level=INFO`. No live gdb.
  Teardown by real binary PID (`pgrep -x`), `down.sh`, lock deleted and
  confirmed gone, `core_pattern` and governor restored.

## Verification

- x86 suite untouched (**2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) — a sanity check that your tree is
  clean, since you are changing no code.

## Review

(executor fills: tip and binary size; **Tour B N of 10**, with every failure
classified OOM-vs-crash from `dmesg`; **MB per crossing on ARM** against
T127's x86 figures; the frame-cost comparison against T112's baseline; whether
any audio underrun signal appeared, or whether the logs carry no such signal;
any new signature and whether T127's `drop()` changes are implicated;
deviations, including invocation count against the 12 cap)

## Board

Lead with Tour B's N of 10. If it completes, say that T125's open leg is
closed and what remains of the Pi arc. Then give the arena verdict separately:
if `M_ARENA_MAX=1` costs measurably, put the `M_ARENA_MAX=2/4` decision on the
board with the numbers; if it does not, say the T127 caveat is discharged.
