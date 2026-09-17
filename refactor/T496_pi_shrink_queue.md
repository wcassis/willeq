# T496 — the Pi's own shrink queue, then the largest category at the root

Class A/P. **Will's standing decision, 2026-09-17:** "the goal is willeq's RSS
footprint between 32 and 64 MB" (256 MB is an interim ceiling). T491 gave 100%
attribution **on x86** and printed the shrink queue; T490 proved **on the Pi** that
the gap is the pre-zone baseline. This unit makes the queue a **Pi** measurement
and then attacks its head. Leg A needs the **LOCAL eqemu lane + the Pi**; Leg B is
offline. Board entries start at **B816**. Requires fable to carry T490+T491+T495
(the chain) — rebase onto that tip first.

## What exists (read first)

- `include/client/core/memory_ledger.h` / `src/client/core/memory_ledger.cpp`:
  18 categories, `MemoryScope`, `snapshot/recordPoint/toLogLine`, `gapToTarget()`,
  the smaps census (`readMappings`/`mappingsToLogLines`, owner rows
  `[heap]`/`[stack]`/`[anon]`/`so:`/`file:`). [measured] Emission points wired:
  `application.cpp:805,811` (`startup` + `[memmap]`), `bridge_component.cpp:905-911`
  (`login_world`, `ready` + `[memmap]`), seven zone-load points,
  `player_component.cpp:118` (`camp`), `steady_60s`, and `/pmem`
  (`commands_component.cpp:129`, `irrlicht_bridge.cpp:1094`). **Nothing new has to
  be written to take the Pi measurement.**
- `EQT_MEMORY_LEDGER` ON by default (`CMakeLists.txt:29`); `scripts/build-arm-noble.sh`
  builds `willeq` and `gles2_zone_benchmark` for armhf. [measured]
- Pi lane: `EQEMU_HOST=local` + `scripts/eqemu/env.sh`; Pi config host 10.0.30.23
  (local copy `review/T490/pi/willeq_pi_local.json`), Pi 10.0.30.192 armv7l 483 MB;
  `review/T490/scripts/pi_sweep.sh` (settle → update → READ BACK `character_data`,
  refuse on mismatch — B802) driving `review/T490/scripts/pi_zone_run.sh` (0.5 s
  RssAnon/VmSwap sampler, watchdog, dmesg OOM capture); deploy shape in
  `review/AUDIT-0914/scripts/run_pi.sh`. [measured]
- Starting numbers: Pi nro `00_start` 192,284 kB anon → `13_Cleanup` 190,548 kB
  (a loaded zone costs ~0 anon; the ~126 MB gap to 64 MB is the pre-zone baseline,
  B801) [measured]; x86 queue `entity_models` 64.56 / `startup` 49.73 /
  `textures_driver` 30.50 / `objects` 28.91 / `irrlicht_scene` 27.95 / `zone_mesh`
  20.26 / `malloc_slack` 17.43 / `textures_decoded` 13.04 MiB [measured, x86];
  every driver-coupled row is llvmpipe and [assumed] for Mali (B812). x86 startup
  heaptrack heads: `createSoftwareCursor()` 8.70 MiB, `initLoadingScreen()` 7.47,
  `ParticleManager::init()` 4.19, `decodeBMP()` 4.19 — ~9.5 MiB of the 88.85 MiB x86
  startup peak is in willeq frames; the x86 trace does NOT explain the Pi's 190 MB.

## Tasks

### Leg A — settle B812: the Pi's shrink queue (lock; FIRST; no source changes)
1. Cross-build at the tip (`scripts/build-arm-noble.sh`), deploy per `run_pi.sh`.
   A measurement leg that edits the thing it measures is not a measurement.
2. One `willeq` run per gate zone (qeynos2, gfaydark, befallen) + iceclad,
   `--drm --constrained orangepi -r 1280 720 --load-mem-audit`, through
   `pi_sweep.sh`/`pi_zone_run.sh` (B802 read-back guard, watchdog, `=== Entering
   <zone> ===` re-check). **Every row names the zone actually entered.** iceclad is
   expected to OOM at step 4 (B800); its `startup`/`ready` lines are still the point.
3. Per run: `[memledger]` at `startup`, `login_world`, `ready`, `steady_60s`;
   `[memmap]` at `startup` and `ready` (top 16 owners); one `/pmem` at `ready`;
   one `gles2_zone_benchmark --zone <Z> --memory-json` per zone as the headless
   cross-check.
