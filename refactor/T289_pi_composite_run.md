# T289 — the Pi composite measurement (T287 Part 2; Will gave the go 2026-08-26)

Slot: slot1. OWNS the Pi. NO eqemu unless a zone-in needs it (then noclobber).
Class B, measurement only. Territory: measurement scripts + review/T289/.
**Do NOT change `config/constrained_presets.json` or any source.** Re-enabling
composites on the orangepi is a SEPARATE decision after Will reads these numbers.

## Why this is worth a Pi session
Decision #3 (composites OFF on orangepi) rests on T195/T197/T198: `std::bad_alloc`
on the Pi. T195 proved the cause precisely — `EQE1Loader::open()` **mmaps**, and
the eager loop mapped the whole composite directory: **VmSize peaked at 3,128 MB
against a ~3 GB 32-bit user ceiling, while RSS rose only ~40 MB and MemAvailable
never fell below ~260 MB**. T195's own words: *"the platform difference is 32-bit
vs 64-bit, not 483MB vs 32GB."*
T284 deleted that eager loop (1506 packs → 1 per sheet; 4,676,044 kB → 174,856 B
for a Human), and T287 Part 1 confirmed it live on x86: 7 sheet packs for
befallen's 7 composite races, all via index late-open, and composites ON mapped
**3.2 MB LESS in total** than OFF.

