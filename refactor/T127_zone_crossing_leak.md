# T127 — Memory growth across repeated zone crossings

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: zone unload/load teardown paths, the persistent caches,
memory diagnostics (`renderer_diagnostics.cpp`), `tests/`, `refactor/`,
`/mnt/projects/fable/review/T127/`.**

Read T125's Review first (Tour B's OOM kills), then T106's (the untracked-
memory attribution work) and T108's (mapped-vs-resident).

## It already reproduces on x86 — you do not need the Pi

T125 could not complete a Pi zone-crossing tour because **every attempt was
killed by the kernel OOM killer**, confirmed from `dmesg`. That was read as a
Pi resource limit. **It is not Pi-specific.** The same growth is plainly
visible in T120's existing x86 logs
(`review/T120/baseline_x86/run1.log`, 61 `Memory: RSS` samples):

```
RSS first sample : 549.6 MB
RSS last sample  : 856.5 MB
growth           : +306.9 MB (1.6x) over one crossing tour
```

and it is **stepwise at each zone transition**, not gradual:
`549.8 → 551.2 → 575.6 → 724.1 → … → 856.5`. Roughly **75–150 MB per
crossing**, which on a 512 MB machine is fatal after one or two — exactly what
T125 observed.

**So this is an x86-reproducible bug, cheap to iterate on.** Start from
T120's existing tour and runner; a Pi run adds nothing this unit needs.

## The decisive clue: the tracked categories do NOT move

In the same log, across a crossing that added ~175 MB of RSS, the tracked
accounting is **flat**:

```
before: [Zone Geometry] 1.2 MB (1898 regions, 32491V/49989I)   Geometries: 1915
after : [Zone Geometry] 1.2 MB (1898 regions, 32491V/49989I)   Geometries: 1915
```

**RSS grows by hundreds of megabytes while every tracked category stays
identical.** Whatever is growing is *not* in the memory accounting at all —
which is why the memory gate and the `/pmem` breakdown have never shown this.
This is the same blind spot T106 investigated for untracked memory; re-read it
before assuming the accounting is trustworthy here.

## Establish the shape before hunting

**Leak (unreachable) and unbounded retention (reachable but never released)
both cause this OOM, and they need different fixes.** Distinguish them
explicitly and early:

1. **LeakSanitizer first — cheapest.** An ASAN build with `detect_leaks=1`
   over the crossing tour, exiting cleanly via `/camp`. If it names
   unreachable allocations, you have a true leak and probably the answer.
2. **If LSAN is quiet, it is retention, not a leak** — memory still reachable
   from a live cache or container. Then use an allocation profiler
   (`heaptrack`, or massif) to attribute growth by call site regardless of
   reachability. Say which you used.

Do not skip step 1 because you suspect a cache; a clean LSAN result is itself
strong evidence and takes minutes.

## Prime suspects — verify, do not assume

- **`EquipmentModelLoader::meshCache_`** — T126 established this is a
  **persistent cross-zone** cache. Persistent by design; the question is
  whether it is *bounded*. An unbounded cross-zone mesh cache is exactly this
  bug's shape.
- **`RaceModelLoader`'s model/animation caches** — same question.
- **`ConstrainedTextureCache`** — bounded by budget (`Evictions: 26` appears in
  this very log), so probably not the growth, but confirm rather than assume.
- **BSP/PVS data** — the log shows a `BSP tree: 2.6 MB … PVS 2.0 MB` line
  appearing after a crossing. Small, but check whether prior zones' trees are
  released.
- **Anything allocated per zone load and not freed by `unloadZone()`.** The
  stepwise-at-transition profile points squarely here.

## Fixing

Fix the narrowest thing that stops the growth. Likely shapes: bound a cache
with an eviction policy, or release a per-zone structure in `unloadZone()`.

**STOP and report** if the fix requires changing texture or mesh ownership,
the zone load/unload lifecycle, or cache architecture beyond adding a bound.
Those are their own units.

**If the growth is a deliberate, load-bearing cache** (e.g. cross-zone
equipment meshes exist to avoid re-extraction cost), **do not silently delete
it.** Say what it buys, what bounding it would cost, and let that be a
decision rather than a side effect. Report it and stop if the tradeoff is not
obvious.

**Do not paper over**: no periodic "clear everything" hack, no lowering a
budget to mask growth.

## Verification — the number is RSS growth per crossing

- **Primary: RSS growth across N crossings on x86**, before and after, from
  the same `/pmem` sampling. State MB per crossing and the total over the
  tour, against the current **+306.9 MB / 1.6x** baseline. A flat or
  near-flat profile is the target.
- **If you fix it, also fix the accounting**: whatever was growing untracked
  should become visible in `/pmem`, so the next person sees it. That is part
  of the fix, not a nicety.
- x86 suite (current total **2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **A test is required** if you bound a cache — assert the bound holds across
  simulated repeated loads.
- **Visual gate goldens must not move.** Releasing something still needed
  would show up here; if a golden moves, **STOP and report**.
- **No Pi run.** A Pi confirmation (does Tour B now complete?) is the natural
  follow-up unit and closes T125's open leg.

## Review

(executor fills: LSAN result; the profiler used and its attribution; leak vs
retention, decided with evidence; the growing allocation named; the fix and
why it is narrow; **RSS growth per crossing before and after**; whether the
accounting was updated to show it; deviations)

## Board

State the before/after RSS-per-crossing figures. If the growth is closed, say
that **T125's Tour B and the Pi OOM should be re-run** — that is the
confirmation, and it also closes the leg T125 could not complete. If the
growth is a deliberate cache, say so and put the bounding decision on the
board rather than taking it unilaterally.
