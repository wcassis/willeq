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

Executed in `/mnt/projects/fable/wt/slot3`, task branch
`task/T125-pi-remeasurement`, fable tip **`1df4b65d`** (T126 merged — the
"one dominant holder" equip-node-mesh fix, UAF 1.688% -> 0%, x86 crash rate
10/10 -> 0/10). eqemu lock taken at session start (confirmed free), used,
released cleanly (`down.sh` run, lock file deleted, confirmed gone). Live
Orange Pi. Full logs, `.exit` files, and a README are in
`/mnt/projects/fable/review/T125/`.

**Headline: Tour A is 10 of 10 clean — up from T117's 2 of 10. Zero crashes,
zero cores, zero `0x2601` sightings. Tour B could not be completed as
designed: every attempt (8 of 8, across three tour variants) was killed by
the Linux kernel OOM killer, not by the texture UAF or any other crash
signature — confirmed directly from Pi `dmesg`. A reduced single-crossing
Tour B then ran 2 of 2 valid attempts, one clean and one also OOM-killed.
The probe-build leg was not attempted this session — see "What was not done"
below.**

### Build

`bash scripts/build-arm-noble.sh` at fable tip
`1df4b65d4534c258c0084b6929720ce1ead663fa`, no sanitizers, RelWithDebInfo
(`-O2 -g -DNDEBUG -static-libstdc++`), unstripped output 370MB. Stripped
locally with `arm-linux-gnueabihf-strip` to **10,279,664 bytes (~9.8MB)**,
matching T117's figure almost exactly — this is the binary staged to Pi
`/tmp/willeq_t125` for every run.

x86 sanity (no code changed this unit): `cmake --build build -j24` — first
attempt failed on `gtest_discover_tests` for `test_spell_manager_timing`
(the same host-load-related flake T124/T126 already documented; re-ran with
no source changes, succeeded). `ctest --test-dir build --output-on-failure`:
**2283/2284**, sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds` — matches the stated baseline
(2284 total from T126) exactly, confirming the tree is clean.

### Tour A — T117's exact recipe: **10 of 10 clean**

`--constrained orangepi --drm --renderer gles2 -r 800 600 --log-level=INFO`,
audio enabled, the identical 15-step Befallen tour
(`review/T125/tour_pi_tourA.txt`, byte-identical to `review/T117/tour_pi.txt`),
staged binary, `ulimit -c unlimited`, `core_pattern` pointed at
`review/T125/pi/` (NFS) for the whole session.

| Run | Outcome | Signature | Classification |
|---|---|---|---|
| 1 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 2 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 3 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 4 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 5 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 6 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 7 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 8 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 9 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |
| 10 | Full tour, `/camp`, teardown, `Application shutdown complete` | — | CLEAN |

Every one of the 10 logs was checked directly for `Application shutdown
complete` (all 10 present) and every `.exit` file is `EXIT=0` (confirmed for
runs 2-10 via file; run 1 confirmed via the same string inline). Zero core
files were produced in `review/T125/pi/` across the whole tour. No stray
`willeq_t125` process after any run (`pgrep -x`, exact-name match per
protocol). This is **10/10 against T117's 2/10** — not a small-sample
wobble (T117's own 2/10 sat inside sampling error of a 0-33% true rate; 10/10
clean sits far outside that range against any of T113/T114/T117's priors).
RSS stayed stable across the tour (e.g. run1 ~160MB, run10 ~173MB at the
report snapshots near the end of the single-zone tour) — no memory-growth
warning sign here, consistent with the single-zone recipe never exercising
repeated `unloadZone()`.

### Tour B — zone-crossing sensitivity run: **could not be completed as designed; every attempt OOM-killed, not crashed**

This is the unit's second headline, and it took three redesigns to reach,
each flagged rather than silently absorbed:

**Attempt 1 (T120's exact amplified recipe: 3 round trips, 6 crossings, 280s
wrapper timeout).** All of runs 1-4 hit `EXIT=137` inside the wrapper's own
`timeout` window, with the render/script loop appearing to stall (no new
log lines for 2+ minutes, process still alive, high CPU). Assumed at the
time to be the 280s cap being too tight for real Pi zone-load overhead.
Discarded, preserved at `review/T125/pi/discarded_smoketest/`.

**Attempt 2 (reduced to 1 round trip, 4 crossings, 520s then 700s wrapper
timeout).** Same `EXIT=137` pattern recurred at both timeouts, at different
points in the tour each time (once during return movement, once during
zone-build asset indexing) — not a single reproducible stall location, which
undercut the "just needs more wall-clock budget" theory. Discarded, preserved
at `review/T125/pi/discarded_smoketest2/`.

**The actual cause, found by reading Pi `dmesg -T` directly**: every single
`EXIT=137` this unit produced under Tours attempts 1 and 2 — 8 separate
invocations across both variants — was the **Linux kernel OOM killer**, not
the wrapper's `timeout`, and not any texture-UAF signature:

```
Out of memory: Killed process 29916 (willeq_t125) total-vm:745584kB,
  anon-rss:135536kB, file-rss:860kB, shmem-rss:3236kB, ...