## What is genuinely unknown and only the Pi can answer
1. **VmSize/VmPeak with composites ON** — does it now sit far below the 3 GB
   ceiling? (Expected yes; this is the direct test of the decision's premise.)
2. **RSS** — mapping is cheap but the GPU-side upload of a decoded sheet is not.
   This is the number T195 never had to care about and the one that could still
   argue against composites on a 512 MB board.
3. **Zone-in time** and whether it completes at all (T193's `std::bad_alloc` is
   the failure to watch for).
4. Whether Mali/Lima behaves like llvmpipe for the composite draw path.

## Method
P02 protocol via `scripts/pi/pi_common.py`. `ARM_BUILD_BIN_DIR` must point at
THIS slot's `build-arm-noble/bin` (env.sh defaults to main — the trap). Rebuild
ARM before staging. All timed-run I/O to Pi-local `/tmp`. `drop_caches`, never
reboot. **≤12 invocations.** Leave the Pi pristine (governor ondemand, /tmp
clean). Pi is `user@10.0.30.192` via `scripts/pi/env.sh` — `opi1` in ssh config
is STALE. Report blocked if unreachable (kmsg: `/mnt/projects/fable/pi_console/`).

Run the same zone twice: composites ON (override the preset ON THE COMMAND LINE
or via a run-local config — not by editing the committed preset) and the shipped
OFF baseline. Sample RSS/VmSize/VmPeak/MemAvailable every 0.3 s as T195 did, so
the numbers are directly comparable to its table. Capture `EQT_EQE1_REQUEST_LOG_DIR`
for packs opened and bytes mapped.

## Deliverable
A table in the Review directly comparable to T195's, plus a plain statement:
does the address-space argument still hold, and does RSS give a NEW argument
against composites on this board? **Numbers and a recommendation — no preset
change.**

## Verification
No source change, so no ctest/gate needed unless something is fixed. Review in
this file; evidence review/T289/. Commit: `T289: composites measured on the Pi`.

## Review

**STATUS: complete. Composites ON zoned into befallen on the Orange Pi twice,
exit 0 both times, zero `std::bad_alloc`. VmPeak 575 MB against a ~3 GB
32-bit ceiling — T195's 3,128 MB peak is gone. The address-space argument
behind decision #3 no longer holds. RSS does not replace it: composites ON
costs ~+13 MB RSS on a 483 MB board. Recommendation: composites can be
re-enabled on `orangepi` — but that is Will's call, and this unit changed
nothing.**

No source, config or preset was touched. `git status` clean against `fable`
apart from this plan file. Composites were switched on the same way T287 did
it on x86: the `--composite-entities <dir>` CLI flag, which sets
`enableCompositeEntities=true` in `application.cpp:462-467`. The `orangepi`
preset never carried an `enableCompositeEntities` key at all — the member
simply defaults to `false` (`constrained_renderer_config.h:161`), so the
shipped OFF baseline is "no flag" and required no edit.

### How the runs were made

`/mnt/projects/fable/wt/slot1/build-arm-noble/bin/willeq` (rebuilt in THIS
slot, exit 0; `ARM_BUILD_BIN_DIR` never resolved to `main`), stripped to
10.6 MB, staged to Pi-local `/tmp` with a freshly-tarred `config/` + `data/`
from this slot. **The Pi's pre-existing `/tmp/config` and `/tmp/data` were
STALE and would have voided the measurement** — old `race_models.json`, an
Aug-23 `willeq.db`, and no `data/eqe1_texture_index.json` at all, i.e. no
T207 index, i.e. no late-open path to measure. Replaced before any timed run.

Per leg: `willeq -c t289_pi.json --drm --renderer gles2 --constrained orangepi
--eqe1-path /mnt/projects/EverQuest_ETC1 [--composite-entities
/mnt/projects/EverQuest_ETC1] -r 800 600 --log-level=INFO --script
t289_tour.txt`, account `willeq_test` / `Testchar`, zone **befallen**
(Testchar's home zone — the login zone-in, same as T287). Tour: `wait 180`,
`/quit`. `EQT_EQE1_REQUEST_LOG_DIR` set to a Pi-local dir. All timed I/O to
Pi `/tmp`; nothing written to NFS during a run.

Sampler: T195's, extended — RSS, VmSize, **VmPeak**, VmHWM, MemAvailable and
CmaFree every 0.3 s, with T195's `pgrep -x` fix (never `$!`, which is
`timeout`'s pid). **Two reps per leg, orders reversed** (control, composite,
composite, control), `drop_caches` before each.

### The table (directly comparable to T195's)

T195's row is the last sample before `std::bad_alloc`, at the same
`loadZoneSequential: [48%] Opening EQE1 textures` phase.

| run | peak RSS | **VmPeak** | peak VmSize | min MemAvailable | outcome |
|---|---|---|---|---|---|
| **T195 (eager composite scan, 2026-08-24)** | 197 MB | **3,128 MB** | 3,128 MB | 254 MB | **`std::bad_alloc`** |
| T289 composites **OFF** rep1 | 217.7 MB | **571.5 MB** | 571.4 MB | 63.0 MB | exit 0 |
| T289 composites **OFF** rep2 | 208.9 MB | **571.3 MB** | 571.2 MB | 66.2 MB | exit 0 |
| T289 composites **ON** rep1 | 230.3 MB | **575.0 MB** | 574.4 MB | 61.9 MB | exit 0 |
| T289 composites **ON** rep2 | 222.6 MB | **573.1 MB** | 572.6 MB | 54.9 MB | exit 0 |

The `[48%] Opening EQE1 textures` phase itself — the exact phase that threw in
T195 — now moves VmSize **367 MB → 435 MB** in *both* legs (1.2 s / 1.1 s).
T195 measured the same phase going **399 MB → 3,128 MB** in 1.4 s. That
2.7 GB spike is the deleted eager loop, and it is gone.

Per-leg, and what it cost:

| metric | composites OFF (rep1 / rep2) | composites ON (rep1 / rep2) | delta (mean) |
|---|---|---|---|
| zone-in | 23,858 / 22,944 ms | 27,507 / 22,988 ms | +1.8 s, **overlapping — see below** |
| peak RSS | 222,964 / 213,932 kB | 235,836 / 227,924 kB | **+13.4 MB** |
| VmPeak | 585,212 / 584,976 kB | 588,836 / 586,824 kB | **+2.7 MB** |
| min MemAvailable | 64,504 / 67,812 kB | 63,344 / 56,268 kB | -6.3 MB |
| EQE1 archives open | 11 / 10 | 17 / 17 | +6.5 |
| EQE1 mapped | 38,365 / 37,873 KB | 38,898 / 38,898 KB | +780 KB |
| lookups served | 268 / 267 | 234 / 234 | -33.5 |
| `buildCompositeMesh` | 0 / 0 | 2 / 2 | |
| `bad_alloc` / FATAL | 0 / 0 | 0 / 0 | |

**Zone-in is not a composite cost.** The reps overlap: composites ON rep2
(22,988 ms) is *faster* than composites OFF rep1 (23,858 ms). Two samples per
leg cannot separate a 1.8 s mean difference from a 0.9 s within-leg spread —
do not read a zone-in penalty into this. (Load time is secondary to
correctness with no maximum anyway.)

### The mechanism reproduced exactly, on ARM

Seven sheet packs, seven `late-open` lines, rule R2, one owner each —
byte-identical to the x86 numbers in `review/T287/README.md`:

| pack | mapped | x86 (T287) |
|---|---|---|
| `dam_hs_def_0_0` | 170 KB | 174,856 B |
| `gnf_hs_def_0_0` | 170 KB | 174,856 B |
| `gnm_hs_def_0_0` | 170 KB | 174,856 B |
| `hom_hs_def_0_0` | 85 KB | 87,496 B |
| `hum_hs_def_0_0` | 170 KB | 174,856 B |
| `rat_hs_def_0_0` | 85 KB | 87,464 B |
| `ske_hs_def_0_0` | 170 KB | 174,984 B |

Total mapped with composites ON is **38,898 KB on the Pi and 38,898 KB on
x86** — the same number to the kilobyte. Two composite meshes built on the Pi,
the same two as x86: race 367 (SKE, 1053 verts, 13 textures, 1 buffer) and
race 36 (RAT, 904 verts, 8 textures, 1 buffer). Zero `not yet uploaded`, zero
`empty input data` (T192's misconfiguration tell), zero NOT-FOUND, zero wasted
opens under the T214 rule. **Mali/Lima behaves like llvmpipe on the composite
draw path** — same builds, same buffer counts, same texture counts, no
driver-specific failure.

One honest difference from T287: on x86 composites ON mapped **3.2 MB LESS**
than OFF, because the control leg there opened `beholder_chr`. On the Pi the
control never opens `beholder_chr` (the `orangepi` preset's tighter
`entityLoadRadius`/`entityRenderDistance` populate fewer races), so composites
ON maps **+780 KB more**, not less. The x86 saving was a property of that
tour's entity mix, not a general result. Either way the difference is under a
megabyte of *mapped* bytes and is not the interesting number.

### Answer 1 — does the address-space argument still hold? **No.**

T195's finding was that mapping the whole composite directory pushed VmSize to
3,128 MB against the 32-bit user ceiling of ~3 GB, and that the throw was a
mapping with nowhere to go. With T284's index-driven late-open, composites ON
peaks at **575.0 MB — 18.4 % of the ceiling, and only 2.7 MB above the
composites-OFF peak of 571.4 MB.** There are ~2.4 GB of unused address space
left. The premise decision #3 rests on is measured, on the real board, to be
false today.

### Answer 2 — does RSS give a NEW argument against composites? **No, but it is the number to watch.**

RSS is where the cost actually landed: **+13.4 MB mean peak RSS** (217.7/208.9
→ 230.3/222.6 MB), i.e. the decoded-and-uploaded sheets that mapping never
charged for. On a 483 MB board with ~365 MB MemAvailable at rest that is about
3.7 % of the machine, and MemAvailable at the worst sample was 54.9 MB (ON)
against 64.5 MB (OFF) — thinner, but never near exhaustion, and no OOM kill or
allocation failure occurred in either rep.

The honest caveat is that this is **one zone with seven composite races and
two composite builds**. RSS scales with the number of *distinct* composite
races resident, not with the directory size — the thing that used to scale
with directory size was address space, and that link is severed. A zone with
substantially more composite-eligible races would cost proportionally more
RSS, and 13 MB is not a budget that survives being multiplied by five. That is
a reason to re-measure a denser zone before trusting composites everywhere on
the Pi, not a reason to keep them off in befallen-class zones.

### CmaFree: a pre-existing condition, not a composite one

P02 established that the EQE1/ETC1 path leaks CMA kernel-side. It is still
true and it is **not composite-related**: `CmaFree` reached **0 kB during
every one of the four runs, control legs included**, and every run completed
anyway. The board was found at **CmaFree 11,184 kB after 4 days of uptime**;
`drop_caches` recovered it to 69 MB (P02 believed only a reboot recovered CMA
— on this kernel `drop_caches` does, which is worth knowing). It was left at
63,872 kB. No reboot was performed. No panic: the VM-side kmsg follower's last
entry is dated 2026-08-03 (the follower is not currently running), and the log
contains zero panic/oops/BUG lines.

### Recommendation (numbers only — no preset change made)

Re-enabling `enableCompositeEntities` on the `orangepi` preset is now
supported by the evidence: the failure mode that caused it to be turned off
cannot recur (the eager scan is deleted, and the peak is 2.4 GB clear of the
ceiling), the mechanism reproduces on ARM exactly as on x86, and the cost is
~13 MB RSS rather than an unbounded mapping. `src/client/graphics/CLAUDE.md:127`
and `docs/decisions.md` (composites OFF on orangepi, memory pressure) both
carry the superseded rationale and would need updating with it. **Will decides;
this unit measured only.** If he wants more confidence first, the cheap next
measurement is the same A/B on a race-dense zone (qeynos2/gfaydark) to see how
RSS scales with composite race count.

### Deviations / honesty notes

- **Five Pi client invocations, not four.** The first failed instantly
  (`Required files missing: config/constrained_presets.json …`) because the
  driver did not `cd /tmp`; fixed and re-run. Well inside the 12/session cap.
- The Pi's stale `/tmp/config` and `/tmp/data` were replaced (see above) and
  then removed at cleanup. Pre-existing T186/T193/T195/T206 leftovers in Pi
  `/tmp` were left untouched — they are not this unit's to delete.
- Two reps per leg, not more. Enough to show the zone-in difference is noise;
  not enough for a tight interval on it. Peak RSS/VmPeak are stable across
  reps (spread 4 % and 0.3 % respectively), so the memory conclusions do not
  depend on rep count.
- No screenshots. The plan's scope is memory numbers; nothing visual is
  claimed, and no frame from these runs has been judged.
- eqemu: lock taken atomically as `T289`, `up.sh` / `down.sh`, clean shutdown,
  lock deleted. One DB write, reverting one this unit caused — `/quit` saved
  Testchar's position rounded to `-77.875 / 21.875`; restored to
  `-77.9232 / 21.9086 / z=3 / h=190 / zone_id=36`.
  `diff db_before.txt db_final.txt` is **EMPTY**.
- Verification bar narrowed by the plan ("No source change, so no ctest/gate
  needed"). ARM build ran and passed (it had to, to stage a binary). No
  goldens touched.
