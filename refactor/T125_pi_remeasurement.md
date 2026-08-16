# T125 — Re-measure the Pi after the texture-holder fixes

**Slot: whichever is free. Pi required. eqemu lock required. No sanitizers.**

**DISPATCH ONLY AFTER T124 IS MERGED.** This unit measures the fixes from
T119, T123 and T124 together; running it against a tip that lacks T124 wastes
a Pi session and produces a number nobody can interpret. State the tip you
built.

**Territory: `refactor/`, `/mnt/projects/fable/review/T125/`. This unit fixes
nothing — it measures and triages.** If you find yourself editing `src/`,
stop and report.

Read T117's Review first (the method and the 2-of-10 baseline), then T123's
and T124's (what was actually fixed).

## Why

The x86 side of this arc has moved a long way: the corrupting write is
identified (T122), one holder closed (T123), the rest closed or ruled out
(T124). **None of it has been measured on the Pi**, which is the platform the
whole arc exists for.

The standing Pi numbers are **T117's 2 of 10 clean** (the only properly
powered sample) and **T119's 0 of 5** (underpowered, flagged as such). Both
predate every texture-holder fix.

**Do not infer the Pi from x86.** This arc has been burned by cross-platform
inference before — x86 ASAN was "clean" for three units while the Pi crashed,
because the runs differed in configuration. Measure it.

## Run both tours — they answer different questions

**Tour A — T117's exact single-zone tour.** This is the comparability run.
Same zone, same movement, same flags as T117 (`--constrained orangepi --drm
--renderer gles2 -r 800 600 --log-level=INFO`, audio enabled). **10 runs**,
reported as **N of 10 clean** directly against T117's 2 of 10. Deviating from
T117's recipe here destroys the only baseline worth comparing to.

**Tour B — T120's zone-crossing tour**, adapted for the Pi. This is the
sensitivity run: crossing exercises `unloadZone()` with the renderer live,
which is where the fixed defect lives. **10 runs**, reported as its own N of
10. There is no prior Pi baseline for this tour, so it establishes one.

Report the two separately. **Do not average them or blend them into a single
number** — they measure different things.

## The ring count is the sharper instrument here too

T122's probe is in tree, default-off, and its header is already in
`docker/Dockerfile.arm-noble`'s copy-list, so it **builds for ARM**.

A crash rate at n=10 cannot resolve small changes — that is the lesson T117,
T119 and T123 each paid for. **A post-destroy TEXPARAMS count of zero is
direct evidence the use-after-free is gone on this platform**, and it is
far more sensitive than counting crashes.

So: after the two uninstrumented tours, **run a probe-enabled build on the Pi
and report the post-destroy TEXPARAMS count.** Check the probe build's own
crash rate against the uninstrumented one first, exactly as T122 did — if the
probe perturbs the defect on ARM, its negatives cannot be trusted.

Note the probe binary is larger; if it does not fit or the run does not
complete, **say so with numbers** rather than dropping the leg silently. Pi
ASAN at `-g1` was 36.7MB stripped (T118), so a probe build should be
comfortable — but measure, do not assume.

## Triage every crash — the signatures are not all this bug

The Pi has crash shapes that this arc has never attributed to the texture
use-after-free. Classify each crash rather than assuming:

| Signature | Where | Status |
|---|---|---|
| `ZoneMergeBuffers::ranges_` / `__x=0x2601` | mid-tour, at `/move -700` | the texture UAF's known victim |
| `SkyData` texture map corruption | teardown | same family |
| `RaceModelLoader::vertexMappingForAnimation_` double-free | teardown | T110-class, separate |
| Mesa/Lima **SIGBUS** | teardown | never explained, likely unrelated |
| kernel **OOM** | anywhere | resource, not corruption |
| Anything new | — | say so loudly |

**If crashes remain but none carry the `0x2601` signature, that is a major
result**: it would mean the texture UAF is closed on the Pi and what remains
is a different defect — which redirects the whole arc. Say it plainly.

## Do not

- **Do not fix anything.** If triage root-causes something, write the
  diagnosis into the Review — that is valuable — but do not change `src/`. A
  fix mid-measurement invalidates the measurement.
- **Do not stop early on a clean streak.** Run the full count. Stopping at
  good news is how a 1-in-5 defect gets reported as fixed.
- **Do not present a 3- or 5-run sample as a rate.** T119 had to flag exactly
  that deviation. If you cannot complete 10, report what you got and say why.
- **Do not conclude the arc is closed** from a clean tour. Say what the sample
  size can and cannot support.

## Pi operating rules — learned the hard way

- **Cores and logs to NFS, never `/tmp`** — 242MB tmpfs on a 512MB box; T113
  OOM'd the Pi writing a core there.
- **`--log-level=INFO` only.** DEBUG was measured as destabilising (T112).
- **No live gdb** during reproduction — it suppresses this defect (confirmed
  twice).
- **Teardown by real binary PID** (`pgrep -x`, not a substring match on a
  command line — that has produced false positives). Then `down.sh`, delete
  the lock, confirm it is gone, restore `core_pattern` and the governor.

## Verification

- x86 suite untouched (current total **2274** plus whatever T124 added; sole
  failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`) — you
  are not changing code, so this is a sanity check that your tree is clean.
- ARM cross-compile, stated with the commit hash you built.

## Review

(executor fills: the tip built; **Tour A N of 10 against T117's 2 of 10**;
**Tour B N of 10** as a new baseline; the probe-build crash rate and the
**post-destroy TEXPARAMS count**; a per-run table with every crash classified
against the signature table; anything new; comparability caveats; your honest
read on whether the Pi has moved; deviations)

## Board

Lead with Tour A's number against 2 of 10, and the ring count. Then say what
remains of the Pi arc: which signatures still appear, whether any are now
unattributed, and what the next unit should target. If the Pi is measurably
better but not clean, say so plainly rather than picking a side.
