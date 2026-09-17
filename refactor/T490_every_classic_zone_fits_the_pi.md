# T490 — every original / Kunark / Velious zone loads inside the Pi ceiling (Iceclad OOM)

Class A. **Will's ruling, 2026-09-14:** "zoning into any of the original,
Kunark, or Velious zones (including iceclad) should not cause OOM. This game
was playable with these assets on a Pentium 60 class computer with a 3dfx
Voodoo 1/TNT level video card and 32 MB system RAM." The RSS ceiling is the
settled 256 MB (`docs/decisions.md`, T293 envelope). Leg A offline (no lock),
slot **t1**; Leg B Pi + lock.

## The measurement (AUDIT-0914)

`review/AUDIT-0914/pi/raw/R4L_tip_1280_long/willeq.log` + the Pi's dmesg: the
tip client (0daacef4, Will's flags, no `--eqe1-path`) loading **iceclad** was
OOM-killed at `loadZoneSequential: [55%] Computing spatial data...` after
`seq_s3d_parse took 16210 ms`; `Out of memory: Killed process (willeq)
total-vm:833948kB, anon-rss:318500kB`. The pre-T460 binary died the same way
(233 MB anon-rss). Iceclad's assets on disk total ~13 MB (`iceclad.s3d`
7.6 MB, `iceclad_chr.s3d` 3.7 MB, obj 1.8 MB). On x86 the same zone loads:
`LoadZoneSources: Loaded zone 'iceclad' in 43606ms (37609 BSP regions, 75217
nodes)` (`review/AUDIT-0914/x86/raw/R5_x86_tip_1280_video/willeq.log`, the
first, wasted R5). nro is 820 regions. **The lead: something scales with
regions² or regions × nodes** — PVS/visibility tables, per-region bounding
boxes + meshes, the region→mesh split, the "spatial data" step itself.

## Tasks

### Leg A — find and fix the allocation (x86, offline)
1. Measure, don't reason: `heaptrack` (installed on the dev VM) over
   `build/bin/gles2_zone_benchmark --zone iceclad` (or the headless loader
   path that runs the same `loadZoneSequential` steps — confirm which binary
   executes the "Computing spatial data" step; the benchmark renders the gate
   zones, so it should). Report the top allocation sites by peak bytes at the
   55% mark and at zone-ready, and the same for nro and one Kunark zone
   (e.g. firiona) and one more Velious zone (e.g. kael/wakening) for the
   scaling law. Put the tables in `review/T490/`.