4. Output `review/T496/pi/table_categories.tsv`: per zone, per bucket live and
   peak, `unattributed`, `heap_uncharged`, `malloc_slack`, `unaccounted`, non-heap
   anon by owner, `over_target` vs 64 MiB, **sorted largest-first** (`gapToTarget()`
   order), plus the same for `startup` alone. Every cell [measured]; nothing from
   x86 in it.
5. Board **B816** with the table; **strike B812** with the measured
   `startup`/`textures_driver`/`irrlicht_scene`/non-heap numbers; Mali budgets for
   those three in the orangepi preset ONLY if stable across the four runs, else say
   why not. Close B812's second half: hook cost on ARM from the benchmark's
   `frame_allocs` × the per-pair delta.

### Leg B — the head of the Pi queue, at the root (offline, red first)
Target = whatever Leg A names first. Deliverable: a headless red test asserting
that category's bytes under a stated bound, failing at this tip on x86, with the Pi
run as confirmation.
- **If `entity_models`** (B814): `loadedModels_` is shared per race [measured] but
  `variantModels_` (race,gender,variant) may retain a full geometry+skeleton copy
  per texture variant [assumed — header comment says so; the byte split does not
  exist]. Third term: `otherChrCaches_`/`characterModelBytes`. Red: split
  `MemoryStats` per variant; assert `bytes(N variants) − bytes(1) ≤ B` (texture
  bindings only). Root fix: share geometry/skeleton across variants. No pixel changes.
- **If `startup`** (likelier — the Pi baseline is 190 MB): candidates confirmable
  from Leg A's `[memmap]` + `startup` line without new code: `createSoftwareCursor()`
  8.70 MiB and `initLoadingScreen()` 7.47 (one-shot decode staging retained past
  upload?), `ParticleManager::init()` 4.19, `decodeBMP()`/`processTextureData()`
  staging, the orangepi texture cache (44,116 kB by the Pi's own audit line
  [measured]), the Irrlicht device/DRM surface (shows as `so:`/`[anon]`, not in the
  ledger). Red: after startup, `startup` live ≤ bound, or a named staging buffer is
  zero once resident. Root fix: free staging after upload / bound the atlas — never
  "shrink the image".
- Green on the same red; re-measure that category in one confirming Pi run with the
  Leg A recipe verbatim; move the x86 gate's per-category budget down (T293 headroom).

### Leg C — record
`docs/decisions.md` (the Pi queue replaces the x86 queue as the planning list);
BOARD B816 (queue / B812 struck), **B817** (root cause + its red), **B818** deferred
items; `src/client/graphics/CLAUDE.md` + `docs/module_map.md` if Leg B touches them.

## Proof
`cmake --build` exit 0; full ctest green incl. the new test; `build-gles2-x86.sh`;
`build-arm-noble.sh`; `visual_gate.py` PASS with goldens untouched;
`check_file_sizes.sh`. Plus `review/T496/pi/table_categories.tsv` with every row's
actual zone, and a before/after Pi number for the category attacked.

## STOP (report, do not proceed)
1. Anything needing a golden re-bless (adding iceclad/Velious to the gate = Will's
   call, open half of B813).
2. Anything that changes what is drawn (texture size, variants, cursor/loading-screen
   resolution, particle sets). Only lifetime/sharing/staging changes are in scope.
3. Leg A's four runs disagree by > ~10% on `startup` live bytes.
4. The head of the Pi queue is NON-HEAP (Mali/DRM mappings): report the `[memmap]`
   owner rows and stop — a driver/surface decision.

## Rules
- LOCAL lane only; lock `eqemu_lock_take T496`; release on every path; down.sh.
  Never the VM, never hermes-ext. No Testchar. Private scratch `scratchpad/T496/`.
- No ccache; -j8; `review/` beside the checkout; visual_gate flaky under load —
  rerun alone on a quiet host.
- Territory: `review/T496/**`, `refactor/T496_*.md`, BOARD, decisions.md; Leg B:
  ONE of `race_model_loader.{cpp,h}` or `renderer_init.cpp`/`irrlicht_renderer.cpp`
  + its test + `config/constrained_presets.json` if a budget moves. NOT the gate's
  zone set, NOT goldens, NOT `zone_bsp*`/PVS (T490), NOT the ledger's categories.

## Review
(executor)
