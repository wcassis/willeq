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