2. Fix the dominant term at the root. Candidates to confirm or refute:
   dense per-region PVS bitsets/arrays sized regions²; a std::vector of
   bounding boxes or meshes per BSP leaf including empty leaves; the
   per-region mesh split duplicating vertex data per region; the WLD
   post-load data kept alive through the spatial step ("released WLD
   post-load data" only logs at 99%); `std::map`/`unordered_map` per region
   with heavy nodes. The official client did this in 32 MB total; a 13 MB
   zone must not need 300 MB of working set. Keep load time secondary to
   memory (Will's standing rule) but report both.
3. Pin it: a headless test that loads iceclad's BSP/spatial data and asserts
   peak allocation (via mallinfo2 / a counting allocator seam) under a stated
   bound derived from the ceiling, and the same for the three gate zones so
   the bound is not iceclad-specific. Extend the memory gate
   (`scripts/ci/visual_gate.py`, `config/constrained_presets.json` budgets)
   with the new category if it is not tallied — "untracked RSS" is where
   this hid (decisions.md: 76–90% of RSS is untracked).

### Leg B — prove it on the Pi (lock)
4. Under `eqemu_lock_take T490`, on the Pi with the AUDIT-0914 driver
   (`review/AUDIT-0914/scripts/run_pi.sh`, Will's flags at 1280x720): log
   Testcaster into iceclad (set `character_data` zone_id 110 at its safe
   point, restore to nro afterwards: `zone_id=34, x=-41.87, y=2723.33,
   z=-24.84`), before and after. Record `/pmem` and `meminfo` at zone-ready,
   and dmesg. Then a sweep: every classic/Kunark/Velious zone the launcher
   fleet runs (`scripts/eqemu/status.sh` lists the static zones; add the
   biggest Velious ones with `#zone`), zone-in only, watchdog on the client
   pid, 5 min stall deadline, heaviest first (`feedback-watch-long-live-runs`).
   Table: zone, regions, peak RSS, pass/fail against 256 MB.

## Proof

Standard verification bar; goldens for touched TUs in place; ARM; file sizes;
memory gate green with the new category or a written reason. Board: new
B item for the Iceclad OOM struck by this unit; B751 (dynamic-zone login
stall at 40%) is adjacent — if the sweep reproduces it, record which zones
and hand it on, do not fix it here.

File territory: `src/client/zone_bsp*.cpp`, `include/client/zone_bsp.h`, the
zone loader's spatial/PVS steps (`renderer_zone_load*.cpp` Step 2/BSP,
`zone_load/`), the WLD BSP fragment parser, the memory gate scripts and
presets, new tests, `review/T490/`. **Not** the camera, held items (T485),
T486's files, the texture cache eviction policy (T487/T463), the loaders'
object placement (T479 branch).

STOP: the dominant term is GPU-side (Mali driver allocations the client
cannot see) — report with the heaptrack table; or the fix needs a zone data
format change (report).

---

# Review (T490, 2026-09-17)

Rebased onto fable `c637518a` first — **no conflicts**, the 34-file inherited
WIP applied clean. Every claim below is `[measured]` unless marked
`[assumed]`.

## 1. What did not work

- **The inherited WIP did not build.** Three link failures the dead executor
  never reached: `equipment_index_builder`, `test_graphics` and
  `test_asset_loading` compile the WLD parser but not `src/client/zone_bsp.cpp`,
  and the WIP moved `BspRegion`/`BspTree`'s PVS methods out of the header into
  that TU. Plus `tests/test_sim_pvs_candidates.cpp` used `BspTree` as a complete
  type while `simulation_worker.h` only forward-declares it. Fixed here.
- **The inherited test failed on the zone it was written for.** `test_zone_bsp_pvs`
  `IcecladFitsTheBudget` was RED: 26,941,400 B tallied against a 24 MiB bound.
  The bound was the problem, not the fix — see §4.
- **The PVS fix alone does NOT make Iceclad load on the Pi.** It moves the kill
  one step later. See §5; this is the unit's main negative result.
- **My first Pi sweep wasted four runs to a rig trap** (the server re-saves
  `character_data` on the PREVIOUS client's linkdead, clobbering the zone I
  wrote). Now a board item — see §7.

## 2. The allocation finding

**`BspRegion::visibleRegions` was a dense `std::vector<bool>` sized to the
zone's region count, decoded for EVERY region at WLD parse time: regions²/8
bytes per tree.** For iceclad's 37,609 regions that is 176,804,610 B. The WIP
keeps the WLD's own RLE byte string (fragment 0x22) in one arena on the tree
and decodes a bitset on demand through a 16-slot cache. `[measured]`

A **second** term of the same shape: `SimBridgeComponent::buildSimulationZoneData()`
pre-built `pvsVisibleBoundsIndices`, one candidate list per camera region —
`sum(visible-per-region)` `size_t`s across the zone. For iceclad that is
31,923,639 pairs = **257,796,088 B**, for warslikswood 224,283,968 B. It is now
built for ONE camera region, on demand. `[measured]`

Combined pre-T490 PVS cost, worst zones (`review/T490/x86/pvs_census.tsv`,
all 118 original/Kunark/Velious archives):

| zone | regions | dense PVS | sim table | total | T490 RLE arena |
|---|---:|---:|---:|---:|---:|
| iceclad | 37,609 | 168.6 MiB | 245.9 MiB | **414.5 MiB** | 4.61 MiB |
| cabeast | 9,172 | 10.0 MiB | 279.5 MiB | 289.5 MiB | — |
| warslikswood | 14,832 | 26.2 MiB | 213.9 MiB | 240.1 MiB | 2.35 MiB |
| timorous | 19,321 | 44.5 MiB | 138.0 MiB | 182.5 MiB | 2.03 MiB |
| gfaydark (gate) | 2,905 | 1.0 MiB | 22.9 MiB | 23.9 MiB | 0.20 MiB |

## 3. Red proof

`review/T490/x86/redproof/` — ONE hunk of `parseFragment22()` put back to
`setPvs(decodeRlePvs(...))`, which is the pre-T490 dense form exactly. Built
outside the build tree so the running build was untouched.

| | GREEN (tip) | RED |
|---|---|---|
| suite | **16/16 pass** | **5 FAILED** (every real-zone case) |
| iceclad PVS storage | 4,832,309 B | 178,417,096 B |
| iceclad heap retained by the parse | 28,869,232 B | 205,197,400 B |
| iceclad parse peak | 65,456,736 B | 232,714,944 B |

The 176.3 MB retained difference is regions²/8 and nothing else. `[measured]`

## 4. The test's bound

The inherited constant was `orangepi.s3dZoneSourceMemoryBytes` (24 MiB, T293's
gfaydark-derived figure). **iceclad's zone source measures 24.55 MiB, so that
preset does not describe the largest zone in the game** — it is exceeded by
572,088 B. The visual gate applies the preset to the three GOLDEN zones only
(qeynos2 2.68 MiB, gfaydark 5.83 MiB, nro 1.46 MiB), so nothing is red today.
The test's bound is now 32 MiB, derived from the measured maxima over all 118
zones (retained max 27.53 MiB, parse peak max 62.42 MiB), and `timorous` was
added as a second top-end zone so the bound is not iceclad-specific.

**For T491:** the `orangepi` `s3dZoneSourceMemoryBytes` preset should move to
32 MiB, or the ledger must record that the gate's three zones are not the
worst case. I did not touch `config/constrained_presets.json` or
`scripts/ci/visual_gate.py`.

## 5. The Pi (Leg B, local eqemu lane, lock held as T490)

`review/T490/pi/raw/<run>/`, table `review/T490/pi/table_zones.tsv`. Every row
names the zone the client ACTUALLY entered, read from its own log.

The decisive A/B, same rig, same lane, same hour:

| binary | last `[LOAD-MEM-AUDIT]` step reached | peak RssAnon | outcome |
|---|---|---:|---|
| fable tip (pre-T490) | `01_S3D` | 326,724 kB | OOM **inside** step 2 ("Computing spatial data") |
| T490 | `02_BSP` | 318,880 kB | OOM **after** step 2, in the mesh build |

**T490 moves the kill past the step that AUDIT-0914 recorded it in.** Step 2's
own cost is now +10 MB of anon (184,444 → 194,732 kB). Iceclad still does not
fit. **That the kill is now in step 4 is `[measured]`** — the run logs `02_BSP`,
never `04_Regions`, and there is no step 3. **Which allocation inside step 4 is
`[assumed]`**: on x86 heaptrack puts `uploadMeshHWBuffers` at 31.04 MiB for
iceclad against 2.10 MiB for wakening, a clean per-region term of the same shape
as B750/B752 — but that is llvmpipe, and on the Pi the buffer lives in the Mali
driver where the client cannot see it. Boarded as **B800**.

## 6. Will's 32–64 MB target (addendum, 2026-09-17)

Per-zone peak RSS / RssAnon / gap to 64 MB is in
`review/T490/pi/table_zones.tsv`. The finding that matters for planning:

**For a zone that loads, the zone costs approximately nothing in anon.** nro on
the Pi, full ladder, `00_start` → `13_Cleanup`: anon **192,284 kB → 190,548 kB**
— a net *decrease*. The client is already 190 MB of anonymous memory before it
touches a zone, so the gap to 64 MB (≈126 MB) is **entirely the pre-zone
baseline**, not zone data. `[measured]` Chasing zone-load terms cannot reach
64 MB; the startup baseline is where the next units must go. qeynos2 says the
same on the Pi: `00_start` 172,512 kB anon → `13_Cleanup` 157,516 kB.

What the baseline is made of is **not answered here**. heaptracking willeq
itself from process start to `=== Entering <zone> ===` on x86 gives a peak heap
of 88.85 MiB of which only ~9.5 MiB sits in willeq frames (largest:
`createSoftwareCursor()` 8.70 MiB, `initLoadingScreen()` 7.47 MiB) — so the
x86 trace does **not** account for the Pi's 190 MB, and the client's own audit
line agrees that it cannot: `accounted_kb=43`, `untracked_kb=228,621`. That is
B801, and T491's ledger is what has to answer it.

The top zone-load sites that remain, for the record `[measured]`, iceclad on x86:
`uploadMeshHWBuffers` 31.04 MiB, Irrlicht `S3DVertex` arrays 8.92 MiB,
`parseFragment36` (mesh fragments) 13.2 MiB total, `buildTexturedMesh`
5.01 MiB, the T490 PVS arena 4.83 MiB. Tables:
`review/T490/x86/heaptrack_peak_sites.md`, traces in
`review/T490/x86/heaptrack/`.

## 7. Adjacent, not fixed here

- **B751 reproduced** on this lane: warslikswood and stonebrunt never reached
  the zone at all (no `[LOAD-MEM-AUDIT]` line, 150 s and 300 s deadlines both),
  while the server had booted both zone processes. Handed on, not fixed, as the
  plan directs.
- The eager per-region mesh build is B752's lever question in a new zone.

## 8. Inherited WIP: kept / changed

Kept, after the red proof: the RLE PVS store and every call-site migration, the
on-demand sim candidate list, the RssAnon/RssFile reporting in
`load_mem_audit`, both new tests, the golden edit. Changed by me: the three
cmake targets, the missing include, the test's bound, `timorous` added, and one
real regression the WIP introduced — `renderer_frame_render.cpp` called
`->pvs()` on `regions[i]` with no null check where the old code held the
region as a `shared_ptr` and tested it. Guard moved into the condition;
golden entry 132 re-taught in place, count unchanged at 328.