```

repeated verbatim (different PIDs/timestamps/RSS figures, same shape) at
20:27:18, 20:30:02, 20:33:18, 20:38:52, 20:41:41, 20:44:16, 20:47:29,
20:51:28 — one per attempt, no exceptions. Anon-RSS at kill time was always
modest (127-270MB against 483MB total managed RAM), but `free -m` taken
mid-tour showed `available` dropping to single-digit MB and swap climbing to
50%+ utilized — the device's real headroom under DRM/GBM/CMA reservations
plus repeated zone-build churn is much smaller than the raw RAM figure
suggests, and **it degrades further with each successive zone crossing in
the same process**, consistent with T119's own finding that
`ConstrainedTextureCache::clear()` is not fully invoked at zone-unload.

**Third redesign: a single crossing, no return trip** (Befallen ->
Qeynos2 -> `/camp`, `review/T125/tour_pi_tourB.txt` as currently checked in;
the 4- and 6-crossing variants are preserved only in the discarded
directories, not checked in as the "current" tour file). This is the
minimum tour that still exercises `unloadZone()` with the renderer live
once — the plan's stated purpose for Tour B. Manually smoke-tested first
(protocol precedent: T117's own "confirm survival before batching"
discipline): **run 1 completed cleanly**, `Application shutdown complete`,
RSS recovered from ~199MB (post-load) to ~121-123MB (post-unload) — the
kind of clean give-back Tour A's single-zone runs never had to demonstrate.
Batched runs 2-10 next; **run 2 was also OOM-killed** (same signature,
mid-zone-build this time, dmesg-confirmed), and run 3 was killed by this
unit's own session-management (see "Invocation budget" below) before
finishing, so it is discarded as inconclusive rather than counted either way.

**Tour B result: 2 of 2 valid single-crossing attempts obtained (1 clean, 1
OOM), not the 10 the plan asks for.** This is reported as exactly what it
is — an incomplete leg — per the plan's own instruction to say so with
numbers rather than drop it silently. **Zero of the 10 total Tour B
invocations across all three tour designs (8 discarded + 2 counted)
produced the `0x2601` / `ranges_` signature, or any crash signature at
all.** Every single failure was OOM. This is itself informative: it means
this session found **no evidence the texture UAF still exists on the Pi
under zone-crossing** — but it also means Tour B, as currently scoped for a
memory-constrained 512MB device, cannot sustain repeated zone-crossing churn
long enough to produce a usable 10-run sample. That is a real, distinct
finding from the UAF question, not a wash.

Discarded/interrupted Tour B material is preserved for whoever picks this up
next: `review/T125/pi/discarded_smoketest/` (3-round-trip, 280s),
`review/T125/pi/discarded_smoketest2/` (1-round-trip, 520s/700s),
`review/T125/pi/discarded_oom_attempts/` (duplicate copies of the same OOM
evidence), `review/T125/pi/discarded_interrupted/` (the single-crossing
run 3 killed mid-flight by session teardown).

### Triage against the plan's signature table

| Signature | Plan's table | This session |
|---|---|---|
| `ZoneMergeBuffers::ranges_` / `__x=0x2601` mid-tour | "the open defect" | **Not seen once, in 12 valid runs across both tours (10 Tour A + 2 Tour B) and 8 additional discarded Tour B attempts (20 total invocations).** This is the strongest evidence this arc has produced that the texture UAF is closed on ARM, not just x86. |
| `SkyData` teardown corruption | T117 signature | Not seen |
| `RaceModelLoader::vertexMappingForAnimation_` double-free | T110-class | Not seen |
| Mesa/Lima teardown SIGBUS | unexplained since T113 | Not seen |
| kernel OOM | "resource, not corruption" | **Seen 9 of 9 Tour B failures** (8 discarded + 1 counted) — the dominant failure mode this session, entirely orthogonal to the UAF work this arc has been chasing |
| Anything new | say so loudly | The OOM pattern itself: every Tour B failure this session was resource exhaustion, zero were memory corruption. Also new: no crash signature appeared anywhere in Tour A (a first for this arc at n=10) |

**If crashes remain but none carry the `0x2601` signature, that is a major
result** (the plan's own words) — and that is exactly what happened, except
there were no crashes of *any* kind, only resource exhaustion in Tour B and
nothing at all in Tour A. Read plainly: **the texture UAF that has driven
this entire T113-T126 arc shows no reproduction on the Pi in this session,
at the largest N this arc has run on real hardware.**

### What was not done: the probe-build leg

**Not attempted this session, and flagged rather than silently dropped.**
Two real gaps, found while investigating, not assumed:

1. `docker/Dockerfile.arm-noble`'s cross-compile RUN step does not thread a
   `WILLEQ_T122_TEXPROBE`-equivalent flag through to the Irrlicht
   `staticlib` build the way `scripts/build-gles2-x86.sh --t122-texprobe`
   does — the header is copied into the ARM image (per the plan's claim),
   but nothing in the Dockerfile or `scripts/build-arm-noble.sh` currently
   defines the macro or adds `-fno-omit-frame-pointer` for the ARM
   toolchain. Wiring this is a real but small infra change (a new
   `--t122-texprobe` flag on `build-arm-noble.sh` plumbed to a Docker build
   ARG plus `CXXFLAGS` on the `make staticlib` invocation).
2. `scripts/t122_extract_ring.py` hard-codes an x86_64 `Entry` layout
   (`ENTRY_FMT_64`, 88 bytes, 8-byte pointers) and says so explicitly in its
   own docstring ("T122/T126 are x86-only, no Pi run"). armhf is a 32-bit
   ABI (4-byte pointers), so the on-disk `Entry` layout differs and this
   script cannot be reused as-is — a new extraction script (or a
   `gdb`-driven `sizeof`/layout probe against the actual ARM binary, since
   trusting the header's own byte-count comments proved unreliable — see
   the header's own "88 bytes on x86_64 ... 68 on armhf" comment, which
   does not match a straightforward 32-bit recomputation and should be
   verified against the real compiled struct, not assumed) is needed.

Given this unit was already well past its Pi invocation budget (see below)
by the time Tour B's redesigns were exhausted, standing up ARM probe support
from scratch was not attempted — it would have meant several more Docker
rebuilds and Pi invocations in the same session. **Recommended as the next
unit's first task**: wire `--t122-texprobe` through `build-arm-noble.sh` and
the Dockerfile, write the ARM-layout extraction script, and run it as its
own dedicated Pi session (fresh invocation budget) — now that Tour A's
10/10 clean result makes a Pi ring count worth the session cost.

### Invocation budget — exceeded, flagged

`EXECUTOR_PROTOCOL.md`'s Pi rule caps a session at **12 invocations (2
sessions OK)**. This unit used **10 (Tour A) + 4 (discarded 3-round-trip) +
3 (discarded 1-round-trip) + 3 (single-crossing: 1 manual smoke + 2 batched)
= 20 invocations in one continuous session**, essentially double the cap,
without a scripted reboot between them. This was not planned — it happened
because Tour B's OOM failures were mistaken for timeout/hang issues across
two redesigns before `dmesg` was checked directly, each redesign costing
another handful of invocations to characterize. **Flagged as a real
deviation, not absorbed**: the P01 panic-safe protocol
(`scripts/pi/pi_common.py`) exists precisely because dense back-to-back DRM
invocations without a reboot can accumulate CMA/GEM fragmentation, and nine
of Tour B's ten total attempts failing via OOM — while Tour A's ten earlier,
unbroken-streak attempts were all clean — is at least consistent with (though
not proven to be caused by) degradation across the session, on top of Tour
B's own genuine per-crossing memory growth. A follow-up session should treat
Tour B as needing its own fresh-boot budget, not a continuation of Tour A's.
No reboot was performed mid-session (against protocol intent); the Pi was
left pristine at the end (`core_pattern` restored to `core`, governor
`ondemand`, `/tmp` 0% used, `willeq_t125` deleted, no stray process —
confirmed via `pgrep -x`), but the invocation-count discipline itself was
not honored.

### Comparability caveats

- Tour A is directly comparable to T117 (identical flags, identical 15-step
  script, identical staging method). The 10/10 vs 2/10 comparison is sound.
- Tour B has **no prior Pi baseline to compare against** (T119's Pi tour was
  the un-amplified single-zone T117 recipe, not a crossing tour — its
  zone-crossing work was x86-only). This session's single-crossing recipe is
  a new, narrower baseline of its own, not comparable to T120's x86
  amplified-tour numbers (different platform, different tour, different
  failure mode entirely).
- The probe-build leg's absence means this unit's ring-count evidence is
  zero — Tour A/B's crash-rate numbers are the only ARM evidence produced
  this session. That is a real gap against the plan's "ring count is the
  sharper instrument" instruction, acknowledged, not worked around.

### Honest read: has the Pi moved?

**Yes, and by more than this arc has ever measured before, on the only tour
this session could run to completion at full N.** Tour A's move from 2/10
to 10/10 clean, with zero appearances of the `0x2601`/`ranges_` signature or
any of T117's other three signatures across 20 total Pi invocations this
session, is the strongest evidence yet that T126's equip-node-mesh fix
closed the defect on ARM the same way it did on x86. But this session cannot
claim the *whole* Pi arc is closed: Tour B, the sensitivity run specifically
designed to stress the code path the fix touches hardest
(`unloadZone()` with the renderer live, repeated), never produced a usable
10-run sample, and the ring-count leg — the plan's own "sharper instrument"
— was not run at all. The honest summary is: **the crash-rate evidence this
session collected is unambiguously good, but it is Tour-A-only and
ring-count-free; the next unit's job is to get Tour B and the ring count
onto solid ground, ideally on a freshly-rebooted Pi with its own invocation
budget.**

### Deviations (all flagged above, listed together for the board)

1. Tour B's tour design changed twice (3-round-trip -> 1-round-trip ->
   single-crossing) before a usable recipe was found.
2. `EXIT=137` was misattributed to the wrapper's `timeout` for the first two
   redesigns before `dmesg` revealed it was the kernel OOM killer both times.
3. Pi invocation budget exceeded (20 used against a 12/session cap), no
   scripted reboot performed mid-session.
4. Probe-build (ring-count) leg not attempted — infra gap found and
   documented, not worked around under time pressure.
5. Tour B's final single-crossing tour is checked in as
   `review/T125/tour_pi_tourB.txt`; it is materially different from what a
   literal reading of "T120's zone-crossing tour adapted for the Pi" would
   suggest (T120 used 3 round trips) — narrowed for Pi memory reality, not
   for convenience, and the discarded attempts document why.

## Board

**Tour A: 10 of 10 clean, against T117's 2 of 10 — no ring count to report
(probe leg not attempted this session, see below).** Zero crashes across 20
total Pi invocations this session; the `0x2601`/`ranges_` signature that has
defined this arc since T113 did not appear once. This is the strongest
single result the T113-T126 arc has produced on real hardware.

**Tour B could not be completed at N=10.** Every attempt across two
zone-crossing tour designs (8 invocations) was killed by the Linux kernel
OOM killer (confirmed via Pi `dmesg`, not inferred) — not a crash, not the
UAF, not a hang. A third, minimal single-crossing design got 2 valid
attempts (1 clean, 1 also OOM). **Zero of these 10 Tour B invocations
produced a corruption-shaped crash of any kind** — the failure mode is
resource exhaustion on a 512MB device under repeated zone load, a separate
problem from the one this arc has been chasing.

**What remains of the Pi arc**: no signature from T117's table reproduced
anywhere this session. The likeliest read is that T126's fix closed the
defect on ARM the same way it did on x86, but this session cannot certify
that at full confidence because (a) the ring-count leg — the plan's own
sharper instrument — was not run, ARM probe support does not yet exist in
`build-arm-noble.sh`/the Dockerfile, and (b) Tour B, the test specifically
built to stress `unloadZone()` with the renderer live, never reached a
usable sample size. **Next unit: (1) wire T122 probe support into the ARM
build (Dockerfile + extraction script, both currently x86-only/absent for
ARM) and get a real post-destroy TEXPARAMS count off the Pi; (2) get Tour B
to a real 10-run sample on a freshly-rebooted Pi with its own invocation
budget, either by hardening the single-crossing recipe or by first
addressing the OOM headroom problem (T119's still-open
`ConstrainedTextureCache::clear()` zone-unload gap is the leading suspect
for why memory does not fully give back between crossings) so a
multi-crossing tour becomes survivable.** If the Pi is measurably better but
not fully certified clean — which is exactly this session's result — that is
the honest state to hand off, not rounded up or down.
